#include "EngineAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Engine/Message.h>
#include <MicroWorld/Engine/MessageRouter.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::ActorMessageHeaderBytes;
using MicroWorld::BroadcastActorId;
using MicroWorld::EMessageResult;
using MicroWorld::EncodeActorMessage;
using MicroWorld::FActorMessageHeader;
using MicroWorld::FMessageActorId;
using MicroWorld::FMessageChannelId;
using MicroWorld::FMessageHandlerBinding;
using MicroWorld::FMessageHandlerHandle;
using MicroWorld::FMessageTypeId;
using MicroWorld::FMessageView;
using MicroWorld::IMessageChannel;
using MicroWorld::LocalChannelId;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TMessageRouter;
using MicroWorld::TSpan;
using MicroWorld::Tests::GlobalAllocationCount;

/** Asserts a router operation returned Success without discarding the result. */
#define MW_EXPECT_SUCCESS(TestContext, Result, Message) MW_EXPECT_EQ(TestContext, EMessageResult::Success, Result, Message)

/** Capacities shared by every router test; each test constructs its own fresh router instance. */
constexpr std::size_t HandlerCapacity = 3;
constexpr std::size_t QueueCapacity = 2;
constexpr std::size_t MessageByteCapacity = 32;
constexpr std::size_t ChannelCapacity = 2;
using FTestRouter = TMessageRouter<HandlerCapacity, QueueCapacity, MessageByteCapacity, ChannelCapacity>;

/** Message type and actor ids shared across cases; values are arbitrary and only need to be distinct. */
constexpr FMessageTypeId TypeAlpha = 1;
constexpr FMessageTypeId TypeBeta = 2;
constexpr FMessageActorId ListenerA = 10;
constexpr FMessageActorId ListenerB = 20;
constexpr FMessageActorId SenderId = 99;
constexpr FMessageChannelId StubChannelId = 1;

/** A valid-looking handle value used to prove failed registrations clear their output. */
constexpr FMessageHandlerHandle CanaryHandle{0u, 1u};

/** Records each delivered message's caller-supplied identity and header fields in invocation order. */
struct FHandlerCallRecord final
{
	/** Bounds the recorded sequence so test fixtures stay allocation-free and fixed-size. */
	static constexpr std::size_t MaxEntries = 8;

	/** Tracks the next write position so later reads observe invocation-order dispatch. */
	std::size_t Count{0};

	/** Stores the caller-supplied identity of each invoked handler. */
	int Identities[MaxEntries]{0};

	/** Stores the TargetActorId each invocation observed. */
	FMessageActorId TargetActorIds[MaxEntries]{0};

	/** Stores the SenderActorId each invocation observed. */
	FMessageActorId SenderActorIds[MaxEntries]{0};

	/** Records one observed invocation and the view it was invoked with. */
	void Record(const int InIdentity, const FMessageView& InView) noexcept
	{
		if (Count >= MaxEntries)
		{
			return;
		}
		Identities[Count] = InIdentity;
		TargetActorIds[Count] = InView.Header.TargetActorId;
		SenderActorIds[Count] = InView.Header.SenderActorId;
		++Count;
	}
};

/** Binds a nothrow inline handler that records its identity and the delivered view in the shared recorder. */
FMessageHandlerBinding MakeRecordingHandler(FHandlerCallRecord& InRecorder, const int InIdentity) noexcept
{
	FMessageHandlerBinding Delegate;
	(void)Delegate.Bind([&InRecorder, InIdentity](const FMessageView& InView) noexcept { InRecorder.Record(InIdentity, InView); });
	return Delegate;
}

/** A minimal IMessageChannel test double that returns a scripted result sequence and records what it received. */
class FStubChannel final : public IMessageChannel
{
public:
	/** Bounds the scripted-result queue and the receive log without allocating. */
	static constexpr std::size_t MaxScriptedResults = 4;

	/** Bounds how many accepted sends this stub retains for inspection. */
	static constexpr std::size_t MaxReceivedMessages = 4;

