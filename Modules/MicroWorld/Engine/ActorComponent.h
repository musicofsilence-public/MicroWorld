#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Core/LifecycleGuard.h>
#include <MicroWorld/Core/LifecycleState.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Tickable.h>
#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Engine
{

class AActor;
struct FClassDescriptor;
class FReferenceCollector;

/**
 * Motivation: Provides the smallest managed component anchored on UObject and Core's tick mix-in so applications can
 *   compose actor behavior without tying actors to each behavior.
 * Responsibilities: Hold only a weak reference to its owning actor so the parent-child graph stays acyclic for the
 *   iterative collector, and drive a forward-only BeginPlay/Tick/EndPlay lifecycle.
 * Example:
 *   UActorComponent& Comp = *Store.NewObject<UActorComponent>().Object.Get();
 *   (void)Actor.RegisterComponent(Comp);
 *   (void)World.BeginPlay(Now);
 */
class UActorComponent : public UObject, private Core::FTickable
{
public:
	/**
	 * Motivation: Prevents copying or moving from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy construction so each component lives and dies in one store slot.
	 */
	UActorComponent(const UActorComponent&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy assignment so each component keeps one slot identity.
	 */
	UActorComponent& operator=(const UActorComponent&) = delete;

	/**
	 * Motivation: Prevents moving a managed object away from its stable slot.
	 * Responsibilities: Reject move construction so each component keeps one slot identity.
	 */
	UActorComponent(UActorComponent&&) = delete;

	/**
	 * Motivation: Prevents moving another identity into this managed object's stable slot.
	 * Responsibilities: Reject move assignment so each component keeps one slot identity.
	 */
	UActorComponent& operator=(UActorComponent&&) = delete;

	/**
	 * Motivation: Returns the stable descriptor that lets the store construct and trace this type.
	 * Responsibilities: Return the canonical UActorComponent class descriptor registered into the registry.
	 */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Motivation: Captures the consumer-selected tick capability and cadence at construction.
	 * Responsibilities: Construct the component with the given primary tick configuration.
	 */
	explicit UActorComponent(Core::FTickConfiguration InTickConfiguration = {}) noexcept;

	/**
	 * Motivation: Keeps exact derived destruction behind the descriptor/store boundary.
	 * Responsibilities: Override the destructor so the registered exact destructor runs derived teardown.
	 */
	~UActorComponent() noexcept override;

	/**
	 * Motivation: Forwards tick enablement to the primary tick function.
	 * Responsibilities: Apply the enabled flag to the primary tick function.
	 */
	Core::ERuntimeResult SetTickEnabled(bool bInEnabled) noexcept { return Core::FTickable::SetTickEnabled(bInEnabled); }

	/**
	 * Motivation: Forwards the minimum tick interval to the primary tick function.
	 * Responsibilities: Apply the interval to the primary tick function's cadence.
	 */
	Core::ERuntimeResult SetTickInterval(Core::DurationMilliseconds InIntervalMilliseconds) noexcept
	{
		return Core::FTickable::SetTickInterval(InIntervalMilliseconds);
	}

	/**
	 * Motivation: Exposes tick enablement using the primary tick function's representation.
	 * Responsibilities: Report whether the primary tick function is enabled.
	 */
	bool IsTickEnabled() const noexcept { return Core::FTickable::IsTickEnabled(); }

	/**
	 * Motivation: Exposes the minimum tick interval using the primary tick function's cadence.
	 * Responsibilities: Return the primary tick function's minimum interval.
	 */
	Core::DurationMilliseconds GetTickInterval() const noexcept { return Core::FTickable::GetTickInterval(); }

	/**
	 * Motivation: Lets a caller branch on whether this component was ever assigned an actor identity.
	 * Responsibilities: Report true when an owner handle was assigned, even after the weak link has expired.
	 */
	bool HasAssignedActor() const noexcept { return OwnerObjectHandle.IsValid(); }

	/**
	 * Motivation: Lets a caller read the owning actor while its weak parent link is live, returning null once the actor
	 *   is reclaimed so the parent reference expires rather than dangling.
	 * Responsibilities: Resolve the weak owner handle each call; the returned pointer is an observation, not an owning
	 *   reference, and must not be retained across mutation barriers.
	 */
	AActor* GetOwnerActor() const noexcept;

protected:
	/**
	 * Motivation: Lets a derived component run once after it enters play, before its owning actor's hook.
	 * Responsibilities: Override to perform BeginPlay work; the default does nothing.
	 */
	virtual void BeginPlay() {}

	/**
	 * Motivation: Lets a derived component run at most once per Advance when the primary tick function is due.
	 * Responsibilities: Override to perform per-frame work; the default does nothing.
	 */
	virtual void TickComponent(const Core::FTickContext&) {}

	/**
	 * Motivation: Lets a derived component run once before it leaves play, after its owning actor's hook.
	 * Responsibilities: Override to perform EndPlay work; the default does nothing.
	 */
	virtual void EndPlay() {}

private:
	friend class AActor;

	/**
	 * Motivation: Begins this component's lifecycle, primary tick, and consumer hook.
	 * Responsibilities: Move the component lifecycle forward and begin the primary tick and consumer hook.
	 */
	Core::ERuntimeResult DispatchBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Advances this component's primary tick for one dispatcher step.
	 * Responsibilities: Advance the primary tick and run the consumer hook when due.
	 */
	Core::ERuntimeResult DispatchAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends this component's consumer hook and primary tick; idempotent after success.
	 * Responsibilities: End the component and stay idempotent after a successful first call.
	 */
	Core::ERuntimeResult DispatchEndPlay() noexcept;

	/**
	 * Motivation: Binds one weak actor handle after same-store registration validation.
	 * Responsibilities: Record the owner handle without rooting the actor.
	 */
	void AssignOwner(FObjectHandle InOwner) noexcept;

	/**
	 * Motivation: Reports whether registration into a new owner is still permitted.
	 * Responsibilities: Return true only while the component lifecycle is still Constructed.
	 */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.GetState() == Core::ELifecycleState::Constructed; }

	/**
	 * Motivation: Confirms UActorComponent holds no traced outgoing references, only a weak parent link.
	 * Responsibilities: Present no references to the collector.
	 */
	void VisitReferences(FReferenceCollector&) noexcept override {}

	/** Motivation: Carries the weak owner identity without keeping the actor reachable. */
	FObjectHandle OwnerObjectHandle{};

	/** Motivation: Guards the forward-only component lifecycle without scattering boolean flags. */
	Core::FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld::Engine
