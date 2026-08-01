#pragma once

#include <MicroWorld/Core/IO/TransportDevice.h>

namespace MicroWorld::Transport
{

/**
 * Core owns this type now; this declaration exists only so existing Transport and Platform code keeps compiling.
 * This file disappears with the rest of the moved Transport headers.
 *
 * Motivation: Preserves the former Transport result name while callers complete the move to Core ownership.
 * Responsibilities: Alias Core's result type without declaring another result vocabulary or behavior.
 */
using ETransportResult = Core::ETransportResult;

} // namespace MicroWorld::Transport
