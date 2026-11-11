# gta4l N10.16 4-bit SDR width-mode candidate (2026-11-10)

Status: **superseded** by the 2026-11-11 gate-fix candidate
(`docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`). This record
describes the pre-fix C2 candidate, whose report gate was unbounded. It is
host-only candidate evidence, not a hardware result. No flash, boot, mount or
device access was performed while producing it. The ABI audit used the
established QEMU-emulated Linux/QEMU qualification path; it executed only the
synthetic firmware/kernel fixture, never the tablet or physical MMIO.

## 1. Scope and outcome

The driver, selector, read scope, recovery behavior and report contract are
unchanged from the N10.16 host batch. The candidate keeps the opt-in token
requirement, 400 kHz SDR, two-block CMD18 chunks, the production
64-block/32,768-byte/one-segment advertisement, the same-boot 1-bit and 4-bit
two-block BOOT reference reads, the verified revert and fail-closed
first-failure handling. No CMD19/CMD14, mount, user-data read or storage write
is added.

The candidate is built from an uncommitted working tree. It is not identified
by HEAD alone. The source identity is in
`out/gta4l/ram-root-emmc-n10.16-width4-candidate-evidence/source-sha256.txt`,
`final-worktree-sha256.txt` and `device-status-final.txt`; the source files
that produced the kernel are the `package/Gta4lEmmcBlock.{c,cpp,h}` hashes
recorded there.

## 2. Candidate identity

- prelinked kernel `4a7d254e4d73987fb810794c869d9f7dc94e1882f0db9e4565840cb2aaf760e2`
  (51,470,336 bytes; plan
  `015bf7bfc54423186b5c724ddae3d477f0fd90f5d630a20ad870afa0ea0e62a8`)
- kernel-build archive `5c33be107d72a3b7451403c651dfdcef6c4c109d97522f9b397581e47088dcfb`
  (1,499,440 bytes; container metadata differs from the requalified archive)
- requalified archive `e0e13c9368a7844fc04eaf2f668b8251dae396b73cbf440fed1a08580ca607b9`
  with byte-identical members:
  - `Gta4lEmmcBlock.o`
    `a52966cd9a539c7a60d3db9a269ac6ba2f1a90149dd0b8b084a511cdf2eb269f`
  - `Gta4lEmmcBlockCpp.o`
    `e3f3ed6d4cf07145ad4d37d39f1e290da9bfab61051d8712baddb831c0351af5`
  - `original-bindings.o`
    `3e53b66dd177598ce54175d82e4d35b3fe6bf0572dc7ff1c78c75a6c6a461bd7`
- candidate storage-ABI audit `out/gta4l/storage-abi-4a7d254e4d73/`:
  `gdb-stop.json` `d15f4af0c1c85ecce583ba5ac0439236d18b9c7337255bf09046a6629bc8fbbe`,
  `interface-audit.json` `85f2970823f671822b896b3db17556619c61dcec72a287061d3f06f9878382a9`,
  `provenance.json` `0af27fd1d82384b542c4d94068fcb8906a5896dcdd05b62dbc3c2e0424566d8f`,
  `run.json` `72a445f4ee3d25a3c07f02d9797d05774cb89f3500fc94bbd4752ae4a3db98e0`;
  190 method slots, 181 kernel bindings (167 inherited + 14 pure virtual),
  26 concrete overrides, 0 unresolved original imports, vtable
  `0xfffffff005cb44c0`, metaclass `0xfffffff006add1d0`, service metaclass
  `0xfffffff007b1f5b8`, `bsd_init` `0xfffffff0076efa3c`, live original sizes
  0x90/0x88, native 4K, `runtime_qualified=false`; QEMU 8.2.0 and firmware
  SHA-256 `f4cd2f7780667e5614b2390b5b9c40e60c81b685384af87496df3c2159e1cd5d`.
  The annotation object is never linked or executed.
- payload `fafca83a3e77ffd828cfc33341b446bb2b1294566a712596e3fc8cc14da70a00`
  (83,886,080 bytes; manifest
  `payload.img.manifest.json`; 55/55 contract; diagnostic
  `emmc-block-3-width4` mapping to exactly
  `gta4l-emmc-block=3 gta4l-emmc-width=4`; hold root `327ac826…`, fixture
  `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, owner trust cache `30a72f78…`; packaged
  kernel `f27822f4d2ec179a5307d28e72c49a899823289b15fb6b2ddbb0c8e27abd1d3b`)
- BOOT image
  `f18694b73083b680679238ec72f315f8b11d503248d80a320bd9b91660a983d7`
  (85,688,320 bytes; manifest `boot-ram-root-emmc-n10.16-width4.img.manifest.json`;
  component gate 18/18; `only_ramdisk_differs=true`; baseline kernel section
  `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` unchanged;
  header ID `3326d76079da83cf8201b844c49f329851720aef…`; first-1,024-byte
  SHA-256 `b29e23fda6d6a1dfc969c652a17f7ae64d55eef8706bdf6f3ccba4fc170beb66`,
  formatter prefix `b29e23fda6d6a1dfc969c652a17f7ae6`)

## 3. Build chain

Run from the device repository with the 3.11 interpreter shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The shared `UniXNU` checkout was at HEAD `588ff8c…` with
the two established fixture modifications. The device working tree was
uncommitted at HEAD `b103aba…`.

1. Kernel build bootstrapped with the previous candidate-matched audit
   `out/gta4l/storage-abi-d943c228`, then with
   `out/gta4l/storage-abi-922c9d569981`, and finally with
   `out/gta4l/storage-abi-4a7d254e4d73`:
   `kernel-build.log`, `kernel-build-2.log`, `kernel-build-3.log`; the final
   build produced `4a7d254e…`.
2. Candidate-specific ABI audit against `4a7d254e…`:
   `audit-4a7d254e4d73.log` and `out/gta4l/storage-abi-4a7d254e4d73`;
   `scripts/test-gta4l-storage-abi.py --artifacts` passes.
3. Archive requalification against the final audit from the shared tree:
   `archive-tree-4a7d254e4d73.log`, `archive-requalified-4a7d.a`,
   `archive-report-4a7d.json`; the three members match the archive linked
   into the kernel (`archive-member-compare.txt`). The archive-container
   hashes differ only because the kernel build and the direct requalification
   selected different `ar` implementations; the member hashes are identical.
4. Payload build: `payload-build.log` -> `fafca83a…`; the contract check is
   recorded in `payload.img.manifest.json` (55/55 checks, including exactly
   one width=4 token, block-3 requirement, hold-fixture requirement and all
   mode conflicts). Extracted `unixnu.toml` records
   `gta4l-emmc-block=3 gta4l-emmc-width=4`.
5. BOOT packaging: `package.log` ->
   `boot-ram-root-emmc-n10.16-width4.img` `f18694b7…`;
   `boot-image-gate.json` and the manifest component gate pass 18/18 with
   `only_ramdisk_differs=true`.

## 4. Digest binding and host check

The pinned BOOT window (device LBA 2,255,360, two blocks, offset 0 of the
BOOT partition) is rebound to the final packaged image, not to the payload or
an earlier candidate. Its first 1,024 bytes hash to
`b29e23fda6d6a1dfc969c652a17f7ae64d55eef8706bdf6f3ccba4fc170beb66`.
`gta4l_emmc_block_put_digest()` takes a byte count; the width formatter
passes `16u`, so it prints 16 bytes (32 hex characters). Both `digest` and
`predigest` must equal `b29e23fda6d6a1dfc969c652a17f7ae6`.

`scripts/check-gta4l-width4-evidence.py` recomputes that prefix and checks
both fields, the fixed case shape and `match=1`. Its self-test covers matching,
changed, missing, malformed, truncated, wrong-match and wrong-shape cases; the
final-image fixture also passes. A device report is not present, so this is
host binding only.

## 5. Host results

- block suite: 48 core scenarios/38 core mutation controls, 278 adapter
  scenarios with 104 adapter/support and 10 policy mutation controls
- native core: 189 scenarios/117 mutation controls
- probe: 196 scenarios/100 mutation controls, 13 width-bound
  materializations and one oversized-report control
- payload builder tests and packager self-test: pass, including the
  `emmc-block-3-width4` mapping, hold-fixture requirement, early-panic
  rejection and conflict/isolation cases
- storage-ABI positive/negative checks: pass
- inventory/board, packaging, fixture-recipe, evidence-count and TOML checks:
  pass
- `git diff --check`: clean

The final build logs, manifests, gate JSON, archive-member comparison,
digest files and host check are in the gitignored
`out/gta4l/ram-root-emmc-n10.16-width4-candidate-evidence/` directory.

## 6. Status

This is a **host-qualified candidate**, not a hardware qualification. F5
remains the measured 1-bit result and F3 the separate measured 4-bit result;
no 4-bit storage-publication hardware result exists. Initialization
reliability, repeatability and failure rates remain `[U]`.

Hardware remains **NO-GO**. The conditional 24,230 ms success and 21,030 ms
failed-switch timing figures against the 30,000 ms watchdog remain unproven
end to end; the pre-watchdog startup path and gate-acquisition/in-flight
request bound are unresolved. The 2026-11-11 pre-hardware review found that a
slow 64-block in-flight request can hold the controller gate long enough for
the watchdog to fire before the width report starts:
`docs/emmc-n10.16-prehardware-review-2026-11-11.md`. That review also
corrected the digest-field width to 16 bytes/32 hex characters. A bounded-gate
corrective batch and a rebuilt/rebound candidate are required before an
execution proposal. The owner must separately approve CMD6
`BUS_WIDTH` writes/host width changes, the exact read scope and a finite boot
budget before any device run. Candidate success alone does not authorize
flashing, booting, mounting, repair, retry or rollback.

Acceptance gates are tracked in
`docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md` §10.
