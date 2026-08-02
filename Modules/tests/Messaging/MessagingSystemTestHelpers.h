#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Messaging/DefaultMessagingTraits.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>

#include <cstddef>

namespace MicroWorld::Tests
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::TMessagingSystem;

/** Motivation: Names the default Messaging system for reusable local-delivery test recorders. */
using FDefaultMessagingSystem = TMessagingSystem<>;

/** Motivation: Names the bounded default subscriber delegate without repeating its system-qualified declaration. */
using FDefaultSubscriberDelegate = FDefaultMessagingSystem::FSubscriberDelegate;

/**
 * Motivation: Provides a non-owning device pointer for channel creation tests.
 * Responsibilities: Satisfy the transport device contract without retaining packets or performing I/O.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Lets a test count a channel's outbound frames without modelling a medium.
	 * Responsibilities: Record each send request and report success without retaining the supplied packet or destination.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override
	{
		++TrySendCallCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Completes the transport contract without modelling inbound packets for channel creation tests.
	 * Responsibilities: Report no packet available without changing output values.
	 */
	ETransportResult TryReceive(FDeviceAddress&, TSpan<std::uint8_t>, FReceiveResult&) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: Supplies a bounded device packet size without requiring hardware state.
	 * Responsibilities: Return the fixed test packet byte limit.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return 64; }

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no pre-advance work.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no post-advance work.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a test assert exactly how many remote transmissions one send initiated.
	 * Responsibilities: Return the number of TrySend requests observed without changing the device.
	 */
	std::size_t GetTrySendCallCount() const noexcept { return TrySendCallCount; }

private:
	/** Motivation: Records every remote transmission this device was asked to make. */
	std::size_t TrySendCallCount{0};
};

} // namespace MicroWorld::Tests
