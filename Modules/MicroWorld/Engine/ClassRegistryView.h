#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>

#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Provides a non-owning lookup over an explicitly registered class set so FObjectStore stays independent of
 *   registry capacity while still validating descriptor identity without RTTI.
 * Responsibilities: Bind one application-owned registry context with an allocation-free find operation and answer class
 *   lookups or null when unbound.
 * Example:
 *   FClassRegistryView View = MakeClassRegistryView(Registry);
 *   const FClassDescriptor* D = View.Find(Id);
 */
class FClassRegistryView final
{
public:
	/** Motivation: Defines the only operation needed from an application-owned registry. */
	using FFindClass = const FClassDescriptor* (*)(const void*, FTypeId) noexcept;

	/**
	 * Motivation: Creates an empty view that rejects every class as unknown.
	 * Responsibilities: Produce an unbound view whose Find always returns null.
	 */
	FClassRegistryView() noexcept = default;

	/**
	 * Motivation: Binds a stable registry context and its allocation-free lookup operation.
	 * Responsibilities: Store the context and find callable for the store's lifetime.
	 */
	FClassRegistryView(const void* InContext, FFindClass InFindClass) noexcept : Context(InContext), FindClass(InFindClass) {}

	/**
	 * Motivation: Finds one descriptor by local type identifier without changing registry state.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* Find(const FTypeId InTypeId) const noexcept
	{
		return Context != nullptr && FindClass != nullptr ? FindClass(Context, InTypeId) : nullptr;
	}

private:
	/** Motivation: Identifies the application-owned registry retained for the store lifetime. */
	const void* Context{nullptr};

	/** Motivation: Performs bounded registry lookup without virtual allocation or RTTI. */
	FFindClass FindClass{nullptr};
};

/**
 * Motivation: Creates a type-erased non-owning view over one fixed-capacity class registry.
 * Responsibilities: Bind the registry and a lambda that forwards Find by id.
 */
template<std::size_t MaxClasses>
FClassRegistryView MakeClassRegistryView(const TClassRegistry<MaxClasses>& Registry) noexcept
{
	return FClassRegistryView(
		&Registry,
		[](const void* InContext, const FTypeId InTypeId) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->Find(InTypeId); });
}

} // namespace MicroWorld::Engine
