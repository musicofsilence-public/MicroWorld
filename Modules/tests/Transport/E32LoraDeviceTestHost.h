#pragma once

#include "TestSupport.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Core/IO/UartByteStreamResult.h>
#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/Lora/E32Lora.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Tests
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::EUartByteStreamResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::IUartByteStream;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::E32MaxPayloadBytes;
using MicroWorld::Transport::FE32LoraDevice;
using MicroWorld::Transport::MakeLoraAddress;
using MicroWorld::Transport::FrameCodec::FrameOverheadBytes;

/** Motivation: Names the time handed to the device's pre-advance turn; the E32 device paces nothing by the clock and ignores it. */
constexpr TimePointMilliseconds PumpTimeMilliseconds{0};

/** Motivation: Fixed encoded E32 frame capacity used by transmit and receive test fixtures. */
constexpr std::size_t EncodedFrameCapacity = E32MaxPayloadBytes + FrameOverheadBytes;

/** Motivation: Device receive budget derived from the largest possible encoded E32 frame. */
constexpr std::size_t ReceivePumpByteCap = 2u * EncodedFrameCapacity;

/** Motivation: Fixed receive-stream capacity for one capped prefix followed by up to two encoded frames. */
constexpr std::size_t ReceiveStreamCapacity = ReceivePumpByteCap + (2u * EncodedFrameCapacity);

/** Motivation: Local node id used by every initialized transmitting device fixture. */
constexpr std::uint8_t LocalNodeId = 7;

/** Motivation: Peer node id used by every valid one-byte E32 destination and decoded frame fixture. */
constexpr std::uint8_t PeerNodeId = 9;

/** Motivation: Distinct node id used to prove non-success receives preserve caller sender outputs. */
constexpr std::uint8_t SentinelNodeId = 0xEE;

/** Motivation: Distinct value used to prove non-success receives preserve caller byte outputs. */
constexpr std::uint8_t SentinelByte = 0xD3;

/** Motivation: Distinct byte count used to prove non-success receives preserve the result output. */
constexpr std::size_t SentinelByteCount = 123;

/** Motivation: Three-byte payload used by normal send, receive, recovery, and exchange cases. */
constexpr std::uint8_t Payload[] = {0x10, 0x20, 0x30};

/** Motivation: Different payload that proves a released transmit slot accepts later work. */
constexpr std::uint8_t ReplacementPayload[] = {0x91, 0x82};

/** Motivation: Largest valid payload, used to exercise fixed-frame capacity and bounded burst progress. */
constexpr std::uint8_t MaximumPayload[E32MaxPayloadBytes] = {};

/** Motivation: One backed byte paired with an oversize span length so validation rejects before reading beyond it. */
constexpr std::uint8_t OversizePayloadByte = 0;

