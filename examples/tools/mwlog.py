#!/usr/bin/env python3
"""View MicroWorld MW_LOG output from an ESP32-S3 over its native USB port.

The examples route their console to the native USB-Serial-JTAG port (see
examples/LOGGING.md). That port re-enumerates on every reset, so a plain terminal
that opens the port once binds a dead handle and shows nothing after a reset. This
reader reopens the port whenever it goes idle or errors, so it survives the
re-enumeration -- and it never pulses DTR/RTS as a reset sequence (steady DTR=on,
RTS=off), so it will NOT knock the board into ROM download mode the way an
auto-reset monitor (including `pio device monitor`) can on these boards.

Usage:
    python mwlog.py COM5              # read until Ctrl-C
    python mwlog.py COM5 30           # read for 30 seconds, then exit
    python mwlog.py COM5 30 log.txt   # ... and also mirror the lines to log.txt

Run it with PlatformIO's bundled Python (it already has pyserial):
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe mwlog.py COM5
"""
import sys
import time

try:
    import serial
except ImportError:
    sys.exit(
        "pyserial not found -- run this with PlatformIO's Python:\n"
        r"  %USERPROFILE%\.platformio\penv\Scripts\python.exe mwlog.py COM5"
    )

IDLE_REOPEN_SECONDS = 3.0


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    port = sys.argv[1]
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else None
    outfile = sys.argv[3] if len(sys.argv) > 3 else None

    captured = []
    start = time.time()

    def time_is_up():
        return duration is not None and (time.time() - start) >= duration

    horizon = "%.0fs" % duration if duration else "until Ctrl-C"
    print("mwlog: reading %s (%s). Ctrl-C to stop." % (port, horizon), flush=True)

    try:
        while not time_is_up():
            try:
                link = serial.Serial()
                link.port = port
                link.baudrate = 115200
                link.timeout = 0.2
                link.dtr = True
                link.rts = False
                link.open()
            except Exception:
                time.sleep(0.5)  # port not ready yet (re-enumerating) -- retry
                continue

            buffer = b""
            last_data = time.time()
            try:
                while not time_is_up():
                    chunk = link.read(4096)
                    now = time.time()
                    if chunk:
                        last_data = now
                        buffer += chunk
                        while b"\n" in buffer:
                            raw, buffer = buffer.split(b"\n", 1)
                            line = raw.decode("utf-8", "replace").rstrip("\r")
                            print(line, flush=True)
                            if outfile:
                                captured.append(line)
                    elif now - last_data > IDLE_REOPEN_SECONDS:
                        break  # gone quiet: reopen to recover from a stale handle
            except Exception:
                pass
            finally:
                try:
                    link.close()
                except Exception:
                    pass
            time.sleep(0.3)
    except KeyboardInterrupt:
        print("\nmwlog: stopped.", flush=True)

    if outfile and captured:
        with open(outfile, "w", encoding="utf-8") as handle:
            handle.write("\n".join(captured) + "\n")
        print("mwlog: wrote %d lines to %s" % (len(captured), outfile), flush=True)


if __name__ == "__main__":
    main()
