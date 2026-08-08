#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ReferenceCollector.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Type-erases invocation, destruction, reference tracing, and descriptor resolution for one inline factory
 *   so non-template storage can drive a template factory through function pointers.
 * Responsibilities: Hold one pointer per erased operation and leave them null until a factory is activated.
 * Example:
 *   FFactoryOperations Ops{&F::Invoke, &F::Destroy, &F::VisitReferences, &F::ResolveDescriptor};
 */
struct FFactoryOperations final
{
	using FInvoke = TObjectCreationResult<AActor> (*)(void*, FObjectStore&, const FClassDescriptor&, FClassRegistryRegistrationView) noexcept;
	using FDestroy = void (*)(void*) noexcept;
	using FVisitReferences = void (*)(const void*, FReferenceCollector&) noexcept;
	using FResolveDescriptor = EObjectResult (*)(FClassRegistryRegistrationView, const FClassDescriptor*&) noexcept;

	/** Motivation: Invokes the factory to construct its actor in the store. */
	FInvoke Invoke{nullptr};

	/** Motivation: Destroys moved constructor values after construction or terminal failure. */
	FDestroy Destroy{nullptr};

	/** Motivation: Traces direct managed pointer captures while the queued factory owns them. */
	FVisitReferences VisitReferences{nullptr};

	/** Motivation: Resolves or registers the descriptor the factory needs to construct. */
	FResolveDescriptor ResolveDescriptor{nullptr};
};

} // namespace MicroWorld::Engine
