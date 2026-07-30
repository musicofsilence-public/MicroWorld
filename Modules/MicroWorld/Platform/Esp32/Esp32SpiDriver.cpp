#include <MicroWorld/Platform/Esp32/Esp32SpiDriver.h>

#include "Detail/SpiPlatformImplementation.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace MicroWorld
{

static_assert(
	sizeof(spi_slave_transaction_t) <= SpiSlaveTransactionStorageBytes,
	"SpiSlaveTransactionStorageBytes must hold one spi_slave_transaction_t; grow it.");
static_assert(alignof(spi_slave_transaction_t) <= 8, "TransactionStorage alignment must cover spi_slave_transaction_t.");

namespace
{

	/** Maps one SPI transmit outcome to the shared driver result (mirrors the UART/I2C drivers' mapping). */
	ENetResult MapSpiTransmitOutcome(const Detail::ESpiTransmitOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case Detail::ESpiTransmitOutcome::Sent:
				return ENetResult::Success;
			case Detail::ESpiTransmitOutcome::WouldBlock:
				return ENetResult::Full;
			case Detail::ESpiTransmitOutcome::Error:
			default:
				return ENetResult::Invalid;
		}
	}

	/** Reports the first reason an outgoing packet cannot be framed and sent, or `Success`. */
	ENetResult ValidateOutgoingSpiPacket(const FNetAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
	{
		// Validate every argument before any syscall so a rejection is truly transactional.
		if (!IsSpiAddress(InTo))
		{
			return ENetResult::Invalid;
		}
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize > SpiMaxPayloadBytes)
		{
			return ENetResult::Invalid;
		}
		if (PacketSize != 0 && InPacket.Data() == nullptr)
		{
			return ENetResult::Invalid;
		}
		return ENetResult::Success;
	}

	/** Copies the decoder's held frame into the destination and clears it, or returns `Full`
	 * (leaving the frame held) when the payload exceeds the destination. */
	ENetResult DeliverFrameFromDecoder(
		TFrameDecoder<SpiMaxPayloadBytes>& InDecoder, TSpan<std::uint8_t> InDestination, FNetAddress& OutFrom, FNetReceiveResult& OutResult) noexcept
	{
		// On Full the destination is untouched and the frame stays held for the next call, so a
		// receive that cannot fit is transactional.
		const std::size_t HeldLength = InDecoder.FramePayload().Size();
		if (HeldLength > InDestination.Size())
		{
			return ENetResult::Full;
		}
		std::memcpy(InDestination.Data(), InDecoder.FramePayload().Data(), HeldLength);
		OutFrom = MakeSpiAddress(InDecoder.FrameNodeId());
		OutResult.BytesReceived = HeldLength;
		InDecoder.ClearFrame();
		return ENetResult::Success;
	}

	/** Pushes a whole received window through the decoder, stopping at the first completed frame; the
	 * caller must ensure the decoder holds no frame before calling. */
	void PumpWindowIntoDecoder(TFrameDecoder<SpiMaxPayloadBytes>& InDecoder, const std::uint8_t* const InWindow, const std::size_t InLength) noexcept
	{
		for (std::size_t Index = 0; Index < InLength; ++Index)
		{
			const EFrameEvent Event = InDecoder.PushByte(InWindow[Index]);
			if (Event == EFrameEvent::FrameReady)
			{
				return;
			}
			if (Event == EFrameEvent::Discarded)
			{
				MW_LOG_MSG(Warning, "Spi", "decoder discarded a candidate frame (bad length or CRC)");
			}
		}
	}

} // namespace

FEsp32SpiMasterDriver::FEsp32SpiMasterDriver(const FEsp32SpiMasterConfig& InConfig) noexcept
{
	const Detail::FOpenedSpiMaster Opened =
		Detail::OpenConfiguredSpiMaster(InConfig.SpiHost, InConfig.MosiGpio, InConfig.MisoGpio, InConfig.SclkGpio, InConfig.CsGpio, InConfig.ClockHz);
	if (!Opened.bOpen)
	{
		DeviceHandle = nullptr;
		SpiHostValue = 0;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	DeviceHandle = Opened.Device;
	SpiHostValue = InConfig.SpiHost;
	LocalNodeIdValue = InConfig.LocalNodeId;
	bOpen = true;
}

FEsp32SpiMasterDriver::~FEsp32SpiMasterDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseSpiMaster(SpiHostValue, static_cast<spi_device_handle_t>(DeviceHandle));
	}
}

ENetResult FEsp32SpiMasterDriver::ExchangeAndPump(const std::uint8_t* const InTransmitWindow) noexcept
{
	const Detail::ESpiTransmitOutcome Outcome =
		Detail::TransmitSpiMaster(static_cast<spi_device_handle_t>(DeviceHandle), InTransmitWindow, ReceiveWindow, SpiTransactionWindowBytes);
	const ENetResult Result = MapSpiTransmitOutcome(Outcome);
	// SPI is full-duplex: the received window holds the slave's simultaneous output, so feed it to the
	// decoder rather than discard it. Only pump when no frame is already held (the decoder precondition).
	if (Result == ENetResult::Success && !Decoder.HasFrame())
	{
		PumpWindowIntoDecoder(Decoder, ReceiveWindow, SpiTransactionWindowBytes);
	}
	return Result;
}

