#include <MicroWorld/Platform/Esp32/Esp32I2cDevice.h>

#include "Internal/I2cPlatformImplementation.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Platform::Esp32
{

void FI2cReceiveInbox::PushFromIsr(const std::uint8_t InByte) noexcept
{
	const std::uint32_t Write = WriteIndex;
	const std::uint32_t Next = static_cast<std::uint32_t>((Write + 1u) % Capacity);
	if (Next == ReadIndex)
	{
		return; // Ring full: drop the byte; the decoder's resync tolerates the loss.
	}
	Bytes[Write] = InByte;
	WriteIndex = Next;
}

bool FI2cReceiveInbox::Pop(std::uint8_t& OutByte) noexcept
{
	const std::uint32_t Read = ReadIndex;
	if (Read == WriteIndex)
	{
		return false; // Ring empty.
	}
	OutByte = Bytes[Read];
	ReadIndex = static_cast<std::uint32_t>((Read + 1u) % Capacity);
	return true;
}

namespace
{

	/** Maps one I2C write outcome to the shared device result (mirrors the UART device's mapping). */
	ETransportResult MapI2cWriteOutcome(const EI2cWriteOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case EI2cWriteOutcome::Sent:
				return ETransportResult::Success;
			case EI2cWriteOutcome::WouldBlock:
				return ETransportResult::Full;
			case EI2cWriteOutcome::Error:
			default:
				return ETransportResult::Invalid;
		}
	}

	/** Reports the first reason an outgoing packet cannot be framed and sent, or `Success`. */
	ETransportResult ValidateOutgoingI2cPacket(const FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		// Validate every argument before any syscall so a rejection is truly transactional.
		if (!IsI2cAddress(InTo))
		{
			return ETransportResult::Invalid;
		}
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize > I2cMaxPayloadBytes)
		{
			return ETransportResult::Invalid;
		}
		if (PacketSize != 0 && InPacket.Data() == nullptr)
		{
			return ETransportResult::Invalid;
		}
		return ETransportResult::Success;
	}

	/** Copies the decoder's held frame into the destination and clears it, or returns `Full`
	 * (leaving the frame held) when the payload exceeds the destination. */
	ETransportResult DeliverFrameFromDecoder(
		TFrameDecoder<I2cMaxPayloadBytes>& InDecoder,
		Core::TSpan<std::uint8_t> InDestination,
		FDeviceAddress& OutFrom,
		FReceiveResult& OutResult) noexcept
	{
		// On Full the destination is untouched and the frame stays held for the next call, so a
		// receive that cannot fit is transactional.
		const std::size_t HeldLength = InDecoder.FramePayload().Size();
		if (HeldLength > InDestination.Size())
		{
			return ETransportResult::Full;
		}
		std::memcpy(InDestination.Data(), InDecoder.FramePayload().Data(), HeldLength);
		OutFrom = MakeI2cAddress(InDecoder.FrameNodeId());
		OutResult.BytesReceived = HeldLength;
		InDecoder.ClearFrame();
		return ETransportResult::Success;
	}

} // namespace