	/** Binds this stub to one channel id and one MaxEncodedMessageBytes budget. */
	FStubChannel(const FMessageChannelId InChannelId, const std::size_t InMaxEncodedMessageBytes) noexcept
		: ChannelId(InChannelId), MaxBytes(InMaxEncodedMessageBytes)
	{
	}

	/** Reports this stub's fixed channel id. */
	FMessageChannelId GetChannelId() const noexcept override { return ChannelId; }

	/** Reports this stub's fixed per-message byte budget. */
	std::size_t MaxEncodedMessageBytes() const noexcept override { return MaxBytes; }

	/** Appends one scripted result that a future call will return, in call order. */
	void ScriptResult(const EMessageResult InResult) noexcept
	{
		if (ScriptedResultCount < MaxScriptedResults)
		{
			ScriptedResults[ScriptedResultCount] = InResult;
			++ScriptedResultCount;
		}
	}

	/** Returns the next scripted result (Success once the script is exhausted) and records an accepted send's bytes. */
	EMessageResult TrySendEncodedMessage(const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		const EMessageResult Result = (ScriptedReadIndex < ScriptedResultCount) ? ScriptedResults[ScriptedReadIndex++] : EMessageResult::Success;
		++SendAttemptCount;
		if (Result == EMessageResult::Success && ReceivedMessageCount < MaxReceivedMessages)
		{
			ReceivedLengths[ReceivedMessageCount] = InEncoded.Size();
			for (std::size_t Index = 0; Index < InEncoded.Size() && Index < MaxMessageBytes; ++Index)
			{
				ReceivedBytes[ReceivedMessageCount][Index] = InEncoded.Data()[Index];
			}
			++ReceivedMessageCount;
		}
		return Result;
	}

	/** Reports how many TrySendEncodedMessage calls this stub has observed, succeeded or not. */
	std::size_t SendAttempts() const noexcept { return SendAttemptCount; }

	/** Reports how many sends this stub accepted (returned Success). */
	std::size_t ReceivedCount() const noexcept { return ReceivedMessageCount; }

	/** Reports the encoded length of the Index'th accepted send. */
	std::size_t ReceivedLength(const std::size_t InIndex) const noexcept { return ReceivedLengths[InIndex]; }

	/** Reports one byte of the Index'th accepted send's encoded bytes. */
	std::uint8_t ReceivedByte(const std::size_t InIndex, const std::size_t InByteOffset) const noexcept
	{
		return ReceivedBytes[InIndex][InByteOffset];
	}

private:
	/** Bounds the per-message receive log to the router's own byte budget used across these tests. */
	static constexpr std::size_t MaxMessageBytes = MessageByteCapacity;

	/** This stub's fixed channel id, returned by GetChannelId. */
	FMessageChannelId ChannelId;

	/** This stub's fixed per-message byte budget, returned by MaxEncodedMessageBytes. */
	std::size_t MaxBytes;

	/** Results returned in order by successive TrySendEncodedMessage calls. */
	EMessageResult ScriptedResults[MaxScriptedResults]{};

	/** Number of results appended by ScriptResult so far. */
	std::size_t ScriptedResultCount{0};

	/** Number of scripted results already consumed by TrySendEncodedMessage. */
	std::size_t ScriptedReadIndex{0};

	/** Total TrySendEncodedMessage calls observed, regardless of the returned result. */
	std::size_t SendAttemptCount{0};

	/** Total accepted sends recorded in ReceivedLengths and ReceivedBytes. */
	std::size_t ReceivedMessageCount{0};

	/** Encoded length of each accepted send, indexed by acceptance order. */
	std::size_t ReceivedLengths[MaxReceivedMessages]{};

	/** Encoded bytes of each accepted send, indexed by acceptance order. */
	std::uint8_t ReceivedBytes[MaxReceivedMessages][MessageByteCapacity]{};
};

