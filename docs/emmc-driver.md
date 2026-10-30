# gta4l native SDHCI v5 / eMMC core

Status: host-verified implementation `[S]`; no device run `[U]`. The core is
opt-in and has not been booted on the tablet. The on-device probe is a device
action and requires the owner's written approval plus a verified EFS/CPEFS
backup; the `sdhc-bus-power-activation` NO-GO remains for that operation.

This document records the first native eMMC implementation. It is deliberately
small: one-bit bus, legacy timing, polling, single 512-byte PIO reads and no
card writes. It is the host-testable base for later block-device publication,
DMA, interrupts and higher-speed modes.

## 1. Files

| Path | Purpose |
| --- | --- |
| `package/Gta4lEmmc.h` | Register map, command/error/result constants, host struct and ops interface |
| `package/Gta4lEmmc.c` | Portable controller, clock, power, enumeration and PIO-read core |
| `package/Gta4lEmmcProbe.c` | Opt-in XNU probe: maps MMIO, runs the core, reads blocks 0/1 and reports |
| `scripts/sim-gta4l-emmc/harness.c` | Simulated controller and eMMC card with injectable failures |
| `scripts/test-gta4l-emmc.py` | Compiles the core against the simulator, runs 25 scenarios and 3 mutation controls |
| `scripts/build-gta4l-emmc-probe.py` | Builds `libGta4lEmmcProbe.a` with the `bsd_init.o` compile record |
| `patches/xnu/gta4l-emmc-probe.patch` | Adds the opt-in preflight/run hooks to `bsd_init` |
| `board.toml` | Links `build-gta4l-emmc-probe.py` into the kernel archive list |

## 2. Sequence

### Controller and clock

1. Validate the Qualcomm v5 MCI version (major nibble 1).
2. Suppress normal/error interrupt enables and signal enables.
3. Software-reset the controller and poll for self-clear.
4. Program the maximum timeout, one-bit host control and legacy host control 2.
5. Stop the card clock, enable the SDCC1 AHB and APPS branches and poll
   `CLK_OFF` clear.
6. Program the GCC SDCC1 apps RCG to 400 kHz:

   | Field | Value |
   | --- | --- |
   | Source select | `0` (`BI_TCXO`, 19.2 MHz) |
   | Divider field | `23` (`0x17`) |
   | M | `1` |
   | N | `0xFC` (encoded `~(4-1)`) |
   | D | `0xFB` (encoded `~4`) |
   | CFG | `0x2017` (dual-edge MND mode) |

   The values are derived from the pinned `gcc-sm6115.c` frequency entry
   `F(400000, P_BI_TCXO, 12, 1, 4)` and the `clk-rcg2` encoding, not from a
   guessed divider.
7. Set `CMD_UPDATE` and poll it clear.
8. Enable the internal clock and poll `INT_STABLE`.

### Power and pads

1. Write `POWER_CONTROL = bus power | 3.3 V`.
2. Poll `CORE_PWRCTL_STATUS` for up to 500 ms.
3. If `BUS_OFF` is requested, fail; if `BUS_ON`/`IO_*` is requested, clear the
   status with the documented retry and acknowledge `BUS_SUCCESS`/`IO_SUCCESS`.
4. If no request is observed, record `0/0` and continue on the retained-rail
   assumption. The ack means the software procedure completed; it is not a
   measurement of the rail.
5. Write the UEFI-derived active SDC1 pad value `0x9FE4` to the SDC1 QDSD
   register at TLMM `0x00575000`.
6. Enable the card clock.

### Card

| Step | Command | Notes |
| --- | --- | --- |
| 1 | `CMD0` | Reset the card |
| 2 | `CMD1` | Poll OCR bit 31, 100 ms per attempt, 20 attempts |
| 3 | `CMD2` | Read the 128-bit CID; reject all-zero/all-ones |
| 4 | `CMD3` | Assign/read the RCA; reject zero |
| 5 | `CMD9` | Read the 128-bit CSD; reject all-zero/all-ones |
| 6 | `CMD7` | Select the card |
| 7 | `CMD13` | Poll until state `TRAN` and `READY_FOR_DATA` |
| 8 | `CMD8` | Read 512-byte EXT_CSD |
| 9 | `CMD17` | Read one 512-byte block |

