#pragma once

#include <cstdint>

/**
 * MicroWorld logging facade.
 *
 * Design invariants:
 *   * Level gating happens in the preprocessor, so a below-floor call expands
 *     to ((void)0): zero emitted code, zero format/category string literals in
 *     flash, and its arguments are never evaluated.
 *   * Formatting uses a fixed-size caller-stack buffer plus vsnprintf (see
 *     src/Log.cpp): no heap, no exceptions, no hidden clock.
 *   * One process-global function-pointer output device; a null device (the default)
 *     disables logging. Install it once at startup (MicroWorld is
 *     single-threaded).
 *
 * Worked expansion of MW_LOG(Log, "Boot", "x=%d", X):
 *   * level enabled  ->  ::MicroWorld::Detail::DispatchLogFormatted(
 *                         ::MicroWorld::ELogLevel::Log, ("Boot"), "x=%d", X)
 *   * below floor    ->  ((void)0)      // X is never evaluated
 */

// Compile-time severity ranks. Lower value = more important. The preprocessor
// uses these to strip below-floor call sites; the enum mirrors them for the
// output-device signature.
#define MW_LOG_LEVEL_Error 0
#define MW_LOG_LEVEL_Warning 1
#define MW_LOG_LEVEL_Log 2
#define MW_LOG_LEVEL_Verbose 3

// Compile-time floor: call sites whose level rank is greater than this value are
// stripped entirely. Override on the command line (e.g.
// -DMW_LOG_MIN_LEVEL=MW_LOG_LEVEL_Verbose) before including this header.
#ifndef MW_LOG_MIN_LEVEL
#define MW_LOG_MIN_LEVEL MW_LOG_LEVEL_Log
#endif

// Maximum formatted-message length, including the terminating null, written to
// the per-call stack buffer. Longer messages are safely truncated by vsnprintf.
// Override to trade stack footprint against message length.
#ifndef MW_LOG_MESSAGE_CAPACITY
#define MW_LOG_MESSAGE_CAPACITY 128
#endif

// Per-level enable flags resolved at preprocessing time. A level is enabled when
// its rank is at least as important as the configured floor.
#if MW_LOG_LEVEL_Error <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Error 1
#else
#define MW_LOG_ENABLED_Error 0
#endif

#if MW_LOG_LEVEL_Warning <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Warning 1
#else
#define MW_LOG_ENABLED_Warning 0
#endif

#if MW_LOG_LEVEL_Log <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Log 1
#else
#define MW_LOG_ENABLED_Log 0
#endif

#if MW_LOG_LEVEL_Verbose <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Verbose 1
#else
#define MW_LOG_ENABLED_Verbose 0
#endif

// printf-format checking on the variadic dispatch helper, where available.
#if defined(__GNUC__) || defined(__clang__)
#define MW_LOG_PRINTF_FORMAT __attribute__((format(printf, 3, 4)))
#else
#define MW_LOG_PRINTF_FORMAT
#endif

namespace MicroWorld
{

/** Ranks a log record by importance so a compile-time floor can strip the rest. */
enum class ELogLevel : std::uint8_t
{
	Error = MW_LOG_LEVEL_Error,		///< Reports an unrecoverable fault the caller must handle.
	Warning = MW_LOG_LEVEL_Warning, ///< Reports a recoverable anomaly worth surfacing.
	Log = MW_LOG_LEVEL_Log,			///< Reports ordinary operational milestones.
	Verbose = MW_LOG_LEVEL_Verbose, ///< Reports fine-grained detail usually stripped in release.
};

/**
 * Receives one fully formed record; the process installs exactly one output device.
 *
 * Named after UE5's FOutputDevice because it fills the same role, and suffixed
 * Function because it is a plain function pointer rather than an abstract class:
 * there is nothing to subclass and no Serialize override. A platform supplies one
 * free function, so every function-pointer alias here announces itself.
 */
using FOutputDeviceFunction = void (*)(ELogLevel Level, const char* Category, const char* Message);

/** Installs the process-global output device; pass nullptr (the default) to disable logging. */
void SetOutputDevice(FOutputDeviceFunction Device) noexcept;

namespace Detail
{

	/** Forwards a ready-made message to the installed output device, doing nothing when none is set. */
	void DispatchLogMessage(ELogLevel Level, const char* Category, const char* Message) noexcept;

	/** Formats into a bounded stack buffer then forwards to the output device, skipping work when none is set. */
	void DispatchLogFormatted(ELogLevel Level, const char* Category, const char* Format, ...) noexcept MW_LOG_PRINTF_FORMAT;

} // namespace Detail

} // namespace MicroWorld

// Two-step paste so the level's enable flag expands before it selects an emitter.
#define MW_LOG_CONCAT_(Prefix, Suffix) Prefix##Suffix
#define MW_LOG_CONCAT(Prefix, Suffix) MW_LOG_CONCAT_(Prefix, Suffix)

// A below-floor call drops its arguments UNEVALUATED; an enabled one dispatches.
#define MW_LOG_EMIT_FORMATTED_0(Level, Category, ...) ((void)0)
#define MW_LOG_EMIT_FORMATTED_1(Level, Category, ...) \
	::MicroWorld::Detail::DispatchLogFormatted(::MicroWorld::ELogLevel::Level, (Category), __VA_ARGS__)

#define MW_LOG_EMIT_MESSAGE_0(Level, Category, Message) ((void)0)
#define MW_LOG_EMIT_MESSAGE_1(Level, Category, Message) \
	::MicroWorld::Detail::DispatchLogMessage(::MicroWorld::ELogLevel::Level, (Category), (Message))

/**
 * Logs a printf-style record at the given level and category, e.g.
 * MW_LOG(Warning, "Net", "peer %u timed out", Index). Stripped to nothing when
 * the level is below MW_LOG_MIN_LEVEL. Use MW_LOG_MSG for a runtime string that
 * may itself contain '%'.
 */
#define MW_LOG(Level, Category, ...) MW_LOG_CONCAT(MW_LOG_EMIT_FORMATTED_, MW_LOG_CONCAT(MW_LOG_ENABLED_, Level))(Level, Category, __VA_ARGS__)

/**
 * Logs an already-formed message string at the given level and category without
 * printf interpretation, e.g. MW_LOG_MSG(Log, "Boot", "ready"). Stripped to
 * nothing when the level is below MW_LOG_MIN_LEVEL.
 */
#define MW_LOG_MSG(Level, Category, Message) MW_LOG_CONCAT(MW_LOG_EMIT_MESSAGE_, MW_LOG_CONCAT(MW_LOG_ENABLED_, Level))(Level, Category, Message)
