# Performance

Prefer the simple implementation until a measured application budget fails.
MicroWorld already uses fixed storage, one-pass dispatch, disabled/not-due
skips, and no tick-path allocation because those choices are both clear and
bounded.

Measure a representative fixed workload before retaining an optimization. Keep
the same behavior checks with the measurement so a faster semantic regression
is not accepted.

Host timing is comparative development evidence. A target compile proves build
and static image size, not cycle cost, heap behavior, stack margin, or hardware
behavior. Record the source, toolchain, configuration, workload, and result in
the applicable [benchmark evidence](../Modules/Core/benchmarks/Results).
