# PM6125 L11/L24 SPMI register map

Status: source investigation only. No XNU code, build, boot or hardware access
was added. This records a source-backed register map for the two eMMC rails and
the read-safety analysis for using it as a handoff readback. It does not measure
either rail.

## Why this document exists

`rpm-power-interface.md` concluded that the pinned port contained no PM6125 LDO
register map, and that an SPMI read of L11/L24 would therefore be invented. That
conclusion was about the RPM transport and the board DTS. It does not hold for
the kernel's SPMI regulator driver, which does carry a PM6125 regulator table.
This document records that table, the register layout, and what it does and does
not support.

## Source pin

Same reference as `rpm-power-interface.md`:

- Repository: `https://github.com/limonabisi/linux-gta4l`
- Branch: `gta4l-7.2`
- Commit: `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`
- Raw prefix:
  `https://raw.githubusercontent.com/limonabisi/linux-gta4l/cf5f763f0ef8a5b4d8bb4163833cba1160c51894/`

Inspected files:

- `drivers/regulator/qcom_spmi-regulator.c`
- `drivers/spmi/spmi.c`
- `drivers/spmi/spmi-pmic-arb.c`
- `arch/arm64/boot/dts/qcom/pm6125.dtsi`
- `arch/arm64/boot/dts/qcom/sm6115.dtsi`
- `arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`

Local device evidence:

- `device-dump/private/gta4l-dump/live.dts` and `live-fdt.dtb`, the decompiled
  vendor device tree captured from the tablet.

## SPMI topology

| Resource | Value |
| --- | --- |
| Controller | `spmi@1c40000`, `qcom,spmi-pmic-arb` |
| `core` | `0x01c40000+0x1100` |
| `chnls` | `0x01e00000+0x2000000` |
| `obsrvr` | `0x03e00000+0x100000` |
| `intr` | `0x03f00000+0xa0000` |
| `cnfg` | `0x01c0a000+0x26000` |
| Execution environment | `qcom,ee = <0>`, `qcom,channel = <0>` |
| PM6125 | `pmic@0` USID 0 and `pmic@1` USID 1, `compatible = "qcom,pm6125", "qcom,spmi-pmic"` |

## Regulator bases

`pm6125_regulators[]` in `qcom_spmi-regulator.c` gives every regulator a base
address in the PMIC's SPMI address space:

| Rail | Base | Supply name | Board DT range |
| --- | --- | --- | --- |
| L11 | `0x4a00` | `vdd_l9_l11` | 1.704–1.952 V |
| L24 | `0x5700` | `vdd_l23_l24` | 2.704–3.600 V |

LDO windows run `0x4000 + (n - 1) * 0x100`, so L11 is the eleventh LDO window and
L24 the twenty-fourth. SMPS windows start at `0x1400`.

`[S]` `struct spmi_regulator_data` (`qcom_spmi-regulator.c:437-443`) carries
`{name, base, supply, ocp, force_type}`. The L11 entry (`:2038`) and L24 entry
(`:2051`) leave `ocp` and `force_type` zero, so the table does not select a
variant; `spmi_regulator_match()` (`:1688-1741`) reads
`TYPE`/`SUBTYPE`/`DIG_MAJOR_REV` from the hardware instead. `qcom-spmi-pmic.c`
has no `qcom,pm6125` compatible entry (`pmic_spmi_id_table[]`, `:39-72`), so it
cannot supply the variant either. The candidate LDO rows in
`supported_regulators[]` (`:1586-1678`) span both getter families, so the
voltage encoding still cannot be chosen statically.

## Address encoding

An SPMI address is 16 bits and splits into two fields:

```text
address = (peripheral << 8) | register_offset
```

The arbiter derives the target from those fields directly: every version's
offset routine (`pmic_arb_offset_v5/v7/v8`) computes
`ppid = (sid << 8) | (addr >> 8)`. So `0x4a00` is not a linear offset in one
register space; it is **peripheral `0x4a`, register `0`**.

| Rail | Peripheral | Range | Voltage | Mode | Enable |
| --- | --- | --- | --- | --- | --- |
| L11 | `0x4a` | `0x4a40` | `0x4a41` | `0x4a45` | `0x4a46` |
| L24 | `0x57` | `0x5740` | `0x5741` | `0x5745` | `0x5746` |

The peripheral byte selects *which* block a transaction targets, and the register
byte decides *which* register inside it. A write to register `0x14` of peripheral
`0x4a` would reach the L11 block, but it would not reach the enable or voltage
registers, which are registers `0x46` and `0x41`.

## Common register layout

From `enum spmi_common_regulator_registers`:

