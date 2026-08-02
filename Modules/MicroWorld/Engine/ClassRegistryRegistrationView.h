#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ObjectResult.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Gives World the only two mutable-registry operations it needs for deferred actor construction without
 *   exposing registry capacity or storage.
 * Responsibilities: Bind one application-owned registry for find, type-token lookup, and automatic registration, and
 *   forward each operation or a safe rejection when unbound.
 * Example:
 *   FClassRegistryRegistrationView View = MakeClassRegistryRegistrationView(Registry);
 *   View.RegisterAutomatic(Candidate, OutDescriptor);
 */
class FClassRegistryRegistrationView final
{
public:
	/** Motivation: Defines bounded find, type-token lookup, and automatic registration operations. */
	using FFindClass = const FClassDescriptor* (*)(const void*, FTypeId) noexcept;
	using FFindByTypeToken = const FClassDescriptor* (*)(const void*, const void*) noexcept;
	using FRegisterAutomatic = EObjectResult (*)(void*, FClassDescriptor, const FClassDescriptor*&) noexcept;

	/**
	 * Motivation: Creates an empty view that rejects registration without mutation.
	 * Responsibilities: Produce an unbound view whose operations reject without touching any registry.
	 */
	FClassRegistryRegistrationView() noexcept = default;

	/**
	 * Motivation: Binds one application-owned registry for the world lifetime.
	 * Responsibilities: Store the context and all three callables for the world's lifetime.
	 */
	FClassRegistryRegistrationView(
		void* const InContext,
		const FFindClass InFindClass,
		const FFindByTypeToken InFindByTypeToken,
		const FRegisterAutomatic InRegisterAutomatic) noexcept
		: Context(InContext), FindClass(InFindClass), FindTypeToken(InFindByTypeToken), RegisterAutomaticFunction(InRegisterAutomatic)
	{
	}

	/**
	 * Motivation: Finds a canonical descriptor by local class ID without mutation.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* Find(const FTypeId InTypeId) const noexcept
	{
		return Context != nullptr && FindClass != nullptr ? FindClass(Context, InTypeId) : nullptr;
	}

	/**
	 * Motivation: Finds a canonical descriptor by exact no-RTTI type token without mutation.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* FindByTypeToken(const void* const InTypeToken) const noexcept
	{
		return Context != nullptr && FindTypeToken != nullptr ? FindTypeToken(Context, InTypeToken) : nullptr;
	}

	/**
	 * Motivation: Registers a candidate or returns its existing canonical descriptor.
	 * Responsibilities: Forward to automatic registration for a bound view or return UnknownClass when unbound.
	 */
	EObjectResult RegisterAutomatic(const FClassDescriptor InCandidate, const FClassDescriptor*& OutDescriptor) const noexcept
	{
		OutDescriptor = nullptr;
		return Context != nullptr && RegisterAutomaticFunction != nullptr ? RegisterAutomaticFunction(Context, InCandidate, OutDescriptor)
																		  : EObjectResult::UnknownClass;
	}

	/**
	 * Motivation: Lets a caller confirm all required registry operations are available before use.
	 * Responsibilities: Report true only when context and all three callables are set.
	 */
	bool IsValid() const noexcept
	{
		return Context != nullptr && FindClass != nullptr && FindTypeToken != nullptr && RegisterAutomaticFunction != nullptr;
	}

private:
	/** Motivation: Identifies the registry whose lifetime encloses this view. */
	void* Context{nullptr};

	/** Motivation: Performs canonical descriptor lookup by local ID. */
	FFindClass FindClass{nullptr};

	/** Motivation: Reuses an explicitly registered descriptor by exact C++ type token. */
	FFindByTypeToken FindTypeToken{nullptr};

	/** Motivation: Adds only a validated descriptor to caller-owned fixed registry storage. */
	FRegisterAutomatic RegisterAutomaticFunction{nullptr};
};

/**
 * Motivation: Creates World's narrow mutable capability over an application-owned class registry.
 * Responsibilities: Bind the registry and lambdas that forward Find, FindByTypeToken, and RegisterAutomatic.
 */
template<std::size_t MaxClasses>
FClassRegistryRegistrationView MakeClassRegistryRegistrationView(TClassRegistry<MaxClasses>& Registry) noexcept
{
	return FClassRegistryRegistrationView(
		&Registry,
		[](const void* const InContext, const FTypeId InTypeId) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->Find(InTypeId); },
		[](const void* const InContext, const void* const InTypeToken) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->FindByTypeToken(InTypeToken); },
		[](void* const InContext, const FClassDescriptor InCandidate, const FClassDescriptor*& OutDescriptor) noexcept -> EObjectResult
		{ return static_cast<TClassRegistry<MaxClasses>*>(InContext)->RegisterAutomatic(InCandidate, OutDescriptor); });
}

} // namespace MicroWorld::Engine
