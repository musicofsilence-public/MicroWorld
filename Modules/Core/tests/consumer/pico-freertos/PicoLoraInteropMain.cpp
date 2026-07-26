#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetDriver.h>

#include <FreeRTOS.h>
#include <task.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

/** Satisfies virtual deleting-destructor linkage; the LoRa driver is static and is never dynamically deleted. */
void operator delete(void*) noexcept {}

/** Satisfies array deleting-destructor linkage without enabling an allocator for this static-only firmware. */
void operator delete[](void*) noexcept {}

/** Satisfies sized virtual deleting-destructor linkage; static driver storage is never reclaimed. */
void operator delete(void*, std::size_t) noexcept {}

/** Satisfies sized array deleting-destructor linkage without creating a heap dependency. */
void operator delete[](void*, std::size_t) noexcept {}

namespace
{

/** Identifies the Pico as the node that starts the paired LoRa counter volley. */
constexpr std::uint8_t LocalNodeId = 1;

/** Identifies the unchanged ESP32 example-17 node-B peer. */
constexpr std::uint8_t PeerNodeId = 2;

/** Matches the E32-433T20D factory UART rate used by the paired ESP32 example. */
constexpr std::uint32_t LoraBaudRate = 9600;

/** Routes Pico UART1 transmit output to the wired E32 RXD pin. */
constexpr unsigned int LoraTransmitPin = 4;

/** Routes Pico UART1 receive input from the wired E32 TXD pin. */
constexpr unsigned int LoraReceivePin = 5;

/** Bounds each framed E32 payload to the existing ESP32 transport capacity. */
constexpr std::size_t E32MaxPayloadBytes = 58;

/** Preserves the node-id plus big-endian counter payload shared with example 17. */
constexpr std::size_t VolleyPayloadBytes = 5;

/** Leaves enough air time between alternating transparent-mode E32 messages. */
constexpr TickType_t VolleyPeriodTicks = pdMS_TO_TICKS(1000);

/** Paces UART polling and transmit advancement without busy-spinning the task. */
constexpr TickType_t PollPeriodTicks = pdMS_TO_TICKS(10);

/** Limits UART receive work performed during one `TryReceive` call. */
constexpr std::size_t ReceivePumpByteCap = 2u * (E32MaxPayloadBytes + MicroWorld::FrameOverheadBytes);

/** Reserves a fixed stack for the sole LoRa task through the firmware lifetime. */
constexpr configSTACK_DEPTH_TYPE LoraTaskStackDepth = 512;

/** Fails the task before an unmeasured stack margin can silently become a radio failure. */
constexpr UBaseType_t MinimumStackHeadroomWords = 128;

/** Encodes one local transparent-mode E32 address without depending on the ESP32 platform package. */
MicroWorld::FNetAddress MakeInteropLoraAddress(const std::uint8_t InNodeId) noexcept
{
	MicroWorld::FNetAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/** Reports whether `InNow` has reached `InDue`, including the normal tick-counter wraparound case. */
bool IsTickDue(const TickType_t InNow, const TickType_t InDue) noexcept
{
	return static_cast<std::int32_t>(InNow - InDue) >= 0;
}

/** Writes the shared five-byte node-and-counter payload without allocating. */
void WriteVolleyPayload(std::uint8_t (&OutPayload)[VolleyPayloadBytes], const std::uint8_t InNodeId, const std::uint32_t InCounter) noexcept
{
	OutPayload[0] = InNodeId;
	OutPayload[1] = static_cast<std::uint8_t>(InCounter >> 24u);
	OutPayload[2] = static_cast<std::uint8_t>(InCounter >> 16u);
	OutPayload[3] = static_cast<std::uint8_t>(InCounter >> 8u);
	OutPayload[4] = static_cast<std::uint8_t>(InCounter);
}

/** Reads the big-endian counter field after the caller validated the volley payload shape. */
std::uint32_t ReadVolleyCounter(const std::uint8_t* const InPayload) noexcept
{
	return (static_cast<std::uint32_t>(InPayload[1]) << 24u) | (static_cast<std::uint32_t>(InPayload[2]) << 16u)
		| (static_cast<std::uint32_t>(InPayload[3]) << 8u) | static_cast<std::uint32_t>(InPayload[4]);
}

/** Implements a one-frame, non-blocking E32 UART transport solely for the Pico interoperability proof. */
class FPicoE32InteropDriver final : public MicroWorld::INetDriver
{
public:
	/** Leaves the static transport inert until `Initialize` configures UART1 from `main`. */
	FPicoE32InteropDriver() noexcept = default;

	/** Configures the wired UART once and stamps the supplied node id on later outgoing frames. */
	bool Initialize(std::uint8_t InLocalNodeId) noexcept;

	/** Accepts one complete packet into the fixed transmit slot when the slot is empty. */
	MicroWorld::ENetResult TrySend(const MicroWorld::FNetAddress& InTo, MicroWorld::TSpan<const std::uint8_t> InPacket) noexcept override;

	/** Returns at most one decoded packet while preserving caller outputs on every non-success result. */
	MicroWorld::ENetResult TryReceive(
		MicroWorld::FNetAddress& OutFrom, MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept override;

	/** Reports the shared E32 payload bound rather than its larger framed UART byte count. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Advances one queued UART byte so the public Net calls remain non-blocking. */
	void PumpTransmit() noexcept;

private:
	/** Copies the decoder's held frame on success or keeps it held when the destination is too small. */
	MicroWorld::ENetResult DeliverHeldFrame(
		MicroWorld::FNetAddress& OutFrom, MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept;

	/** Pumps bounded readable UART bytes until one frame becomes deliverable or no data remains. */
	MicroWorld::ENetResult PumpReceive(
		MicroWorld::FNetAddress& OutFrom, MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept;

	/** Rejects addresses and spans that cannot form one complete E32 frame before mutating transport state. */
	MicroWorld::ENetResult ValidateOutgoingPacket(const MicroWorld::FNetAddress& InTo, MicroWorld::TSpan<const std::uint8_t> InPacket) const noexcept;

	/** Owns bounded frame assembly and one held receive frame for transactional delivery. */
	MicroWorld::TFrameDecoder<E32MaxPayloadBytes> Decoder{};

	/** Holds the one accepted outgoing frame until the UART accepts every byte. */
	std::uint8_t TransmitFrame[E32MaxPayloadBytes + MicroWorld::FrameOverheadBytes]{};

	/** Counts meaningful bytes in `TransmitFrame`; zero marks the transmit slot as available. */
	std::size_t TransmitFrameLength{0};

	/** Identifies the next queued byte awaiting non-blocking UART transmission. */
	std::size_t NextTransmitByteIndex{0};

	/** Stamps every encoded frame so ESP32 node B can identify this Pico sender. */
	std::uint8_t LocalNodeIdValue{0};

	/** Prevents UART access until initialization has configured the Pico hardware. */
	bool bOpen{false};
};

bool FPicoE32InteropDriver::Initialize(const std::uint8_t InLocalNodeId) noexcept
{
	if (bOpen)
	{
		return false;
	}

	static_cast<void>(uart_init(uart1, LoraBaudRate));
	gpio_set_function(LoraTransmitPin, GPIO_FUNC_UART);
	gpio_set_function(LoraReceivePin, GPIO_FUNC_UART);
	uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
	uart_set_hw_flow(uart1, false, false);
	uart_set_fifo_enabled(uart1, true);
	LocalNodeIdValue = InLocalNodeId;
	bOpen = true;
	return true;
}

MicroWorld::ENetResult FPicoE32InteropDriver::TrySend(
	const MicroWorld::FNetAddress& InTo, const MicroWorld::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return MicroWorld::ENetResult::Unavailable;
	}

	const MicroWorld::ENetResult Validation = ValidateOutgoingPacket(InTo, InPacket);
	if (Validation != MicroWorld::ENetResult::Success)
	{
		return Validation;
	}
	if (TransmitFrameLength != 0)
	{
		return MicroWorld::ENetResult::Full;
	}

	std::size_t WrittenBytes = 0;
	const MicroWorld::ENetResult EncodeResult =
		MicroWorld::EncodeFrame(LocalNodeIdValue, InPacket, MicroWorld::TSpan<std::uint8_t>(TransmitFrame, sizeof(TransmitFrame)), WrittenBytes);
	if (EncodeResult != MicroWorld::ENetResult::Success)
	{
		return EncodeResult;
	}

	TransmitFrameLength = WrittenBytes;
	NextTransmitByteIndex = 0;
	return MicroWorld::ENetResult::Success;
}

MicroWorld::ENetResult FPicoE32InteropDriver::TryReceive(
	MicroWorld::FNetAddress& OutFrom, const MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept
{
	if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
	{
		return MicroWorld::ENetResult::Invalid;
	}
	if (!bOpen)
	{
		return MicroWorld::ENetResult::Unavailable;
	}
	if (Decoder.HasFrame())
	{
		return DeliverHeldFrame(OutFrom, InDestination, OutResult);
	}

	return PumpReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPicoE32InteropDriver::MaxPacketBytes() const noexcept
{
	return E32MaxPayloadBytes;
}

void FPicoE32InteropDriver::PumpTransmit() noexcept
{
	if (TransmitFrameLength == 0 || !uart_is_writable(uart1))
	{
		return;
	}

	uart_putc_raw(uart1, TransmitFrame[NextTransmitByteIndex]);
	++NextTransmitByteIndex;
	if (NextTransmitByteIndex == TransmitFrameLength)
	{
		TransmitFrameLength = 0;
		NextTransmitByteIndex = 0;
	}
}

MicroWorld::ENetResult FPicoE32InteropDriver::DeliverHeldFrame(
	MicroWorld::FNetAddress& OutFrom, const MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept
{
	const MicroWorld::TSpan<const std::uint8_t> Payload = Decoder.FramePayload();
	const std::size_t PayloadBytes = Payload.Size();
	if (PayloadBytes > InDestination.Size())
	{
		return MicroWorld::ENetResult::Full;
	}

	const MicroWorld::FNetAddress Sender = MakeInteropLoraAddress(Decoder.FrameNodeId());
	if (PayloadBytes != 0)
	{
		std::memcpy(InDestination.Data(), Payload.Data(), PayloadBytes);
	}
	OutFrom = Sender;
	OutResult.BytesReceived = PayloadBytes;
	Decoder.ClearFrame();
	return MicroWorld::ENetResult::Success;
}

MicroWorld::ENetResult FPicoE32InteropDriver::PumpReceive(
	MicroWorld::FNetAddress& OutFrom, const MicroWorld::TSpan<std::uint8_t> InDestination, MicroWorld::FNetReceiveResult& OutResult) noexcept
{
	for (std::size_t PumpedBytes = 0; PumpedBytes < ReceivePumpByteCap && uart_is_readable(uart1); ++PumpedBytes)
	{
		const MicroWorld::EFrameEvent Event = Decoder.PushByte(uart_getc(uart1));
		if (Event == MicroWorld::EFrameEvent::FrameReady)
		{
			return DeliverHeldFrame(OutFrom, InDestination, OutResult);
		}
	}

	return MicroWorld::ENetResult::Unavailable;
}

MicroWorld::ENetResult FPicoE32InteropDriver::ValidateOutgoingPacket(
	const MicroWorld::FNetAddress& InTo, const MicroWorld::TSpan<const std::uint8_t> InPacket) const noexcept
{
	if (InTo.Size != 1 || InPacket.Size() > E32MaxPayloadBytes)
	{
		return MicroWorld::ENetResult::Invalid;
	}
	if (InPacket.Size() != 0 && InPacket.Data() == nullptr)
	{
		return MicroWorld::ENetResult::Invalid;
	}

	return MicroWorld::ENetResult::Success;
}

/** Retains the initialized driver and all UART/codec state for the firmware lifetime. */
FPicoE32InteropDriver LoraDriver;

/** Owns FreeRTOS metadata for the one LoRa task. */
StaticTask_t LoraTaskControlBlock;

/** Owns the static stack consumed by the LoRa task. */
StackType_t LoraTaskStack[LoraTaskStackDepth];

/** Checks one decoded packet is the expected five-byte reply from ESP32 node B. */
bool IsExpectedPeerPayload(
	const MicroWorld::FNetAddress& InFrom,
	const MicroWorld::FNetReceiveResult& InResult,
	const std::uint8_t (&InPayload)[E32MaxPayloadBytes]) noexcept
{
	return InFrom.Size == 1 && InFrom.Bytes[0] == PeerNodeId && InResult.BytesReceived == VolleyPayloadBytes && InPayload[0] == PeerNodeId;
}

/** Drives the Pico node-1 counter volley and continuously checks its measured task-stack margin. */
void RunLoraInteropTask(void*)
{
	std::uint8_t ReceiveBuffer[E32MaxPayloadBytes]{};
	bool bHasPendingTransmit = true;
	std::uint32_t PendingCounter = 1;
	TickType_t PendingTransmitDue = xTaskGetTickCount() + VolleyPeriodTicks;

	for (;;)
	{
		LoraDriver.PumpTransmit();

		MicroWorld::FNetAddress From{};
		MicroWorld::FNetReceiveResult Received{};
		const MicroWorld::ENetResult ReceiveResult =
			LoraDriver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(ReceiveBuffer, sizeof(ReceiveBuffer)), Received);
		const TickType_t Now = xTaskGetTickCount();
		if (ReceiveResult == MicroWorld::ENetResult::Success && IsExpectedPeerPayload(From, Received, ReceiveBuffer))
		{
			bHasPendingTransmit = true;
			PendingCounter = ReadVolleyCounter(ReceiveBuffer) + 1u;
			PendingTransmitDue = Now + VolleyPeriodTicks;
		}

		if (bHasPendingTransmit && IsTickDue(Now, PendingTransmitDue))
		{
			std::uint8_t Payload[VolleyPayloadBytes]{};
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::ENetResult SendResult =
				LoraDriver.TrySend(MakeInteropLoraAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			if (SendResult == MicroWorld::ENetResult::Success)
			{
				bHasPendingTransmit = false;
			}
		}

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}

} // namespace

/** Configures the static LoRa task and transfers firmware control to FreeRTOS. */
int main()
{
	if (!LoraDriver.Initialize(LocalNodeId))
	{
		return 1;
	}

	TaskHandle_t LoraTask = xTaskCreateStatic(
		RunLoraInteropTask, "MicroWorldLora", LoraTaskStackDepth, nullptr, tskIDLE_PRIORITY + 1, LoraTaskStack, &LoraTaskControlBlock);
	if (LoraTask == nullptr)
	{
		return 2;
	}

	vTaskStartScheduler();
	return 3;
}
