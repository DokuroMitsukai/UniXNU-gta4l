# NativeEmmc reference audit (Redmi Note 9)

Status: host-only static audit `[S]`. No code was imported, built or run, and
the source repository documents no hardware result `[S]/[U]`. This audit does
not change the SDHC decision, the electrical contract or any blocker `[S]`.

## 1. Source identity

| Item | Value |
| --- | --- |
| Repository | `https://github.com/Dracen-W/NativeEmmc` |
| Maintainer | Dracen (`Dracen-W`) |
| Pinned commit | `4980f1070b0c822dedb9eb350df710a57df28590` (2026-10-25) |
| Catalog entry | `https://unixnu.com/drivers/community-native-emmc` |
| Files inspected | `README.md`, `NativeEmmc.cpp`, `LICENSE` |
| Source license | `SPDX-License-Identifier: BSD-2-Clause-Patent` in `NativeEmmc.cpp` `[S]` |
| Repository license | `LICENSE` contains the ordinary BSD 2-Clause text, without the patent clause `[S]` |
| Reported state | "Repository present. No hardware result is documented." `[S]` |

The source/license mismatch is recorded as a reuse blocker, not as a claim
about the maintainer's intent `[S]`. Any later code reuse needs the license
question resolved and a separate integration decision `[U]`.

## 2. Scope and applicability

- The driver targets the MediaTek MT6768 MSDC controller at physical
  `0x11230000` for the Xiaomi Redmi Note 9 (merlinx) `[S]`. The gta4l eMMC is
  a Qualcomm SDHCI v5/CQE controller at `0x04744000` with separate CQHCI and
  ICE ranges `[S]`. Register offsets, command encoding, FIFO layout, clock and
  reset controls are therefore not transferable `[S]`.
- The driver hardcodes the iPadOS userdata LBA `20332544` and a
  `114,597,789,696`-byte image `[S]`. The gta4l userdata partition starts at
  LBA `15,203,328` and the whole device is `61,071,360` logical sectors `[S]`;
  the constants are image-specific and must not be copied.
- The driver assumes the platform UEFI has already initialized the controller
  and card, and it does not reset, initialize, tune or write the card `[S]`.
  For gta4l, the Mu-Silicium handoff resets the controller, leaves `pwr=00`,
  does not re-enable bus power and provides no PMIC/RPM request `[S]`. The
  assumption is therefore not satisfied on this board `[S]/[U]`.
- The driver maps only the MSDC and "top" register pages and performs no
  clock, reset, regulator, interconnect or SMMU management `[S]`. Those are
  exactly the unresolved gta4l dependencies `[S]/[U]`.

## 3. Reusable design patterns

These are design references, not imported code `[S]`:

- Staged boot-argument enablement: probe 1 only snapshots registers, probe 2
  performs one read-only CMD17, and root mode publishes windows `[S]`. The
  default is a no-op when no argument is set `[S]`. This matches the gta4l
  policy of keeping hardware activation opt-in and separate from the working
  RAM-root diagnostics `[C]`.
- `IOBlockStorageDevice` publication with explicit write rejection
  (`kIOReturnNotWritable`), write-protection reporting, media-state reporting
  and a maximum-valid-block report `[S]`. This is a useful shape for a future
  gta4l read-only block device `[C]`.
- One global controller lock serializes command submission across all
  published windows, while each window has its own lock and bounce buffer `[S]`.
  This is a sound starting point for a future backend `[C]`.
- Fail-closed manifest parsing: it rejects a bad magic, version, count above
  four, bad digest, non-512-byte window size, unaligned or out-of-image
  offsets, duplicate names and overlapping windows `[S]`.
- Exact-one-match `IOMedia` lookup for the UUID-to-window mapping `[S]`. The
  hardcoded UUIDs themselves are image-specific `[S]`.

## 4. Correctness and safety findings

- Multi-block reads can return success after draining the expected byte count
  without observing `MSDC_INT_XFER_COMPL` `[S]`. The loop exits when
  `got == total`; the completion bit is only a conditional early break `[S]`.
  A future gta4l driver must require an independently confirmed completion
  condition and must not infer success from the byte count `[S]/[C]`.
- `stopTransfer()` ignores the result of `waitCommand()` and does not test the
  stop command's error status `[S]`. Data-phase error paths return without
  issuing a stop for multi-block transfers `[S]`. A future driver needs an
  explicit failure/stop policy and a confirmed end-of-transfer state `[S]/[C]`.
- The read path inspects controller interrupt error bits but does not decode
  the R1 card status in `SDC_RESP0` `[S]`. Card-side errors are therefore not
  excluded by the source's success path `[S]/[U]`.
- The source writes `SDC_ARG`, `SDC_CMD`, `MSDC_INT`, `MSDC_FIFOCS` and
  `MSDC_CFG`, and issues CMD17/CMD18/CMD12 `[S]`. "Read-only storage" here
  means no card writes; it is not passive hardware observation `[S]`.
- The manifest window SHA-256 fields are copied but never verified, and the
  root-authorization hook accepts an online `OS`/`SystemOS` window by label
  rather than by content digest `[S]`. Manifest consistency is not content
  authentication `[S]`.
- The root hook checks the boot argument and the block major/minor, then walks
  the provider chain for a trusted online `NativeEmmcBlock` `[S]`. It does not
  re-check the mounted media's UUID in that path `[S]`. This is a hardening
  question for any adapted design, not a claim of an exploit `[C]/[U]`.
- The diagnostic path panics after printing registers or a media report, and
  the publication path panics on failure `[S]`. That is acceptable for a
  bring-up probe, not for a production storage stack `[S]/[C]`.
- The repository contains only `README.md`, `NativeEmmc.cpp` and `LICENSE`;
  there are no build scripts, tests or capture artifacts `[S]`. The catalog
  page likewise documents no hardware result `[S]`.

## 5. Comparison with the gta4l blockers

- The audit adds no L11/L24 voltage, mode, enable or retention evidence `[S]`.
  E7 stays blocking `[U]`.
- The audit adds no gta4l controller, clock, reset, interconnect, SMMU, card
  or DMA result `[S]`. The storage completion criteria remain unmet `[S]`.
- The staged boot-argument pattern is compatible with the current NO-GO, but
  it does not authorize card commands, bus power or clock activation `[S]`.
- The source's firmware-left-running assumption is the opposite of the
  observed gta4l reset and controller bus-power-down handoff; it cannot be used
  to argue that the gta4l controller or card is usable after `ExitBootServices`
  `[S]/[U]`.

## 6. Reuse decision

Design reference only `[S]`. No source is copied or translated `[S]`. Any
future reuse requires:

1. a resolved license question (`BSD-2-Clause-Patent` versus the repository's
   BSD-2-Clause `LICENSE` text) `[U]`;
2. a gta4l-specific SDHCI v5 backend and access policy, not an MSDC port `[S]`;
3. the electrical-contract and E7 gates, not a firmware-state assumption `[S]`;
4. host-only tests that fail closed on completion, error and bounds defects `[C]`.

## 7. Host-only test plan for a future gta4l backend

These tests are proposed, not implemented `[C]`:

1. A source-policy test that rejects any controller command or register write
   unless the staged boot argument is set, mirroring the existing host-init
   allowlist tests `[C]`.
2. A completion test that fails when the transfer-complete condition is
   absent even if the expected byte count was drained `[C]`.
3. An error-path test that requires a confirmed stop or a documented safe-stop
   decision for every multi-block failure `[C]`.
4. Manifest tests for bad magic, digest, window size, offset, duplicate names
   and overlapping windows `[C]`.
5. Write rejection, write-protection, media-state and maximum-block tests `[C]`.
6. Concurrent-read serialization tests across published windows `[C]`.
7. A synthetic trace/oracle test for the future gta4l register sequence, kept
   separate from the existing SDHCI host-init oracle `[C]`.

No test in this plan reads the tablet, issues a card command, enables bus power
or enables the card clock `[S]`.

## 8. Credit

The reference is the work of Dracen (`Dracen-W`):
`https://github.com/Dracen-W/NativeEmmc` at
`4980f1070b0c822dedb9eb350df710a57df28590`. The catalog entry is
`https://unixnu.com/drivers/community-native-emmc`. The source declares
BSD-2-Clause-Patent; the repository `LICENSE` text is BSD-2-Clause, and that
discrepancy is recorded above `[S]`.
