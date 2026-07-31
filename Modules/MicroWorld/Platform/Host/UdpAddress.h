#pragma once

// The 6-byte UDP address encoding is defined once in Transport (UdpAddressCodec.h) so
// both platform adapters and the shared UDP drivers agree on one layout. This
// public header keeps its historical path and forwards to the canonical codec.
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
