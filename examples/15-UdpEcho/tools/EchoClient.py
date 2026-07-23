"""Echo + oversize probe for example 15. Usage: EchoClient.py <board-ip> [port].

Sends one normal line and prints whether the echo matched, then sends a payload
larger than the driver's UdpMaxPacketBytes (1200) and reports what came back:
- FULL-ECHO       the board echoed all 1500 bytes (unexpected on a 1200 cap)
- TRUNCATED n/m   the board silently echoed a short packet (MSG_TRUNC absent)
- no echo         the board reported Full and dropped/kept the datagram queued
This distinguishes the three real oversize outcomes rather than assuming one.
"""

import socket
import sys

board_ip = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 40404

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5.0)

normal = b"hello microworld"
sock.sendto(normal, (board_ip, port))
echo, _ = sock.recvfrom(2048)
print(f"echo: {len(echo)}B {'OK' if echo == normal else 'MISMATCH'}")

oversize = b"X" * 1500  # > UdpMaxPacketBytes (1200)
sock.sendto(oversize, (board_ip, port))
try:
    got, _ = sock.recvfrom(4096)
    verdict = "FULL-ECHO" if len(got) == len(oversize) else f"TRUNCATED {len(got)}/{len(oversize)}B"
    print(f"oversize: echoed {len(got)}B -> {verdict}")
except socket.timeout:
    print("oversize: no echo (board reported Full and dropped/kept the datagram queued)")
