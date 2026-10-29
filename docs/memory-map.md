# gta4l memory map and DarwinBoot window

Status: host analysis, 2026-10-27. No tablet boot has validated this map.

## Sources

- `device-dump/private/gta4l-dump/live.dts` (SM-T505N, Android boot, 3 GiB).
- `src/Mu-Silicium/Platforms/Samsung/gta4lPkg/Library/MemoryMapLib/MemoryMapLib.c`
  (UEFI DDR reservations).
- `src/UniXNU/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c`
  (`AllocateWindow`, `RunEnd`).
- `scripts/unixnu/devicetree.py` (ADT `dram-base`, `dram-size` and
  `darwinboot-memory-size`).

## Live DRAM

The live FDT describes three banks:

| Bank | Start | Size | End |
| --- | --- | --- | --- |
| 0 | `0x40000000` | `0x3e580000` | `0x7e580000` |
| 1 | `0x80000000` | `0x20000000` | `0xa0000000` |
| 2 | `0xa0000000` | `0x60000000` | `0x100000000` |

There is a `0x7e580000..0x80000000` hole. The physical span from
`0x40000000` to `0x100000000` is 3 GiB, but it is not one contiguous bank.

The ADT generator requires exactly one memory range. `dts/gta4l.dts` therefore
uses `0x40000000+0xc0000000` and `board.toml` keeps
`dram_size = "0xC0000000"`. This is a description of the physical span, not a
claim that all of it is free.

## Reserved regions

The live FDT marks these static regions `no-map` or reserved:

| Region | Start | End |
| --- | --- | --- |
| HYP | `0x45700000` | `0x45d00000` |
| XBL/AOP | `0x45e00000` | `0x45f40000` |
| sec_apps | `0x45fff000` | `0x46000000` |
| SMEM | `0x46000000` | `0x46200000` |
| modem | `0x4ab00000` | `0x51400000` |
| video | `0x51400000` | `0x51900000` |
| WLAN | `0x51900000` | `0x51a00000` |
| CDSP | `0x51a00000` | `0x53800000` |
| ADSP | `0x53800000` | `0x55600000` |
| IPA firmware/GSI/GPU | `0x55600000` | `0x55617000` |
| splash / display | `0x5c000000` | `0x5d000000` |
| `removed_mem` | `0x60000000` | `0x63900000` |
| WtShareMem | `0x68000000` | `0x68100000` |
| ss_plog / ramoops / sec_debug | `0x71100000` | `0x71d00000` |

The Mu-Silicium `MemoryMapLib` adds the UEFI-side reservations at
`0x40000000..0x45d20000`, `0x45e00000..0x45f40000`, `0x45fff000..0x46200000`,
`0x4ab00000..0x57500000`, `0x5c000000..0x5d000000`, `0x5f800000..0x5ff8c000`,
`0x5ff8d000..0x5ffd0000`, `0x5fff7000..0x63900000`, `0x68000000..0x68100000`
and `0x71400000..0x71d00000`.

The fixed free runs are approximately:

| Run | Size |
| --- | --- |
| `0x45d20000..0x45e00000` | 896 KiB |
| `0x45f40000..0x45fff000` | 764 KiB |
| `0x46200000..0x4ab00000` | 73 MiB |
| `0x57500000..0x5c000000` | 75 MiB |
| `0x5d000000..0x5f800000` | 40 MiB |
| `0x63900000..0x68000000` | 71 MiB |
| `0x68100000..0x71100000` | 144 MiB |
| `0x71d00000..0x7e580000` | 200.5 MiB |
| **`0x80000000..0x100000000`** | **2 GiB** |

The last run is the only one large enough for a useful DarwinBoot window. It is
free in both the live FDT and the Mu-Silicium map: all listed reservations end
below `0x80000000`.

## Decision

`board.toml` requests:

```toml
[payload.adt]
owned_memory_size = "0x7fe00000"
```

DarwinBoot scans the UEFI memory map and allocates with
`Candidate = ALIGN_VALUE(run_start, 2 MiB) + (Kernel.Base & (2 MiB - 1))`.
The prelinked kernel base ends in `0x7c000`, so a request for the full
`0x80000000` (2048 MiB) would end `0x7c000` past the `0x100000000` end of the
run. The 2046 MiB request leaves 1.5 MiB of headroom while still fitting the
kernel, ADT, ramdisk and payload.

The first tablet attempt with `0x80000000` produced exactly:

```text
NO WINDOW 80000000 BIG RUN 80000000 @ 80000000
```

That confirms the run is present and that the offset, not the map, caused the
failure. The next attempt uses `0x7fe00000`.

The previous `0xC0000000` (3 GiB) request could not be satisfied by any one
run either.

## Limits

- This is a static host derivation. The runtime UEFI map can differ, especially
  if firmware adds reservations or describes the hole differently.
- The first tablet boot must be treated as the validation of this window. If
  DarwinBoot reports `NO WINDOW`, capture the `BIG RUN` and `FREE` diagnostic
  rows before changing the request.
- The ADT still reports the 3 GiB physical span. Do not describe it as
  contiguous RAM or as a 3 GiB owned window.
- The DMA pools in the live FDT (`user_contig`, `qseecom`, `secure_display`,
  `adsp`, `dump`, `linux,cma`) are reusable allocations, not fixed UEFI
  reservations; XNU does not use them in this configuration.