| Offset | Name | Access |
| --- | --- | --- |
| `0x01` | `DIG_MAJOR_REV` | read |
| `0x04` | `TYPE` | read |
| `0x05` | `SUBTYPE` | read |
| `0x40` | `VOLTAGE_RANGE` | read/write |
| `0x41` | `VOLTAGE_SET` | read/write |
| `0x45` | `MODE` | read/write |
| `0x46` | `ENABLE` | read/write |
| `0x48` | `PULL_DOWN` | read/write |
| `0x4c` | `SOFT_START` | read/write |
| `0x61` | `STEP_CTRL` | read/write |

`ENABLE` bit 7 is the software enable (`SPMI_COMMON_ENABLE_MASK`, `0x80`). Bits
3:0 select the hardware-enable followers.

A bounded scan of the captured `xbl_config.img` boot PMIC settings found nine
PSI2 records targeting the L11 (`0x4a`) or L24 (`0x57`) peripherals. Their
registers are `0x88`, `0x60`, `0x39`, `0x3a` and `0xe6` — none is `0x40`,
`0x41`, `0x45` or `0x46` `[S]`. The boot settings therefore write other L11/L24
registers but do not directly program the voltage, mode or enable registers in
this table; the PBS/RSBP execution semantics are unresolved `[U]`. The captured
`rpm.img` also carries LDOA resource tables for both rails, but the bounded
scan found no linked static path that reads them or writes these registers
`[S]`; the runtime class-object build, a runtime request, computed address or
relocation remains `[U]` (see `power-retention-evidence.md`
§7).

## Slave identity is not yet established

The kernel reaches these registers through an `spmi_device` whose USID comes from
the node's `reg` property: `of_spmi_register_devices()` stores `reg[0]` as
`usid` and passes it to every transaction. `pm6125.dtsi` declares the PMIC twice,
at USID 0 and USID 1, but neither node instantiates `qcom,pm6125-regulators`.

The vendor device tree captured from this tablet (`live.dts`, see
`device-dump/private/gta4l-dump/`) agrees on the topology and adds one useful
bound:

| Node | `reg` | Meaning |
| --- | --- | --- |
| `qcom,pm6125@0` | `<0x00 0x00>` | PM6125, USID 0 |
| `qcom,pm6125@1` | `<0x01 0x00>` | PM6125, USID 1 |
| `qcom,pmi632@2` | `<0x02 0x00>` | PMI632, USID 2 |

The vendor tree also confirms the arbiter parameters: `qcom,spmi@1c40000` with
`qcom,ee = <0>`, `qcom,channel = <0>`, and the same five `reg-names` windows
(`core`, `chnls`, `obsrvr`, `intr`, `cnfg`) that mainline uses.

Two facts still do not follow. The vendor tree has **no regulator subnode** under
either PM6125 USID either; the rails come from
`qcom,rpm-smd-regulator-resource` with `qcom,resource-name = "ldoa"`,
`qcom,resource-id = <0x0b>` for L11 and `qcom,set = <0x03>`. And the LDO
peripheral addresses `0x4a`/`0x57` are not named anywhere in the DT. So the board
evidence bounds the candidate set to USID 0 or 1, but does not choose between
them.

L11's vendor `regulator-min-microvolt` is `0x1a0040` (1,704,000 µV), consistent
with both the mainline range and the RPM resource id 11 — which is why the
numbering, not the USID, can be trusted.

The match criterion is `spmi_regulator_match()`: it reads the five bytes at
offset `0x01` (from `DIG_MAJOR_REV` through `SUBTYPE`) and accepts only a
`(TYPE, SUBTYPE, DIG_MAJOR_REV)` triple present in `supported_regulators[]`.
On a mismatch it fails with `-ENODEV` and logs
`unsupported regulator: name=… type=… subtype=… dig major rev=…`.

That criterion is **necessary but not sufficient**, and an earlier revision of
this document wrongly called probing candidates "cheap and safe":

- A match shows that *some* recognized regulator implementation is present at
  that address. It does not independently establish that the part is PM6125, or
  that the address is specifically L11 or L24.
- It gives no rule for a multi-candidate match, or for a candidate that matches
  while the intended rail does not.
- A non-matching or wrongly addressed peripheral has no established read
  behaviour on this silicon. The driver's match is a software check, not a
  statement that every rejected address is safe to touch.

The correct sequence is therefore to establish the slave assignment from
**pinned evidence** first, and only then consider a read path. Do not scan
candidate slaves on hardware to compensate for missing evidence.

## Voltage encoding depends on the matched operation set

