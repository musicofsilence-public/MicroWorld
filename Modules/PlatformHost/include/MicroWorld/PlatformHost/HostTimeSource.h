#pragma once

#include <MicroWorld/Time.h>

#include <chrono>

namespace MicroWorld
{

/**
 * Host clock that feeds the engine's caller-supplied monotonic time contract.
 *
 * Captures a `steady_clock` baseline at construction and reports elapsed
 * milliseconds from it, so the host platform is the single source of real time
 * and no engine path reads a hidden clock. The type owns no resource and is
 * safe to copy or default-construct by value.
 */
class FHostTimeSource final
{
public:
	/** Records the baseline instant used by every subsequent `Now()` call. */
	FHostTimeSource() noexcept : Baseline(std::chrono::steady_clock::now()) {}

	/** Defaults the baseline so a value-initialized source still observes elapsed time. */
	FHostTimeSource(const FHostTimeSource&) noexcept = default;

	/** Keeps the single baseline invariant while allowing reassignment of the source. */
	FHostTimeSource& operator=(const FHostTimeSource&) noexcept = default;

	/** Defaults the special members; the type holds only a trivially destructible time point. */
	~FHostTimeSource() noexcept = default;

	/** Reports milliseconds elapsed since construction as the engine's canonical time point. */
	TimePointMilliseconds Now() const noexcept
	{
		const std::chrono::steady_clock::duration Elapsed = std::chrono::steady_clock::now() - Baseline;
		return static_cast<TimePointMilliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count());
	}

private:
	/** Anchor instant subtracted from the current reading to form a monotonic elapsed time. */
	std::chrono::steady_clock::time_point Baseline;
};

} // namespace MicroWorld