/** Encodes one zero-payload message of MessageTypeId Type into a caller-owned fixed buffer for direct ReceiveEncodedMessage tests. */
template<std::size_t BufferBytes>
std::size_t EncodeZeroPayloadMessage(const FMessageTypeId InType, std::array<std::uint8_t, BufferBytes>& InBuffer) noexcept
{
	std::size_t WrittenBytes = 0;
	(void)EncodeActorMessage(
		FActorMessageHeader{InType, BroadcastActorId, SenderId},
		TSpan<const std::uint8_t>(nullptr, 0),
		TSpan<std::uint8_t>(InBuffer.data(), InBuffer.size()),
		WrittenBytes);
	return WrittenBytes;
}

// ---------------------------------------------------------------------------
// Category 1: Delivery matching and ordering
// ---------------------------------------------------------------------------

/** Proves a broadcast reaches every subscriber of its type in registration order. */
MW_TEST_CASE(EngineMessageRouter_BroadcastReachesAllSubscribersInRegistrationOrder)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle HandleA{};
	FMessageHandlerHandle HandleB{};
	FMessageHandlerHandle HandleC{};

	MW_EXPECT_SUCCESS(Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 1), HandleA), "A should register");
	MW_EXPECT_SUCCESS(Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 2), HandleB), "B should register");
	MW_EXPECT_SUCCESS(Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 3), HandleC), "C should register");

	const std::array<std::uint8_t, 1> Payload{0xAA};
	MW_EXPECT_SUCCESS(
		Test,
		Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"Broadcast enqueue should succeed");

	Router.TickFlush(1);
	Router.TickDispatch(1);

	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "All three broadcast subscribers should be invoked");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "The first-registered handler should fire first");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "The second-registered handler should fire second");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "The third-registered handler should fire third");
}

/** Proves a targeted message reaches only the handler whose ListenerActorId matches, not a same-type handler for another actor. */
MW_TEST_CASE(EngineMessageRouter_TargetedMessageReachesOnlyMatchingListener)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle HandleForA{};
	FMessageHandlerHandle HandleForB{};

	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, ListenerA, MakeRecordingHandler(Recorder, 1), HandleForA), "Listener A should register");
	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, ListenerB, MakeRecordingHandler(Recorder, 2), HandleForB), "Listener B should register");

	const std::array<std::uint8_t, 1> Payload{0x01};
	MW_EXPECT_SUCCESS(
		Test,
		Router.SendMessageToActor(LocalChannelId, TypeAlpha, ListenerA, SenderId, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"Targeted send should succeed");

	Router.TickFlush(1);
	Router.TickDispatch(1);

	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.Count, "Only the matching listener should be invoked");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "Listener A's handler must be the one invoked");
	MW_EXPECT_EQ(Test, ListenerA, Recorder.TargetActorIds[0], "The delivered view must carry the original TargetActorId");
}

// ---------------------------------------------------------------------------
// Category 2: One-frame local latency (D5)
// ---------------------------------------------------------------------------

/** Proves a local send is never delivered inline and only reaches its handler on the TickDispatch after a TickFlush. */
MW_TEST_CASE(EngineMessageRouter_LocalSendIsDeliveredOnlyAtNextTickDispatchNeverInline)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle Handle{};

	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 1), Handle), "Registration should succeed");

	const std::array<std::uint8_t, 1> Payload{0x02};
	const EMessageResult SendResult =
		Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, TSpan<const std::uint8_t>(Payload.data(), Payload.size()));
	MW_EXPECT_SUCCESS(Test, SendResult, "Broadcast enqueue should succeed");
	MW_EXPECT_EQ(Test, std::size_t{0}, Recorder.Count, "A queued send must never invoke a handler inline");

	Router.TickDispatch(1);
	MW_EXPECT_EQ(Test, std::size_t{0}, Recorder.Count, "TickDispatch before the message reaches inbound must not deliver it");

	Router.TickFlush(1);
	MW_EXPECT_EQ(Test, std::size_t{0}, Recorder.Count, "TickFlush by itself must not invoke any handler");
	MW_EXPECT_EQ(Test, std::size_t{1}, Router.QueuedInboundCount(), "TickFlush must move the local entry into the inbound queue");

	Router.TickDispatch(2);
	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.Count, "The next TickDispatch must deliver the flushed local message");
}