`VOLTAGE_SET` (`0x41`) is not always a voltage, and not always a selector. The
matched `(TYPE, SUBTYPE, DIG_MAJOR_REV)` entry in `supported_regulators[]`
selects an ops set, and the getters differ:

| Operation set | Getter and register encoding |
| --- | --- |
| `spmi_ldo_ops`, `spmi_smps_ops` | `spmi_regulator_common_get_voltage()`: reads `0x41` and resolves the hardware range from `0x40` with `spmi_regulator_find_range()`, then converts the hardware selector with `spmi_hw_selector_to_sw()` |
| `spmi_ftsmps426_ops`, `spmi_hfs430_ops`, `spmi_hfsmps_ops` | `spmi_regulator_ftsmps426_get_voltage()`: reads two bytes at `0x40`, computes `LE16(buf) * 1000` µV, then converts that to a selector |

Two corrections to an earlier revision of this document:

- `hfs430` and `hfsmps` do **not** use the range/selector getter. Their
  `.get_voltage_sel` is `spmi_regulator_ftsmps426_get_voltage`, the same
  two-byte millivolt layout as `ftsmps426`.
- Neither getter returns microvolts. Both return a **software selector**;
  `list_voltage()` is what maps a selector back to microvolts. Reading
  `get_voltage_sel()` and labelling it as microvolts would be wrong.

`[S]` Both getter implementations ignore the return value of their initial
`spmi_vreg_read()` (`qcom_spmi-regulator.c:926`, `:942`). A diagnostic readback
must check the transaction status explicitly rather than copying that pattern.

Applying the selector decode to an FTSMPS426-layout part produces a wrong
voltage, and applying the millivolt decode to a selector part produces a number
in the wrong units. Which set applies is decided only by the subtype read from
the hardware, and the LDO subtypes in `supported_regulators[]` span both
families (`HT_P600`/`HT_P150` use `hfs430`; `P*` and `N*` use `ldo`;
`HT_N*_ST` and `L660_*` use `ftsmps426`).

So the voltage cannot be decoded from `0x41` alone and cannot be assumed from the
board DT range. Report the raw `0x40`/`0x41` bytes unmodified, and decode only
after the subtype resolves, labelling the result as derived and naming the ops set
used.

`ENABLE` is the simpler case: `spmi_regulator_probe()` sets
`enable_reg = reg->base + SPMI_COMMON_REG_ENABLE` (`0x46`) with mask `0x80` and
enable value `0x80` for every mapping, so register `0x46` bit 7 is the software
enable regardless of subtype.

## Which path owns the rails on this board

The board DTS uses `qcom,rpm-pm6125-regulators`, not `qcom,pm6125-regulators`.
The rails are programmed by RPM firmware, not by the AP's SPMI regulator driver,
and `pm6125.dtsi` does not instantiate the SPMI regulator node.

This is what makes the map useful: the SPMI registers are the *hardware* state
the RPM firmware last wrote, not a software cache. Reading `0x4a46` and `0x5746`
observes the same bits the RPM regulator votes end up programming, which is
exactly what the RPM path in `rpm-power-interface.md` cannot report.

## Read path and side effects

Two separate questions have to be answered: what the PMIC does on a read, and
what the *transport* does to perform one.

### PMIC side

`spmi_vreg_read()` is `regmap_bulk_read(vreg->regmap, vreg->base + addr, ...)`,
and the driver's own reads of `0x40`, `0x41`, `0x45` and `0x46` use that helper.

- None of the common registers is documented as clear-on-read.
- The common regulator layout in this driver contains no status, ready or
  interrupt register. That is a statement about the driver, not about the
  silicon: it does not establish that PM6125 has no such register elsewhere in
  its address space.
- The only register at `0x14` in the regulator driver is `SAW3_PMIC_STS`, a SAW3
  CPU-regulator status register, and the driver only reads it. This does not make
  `0x14` harmless in every peripheral: `spmi-pmic-arb.c` defines `0x14` as
  `QPNPINT_REG_LATCHED_CLR` and `0x16` as `QPNPINT_REG_EN_CLR` for PMIC
  peripherals that implement that interrupt layout. See the interrupt note below.

At the register level, reading a configuration register is non-destructive. This
is an inference from the examined layout, not a vendor statement: no PM6125
datasheet or register description is available in the pinned tree. It is not a
substitute for a per-address, per-silicon read-safety statement.

### Interrupt registers are a separate block

The pinned `spmi-pmic-arb.c` interrupt code writes:

