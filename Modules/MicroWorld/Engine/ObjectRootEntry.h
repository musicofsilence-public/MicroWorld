#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Holds one independently owned explicit-root token in caller-supplied storage.
 * Responsibilities: Carry the rooted lifetime's handle or remain invalid while free.
 * Example:
 *   FObjectRootEntry Root;
 *   Root.Handle = Store.AddRoot(Handle);
 */
struct FObjectRootEntry
{
	/** Motivation: Identifies the rooted lifetime or remains invalid while this entry is free. */
	FObjectHandle Handle{};
};

} // namespace MicroWorld::Engine
