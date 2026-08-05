#pragma once

#include "CoreConsumerProbe.h"

#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>

static_assert(__cplusplus >= 201703L);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Messaging consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Messaging consumer must compile with RTTI disabled."
#endif

/**
 * Motivation: Exercises the direct local Messaging contract without composing Networking or Transport.
 * Responsibilities: Create one local channel, publish one message, and report the first public API failure.
 */
inline int RunMessagingConsumerProbe() noexcept
{
	if (RunCoreConsumerProbe() != 0)
	{
		return 1;
	}

	MicroWorld::Messaging::FMessagingSystem Messaging;
	const MicroWorld::Messaging::FChannelInformation Channel{"Consumer", false, nullptr, {}};
	if (Messaging.CreateChannel(Channel) != MicroWorld::Messaging::EMessagingResult::Success)
	{
		return 2;
	}

	MicroWorld::Messaging::FMessage Message;
	Message.SetMessageNameId("Probe");
	return Messaging.SendMessageToChannel(Message, "Consumer") == MicroWorld::Messaging::EMessagingResult::Success ? 0 : 3;
}
