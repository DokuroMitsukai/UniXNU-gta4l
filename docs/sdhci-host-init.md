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

The pinned mainline gta4l port is consistent with, but does not establish, the
retained-rail reading. It enables
`sdhc_1` without `vmmc-supply` or `vqmmc-supply`; `pm6125_l24a` and
`pm6125_l11a` are defined but unreferenced. Its RPM path is GLINK over
`sram@45f0000`, and its regulator driver returns cached state only, with no
hardware readback. The port relies on the eMMC/L11/L24 rail enable and voltage
state left by firmware, which it does not read back. The SDHCI
`core_vendor_spec` pad-switch bits are not inherited: the probe's `0xa9c` write
clears them and `caps == 0` prevents re-enable. That is a reference design, not
proof of the XNU-time state. See `rpm-power-interface.md` and
`linux-regulator-lifecycle.md` (pinned
`https://github.com/limonabisi/linux-gta4l`, commit
`cf5f763f0ef8a5b4d8bb4163833cba1160c51894`) for the
`drivers/mmc/host/sdhci-msm.c` line ranges.

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
- Read the register once more immediately after the stable poll read and require
  the same stable bits before any stage-4 access.
- Require `SDHCI_CLOCK_CARD_EN == 0` after the write.
- The controller may be at the existing nominal 384 MHz SDCC1 apps rate; the
  probe does not measure it and does not program a card-clock divider.

### Stage 4 — power, blocked

No power-control, regulator, VDDCX, interconnect or vendor power-request write
is issued. After the final clock confirmation, read `PRESENT_STATE`,
`POWER_CONTROL`, `INT_ENABLE`, `SIGNAL_ENABLE`, `SOFTWARE_RESET`,
`CORE_GENERICS`, `CORE_PWRCTL_STATUS`, `CORE_PWRCTL_MASK`,
`CORE_VENDOR_SPEC`, `CORE_DDR_CONFIG` and `CORE_DLL_STATUS`; the oracle
requires these final observations `[S]`. Report:

```text
HOST INIT REPORT: host-local stages complete; power stage blocked;
vendor state reported, not enforced
```

### Stage 5 — report

Report before/after values and completed stages with an intentional panic. No
card command follows. The report does not reject an unexpected vendor value or a
pending vendor power request; it only surfaces them.

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
GTA4L-SDHCI HOST INIT REPORT: host-local stages complete; power stage blocked; vendor state reported, not enforced
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
   stores and forbidden paths, and runs eight negative mutations: a direct MMIO
   store, a reordered `volatile` qualifier, a typedef-hidden store, a
   whitespace-split store, a pointer-index store, a `POWER_CONTROL` write, a
   zero timeout and a lost conflict check. The dereference-cast count (six,
   matched with a whitespace-tolerant `*`/`(` pattern) and `volatile` token
   count (eight) are asserted, `typedef` and non-volatile pointer casts are
   rejected outright, and pointer-index stores are rejected. These source
   patterns are guardrails, not a proof that no direct MMIO access exists.
   The simulator description in this item records the earlier revision. This
   static self-test's eight source mutations are separate from the simulator's
   current ten-mutation set; the current 33-scenario, ten-mutation and
   separate-implementation-oracle coverage is in
   [Host access-policy verification (Step 5)](#host-access-policy-verification-step-5)
   below.
   `scripts/test-gta4l-sdhci-host-init-sim.py` compiles the production source
   against a fake MMIO window, replaces only the six MMIO helpers with logging
   hooks, and exercises the actual stage order for conflicting arguments, an
   absent boot argument, an SDHCI map failure, a GCC-only map failure, bus power
   already on, reset timeout, unstable clock, a pending vendor power request and
   a device-side vendor change. A compiled-store inspection disassembles the
   `run` function and confirms a pointer-index mutation adds memory stores that
   the event trace would not see; it is a scoped check, not full dataflow. The
   interrupt-suppression case starts with
   nonzero `INT_ENABLE`/`SIGNAL_ENABLE` and asserts the `0x34`/`0x38` clears
   precede reset and the final enable registers are zero; a suppression-failure
   case makes the device ignore those clears and asserts no reset or clock write
   occurs. The device model now clears `POWER_CONTROL` and `CLOCK_CONTROL` on
   reset self-clear and sets `INT_STABLE` only when the clock is written with
   `INT_EN` while the modelled clock is ready, so the success path writes
   `0x0001` and reports `0x0003`. It asserts the exact write sequence in each
   case, rejects any read or write outside the allowlist, and builds a second
   probe with an injected same-value `POWER_CONTROL` write to prove that the
   event trace catches a transient write that leaves final memory unchanged. The
   vendor-request case confirms the probe reports `pst=80000000` without writing
   `CORE_PWRCTL_STATUS` or `CORE_PWRCTL_MASK`; the vendor-change case confirms
   the report surfaces the changed value without the probe writing it.
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
   https://github.com/NoNameBuilder check passes the header, FDT and Qualcomm-ID checks; its known
   FV/LZMA false positives are ignored.
10. Corrected v2 booted on the tablet and completed the host-local probe. The
    report shows `reset=00`, `clk=0003`, `pwr=00`, `ie=00000000`,
    `se=00000000`, and unchanged vendor values. The card clock bit remains
    clear. Full transcription and limits:
    `out/gta4l/ram-root-sdhci-host-init-evidence/host-init-success.txt`.

Build evidence:

These hashes describe the earlier revision that was built and booted on the
tablet. The current tree changes the completion string and the simulator/test
files, so the current source hash differs. The tablet result is not re-claimed
for the current source. The current-tree revision, dirty inventory and file
hashes are recorded in `evidence-baseline.md`.

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

Remaining: the controller-local diagnostic evidence is complete only within the
limitations in §5.7; this is not completion of the selected bus-power-only
candidate. The next milestone is a separate bus-power-only probe, and only after
the retained-rail electrical contract and a direct rail measurement are
established. The Step 4 assessment
(`sdhc-electrical-contract.md`) records the bus-power-only candidate as blocked
by every safety-critical electrical area, and the Step 6 memo
(`sdhc-decision-memo.md`) records **NO-GO** for SDHC bus-power activation. The
card clock and bus power remain off; no card command, DMA or storage write is
permitted. The agent has issued no reboot.

## Host access-policy verification (Step 5)

Status: host-only software evidence `[S]`. It uses the production
`Gta4lSdhciHostInit.c` with only the generic counter, the barrier and the six
MMIO helpers replaced by the harness. It does not establish electrical safety,
handoff rail state or permission to run on hardware `[S]`.

### 5.1 Coverage inventory

The existing simulator already exercises the production stage order,
preconditions, register offsets, write values and panic paths. It records every
hooked access, so a forbidden write followed by a restoration is still visible.
The static checker independently inspects the source for direct MMIO stores and
for the allowlist, and a compiled-store inspection disassembles `run` to confirm
that a pointer-index mutation adds stores the event trace would not see. That
disassembly check is scoped, not full dataflow `[S]`.

The current revision adds an explicit phase and sequence/time field to every
event and a versioned trace. Coverage now includes the scenario set in §5.6:
argument and mapping refusals, pre-set bus power/card clock/inhibit, unexpected
version and unavailable reads, reset and clock timeouts and deadlines, stuck
counters, delayed reset/clock completion, GCC reset/branch/RCG preconditions,
post-reset version/power/clock/interrupt changes, interrupt suppression, and
vendor power reporting; ten negative-control mutations are also applied `[S]`.

### 5.2 Declared access policy

This is the policy for the existing controller-local diagnostic (capability 1),
not for bus-power or card-clock activation `[S]`.

| Item | Policy |
| --- | --- |
| Allowed SDHCI reads | `0x24`/32, `0x29`/8, `0x2C`/16, `0x2F`/8, `0x34`/32, `0x38`/32, `0x318`/32, `0x320`/32, `0x240`/32, `0x244`/32, `0x20C`/32, `0x25C`/32, `0x208`/32 |
| Allowed GCC reads | `0x00`/32, `0x04`/32, `0x08`/32, `0x0C`/32, `0x28`/32, `0x2C`/32, `0x30`/32, `0x34`/32, `0x38`/32 |
| Allowed writes | `0x34`/32 ← `0`; `0x38`/32 ← `0`; `0x2F`/8 ← `0x01`; `0x2C`/16 ← `(old & ~0x0004) \| 0x0001` |
| Forbidden writes | `0x29`, `0x240`, `0x244`, `0x248`, `0x24C`, `0x20C`; every GCC write; every CQHCI, ICE, SMMU, data-window or command access |
| Read side effects | Reads are treated as passive; `CORE_PWRCTL_STATUS` is read but never cleared or acknowledged `[S]` |
| Ordering | `0x34` and `0x38` precede `0x2F`; `0x2F` precedes `0x2C` |
| Bounds | `MAX_POLL_ITERATIONS = 100000`; 10 ms = 192000 counter ticks; reset reads ≤ max + 1; clock reads ≤ max + 2 |
| Preconditions | v5 major version 1; bus power off; card clock off; CMD/DAT inhibit clear; SDCC1 block reset deasserted; GCC branches enabled/unhalted; apps RCG parent 4, divider 1, M/N off; conflicting diagnostics refuse before mapping; absent argument performs no mapping or access |
| Failure behavior | Panic with the stage, last value and counter-advanced flag; no later stage runs; no write outside the contract |

The policy intentionally keeps bus-power enable, card-clock enable, power-status
acknowledgement and PMIC/RPM/SPMI activity prohibited `[S]`.

### 5.3 Deterministic execution

The harness uses scripted inputs and a modeled counter, not wall-clock sleeps or
uncontrolled threads `[S]`. The modeled cases are: valid success; conflicting or
absent arguments; SDHCI or GCC map failure; bus power, card clock or inhibit
already set; unexpected MCI version; an all-ones unavailable read; reset
timeout; internal-clock timeout; reset deadline expiry; clock deadline expiry;
stuck reset and clock counters; delayed reset and clock completion; GCC
reset/branch/RCG precondition failures; post-reset version/power/clock/interrupt
changes; interrupt-clear failure; a pending vendor power request; a vendor
status value that changes after the first read; and every poll's fixed
iteration bound `[S]`.

Unknown hardware semantics remain explicit assumptions: the fake device
self-clears reset, sets `INT_STABLE` only after `INT_EN`, and may ignore an
interrupt clear or change the power status `[S]`. These are test models, not
claims about the silicon `[C]`.

### 5.4 Trace format and capture

`GTA4L_SDHCI_TRACE=1` makes the harness emit:

```text
TRACE v1 scenario=<name> events=<count>
TRACE v1 seq=<n> time=<ticks> space=<sdhci|gcc> off=<offset> width=<bits> dir=<r|w> value=<value> phase=<tag>
```

`seq` is the access index, `time` is the modeled counter, `space` distinguishes
the controller from GCC, and `phase` is one of `capture`, `irq-suppress`,
`reset`, `clock`, `other-write` or `gcc-write` `[S]`. The event capacity is
400000; overflow fails the run rather than silently truncating `[S]`. The trace
is emitted for every scenario, including panics, so intermediate accesses are
checked rather than only the final register snapshot `[S]`.

### 5.5 Separate-implementation oracle (same author)

`scripts/oracle-gta4l-sdhci-host-init.py` is a separate implementation. It
derives its read rules from the header's `REG_*`/`WIDTH_*` definitions, its four
write rules and their expected values from the `## Write contract` table in this
document, its forbidden write offsets from the "No other write is permitted"
list, and its phase/order checks from the stage definitions `[S]`. It does not
read the harness `allowed_reads`/`allowed_writes` arrays `[S]`.

The oracle runs all 33 traced scenarios and checks sequence/time monotonicity,
address/width/direction allowlists, forbidden writes even if later restored,
write values (including the clock expression against the preceding read),
duplicate writes, prerequisite reads before the first write and their required
values, interrupt-clear confirmation before reset on both the initially-zero
and initially-nonzero paths, including every post-clear readback so a transient
nonzero cannot be masked by a later zero, reset self-clear before the clock
write, ordering, required and forbidden writes, post-reset version/power/clock
interrupt observations in the reset-to-clock window, the internal-clock-stable
poll completion, the separate final clock confirmation as the immediate next
access, both preceding any downstream stage-4 access, the eleven final stage-4
observations after the clock confirmation, universal stage-local reset and clock
poll bounds, phase tags and the expected report or panic text `[S]`. It also
constructs forty-one synthetic traces that must be rejected: every read removed,
an already-on bus-power precondition value, the reset-clear observation moved
before the reset write, the internal-clock-stable observation removed,
post-reset observations moved after the clock write, the interrupt readback
removed, the interrupt readback changed to nonzero, transient nonzero
`INT_ENABLE` and `SIGNAL_ENABLE` readbacks before clear readbacks (two traces),
a final unstable clock after
an earlier stable sample, the separate final clock confirmation removed, a
clock poll overrun with a final confirmation, the final confirmation moved after
downstream reads, both clock reads moved after downstream reads, all final
observations removed, each of the eleven final observations individually
removed (eleven traces), each of the eleven final observations moved before the
clock confirmation (eleven traces), and missing, nonzero and transient
confirmation readbacks for both registers in an initially-zero interrupt trace
(four traces) `[S]`. It separately
verifies that the initially-zero trace with valid confirmation readbacks is
accepted `[S]`. The `vendor-arbitrary` scenario executes the probe with
`CORE_PWRCTL_STATUS = 0x5a5a5a5a`, requires the report to carry
`pst=5a5a5a5a`, and checks that the status, mask, bus-power and card-clock
registers are unchanged `[S]`. It reports 22 read rules, 4 write rules, 6
forbidden write offsets and a 100000-iteration poll bound `[S]`. The oracle
asserts its own synthetic-control count, and
`scripts/test-gta4l-sdhci-evidence-counts.py` requires every documented
scenario, mutation and synthetic count in this document, the decision memo and
`porting.toml` to match the executable sources `[S]`.
It is tool-independent of the simulator's expectations but is still same-author
software, not an independent human electrical review `[S]`.

### 5.6 Scenarios and mutation results

| Scenario | Models |
| --- | --- |
| `conflict` | Conflicting diagnostic arguments refused before mapping |
| `no-argument` | Probe argument absent; no mapping or access |
| `map-failure` | SDHCI mapping fails |
| `gcc-map-failure` | GCC mapping fails |
| `bus-power` | Bus power already on before writes |
| `reset-timeout` | Reset never self-clears; bounded poll; panic |
| `clock-timeout` | Internal clock never stable; bounded poll; panic |
| `reset-deadline` | Counter advances past the deadline; reset poll stops early; panic |
| `clock-deadline` | Counter advances past the deadline; clock poll stops early; panic |
| `stuck-counter` | Counter does not advance; reset poll stops at the iteration cap; panic |
| `clock-stuck-counter` | Counter does not advance; clock poll stops at the iteration cap; panic |
| `reset-delayed-success` | Reset self-clears late but within the deadline; success |
| `clock-delayed-success` | Internal clock becomes stable late but within the deadline; success |
| `gcc-reset-asserted` | SDCC1 block reset asserted; refuse before writes |
| `gcc-apps-branch` | Apps branch not running; refuse before writes |
| `gcc-ahb-branch` | AHB branch not running; refuse before writes |
| `gcc-ice-branch` | ICE branch not running; refuse before writes |
| `rcg-busy` | Apps RCG busy or off; refuse before writes |
| `rcg-config` | Apps RCG parent/divider/mode mismatch; refuse before writes |
| `post-reset-version` | MCI version changes after reset; panic before the clock write |
| `post-reset-power` | Bus power reappears after reset; panic before the clock write |
| `post-reset-clock` | Card clock reappears after reset; panic before the clock write |
| `post-reset-irq` | Interrupts re-enabled after reset; panic before the clock write |
| `irq-suppress` | Nonzero enable registers; clears precede reset; success |
| `irq-suppress-failure` | Device ignores enable clears; panic before reset/clock |
| `vendor-power` | Pending `CORE_PWRCTL_STATUS = 0x80000000`; report only |
| `vendor-change` | Status changes after the first read; report only |
| `vendor-arbitrary` | Arbitrary `CORE_PWRCTL_STATUS = 0x5a5a5a5a`; reported, not acknowledged |
| `unexpected-version` | Wrong MCI major version; refuse before GCC mapping |
| `read-unavailable` | All-ones read; refused as an unexpected version |
| `card-clock` | Card clock already on; refuse before writes |
| `inhibit` | Command/data inhibit set; refuse before writes |
| `status-change` | Power status changes after the first read; report only |

All 33 scenarios pass `[S]`. The ten mutations are applied to a copy of the
production source and must fail deterministically `[S]`:

| Mutation | Injected violation | Rejected by |
| --- | --- | --- |
| `unexpected-address` | Write to `0x30` (`INT_STATUS`) | Simulator and oracle: forbidden write |
| `unexpected-width` | Reset write changed from 8-bit to 32-bit | Simulator and oracle: forbidden width |
| `premature-ack` | Write to `0x24C` (`CORE_PWRCTL_CTL`) | Simulator and oracle: forbidden write |
| `missing-prerequisite` | Bus-power check removed | Simulator and oracle: writes in the refusal scenario |
| `excessive-polling` | Reset loop bound doubled | Simulator and oracle: poll bound exceeded |
| `false-success` | Reset poll returns success on timeout | Simulator: missing timeout panic; oracle: downstream clock write / missing panic text |
| `wrong-reset-value` | Reset write changed from `0x01` to `0x02` | Simulator: reset timeout; oracle: wrong write value |
| `wrong-clock-value` | Clock write changed to `0x0005` (card clock set) | Simulator: card-clock panic; oracle: wrong clock value |
| `interrupts-enabled` | `INT_ENABLE` clear changed to `0xFFFFFFFF` | Simulator: suppression failure; oracle: wrong write value |
| `duplicate-write` | Second `INT_ENABLE` clear after the clock write | Simulator: unexpected write; oracle: repeated write |

The oracle's mutation pass is a negative control for the oracle itself: each
mutated trace must produce at least one independent policy or behavior finding,
not merely a harness failure `[S]`. The forty-one synthetic traces described in
§5.5 are also rejected by their corresponding rules `[S]`.

Reproduce the host evidence with:

```sh
python3.11 -B scripts/test-gta4l-sdhci-host-init-sim.py
python3.11 -B scripts/oracle-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-evidence-counts.py
```

### 5.7 Coverage and limitations

| Area | Exercised | Substituted or omitted | Limit |
| --- | --- | --- | --- |
| Production control flow | Yes; only counter, barrier and MMIO helpers are replaced | Compiler/kernel runtime, real MMIO ordering | Host compiler and model only `[S]` |
| Register accesses | Every hooked read/write is recorded | Direct stores are guarded statically and by scoped disassembly | Not full dataflow; a sufficiently indirect store could escape `[S]` |
| Bus faults | All-ones unavailable read | Timeout, abort, partial transaction, retry | No bus-fault model `[U]` |
| Asynchronous events | Status change after first read | Interrupt delivery, power IRQ, concurrent device activity | No interrupt or concurrency model `[U]` |
| Timing | Modeled counter and fixed iteration bounds | Wall-clock delays, MMIO hangs, scheduler preemption | No real-time or hang model `[U]` |
| Bus-power activation | Not exercised; prohibited | Bus power, card clock, vendor acknowledgement, PMIC/RPM/SPMI | Not covered by this policy `[S]` |
| DMA/SMMU/ICC/CQE/ICE | Not exercised | All data-path and fabric state | Out of scope `[S]` |
| Electrical behavior | Not exercised | Voltage, current, transients, sequencing, back-powering | Requires separate measurement approval `[U]` |
| Oracle separation | Separate script and separate policy derivation | Same author; no independent human review | Software cross-check only `[S]` |

Step 5 exits with all 33 host scenarios passing, all ten deliberate source
mutations and forty-one synthetic trace controls rejected, and the
initially-zero interrupt baseline accepted `[S]`. The checks actually performed
were: the static access checks; every simulator scenario and mutation; the
separate-implementation oracle's prerequisite, ordering, poll-bound and
final-observation rules; individual removal and move-before-confirmation of
each of the eleven final stage-4 observations; removal of the whole final set;
clock-confirmation adjacency; and the `vendor-arbitrary` report and
unchanged-register checks `[S]`. The results establish only that the captured
host traces match the declared controller-local access policy and that the
tested mutations are rejected `[S]`.

That is not a complete correctness or safety claim `[S]`. The oracle does not
prove that every permitted access is necessary, that an observed value is the
correct value beyond the specific reset-clear, clock-stable and
interrupt-clear checks, or that the probe reports every observation faithfully;
it checks that the required final observations occur and that the harness text
matches its expected outcomes `[S]`. The model omits bus faults, interrupts,
concurrency, wall-clock timing and hangs, and the same-author oracle is not an
independent human review `[S]`; the remaining limits are in the table above.
An access being permitted, a required observation actually occurring, an
observed value being enforced and reported text matching an observation are
separate properties and are not interchangeable `[S]`. This completion is
limited to capability 1; it does not supply a policy, oracle or evidence pass
for the selected bus-power-only candidate, which stays blocked and deferred to
the Step 4 and Step 6 gates `[S]`. This is software evidence only; it is not
permission to run the probe on hardware or to activate bus power or the card
clock `[S]`.

## Next power milestone

The source trace is complete; `rpm-power-interface.md` records the pinned
mainline evidence. The Mu-Silicium `ExitBootServices` trace is recorded in
`firmware-handoff.md`; it shows the SDCC exit path configures pads, sets SDHCI
bus power and runs the v5 power handshake, but the reset clears the standard
power and clock registers and the handler does not re-enable bus power. It does
not name L24/L11. The mainline port uses
RPM GLINK, not the live FDT's legacy SMD path, and has no regulator readback.
It does not wire L24/L11 to `sdhc_1`, so its eMMC operation depends on retained
firmware state. That answers the source questions:

- the RPM firmware controls the PMIC, and if the bootloader/RPM programming is
  retained through handoff the rails stay in that state; retention is not
  established here, and the mainline APPS side has no L24/L11 consumer and
  sends no vote;
- the only RPM regulator write path is the GLINK `rpm_requests` service with
  `swen`, `uv` and `ma` keys, and completion is a `msg#`/`err` response;
- L24/L11 have no mainline consumer, while L5/L22 are the removable SD-card
  rails; a new XNU registration would add ownership and shutdown behavior that
  the mainline no-supply design avoids;
- `sdhc_1` still needs `SM6115_VDDCX`, the SDHC OPP/interconnect votes and the
  SMMU/clock state before card operation; retained L24/L11 alone is not enough.

Do not treat `pwr=00`, DT `always-on` properties or power-request
acknowledgements as proof of rail state. The mainline port can acknowledge a
bus-power request after pin configuration even when it performed no regulator
action. XNU must not copy that acknowledgement without a proven retained state
or a corresponding regulator action. If no supported control or retained-state
contract is proven, stop at this successful host-local milestone.

A separate bus-power-only probe may then explicitly allowlist power operations,
bound every wait and report failures. It must not acknowledge a vendor power
request; that path needs its own approved design. The electrical contract and
direct measurement above are necessary but not sufficient: the probe also needs
its own access policy, host oracle, negative tests, recovery criteria and
explicit owner approval. It must keep the card clock disabled and issue no
command or transfer. Card identification remains a later milestone. The Step 4
contract assessment and Step 6 decision memo record that the prerequisites are
not met, so the bus-power-only probe is **NO-GO** (decision memo, dated
2026-10-29).

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
