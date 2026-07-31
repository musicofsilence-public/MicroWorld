#include <MicroWorld/Core/Log.h>

#include <cstdarg>
#include <cstdio>

namespace MicroWorld
{

namespace
{

	/** Holds the one process-global output device; nullptr means every log call is a no-op. */
	FOutputDeviceFunction WriteRecord = nullptr;

} // namespace

void SetOutputDevice(FOutputDeviceFunction InOutputDevice) noexcept
{
	WriteRecord = InOutputDevice;
}

void DispatchLogMessage(ELogLevel InLevel, const char* InCategory, const char* InMessage) noexcept
{
	if (WriteRecord != nullptr)
	{
		WriteRecord(InLevel, InCategory, InMessage);
	}
}

void DispatchLogFormatted(ELogLevel InLevel, const char* InCategory, const char* InFormat, ...) noexcept
{
	// Skip formatting entirely when no output device can consume the result.
	if (WriteRecord == nullptr)
	{
		return;
	}

	// Fixed caller-stack buffer keeps formatting allocation-free; vsnprintf
	// always null-terminates and truncates rather than overflowing.
	char Message[MW_LOG_MESSAGE_CAPACITY];
	std::va_list Arguments;
	va_start(Arguments, InFormat);
	std::vsnprintf(Message, sizeof(Message), InFormat, Arguments);
	va_end(Arguments);

	WriteRecord(InLevel, InCategory, Message);
}

} // namespace MicroWorld
