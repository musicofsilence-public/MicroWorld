#include <MicroWorld/Net/NetDriver.h>

namespace MicroWorld
{

/** Defines the interface destructor out of line so one vtable entry lives in the Net archive. */
INetDriver::~INetDriver() noexcept = default;

} // namespace MicroWorld
