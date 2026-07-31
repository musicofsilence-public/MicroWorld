#pragma once

#include <MicroWorld/Core/Time.h>

#include <chrono>

namespace MicroWorld::Platform::Host
{

/**
 * Motivation: Makes the host platform the single source of real time so no engine path reads a hidden clock,
 *   by turning the C++ steady_clock into the engine's caller-supplied monotonic time contract.
 * Responsibilities: Capture one baseline instant at construction and report only elapsed milliseconds from it,
 *   never reading a wall clock or moving backward; own no resource so the value stays safe to copy.
 * Example:
 *   FHostTimeSource Clock;
 *   const Core::TimePointMilliseconds Now = Clock.Now();
 */
class FHostTimeSource final
{
public:
	/**
	 * Motivation: Lets the source observe elapsed time from the moment construction finishes.
	 * Responsibilities: Record the steady_clock baseline that every subsequent Now() call subtracts from.
	 */
	FHostTimeSource() noexcept : Baseline(std::chrono::steady_clock::now()) {}

	/**
	 * Motivation: Lets a copied source keep observing elapsed time rather than resetting its baseline.
	 * Responsibilities: Copy the stored baseline so the copy reports the same elapsed timeline as the original.
	 */
	FHostTimeSource(const FHostTimeSource&) noexcept = default;

	/**
	 * Motivation: Lets a reassigned source keep observing elapsed time rather than resetting its baseline.
	 * Responsibilities: Copy the stored baseline so the source continues reporting the original timeline after assignment.
	 */
	FHostTimeSource& operator=(const FHostTimeSource&) noexcept = default;

	/**
	 * Motivation: Keeps destruction trivial since the type owns only a trivially destructible time point.
	 * Responsibilities: Release no resource and never throw.
	 */
	~FHostTimeSource() noexcept = default;

	/**
	 * Motivation: Lets the engine ask for its canonical time point through one stable call.
	 * Responsibilities: Report milliseconds elapsed since construction, derived only from the captured baseline.
	 */
	Core::TimePointMilliseconds Now() const noexcept
	{
		const std::chrono::steady_clock::duration Elapsed = std::chrono::steady_clock::now() - Baseline;
		return static_cast<Core::TimePointMilliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count());
	}

private:
	/** Motivation: Anchor instant subtracted from the current reading to form a monotonic elapsed time. */
	std::chrono::steady_clock::time_point Baseline;
};

} // namespace MicroWorld::Platform::Host
