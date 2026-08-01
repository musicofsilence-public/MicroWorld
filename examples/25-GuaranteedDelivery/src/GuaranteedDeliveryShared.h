#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/NameId.h>

#include <cstdint>

/**
 * Motivation: Shares Messaging names, WiFi/UDP configuration, and the engine alias between both roles.
 * Responsibilities: Define the channel and message names, endpoint values, and engine alias exactly once
 *   (DRY), so both translation units agree without restating them.
 */
namespace Ex25
{
/** Motivation: Names the channel that demonstrates one-shot delivery under injected loss. */
inline constexpr MicroWorld::Messaging::FNameId BestEffortChannelName = MicroWorld::Messaging::MakeNameId("BestEffort");

/** Motivation: Names the channel that retries counter values until its peer acknowledges them. */
inline constexpr MicroWorld::Messaging::FNameId GuaranteedChannelName = MicroWorld::Messaging::MakeNameId("Guaranteed");

/** Motivation: Names the shared one-byte counter payload carried by both reliability settings. */
inline constexpr MicroWorld::Messaging::FNameId CounterMessageName = MicroWorld::Messaging::MakeNameId("Counter");

/** Motivation: First and last counter value the client sends on both channels (inclusive). */
inline constexpr std::uint8_t FirstCounterValue = 1;
inline constexpr std::uint8_t LastCounterValue = 30;

/** Motivation: Names how many distinct counter values each channel can deliver during one trace. */
inline constexpr std::uint8_t CounterValueCount = LastCounterValue - FirstCounterValue + 1;

// The ledger tracks which values arrived in one uint32 bit per value, so widening the counter range past
// 32 would silently shift out of that mask rather than fail.
static_assert(CounterValueCount <= 32, "FLedgerActor's arrival bitmask holds one bit per counter value");

/** Motivation: How often the client emits the next counter value on both channels. */
inline constexpr MicroWorld::Core::DurationMilliseconds CounterIntervalMilliseconds = 500;

/** Motivation: The client drops every third outgoing packet through this injector (Task 5.1). */
inline constexpr std::uint32_t DropEveryNthSend = 3;

/** Motivation: Poll pace for both boards; far faster than any app cadence so the watchdog idle task runs. */
inline constexpr unsigned PollPacingMilliseconds = 20;

/** Motivation: SoftAP the server hosts and the client joins -- demo-only values, not a secret. */
constexpr const char* DemoApSsid = "microworld-ex25";

/** Motivation: Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** Motivation: The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** Motivation: UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Motivation: The SoftAP hands its first station this address; the server must name it to acknowledge. */
constexpr std::uint8_t ClientIpv4[4] = {192, 168, 4, 2};

/** Motivation: Fixed client port, so a reliable channel's acknowledgement has a destination to reach. */
constexpr std::uint16_t ClientPort = 40405;

/** Motivation: Stable descriptor id for the managed FCounterActor type (0x0019 == example 25). */
constexpr MicroWorld::Engine::FTypeId CounterActorTypeId{0x00190001u};

/** Motivation: Stable descriptor id for the managed FLedgerActor type. */
constexpr MicroWorld::Engine::FTypeId LedgerActorTypeId{0x00190002u};

/** Motivation: The engine both roles compose; sized for one world with a couple of small actors using direct component storage. */
using FWorldEngine = MicroWorld::Engine::TEngine<>;
} // namespace Ex25
