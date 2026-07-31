#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Core/Lifecycle.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Tickable.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>

namespace MicroWorld::Engine
{

struct FClassDescriptor;
class FReferenceCollector;
class UActorComponent;
class UWorld;

/**
 * Motivation: Provides the smallest managed Actor for Object and Engine applications that need generation-checked
 *   identity, traced Component references, and a weak World parent.
 * Responsibilities: Hold a bounded set of registered components, trace them for collection without making the weak world
 *   parent owning, and drive a forward-only BeginPlay/Tick/EndPlay lifecycle.
 * Example:
 *   AActor& Actor = *Store.NewObject<AActor>().Object.Get();
 *   (void)Actor.RegisterComponent(Comp);
 *   (void)World.BeginPlay(Now);
 */
class AActor : public UObject, private Core::FTickable
{
public:
	/** Motivation: Bounds how many components one actor may register before BeginPlay. */
	static constexpr std::size_t MaxComponentsPerActor = 4;

	/**
	 * Motivation: Prevents copying or moving from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy construction so each actor lives and dies in one store slot.
	 */
	AActor(const AActor&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy assignment so each actor keeps one slot identity.
	 */
	AActor& operator=(const AActor&) = delete;

	/**
	 * Motivation: Prevents moving a managed object away from its stable slot.
	 * Responsibilities: Reject move construction so each actor keeps one slot identity.
	 */
	AActor(AActor&&) = delete;

	/**
	 * Motivation: Prevents moving another identity into this managed object's stable slot.
	 * Responsibilities: Reject move assignment so each actor keeps one slot identity.
	 */
	AActor& operator=(AActor&&) = delete;

	/**
	 * Motivation: Returns the stable descriptor that lets the store construct and trace this type.
	 * Responsibilities: Return the canonical AActor class descriptor registered into the registry.
	 */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Motivation: Configures only this actor's primary tick; components are registered afterwards.
	 * Responsibilities: Construct the actor with the given primary tick configuration and no components.
	 */
	explicit AActor(Core::FTickConfiguration InTickConfiguration = {}) noexcept;

	/**
	 * Motivation: Keeps exact derived destruction behind the descriptor/store boundary.
	 * Responsibilities: Override the destructor so the registered exact destructor runs derived teardown.
	 */
	~AActor() noexcept override;

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
	 * Motivation: Lets a caller branch on whether this actor was ever assigned a world identity.
	 * Responsibilities: Report true when a world handle was assigned, even after the weak link has expired.
	 */
	bool HasAssignedWorld() const noexcept { return WorldObjectHandle.IsValid(); }

	/**
	 * Motivation: Lets a caller read the owning world while its weak parent link is live, returning null once the world
	 *   is reclaimed so the parent reference expires rather than dangling.
	 * Responsibilities: Resolve the weak world handle each call; the returned pointer is an observation, not an owning
	 *   reference, and must not be retained across mutation barriers.
	 */
	UWorld* GetOwnerWorld() const noexcept;

	/**
	 * Motivation: Lets a caller register one component before BeginPlay.
	 * Responsibilities: Reject duplicates, exhausted or zero capacity, a lifecycle-locked actor, a component already
	 *   owned by another actor, a cross-store component, and an empty, stale, or non-resolvable reference atomically,
	 *   leaving the actor and component unchanged on rejection.
	 */
	EEngineResult RegisterComponent(TObjectPtr<UActorComponent> InComponent) noexcept;

protected:
	/**
	 * Motivation: Lets a derived actor run once after its components have begun play.
	 * Responsibilities: Override to perform BeginPlay work; the default does nothing.
	 */
	virtual void BeginPlay() {}

	/**
	 * Motivation: Lets a derived actor run at most once per Advance, after its components have ticked.
	 * Responsibilities: Override to perform per-frame work; the default does nothing.
	 */
	virtual void Tick(const Core::FTickContext&) {}

	/**
	 * Motivation: Lets a derived actor run once before its components end play.
	 * Responsibilities: Override to perform EndPlay work; the default does nothing.
	 */
	virtual void EndPlay() {}

private:
	friend class UWorld;

	/**
	 * Motivation: Reports the first reason a component cannot register before any actor or component mutation.
	 * Responsibilities: Return Success or the first rejection reason for the candidate component.
	 */
	EEngineResult CheckComponentRegistrable(TObjectPtr<UActorComponent> InComponent) const noexcept;

	/**
	 * Motivation: Links a component to this actor and adds it to the fixed slots after all checks pass.
	 * Responsibilities: Store the component and advance the component count.
	 */
	void PublishComponent(TObjectPtr<UActorComponent> InComponent) noexcept;

	/**
	 * Motivation: Begins this actor's lifecycle, primary tick, components, and consumer hook.
	 * Responsibilities: Move the actor lifecycle forward and begin components and the primary tick.
	 */
	Core::ERuntimeResult DispatchBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Advances this actor's components and primary tick for one dispatcher step.
	 * Responsibilities: Tick components in order then the primary tick for the given time.
	 */
	Core::ERuntimeResult DispatchAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends this actor's consumer hook and components; idempotent after success.
	 * Responsibilities: End the actor in reverse order and stay idempotent after a successful first call.
	 */
	Core::ERuntimeResult DispatchEndPlay() noexcept;

	/**
	 * Motivation: Begins every registered component in order and, on the first failure, ends the already-begun
	 *   components in reverse so the actor lifecycle fails atomically.
	 * Responsibilities: Begin components forward and roll back on the first failure.
	 */
	Core::ERuntimeResult BeginComponentsWithRollback(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Binds one weak world handle after same-store registration validation.
	 * Responsibilities: Record the world handle without rooting the world.
	 */
	void AssignWorld(FObjectHandle InWorld) noexcept;

	/**
	 * Motivation: Marks every registered component for the store destruction barrier after end.
	 * Responsibilities: Mark each registered component pending destroy.
	 */
	void MarkRegisteredComponentsPendingDestroy() noexcept;

	/**
	 * Motivation: Reports whether registration into a new owner is still permitted.
	 * Responsibilities: Return true only while the actor lifecycle is still Constructed.
	 */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.GetState() == Core::ELifecycleState::Constructed; }

	/**
	 * Motivation: Presents every registered component to the active iterative collector.
	 * Responsibilities: Add each registered component reference to the collector.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override;

	/** Motivation: Holds components registered before BeginPlay; slots at or past ComponentCount are empty. */
	TObjectPtr<UActorComponent> Components[MaxComponentsPerActor]{};

	/** Motivation: Records how many leading Components slots hold a registered component. */
	std::size_t ComponentCount{0};

	/** Motivation: Carries the weak world identity without keeping the world reachable. */
	FObjectHandle WorldObjectHandle{};

	/** Motivation: Guards the forward-only actor lifecycle without scattering boolean flags. */
	Core::FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld::Engine
