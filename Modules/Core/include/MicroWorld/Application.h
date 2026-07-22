#pragma once

#include <MicroWorld/Lifecycle.h>

namespace MicroWorld
{

/** Guards a consumer-owned composition root's lifecycle and monotonic updates. */
class FApplication
{
public:
	/** Prevents duplicating a live composition-root lifecycle. */
	FApplication(const FApplication&) = delete;

	/** Prevents assigning lifecycle state across composition roots. */
	FApplication& operator=(const FApplication&) = delete;

	/** Keeps the composition root address and consumer references stable. */
	FApplication(FApplication&&) = delete;

	/** Prevents moving lifecycle state behind consumer-held references. */
	FApplication& operator=(FApplication&&) = delete;

	/** Allows consumers to destroy derived composition roots polymorphically. */
	virtual ~FApplication() = default;

	/** Starts the consumer composition once from canonical time. */
	ERuntimeResult BeginPlay(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Validates monotonic time before forwarding one consumer frame. */
	ERuntimeResult Advance(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Ends the consumer composition once and is idempotent after success. */
	ERuntimeResult EndPlay() noexcept;

protected:
	/** Restricts construction to a concrete consumer composition root. */
	FApplication() = default;

	/** Lets the consumer start subsystems in its policy-selected order. */
	virtual ERuntimeResult OnBeginPlay(TimePointMilliseconds NowMilliseconds) = 0;

	/** Requires the consumer to roll back partial startup after a failed begin. */
	virtual void OnBeginPlayFailed() noexcept = 0;

	/** Lets the consumer route one canonical update through its subsystems. */
	virtual ERuntimeResult OnAdvance(TimePointMilliseconds NowMilliseconds) = 0;

	/** Lets the consumer stop subsystems in its policy-selected reverse order. */
	virtual void OnEndPlay() = 0;

private:
	/** Makes failed startup terminal and successful end idempotent. */
	FLifecycleGuard Lifecycle;

	/** Rejects backward composition time before any subsystem observes it. */
	TimePointMilliseconds LastUpdateMilliseconds{0};
};

} // namespace MicroWorld
