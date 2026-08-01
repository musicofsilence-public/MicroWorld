#include "TestSupport.h"

#include <MicroWorld/Transport/LoopbackNetwork.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::MakeBroadcastAddress;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::TLoopbackNetwork;

/** Motivation: Sentinel byte proves a failed receive leaves a caller's address unchanged. */
constexpr std::uint8_t UntouchedAddressByte = 0xA5;
/** Motivation: Sentinel byte proves a failed receive leaves caller-owned destination storage unchanged. */
constexpr std::uint8_t DestinationPrefillByte = 0x5A;
/** Motivation: Sentinel count proves a failed receive leaves the reported byte count unchanged. */
constexpr std::size_t UntouchedByteCount = 91;
/** Motivation: Port zero originates packets in cross-port routing scenarios. */
constexpr std::uint8_t PortZero = 0;
/** Motivation: Port one receives packets sent from port zero. */
constexpr std::uint8_t PortOne = 1;
/** Motivation: Port two keeps an independent queue during drain scenarios. */
constexpr std::uint8_t PortTwo = 2;
/** Motivation: Two ports provide the smallest cross-port routing network. */
constexpr std::size_t TwoPorts = 2;
/** Motivation: Three ports allow independent mailbox-drain verification. */
constexpr std::size_t ThreePorts = 3;
/** Motivation: One slot forces a full-destination result after one accepted packet. */
constexpr std::size_t OneMailboxSlot = 1;
/** Motivation: Two slots exercise retained FIFO order after a rejected overflow packet. */
constexpr std::size_t TwoMailboxSlots = 2;
/** Motivation: Three slots retain three distinct FIFO packets. */
constexpr std::size_t ThreeMailboxSlots = 3;
/** Motivation: Four bytes bound destinations and accepted packets in ordinary scenarios. */
constexpr std::size_t FourPacketBytes = 4;
/** Motivation: Two bytes make the oversized-packet boundary directly observable. */
constexpr std::size_t TwoPacketBytes = 2;
/** Motivation: Index equal to the port count is outside a two-port network. */
constexpr std::uint8_t FirstInvalidPort = static_cast<std::uint8_t>(TwoPorts);

/** Motivation: Delivers two bytes from port zero to port one. */
constexpr std::uint8_t CrossPortPacket[] = {0x10, 0x20};
/** Motivation: Fills the destination queue before an overflow attempt. */
constexpr std::uint8_t FullQueueFirstPacket[] = {0x31};
/** Motivation: Occupies the second FIFO position before an overflow attempt. */
constexpr std::uint8_t FullQueueSecondPacket[] = {0x32};
/** Motivation: Must be rejected without replacing either accepted FIFO packet. */
constexpr std::uint8_t FullQueueRejectedPacket[] = {0x33};
/** Motivation: Requires a larger retry destination than the first receive provides. */
constexpr std::uint8_t TooLargeForFirstDestinationPacket[] = {0x41, 0x42, 0x43};
/** Motivation: Stays queued while a null receive destination is rejected. */
constexpr std::uint8_t NullDestinationHeadPacket[] = {0x51, 0x52};
/** Motivation: Identifies the first packet in a three-packet FIFO sequence. */
constexpr std::uint8_t FifoFirstPacket[] = {0x61};
/** Motivation: Identifies the second packet in a three-packet FIFO sequence. */
constexpr std::uint8_t FifoSecondPacket[] = {0x62};
/** Motivation: Identifies the third packet in a three-packet FIFO sequence. */
constexpr std::uint8_t FifoThirdPacket[] = {0x63};
/** Motivation: Supplies valid bytes while malformed destinations are rejected. */
constexpr std::uint8_t AddressValidationPacket[] = {0x71};
/** Motivation: Verifies sends and receives through the abstract device interface. */
constexpr std::uint8_t InterfacePacket[] = {0x81, 0x82};

/**
 * Motivation: The application entry point needs fixed network capacities visible before it starts sending.
 * Responsibilities: Report the configured port, mailbox, and packet capacities while every fresh mailbox is empty.
 */
