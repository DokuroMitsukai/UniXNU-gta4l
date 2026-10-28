# gta4l bounded host-local SDHCI initialization

Status: design only. No host-init probe, no register writes and no new hardware
test exists yet. The completed read-only audit is the baseline; see
`sdhci-handoff-audit.md`.

## Purpose

The next probe may initialize only controller-local state that does not require
the eMMC rails or the card. It must not submit a command, touch the data/FIFO
window, program DMA, enable bus power, enable the card clock, acknowledge a
vendor power request, or write storage.

The probe is opt-in:

```text
gta4l-sdhci-host-init=1
```

If the argument is absent there must be no controller or GCC mapping and no
register access. If `gta4l-sdhci-audit=1` or `gta4l-sdhci-snapshot=1` is also
present, the probe must refuse before any mapping so an earlier panic cannot
hide the requested test.

## Power prerequisite

The live FDT resolves the controller supplies as RPM-SMD resources:

| Property | Phandle | Live node | Regulator |
| --- | --- | --- | --- |
| `vdd-supply` | `0x1f7` | `/soc/qcom,rpm-smd/rpm-regulator-ldoa24/regulator-l24` | `pm6125_l24` |
| `vdd-io-supply` | `0x1f0` | `/soc/qcom,rpm-smd/rpm-regulator-ldoa11/regulator-l11` | `pm6125_l11` |

The same live node reports:

- L24 range `2,704,000`–`3,600,000` µV, `qcom,init-voltage` `2,704,000` µV.
- L11 range `1,704,000`–`1,952,000` µV, `qcom,init-voltage` `1,704,000` µV.
- SDHCI `qcom,vdd-voltage-level` `2,960,000` µV.
- SDHCI `qcom,vdd-io-voltage-level` `1,800,000` µV.
- `qcom,vdd-io-always-on` and `qcom,vdd-io-lpm-sup` are present as booleans.

These are board-description values, not proof of the XNU-time rail state. The
recovery L24 reading was internally inconsistent, and the XNU handoff has not
measured either rail.

No Qualcomm RPM, RPMh, SPMI or regulator component is present in this tree.
`ShengPlatformPower.cpp` publishes the original Apple PMGR nub; it is not a
Qualcomm regulator or power-domain driver. There is therefore no supported XNU
mechanism in this tree to enable, disable or set L24/L11.

**Decision: the power stage is blocked.** The first host-init probe must keep
`SDHCI_POWER_CONTROL` bus power off, keep the card clock off, and not clear or
acknowledge `CORE_PWRCTL_STATUS`. A future power milestone must first implement
and test an XNU RPM/SPMI/regulator path or prove a retained-rail contract.

`SDHCI_POWER_CONTROL == 0` is not evidence that L24/L11 are off or on. It only
reports the controller's own bus-power bit.

## Proposed stages

### Stage 0 — preconditions

- Map only the controller 4 KiB window and the GCC SDCC1 4 KiB page.
- Read and validate the v5 MCI version; an unexpected major version refuses
  before the GCC mapping and before any write.
- Capture standard, vendor and GCC state.
- Refuse if the card clock is enabled, bus power is enabled, CMD/DAT inhibit is
  set, or the SDCC1 block reset is asserted.
- Refuse if the apps/AHB/ICE branches are not enabled and not unhalted, or if
  the apps RCG is not the observed parent-4/divider-1/MN-off configuration.
- Do not overwrite an unexpected state merely to force the test to proceed.

### Stage 1 — GCC verification, no writes

The audit already observed the only configuration this probe accepts:

- `ac=00004221`, `ahb=00000001`, `ice=00004229`: `CBCR_CLOCK_ENABLE` set and
  `CBCR_CLK_OFF` clear.
- `bcr=00000000`: SDCC1 block reset not asserted.
- `cmd=00000000`: no RCG update in progress and root not off.
- `cfg=00000401`: source select 4 (`GPLL6`), divider encoding 1, M/N mode off.
- `m=00000001`, `n=000000fe`, `d=000000fd`.

Stage 1 only reads and checks these conditions. It does not reprogram a
branch, PLL, RCG or the block reset. Preserving the already-running
configuration is safer than replaying a Linux initialization sequence.

### Stage 2 — interrupt suppression and controller reset

