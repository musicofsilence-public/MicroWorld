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
 * Provides the smallest managed Actor for Object and Engine applications that
 * need generation-checked identity, traced Component references, and a weak
 * World parent.
 *
 * The application creates AActor inside an FObjectStore, registers its
 * UActorComponent instances before BeginPlay, and attaches it to one UWorld;
 * the Actor traces its Components without making its weak World parent owning.
 */
class AActor : public UObject, private Core::FTickable
{
public:
	/** Bounds how many components one actor may register before BeginPlay. */
	static constexpr std::size_t MaxComponentsPerActor = 4;

	/** Copying or moving would duplicate a managed object's slot identity; each
	 * lives and dies in one object-store slot. */
	AActor(const AActor&) = delete;
	AActor& operator=(const AActor&) = delete;
	AActor(AActor&&) = delete;
	AActor& operator=(AActor&&) = delete;

	/** Returns the stable descriptor that lets the store construct and trace this type. */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/** Configures only this actor's primary tick; components are registered afterwards. */
	explicit AActor(Core::FTickConfiguration InTickConfiguration = {}) noexcept;

	/** Keeps exact derived destruction behind the descriptor/store boundary. */
	~AActor() noexcept override;

	/** Forwards tick enablement to the primary tick function. */
	Core::ERuntimeResult SetTickEnabled(bool bInEnabled) noexcept { return Core::FTickable::SetTickEnabled(bInEnabled); }

	/** Forwards the minimum tick interval to the primary tick function. */
	Core::ERuntimeResult SetTickInterval(Core::DurationMilliseconds InIntervalMilliseconds) noexcept
	{
		return Core::FTickable::SetTickInterval(InIntervalMilliseconds);
	}

	/** Exposes tick enablement using the primary tick function's representation. */
	bool IsTickEnabled() const noexcept { return Core::FTickable::IsTickEnabled(); }

	/** Exposes the minimum tick interval using the primary tick function's cadence. */
	Core::DurationMilliseconds GetTickInterval() const noexcept { return Core::FTickable::GetTickInterval(); }

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
	EEngineResult RegisterComponent(TObjectPtr<UActorComponent> InComponent) noexcept;

protected:
	/** Runs once after this actor's components have begun play. */
	virtual void BeginPlay() {}

	/** Runs at most once per Advance, after this actor's components have ticked. */
	virtual void Tick(const Core::FTickContext&) {}

	/** Runs once before this actor's components end play. */
	virtual void EndPlay() {}

private:
	friend class UWorld;

	/** Reports the first reason a component cannot register, or Success. */
	EEngineResult CheckComponentRegistrable(TObjectPtr<UActorComponent> InComponent) const noexcept;

	/** Links a component to this actor and adds it to the fixed slots after all checks pass. */
	void PublishComponent(TObjectPtr<UActorComponent> InComponent) noexcept;

	/** Begins this actor's lifecycle, primary tick, components, and consumer hook. */
	Core::ERuntimeResult DispatchBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Advances this actor's components and primary tick for one dispatcher step. */
	Core::ERuntimeResult DispatchAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends this actor's consumer hook and components; idempotent after success. */
	Core::ERuntimeResult DispatchEndPlay() noexcept;

	/** Begins every registered component in order and, on the first failure, ends the
	 * already-begun components in reverse and fails the actor lifecycle. */
	Core::ERuntimeResult BeginComponentsWithRollback(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Binds one weak world handle after same-store registration validation. */
	void AssignWorld(FObjectHandle InWorld) noexcept;

	/** Marks every registered component for the store destruction barrier after end. */
	void MarkRegisteredComponentsPendingDestroy() noexcept;

	/** Reports whether registration into a new owner is still permitted. */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.GetState() == Core::ELifecycleState::Constructed; }

	/** Presents every registered component to the active iterative collector. */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override;

	/** Holds components registered before BeginPlay; slots at or past ComponentCount are empty. */
	TObjectPtr<UActorComponent> Components[MaxComponentsPerActor]{};

	/** Records how many leading Components slots hold a registered component. */
	std::size_t ComponentCount{0};

	/** Carries the weak world identity without keeping the world reachable. */
	FObjectHandle WorldObjectHandle{};

	/** Guards the forward-only actor lifecycle without scattering boolean flags. */
	Core::FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld::Engine
