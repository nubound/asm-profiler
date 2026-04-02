# asm-profiler

`asm-profiler` is a Linux x86-64 sampling profiler for native ELF executables.
It launches a target command, samples user-space execution with
`perf_event_open(2)`, resolves sampled instruction pointers to function symbols
from the main executable, and renders a text UI of the hottest symbols.

The current implementation reports symbol-level hotspots. It does not report
per-instruction hotspots or annotated disassembly.

## Use Case

This tool is intended for:

- quickly identifying hot functions in a launched native executable
- checking whether a workload is spending time in the expected functions
- experimenting with a small Linux perf-event based profiler

## Not Implemented

This tool does not currently provide:

- annotated assembly views
- per-instruction hotspot reports
- call graphs or stack traces
- source-line mapping
- shared-library symbol resolution
- attach-to-process support

## Capabilities

- launch and profile a target command
- collect user-space samples with `perf_event_open(2)`
- aggregate samples by instruction pointer
- resolve function symbols from the main executable ELF
- handle PIE load addresses
- display ranked hotspot symbols in a `ncurses` TUI

## Implementation

- language: C
- platform: Linux x86-64
- sampling source: `PERF_TYPE_SOFTWARE` with `PERF_COUNT_SW_CPU_CLOCK`
- recorded sample fields: instruction pointer and thread id
- attribution: function symbols from the main executable only
- UI: `ncurses`

## Limitations

- profiles launched commands only
- does not attach to an existing process
- resolves symbols for the main executable only
- does not display annotated disassembly
- does not build a call graph
- redirects target standard streams to `/dev/null` while the TUI is active
- multithreaded workloads are not presented with thread-aware attribution

## Requirements

- Linux kernel with `perf_event_open`
- `gcc`
- `make`
- `pkg-config`
- `ncurses`
- `libelf`

## Build

```bash
make
```

## Run

```bash
./build/asm-profiler -- /path/to/executable [args...]
```

Example:

```bash
./build/asm-profiler -- /usr/bin/sleep 1
```

## Output

- `rank`: hotspot position, sorted by sample count
- `samples`: number of collected samples attributed to the symbol
- `address`: start address of the resolved symbol
- `symbol`: resolved function symbol name from the main executable ELF

## Output Semantics

- A sample is a statistical snapshot of the current instruction pointer, not an
  exact instruction count.
- Higher sample counts generally indicate hotter code paths.
- Output is reported at function-symbol granularity, not instruction
  granularity.
