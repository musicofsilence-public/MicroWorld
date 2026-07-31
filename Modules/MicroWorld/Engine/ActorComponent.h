#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Core/Lifecycle.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Tickable.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Engine
{

class AActor;
struct FClassDescriptor;
class FReferenceCollector;

/**
 * The smallest managed component anchored on UObject and Core's tick mix-in.
 *
 * The application creates one UActorComponent (or a user-derived class) inside
 * an FObjectStore, then registers it with exactly one AActor before BeginPlay.
 * UActorComponent holds only a weak reference to its owning actor so the
 * parent-child graph stays acyclic for the iterative collector.
 */
class UActorComponent : public UObject, private Core::FTickable
{
public:
	/** Copying or moving would duplicate a managed object's slot identity; each
	 * lives and dies in one object-store slot. */
	UActorComponent(const UActorComponent&) = delete;
	UActorComponent& operator=(const UActorComponent&) = delete;
	UActorComponent(UActorComponent&&) = delete;
	UActorComponent& operator=(UActorComponent&&) = delete;

	/** Returns the stable descriptor that lets the store construct and trace this type. */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/** Captures the consumer-selected tick capability and cadence at construction. */
	explicit UActorComponent(Core::FTickConfiguration InTickConfiguration = {}) noexcept;

	/** Keeps exact derived destruction behind the descriptor/store boundary. */
	~UActorComponent() noexcept override;

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
	 * Reports whether this component was assigned an actor identity, even when
	 * that weak identity has since expired.
	 */
	bool HasAssignedActor() const noexcept { return OwnerObjectHandle.IsValid(); }

	/**
	 * Returns the owning actor while its weak parent link is still live, or null
	 * once the actor has been reclaimed (so the parent reference expires rather
	 * than dangling). The returned pointer is an observation, not an owning
	 * reference; the caller must not retain it across mutation barriers.
	 */
	AActor* GetOwnerActor() const noexcept;

protected:
	/** Runs once after this component enters play, before its owning actor's hook. */
	virtual void BeginPlay() {}

	/** Runs at most once per Advance when the primary tick function is due. */
	virtual void TickComponent(const Core::FTickContext&) {}

	/** Runs once before this component leaves play, after its owning actor's hook. */
	virtual void EndPlay() {}

private:
	friend class AActor;

	/** Begins this component's lifecycle, primary tick, and consumer hook. */
	Core::ERuntimeResult DispatchBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Advances this component's primary tick for one dispatcher step. */
	Core::ERuntimeResult DispatchAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends this component's consumer hook and primary tick; idempotent after success. */
	Core::ERuntimeResult DispatchEndPlay() noexcept;

	/** Binds one weak actor handle after same-store registration validation. */
	void AssignOwner(FObjectHandle InOwner) noexcept;

	/** Reports whether registration into a new owner is still permitted. */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.GetState() == Core::ELifecycleState::Constructed; }

	/** UActorComponent holds no traced outgoing references; only a weak parent link. */
	void VisitReferences(FReferenceCollector&) noexcept override {}

	/** Carries the weak owner identity without keeping the actor reachable. */
	FObjectHandle OwnerObjectHandle{};

	/** Guards the forward-only component lifecycle without scattering boolean flags. */
	Core::FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld::Engine
