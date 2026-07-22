#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Lifecycle.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectHandle.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Tickable.h>
#include <MicroWorld/Time.h>

namespace MicroWorld
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
class UActorComponent : public UObject, private FTickable
{
public:
	UActorComponent(const UActorComponent&) = delete;
	UActorComponent& operator=(const UActorComponent&) = delete;
	UActorComponent(UActorComponent&&) = delete;
	UActorComponent& operator=(UActorComponent&&) = delete;

	/** Returns the stable descriptor that lets the store construct and trace this type. */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/** Captures the consumer-selected tick capability and cadence at construction. */
	explicit UActorComponent(FTickConfiguration TickConfiguration = {}) noexcept;

	/** Keeps exact derived destruction behind the descriptor/store boundary. */
	~UActorComponent() noexcept override;

	/** Forwards tick enablement to the primary tick function. */
	ERuntimeResult SetTickEnabled(bool bEnabled) noexcept { return FTickable::SetTickEnabled(bEnabled); }

	/** Forwards the minimum tick interval to the primary tick function. */
	ERuntimeResult SetTickInterval(DurationMilliseconds IntervalMilliseconds) noexcept { return FTickable::SetTickInterval(IntervalMilliseconds); }

	/** Exposes tick enablement using the primary tick function's representation. */
	bool IsTickEnabled() const noexcept { return FTickable::IsTickEnabled(); }

	/** Exposes the minimum tick interval using the primary tick function's cadence. */
	DurationMilliseconds GetTickInterval() const noexcept { return FTickable::GetTickInterval(); }

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
	virtual void TickComponent(const FTickContext&) {}

	/** Runs once before this component leaves play, after its owning actor's hook. */
	virtual void EndPlay() {}

private:
	friend class AActor;

	/** Begins this component's lifecycle, primary tick, and consumer hook. */
	ERuntimeResult DispatchBeginPlay(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Advances this component's components and primary tick for one dispatcher step. */
	ERuntimeResult DispatchAdvance(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Ends this component's consumer hook and primary tick; idempotent after success. */
	ERuntimeResult DispatchEndPlay() noexcept;

	/** Binds one weak actor handle after same-store registration validation. */
	void AssignOwner(FObjectHandle Owner) noexcept;

	/** Reports whether registration into a new owner is still permitted. */
	bool IsRegistrationOpen() const noexcept { return Lifecycle.State() == ELifecycleState::Constructed; }

	/** UActorComponent holds no traced outgoing references; only a weak parent link. */
	void VisitReferences(FReferenceCollector&) noexcept override {}

	/** Carries the weak owner identity without keeping the actor reachable. */
	FObjectHandle OwnerObjectHandle{};

	/** Guards the forward-only component lifecycle without scattering boolean flags. */
	FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld
