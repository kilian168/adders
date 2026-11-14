# SD/TC-PM exhaustive sweep — GPU port (not yet built or verified)

Ports the "exhaustive v2" SD/TC-PM path-aware EDP sweep from
`../"exhaustive v2"/<N>bit/sdtc_glitch_pm_pathEDP.cpp` to CUDA. Same NAF decode,
same two-inverter gate cell, same per-gate switch counting, same path-aware
delay lookup — one GPU thread per contiguous chunk of the outer (SD-value)
loop, mirroring the CPU's fork-per-chunk worker split but with far more
workers.

## Status

Written, **not compiled, not run**. `nvcc` is not installed in this
environment. The `cuda` package is available but installing it needs root:

```
sudo pacman -S cuda
```

## Build

```
./build.sh
```

## Validate before trusting any output

Run at the two widths already cross-checked on CPU and diff against the
published CSV:

```
./build/pm_exh_gpu 8  delay_tables/pm_delay_8bit.txt
./build/pm_exh_gpu 12 delay_tables/pm_delay_12bit.txt
```

Compare `AVG_SWITCHES` / `AVG_DELAY_ns` / `AVG_EDP_correct` against the
`SD/TC-PM,8,...` / `SD/TC-PM,12,...` rows in
`../"exhaustive v2"/exhaustive_path_aware_edp_results_v2.csv`. They must match
to the printed precision (the CPU binaries also assert internally on every
vector, so the CPU numbers are the trusted reference). Only once that passes
should 16/32-bit GPU output be trusted and spliced into a results CSV.

If they *don't* match, the bug is almost certainly in one of:
- the NAF digit recurrence (`naf_digits` — must use integer division "/2",
  not a right shift, though they're equivalent here since `v - d` is always
  exactly even by construction),
- the `exh_map`/`exh_signed` index-to-value remapping (off-by-one in
  `offset`/`mask`/`top`),
- the warm-up vector seeding at a chunk boundary (wrong predecessor vector
  means the first real vector's switch/delay count is wrong for just that
  one vector per chunk — negligible at huge vector counts, but would show up
  clearly at width 8 with few, small chunks).

## Known limitations of this first version

- `MAX_WIDTH` is 32, not 64: each thread carries two full `VectorState`
  structs (current + previous vector's per-gate state) in registers/local
  memory; raise it only if you've checked the occupancy/register-pressure
  hit. Width 64 is excluded outright — `1ULL << 64` is undefined behaviour,
  and 2^128 total vectors isn't reachable by exhaustive coverage regardless
  of hardware. Width 64 is what the Monte Carlo sweeps are for.
- `path_delay_lookup` is a literal, deliberately unoptimized port of the
  CPU's double loop over every (input pin, output pin) pair — correct but
  does `O(width²)` work per vector even though at most ~3 inputs and 2
  outputs are ever really connected per digit. Optimize only after the
  width-8/12 validation above passes, not before.
- No persistence/checkpointing: a run that's interrupted restarts from
  scratch. Add this if 32-bit-width runs turn out to take long enough to
  need it.

## Files

- `sdtc_pm_exhaustive_gpu.cu` — the kernel + host driver.
- `build.sh` — `nvcc` build command.
- `delay_tables/pm_delay_<N>bit.txt` — the same Nangate45 per-pin delay
  tables already extracted this session and embedded in the CPU binaries'
  `path_delay[][]` arrays (widths 2/4/8/12/16/32), copied here so this folder
  is self-contained.