/** Proves a send issued from inside a handler is legal and its message arrives one full frame later, not in the same pass. */
MW_TEST_CASE(EngineMessageRouter_SendFromInsideHandlerArrivesOneFrameLater)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle EchoHandle{};
	FMessageHandlerHandle ObserverHandle{};

	FMessageHandlerBinding EchoingHandler;
	(void)EchoingHandler.Bind(
		[&Router](const FMessageView&) noexcept
		{
			const std::array<std::uint8_t, 1> EchoPayload{0x03};
			(void)Router.BroadcastMessage(LocalChannelId, TypeBeta, SenderId, TSpan<const std::uint8_t>(EchoPayload.data(), EchoPayload.size()));
		});

	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, std::move(EchoingHandler), EchoHandle), "Echo handler should register");
	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeBeta, BroadcastActorId, MakeRecordingHandler(Recorder, 1), ObserverHandle), "Observer should register");

	const std::array<std::uint8_t, 1> Payload{0x04};
	MW_EXPECT_SUCCESS(
		Test,
		Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"The triggering broadcast should enqueue");

	// Frame 1 (engine order: dispatch, then flush): inbound is still empty, so nothing fires yet;
	// flush moves the TypeAlpha entry into inbound.
	Router.TickDispatch(1);
	Router.TickFlush(1);
	MW_EXPECT_EQ(Test, std::size_t{0}, Recorder.Count, "The observer must not fire before its message is dispatched");

	// Frame 2: dispatch delivers TypeAlpha, whose handler enqueues the TypeBeta echo; that echo is
	// only outbound so far, so the observer still has not fired. Flush then moves it into inbound.
	Router.TickDispatch(2);
	MW_EXPECT_EQ(Test, std::size_t{0}, Recorder.Count, "The in-handler send must not be visible to the observer within the same dispatch pass");
	Router.TickFlush(2);

	// Frame 3: dispatch finally delivers the echoed TypeBeta message.
	Router.TickDispatch(3);
	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.Count, "The echoed message must arrive exactly one frame after the handler queued it");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "The observer's handler must be the one invoked");
}

// ---------------------------------------------------------------------------
// Category 3: Handler table mutation rules
// ---------------------------------------------------------------------------

/** Bundles one in-dispatch self-mutation attempt so its captures fit the handler's inline delegate budget. */
struct FSelfMutationAttempt final
{
	/** Handle of the handler performing the attempted mutation, set after its own registration. */
	FMessageHandlerHandle SelfHandle{};

	/** Output handle from the attempted in-dispatch AddMessageHandler call. */
	FMessageHandlerHandle RejectedAddHandle{CanaryHandle};

	/** Result of the attempted in-dispatch AddMessageHandler call. */
	EMessageResult AddResult{EMessageResult::Success};

	/** Result of the attempted in-dispatch RemoveMessageHandler call (removing SelfHandle). */
	EMessageResult RemoveResult{EMessageResult::Success};

	/** Records whether the attempt actually executed. */
	bool bObserved{false};
};