The core parses `EXT_CSD_REV` at byte 192, `BUS_WIDTH` at byte 183 and
`SEC_COUNT` at bytes 212-215. The captured eMMC is eMMC 5.1 and uses block
addressing, so the `CMD17` argument is the logical block number, not a byte
address.

## 3. Completion and error rules

- A command succeeds only on `CC`; a data read succeeds only when `CC`, `BRR`
  and `TC` are all observed and exactly the requested bytes were read.
- Draining the expected byte count without `TC` is a failure.
- `BRR` without `TC`, `TC` without `BRR` and stale status are failures.
- Command/data timeout, CRC, index, end-bit and other controller errors map to
  distinct result codes.
- Card R1 errors are checked separately from controller status.
- Every wait has a microsecond bound and an iteration cap.
- The core never submits `CMD12`, `CMD18`, `CMD24`, `CMD25`, DMA, CQE, ICE or
  tuning operations.

## 4. Host tests

```sh
python3.11 -B scripts/test-gta4l-emmc.py
```

The test compiles `package/Gta4lEmmc.c` with `scripts/sim-gta4l-emmc/harness.c`
using `-std=c11 -Wall -Wextra -Werror`. It covers:

- successful enumeration, CID/CSD/EXT_CSD parse and capacity;
- block 0, block 1 and repeated block reads;
- block-address overflow and read-before-init;
- command timeout, command CRC and command index errors;
- data timeout, data CRC, missing `TC` and missing `BRR`;
- card-not-ready, bad CID, bad CSD and zero RCA;
- reset timeout, clock timeout, RCG busy and power-off;
- EXT_CSD data read;
- mutation controls for the 400 kHz CFG, M and pad values.

The test also checks the source policy: only the injected ops are used, and
the core contains no DMA, CQE, ICE, tuning or card-write command.

## 5. XNU integration

The probe is disabled unless the boot argument `gta4l-emmc-probe=1` is present.
It conflicts with `gta4l-sdhci-audit`, `gta4l-sdhci-snapshot` and
`gta4l-sdhci-host-init`; the preflight panics before mapping if more than one is
requested.

The archive is built with the same compile record as `bsd_init.o`, so the core
and probe use the selected XNU headers and ABI:

```sh
./unixnu build kernel -D ../UniXNU-gta4l
./unixnu build payload -D ../UniXNU-gta4l
./unixnu bootimg -D ../UniXNU-gta4l
```

The build report at `build/gta4l-emmc-probe/build-report.json` records the
source/header/object hashes and the archive hash. A bootstrap build with no
compile record links a no-op stub and must be rebuilt before use.

## 6. Expected report

On success the probe panics with a report containing:

- MCI version, RCA, capacity, EXT_CSD revision, bus width, OCR and EXT_CSD sum;
- CID and CSD words;
- block 0 and block 1 sums and selected words, including the protective MBR
  signature area and the GPT header fields.

On failure it reports the result code, MCI version, power/clock registers,
power-control status, GCC CFG/CMD and the last log event. The report is a
bring-up checkpoint, not a storage or filesystem result.

## 7. Limits and residual risks

- No device run exists; the probe has not been booted on the tablet `[U]`.
- The port uses retained rails. It does not measure L22/L5/L24/L11 or the pad
  levels, and it does not prove retention across suspend or shutdown `[U]`.
- The power acknowledgement is a controller-local software ack; `BUS_SUCCESS`
  does not prove the rail voltage `[U]`.
- No DMA, SMMU programming, CQE, ICE, tuning, high-speed mode, multi-block read
  or write path exists `[U]`.
- No block-device publication, mount, root switch or persistent Data path
  exists `[U]`.
- microSD/SDHC2 is not implemented; only SDHC1/eMMC is in scope `[U]`.
- The exact vendor source/binary correspondence and the firmware runtime slot
  table remain `[U]`.

The next device step is the approved probe run. The next software step is a
read-only `IOBlockStorageDevice` publication over the same core, followed by
multi-block reads and a documented stop/recovery policy.
