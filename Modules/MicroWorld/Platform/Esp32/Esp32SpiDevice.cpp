#include <MicroWorld/Platform/Esp32/Esp32SpiDevice.h>

#include "Internal/SpiPlatformImplementation.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace MicroWorld::Platform::Esp32
{

static_assert(
	sizeof(spi_slave_transaction_t) <= SpiSlaveTransactionStorageBytes,
	"SpiSlaveTransactionStorageBytes must hold one spi_slave_transaction_t; grow it.");
static_assert(alignof(spi_slave_transaction_t) <= 8, "TransactionStorage alignment must cover spi_slave_transaction_t.");

namespace
{

	/**
	 * Motivation: Translates one ESP-IDF-normalized SPI transmit outcome into the shared device result so the
	 *   device body never inspects platform codes.
	 * Responsibilities: Map Sent to Success, WouldBlock to Full, and anything else to Invalid.
	 */
	Transport::ETransportResult MapSpiTransmitOutcome(const ESpiTransmitOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case ESpiTransmitOutcome::Sent:
				return Transport::ETransportResult::Success;
			case ESpiTransmitOutcome::WouldBlock:
				return Transport::ETransportResult::Full;
			case ESpiTransmitOutcome::Error:
			default:
				return Transport::ETransportResult::Invalid;
		}
	}

	/**
	 * Motivation: Guards a send against a malformed address, oversize packet, or null span before any syscall so
	 *   a rejection is truly transactional.
	 * Responsibilities: Return the first reason an outgoing packet cannot be framed and sent, or Success.
	 */
	Transport::ETransportResult ValidateOutgoingSpiPacket(
		const Transport::Address::FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		// Validate every argument before any syscall so a rejection is truly transactional.
		if (!IsSpiAddress(InTo))
		{
			return Transport::ETransportResult::Invalid;
		}
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize > SpiMaxPayloadBytes)
		{
			return Transport::ETransportResult::Invalid;
		}
		if (PacketSize != 0 && InPacket.Data() == nullptr)
		{
			return Transport::ETransportResult::Invalid;
		}
		return Transport::ETransportResult::Success;
	}

	/**
	 * Motivation: Moves the decoder's held frame into the caller's destination so a completed frame is delivered
	 *   in one transactional step.
	 * Responsibilities: Copy the payload, byte count, and sender node id and clear the held frame, or return Full
	 *   (leaving the frame held) when the payload exceeds the destination.
	 */
	Transport::ETransportResult DeliverFrameFromDecoder(
		Transport::FrameCodec::TFrameDecoder<SpiMaxPayloadBytes>& InDecoder,
		Core::TSpan<std::uint8_t> InDestination,
		Transport::Address::FDeviceAddress& OutFrom,
		Transport::Device::FReceiveResult& OutResult) noexcept
	{
		// On Full the destination is untouched and the frame stays held for the next call, so a
		// receive that cannot fit is transactional.
		const std::size_t HeldLength = InDecoder.FramePayload().Size();
		if (HeldLength > InDestination.Size())
		{
			return Transport::ETransportResult::Full;
		}
		std::memcpy(InDestination.Data(), InDecoder.FramePayload().Data(), HeldLength);
		OutFrom = MakeSpiAddress(InDecoder.FrameNodeId());
		OutResult.BytesReceived = HeldLength;
		InDecoder.ClearFrame();
		return Transport::ETransportResult::Success;
	}

	/**
	 * Motivation: Drains one full-duplex received window into the decoder so every received byte is processed until
	 *   a frame completes.
	 * Responsibilities: Push bytes until the first completed frame appears, then stop; the caller must ensure the
	 *   decoder holds no frame before calling.
	 */
	void PumpWindowIntoDecoder(
		Transport::FrameCodec::TFrameDecoder<SpiMaxPayloadBytes>& InDecoder, const std::uint8_t* const InWindow, const std::size_t InLength) noexcept
	{
		for (std::size_t Index = 0; Index < InLength; ++Index)
		{
			const Transport::FrameCodec::EFrameEvent Event = InDecoder.PushByte(InWindow[Index]);
			if (Event == Transport::FrameCodec::EFrameEvent::FrameReady)
			{
				return;
			}
			if (Event == Transport::FrameCodec::EFrameEvent::Discarded)
			{
				MW_LOG_MSG(Warning, "Spi", "decoder discarded a candidate frame (bad length or CRC)");
			}
		}
	}

} // namespace

