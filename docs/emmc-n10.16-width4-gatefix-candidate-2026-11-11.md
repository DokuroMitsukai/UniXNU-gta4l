# gta4l N10.16 width-mode gate-fix candidate (2026-11-11)

Status: host-only corrective candidate, subsequently executed on hardware. It
supersedes the pre-fix C2 candidate and adds a bounded report-gate acquisition
with a terminal gate-failure path. The owner approved one controlled boot; the
run completed on 2026-11-11 with `complete=1` and the pre-flash BOOT image was
restored and verified. Result:
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`.
The ABI audit uses the established QEMU-emulated Linux/QEMU path; it executes
only the synthetic firmware/kernel fixture, never the tablet or physical MMIO.

## 1. Corrective change

The 2026-11-11 pre-hardware review found that `runWidth()` blocked on the
controller gate while a slow 64-block in-flight request could hold it past the
30,000 ms watchdog. This candidate adds:

- `GTA4L_EMMC_BLOCK_WIDTH_GATE_MS` 2,000 ms with a 10 ms try-lock step;
- a terminal gate failure reason (8) when acquisition times out;
- media offline, no 4-bit read and no revert claim on that path;
- `panicWithReportNoGate()`, which snapshots with a try-lock instead of
  blocking the terminal report on the held gate;
- host `width-gate-timeout`, `width-gate-release` and deadline-boundary
  scenarios: the first holds the gate and expects `reason=8`/`WIDTHFAIL
  stage=8`, no `WIDTHREAD`, media offline, no read/revert counters and the
  card left at 4-bit; the second releases the gate mid-wait and requires the
  full success path and verified revert; releasing exactly at the deadline
  still succeeds, while releasing 10 ms later is a terminal gate failure. A
  later watchdog pass after the timeout must return without a second panic;
- cancellation interleaving scenarios: a cancel requested while the bounded
  gate acquisition sleeps and a cancel requested immediately after the 4-bit
  read both continue through the verified revert and latch `reason=7` without
  a `complete=1` claim, and a watchdog pass that takes the terminal claim
  after the failure-latch point but before the width path commits its report
  still produces the terminal watchdog report and no second panic;
- updated bounds: 13,010 ms pre-watchdog + 13,220 ms report (2,000 gate +
  3,000 read + 8,220 revert) = 26,230 ms success; failed-switch bound remains
  21,030 ms.

The pre-watchdog start path is still a conditional design budget, not a
source-derived sum. The review's "unbounded report gate" finding is closed at
the source and host-test level; a hardware result is not claimed.

## 2. Candidate identity

- prelinked kernel `2be2eb0605b25758c57dcc3a80e631a76225dc975ffc4ac15aa8320e71cd7005`
  (51,470,336 bytes; plan
  `f96f6ada1966ee09c60e9bd90930fa96f93c19f3dca522a523dbcc519734b790`)
- candidate storage-ABI audit `out/gta4l/storage-abi-2be2eb0605b2/`:
  `gdb-stop.json` `3ae6d785a0620ed7cc4451f9a8b74f8e66b06dc34d5bf80c90fdac4ee65fa447`,
  `interface-audit.json` `aec2143d281b5a32b85d4d84250aa779e1688a6225f946160cabc6b48a9dffa6`,
  `provenance.json` `afc5d71f20d83ae04a32cc842fd9ad3ccab412238da23b28497808db32148ce2`,
  `run.json` `c3d53561eb64dd50d41d95552db0755028a039af6ee86ebbe7d9c5406448866e`;
  190 slots, 181 bindings, 26 overrides, 0 unresolved original imports,
  service metaclass `0xfffffff007b1f5b8`, `bsd_init` `0xfffffff0076efa3c`,
  QEMU 8.2.0, live original sizes 0x90/0x88, native 4K.
- linked kernel-build archive
  `6ea8e3bf2ee274c9daf9313f1ddbb13b5d86329ed5c0a58096dd1ae17d8b8ff3`;
  requalified archive
  `abcf935feab77258b283c910383647a58eab82fa969bffc48119c7e9031c00dc`;
  members match:
  `Gta4lEmmcBlock.o`
  `41b7e08cbbbd2994304a51ae191de51ae51c20a73e4cf594e03b46a33d424025`,
  `Gta4lEmmcBlockCpp.o`
  `c26063974fbfcd6f24353b19c186876556004802b6ca88a7f34b214dec50cbc7`,
  `original-bindings.o`
  `3e53b66dd177598ce54175d82e4d35b3fe6bf0572dc7ff1c78c75a6c6a461bd7`.
- payload `10ff7ee86d82c7d7838b4d37c8ed426f2b56b9c23b926a51e0f0102bfbade79f`
  (83,886,080 bytes; 55/55 contract; diagnostic `emmc-block-3-width4`;
  packaged kernel
  `e71fa4eb9cce4441d4fa516352ed888f9f0516f7393d6c11c6cad9c110448a01`)
- BOOT image
  `58352cd37f8643ba89c07c4911914fd50b4728c0333602aa4ca7840633c8aba7`
  (85,688,320 bytes; component gate 18/18; only the ramdisk differs;
  header ID `7b9d590755fb87780b983189a9058edf0fe3b0b7…`; first-1,024-byte
  SHA-256 `a6da82e356863d1ab7e31c21a8fd85fcdf0d395520d17d77427b425d6edecda3`,
  prefix `a6da82e356863d1ab7e31c21a8fd85fc`)

## 3. Build and evidence

The kernel was built with the prior candidate-matched audit
`out/gta4l/storage-abi-4a7d254e4d73` as the bootstrap, then audited as
`2be2eb0605b2`; the block archive was requalified against that audit and its
members match the archive linked into the kernel. The payload and BOOT image
were then built from that kernel. Logs and manifests are in
`out/gta4l/ram-root-emmc-n10.16-width4-gatefix-candidate-evidence/`.

Host checks: block 48 scenarios/38 core mutation controls and 285 adapter
scenarios with 104 adapter/support and 10 policy mutation controls; core
189/117; probe 196/100; payload builder, packager, storage-ABI, fixture,
inventory/board, packaging, evidence-count and TOML checks pass;
`git diff --check` is clean.

## 4. Status

This is a **host-qualified corrective candidate** that was measured on one
cold boot: the driver's terminal guard computed `complete=1`, the WIDTHREAD
digest matched the final image, and the verified revert returned the card to
1-bit. BOOT was restored afterwards. F5/F3 remain their earlier bounded
observations; initialization reliability remains `[U]`. The 26,230 ms success
bound still contains a conditional 13,010 ms pre-watchdog prefix; the report
gate is bounded at 2,000 ms and a gate timeout is a terminal non-pass. The
executed result does not authorize retries, reflash, mount, writes or extra
boots. The exact protocol and result are in
`docs/emmc-n10.16-width4-hardware-protocol-2026-11-11.md` and
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`.
