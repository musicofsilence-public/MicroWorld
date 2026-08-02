#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Gives one local managed object a type-safe diagnostic identifier that never masquerades as a transport
 *   or serialized identity.
 * Responsibilities: Carry an application-defined diagnostic value only, with no wire semantics.
 * Example:
 *   FObjectId Id{0x1234u};
 */
struct FObjectId
{
	/** Motivation: Carries an application-defined diagnostic value without wire semantics. */
	std::uint32_t Value{0};
};

} // namespace MicroWorld::Engine
