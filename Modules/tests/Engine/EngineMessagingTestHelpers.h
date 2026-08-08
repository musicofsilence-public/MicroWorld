#pragma once

#include "TestSupport.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>
#include <MicroWorld/Messaging/NameId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::FWeakOwner;
using MicroWorld::Core::IPlaySystem;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FTypeId;
using MicroWorld::Engine::MakeWeakOwner;
using MicroWorld::Engine::TEngine;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::MakeNameId;

/** Motivation: Names the production-shaped engine type exercised by this composition test suite. */
using FEngine = TEngine<FDefaultEngineTraits>;

/**
 * Motivation: Finishes a whole collection cycle in one slice so no case here is shaped by a partial one; these tests
 *   create no garbage, so the budget only has to stay out of the way.
 */
constexpr FGarbageCollectionBudget EngineMessagingCollectionBudget{
	static_cast<std::uint32_t>(FEngine::MaxRoots), static_cast<std::uint32_t>(FEngine::MaxObjects), static_cast<std::uint32_t>(FEngine::MaxObjects)};

/** Motivation: Names the bounded subscriber delegate accepted by the engine-owned Messaging system. */
using FSubscriberDelegate = FMessagingSystem::FSubscriberDelegate;

/** Motivation: Identifies the bit count in one byte for independent little-endian test-frame encoding. */
constexpr std::size_t BitsPerByte = 8;

/** Motivation: Identifies the byte count in a wire name id for independent test-frame encoding. */
constexpr std::size_t NameIdByteCount = sizeof(std::uint32_t);

/**
 * Motivation: Builds inbound wire identifiers without reusing Messaging's production encoder.
 * Responsibilities: Write InNameId into the caller-owned four-byte destination in little-endian order.
 */
inline void WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutDestination) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < NameIdByteCount; ++ByteIndex)
	{
		OutDestination[ByteIndex] = static_cast<std::uint8_t>(InNameId.Value >> (ByteIndex * BitsPerByte));
	}
}

/**
 * Motivation: Gives engine-owned Messaging a deterministic single-frame device without hardware or wall-clock time.
 * Responsibilities: Count outbound sends, hand one queued inbound frame to Messaging, and otherwise report no input.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/** Motivation: Bounds one test frame to the largest frame the default Messaging system can process. */
	static constexpr std::size_t MaxTestPacketBytes = FMessagingSystem::MaxFrameBytes;

	/**
	 * Motivation: Lets a test provide one inbound frame before the engine's pre-advance turn drains it.
	 * Responsibilities: Copy one frame that fits the fixed test storage and make it available to exactly one receive call.
	 */
	void QueueInboundFrame(const TSpan<const std::uint8_t> InFrame) noexcept
	{
		if (InFrame.Size() > MaxTestPacketBytes)
		{
			return;
		}

		QueuedFrameByteCount = InFrame.Size();
		for (std::size_t ByteIndex = 0; ByteIndex < QueuedFrameByteCount; ++ByteIndex)
		{
			QueuedFrameBytes[ByteIndex] = InFrame[ByteIndex];
		}
		bHasQueuedFrame = true;
	}

	/**
	 * Motivation: Lets outbound Messaging behavior be observed without emulating a physical medium.
	 * Responsibilities: Count each complete send request and report that the fake device accepted it.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override
	{
		++TrySendCallCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Supplies the one queued inbound frame to Messaging's device drain turn.
	 * Responsibilities: Copy the queued frame transactionally when it fits, then report Unavailable forever after.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		if (!bHasQueuedFrame)
		{
			return ETransportResult::Unavailable;
		}

		bHasQueuedFrame = false;
		if (QueuedFrameByteCount > InDestination.Size())
		{
			return ETransportResult::Invalid;
		}

		for (std::size_t ByteIndex = 0; ByteIndex < QueuedFrameByteCount; ++ByteIndex)
		{
			InDestination[ByteIndex] = QueuedFrameBytes[ByteIndex];
		}
		OutFrom = FDeviceAddress{};
		OutResult.BytesReceived = QueuedFrameByteCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Lets Messaging preflight the fake device's bounded packet capacity.
	 * Responsibilities: Return the fixed maximum packet byte count.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return MaxTestPacketBytes; }

	/**
	 * Motivation: Observes lifecycle ownership without adding device behavior to the transport fake.
	 * Responsibilities: Count each BeginPlay turn received from the direct owner.
	 */
	void BeginPlay(TimePointMilliseconds) noexcept override { ++BeginPlayCallCount; }

	/**
	 * Motivation: Observes the device turn that precedes Engine-owned Messaging and Networking work.
	 * Responsibilities: Count each PreAdvance turn without receiving or sending test packets.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override { ++PreAdvanceCallCount; }

	/**
	 * Motivation: Observes the device turn that follows Engine-owned Messaging and Networking work.
	 * Responsibilities: Count each PostAdvance turn without receiving or sending test packets.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override { ++PostAdvanceCallCount; }

	/**
	 * Motivation: Observes lifecycle cleanup after its owner ends play.
	 * Responsibilities: Count each EndPlay turn without releasing caller-owned fake state.
	 */
	void EndPlay() noexcept override { ++EndPlayCallCount; }

	/** Motivation: Counts outbound frames Messaging asks this fake device to accept. */
	std::size_t TrySendCallCount{};

	/** Motivation: Counts BeginPlay calls delivered to this fake transport device. */
	std::size_t BeginPlayCallCount{};

	/** Motivation: Counts PreAdvance calls delivered before Engine-owned Messaging and Networking work. */
	std::size_t PreAdvanceCallCount{};

	/** Motivation: Counts PostAdvance calls delivered after Engine-owned Messaging and Networking work. */
	std::size_t PostAdvanceCallCount{};

	/** Motivation: Counts EndPlay cleanup calls delivered to this fake transport device. */
	std::size_t EndPlayCallCount{};