FEsp32I2cMasterDevice::FEsp32I2cMasterDevice(const FEsp32I2cMasterConfig& InConfig) noexcept
{
	const FOpenedI2cMaster Opened =
		OpenConfiguredI2cMaster(InConfig.I2cPort, InConfig.SdaGpio, InConfig.SclGpio, InConfig.SclSpeedHz, InConfig.SlaveAddress);
	if (!Opened.bOpen)
	{
		BusHandle = nullptr;
		DeviceHandle = nullptr;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	BusHandle = Opened.Bus;
	DeviceHandle = Opened.Dev;
	LocalNodeIdValue = InConfig.LocalNodeId;
	bOpen = true;
}

FEsp32I2cMasterDevice::~FEsp32I2cMasterDevice() noexcept
{
	if (bOpen)
	{
		CloseI2cMaster(static_cast<i2c_master_bus_handle_t>(BusHandle), static_cast<i2c_master_dev_handle_t>(DeviceHandle));
	}
}

ETransportResult FEsp32I2cMasterDevice::TrySend(const FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	const ETransportResult Validation = ValidateOutgoingI2cPacket(InTo, InPacket);
	if (Validation != ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[I2cTransactionWindowBytes];
	std::size_t Written = 0;
	const ETransportResult EncodeResult = EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != ETransportResult::Success)
	{
		return EncodeResult;
	}
	const EI2cWriteOutcome Outcome = WriteI2cMaster(static_cast<i2c_master_dev_handle_t>(DeviceHandle), Frame, Written);
	return MapI2cWriteOutcome(Outcome);
}

ETransportResult FEsp32I2cMasterDevice::TryReceive(
	FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any bus read.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	// One bounded read transaction harvests a whole-frame window, then its bytes are pumped (ADR Appendix A5).
	std::uint8_t Window[I2cTransactionWindowBytes];
	const EI2cReadOutcome Outcome = ReadI2cMaster(static_cast<i2c_master_dev_handle_t>(DeviceHandle), Window, sizeof(Window));
	if (Outcome == EI2cReadOutcome::WouldBlock)
	{
		return ETransportResult::Unavailable;
	}
	if (Outcome == EI2cReadOutcome::Error)
	{
		return ETransportResult::Invalid;
	}
	for (std::size_t Index = 0; Index < sizeof(Window); ++Index)
	{
		const EFrameEvent Event = Decoder.PushByte(Window[Index]);
		if (Event == EFrameEvent::FrameReady)
		{
			// A completed frame is delivered immediately; Full keeps it held for the next call.
			return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
		}
		if (Event == EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "I2c", "master decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return ETransportResult::Unavailable;
}

std::size_t FEsp32I2cMasterDevice::MaxPacketBytes() const noexcept
{
	return I2cMaxPayloadBytes;
}

bool FEsp32I2cMasterDevice::IsOpen() const noexcept
{
	return bOpen;
}

FEsp32I2cSlaveDevice::FEsp32I2cSlaveDevice(const FEsp32I2cSlaveConfig& InConfig) noexcept
{
	const FOpenedI2cSlave Opened = OpenConfiguredI2cSlave(InConfig.I2cPort, InConfig.SdaGpio, InConfig.SclGpio, InConfig.SlaveAddress, Inbox);
	if (!Opened.bOpen)
	{
		SlaveHandle = nullptr;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	SlaveHandle = Opened.Dev;
	LocalNodeIdValue = InConfig.LocalNodeId;
	bOpen = true;
}

FEsp32I2cSlaveDevice::~FEsp32I2cSlaveDevice() noexcept
{
	if (bOpen)
	{
		CloseI2cSlave(static_cast<i2c_slave_dev_handle_t>(SlaveHandle));
	}
}

ETransportResult FEsp32I2cSlaveDevice::TrySend(const FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	const ETransportResult Validation = ValidateOutgoingI2cPacket(InTo, InPacket);
	if (Validation != ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[I2cTransactionWindowBytes];
	std::size_t Written = 0;
	const ETransportResult EncodeResult = EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != ETransportResult::Success)
	{
		return EncodeResult;
	}
	const EI2cWriteOutcome Outcome = WriteI2cSlave(static_cast<i2c_slave_dev_handle_t>(SlaveHandle), Frame, Written);
	return MapI2cWriteOutcome(Outcome);
}

ETransportResult FEsp32I2cSlaveDevice::TryReceive(
	FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any inbox read.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
	}
	// Drain the ISR-filled inbox one byte at a time, bounded so a flood cannot starve the caller (ADR Appendix A1).
	const std::size_t PumpByteCap = 2u * I2cTransactionWindowBytes;
	for (std::size_t Pumped = 0; Pumped < PumpByteCap; ++Pumped)
	{
		std::uint8_t IncomingByte = 0;
		if (!Inbox.Pop(IncomingByte))
		{
			break;
		}
		const EFrameEvent Event = Decoder.PushByte(IncomingByte);
		if (Event == EFrameEvent::FrameReady)
		{
			// A completed frame is delivered immediately; Full keeps it held for the next call.
			return DeliverFrameFromDecoder(Decoder, InDestination, OutFrom, OutResult);
		}
		if (Event == EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "I2c", "slave decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return ETransportResult::Unavailable;
}

std::size_t FEsp32I2cSlaveDevice::MaxPacketBytes() const noexcept
{
	return I2cMaxPayloadBytes;
}

bool FEsp32I2cSlaveDevice::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld::Platform::Esp32
