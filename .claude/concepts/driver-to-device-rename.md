# Driver → Device Rename

## Problem

ADR 0005 classifies medium devices by the medium they expose, but eleven classes
that realise `IDevice` still use the retired `Driver` vocabulary inherited from
the Net sweep. The stale names now make the public model describe an encoding or
implementation mechanism instead of the device medium, and the remaining
case-insensitive `driver` prose would leave the rename incomplete.

## Proposed Approach

Apply the supplied rename table as the sole source of truth. Rename each class,
matching source/header/test stems, every consumer, build source list, manifest,
example, and option reference in six bounded units: portable Transport, Host
Wi-Fi, ESP32 wireless, ESP32 wired, Pico LoRa, then the case-insensitive prose
sweep. Preserve encoding-specific address types, `Esp32WifiLink`, byte-stream
interfaces, platform implementation headers, the roadmap filename, and `E32`
only on the portable E32 protocol class. Verify each unit at its stated boundary,
then run the full CTest/checker/formatting gates and inspect the final impact
radius for dangling old symbols.

## Open Questions

- Explicit approval of this concept is required before generating or executing
  the implementation plan.

## Decisions Log

- 2026-07-31: Use the user-supplied rename table as the only naming authority —
  it resolves medium-versus-encoding naming and prevents derived variants.
- 2026-07-31: Keep the work in six verification units — platform-specific
  consumers need their own compile boundary and the prose sweep is intentionally
  separate from class renames.