| Register | Name | Effect |
| --- | --- | --- |
| `0x14` | `QPNPINT_REG_LATCHED_CLR` | clears latched interrupt bits |
| `0x16` | `QPNPINT_REG_EN_CLR` | clears interrupt enables |

That is the meaning of the `0xff` writes in the PmicDxe exit callback **for
peripherals that implement this interrupt layout**. Two consequences follow:

- These writes are not mere status observation. They can clear fault
  indications and mask interrupt delivery.
- Interrupt masking is a real state change. It is not automatically harmless,
  because interrupt paths may participate in fault reporting or other system
  behaviour.

Whether PM6125's LDO peripherals expose this exact interrupt layout at
`0x14`/`0x16` is **not** established by the pinned sources. Treat it as an
unresolved hardware question.

### Transport side

A read is **not** free of writes. In the mainline arbiter driver
(`spmi-pmic-arb.c`):

| Step | Operation |
| --- | --- |
| Resolve `ppid` for the target | reads the APID owner/mapping tables at probe (`pmic_arb_read_apid_map_*()`); if the mapping is absent the transaction fails before reaching the bus |
| Format the command | `pmic_arb_fmt_read_cmd()` builds the read command word |
| Issue the command | `pmic_arb_set_rd_cmd()` → `writel_relaxed(val, pmic_arb->rd_base + offset)`, i.e. **a write into the observer window** |
| Wait | `pmic_arb_wait_for_done()` polls the channel status with `udelay(1)`, bounded by `PMIC_ARB_TIMEOUT_US = 1000` |
| Collect data | `pmic_arb_read_data()` reads `PMIC_ARB_RDATA0`/`RDATA1` |
| Report | `pmic_arb_check_chnl_status_*()` maps status bits to done / failed / denied / dropped |

Reads use `PMIC_ARB_CHANNEL_OBS` and an offset of `ee_stride * ee + apid_stride *
apid` over the `obsrvr` region, where the strides depend on the arbiter version
(`0x10000`/`0x80` on v5, `0x8000`/`0x20` on v7, `0x40000`/`0x20` on v8). The
version is not assumed: `spmi_pmic_arb_probe()` reads `PMIC_ARB_VERSION` at
`core + 0x0` and selects ops against the thresholds `0x20010000`, `0x30000000`,
`0x50000000`, `0x70000000`, `0x80000000` and `0x80050000`. Per channel the
registers are `CMD 0x00`, `STATUS 0x08`, `RDATA0 0x18` and `RDATA1 0x1c`.

Writes use `PMIC_ARB_CHANNEL_RW` and are refused with `-EPERM` unless
`apid_data[apid].write_ee` equals this EE, so read access does not require write
ownership. The arbiter's own IRQ path does write to `irq_clear` and to
`SPMI_PIC_ACC_ENABLE_BIT`, but those writes are only reached when SPMI interrupts
are used; a status-polled read does not need them.

The expensive part is the APID mapping. `pmic_arb_ppid_to_apid_v*()` needs
`bus->apid_data[]`, which is built by the version-specific
`pmic_arb_read_apid_map_v*()` from the owner tables. A probe that skips the port
cannot resolve `sid`/`peripheral` to a channel, so a readback is not a
self-contained helper: it needs the version detection, the owner-table parse and
the observer command.

**Bound scope.** `PMIC_ARB_TIMEOUT_US = 1000` bounds the per-transaction status
poll. It does not bound the mapping-table walks, the driver probe, or the whole
call graph. A bounded transaction wait is not a bounded operation, and a
software timeout does not prevent a hazardous access that was already issued.

Consequences for this design:

- "Read-only" must mean **no PMIC register writes**. It cannot mean no MMIO
  writes, because the command itself is a write to the arbiter.
- A probe must allowlist that one observer-window command write, and must not
  allowlist any write to `chnls`, `cnfg` APID tables, `intr`, or the PMIC itself.
- Arbiter failures are observable: a denied transaction returns `-EPERM`, a
  dropped or failed one `-EIO`, and a stall `-ETIMEDOUT`.

## What a readback would and would not prove

It would prove:

- the commanded software-enable bit for each rail;
- the commanded voltage encoding — selector plus range, or raw millivolts,
  depending on the matched ops set;
- that the identifier block reports a known regulator type and subtype.

It would not prove:

- the actual rail voltage. The examined driver has no power-good or status bit in
  its common layout, and no ADC read is part of this path. That is not a claim
  that the silicon lacks such a register;
- the load or current;
- that the rail is in regulation.

## What a probe would require first

The identity requirement is now a blocker, not a probe step. A future design may
only move forward once **pinned evidence** establishes the PM6125 slave
assignment and the L11/L24 variants. Then the shape would be:

