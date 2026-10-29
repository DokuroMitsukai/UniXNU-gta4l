# PMIC/SPMI readback audit

Status: host-only audit. No device connection, hardware access, compilation,
flashing or power change was made. This record corrects the PMIC/SPMI
investigation and states what it does and does not establish.

## Why this audit exists

The work in `firmware-handoff.md`, `rpm-power-interface.md` and
`pm6125-register-map.md` was progressing toward an opt-in SPMI readback of the
PM6125 L11/L24 rails. PMIC register access can cause permanent hardware damage,
so the evidence had to be rechecked against the sources and binaries rather than
carried forward on the strength of earlier conclusions.

Several earlier statements did not survive that recheck. The documents have been
corrected; this record keeps the reasoning and the remaining blockers.

## Method

- Re-read every affected document and the related `porting.toml` entries.
- Re-derived the binary findings from the PE images rather than from cached
  notes: hashes, PE section mapping, GUIDs, interface pairings, calling
  conventions and the reachable call graph of the exit callback.
- Re-fetched the pinned kernel and DTS files and compared them byte-for-byte
  with the cached copies.
- Checked the host-init implementation, its static checker and its simulation
  harness against the claims made about them.
- Spot-checked the historical boot, rollback, payload and photographic evidence.

## Checks that passed

| Check | Result |
| --- | --- |
| `SdccDxe.efi` SHA-256 | matches `docs/firmware-handoff.md` |
| `PmicDxeLa.efi` SHA-256 | matches |
| `SPMIDxe.efi` SHA-256 | matches |
| `uefiplat.cfg` SHA-256 | matches |
| PE section VMA == file offset for the inspected images | holds |
| Cached PMIC/SPMI disassembly vs fresh disassembly | identical |
| Six pinned kernel/DTS files vs fresh fetch | byte-identical |
| Both retained copies of the live FDT | identical |
| PMIC protocol GUID at `0x1d1e8` | `cea4489f-da0d-43d9-a3b2-766977a109f7` |
| SPMI protocol GUID at `0x1d108` and `0x8048` | `fa5f306b-f47d-4ac4-a47d-882f8204ec30` |
| Rollback image vs pre-flash backup | both `55d80d52…cd701` |
| Corrected image vs flashed image bytes | both `d4aee57a…ab133` |
| Booted-image provenance for the three drivers | byte-identical to checkout in the recorded v2 image |
| Payload vs readback payload | both `0afaae8d…ac8b1` |
| Photograph SHA-256 | matches the record |
| `porting.toml` | parses |

The photograph supports the recorded controller-local `HOST INIT OK` result:
`ps=01f800f0`, `clk=0003`, `pwr=00`, `ie=0`, `se=0`, `reset=00`, with unchanged
vendor values.

These checks establish input consistency and a historical controller-local
result. They do not establish PMIC access safety.

## Findings

### 1. The complete callback's safety is unproven

`firmware-handoff.md` previously concluded that the callback "cannot change the
rails". The constants support something narrower: the two traced callees cannot
directly address the listed control registers. They do not cover the `+0x28`
arbiter interrupt status/clear, interrupt masking, or indirect paths.

Corrected: the document now states the direct-address exclusion and says
explicitly that it is not a rail-state guarantee.

### 2. The `+0x28` operation was misnamed and its effects understated

The `+0x28` entry resolves to `0x164c`, which calls `0x2d48`, which calls
`0x2e20`. `0x2e20` reads a bitmap, iterates selected channel positions, reads two
registers and copies the low eight bits of one into the other through
read/modify/write.

There is no demonstrated queue-drain or commit behaviour. Earlier text called it
a flush and claimed the transport was "fully resolved"; both were wrong.

The instruction-level trace is now recorded in `spmi-0x28-analysis.md`: the
channel byte is loaded from `config[0]` (not from the descriptor), `0x2e20` reads
`base+0x200104+n*0x10000`, copies the low eight bits into
`base+0x200108+n*0x10000`, and skips unset bitmap bits. The status/clear pair's
low byte is always `0x04`/`0x08`, and more importantly `0x2e20` has exactly one
store, to offset `0x108` in the selected channel window; it does not write the
command register at offset `0x00`, so it does not issue an SPMI transaction to
the LDO control registers `0x40`/`0x41`/`0x45`/`0x46`/`0x48`/`0x4c`/`0x61`. Under the v5
interpretation the pair is `irq_status`/`irq_clear`, and the image's initial
`.data` base is `0x01C00000`; see `spmi-0x28-analysis.md`.