MW_TEST_CASE(LoopbackNetworkStartsEmptyWithConfiguredCapacities)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, FourPacketBytes> Network;

	// Act
	const std::size_t PortCount = Network.PortCount();
	const std::size_t MailboxCapacity = Network.MailboxCapacityValue();
	const std::size_t MaximumPacketBytes = Network.MaximumPacketBytes();
	const bool bPortZeroEmpty = Network.IsEmpty(PortZero);
	const bool bPortOneEmpty = Network.IsEmpty(PortOne);
	const bool bPortZeroFull = Network.IsFull(PortZero);
	const std::size_t PortZeroQueuedCount = Network.QueuedCount(PortZero);

	// Assert
	MW_EXPECT_EQ(Test, TwoPorts, PortCount, "Port count must match the template capacity");
	MW_EXPECT_EQ(Test, TwoMailboxSlots, MailboxCapacity, "Mailbox capacity must match the template capacity");
	MW_EXPECT_EQ(Test, FourPacketBytes, MaximumPacketBytes, "Packet byte capacity must match the template capacity");
	MW_EXPECT_EQ(Test, true, bPortZeroEmpty, "Port zero mailbox must start empty");
	MW_EXPECT_EQ(Test, true, bPortOneEmpty, "Port one mailbox must start empty");
	MW_EXPECT_EQ(Test, false, bPortZeroFull, "Fresh port zero mailbox must not start full");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortZeroQueuedCount, "Fresh port zero mailbox must have no queued packets");
}

/**
 * Motivation: A two-node composition needs addressed bytes to arrive only at the selected peer.
 * Responsibilities: Deliver exact bytes, length, and sender to port one while port zero remains empty after outbound traffic.
 */
MW_TEST_CASE(LoopbackNetworkRoutesBytesToAddressedPortWithoutEcho)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);

	// Act
	const ETransportResult SendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(CrossPortPacket, sizeof(CrossPortPacket)));
	const bool bPortZeroEmptyAfterSend = Network.IsEmpty(PortZero);

	std::uint8_t Destination[FourPacketBytes] = {};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	const ETransportResult ReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "Cross-port send must succeed");
	MW_EXPECT_EQ(Test, true, bPortZeroEmptyAfterSend, "Sender port must stay empty after sending only to port one");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveStatus, "Addressed port must receive the queued packet");
	MW_EXPECT_EQ(Test, sizeof(CrossPortPacket), ReceiveResult.BytesReceived, "Receive must report the transmitted byte length");
	MW_EXPECT_EQ(Test, CrossPortPacket[0], Destination[0], "Receive must preserve the first transmitted byte");
	MW_EXPECT_EQ(Test, CrossPortPacket[1], Destination[1], "Receive must preserve the second transmitted byte");
	const bool bSenderMatchesPortZero = ReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bSenderMatchesPortZero, "Receive must report port zero as the packet sender");
}

/**
 * Motivation: Polling an idle port must not corrupt caller-owned receive outputs.
 * Responsibilities: Return Unavailable and leave destination bytes, received count, and sender address unchanged.
 */
MW_TEST_CASE(LoopbackNetworkEmptyReceiveLeavesOutputsUnchanged)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	std::uint8_t Destination[FourPacketBytes] = {DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedByteCount};
	FDeviceAddress ReceiveFrom{MakeLoopbackAddress(UntouchedAddressByte)};

	// Act
	const ETransportResult ReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, ReceiveStatus, "Empty mailbox receive must return Unavailable");
	MW_EXPECT_EQ(Test, UntouchedByteCount, ReceiveResult.BytesReceived, "Unavailable receive must preserve the received byte count");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Unavailable receive must preserve the destination prefix");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[3], "Unavailable receive must preserve the destination suffix");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Unavailable receive must preserve the sender address");
}

/**
 * Motivation: Protocols may use an empty packet as a meaningful message.
 * Responsibilities: Queue and deliver a zero-length packet while preserving caller destination bytes and reporting its sender.
 */
MW_TEST_CASE(LoopbackNetworkRoundTripsZeroLengthPacket)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);

	// Act
	const ETransportResult SendResult = Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(nullptr, 0));
	const std::size_t QueuedAfterSend = Network.QueuedCount(PortOne);

	std::uint8_t Destination[FourPacketBytes] = {DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	const ETransportResult ReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "Zero-length packet send must succeed");
	MW_EXPECT_EQ(Test, OneMailboxSlot, QueuedAfterSend, "Zero-length packet must occupy one mailbox slot");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveStatus, "Queued zero-length packet receive must succeed");
	MW_EXPECT_EQ(Test, std::size_t{0}, ReceiveResult.BytesReceived, "Zero-length packet receive must report zero bytes");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Zero-length receive must preserve destination storage");
	const bool bSenderMatchesPortZero = ReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bSenderMatchesPortZero, "Zero-length receive must report the sending port");
}

