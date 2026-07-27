#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformPico/PicoE32LoraDriver.h>

#include <FreeRTOS.h>
#include <task.h>

#include <cstddef>
#include <cstdint>

/** Satisfies virtual deleting-destructor linkage; the LoRa driver is never dynamically deleted. */
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

/** Selects RP2040 UART1, whose supported GP4/GP5 routing matches the wired Pico H. */
constexpr std::uint8_t LoraUartIndex = 1;

/** Matches the E32-433T20D factory UART rate used by the paired ESP32 example. */
constexpr std::uint32_t LoraBaudRate = 9600;

/** Routes Pico UART1 transmit output to the wired E32 RXD pin. */
constexpr unsigned int LoraTransmitPin = 4;

/** Routes Pico UART1 receive input from the wired E32 TXD pin. */
constexpr unsigned int LoraReceivePin = 5;

/** Preserves the node-id plus big-endian counter payload shared with example 17. */
constexpr std::size_t VolleyPayloadBytes = 5;

/** Leaves enough air time between alternating transparent-mode E32 messages. */
constexpr TickType_t VolleyPeriodTicks = pdMS_TO_TICKS(1000);

/** Paces UART polling and one-byte transmit advancement without busy-spinning the task. */
constexpr TickType_t PollPeriodTicks = pdMS_TO_TICKS(10);

/** Reserves a fixed stack for the sole LoRa task through the firmware lifetime. */
constexpr configSTACK_DEPTH_TYPE LoraTaskStackDepth = 512;

/** Fails the task before an unmeasured stack margin can silently become a radio failure. */
constexpr UBaseType_t MinimumStackHeadroomWords = 128;

/** Configures the reusable driver with the exact UART wiring proven by the Pico-to-ESP32 exchange. */
constexpr MicroWorld::FPicoE32LoraConfig LoraConfig{
	LoraUartIndex,
	LoraTransmitPin,
	LoraReceivePin,
	LoraBaudRate,
	LocalNodeId,
};

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

/** Owns FreeRTOS metadata for the one LoRa task. */
StaticTask_t LoraTaskControlBlock;

/** Owns the static stack consumed by the LoRa task. */
StackType_t LoraTaskStack[LoraTaskStackDepth];

/** Checks one decoded packet is the expected five-byte reply from ESP32 node B. */
bool IsExpectedPeerPayload(
	const MicroWorld::FNetAddress& InFrom,
	const MicroWorld::FNetReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::E32MaxPayloadBytes]) noexcept
{
	return MicroWorld::IsLoraAddress(InFrom) && MicroWorld::LoraAddressNodeId(InFrom) == PeerNodeId && InResult.BytesReceived == VolleyPayloadBytes
		&& InPayload[0] == PeerNodeId;
}

/** Drives the Pico node-1 counter volley and continuously checks its measured task-stack margin. */
void RunLoraInteropTask(void* const InContext)
{
	auto& LoraDriver = *static_cast<MicroWorld::FPicoE32LoraDriver*>(InContext);
	std::uint8_t ReceiveBuffer[MicroWorld::E32MaxPayloadBytes]{};
	bool bHasPendingTransmit = true;
	std::uint32_t PendingCounter = 1;
	TickType_t PendingTransmitDue = xTaskGetTickCount() + VolleyPeriodTicks;

	for (;;)
	{
		LoraDriver.AdvanceTransmit();

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
				LoraDriver.TrySend(MicroWorld::MakeLoraAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			if (SendResult == MicroWorld::ENetResult::Success)
			{
				// Success transfers the complete frame into the driver slot; later task iterations advance it onto UART.
				bHasPendingTransmit = false;
			}
		}

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}

} // namespace

/** Initializes the reusable driver, creates the static LoRa task, and transfers control to FreeRTOS. */
int main()
{
	MicroWorld::FPicoE32LoraDriver LoraDriver;
	if (LoraDriver.Initialize(LoraConfig) != MicroWorld::ENetResult::Success)
	{
		return 1;
	}

	TaskHandle_t LoraTask = xTaskCreateStatic(
		RunLoraInteropTask, "MicroWorldLora", LoraTaskStackDepth, &LoraDriver, tskIDLE_PRIORITY + 1, LoraTaskStack, &LoraTaskControlBlock);
	if (LoraTask == nullptr)
	{
		return 2;
	}

	vTaskStartScheduler();
	return 3;
}