1. Opt-in boot argument; with no argument, no mapping and no access.
2. Map the `obsrvr` region, plus whatever read-only APID window the mapping
   lookup needs. Do not map `chnls` for writing.
3. Read the identifier bytes only at the addresses the pinned evidence names.
   Do not iterate candidate slaves.
4. Read `0x40`, `0x41`, `0x45`, `0x46` for both peripherals. Report raw bytes.
5. Decode voltage only when the subtype resolves, using the ops set the match
   selected: `ldo`/`smps` use the range/selector getter; `ftsmps426`, `hfs430`
   and `hfsmps` use the two-byte millivolt getter. Report the software selector,
   and label any microvolt value as derived.
6. Bound the transaction poll at 1000 µs and report `-EPERM`, `-EIO` and
   `-ETIMEDOUT` distinctly. Do not describe that bound as bounding the whole call
   graph or the mapping walk.
7. Never write a PMIC register, never enable SPMI interrupts, never register a
   regulator, never vote through RPM.

Abort conditions: the pinned assignment is absent, any transaction is denied, or
the map cannot be established without writing outside the observer command
register. On abort, report "unmeasured" rather than a state.

## What the mainline port does and does not supply

The mainline tree carries many of the values, but not a licence to access the
bus:

| Mainline supplies | Mainline does not establish here |
| --- | --- |
| L11/L24 base addresses and the common offsets | Which SPMI USID carries the LDO peripherals |
| The `supported_regulators[]` match and both voltage getters | The actual subtype/revision on this tablet without a read |
| The arbiter implementation and its register offsets | The live arbiter configuration and availability at the XNU handoff |
| Board regulator constraints and the RPM resource ids | Actual voltage, readiness or electrical limits |
| The RPM vote format | Hardware state through the RPM getter functions |

The board also uses `qcom,rpm-pm6125-regulators`, so the proven path is
RPM-owned regulation, not AP-side SPMI access. That difference matters: copying
the register map is not the same as reproducing the working design.

An earlier revision of this document claimed the pinned sources contained no
register map at all. That was wrong. The stronger statement that is actually
supported is narrower: the pinned sources do not contain every value needed to
justify direct SPMI access, and the investigation has not exhausted all
candidate sources.

## Decision

Go/no-go for this readback, on the evidence collected so far:

| Question | Status |
| --- | --- |
| Are the target addresses source-backed? | Yes — peripheral `0x4a` and `0x57`, common offsets fixed |
| Is the enable register subtype-independent? | Yes — always `0x46` bit 7 |
| Can the voltage be decoded without the hardware? | No — depends on the matched ops set |
| Is the slave USID established by a pinned source? | No — bounded to 0 or 1, not chosen |
| Are PMIC-side reads documented side-effect-free? | No vendor statement; layout inference only |
| Is a read free of writes at the transport? | No — one observer command write per transaction |
| Is the transport bound only by the 1000 µs poll? | No — mapping walks are outside that bound |
| Can the transport be reached without porting the arbiter? | No — needs version detect plus APID map parse |
| Is the board-level electrical contract established? | No |

The identity, transport-bound and electrical rows decide it. The readback is not
a small addition to the diagnostic path; it is most of a PMIC-arbiter client, and
the board-level electrical contract is not established. Nothing in the evidence
justifies writing that directly against the handoff state, so the decision is
**blocked**, with these rules if it is ever picked up:

- Establish the slave assignment and rail variants from pinned evidence first.
  Do not scan candidate slaves, and do not hard-code USID 0.
- Do not assume an ops set: `ldo`/`smps` and `ftsmps426`/`hfs430`/`hfsmps` decode
  differently, and the getters return selectors, not microvolts.
- Build it as a host-testable component first, with a fake MMIO window and an
  independent event trace, in the same shape as the existing SDHCI host-init
  probe. Note that the existing checker is not a proof of the write allowlist.
- Establish the board-level electrical contract separately.
- Keep the hardware gate opt-in and separate from any boot path that touches
  storage.

## Conclusion

A source-backed readback of the L11/L24 enable and voltage selectors is
*possible*, but it is a transport bring-up, not a register poke: it needs the
observer window, the APID mapping, and one observer command write per
transaction. It reports commanded state, not measured voltage.

If the assignment does not resolve, or the transport cannot be reached without
configuration writes, the honest answer is physical measurement, not a
substituted USID or a guessed subtype. Physical measurement is not automatically
safe either and needs qualified handling.

Nothing here unblocks the SDHCI power stage on its own. The audit record in
`pmic-audit.md` lists the corrections and the remaining blockers.