/**
 * Motivation: The network packet bound must reject requests that cannot fit in fixed storage.
 * Responsibilities: Return Invalid for an oversized packet without queueing any packet on the addressed port.
 */
MW_TEST_CASE(LoopbackNetworkRejectsOversizedPacketWithoutQueueing)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, TwoPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);

	// Act
	const ETransportResult SendResult = Network.Port(PortZero).TrySend(
		PortOneAddress, TSpan<const std::uint8_t>(TooLargeForFirstDestinationPacket, sizeof(TooLargeForFirstDestinationPacket)));
	const std::size_t QueuedAfterSend = Network.QueuedCount(PortOne);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, SendResult, "Oversized packet send must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, QueuedAfterSend, "Oversized packet must not queue at the destination");
}

/**
 * Motivation: A non-empty byte span must always own valid source storage.
 * Responsibilities: Return Invalid for null nonzero source data without changing the addressed mailbox.
 */
MW_TEST_CASE(LoopbackNetworkRejectsNullPacketWithNonzeroLength)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);

	// Act
	const ETransportResult SendResult = Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(nullptr, 1));
	const std::size_t QueuedAfterSend = Network.QueuedCount(PortOne);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, SendResult, "Null nonzero packet send must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, QueuedAfterSend, "Null nonzero packet send must not queue a packet");
}

/**
 * Motivation: Addressed loopback has exactly one legal one-byte port encoding and no broadcast route.
 * Responsibilities: Reject malformed, out-of-range, and broadcast destinations without queueing a packet anywhere.
 */
MW_TEST_CASE(LoopbackNetworkRejectsInvalidAndBroadcastDestinations)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	FDeviceAddress EmptyAddress{};
	FDeviceAddress TwoByteAddress{};
	TwoByteAddress.Bytes[0] = PortOne;
	TwoByteAddress.Bytes[1] = PortZero;
	TwoByteAddress.Size = 2;
	const FDeviceAddress InvalidPortAddress = MakeLoopbackAddress(FirstInvalidPort);
	const FDeviceAddress BroadcastAddress = MakeBroadcastAddress();

	// Act
	const ETransportResult EmptyAddressResult =
		Network.Port(PortZero).TrySend(EmptyAddress, TSpan<const std::uint8_t>(AddressValidationPacket, sizeof(AddressValidationPacket)));
	const ETransportResult TwoByteAddressResult =
		Network.Port(PortZero).TrySend(TwoByteAddress, TSpan<const std::uint8_t>(AddressValidationPacket, sizeof(AddressValidationPacket)));
	const ETransportResult InvalidPortResult =
		Network.Port(PortZero).TrySend(InvalidPortAddress, TSpan<const std::uint8_t>(AddressValidationPacket, sizeof(AddressValidationPacket)));
	const ETransportResult BroadcastResult =
		Network.Port(PortZero).TrySend(BroadcastAddress, TSpan<const std::uint8_t>(AddressValidationPacket, sizeof(AddressValidationPacket)));
	const std::size_t PortZeroQueuedCount = Network.QueuedCount(PortZero);
	const std::size_t PortOneQueuedCount = Network.QueuedCount(PortOne);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, EmptyAddressResult, "Empty destination address must return Invalid");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, TwoByteAddressResult, "Two-byte destination address must return Invalid");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, InvalidPortResult, "Out-of-range destination port must return Invalid");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, BroadcastResult, "Broadcast destination must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortZeroQueuedCount, "Rejected destinations must not queue on port zero");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortOneQueuedCount, "Rejected destinations must not queue on port one");
}

/**
 * Motivation: A full destination must preserve already accepted messages for later polling.
 * Responsibilities: Return Full for the overflow send and deliver both accepted packets in their original FIFO order.
 */
