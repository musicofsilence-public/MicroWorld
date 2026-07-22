#pragma once

#include <MicroWorld/Object/ObjectHandle.h>

namespace MicroWorld
{

/**
 * Stable type ids for the three engine base managed classes.
 *
 * Tests and consumers register these descriptors into their own TClassRegistry
 * before constructing engine base objects. The ids are deliberately distinct
 * from any Object-internal id and stable for the lifetime of the engine
 * contract so a registry-owned descriptor copy keeps matching the descriptor a
 * store validates against.
 */
constexpr FTypeId UActorComponentClassId{0x00001001u};
constexpr FTypeId AActorClassId{0x00001002u};
constexpr FTypeId UWorldClassId{0x00001003u};

} // namespace MicroWorld