/**
 * Motivation: Fixed-capacity UART fake exposing explicit non-blocking read and write outcomes. Each test owns one
 *   fresh fake, so recorded traffic and configured outcomes never cross test boundaries.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FFakeUartByteStream final : public IUartByteStream
{
public:
	/**
	 * Motivation: Records a successful byte write or reports the result currently selected by the test.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	EUartByteStreamResult TryWriteByte(const std::uint8_t InByte) noexcept override
	{
		++WriteCallCountValue;
		if (WriteResult != EUartByteStreamResult::Success)
		{
			return WriteResult;
		}
		if (WrittenByteCountValue == SuccessfulWriteLimit)
		{
			return EUartByteStreamResult::Error;
		}
		if (WrittenByteCountValue == EncodedFrameCapacity)
		{
			return EUartByteStreamResult::Error;
		}

		WrittenBytes[WrittenByteCountValue] = InByte;
		++WrittenByteCountValue;
		return EUartByteStreamResult::Success;
	}

	/**
	 * Motivation: Supplies the next queued byte or reports the result currently selected by the test.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override
	{
		++ReadCallCountValue;
		if (ReadResult != EUartByteStreamResult::Success)
		{
			return ReadResult;
		}
		if (NextReceivedByteIndex == QueuedReceiveByteCount)
		{
			return EUartByteStreamResult::Unavailable;
		}

		OutByte = QueuedReceiveBytes[NextReceivedByteIndex];
		++NextReceivedByteIndex;
		return EUartByteStreamResult::Success;
	}

	/**
	 * Motivation: Selects the outcome returned by every later write attempt until another test change.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetWriteResult(const EUartByteStreamResult InResult) noexcept { WriteResult = InResult; }

	/**
	 * Motivation: Limits accepted writes before later attempts report Error, exercising partial-frame failure without
	 *   result storage.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetSuccessfulWriteLimit(const std::size_t InLimit) noexcept { SuccessfulWriteLimit = InLimit; }

	/**
	 * Motivation: Selects the outcome returned by every later read attempt until another test change.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetReadResult(const EUartByteStreamResult InResult) noexcept { ReadResult = InResult; }

	/**
	 * Motivation: Queues one raw incoming UART byte when fixed test storage remains available.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	bool QueueReceivedByte(const std::uint8_t InByte) noexcept
	{
		if (QueuedReceiveByteCount == ReceiveStreamCapacity)
		{
			return false;
		}

		QueuedReceiveBytes[QueuedReceiveByteCount] = InByte;
		++QueuedReceiveByteCount;
		return true;
	}

	/**
	 * Motivation: Reports how many write attempts the device made, including blocked and failed attempts.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t WriteCallCount() const noexcept { return WriteCallCountValue; }

	/**
	 * Motivation: Reports how many bytes the fake accepted after successful write attempts.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t WrittenByteCount() const noexcept { return WrittenByteCountValue; }

	/**
	 * Motivation: Reports how many read attempts the device made, including empty and failed attempts.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t ReadCallCount() const noexcept { return ReadCallCountValue; }

	/**
	 * Motivation: Reads one captured successful write for observable wire-traffic assertions.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::uint8_t WrittenByteAt(const std::size_t InIndex) const noexcept { return WrittenBytes[InIndex]; }

private:
	/** Motivation: Stores all successful writes from the one fixed frame a device may queue at once. */
	std::uint8_t WrittenBytes[EncodedFrameCapacity]{};

	/** Motivation: Stores raw bytes supplied to later receive polls without dynamic storage. */
	std::uint8_t QueuedReceiveBytes[ReceiveStreamCapacity]{};

	/** Motivation: Controls whether a write succeeds, blocks, or fails for the active test scenario. */
	EUartByteStreamResult WriteResult{EUartByteStreamResult::Success};

	/** Motivation: Controls whether a read consumes queued data, blocks, or fails for the active test scenario. */
	EUartByteStreamResult ReadResult{EUartByteStreamResult::Success};

	/** Motivation: Bounds successful writes before the fake reports Error for later attempts in a partial-frame failure scenario. */
	std::size_t SuccessfulWriteLimit{EncodedFrameCapacity};

	/** Motivation: Counts accepted bytes in WrittenBytes and bounds later indexed observations. */
	std::size_t WrittenByteCountValue{0};

	/** Motivation: Counts queued inbound bytes and bounds later UART read attempts. */
	std::size_t QueuedReceiveByteCount{0};

	/** Motivation: Identifies the next queued inbound byte that a successful read may consume. */
	std::size_t NextReceivedByteIndex{0};

	/** Motivation: Counts every write operation so blocked/error attempts stay observable. */
	std::size_t WriteCallCountValue{0};

	/** Motivation: Counts every read operation so bounded receive polling stays observable. */
	std::size_t ReadCallCountValue{0};
};

/**
 * Motivation: Encodes one peer frame into fixed storage for public-device receive scenarios.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline ETransportResult EncodePeerFrame(
	const std::uint8_t* const InPayload,
	const std::size_t InPayloadSize,
	std::uint8_t (&OutFrame)[EncodedFrameCapacity],
	std::size_t& OutFrameBytes) noexcept
{
	return MicroWorld::Transport::FrameCodec::EncodeFrame(
		PeerNodeId, TSpan<const std::uint8_t>(InPayload, InPayloadSize), TSpan<std::uint8_t>(OutFrame, sizeof(OutFrame)), OutFrameBytes);
}

/**
 * Motivation: Queues every byte of one fixed frame into a fake stream and reports whether its capacity was
 *   sufficient.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline bool QueueFrame(FFakeUartByteStream& InStream, const std::uint8_t* const InFrame, const std::size_t InFrameBytes) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InFrameBytes; ++ByteIndex)
	{
		if (!InStream.QueueReceivedByte(InFrame[ByteIndex]))
		{
			return false;
		}
	}

	return true;
}

/**
 * Motivation: Reports whether a destination retains one expected repeated sentinel value.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline bool DestinationContains(const std::uint8_t* const InDestination, const std::size_t InDestinationBytes, const std::uint8_t InExpected) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InDestinationBytes; ++ByteIndex)
	{
		if (InDestination[ByteIndex] != InExpected)
		{
			return false;
		}
	}

	return true;
}

/**
 * Motivation: Reports whether one received destination exactly matches the supplied expected payload.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline bool DestinationMatches(const std::uint8_t* const InDestination, const std::uint8_t* const InExpected, const std::size_t InBytes) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InBytes; ++ByteIndex)
	{
		if (InDestination[ByteIndex] != InExpected[ByteIndex])
		{
			return false;
		}
	}

	return true;
}

/**
 * Motivation: Reports whether an output address remains the expected one-byte E32 address.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline bool AddressHasNodeId(const FDeviceAddress& InAddress, const std::uint8_t InNodeId) noexcept
{
	return InAddress.Size == 1 && InAddress.Bytes[0] == InNodeId;
}

} // namespace MicroWorld::Tests