MW_TEST_CASE(LoopbackNetworkFullDestinationRetainsAcceptedFifoPackets)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);

	// Act
	const ETransportResult FirstSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FullQueueFirstPacket, sizeof(FullQueueFirstPacket)));
	const ETransportResult SecondSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FullQueueSecondPacket, sizeof(FullQueueSecondPacket)));
	const ETransportResult OverflowResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FullQueueRejectedPacket, sizeof(FullQueueRejectedPacket)));
	const bool bDestinationFull = Network.IsFull(PortOne);

	std::uint8_t FirstDestination[FourPacketBytes] = {};
	FReceiveResult FirstReceiveResult{};
	FDeviceAddress FirstReceiveFrom{};
	const ETransportResult FirstReceiveStatus =
		Network.Port(PortOne).TryReceive(FirstReceiveFrom, TSpan<std::uint8_t>(FirstDestination, sizeof(FirstDestination)), FirstReceiveResult);

	std::uint8_t SecondDestination[FourPacketBytes] = {};
	FReceiveResult SecondReceiveResult{};
	FDeviceAddress SecondReceiveFrom{};
	const ETransportResult SecondReceiveStatus =
		Network.Port(PortOne).TryReceive(SecondReceiveFrom, TSpan<std::uint8_t>(SecondDestination, sizeof(SecondDestination)), SecondReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "First packet must fill the first destination slot");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondSendResult, "Second packet must fill the second destination slot");
	MW_EXPECT_EQ(Test, ETransportResult::Full, OverflowResult, "Overflow packet must return Full");
	MW_EXPECT_EQ(Test, true, bDestinationFull, "Destination mailbox must remain full after overflow rejection");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstReceiveStatus, "First accepted packet must remain receivable");
	MW_EXPECT_EQ(Test, FullQueueFirstPacket[0], FirstDestination[0], "First accepted packet must remain FIFO head");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondReceiveStatus, "Second accepted packet must remain receivable");
	MW_EXPECT_EQ(Test, FullQueueSecondPacket[0], SecondDestination[0], "Second accepted packet must remain next in FIFO order");
	const bool bFirstSenderMatchesPortZero = FirstReceiveFrom == PortZeroAddress;
	const bool bSecondSenderMatchesPortZero = SecondReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bFirstSenderMatchesPortZero, "First retained packet must preserve its sender");
	MW_EXPECT_EQ(Test, true, bSecondSenderMatchesPortZero, "Second retained packet must preserve its sender");
}

/**
 * Motivation: A caller can retry a failed receive without losing the queued head packet.
 * Responsibilities: Return Full for a too-small destination, preserve outputs and queue state, then deliver the head on retry.
 */
MW_TEST_CASE(LoopbackNetworkTooSmallDestinationRetainsHeadForRetry)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);
	const ETransportResult SendResult = Network.Port(PortZero).TrySend(
		PortOneAddress, TSpan<const std::uint8_t>(TooLargeForFirstDestinationPacket, sizeof(TooLargeForFirstDestinationPacket)));

	std::uint8_t TooSmallDestination[2] = {DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult TooSmallReceiveResult{UntouchedByteCount};
	FDeviceAddress TooSmallReceiveFrom{MakeLoopbackAddress(UntouchedAddressByte)};

	// Act
	const ETransportResult TooSmallStatus = Network.Port(PortOne).TryReceive(
		TooSmallReceiveFrom, TSpan<std::uint8_t>(TooSmallDestination, sizeof(TooSmallDestination)), TooSmallReceiveResult);
	const std::size_t QueuedAfterTooSmallReceive = Network.QueuedCount(PortOne);

	std::uint8_t RetryDestination[FourPacketBytes] = {};
	FReceiveResult RetryReceiveResult{};
	FDeviceAddress RetryReceiveFrom{};
	const ETransportResult RetryStatus =
		Network.Port(PortOne).TryReceive(RetryReceiveFrom, TSpan<std::uint8_t>(RetryDestination, sizeof(RetryDestination)), RetryReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "Head packet setup send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Full, TooSmallStatus, "Too-small destination must return Full");
	MW_EXPECT_EQ(Test, UntouchedByteCount, TooSmallReceiveResult.BytesReceived, "Too-small receive must preserve the byte count output");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, TooSmallDestination[0], "Too-small receive must preserve destination storage");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, TooSmallReceiveFrom.Bytes[0], "Too-small receive must preserve sender output");
	MW_EXPECT_EQ(Test, OneMailboxSlot, QueuedAfterTooSmallReceive, "Too-small receive must retain the FIFO head");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetryStatus, "Larger retry destination must receive retained head");
	MW_EXPECT_EQ(Test, sizeof(TooLargeForFirstDestinationPacket), RetryReceiveResult.BytesReceived, "Retry must report retained head length");
	MW_EXPECT_EQ(Test, TooLargeForFirstDestinationPacket[0], RetryDestination[0], "Retry must deliver retained head first byte");
	MW_EXPECT_EQ(Test, TooLargeForFirstDestinationPacket[2], RetryDestination[2], "Retry must deliver retained head final byte");
	const bool bRetrySenderMatchesPortZero = RetryReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bRetrySenderMatchesPortZero, "Retry must preserve retained head sender");
}

