#pragma once

#include "MessagingConsumerProbe.h"

#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/NetworkResult.h>
#include <MicroWorld/Networking/NetworkRole.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Networking/NetworkSystemInformation.h>

static_assert(__cplusplus >= 201703L);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Networking consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Networking consumer must compile with RTTI disabled."
#endif

/**
 * Motivation: Exercises the bounded Networking composition that depends only on Messaging and Core.
 * Responsibilities: Initialize and release Network's private Messaging state without naming any Transport device.
 */
inline int RunNetworkingConsumerProbe() noexcept
{
	if (RunMessagingConsumerProbe() != 0)
	{
		return 1;
	}

	MicroWorld::Messaging::FMessagingSystem Messaging;
	MicroWorld::Networking::FNetworkSystemInformation Information{};
	Information.Role = MicroWorld::Networking::ENetworkRole::Server;
	MicroWorld::Networking::FNetworkSystem Networking(Messaging, Information);
	if (Networking.Initialize() != MicroWorld::Networking::ENetworkResult::Success)
	{
		return 2;
	}
	if (Networking.GetRole() != MicroWorld::Networking::ENetworkRole::Server)
	{
		Networking.Shutdown();
		return 3;
	}

	Networking.Shutdown();
	return 0;
}