private:
	/** Motivation: Owns the bytes of the single inbound frame until Messaging consumes them. */
	std::array<std::uint8_t, MaxTestPacketBytes> QueuedFrameBytes{};

	/** Motivation: Identifies how many queued bytes TryReceive may copy into its caller-owned destination. */
	std::size_t QueuedFrameByteCount{};

	/** Motivation: Distinguishes one queued frame from a device that has no input available. */
	bool bHasQueuedFrame{};
};

/**
 * Motivation: Retains an inbound message's observable data after the subscriber's borrowed payload span expires.
 * Responsibilities: Count deliveries and copy the latest message name and bounded payload bytes for assertions.
 * Example:
 *   FInboundMessageRecorder Recorder;
 */
struct FInboundMessageRecorder final
{
	/** Motivation: Reserves storage large enough for any payload copied from the default Messaging frame. */
	static constexpr std::size_t MaxRecordedPayloadBytes = FMessagingSystem::MaxFrameBytes;

	/**
	 * Motivation: Captures the message received during the engine-driven inbound turn.
	 * Responsibilities: Copy the name and payload into recorder-owned storage without retaining a transient span.
	 */
	void Record(const FMessage& InMessage) noexcept
	{
		++DeliveryCount;
		MessageNameId = InMessage.GetMessageNameId();
		const TSpan<const std::uint8_t> Payload = InMessage.GetPayload();
		PayloadByteCount = Payload.Size();
		const std::size_t CopiedByteCount = PayloadByteCount < MaxRecordedPayloadBytes ? PayloadByteCount : MaxRecordedPayloadBytes;
		for (std::size_t ByteIndex = 0; ByteIndex < CopiedByteCount; ++ByteIndex)
		{
			PayloadBytes[ByteIndex] = Payload[ByteIndex];
		}
	}

	/** Motivation: Counts inbound deliveries that reached the subscriber. */
	std::size_t DeliveryCount{};

	/** Motivation: Preserves the received message identity for routing assertions. */
	FNameId MessageNameId{};

	/** Motivation: Preserves the received payload length for post-delivery assertions. */
	std::size_t PayloadByteCount{};

	/** Motivation: Retains copied payload bytes after the subscriber's callback returns. */
	std::array<std::uint8_t, MaxRecordedPayloadBytes> PayloadBytes{};
};

/**
 * Motivation: Makes the caller-bound play-system turns externally observable in one composition test.
 * Responsibilities: Count each lifecycle and frame turn without adding behavior or dependencies.
 * Example:
 *   FRecordingPlaySystem System;
 */
class FRecordingPlaySystem final : public IPlaySystem
{
public:
	/**
	 * Motivation: Observes that the engine starts its caller-bound system when it begins play.
	 * Responsibilities: Increment only the begin-play observation count.
	 */
	void BeginPlay(TimePointMilliseconds) noexcept override { ++BeginPlayCallCount; }

	/**
	 * Motivation: Observes that the engine drives the caller-bound inbound turn.
	 * Responsibilities: Increment only the pre-advance observation count.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override { ++PreAdvanceCallCount; }

	/**
	 * Motivation: Observes that the engine drives the caller-bound outbound turn.
	 * Responsibilities: Increment only the post-advance observation count.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override { ++PostAdvanceCallCount; }

	/**
	 * Motivation: Observes that the engine ends its caller-bound system when play ends.
	 * Responsibilities: Increment only the end-play observation count.
	 */
	void EndPlay() noexcept override { ++EndPlayCallCount; }

