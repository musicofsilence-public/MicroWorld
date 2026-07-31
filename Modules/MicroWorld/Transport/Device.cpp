#include <MicroWorld/Transport/Device.h>

namespace MicroWorld
{

/** Defines the interface destructor out of line so one vtable entry lives in the Transport archive. */
IDevice::~IDevice() noexcept = default;

} // namespace MicroWorld
