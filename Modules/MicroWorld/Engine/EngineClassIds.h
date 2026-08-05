#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>

namespace MicroWorld::Engine
{

/** Motivation: Stable type id for UActorComponent, distinct from any Object-internal id and constant for the engine contract. */
constexpr FTypeId UActorComponentClassId{0x00001001u};

/** Motivation: Stable type id for AActor, distinct from any Object-internal id and constant for the engine contract. */
constexpr FTypeId AActorClassId{0x00001002u};

/** Motivation: Stable type id for UWorld, distinct from any Object-internal id and constant for the engine contract. */
constexpr FTypeId UWorldClassId{0x00001003u};

/** Motivation: Stable type id for UWorldSubsystem, distinct from any Object-internal id and constant for the engine contract. */
constexpr FTypeId UWorldSubsystemClassId{0x00001004u};

} // namespace MicroWorld::Engine
