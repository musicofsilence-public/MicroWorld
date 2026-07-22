#include <MicroWorld/Log.h>

#include <cstdarg>
#include <cstdio>

namespace MicroWorld
{

namespace
{

/** Holds the one process-global sink; nullptr means every log call is a no-op. */
FLogSink GLogSink = nullptr;

} // namespace

void SetLogSink(FLogSink Sink) noexcept
{
	GLogSink = Sink;
}

namespace Detail
{

void DispatchLogMessage(ELogLevel Level, const char* Category, const char* Message) noexcept
{
	if (GLogSink != nullptr)
	{
		GLogSink(Level, Category, Message);
	}
}

void DispatchLogFormatted(ELogLevel Level, const char* Category, const char* Format, ...) noexcept
{
	// Skip formatting entirely when no sink can consume the result.
	if (GLogSink == nullptr)
	{
		return;
	}

	// Fixed caller-stack buffer keeps formatting allocation-free; vsnprintf
	// always null-terminates and truncates rather than overflowing.
	char Message[MW_LOG_MESSAGE_CAPACITY];
	std::va_list Arguments;
	va_start(Arguments, Format);
	std::vsnprintf(Message, sizeof(Message), Format, Arguments);
	va_end(Arguments);

	GLogSink(Level, Category, Message);
}

} // namespace Detail

} // namespace MicroWorld
