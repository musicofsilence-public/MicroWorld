#pragma once

#include <MicroWorld/Core/LifecycleGuard.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>

namespace MicroWorld::Engine
{

class UWorld;

/**
 * Motivation: Gives an application one managed service whose identity and lifetime belong to a World.
 * Responsibilities: Observe its World weakly and expose deterministic initialize/deinitialize hooks without ticking,
 *   discovery, or application-specific behavior.
 * Example:
 *   class UInventoryWorldSubsystem final : public UWorldSubsystem {};
 */
class UWorldSubsystem : public UObject
{
public:
	/**
	 * Motivation: Constructs an unowned subsystem before one World publishes it.
	 * Responsibilities: Start with no World identity and a constructed lifecycle.
	 */
	UWorldSubsystem() noexcept = default;

	/**
	 * Motivation: Prevents copying from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy construction so each subsystem lives in one store slot.
	 */
	UWorldSubsystem(const UWorldSubsystem&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy assignment so each subsystem keeps one slot identity.
	 */
	UWorldSubsystem& operator=(const UWorldSubsystem&) = delete;

	/**
	 * Motivation: Prevents moving a managed object away from its stable slot.
	 * Responsibilities: Reject move construction so each subsystem keeps one slot identity.
	 */
	UWorldSubsystem(UWorldSubsystem&&) = delete;

	/**
	 * Motivation: Prevents moving another identity into this managed object's stable slot.
	 * Responsibilities: Reject move assignment so each subsystem keeps one slot identity.
	 */
	UWorldSubsystem& operator=(UWorldSubsystem&&) = delete;

	/**
	 * Motivation: Keeps exact derived destruction behind the descriptor/store boundary.
	 * Responsibilities: Override the destructor so the registered exact destructor runs derived teardown.
	 */
	~UWorldSubsystem() noexcept override;

	/**
	 * Motivation: Returns the stable descriptor that lets the store construct and trace this type.
	 * Responsibilities: Return the canonical UWorldSubsystem descriptor registered into the Engine class registry.
	 */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Motivation: Lets a caller distinguish an unregistered subsystem from one whose weak World has expired.
	 * Responsibilities: Report true once a World identity has been assigned.
	 */
	bool HasAssignedWorld() const noexcept { return WorldObjectHandle.IsValid(); }

	/**
	 * Motivation: Lets a subsystem reach its owning World while the weak parent link remains live.
	 * Responsibilities: Resolve the World handle for this call and return null when the parent has expired.
	 */
	UWorld* GetWorld() const noexcept;

protected:
	/**
	 * Motivation: Lets a derived application service start before its World's actors begin.
	 * Responsibilities: Override to perform initialization; the default does nothing.
	 */
	virtual void Initialize() {}

	/**
	 * Motivation: Lets a derived application service stop after its World's actors end.
	 * Responsibilities: Override to perform deinitialization; the default does nothing.
	 */
	virtual void Deinitialize() {}

private:
	friend class UWorld;

	/**
	 * Motivation: Moves the subsystem lifecycle forward before invoking the application hook.
	 * Responsibilities: Begin once, invoke Initialize on success, and return the lifecycle result.
	 */
	Core::ERuntimeResult DispatchInitialize() noexcept;

	/**
	 * Motivation: Ends the subsystem lifecycle while keeping repeated successful shutdown idempotent.
	 * Responsibilities: End once, invoke Deinitialize on success, and return the lifecycle result.
	 */
	Core::ERuntimeResult DispatchDeinitialize() noexcept;

	/**
	 * Motivation: Binds one weak World handle after same-store registration validation.
	 * Responsibilities: Record the World handle without tracing or rooting it.
	 */
	void AssignWorld(FObjectHandle InWorld) noexcept;

	/** Motivation: Carries the weak World identity without keeping the World reachable. */
	FObjectHandle WorldObjectHandle{};

	/** Motivation: Guards the forward-only subsystem lifecycle without extra state flags. */
	Core::FLifecycleGuard Lifecycle;
};

} // namespace MicroWorld::Engine