/**
 * Motivation: Invalid receive storage must never consume a message that a caller can retry later.
 * Responsibilities: Return Invalid for a null nonzero destination, preserve outputs and head state, then deliver the head on retry.
 */
MW_TEST_CASE(LoopbackNetworkNullDestinationRetainsHeadAndOutputs)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);
	const ETransportResult SendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(NullDestinationHeadPacket, sizeof(NullDestinationHeadPacket)));
	FReceiveResult NullReceiveResult{UntouchedByteCount};
	FDeviceAddress NullReceiveFrom{MakeLoopbackAddress(UntouchedAddressByte)};

	// Act
	const ETransportResult NullDestinationStatus =
		Network.Port(PortOne).TryReceive(NullReceiveFrom, TSpan<std::uint8_t>(nullptr, 1), NullReceiveResult);
	const std::size_t QueuedAfterInvalidReceive = Network.QueuedCount(PortOne);

	std::uint8_t RetryDestination[FourPacketBytes] = {};
	FReceiveResult RetryReceiveResult{};
	FDeviceAddress RetryReceiveFrom{};
	const ETransportResult RetryStatus =
		Network.Port(PortOne).TryReceive(RetryReceiveFrom, TSpan<std::uint8_t>(RetryDestination, sizeof(RetryDestination)), RetryReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "Head packet setup send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullDestinationStatus, "Null nonzero destination must return Invalid");
	MW_EXPECT_EQ(Test, UntouchedByteCount, NullReceiveResult.BytesReceived, "Invalid receive must preserve the byte count output");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, NullReceiveFrom.Bytes[0], "Invalid receive must preserve sender output");
	MW_EXPECT_EQ(Test, OneMailboxSlot, QueuedAfterInvalidReceive, "Invalid receive must retain the FIFO head");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetryStatus, "Valid retry must receive the retained head");
	MW_EXPECT_EQ(Test, sizeof(NullDestinationHeadPacket), RetryReceiveResult.BytesReceived, "Valid retry must report retained head length");
	MW_EXPECT_EQ(Test, NullDestinationHeadPacket[0], RetryDestination[0], "Valid retry must deliver retained head first byte");
	const bool bRetrySenderMatchesPortZero = RetryReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bRetrySenderMatchesPortZero, "Valid retry must report the retained head sender");
}

/**
 * Motivation: Consumers polling a mailbox must observe accepted messages in send order.
 * Responsibilities: Deliver three queued packets one at a time in FIFO order and leave the mailbox empty afterward.
 */
MW_TEST_CASE(LoopbackNetworkDeliversThreePacketsInFifoOrder)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, ThreeMailboxSlots, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);

	// Act
	const ETransportResult FirstSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FifoFirstPacket, sizeof(FifoFirstPacket)));
	const ETransportResult SecondSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FifoSecondPacket, sizeof(FifoSecondPacket)));
	const ETransportResult ThirdSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(FifoThirdPacket, sizeof(FifoThirdPacket)));

	std::uint8_t Destination[FourPacketBytes] = {};
	FReceiveResult FirstReceiveResult{};
	FReceiveResult SecondReceiveResult{};
	FReceiveResult ThirdReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	const ETransportResult FirstReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), FirstReceiveResult);
	const std::uint8_t FirstReceivedByte = Destination[0];
	const ETransportResult SecondReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), SecondReceiveResult);
	const std::uint8_t SecondReceivedByte = Destination[0];
	const ETransportResult ThirdReceiveStatus =
		Network.Port(PortOne).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ThirdReceiveResult);
	const std::uint8_t ThirdReceivedByte = Destination[0];
	const bool bPortOneEmptyAfterReceives = Network.IsEmpty(PortOne);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "First FIFO packet send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondSendResult, "Second FIFO packet send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ThirdSendResult, "Third FIFO packet send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstReceiveStatus, "First FIFO receive must succeed");
	MW_EXPECT_EQ(Test, FifoFirstPacket[0], FirstReceivedByte, "First FIFO receive must deliver the first packet");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondReceiveStatus, "Second FIFO receive must succeed");
	MW_EXPECT_EQ(Test, FifoSecondPacket[0], SecondReceivedByte, "Second FIFO receive must deliver the second packet");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ThirdReceiveStatus, "Third FIFO receive must succeed");
	MW_EXPECT_EQ(Test, FifoThirdPacket[0], ThirdReceivedByte, "Third FIFO receive must deliver the third packet");
	MW_EXPECT_EQ(Test, true, bPortOneEmptyAfterReceives, "Mailbox must be empty after three successful receives");
}

