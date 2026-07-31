#include <MicroWorld/Transport/E32Lora.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Pico/PicoE32LoraDriver.h>

#include <FreeRTOS.h>
#include <task.h>

#include <cstddef>
#include <cstdint>

#if defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
#include "LoraPayloadRegression.h"
#endif

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

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Preserves the node-id plus big-endian counter payload shared with example 17. */
constexpr std::size_t VolleyPayloadBytes = 5;

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
#endif

/** Owns FreeRTOS metadata for the one LoRa task. */
StaticTask_t LoraTaskControlBlock;

/** Owns the static stack consumed by the LoRa task. */
StackType_t LoraTaskStack[LoraTaskStackDepth];

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Checks one decoded packet is the expected five-byte reply from ESP32 node B. */
bool IsExpectedPeerPayload(
	const MicroWorld::FDeviceAddress& InFrom,
	const MicroWorld::FReceiveResult& InResult,
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

		MicroWorld::FDeviceAddress From{};
		MicroWorld::FReceiveResult Received{};
		const MicroWorld::ETransportResult ReceiveResult =
			LoraDriver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(ReceiveBuffer, sizeof(ReceiveBuffer)), Received);
		const TickType_t Now = xTaskGetTickCount();
		if (ReceiveResult == MicroWorld::ETransportResult::Success && IsExpectedPeerPayload(From, Received, ReceiveBuffer))
		{
			bHasPendingTransmit = true;
			PendingCounter = ReadVolleyCounter(ReceiveBuffer) + 1u;
			PendingTransmitDue = Now + VolleyPeriodTicks;
		}

		if (bHasPendingTransmit && IsTickDue(Now, PendingTransmitDue))
		{
			std::uint8_t Payload[VolleyPayloadBytes]{};
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::ETransportResult SendResult =
				LoraDriver.TrySend(MicroWorld::MakeLoraAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			if (SendResult == MicroWorld::ETransportResult::Success)
			{
				// Success transfers the complete frame into the driver slot; later task iterations advance it onto UART.
				bHasPendingTransmit = false;
			}
		}

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}
#else
/** Distinguishes the Pico's bounded regression exchanges so only the expected peer frame can advance them. */
enum class EPayloadRegressionState : std::uint8_t
{
	SendEmpty,
	AwaitEmptyEcho,
	SendTypical,
	AwaitTypicalEcho,
	AwaitMaximum,
	EchoMaximum
};

/** Requires an empty-frame retry to wait one complete transparent-radio airtime period. */
constexpr TickType_t EmptyRetryPeriodTicks = VolleyPeriodTicks;

/** Checks the sender address, exact length, and canonical bytes before accepting one regression frame. */
bool IsExpectedRegressionPayload(
	const MicroWorld::FDeviceAddress& InFrom,
	const MicroWorld::FReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::E32MaxPayloadBytes],
	const MicroWorld::Example17::EPayloadRegressionCase InExpectedCase) noexcept
{
	return MicroWorld::IsLoraAddress(InFrom) && MicroWorld::LoraAddressNodeId(InFrom) == PeerNodeId
		&& MicroWorld::Example17::IsCanonicalPayload(InExpectedCase, InPayload, InResult.BytesReceived);
}

/** Advances only when the received peer frame matches the payload case expected by the current exchange state. */
void AdvancePayloadRegressionReceiveState(
	EPayloadRegressionState& InOutState,
	const MicroWorld::FDeviceAddress& InFrom,
	const MicroWorld::FReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::E32MaxPayloadBytes]) noexcept
{
	switch (InOutState)
	{
		case EPayloadRegressionState::AwaitEmptyEcho:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Empty))
			{
				InOutState = EPayloadRegressionState::SendTypical;
			}
			return;
		case EPayloadRegressionState::AwaitTypicalEcho:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Typical))
			{
				InOutState = EPayloadRegressionState::AwaitMaximum;
			}
			return;
		case EPayloadRegressionState::AwaitMaximum:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Maximum))
			{
				InOutState = EPayloadRegressionState::EchoMaximum;
			}
			return;
		default:
			return;
	}
}

/** Queues one canonical frame and advances only after the driver accepted it into its bounded transmit slot. */
void QueuePendingPayloadRegressionTransmit(
	MicroWorld::FPicoE32LoraDriver& InDriver,
	EPayloadRegressionState& InOutState,
	TickType_t& OutEmptyRetryDue,
	const TickType_t InNow,
	std::uint8_t (&InOutPayloadBuffer)[MicroWorld::E32MaxPayloadBytes]) noexcept
{
	MicroWorld::Example17::EPayloadRegressionCase TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Empty;
	switch (InOutState)
	{
		case EPayloadRegressionState::SendEmpty:
			break;
		case EPayloadRegressionState::SendTypical:
			TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Typical;
			break;
		case EPayloadRegressionState::EchoMaximum:
			TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Maximum;
			break;
		default:
			return;
	}

	MicroWorld::Example17::FillCanonicalPayload(TransmitCase, InOutPayloadBuffer);
	const std::size_t PayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(TransmitCase);
	const MicroWorld::ETransportResult SendResult =
		InDriver.TrySend(MicroWorld::MakeLoraAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(InOutPayloadBuffer, PayloadBytes));
	if (SendResult != MicroWorld::ETransportResult::Success)
	{
		return;
	}

	switch (InOutState)
	{
		case EPayloadRegressionState::SendEmpty:
			InOutState = EPayloadRegressionState::AwaitEmptyEcho;
			OutEmptyRetryDue = InNow + EmptyRetryPeriodTicks;
			return;
		case EPayloadRegressionState::SendTypical:
			InOutState = EPayloadRegressionState::AwaitTypicalEcho;
			return;
		case EPayloadRegressionState::EchoMaximum:
			InOutState = EPayloadRegressionState::AwaitMaximum;
			return;
		default:
			return;
	}
}

/** Drives the Pico peer through the fixed empty, typical, and maximum E32 payload exchange. */
void RunLoraInteropTask(void* const InContext)
{
	auto& LoraDriver = *static_cast<MicroWorld::FPicoE32LoraDriver*>(InContext);
	std::uint8_t PayloadBuffer[MicroWorld::E32MaxPayloadBytes]{};
	EPayloadRegressionState State = EPayloadRegressionState::SendEmpty;
	TickType_t EmptyRetryDue = xTaskGetTickCount();

	for (;;)
	{
		MicroWorld::FDeviceAddress From{};
		MicroWorld::FReceiveResult Received{};
		const MicroWorld::ETransportResult ReceiveResult =
			LoraDriver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(PayloadBuffer, sizeof(PayloadBuffer)), Received);
		if (ReceiveResult == MicroWorld::ETransportResult::Success)
		{
			AdvancePayloadRegressionReceiveState(State, From, Received, PayloadBuffer);
		}

		const TickType_t Now = xTaskGetTickCount();
		if (State == EPayloadRegressionState::AwaitEmptyEcho && IsTickDue(Now, EmptyRetryDue))
		{
			State = EPayloadRegressionState::SendEmpty;
		}

		QueuePendingPayloadRegressionTransmit(LoraDriver, State, EmptyRetryDue, Now, PayloadBuffer);
		LoraDriver.AdvanceTransmit();

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}
#endif

} // namespace

/** Initializes the reusable driver, creates the static LoRa task, and transfers control to FreeRTOS. */
int main()
{
	MicroWorld::FPicoE32LoraDriver LoraDriver;
	if (LoraDriver.Initialize(LoraConfig) != MicroWorld::ETransportResult::Success)
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