/** Proves AddMessageHandler and RemoveMessageHandler both return DispatchLocked when called from inside a handler. */
MW_TEST_CASE(EngineMessageRouter_AddOrRemoveDuringDispatchReturnsDispatchLocked)
{
	FTestRouter Router;
	FSelfMutationAttempt Attempt;

	FMessageHandlerBinding SelfMutatingHandler;
	(void)SelfMutatingHandler.Bind(
		[&Router, &Attempt](const FMessageView&) noexcept
		{
			FMessageHandlerBinding InnerHandler;
			(void)InnerHandler.Bind([](const FMessageView&) noexcept {});
			Attempt.AddResult = Router.AddMessageHandler(TypeBeta, BroadcastActorId, std::move(InnerHandler), Attempt.RejectedAddHandle);
			Attempt.RemoveResult = Router.RemoveMessageHandler(Attempt.SelfHandle);
			Attempt.bObserved = true;
		});

	MW_EXPECT_SUCCESS(
		Test,
		Router.AddMessageHandler(TypeAlpha, BroadcastActorId, std::move(SelfMutatingHandler), Attempt.SelfHandle),
		"The self-mutating handler should register");

	MW_EXPECT_SUCCESS(
		Test,
		Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, TSpan<const std::uint8_t>(nullptr, 0)),
		"Triggering broadcast should enqueue");
	Router.TickFlush(1);
	Router.TickDispatch(1);

	MW_EXPECT_TRUE(Test, Attempt.bObserved, "The self-mutating handler should have executed");
	MW_EXPECT_EQ(Test, EMessageResult::DispatchLocked, Attempt.AddResult, "In-dispatch AddMessageHandler must return DispatchLocked");
	MW_EXPECT_EQ(Test, EMessageResult::DispatchLocked, Attempt.RemoveResult, "In-dispatch RemoveMessageHandler must return DispatchLocked");
	MW_EXPECT_TRUE(Test, !Attempt.RejectedAddHandle.IsValid(), "The rejected in-dispatch add must clear its output handle");
	MW_EXPECT_EQ(Test, std::size_t{1}, Router.HandlerCount(), "Rejected in-dispatch mutations must not change handler occupancy");
}

/** Proves removing an already-removed handle returns StaleHandle. */
MW_TEST_CASE(EngineMessageRouter_RemovingAlreadyRemovedHandleReturnsStaleHandle)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle Handle{};

	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 1), Handle), "Registration should succeed");
	MW_EXPECT_SUCCESS(Test, Router.RemoveMessageHandler(Handle), "The first removal should succeed");

	const EMessageResult SecondRemoveResult = Router.RemoveMessageHandler(Handle);
	MW_EXPECT_EQ(Test, EMessageResult::StaleHandle, SecondRemoveResult, "A repeated removal must return StaleHandle");
}

/** Proves a full handler table rejects one more registration and leaves the caller's delegate and output handle untouched. */
MW_TEST_CASE(EngineMessageRouter_HandlerCapacityExceededPreservesCallerHandler)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle FirstHandle{};
	FMessageHandlerHandle SecondHandle{};
	FMessageHandlerHandle ThirdHandle{};

	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 1), FirstHandle), "First should register");
	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 2), SecondHandle), "Second should register");
	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 3), ThirdHandle), "Third should register");
	MW_EXPECT_EQ(Test, HandlerCapacity, Router.HandlerCount(), "Three registrations should occupy every slot");

	FMessageHandlerBinding FourthHandler = MakeRecordingHandler(Recorder, 4);
	FMessageHandlerHandle FourthHandle{CanaryHandle};
	const EMessageResult Result = Router.AddMessageHandler(TypeAlpha, BroadcastActorId, std::move(FourthHandler), FourthHandle);

	MW_EXPECT_EQ(Test, EMessageResult::CapacityExceeded, Result, "A full handler table must reject the fourth registration");
	MW_EXPECT_TRUE(Test, !FourthHandle.IsValid(), "The failed registration must clear the canary output handle");
	MW_EXPECT_TRUE(Test, FourthHandler.IsBound(), "The failed registration must leave its input delegate bound to the caller");
	MW_EXPECT_EQ(Test, HandlerCapacity, Router.HandlerCount(), "A failed registration must not change occupancy");
}

// ---------------------------------------------------------------------------
// Category 4: Queue capacity and transactional rejection
// ---------------------------------------------------------------------------

