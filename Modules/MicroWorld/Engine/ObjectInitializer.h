#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/Internal/ManagedTypeResolver.h>
#include <MicroWorld/Engine/Internal/ObjectConstructionTransaction.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

/**
 * Motivation: Gives an actor constructor the sole capability to create its fixed default component graph.
 * Responsibilities: Forward component creation only to the active private construction transaction; it never exposes the store, world, or public
 * mutation APIs.
 *
 * Example: Initializer.CreateDefaultSubobject<FIndicatorComponent>();
 */
class FObjectInitializer final
{
public:
	/**
	 * Motivation: Creates one actor-owned default component while construction is private.
	 * Responsibilities: Reject unsupported component types at compile time and return an unresolved pointer after a sticky failure.
	 */
	template<typename TComponent, typename... TArguments>
	TObjectPtr<TComponent> CreateDefaultSubobject(TArguments&&... Arguments) noexcept
	{
		static_assert(std::is_base_of<UActorComponent, TComponent>::value, "Default subobjects must derive from UActorComponent.");
		static_assert(std::is_nothrow_constructible<TComponent, TArguments...>::value, "Default subobject construction must be noexcept.");
		return Transaction != nullptr ? Transaction->CreateDefaultSubobject<TComponent>(std::forward<TArguments>(Arguments)...)
									  : TObjectPtr<TComponent>{};
	}

private:
	friend class FObjectConstructionTransaction;
	/**
	 * Motivation: Binds this capability to the one active factory transaction.
	 * Responsibilities: Retain no authority after its transaction ends.
	 */
	explicit FObjectInitializer(FObjectConstructionTransaction& InTransaction) noexcept : Transaction(&InTransaction) {}
	/** Motivation: Names the private operation that owns every provisional object slot. */
	FObjectConstructionTransaction* Transaction{nullptr};
};

template<typename TActor, typename... TArguments>
TObjectCreationResult<TActor> FObjectConstructionTransaction::ConstructActor(const FClassDescriptor& InDescriptor, TArguments&&... Arguments) noexcept
{
	TObjectCreationResult<TActor> Creation{};
	if (FirstError != EObjectResult::Success)
	{
		Creation.Result = FirstError;
		return Creation;
	}
	const EObjectResult ValidationResult = Store.ValidateConstruction<TActor>(InDescriptor);
	if (ValidationResult != EObjectResult::Success)
	{
		RecordError(ValidationResult);
		Creation.Result = FirstError;
		return Creation;
	}
	Actor = Reserve(InDescriptor);
	if (FirstError != EObjectResult::Success)
	{
		Creation.Result = FirstError;
		return Creation;
	}
	FObjectInitializer Initializer{*this};
	TActor* const ConstructedActor = ::new (Store.SlotAddress(Actor.SlotIndex)) TActor(Initializer, std::forward<TArguments>(Arguments)...);
	Actor.Object = static_cast<UObject*>(ConstructedActor);
	if (FirstError != EObjectResult::Success)
	{
		Creation.Result = FirstError;
		return Creation;
	}
	for (std::size_t Index = 0; Index < ComponentCount; ++Index)
	{
		UActorComponent* const Component = static_cast<UActorComponent*>(Components[Index].Object);
		Component->AssignOwner(Actor.Handle);
		ConstructedActor->Components[Index] = TObjectPtr<UActorComponent>(Store, Components[Index].Handle);
	}
	ConstructedActor->ComponentCount = ComponentCount;
	for (std::size_t Index = 0; Index < ComponentCount; ++Index)
	{
		Publish(Components[Index]);
	}
	Publish(Actor);
	bCommitted = true;
	Store.bMutationLocked = bPreviousMutationLock;
	Creation.Result = EObjectResult::Success;
	Creation.Object = TObjectPtr<TActor>(Store, Actor.Handle);
	return Creation;
}

template<typename TComponent, typename... TArguments>
TObjectPtr<TComponent> FObjectConstructionTransaction::CreateDefaultSubobject(TArguments&&... Arguments) noexcept
{
	static_assert(std::is_base_of<UActorComponent, TComponent>::value, "Default subobjects must derive from UActorComponent.");
	static_assert(std::is_nothrow_constructible<TComponent, TArguments...>::value, "Default subobject construction must be noexcept.");
	if (FirstError != EObjectResult::Success || ComponentCount >= AActor::MaxComponentsPerActor)
	{
		if (FirstError == EObjectResult::Success)
		{
			RecordError(EObjectResult::CapacityExceeded);
		}
		return {};
	}
	const FClassDescriptor* const ComponentParent = Classes.Find(UActorComponentClassId);
	const FClassDescriptor* Descriptor = nullptr;
	if (ComponentParent == nullptr)
	{
		RecordError(EObjectResult::UnknownClass);
		return {};
	}
	const EObjectResult DescriptorResult = FManagedTypeResolver::Resolve<TComponent>(Classes, *ComponentParent, "DefaultSubobject", Descriptor);
	if (DescriptorResult != EObjectResult::Success || Descriptor == nullptr)
	{
		RecordError(DescriptorResult != EObjectResult::Success ? DescriptorResult : EObjectResult::UnknownClass);
		return {};
	}
	const EObjectResult ValidationResult = Store.ValidateConstruction<TComponent>(*Descriptor);
	if (ValidationResult != EObjectResult::Success)
	{
		RecordError(ValidationResult);
		return {};
	}
	FReservedObject& ReservedComponent = Components[ComponentCount];
	ReservedComponent = Reserve(*Descriptor);
	if (FirstError != EObjectResult::Success)
	{
		return {};
	}
	TComponent* const ConstructedComponent =
		::new (Store.SlotAddress(ReservedComponent.SlotIndex)) TComponent(std::forward<TArguments>(Arguments)...);
	ReservedComponent.Object = static_cast<UObject*>(ConstructedComponent);
	++ComponentCount;
	return TObjectPtr<TComponent>(Store, ReservedComponent.Handle);
}

} // namespace MicroWorld::Engine