ENetResult FEsp32SpiMasterDriver::TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	const ENetResult Validation = ValidateOutgoingSpiPacket(InTo, InPacket);
	if (Validation != ENetResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure; pad the window's tail with idle bytes the peer's decoder ignores.
	std::size_t Written = 0;
	const ENetResult EncodeResult = EncodeFrame(LocalNodeIdValue, InPacket, TSpan<std::uint8_t>(TransmitWindow, SpiTransactionWindowBytes), Written);
	if (EncodeResult != ENetResult::Success)
	{
		return EncodeResult;
	}
	std::memset(TransmitWindow + Written, 0, SpiTransactionWindowBytes - Written);
	return ExchangeAndPump(TransmitWindow);
}

ENetResult FEsp32SpiMasterDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any bus transaction.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	// One idle full-duplex transaction clocks in whatever the slave has staged (ADR Appendix B).
	const ENetResult Exchange = ExchangeAndPump(IdleWindow);
	if (Exchange == ENetResult::Invalid)
	{
		return ENetResult::Invalid;
	}
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	return ENetResult::Unavailable;
}

std::size_t FEsp32SpiMasterDriver::MaxPacketBytes() const noexcept
{
	return SpiMaxPayloadBytes;
}

bool FEsp32SpiMasterDriver::IsOpen() const noexcept
{
	return bOpen;
}

FEsp32SpiSlaveDriver::FEsp32SpiSlaveDriver(const FEsp32SpiSlaveConfig& InConfig) noexcept
{
	const Detail::FOpenedSpiSlave Opened =
		Detail::OpenConfiguredSpiSlave(InConfig.SpiHost, InConfig.MosiGpio, InConfig.MisoGpio, InConfig.SclkGpio, InConfig.CsGpio);
	if (!Opened.bOpen)
	{
		SpiHostValue = 0;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	SpiHostValue = InConfig.SpiHost;
	LocalNodeIdValue = InConfig.LocalNodeId;
	// Give the persistent descriptor a lifetime in its opaque storage before the first queue.
	TransactionPtr = ::new (static_cast<void*>(TransactionStorage)) spi_slave_transaction_t{};
	bOpen = true;
	// Queue one idle transaction so the master's first clock finds a buffer instead of garbage.
	QueueNextTransaction();
}

FEsp32SpiSlaveDriver::~FEsp32SpiSlaveDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseSpiSlave(SpiHostValue);
	}
}

void FEsp32SpiSlaveDriver::QueueNextTransaction() noexcept
{
	// Use the staged frame if one is waiting, otherwise send idle bytes the master's decoder ignores.
	if (bFrameStaged)
	{
		std::memcpy(TransmitWindow, StagedFrame, SpiTransactionWindowBytes);
		bFrameStaged = false;
	}
	else
	{
		std::memset(TransmitWindow, 0, SpiTransactionWindowBytes);
	}
	spi_slave_transaction_t* const Transaction = static_cast<spi_slave_transaction_t*>(TransactionPtr);
	bTransactionQueued = Detail::QueueSpiSlave(SpiHostValue, Transaction, TransmitWindow, ReceiveWindow, SpiTransactionWindowBytes);
	if (!bTransactionQueued)
	{
		MW_LOG_MSG(Warning, "Spi", "slave failed to queue a transaction");
	}
}

ENetResult FEsp32SpiSlaveDriver::TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	const ENetResult Validation = ValidateOutgoingSpiPacket(InTo, InPacket);
	if (Validation != ENetResult::Success)
	{
		return Validation;
	}
	// A staged frame not yet queued must not be overwritten; report Full so the caller retries.
	if (bFrameStaged)
	{
		return ENetResult::Full;
	}
	// The codec is transactional on failure; pad the window's tail with idle bytes.
	std::size_t Written = 0;
	const ENetResult EncodeResult = EncodeFrame(LocalNodeIdValue, InPacket, TSpan<std::uint8_t>(StagedFrame, SpiTransactionWindowBytes), Written);
	if (EncodeResult != ENetResult::Success)
	{
		return EncodeResult;
	}
	std::memset(StagedFrame + Written, 0, SpiTransactionWindowBytes - Written);
	bFrameStaged = true;
	return ENetResult::Success;
}

ENetResult FEsp32SpiSlaveDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before touching the transaction queue.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	// Recover if a prior queue attempt failed, so the master always has a buffer to clock.
	if (!bTransactionQueued)
	{
		QueueNextTransaction();
	}
	// Harvest one completed transaction (the master clocked it); nothing done yet means Unavailable.
	if (!Detail::HarvestSpiSlave(SpiHostValue))
	{
		return ENetResult::Unavailable;
	}
	bTransactionQueued = false;
	PumpWindowIntoDecoder(Decoder, ReceiveWindow, SpiTransactionWindowBytes);
	// Re-queue immediately so the next master clock finds a buffer (with any freshly staged reply).
	QueueNextTransaction();
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	return ENetResult::Unavailable;
}

std::size_t FEsp32SpiSlaveDriver::MaxPacketBytes() const noexcept
{
	return SpiMaxPayloadBytes;
}

bool FEsp32SpiSlaveDriver::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld
