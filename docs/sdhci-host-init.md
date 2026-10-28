# gta4l bounded host-local SDHCI initialization

Status: the first host-init image was flashed but failed at the UniXNU picker
with `Compromised Data`; BOOT was rolled back. Corrected v2 then booted and
completed the host-local stages. The owner's report shows the standard reset
self-cleared, the internal clock enabled and stable, and bus power, card clock
and vendor power state unchanged. The power stage remains blocked. The
completed read-only audit is the baseline; see `sdhci-handoff-audit.md`.

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

## Shared placement

The bounded host-init probe stays in `UniXNU-gta4l` because its preconditions,
write set and report are board handoff policy for this tablet. Reusable SM6115
controller register definitions and operations belong in
`UniXNU-sm6115-common` only when a second consumer needs the same contract; do
not move the board policy there prematurely.

A future RPM-SMD/SPMI/regulator path is different: it is an SoC-wide resource
contract, not a gta4l SDHCI detail. Implement and test it in the shared SM6115
component, then let the board select it through the DTS supplies. The SDHCI
probe must not grow a private regulator driver.

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
   `scripts/test-gta4l-sdhci-host-init-sim.py` compiles the production source
   against a fake MMIO window and exercises the actual stage order for
   conflicting arguments, bus power already on, reset timeout, unstable clock
   and an unexpected vendor power request. The vendor-request case confirms the
   probe reports `pst=80000000` without writing `CORE_PWRCTL_STATUS` or
   `CORE_PWRCTL_MASK`.
5. Host disassembly shows MMIO stores only at controller offsets `0x34`,
   `0x38`, `0x2F` and `0x2C`; all other stores are stack or the one-shot flag.
6. The kernel relink succeeded and `bsd_init` calls preflight, audit, snapshot
   and host-init in that order.
7. The first 80 MiB host-init payload was built with only
   `gta4l-sdhci-host-init=1`, but the temporary board inherited
   `packed_data_ramdisk = true`. Its ADT reserved `PackedRAMDisk` while the raw
   `N4KROOT` fixture reports `RamdiskPacked = FALSE`. DarwinBoot therefore
   returned `EFI_COMPROMISED_DATA` at the UniXNU picker.
8. The corrected payload sets `packed_data_ramdisk = false`; its ADT reserves
   `RAMDisk`. `scripts/check-gta4l-payload.py` checks the placeholder against
   the actual ramdisk layout and rejects the failed payload.
   `scripts/package-gta4l-diagnostic.py` now runs that contract check on every
   payload before publishing an image, and
   `scripts/build-gta4l-diagnostic-payload.py` builds the raw diagnostic
   payload from a temporary board with `size_mib = 80` and
   `packed_data_ramdisk = false` without modifying the tracked `board.toml`.
9. Corrected v2 is packaged against the v5 baseline and passes all 18
   component-gate checks plus the payload contract check. The independent
   dmaccess check passes the header, FDT and Qualcomm-ID checks; its known
   FV/LZMA false positives are ignored.
10. Corrected v2 booted on the tablet and completed the host-local probe. The
    report shows `reset=00`, `clk=0003`, `pwr=00`, `ie=00000000`,
    `se=00000000`, and unchanged vendor values. The card clock bit remains
    clear. Full transcription and limits:
    `out/gta4l/ram-root-sdhci-host-init-evidence/host-init-success.txt`.

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
- Failed v1 payload SHA-256
  `fd584c46a633d63456bff54c49b8c7315d02198fc8c74c7a46e81dd68c8ea244`.
- Failed v1 boot image SHA-256
  `86c96ee8565d2639eb3d56c150f1573e8bea1da3d966f7b10c41f79c02b7485f`.
- Corrected v2 payload SHA-256
  `0afaae8d66251d61c74d80427289da5e79551e52652e5236adaac55c6c8ac8b1`.
- Corrected v2 boot image SHA-256
  `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`.
- Only `_PE_parse_boot_argn`, `_ml_io_map` and `_panic` are undefined in the
  host-init object.
- Hardware result: `host-init-success.txt`; photograph
  `host-init-success-photo.jpg` SHA-256
  `672ecf61d0cb3e5675c5f511e88f3250a3f507afaf0acad84ee3153d97acc967`.
- Evidence:
  `out/gta4l/ram-root-sdhci-host-init-evidence/`.

Remaining: the host-local milestone is complete. The next milestone is a
separate power-only probe, and only after the RPM-SMD/SPMI/regulator control
contract is established. The card clock and bus power remain off; no card
command, DMA or storage write is permitted. The agent has issued no reboot.

## Next power milestone

Before any power write, trace the actual RPM-SMD regulator interface and the
existing component options for PM6125 L24/L11. Establish:

- which agent owns the rails after firmware handoff;
- how XNU requests enable/voltage and receives completion;
- shared-consumer and retention requirements;
- whether VDDCX or interconnect votes are required.

Do not treat `pwr=00`, DT `always-on` properties or power-request
acknowledgements as proof of rail state. If no supported control or
retained-state contract is available, stop at this successful host-local
milestone.

A separate power-only probe may then explicitly allowlist power operations,
bound every wait, report failures, and acknowledge a vendor request only after
the corresponding action succeeds. It must keep the card clock disabled and
issue no command or transfer. Card identification remains a later milestone.

## Failed v1 flash/rollback record

- Pre-flash backup SHA-256
  `55d80d52a7246acec8277a8aeb40e38b2b2aac1eadef1e5d439579d86a7cd701`.
- Flashed partition readback SHA-256
  `73b7a586ee2498277dc77adc7b81714afa0662d36c8dfc48b8c0a2ec456e01b6`.
- Rollback readback SHA-256
  `55d80d52a7246acec8277a8aeb40e38b2b2aac1eadef1e5d439579d86a7cd701`.
- `flash-report.json`, `readback-gate-report.json` and
  `failure-analysis.txt` are in the evidence directory.

Corrected v2 flash record:

- Fresh pre-flash backup SHA-256
  `55d80d52a7246acec8277a8aeb40e38b2b2aac1eadef1e5d439579d86a7cd701`.
- Full partition readback SHA-256
  `7ff725aacfbd5e205f98c6c71229e59908e432491cda91d2257f566d2925874a`.
- Image-byte readback matches
  `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`.
- Readback gate 18/18 pass; extracted readback payload
  `0afaae8d66251d61c74d80427289da5e79551e52652e5236adaac55c6c8ac8b1`
  passes the payload contract check.
- `flash-report-v2.json`, `readback-gate-report-v2.json` and
  `readback-payload-check-v2.json` are in the evidence directory.
