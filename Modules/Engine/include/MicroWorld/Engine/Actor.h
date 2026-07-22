#pragma once

#include <MicroWorld/Engine/EngineRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Lifecycle.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectHandle.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Tickable.h>
#include <MicroWorld/Time.h>

namespace MicroWorld
{

struct FClassDescriptor;
class FReferenceCollector;
class UActorComponent;
class UWorld;

/**
 * Provides the smallest managed Actor for Object and Engine applications that
 * need generation-checked identity, traced Component references, and a weak
 * World parent.
 *
 * The application creates AActor inside an FObjectStore, registers its
 * UActorComponent instances before BeginPlay, and attaches it to one UWorld;
 * the Actor traces its Components without making its weak World parent owning.
 */
class AActor : public UObject, private FTickable
{
public:
	AActor(const AActor&) = delete;
	AActor& operator=(const AActor&) = delete;
	AActor(AActor&&) = delete;
	AActor& operator=(AActor&&) = delete;

	/** Returns the stable descriptor that lets the store construct and trace this type. */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Binds this actor to the unique caller-owned component registry lease that
	 * will hold its registered components.
	 *
	 * The object store assigns canonical ownership only after construction
	 * publishes this UObject, so callers cannot supply a second store identity.
	 */
	explicit AActor(FActorComponentRegistryBase ComponentStorage, FTickConfiguration TickConfiguration = {}) noexcept;

	/** Keeps exact derived destruction behind the descriptor/store boundary. */
	~AActor() noexcept override;

	/** Forwards tick enablement to the primary tick function. */
	ERuntimeResult SetTickEnabled(bool bEnabled) noexcept { return FTickable::SetTickEnabled(bEnabled); }

	/** Forwards the minimum tick interval to the primary tick function. */
	ERuntimeResult SetTickInterval(DurationMilliseconds IntervalMilliseconds) noexcept { return FTickable::SetTickInterval(IntervalMilliseconds); }

	/** Exposes tick enablement using the primary tick function's representation. */
	bool IsTickEnabled() const noexcept { return FTickable::IsTickEnabled(); }

	/** Exposes the minimum tick interval using the primary tick function's cadence. */
	DurationMilliseconds GetTickInterval() const noexcept { return FTickable::GetTickInterval(); }

	/**
	 * Reports whether this actor was assigned a world identity, even when that
	 * weak identity has since expired.
	 */
	bool HasAssignedWorld() const noexcept { return WorldObjectHandle.IsValid(); }

	/**
	 * Returns the owning world while its weak parent link is still live, or null
	 * once the world has been reclaimed (so the parent reference expires rather
	 * than dangling). The returned pointer is an observation, not an owning
	 * reference; the caller must not retain it across mutation barriers.
	 */
	UWorld* GetOwnerWorld() const noexcept;

	/**
	 * Registers one component before BeginPlay.
	 *
	 * Rejects duplicates, exhausted or zero capacity, lifecycle-locked actors,
	 * components already owned by another actor, cross-store components, and
	 * empty, stale, or non-resolvable references atomically: a rejected
	 * registration leaves the actor and the component unchanged.
	 */
	EEngineResult RegisterComponent(TObjectPtr<UActorComponent> Component) noexcept;

protected:
	/** Runs once after this actor's components have begun play. */
	virtual void BeginPlay() {}

	/** Runs at most once per Advance, after this actor's components have ticked. */
	virtual void Tick(const FTickContext&) {}

	/** Runs once before this actor's components end play. */
	virtual void EndPlay() {}

private:
	friend class UWorld;

	/** Begins this actor's lifecycle, primary tick, components, and consumer hook. */
	ERuntimeResult DispatchBeginPlay(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Advances this actor's components and primary tick for one dispatcher step. */
	ERuntimeResult DispatchAdvance(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Ends this actor's consumer hook and components; idempotent after success. */
	ERuntimeResult DispatchEndPlay() noexcept;

	/** Binds one weak world handle after same-store registration validation. */
	void AssignWorld(FObjectHandle World) noexcept;

	/** Marks every registered component for the store destruction barrier after end. */
	void MarkRegisteredComponentsPendingDestroy() noexcept;

	/** Reports whether registration into a new owner is still permitted. */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.State() == ELifecycleState::Constructed; }

	/** Presents every registered component to the active iterative collector. */
	void VisitReferences(FReferenceCollector& Collector) noexcept override;

	/** Holds the unique caller-owned component registry lease for this actor's lifetime. */
	FActorComponentRegistryBase Components;

	/** Carries the weak world identity without keeping the world reachable. */
	FObjectHandle WorldObjectHandle{};

	/** Guards the forward-only actor lifecycle without scattering boolean flags. */
	FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld
