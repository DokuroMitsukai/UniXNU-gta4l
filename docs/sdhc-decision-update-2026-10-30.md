# gta4l SDHC decision update, 2026-10-30

Status: host-only correction `[S]`. No device write, build, flash, boot, power
change, PMIC/SPMI access or RPM vote was made for this update. **NO-GO
remains** (`sdhc-decision-memo.md`).

This update separates three questions that the earlier report conflated. The
full dependency record is `sdhc-power-pad-dependency-report.md`; this note does
not repeat it.

## 1. What the corrected UEFI handshake proves

The booted `SdccDxe.efi` (`4084bbc7...`) exit handler at `0x81a8`:

- sets the SDHCI bus-power bit in `POWER_CONTROL` (`0x29`) without selecting a
  voltage;
- writes `SOFTWARE_RESET = 1` (`0x2f`);
- polls `CORE_PWRCTL_STATUS` (`0x240`) for at most 10,001 reads and 10,001
  one-unit delays;
- writes the observed status to `CORE_PWRCTL_CLEAR` (`0x248`) and polls it to
  zero for at most 10,001 reads and 10,001 delays;
- writes `CORE_PWRCTL_CTL` (`0x24c`) with `IO_SUCCESS` (`4`) when
  `status & 0x3 == 0`, or `BUS_SUCCESS` (`1`) otherwise;
- waits for the reset bit to self-clear (at most 10,000 reads, no delay);
- writes vendor registers `0x21c`/`0x220` and clears bit 18 of `0x20c`.

`CORE_PWRCTL_CTL` carries result bits (`BUS_SUCCESS`/`BUS_FAIL`/`IO_SUCCESS`/
`IO_FAIL`), not the `BUS_OFF`/`BUS_ON`/`IO_LOW`/`IO_HIGH` request bits, which
live in `CORE_PWRCTL_STATUS`. The handler reports success for the class it saw
without performing the corresponding regulator or pad action. This proves a
controller-local reset and result-only status write, not a bus-power-on
regulator sequence `[S]`.

It does not prove that L24/L11 are on, that the SDC1 pads are at the required
voltage, or that a result-only write is electrically safe `[U]`.

## 2. What the pad decode resolves

The pinned `sm6115.dtsi` defines `west=0x00500000`, `south=0x00900000`, and
`east=0x00d00000`. The pinned `pinctrl-sm6115.c` places the SDC1 QDSD group at
`west+0x75000` and SDC2 at `south+0x73000`. The UEFI writes `0x9fe4` to
`0x00575000` and `0x1fe4` to `0x00973000`, exactly those registers.

`0x9fe4` decodes to the vendor `sdc1_*_on` configuration: data 10 mA, CMD
10 mA, CLK 16 mA, data/CMD pull-up, CLK no-pull, RCLK pull-down. The write-to-pad
mapping part of `E2` is therefore closed. The pad supply, actual levels,
pull-up source, and unpowered-drive/back-power limits remain `[U]`. The
`uefiplat.cfg` `Sdc1GpioConfigOn = 0x1E92` / `Off = 0xA00` values are a separate
configuration-map entry; their relationship to the register write is still
`[U]`.

## 3. What still needs board documentation or measurement

- PM6125 datasheet or vendor register description for L11/L24 limits,
  soft-start and fault semantics.
- eMMC `DX68MB` datasheet for VCC/VCCQ ranges, power-up sequence and inrush.
- Board schematic/netlist for test points, decoupling, UFS population and the
  SDC1 pad supply/pull-up source.
- Calibrated measurement of L24/L11, SDC1 pad levels and current at handoff.
- RPM runtime policy for L11/L24 retention.
- A qualified `E9` recovery procedure and independent review.

No software register read or DT request can close these. No bus-power,
card-clock, PMIC/SPMI, RPM or TLMM write is authorized to try. The next
justified device step remains the measurement-only baseline in
`sdhc-power-pad-dependency-report.md` §6, and it is blocked on the documents and
safe test points above `[S]`.

## References

| Reference | Use |
| --- | --- |
| `docs/sdhc-decision-memo.md` | Canonical `E1`-`E9`/`S1`-`S4` blockers and NO-GO |
| `docs/sdhc-power-pad-dependency-report.md` | Full dependency record, corrected handshake and pad decode |
| `docs/firmware-handoff.md` | Exit-handler trace and register inventory |
| `docs/emmc-reference.md` | eMMC/controller identity and pinned revisions |