### 3. The transport has recovery behaviour beyond one command

`0x27bc` can reach `0x3904`, which reaches `0x39ec`, `0x3350` and `0x31d0`. Under
its own configuration and failure conditions this path writes the arbiter owner
and APID mapping registers at `base+apid*4+0xa700` and `base+apid*4+0x40900`
(`0x3ab8`, `0x3b0c`) and retries. It is not limited to software channel-mapping
structures.

This does not establish that recovery ran in the recorded boot, but it
invalidates the model "one command, bounded wait, return".

### 4. Voltage decoding was wrong

`pm6125-register-map.md` grouped `hfs430` and `hfsmps` with the range/selector
getter. The pinned source shows their `.get_voltage_sel` is
`spmi_regulator_ftsmps426_get_voltage`, the two-byte millivolt layout.

The same document also implied that getter returns microvolts. Neither getter
returns microvolts; both return a **software selector**, and `list_voltage()`
maps a selector back to microvolts.

Corrected in `pm6125-register-map.md` and in `porting.toml`.

### 5. Candidate-USID scanning was not justified

The document called reading candidate USIDs "cheap and safe" and gave a
`supported_regulators[]` match as the acceptance test.

A match shows that some recognized regulator implementation is present. It does
not independently establish PM6125 identity, that the address is L11/L24, which
USID is correct, what to do on a multi-candidate match, or that a rejected
address is safe to read.

Corrected: the document now requires pinned evidence for the assignment and
prohibits scanning slaves on hardware.

### 6. Interrupt semantics were available and were missed

The pinned `spmi-pmic-arb.c` defines `0x14` as `QPNPINT_REG_LATCHED_CLR` and
`0x16` as `QPNPINT_REG_EN_CLR`. On peripherals with that layout the `0xff` writes
clear latched interrupts and clear interrupt enables.

That is a state change, not observation. It can drop fault indications and mask
interrupt delivery. PM6125-specific applicability is still unresolved.

### 7. The `0x??d1` write is not necessarily zero

Its value is `u16 >> 14`, while the entry filter tests bits 12–13, so the written
value can be `0`–`3`. The earlier zero-value description was wrong.

### 8. Register-definition absence was treated as hardware knowledge

Statements such as "no ready/status register exists" and "an unspecified
register cannot affect the rail" were not established. What the evidence
supports is narrower: the examined driver does not supply those semantics.

Likewise, `regmap_bulk_read()` describes how Linux reads a register; it does not
prove that every proposed address, slave and silicon revision has no read side
effect.

### 9. The electrical contract is absent

No inspected material establishes:

- the exact regulator variant and physical net mapping;
- all consumers sharing either rail;
- safe sequencing between supply, I/O rail, pads and clocks;
- absolute-maximum ratings and permissible transient behaviour;
- back-powering conditions;
- the consequences of disabling fault-related interrupts;
- a qualified recovery procedure.

Device-tree voltage ranges are not substitutes for these facts. A software
rollback cannot repair electrical damage, and a polling bound cannot prevent a
hazardous transaction already issued. The itemized inventory and the resulting
path recommendation are in `power-path-decision.md`; they do not change this
conclusion.

### 10. The evidence chain has three distinct layers

The audit confirmed that the checkout binaries are self-consistent. It did not
establish that those exact DXE bytes were embedded in the firmware used for each
recorded tablet test. The device capture describes itself as post-unlock and
current-state, not independently authenticated stock firmware.

Analysis of the checkout, the captured firmware and the actually booted firmware
should not be treated as the same evidence.

Scoped update: the chain is now established for the three analysed drivers in one
recorded image. `scripts/extract-gta4l-firmware.py` extracted the boot image
`out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img`
(SHA-256 `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`)
through the Android boot image, gzip kernel, BootShim, outer FV, GUIDed LZMA
section and inner FV, and found `SPMIDxe.efi`, `SdccDxe.efi` and `PmicDxeLa.efi`
byte-identical to the checkout files (PE SHA-256 `50be2e1a…`, `4084bbc7…`,
`db541f63…`; details in `firmware-handoff.md`).

That establishes provenance for those three modules in that image. It does not
authenticate stock firmware, cover the other modules in the image (`DxeCore` and
`UniXNURamDiskDxe` differ from the current build), qualify PMGR publication, or
measure any rail. The three layers remain distinct; only the three-driver link for
this image is closed.

### 11. The static checker is not a proof of the write allowlist

`scripts/test-gta4l-sdhci-host-init.py` passes its own self-test.

An in-memory mutation containing a forbidden GCC write, written with a different
valid C qualifier ordering, was **accepted** by the checker. No file was changed
and no modified C code was executed.

The checker detects some regressions through regular expressions and token
counts. It cannot support a claim that arbitrary forbidden stores are excluded.

### 12. The simulation harness is useful but limited

`scripts/sim-gta4l-sdhci-host-init/harness.c` models the register window and the
stage order. The earlier background-thread device model was removed: the
simulated reset self-clear now runs deterministically from the counter hook, so
the probe's bounded poll cannot lose a scheduling race. Cases cover conflicting
arguments, an absent boot argument, a failed mapping, bus power already on,
reset timeout, unstable clock, a pending vendor power request and a device-side
vendor change.

The harness replaces the production read/write helpers with logging hooks and
records every MMIO access, including a write that restores the original value.
`scripts/test-gta4l-sdhci-host-init-sim.py` builds a second probe with an
injected same-value `POWER_CONTROL` write and requires the trace to reject it;
the normal probes must show only the four contract writes.

Remaining limits:

- no electrical, bus-fault, read-side-effect or asynchronous regulator model;
- the trace is host-side and does not prove the generated kernel code path;
- the harness cannot qualify a PMIC transport.

### 13. The completion line is a report, not a full invariant check

The final stage prints power, reset, interrupt and vendor values but does not
reject every unexpected change. The completion line now says
`GTA4L-SDHCI HOST INIT REPORT` and states `vendor state reported, not enforced`,
so it no longer implies an invariant check. The earlier revision printed
`GTA4L-SDHCI HOST INIT OK`; historical transcripts keep that wording.

The simulator demonstrates the limit: with a pending vendor power request
(`pst=80000000`) the probe still reports completion, and the test asserts only
that the request was not acknowledged. A second case changes
`CORE_VENDOR_SPEC` from the device side after reset; the report surfaces
`vs=00000001` and the trace confirms the probe did not write the register. The
probe does not refuse either condition.

The historical photograph shows the expected values for that run. It does not
turn the reporting path into a general safety guarantee.

### EBS-callback inventory (Stage 2)

An earlier pass traced only `SdccDxe` and `PmicDxe` and left the other
`ExitBootServices` registrants `[U]`. The Stage-2 pass disassembled every
registration in the shipping set and followed each notification body. The
inventory is recorded in `firmware-handoff.md`; the result is:

- `DxeCore` carries the event GUID for its own core handling and registers no
  callback.
- `SdccDxe`, `EnvDxe`, `CapsuleRuntimeDxe`, `UsbConfigDxe`, `UsbMsdDxe`,
  `UsbfnDwc3Dxe` and `XhciDxe` set flags, do status-code lookups or tear down
  their own controllers; none locates the PMIC or SPMI protocol.
- The `SdccDxe` and `EnvDxe` status-code `LocateProtocol` targets are
  `gEfiStatusCodeRuntimeProtocolGuid` (`D2B2B828-0826-48A7-B3DF-983C006024F0`),
  so those two callbacks are benign `[S]`.
- `PmicDxe` is the only EBS body that reaches SPMI. The path is
  `0x14dc → 0x4220 → LocateProtocol(PMIC) → interface +0x08 = 0x41ec →
  0x120a8`/`0x11cd4`, i.e. the already-recorded `0x14`/`0x16` and `0xd1`
  writes. No direct L11/L24 control-register write `[S]`.
- `GlinkDxe` registers a GLINK RPM port (`"rpm"`, `"glink_ssr"`) and `NpaDxe`
  calls the NPA protocol and uninstalls its interfaces. Neither builds an
  L11/L24 resource request in the traced body `[S]`; their remote RPM/NPA
  effect is not established `[U]`.

This closes the earlier "other modules are not traced" gap for the local
shipping volume. It does not close the runtime arbiter bitmap, the `0xd1`
target, the PM6125 `QPNPINT` applicability, or the remote RPM/NPA behaviour.

## 3C source verification

The newly fetched pinned `qcom_spmi-regulator.c` and `qcom-spmi-pmic.c` were read
directly. `pm6125_regulators[]` (`:2019-2052`) confirms L11 `0x4a00` and L24
`0x5700`, but `struct spmi_regulator_data` has no variant field and both entries
leave `force_type=0`; the ops set is selected at run time from the hardware
`TYPE`/`SUBTYPE`/`DIG_MAJOR_REV`. `qcom-spmi-pmic.c` has no `qcom,pm6125` entry
(`:39-72`), and no pinned DT instantiates `qcom,pm6125-regulators`. The USID and
the L11/L24 variant therefore remain `[U]`; the new files narrow the candidate
subtypes but do not choose a USID or an ops set. Details in
`pm6125-usid-research.md` and `pm6125-register-map.md`.

## What the mainline port supplies

The mainline tree is more informative than an earlier revision implied. It
supplies the regulator base addresses, the match table and both voltage getters,
the arbiter implementation and offsets, the board constraints and the RPM
resource ids.

It does not establish the PM6125 USID, the actual subtype on this tablet, the
live arbiter configuration at handoff, the actual voltage or readiness, or the
electrical limits. The board also uses `qcom,rpm-pm6125-regulators`, so the
proven path is RPM-owned regulation rather than AP-side SPMI access.

The correct next question is therefore not "can we invent a probe", but "can the
working Linux path and its assumptions be reconstructed from pinned sources".
That reconstruction is now recorded in `linux-regulator-lifecycle.md`: it traces
the final device tree, parent-supply resolution, registration, constraint
application, late cleanup, the MMC probe, the power IRQ, signal-voltage dispatch,
runtime PM through OPP/genpd/`rpmpd` and `icc-rpm`, and removal, and it tags each
statement as source-established, conditional or unknown. It does not answer the
electrical or firmware questions below. The electrical-contract gap inventory and
the RPM-vs-SPMI decision memo are recorded in `power-path-decision.md`; that memo
recommends the retained-rail form of RPM ownership and does not authorize a live
test.

## Corrections applied

| Area | Correction |
| --- | --- |
| Voltage decoding | `hfs430`/`hfsmps` moved to the two-byte millivolt getter; both getters documented as returning a software selector, not microvolts |
| `+0x28` entry | Renamed from "flush"; resolved as the arbiter v5 `irq_status`/`irq_clear` pair at base `0x01C00000`; "transport fully resolved" withdrawn |
| Transport model | Recovery path `0x27bc → 0x3904 → 0x39ec/0x3350/0x31d0` recorded; transaction-poll bound distinguished from the mapping walk |
| Callback safety | "Cannot change the rails" replaced with the direct-address exclusion |
| `0x14`/`0x16` | Documented as `QPNPINT_REG_LATCHED_CLR`/`EN_CLR` for the interrupt layout, with PM6125 applicability left open |
| `0xd1` value | Corrected from "zero-value" to `u16 >> 14`, which can be `0`–`3` |
| USID identification | "Cheap and safe" scanning withdrawn; pinned evidence required |
| Status registers | "No status register exists" narrowed to "the examined driver supplies none" |
| Mainline availability | The claim that pinned sources contain no register map was wrong; replaced with a supplies/does-not-establish table |
| Booted-image provenance | `extract-gta4l-firmware.py` proved SPMIDxe/SdccDxe/PmicDxeLa byte-identical to the checkout in the recorded v2 image |
| `+0x28` instruction trace | Channel source, bitmap loop, `0x200104`→`0x200108` byte copy and the access-type direct-address exclusion recorded; initial `.data` base resolved to `0x01C00000` and, under the v5 interpretation, the pair is `irq_status`/`irq_clear` |
| Static checker | Reordered-qualifier MMIO access now rejected; self-test case added |
| Simulation | Deterministic counter-serviced reset model; full MMIO event trace with a transient-write rejection test; vendor-change report case |
| Completion label | `HOST INIT OK` changed to `HOST INIT REPORT` with an explicit "vendor state reported, not enforced" qualifier |
| EBS-callback inventory | All ten registering modules traced; `SdccDxe`/`EnvDxe` status-code targets resolved; `GlinkDxe`/`NpaDxe` transport effects left `[U]` |
| Decision | "Defer" raised to "blocked" pending identity, semantics and the electrical contract |

## Findings ledger

| # | Finding | Status |
| --- | --- | --- |
| 1 | Callback safety overclaimed | Partially addressed — narrowed to the direct-address exclusion; `+0x28` resolved to arbiter IRQ status/clear, but the runtime bitmap, the clear-register read-back value and other indirect paths remain |
| 2 | `+0x28` misnamed as a flush | Corrected — resolved as the arbiter v5 IRQ status/clear pair at base `0x01C00000`; the remaining unknowns are the runtime bitmap content and the value read back from `irq_clear` |
| 3 | Transport recovery not recorded | Corrected — `0x27bc → 0x3904 → 0x39ec/0x3350/0x31d0` recorded |
| 4 | Voltage decoding wrong | Corrected — `hfs430`/`hfsmps` moved to the millivolt getter; getters documented as returning selectors |
| 5 | Candidate USID scanning unjustified | Corrected — method withdrawn; pinned evidence required |
| 6 | Interrupt semantics missed | Corrected — `LATCHED_CLR`/`EN_CLR` documented; PM6125 applicability open |
| 7 | `0xd1` value misstated | Corrected — `u16 >> 14`, range `0`–`3` |
| 8 | Absence of a register definition treated as hardware knowledge | Corrected — claims narrowed to "the examined driver supplies none" |
| 9 | Board-level electrical contract absent | Unresolved — no source in this repository |
| 10 | Evidence chain has three layers | Partially addressed — chain established for the three analysed drivers in the recorded v2 image; stock authentication and the other modules' provenance remain open |
| 11 | Static checker not a proof | Corrected as a regression fix — reordered qualifiers rejected and tested; still not a proof of the write allowlist |
| 12 | Simulation limits | Partially addressed — the thread race is removed and a full event trace with a transient-write rejection test is added; electrical, bus-fault and asynchronous models still missing |
| 13 | Completion line is not an invariant check | Partially addressed — label narrowed to `REPORT` and vendor-change reporting tested; the probe still does not enforce unexpected vendor state |
| 14 | Lifecycle reconstruction overclaimed | Corrected — seven statements narrowed in `linux-regulator-lifecycle.md`, including the pad-switch inheritance claim; remaining gaps tagged `[U]` |
| 15 | Other EBS registrants untraced | Corrected — all ten registering modules traced in the shipping volume; only `PmicDxe` reaches SPMI; `GlinkDxe`/`NpaDxe` transport effects remain `[U]` |

### Ledger counts

Recomputed from the table above: **15 entries — 10 corrected, 4 partially
addressed, 1 unresolved.**

- Corrected: 2, 3, 4, 5, 6, 7, 8, 11, 14, 15.
- Partially addressed: 1, 10, 12, 13.
- Unresolved: 9.

A corrected entry closes the documentation error, not the underlying hardware
question. The four partial entries still carry open sub-items (the runtime
`+0x28` bitmap and indirect paths; the three-layer evidence chain; the
simulator's missing electrical, bus-fault and asynchronous models; the
unenforced vendor-state report). The unresolved entry is the board-level
electrical contract.

The host-migration and toolchain records kept outside this repository add no
PMIC/SPMI findings and do not change any entry here.

Three later corrections are outside the numbered ledger because they came from
the Step 3 follow-up audit: the initialization access-enable direction, the
recovery path's arbiter owner/APID writes, and the non-returning configuration
error path in the exit callback. They are recorded in the sections above and do
not change the 15-entry counts.

## Consolidated blocker ledger

Each row states the pinned evidence, the unresolved question and the input that
would close it. "Blocked" means the closing input is a live device read, which
this audit does not authorise. The canonical IDs are defined in
`sdhc-decision-memo.md` §4: PMIC identity and fault rows map to `E8`,
rail/handoff and retention rows to `E1`/`E7`, and the board-contract row to
`E1`, `E3`–`E6` and `E9` `[S]`.

| Blocker | Pinned evidence | Unresolved question | Input that would close it |
| --- | --- | --- | --- |
| PM6125 USID for L11/L24 | `pm6125_regulators[]` bases `0x4a00`/`0x5700`; vendor/mainline DT declares PM6125 at USID 0 and 1; no regulator subnode | Which USID owns peripheral `0x4a`/`0x57` | A pinned APID/owner table, or a DT that instantiates `qcom,pm6125-regulators` under one USID |
| L11/L24 SPMI subtype and getter | `supported_regulators[]` spans `ldo` and `ftsmps426`/`hfs430`/`hfsmps`; `force_type=0` | Which `TYPE`/`SUBTYPE`/`DIG_MAJOR_REV` the silicon reports | One hardware read of `0x01`–`0x05` at `0x4a00`/`0x5700` (blocked) |
| `+0x28` runtime APID set | `.data` config: channel 0, one descriptor, 16-word bitmap | Which owner-status bits are set at callback time | A runtime `intr` bitmap dump or a pinned owner-bit statement (blocked) |
| `+0x28` clear read-back | `0x2e84`–`0x2e8c`: read `irq_clear`, replace bits 7:0, write whole word | What bits 31:8 read as, and whether writing them back is safe | Arbiter register documentation defining read-back and write semantics; a hardware read would only observe values |
| Descriptor base override | `0x38b0` stores `[0x8878]` into the base field when non-zero; `[0x8878]=0`; no direct `.text` reference stores to `0x8878` | Whether anything populates `0x8878` at run time | A write analysis covering indirect and computed stores (the current pass covers direct references only) |
| Rail voltage and enable at handoff | No measurement; only controller-local register captures | Actual L11/L24 state | A qualified physical measurement |
| Board electrical contract | No datasheet, schematic or netlist in the repository | Absmax, transient behaviour, back-powering, sequencing, recovery | Vendor documentation or measurement |
| RPM retention across suspend/shutdown/reboot | `qcom_smd-regulator.c`; no suspend ops; no `regulator-state-mem` | RPM firmware behaviour for L11/L24 | RPM firmware documentation or measurement |
| `GlinkDxe`/`NpaDxe` EBS transport effect | `GlinkDxe` `0x4a04` registers a GLINK RPM port; `NpaDxe` `0x149c` tears down NPA interfaces; neither builds an L11/L24 request | What the remote RPM/NPA side does with the port/teardown | RPM/NPA firmware documentation or a transport trace |

The first five rows are the SPMI/firmware side; the last four are electrical,
firmware-retention and transport. Closing the first five requires evidence that
is not in the current pinned set: register documentation, an owner/APID table, or
runtime state. Closing the last four requires vendor documentation or a qualified
measurement. No further static pass over the current pinned files is known to
supply any of these; that is a judgement about the present evidence, not a proof
of exhaustion.

## Historical evidence vs current tests

The tablet results in `sdhci-handoff-audit.md`, `sdhci-host-init.md` and
`porting.toml` were produced **before** this audit changed the test scripts. The
recorded hashes describe that earlier revision and have been left unchanged.

The additions made during this audit are host-only: one stricter assertion in
`test-gta4l-sdhci-host-init.py`, the deterministic device model, a full MMIO
event trace with a transient-write rejection test, and the vendor-power and
vendor-change report cases in `test-gta4l-sdhci-host-init-sim.py`. The
completion line was also changed from `HOST INIT OK` to `HOST INIT REPORT` with
an explicit "vendor state reported, not enforced" qualifier. These changes make
the current tree's tests and wording stricter; they do not re-run or re-validate
the historical tablet boot, and no new hardware evidence is claimed.

