# gta4l bounded host-local SDHCI initialization

Status: probe implemented, static-tested, relinked into the kernel, packaged into
an 80 MiB diagnostic payload and gated against the hardware-tested v5 baseline.
The image is flashed to BOOT and verified by full image-byte readback; it has
not been booted. The owner performs the manual reboot. The completed read-only
audit is the baseline; see `sdhci-handoff-audit.md`.

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

## Implementation status

Completed on the host:

1. `package/Gta4lSdhciHostInit.c` and `.h` define the allowlist and implement
   the preflight and bounded stages.
2. `scripts/build-gta4l-sdhci-snapshot.py` compiles the real object into
   `libGta4lSdhciSnapshot.a`. The build report contains all three sources.
3. The board patch calls the preflight before the audit and the run function
   after the snapshot. The patch applies cleanly to the old `bsd_init` context.
4. `scripts/test-gta4l-sdhci-host-init.py --self-test` passes. It checks the
   read/write allowlist, exact write set, conflict check, bounded waits, helper
   stores, forbidden paths and four negative mutations.
5. Host disassembly shows MMIO stores only at controller offsets `0x34`,
   `0x38`, `0x2F` and `0x2C`; all other stores are stack or the one-shot flag.
6. The kernel relink succeeded and `bsd_init` calls preflight, audit, snapshot
   and host-init in that order.
7. The 80 MiB host-init payload was built with only
   `gta4l-sdhci-host-init=1`; the tracked `board.toml` was not modified.
8. The boot image was packaged against the v5 baseline and passes all 18
   component-gate checks. The independent dmaccess check passes the header,
   FDT and Qualcomm-ID checks; its known FV/LZMA false positives are ignored.

Build evidence:

- Source SHA-256 `bc228be74eedcda5289cc14b99e62471bbef92671b329f6120584f82e046c60c`.
- Header SHA-256 `a91f76e6c0405315e6df3bca79d067ae798f688dac7b80f665d70f569482f0d5`.
- Host-init object SHA-256
  `2dac8c6e09d510e2d2c052aa599f630eec7950cc932547dd71657de793c7f2b9`.
- Diagnostic archive SHA-256
  `3cccb9e9d25a7e93b470c718e50b53edf50b975956259b37c851538f9c730b0f`.
- Kernel SHA-256
  `ba52b6500ec3d064d173fa06fba23cb42aa4850c0bddd0df9a4178666c084646`.
- Payload kernel SHA-256
  `72eae6f0c0850ac421d0048cb6cc3697cf70db7df176b34b30703d26132bbc3f`.
- Payload image SHA-256
  `fd584c46a633d63456bff54c49b8c7315d02198fc8c74c7a46e81dd68c8ea244`.
- Boot image SHA-256
  `86c96ee8565d2639eb3d56c150f1573e8bea1da3d966f7b10c41f79c02b7485f`.
- Only `_PE_parse_boot_argn`, `_ml_io_map` and `_panic` are undefined in the
  host-init object.
- Evidence:
  `out/gta4l/ram-root-sdhci-host-init-evidence/`.

Remaining: the owner manually reboots and captures the complete
`GTA4L-SDHCI HOST INIT` report. On any failure restore
`boot-before-host-init-flash.img` and stop. The agent has issued no reboot.

Flash record:

- Pre-flash backup SHA-256
  `55d80d52a7246acec8277a8aeb40e38b2b2aac1eadef1e5d439579d86a7cd701`.
- Full partition readback SHA-256
  `73b7a586ee2498277dc77adc7b81714afa0662d36c8dfc48b8c0a2ec456e01b6`.
- Image-byte readback matches the host image
  `86c96ee8565d2639eb3d56c150f1573e8bea1da3d966f7b10c41f79c02b7485f` and
  passes all 18 component-gate checks.
- `flash-report.json` and `readback-gate-report.json` are in the evidence
  directory.
