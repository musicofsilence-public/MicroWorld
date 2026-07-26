#pragma once

#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <cstddef>

namespace MicroWorld
{

namespace Detail
{

	/**
	 * Holds one world's actor-registry storage as a first base so the registry is
	 * fully constructed before the UWorld subobject and
	 * outlives it.
	 *
	 * This base-from-member arrangement lets the world consume its storage during
	 * base construction while retaining the
	 * required lifetime ordering.
	 */
	template<std::size_t MaxActors>
	struct TWorldRegistryHolder
	{
		/** Owns the inline world actor registry referenced by the derived world's base. */
		FWorldActorRegistry<MaxActors> Registry;
	};

} // namespace Detail

/**
 * A UWorld that owns its fixed-capacity actor registry inline through the
 * base-from-member idiom, so callers need not compose or pass a separate
 * FWorldActorRegistry reference.
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
	/** Provides a reference to the inline world actor registry after the holder base is built. */
	TInlineWorld() noexcept : UWorld(this->Registry.MakeReference()) {}
};

} // namespace MicroWorld