The recorded controller-local result remains valid as a statement about the
earlier revision, including its `HOST INIT OK` wording. It should not be quoted
as the result or wording of the current tests.

## What remains valid

- The PM6125 source table gives L11/L24 base addresses.
- The `+0x28` pair is the arbiter v5 `irq_status`/`irq_clear` at the image's initial `.data` base `0x01C00000`; it is not a PMIC regulator access. The clear step is a 32-bit read-modify-write that preserves bits 31:8 from the register read.
- `+0x28` targets the global configuration (the handle is discarded after the null check), and the same routine also runs during driver initialisation.
- The `+0x28` routine has exactly one store, to offset `0x108` in the selected channel window; it does not write the command register at offset `0x00`.
- `0x2ec8` writes bit 0 from its argument; the only call in this image passes 0, so initialization clears the access-enable bit rather than setting it.
- The recovery path writes arbiter owner/APID registers at `base+apid*4+0xa700` and `base+apid*4+0x40900`, not only software maps.
- The status/clear loop covers `n` = 0–511 (16 bitmap words), while the initialization APID walk covers 0–255 (the 256-entry table); these are different bounds.
- The PM6125 USID and L11/L24 variant remain unresolved after direct re-read of the pinned SPMI driver.
- The driver's software-enable field is `base+0x46`, mask `0x80`.
- The recovered PMIC and SPMI GUIDs and interface pairings are supported.
- All ten modules that register an `ExitBootServices` notification were traced; only `PmicDxe` reaches SPMI, and only with register bytes `0x14`/`0x16`/`0xd1`.
- The `SdccDxe` and `EnvDxe` status-code `LocateProtocol` targets are `gEfiStatusCodeRuntimeProtocolGuid`.
- The `GlinkDxe` and `NpaDxe` EBS bodies are GLINK/NPA transport setup and teardown; neither builds an L11/L24 resource request in the traced instructions.
- RPM regulator getters return software cache state.
- The Linux observer read path writes an arbiter command register.
- The mainline no-supply arrangement supports a retained-state hypothesis.
- The recorded host-init result is controller-local evidence.
- No XNU-handoff rail voltage or readiness measurement has been obtained.

None of these establishes a safe PMIC probe.

## Step 3 completion status

| Item | State |
| --- | --- |
| SPMIDxe `+0x28` instruction trace | Recorded; register names remain conditional on the v5 interpretation |
| PM6125 L11/L24 bases and common offsets | Recorded |
| PM6125 USID and regulator variant | Deferred; requires pinned evidence or a hardware read |
| Mu-Silicium exit path and booted provenance | Recorded for the three analysed drivers; all ten EBS registrants traced in the shipping volume |
| RPM/regulator lifecycle | Recorded for the traced paths |
| Board electrical contract | Deferred; no source in this repository |
| Rail state at handoff | Deferred; no measurement |
| SDHC power stage | Blocked |

"Recorded" here means the bounded host-side analysis is written down and its
limits are stated; it does not mean the hardware question is resolved. Deferred
items are not cleared by further editing of these documents.

## Disposition

Live PMIC/SPMI/RPM experimentation remains blocked. Bus power and the card clock
stay off, and `CORE_PWRCTL_STATUS` must not be acknowledged on the strength of
these documents.

Before any hardware proposal:

1. Establish the PM6125 slave assignment and rail variants from pinned evidence.
2. Resolve the runtime `+0x28` bitmap, the clear-register read-back value, and the indirect callback paths.
3. Establish the board-level electrical contract.
4. Rebuild any probe as a host-testable component with an independent event
   trace, and audit the generated machine code, not only the source.
5. Get an independent review that does not rely on the claims in these documents.

## Audit boundary

This is a substantially expanded audit, not an exhaustive certification of the
port or the hardware. It did not reconstruct every historical build, inspect
every firmware dependency, or establish the missing electrical facts. Those
unresolved items are blockers, not assumptions to be filled in during a live
test.
