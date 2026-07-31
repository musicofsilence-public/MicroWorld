#include <MicroWorld/Transport/Device.h>

namespace MicroWorld::Transport::Device
{

/**
 * Motivation: Anchors the IDevice vtable in one translation unit so the interface destructor's single entry lives in the Transport archive.
 * Responsibilities: Emit one out-of-line virtual destructor definition without side effects.
 */
IDevice::~IDevice() noexcept = default;

} // namespace MicroWorld::Transport::Device
