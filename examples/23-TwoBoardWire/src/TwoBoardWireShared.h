#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include <cstdint>

/**
 * Motivation: Holds the shared channel and message names, the UART configuration, and the engine alias
 *   for example 23's two roles, so both translation units define them exactly once (DRY).
 * Responsibilities: Define the shared names, node and pin values, and the engine shape both roles use.
 */
namespace Ex23
{
/** Motivation: Names the single shared UART channel for this point-to-point application wire. */
inline constexpr MicroWorld::Messaging::FNameId AppChannelName = MicroWorld::Messaging::MakeNameId("App");

/** Motivation: Names the switch's 1-byte lamp command (0 = off, 1 = on). */
inline constexpr MicroWorld::Messaging::FNameId SetLampStateMessageName = MicroWorld::Messaging::MakeNameId("SetLampState");

/** Motivation: Names the switch's 1-byte application-level heartbeat counter. */
inline constexpr MicroWorld::Messaging::FNameId HeartbeatCountMessageName = MicroWorld::Messaging::MakeNameId("HeartbeatCount");

// The two message-name filters on one channel are the whole of this demo's addressing, so the actor ids
// and the wire channel byte both retired with no replacement.

/** Motivation: Supplies the local server node value required by the UART configuration. */
constexpr std::uint8_t ServerNodeId = 1;

/** Motivation: Supplies the local client node value required by the UART configuration. */
constexpr std::uint8_t ClientNodeId = 2;

/** Motivation: Fixed UART port and the two crossover data GPIOs, identical to examples 18 and 19. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

/** Motivation: Poll pace for both boards; far faster than the 2 s switch cadence so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Motivation: Stable descriptor id for the managed FLampActor type (0x0017 == example 23). */
constexpr MicroWorld::Engine::FTypeId LampActorTypeId{0x00170001u};

/** Motivation: Stable descriptor id for the managed FDisplayActor type. */
constexpr MicroWorld::Engine::FTypeId DisplayActorTypeId{0x00170002u};

/** Motivation: Stable descriptor id for the managed FSwitchActor type. */
constexpr MicroWorld::Engine::FTypeId SwitchActorTypeId{0x00170003u};

/** Motivation: The engine both roles compose; sized for one world with a couple of small actors using direct component storage. */
using FWireEngine = MicroWorld::Engine::TEngine<>;

/**
 * Motivation: Lets both roles build a board's UART device configuration from one source, so the fixed
 *   pins and baud are never restated.
 * Responsibilities: Fill the UART config with the shared port, GPIO, baud, and node id values.
 */
inline MicroWorld::Platform::Esp32::FEsp32UartConfig MakeUartConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::Platform::Esp32::FEsp32UartConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

} // namespace Ex23