/**
 * Motivation: Test setup needs to discard selected mailbox state without corrupting unrelated peers.
 * Responsibilities: Drain one port only, then drain all ports and report each affected queue as empty.
 */
MW_TEST_CASE(LoopbackNetworkDrainAndDrainAllRespectMailboxScope)
{
	// Arrange
	TLoopbackNetwork<ThreePorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortTwoAddress = MakeLoopbackAddress(PortTwo);
	const ETransportResult PortOneSendResult =
		Network.Port(PortZero).TrySend(PortOneAddress, TSpan<const std::uint8_t>(CrossPortPacket, sizeof(CrossPortPacket)));
	const ETransportResult PortTwoSendResult =
		Network.Port(PortZero).TrySend(PortTwoAddress, TSpan<const std::uint8_t>(CrossPortPacket, sizeof(CrossPortPacket)));

	// Act
	Network.Drain(PortOne);
	const std::size_t PortOneQueuedAfterDrain = Network.QueuedCount(PortOne);
	const std::size_t PortTwoQueuedAfterDrain = Network.QueuedCount(PortTwo);
	Network.DrainAll();
	const std::size_t PortZeroQueuedAfterDrainAll = Network.QueuedCount(PortZero);
	const std::size_t PortOneQueuedAfterDrainAll = Network.QueuedCount(PortOne);
	const std::size_t PortTwoQueuedAfterDrainAll = Network.QueuedCount(PortTwo);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, PortOneSendResult, "Port one setup send must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, PortTwoSendResult, "Port two setup send must succeed");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortOneQueuedAfterDrain, "Drain must empty only the selected mailbox");
	MW_EXPECT_EQ(Test, OneMailboxSlot, PortTwoQueuedAfterDrain, "Drain must preserve packets queued for other ports");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortZeroQueuedAfterDrainAll, "DrainAll must leave port zero mailbox empty");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortOneQueuedAfterDrainAll, "DrainAll must leave port one mailbox empty");
	MW_EXPECT_EQ(Test, std::size_t{0}, PortTwoQueuedAfterDrainAll, "DrainAll must leave port two mailbox empty");
}

/**
 * Motivation: Callers consume loopback ports through the common transport-device boundary.
 * Responsibilities: Route a packet through Core::ITransportDevice references and preserve bytes, length, and sender.
 */
MW_TEST_CASE(LoopbackNetworkPortsSatisfyTransportDeviceInterface)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, FourPacketBytes> Network;
	const FDeviceAddress PortOneAddress = MakeLoopbackAddress(PortOne);
	const FDeviceAddress PortZeroAddress = MakeLoopbackAddress(PortZero);
	ITransportDevice& SendingDevice = Network.Port(PortZero);
	ITransportDevice& ReceivingDevice = Network.Port(PortOne);

	// Act
	const ETransportResult SendResult = SendingDevice.TrySend(PortOneAddress, TSpan<const std::uint8_t>(InterfacePacket, sizeof(InterfacePacket)));
	std::uint8_t Destination[FourPacketBytes] = {};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	const ETransportResult ReceiveStatus =
		ReceivingDevice.TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "Interface send must route to the addressed mailbox");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveStatus, "Interface receive must deliver the queued packet");
	MW_EXPECT_EQ(Test, sizeof(InterfacePacket), ReceiveResult.BytesReceived, "Interface receive must report the packet length");
	MW_EXPECT_EQ(Test, InterfacePacket[0], Destination[0], "Interface receive must preserve the first byte");
	MW_EXPECT_EQ(Test, InterfacePacket[1], Destination[1], "Interface receive must preserve the second byte");
	const bool bSenderMatchesPortZero = ReceiveFrom == PortZeroAddress;
	MW_EXPECT_EQ(Test, true, bSenderMatchesPortZero, "Interface receive must report the sending port");
}

} // namespace
