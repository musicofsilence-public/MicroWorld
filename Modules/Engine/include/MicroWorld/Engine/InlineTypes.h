#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/TickFunction.h>

#include <cstddef>

namespace MicroWorld
{

namespace Detail
{

/**
 * Holds one actor's component-registry storage as a first base so the registry
 * is fully constructed before the AActor subobject and outlives it.
 *
 * This is the base-from-member idiom: a member cannot be passed to a base
 * constructor, but a base declared before AActor can. TInlineActor relies on
 * the declaration order (this holder, then AActor) for both construction and
 * reverse destruction.
 */
template<std::size_t MaxComponents>
struct TActorRegistryHolder
{
	/** Owns the inline component registry leased to the derived actor's base. */
	FActorComponentRegistry<MaxComponents> Registry;
};

/**
 * Holds one world's actor-registry storage as a first base so the registry is
 * fully constructed before the UWorld subobject and outlives it.
 *
 * The same base-from-member idiom as TActorRegistryHolder, over the world's
 * actor registry instead of an actor's component registry.
 */
template<std::size_t MaxActors>
struct TWorldRegistryHolder
{
	/** Owns the inline actor registry leased to the derived world's base. */
	FWorldActorRegistry<MaxActors> Registry;
};

} // namespace Detail

/**
 * An AActor that owns its fixed-capacity component registry inline through the
 * base-from-member idiom, so callers need not compose or pass a separate
 * FActorComponentRegistry lease.
 *
 * Derive a concrete actor from TInlineActor<N> and register its components the
 * usual way (RegisterComponent before BeginPlay).
 *
 * Descriptor requirement: every concrete instantiation is its own managed type,
 * so register an FClassDescriptor from MakeClassDescriptor<ThatExactType> with
 * parent AActor before constructing one, and size the store slots to fit the
 * embedded registry.
 */
template<std::size_t MaxComponents>
class TInlineActor : private Detail::TActorRegistryHolder<MaxComponents>, public AActor
{
public:
	/** Leases the inline component registry to AActor after the holder base is built. */
	explicit TInlineActor(FTickConfiguration TickConfiguration = {}) noexcept : AActor(this->Registry.MakeView(), TickConfiguration) {}
};

/**
 * A UWorld that owns its fixed-capacity actor registry inline through the
 * base-from-member idiom, so callers need not compose or pass a separate
 * FWorldActorRegistry lease.
 *
 * Use TInlineWorld<N> directly or derive from it, then register actors the
 * usual way (RegisterActor before BeginPlay).
 *
 * Descriptor requirement: every concrete instantiation is its own managed type,
 * so register an FClassDescriptor from MakeClassDescriptor<ThatExactType> with
 * parent UWorld before constructing one, and size the store slots to fit the
 * embedded registry.
 */
template<std::size_t MaxActors>
class TInlineWorld : private Detail::TWorldRegistryHolder<MaxActors>, public UWorld
{
public:
	/** Leases the inline actor registry to UWorld after the holder base is built. */
	TInlineWorld() noexcept : UWorld(this->Registry.MakeView()) {}
};

} // namespace MicroWorld