/** Proves a full outbound queue rejects one more send with CapacityExceeded and leaves the queue's occupancy unchanged. */
MW_TEST_CASE(EngineMessageRouter_OutboundQueueFullReturnsCapacityExceededTransactionally)
{
	FTestRouter Router;
	const std::array<std::uint8_t, 1> Payload{0x05};
	const TSpan<const std::uint8_t> PayloadView(Payload.data(), Payload.size());

	MW_EXPECT_SUCCESS(Test, Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, PayloadView), "First send should fill slot one");
	MW_EXPECT_SUCCESS(Test, Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, PayloadView), "Second send should fill slot two");
	MW_EXPECT_EQ(Test, QueueCapacity, Router.QueuedOutboundCount(), "Two sends should fill the two-entry outbound queue");

	const EMessageResult ThirdResult = Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, PayloadView);
	MW_EXPECT_EQ(Test, EMessageResult::CapacityExceeded, ThirdResult, "A full outbound queue must reject a further send");
	MW_EXPECT_EQ(Test, QueueCapacity, Router.QueuedOutboundCount(), "A rejected send must not change outbound occupancy");
}

/** Proves inbound overflow increments DroppedInboundCount and rejects the message without changing prior queue contents. */
MW_TEST_CASE(EngineMessageRouter_InboundOverflowIncrementsDroppedInboundCount)
{
	FTestRouter Router;
	std::array<std::uint8_t, MessageByteCapacity> Encoded{};
	const std::size_t EncodedLength = EncodeZeroPayloadMessage(TypeAlpha, Encoded);
	const TSpan<const std::uint8_t> EncodedView(Encoded.data(), EncodedLength);

	MW_EXPECT_SUCCESS(Test, Router.ReceiveEncodedMessage(StubChannelId, EncodedView), "First receive should fill slot one");
	MW_EXPECT_SUCCESS(Test, Router.ReceiveEncodedMessage(StubChannelId, EncodedView), "Second receive should fill slot two");
	MW_EXPECT_EQ(Test, QueueCapacity, Router.QueuedInboundCount(), "Two receives should fill the two-entry inbound queue");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Router.DroppedInboundCount(), "No message should be dropped yet");

	const EMessageResult ThirdResult = Router.ReceiveEncodedMessage(StubChannelId, EncodedView);
	MW_EXPECT_EQ(Test, EMessageResult::CapacityExceeded, ThirdResult, "A full inbound queue must reject a further receive");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Router.DroppedInboundCount(), "The rejected receive must increment DroppedInboundCount");
	MW_EXPECT_EQ(Test, QueueCapacity, Router.QueuedInboundCount(), "The rejected receive must not change inbound occupancy");
}

// ---------------------------------------------------------------------------
// Category 5: Channel registration and flush discipline
// ---------------------------------------------------------------------------

/** Proves AddChannel rejects LocalChannelId as InvalidChannel and rejects a duplicate id as Duplicate. */
MW_TEST_CASE(EngineMessageRouter_AddChannelRejectsLocalIdAndDuplicateIds)
{
	FTestRouter Router;
	FStubChannel LocalIdChannel(LocalChannelId, MessageByteCapacity);
	const EMessageResult LocalIdResult = Router.AddChannel(LocalIdChannel);
	MW_EXPECT_EQ(Test, EMessageResult::InvalidChannel, LocalIdResult, "Registering LocalChannelId (0) must be rejected as InvalidChannel");

	FStubChannel FirstChannel(StubChannelId, MessageByteCapacity);
	MW_EXPECT_SUCCESS(Test, Router.AddChannel(FirstChannel), "The first registration of a fresh id should succeed");

	FStubChannel DuplicateChannel(StubChannelId, MessageByteCapacity);
	const EMessageResult DuplicateResult = Router.AddChannel(DuplicateChannel);
	MW_EXPECT_EQ(Test, EMessageResult::Duplicate, DuplicateResult, "Registering an already-configured id must be rejected as Duplicate");
}

