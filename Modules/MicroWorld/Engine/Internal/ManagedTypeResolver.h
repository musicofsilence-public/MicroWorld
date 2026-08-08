#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectStore.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Keeps automatic managed-type registration identical for actor factories and default subobjects.
 * Responsibilities: Reuse an existing descriptor by type token or register one direct child of the supplied parent.
 * Example: FManagedTypeResolver::Resolve<TActor>(Classes, Parent, "Actor", Descriptor);
 */
class FManagedTypeResolver final
{
public:
	/**
	 * Motivation: Resolves the one stable descriptor that validates a managed type before construction begins.
	 * Responsibilities: Reuse an existing type token or register one monotonic descriptor owned by the class registry.
	 */
	template<typename T>
	static EObjectResult Resolve(
		const FClassRegistryRegistrationView InClasses,
		const FClassDescriptor& InParent,
		const char* const InDiagnosticName,
		const FClassDescriptor*& OutDescriptor) noexcept
	{
		OutDescriptor = InClasses.FindByTypeToken(ManagedObjectTypeToken<T>());
		if (OutDescriptor != nullptr)
		{
			return EObjectResult::Success;
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<T>(0, InDiagnosticName, &InParent, &TraceManagedObjectReferences);
		return InClasses.RegisterAutomatic(Candidate, OutDescriptor);
	}
};

} // namespace MicroWorld::Engine