- If `INT_ENABLE` or `SIGNAL_ENABLE` is nonzero, write zero to suppress
  signalling for the polling-only probe. Record the prior values.
- Write `SDHCI_SOFTWARE_RESET = SDHCI_RESET_ALL` (`0x2F = 0x01`).
- Poll for self-clear with a bounded deadline.
- Capture post-reset standard and vendor state. Do not assume the pre-reset
  vendor values survive. Revalidate the MCI version and require bus power and
  card clock still off.

### Stage 3 — internal clock only

- Read `SDHCI_CLOCK_CONTROL`.
- Write `(old & ~SDHCI_CLOCK_CARD_EN) | SDHCI_CLOCK_INT_EN`; do not set the card
  clock bit.
- Poll for `SDHCI_CLOCK_INT_STABLE` and `SDHCI_CLOCK_INT_EN`.
- Require `SDHCI_CLOCK_CARD_EN == 0` after the write.
- The controller may be at the existing nominal 384 MHz SDCC1 apps rate; the
  probe does not measure it and does not program a card-clock divider.

### Stage 4 — power, blocked

No power-control, regulator, VDDCX, interconnect or vendor power-request write
is issued. Report:

```text
HOST INIT: host-local stages complete; power stage blocked
```

### Stage 5 — report

Report before/after values and completed stages with an intentional panic. No
card command follows.

## Write contract

| Offset | Width | Register | Write | Preconditions | Expected | Deadline | Failure |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0x34` | 32 | Interrupt Status Enable | `0` | mapped, version valid | reads `0` | immediate | report last value |
| `0x38` | 32 | Interrupt Signal Enable | `0` | mapped, version valid | reads `0` | immediate | report last value |
| `0x2F` | 8 | Software Reset | `SDHCI_RESET_ALL` (`0x01`) | interrupts suppressed | self-clears to `0` | 10 ms and fixed loop cap | reset timeout |
| `0x2C` | 16 | Clock Control | `(old & ~0x0004) \| 0x0001` | post-reset, card clock off | `INT_EN` and `INT_STABLE` set, `CARD_EN` clear | 10 ms and fixed loop cap | clock timeout |

No other write is permitted. In particular:

- no `SDHCI_POWER_CONTROL` write;
- no vendor `CORE_SWRST`, `CORE_PWRCTL_CLEAR`, `CORE_PWRCTL_CTL` or DLL write;
- no GCC branch/RCG/PLL/BCR write;
- no CQHCI, ICE, SMMU or data-window access;
- no command, response, data, DMA or storage access.

## Timeout and reporting

Use a monotonic source that does not depend on the XNU timebase conversion path
being available at this early point. The conservative choice is the ARM generic
counter (`CNTVCT_EL0`) with the documented 19.2 MHz SM6115 timebase:

- 10 ms budget = `192,000` counter ticks.
- Every poll also has a fixed iteration cap so a stuck counter cannot create an
  unbounded loop.
- Each failure reports the stage, last observed register value and whether the
  counter advanced.
- A deadline cannot protect against an MMIO transaction that itself hangs;
  that limitation remains.

Suggested report shape:

```text
GTA4L-SDHCI HOST INIT OK: host-local stages complete; power stage blocked
GTA4L-SDHCI HOST INIT BEFORE ps=... clk=... pwr=... ie=... se=... ...
GTA4L-SDHCI HOST INIT GCC bcr=... ac=... ahb=... ice=... cmd=... cfg=...
GTA4L-SDHCI HOST INIT AFTER ps=... clk=... pwr=... ie=... se=... reset=...
```

## Implementation and test plan

1. Add `package/Gta4lSdhciHostInit.c` and `.h`; compile them into the existing
   diagnostic archive.
2. Add the call to the board patch after the audit and snapshot calls, with the
   conflict check before any mapping.
3. Add a static test covering the read/write allowlists, exact write masks,
   forbidden offsets, conflict arguments, timeout/failure paths and absence of
   command/DMA/storage symbols.
4. Build a diagnostic payload with only `gta4l-sdhci-host-init=1`.
5. Package it with `scripts/package-gta4l-diagnostic.py` against the pinned v5
   baseline, run the component gate and the package tests.
6. Hardware test only with owner approval, a fresh BOOT backup, readback
   verification, manual reboot and a captured report. On any failure restore
   the known-good BOOT image and stop.