	/** Motivation: Counts engine begin-play calls routed to this caller-bound system. */
	std::size_t BeginPlayCallCount{};

	/** Motivation: Counts engine pre-advance calls routed to this caller-bound system. */
	std::size_t PreAdvanceCallCount{};

	/** Motivation: Counts engine post-advance calls routed to this caller-bound system. */
	std::size_t PostAdvanceCallCount{};

	/** Motivation: Counts engine end-play calls routed to this caller-bound system. */
	std::size_t EndPlayCallCount{};
};

/**
 * Motivation: Retains externally observable results from an actor-owned Messaging subscription.
 * Responsibilities: Count delivery and preserve the actor's bind and subscription results without making the actor's
 *   lifetime part of the callback capture.
 * Example:
 *   FBeginPlayMessagingSubscriptionContext Context;
 */
struct FBeginPlayMessagingSubscriptionContext final
{
	/** Motivation: Counts messages delivered through the actor-owned subscription. */
	std::size_t DeliveryCount{};

	/** Motivation: Preserves the delegate-bind result produced during the actor's BeginPlay. */
	EDelegateResult BindResult{EDelegateResult::InvalidHandle};

	/** Motivation: Preserves the subscription result produced during the actor's BeginPlay. */
	EMessagingResult SubscribeResult{EMessagingResult::Invalid};
};

/**
 * Motivation: Subscribes to an engine-owned Messaging channel as part of actor BeginPlay.
 * Responsibilities: Build a generation-backed weak owner from this actor's assigned store and handle, then bind a
 *   callback that captures only external test context.
 * Example:
 *   Engine.CreateObject<FBeginPlayMessagingSubscriberActor>(TypeId, Messaging, ChannelNameId, Context);
 */
class FBeginPlayMessagingSubscriberActor final : public AActor
{
public:
	/**
	 * Motivation: Provides the actor every stable dependency needed to subscribe when its world begins it.
	 * Responsibilities: Retain the optional non-owning Messaging pointer, external context, and channel identity used at
	 *   BeginPlay.
	 */
	FBeginPlayMessagingSubscriberActor(
		FMessagingSystem* const InMessagingSystem,
		const FNameId InChannelNameId,
		FBeginPlayMessagingSubscriptionContext& InSubscriptionContext) noexcept
		: AActor(), MessagingSystem(InMessagingSystem), ChannelNameId(InChannelNameId), SubscriptionContext(InSubscriptionContext)
	{
	}

protected:
	/**
	 * Motivation: Makes the subscription actor-owned so its generation becomes invalid when Engine reclaims the actor.
	 * Responsibilities: Bind the external-context callback, then subscribe with this actor's weak owner when binding
	 *   succeeds.
	 */
	void BeginPlay() noexcept override
	{
		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			SubscriptionContext.SubscribeResult = EMessagingResult::Invalid;
			return;
		}
		if (MessagingSystem == nullptr)
		{
			SubscriptionContext.SubscribeResult = EMessagingResult::Invalid;
			return;
		}

		FSubscriberDelegate Subscriber;
		FBeginPlayMessagingSubscriptionContext& Context = SubscriptionContext;
		Context.BindResult = Subscriber.Bind([&Context](const FMessage&) noexcept { ++Context.DeliveryCount; });
		if (Context.BindResult != EDelegateResult::Success)
		{
			Context.SubscribeResult = EMessagingResult::Invalid;
			return;
		}

		const FWeakOwner Owner = MakeWeakOwner(*ObjectStore, GetObjectHandle());
		Context.SubscribeResult = MessagingSystem->SubscribeToChannel(ChannelNameId, std::move(Subscriber), Owner);
	}

private:
	/** Motivation: Retains the optional engine-owned system used only when BeginPlay can subscribe safely. */
	FMessagingSystem* MessagingSystem;

	/** Motivation: Identifies the channel this actor registers during BeginPlay. */
	FNameId ChannelNameId;

	/** Motivation: Exposes bind, subscription, and delivery observations outside the destroyed actor. */
	FBeginPlayMessagingSubscriptionContext& SubscriptionContext;
};

/** Motivation: Identifies the actor type whose BeginPlay creates an actor-owned Messaging subscription. */
constexpr FTypeId BeginPlayMessagingSubscriberActorTypeId{0x00080001u};

/** Motivation: Names the registered actor descriptor used by every Messaging ownership test in this file. */
constexpr const char* BeginPlayMessagingSubscriberActorClassName = "BeginPlayMessagingSubscriberActor";

} // namespace MicroWorld::Tests