/** Proves TickFlush retains the head entry when a channel returns a non-Success result once, then resumes on the next flush. */
MW_TEST_CASE(EngineMessageRouter_FlushRetainsHeadOnChannelFailureAndResumesNextFlush)
{
	FTestRouter Router;
	FStubChannel Channel(StubChannelId, MessageByteCapacity);
	MW_EXPECT_SUCCESS(Test, Router.AddChannel(Channel), "Channel registration should succeed");
	Channel.ScriptResult(EMessageResult::Unavailable);

	const std::array<std::uint8_t, 1> FirstPayload{0x11};
	const std::array<std::uint8_t, 1> SecondPayload{0x22};
	MW_EXPECT_SUCCESS(
		Test,
		Router.SendMessageToActor(StubChannelId, TypeAlpha, BroadcastActorId, SenderId, TSpan<const std::uint8_t>(FirstPayload.data(), 1)),
		"First send should enqueue");
	MW_EXPECT_SUCCESS(
		Test,
		Router.SendMessageToActor(StubChannelId, TypeAlpha, BroadcastActorId, SenderId, TSpan<const std::uint8_t>(SecondPayload.data(), 1)),
		"Second send should enqueue");

	Router.TickFlush(1);
	MW_EXPECT_EQ(Test, std::size_t{1}, Channel.SendAttempts(), "The first flush must attempt exactly the retained head entry");
	MW_EXPECT_EQ(Test, std::size_t{0}, Channel.ReceivedCount(), "A failed send must not be recorded as accepted");
	MW_EXPECT_EQ(Test, QueueCapacity, Router.QueuedOutboundCount(), "A failed send must retain both queued entries, head first");

	Router.TickFlush(2);
	MW_EXPECT_EQ(Test, std::size_t{0}, Router.QueuedOutboundCount(), "The second flush must drain both entries once the channel accepts them");
	MW_EXPECT_EQ(Test, std::size_t{2}, Channel.ReceivedCount(), "Both retained entries must be accepted on the resumed flush");
	MW_EXPECT_EQ(Test, ActorMessageHeaderBytes + 1, Channel.ReceivedLength(0), "The first accepted entry must retain its original encoded length");
	MW_EXPECT_EQ(
		Test, std::uint8_t{0x11}, Channel.ReceivedByte(0, ActorMessageHeaderBytes), "The first accepted entry must retain its original payload byte");
	MW_EXPECT_EQ(
		Test,
		std::uint8_t{0x22},
		Channel.ReceivedByte(1, ActorMessageHeaderBytes),
		"The second accepted entry must retain its original payload byte");
}

// ---------------------------------------------------------------------------
// Category 6: Allocation-free steady-state operation
// ---------------------------------------------------------------------------

/** Proves registration, send, flush, dispatch, removal, and re-registration perform no observable allocation. */
MW_TEST_CASE(EngineMessageRouter_SteadyStateOperationPerformsNoAllocation)
{
	FTestRouter Router;
	FHandlerCallRecord Recorder;
	FMessageHandlerHandle Handle{};
	MW_EXPECT_SUCCESS(
		Test, Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 1), Handle), "Setup registration should succeed");

	const std::array<std::uint8_t, 1> Payload{0x7F};
	const TSpan<const std::uint8_t> PayloadView(Payload.data(), Payload.size());

	// Warm up once so any one-time lazy allocation is excluded from the steady-state measurement.
	(void)Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, PayloadView);
	Router.TickFlush(1);
	Router.TickDispatch(1);

	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	(void)Router.BroadcastMessage(LocalChannelId, TypeAlpha, SenderId, PayloadView);
	Router.TickFlush(2);
	Router.TickDispatch(2);
	(void)Router.RemoveMessageHandler(Handle);

	FMessageHandlerHandle ReplacementHandle{};
	(void)Router.AddMessageHandler(TypeAlpha, BroadcastActorId, MakeRecordingHandler(Recorder, 2), ReplacementHandle);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "Send, flush, dispatch, remove, and re-add must not allocate in steady state");
}

} // namespace