FEsp32SpiMasterDevice::FEsp32SpiMasterDevice(const FEsp32SpiMasterConfig& InConfig) noexcept
{
	const FOpenedSpiMaster Opened =
		OpenConfiguredSpiMaster(InConfig.SpiHost, InConfig.MosiGpio, InConfig.MisoGpio, InConfig.SclkGpio, InConfig.CsGpio, InConfig.ClockHz);
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

FEsp32SpiMasterDevice::~FEsp32SpiMasterDevice() noexcept
{
	if (bOpen)
	{
		CloseSpiMaster(SpiHostValue, static_cast<spi_device_handle_t>(DeviceHandle));
	}
}

Transport::ETransportResult FEsp32SpiMasterDevice::ExchangeAndPump(const std::uint8_t* const InTransmitWindow) noexcept
{
	const ESpiTransmitOutcome Outcome =
		TransmitSpiMaster(static_cast<spi_device_handle_t>(DeviceHandle), InTransmitWindow, ReceiveWindow, SpiTransactionWindowBytes);
	const Transport::ETransportResult Result = MapSpiTransmitOutcome(Outcome);
	// SPI is full-duplex: the received window holds the slave's simultaneous output, so feed it to the
	// decoder rather than discard it. Only pump when no frame is already held (the decoder precondition).
	if (Result == Transport::ETransportResult::Success && !Decoder.HasFrame())
	{
		PumpWindowIntoDecoder(Decoder, ReceiveWindow, SpiTransactionWindowBytes);
	}
	return Result;
}

Transport::ETransportResult FEsp32SpiMasterDevice::TrySend(
	const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	const Transport::ETransportResult Validation = ValidateOutgoingSpiPacket(InTo, InPacket);
	if (Validation != Transport::ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure; pad the window's tail with idle bytes the peer's decoder ignores.
	std::size_t Written = 0;
	const Transport::ETransportResult EncodeResult =
		Transport::FrameCodec::EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(TransmitWindow, SpiTransactionWindowBytes), Written);
	if (EncodeResult != Transport::ETransportResult::Success)
	{
		return EncodeResult;
	}
	std::memset(TransmitWindow + Written, 0, SpiTransactionWindowBytes - Written);
	return ExchangeAndPump(TransmitWindow);
}

Transport::ETransportResult FEsp32SpiMasterDevice::TryReceive(
	Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any bus transaction.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	// One idle full-duplex transaction clocks in whatever the slave has staged (ADR Appendix B).
	const Transport::ETransportResult Exchange = ExchangeAndPump(IdleWindow);
	if (Exchange == Transport::ETransportResult::Invalid)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	return Transport::ETransportResult::Unavailable;
}

std::size_t FEsp32SpiMasterDevice::MaxPacketBytes() const noexcept
{
	return SpiMaxPayloadBytes;
}

bool FEsp32SpiMasterDevice::IsOpen() const noexcept
{
	return bOpen;
}

FEsp32SpiSlaveDevice::FEsp32SpiSlaveDevice(const FEsp32SpiSlaveConfig& InConfig) noexcept
{
	const FOpenedSpiSlave Opened = OpenConfiguredSpiSlave(InConfig.SpiHost, InConfig.MosiGpio, InConfig.MisoGpio, InConfig.SclkGpio, InConfig.CsGpio);
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

FEsp32SpiSlaveDevice::~FEsp32SpiSlaveDevice() noexcept
{
	if (bOpen)
	{
		CloseSpiSlave(SpiHostValue);
	}
}

void FEsp32SpiSlaveDevice::QueueNextTransaction() noexcept
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
	bTransactionQueued = QueueSpiSlave(SpiHostValue, Transaction, TransmitWindow, ReceiveWindow, SpiTransactionWindowBytes);
	if (!bTransactionQueued)
	{
		MW_LOG_MSG(Warning, "Spi", "slave failed to queue a transaction");
	}
}

Transport::ETransportResult FEsp32SpiSlaveDevice::TrySend(
	const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	const Transport::ETransportResult Validation = ValidateOutgoingSpiPacket(InTo, InPacket);
	if (Validation != Transport::ETransportResult::Success)
	{
		return Validation;
	}
	// A staged frame not yet queued must not be overwritten; report Full so the caller retries.
	if (bFrameStaged)
	{
		return Transport::ETransportResult::Full;
	}
	// The codec is transactional on failure; pad the window's tail with idle bytes.
	std::size_t Written = 0;
	const Transport::ETransportResult EncodeResult =
		Transport::FrameCodec::EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(StagedFrame, SpiTransactionWindowBytes), Written);
	if (EncodeResult != Transport::ETransportResult::Success)
	{
		return EncodeResult;
	}
	std::memset(StagedFrame + Written, 0, SpiTransactionWindowBytes - Written);
	bFrameStaged = true;
	return Transport::ETransportResult::Success;
}

Transport::ETransportResult FEsp32SpiSlaveDevice::TryReceive(
	Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before touching the transaction queue.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
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
	if (!HarvestSpiSlave(SpiHostValue))
	{
		return Transport::ETransportResult::Unavailable;
	}
	bTransactionQueued = false;
	PumpWindowIntoDecoder(Decoder, ReceiveWindow, SpiTransactionWindowBytes);
	// Re-queue immediately so the next master clock finds a buffer (with any freshly staged reply).
	QueueNextTransaction();
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	return Transport::ETransportResult::Unavailable;
}

std::size_t FEsp32SpiSlaveDevice::MaxPacketBytes() const noexcept
{
	return SpiMaxPayloadBytes;
}

bool FEsp32SpiSlaveDevice::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld::Platform::Esp32
