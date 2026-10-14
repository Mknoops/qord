# Performance

Measured with `tools/measure-startup.ps1` (5 launches with no account, 15 s idle
each; medians). Start = process launch to main window shown. Run 1 of each set
is disk-cold and excluded from the median comparison; it is listed for honesty.

Machine: Windows 11 Pro 10.0.26200, MSVC 14.51 (VS 2026), Qt 6.10.2,
MinSizeRel, `x64-windows-static-md`, ffmpeg and voice enabled (same
configuration as Acheron's CI release build).

## Baseline: Acheron `e0191a1696e72c800c629527dbec5e0671ff654e`, unmodified

| Metric | Value |
|---|---|
| Cold start, first launch (creates DB) | 2573 ms |
| Start, median | 131 ms |
| Idle working set, median | 50.9 MiB |
| Idle private bytes, median | 27.9 MiB |
| `qord.exe` size (measured as acheron.exe) | 21.95 MiB |
| Acheron tests | 7/7 passed |

## Qord after batch 1 (plugin host + 5 plugins, all disabled)

| Metric | Value | vs baseline |
|---|---|---|
| Cold start, first launch | 3132 ms | noise (disk cache) |
| Start, median | 127 ms | within noise |
| Idle working set, median | 51.2 MiB | +0.3 MiB |
| Idle private bytes, median | 27.9 MiB | ±0 |
| Binary size | 22.15 MiB | +0.20 MiB |
| Plugin host load (from log) | 0 ms | |

Disabled plugins are never constructed and have no hook-table entries (see
`tst_host_core::disabledPluginsAreNeverConstructed`). The per-message cost
with no enabled plugin for a hook is a single branch
(`tst_host_core::benchmarkIdleDispatch`).

## Budgets

- Default-enabled plugins together: at most +10% idle RSS and +100 ms start
  over the baseline. No batch-1 plugin is enabled by default.
- No polling timers. ClearURLs fetches its rules once at start when the cache
  is older than 7 days and the network permission is granted; the request is
  cancelled when the plugin stops.

Per-message hooks have Qt Test benchmarks: `TestPlugin_clearurls -bench`,
`TestPlugin_textreplace`, `TestPlugin_unindent` (`benchmark*` functions).
