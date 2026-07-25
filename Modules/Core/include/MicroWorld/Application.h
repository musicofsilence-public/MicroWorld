#pragma once

#include <MicroWorld/Lifecycle.h>

namespace MicroWorld
{

/**
 * Base class for an application: BeginPlay runs once, Advance every frame, EndPlay once.
 *
 * This class enforces that order and rejects time that moves backward, so the
 * OnBeginPlay, OnAdvance, and OnEndPlay overrides never run out of order and
 * never see time go back.
 */
class FApplication
{
public:
	/** No copying: a copy would be a second object claiming the same started application. */
	FApplication(const FApplication&) = delete;

	/** No copy assignment: it would overwrite the lifecycle of an application already running. */
	FApplication& operator=(const FApplication&) = delete;

	/** No moving: the runner holds a reference to this object, and a move would change its address. */
	FApplication(FApplication&&) = delete;

	/** No move assignment: same reason — every reference to this object must stay valid. */
	FApplication& operator=(FApplication&&) = delete;

	/** Virtual so deleting through an FApplication& also destroys the derived class. */
	virtual ~FApplication() = default;

	/** Starts the application once, at the caller's current time; fails if already started. */
	ERuntimeResult BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Runs one frame; rejects a time earlier than the last one instead of forwarding it. */
	ERuntimeResult Advance(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Stops the application; calling it again after success does nothing and still succeeds. */
	ERuntimeResult EndPlay() noexcept;

protected:
	/** Protected so only a derived application can be constructed. */
	FApplication() = default;

	/** Starts the application's subsystems, in whatever order it needs. */
	virtual ERuntimeResult OnBeginPlay(TimePointMilliseconds InNowMilliseconds) = 0;

	/** Undoes whatever OnBeginPlay started before it failed; it runs on the failure path, so it cannot throw. */
	virtual void OnBeginPlayFailed() noexcept = 0;

	/** Does one frame of work; InNowMilliseconds never moves backward. */
	virtual ERuntimeResult OnAdvance(TimePointMilliseconds InNowMilliseconds) = 0;

	/** Stops the subsystems, normally in reverse start order. */
	virtual void OnEndPlay() = 0;

private:
	/** Tracks the current phase, so a failed start stays dead and a second EndPlay is harmless. */
	FLifecycleGuard Lifecycle;

	/** The last time Advance accepted; a smaller one is rejected before any override sees it. */
	TimePointMilliseconds LastUpdateMilliseconds{0};
};

} // namespace MicroWorld
