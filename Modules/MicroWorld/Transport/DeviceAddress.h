#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>

namespace MicroWorld::Transport::Address
{

/**
 * Core owns this type now; these declarations exist only so existing Transport and Platform code keeps compiling.
 * This file disappears with the rest of the moved Transport headers.
 *
 * Motivation: Preserves the former Transport address name while callers complete the move to Core ownership.
 * Responsibilities: Alias Core's type and helper without declaring another address type or behavior.
 */
using FDeviceAddress = Core::FDeviceAddress;

/** Motivation: Keeps the former Transport helper name available while Core owns the loopback address behavior. */
using Core::MakeLoopbackAddress;

} // namespace MicroWorld::Transport::Address
