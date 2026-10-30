# gta4l SDHC evidence baseline

Status: host-only snapshot of the working tree used for the software-evidence
closure `[S]`. It is not a hardware result, an electrical measurement or
permission to activate bus power or the card clock; the decision remains
**NO-GO** (`sdhc-decision-memo.md`) `[S]`.

Hashes were taken at `2026-10-30T07:34:09Z`, after the final edits to the
files below. This document's own hash is not listed. Hashes describe the
working tree at the snapshot and change with later edits; re-run the commands
in §6 before reusing them.

## 1. Repository state

| Item | Value |
| --- | --- |
| Repository | `src/UniXNU-gta4l` (device repository) |
| Branch | `bringup/sdhc-evidence` |
| HEAD | `1ef3792e0a396b9d46d1ce4c9854c37ad9770a45` |
| Dirty entries at the hash snapshot | 4 modified (`README.md`, `docs/build-revisions.md`, `porting.toml` and this baseline) and 1 untracked (`docs/ram-root-fixture-reproducibility.md`) |
| Staging and commits | None at the snapshot; the snapshot lands as one documentation and tooling commit |

The current snapshot is the evidence branch after the RPM LDOA attribution,
NativeEmmc credit, attribution-reconciliation, hardening, registration-check
and exhaustive-code-site commits, with the Linux fixture-reproduction record
and the stale-status reconciliation in the working tree. The hash table below
describes that working tree, not the original evidence-closure tree. The
original provenance is retained separately because the path lists and dirty
inventory are historical observations.

### Historical provenance

The original evidence-closure snapshot was taken on
`bringup/tab-a7-qemu-fixture` at
`e2d0cfc08c0c0119f3c55d1bcb6f9b8323893961` with 27 dirty entries: 8 modified
and 19 untracked, including this document. The tree was already dirty. This
task edited only paths already in that set and
added this document and `docs/stock-firmware-analysis-audit.md`; it did not
discard, stage, commit or revert existing work.
No pre-task snapshot exists, so the exact line-level split between earlier work
and this task is not independently recorded. The task-touched paths are:

- `docs/evidence-baseline.md` (new)
- `docs/stock-firmware-analysis-audit.md` (new)
- `docs/firmware-handoff.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/pm6125-register-map.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/sdhci-host-init.md`
- `porting.toml`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-evidence-counts.py`
- `scripts/test-gta4l-sdhci-host-init-sim.py`

The later bounded RPM/XBL trace updated `power-retention-evidence.md`,
`rpm-power-interface.md`, `sdhc-decision-memo.md`, `sdhc-electrical-contract.md`,
`stock-firmware-analysis-audit.md`, `pm6125-register-map.md` and `porting.toml`.

### New paths in this task

- `docs/evidence-baseline.md`
- `docs/stock-firmware-analysis-audit.md`

The pre-existing untracked entries are earlier work in the same SDHC evidence
effort. They are listed so the baseline is reproducible and so unrelated work is
not mistaken for this task's output.

### Modified paths in the pre-existing tree

- `docs/emmc-reference.md`
- `docs/sdhci-handoff-audit.md`
- `docs/sdhci-host-init.md`
- `package/Gta4lSdhciHostInit.c`
- `porting.toml`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-host-init-sim.py`
- `scripts/test-gta4l-sdhci-host-init.py`

### Untracked paths in the pre-existing tree

- `docs/firmware-handoff.md`
- `docs/linux-regulator-lifecycle.md`
- `docs/pm6125-identity-evidence.md`
- `docs/pm6125-register-map.md`
- `docs/pm6125-usid-research.md`
- `docs/pmic-audit.md`
- `docs/power-path-decision.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/spmi-0x28-analysis.md`
- `scripts/extract-gta4l-firmware.py`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/test-extract-gta4l-firmware.py`
- `scripts/test-gta4l-sdhci-evidence-counts.py`

## 2. Current host-tested source hashes

These are the bytes the host checks exercise at the snapshot; they are not the
bytes of the previously booted image `[S]`.

| Artifact | Path | SHA-256 |
| --- | --- | --- |
| Production probe | `package/Gta4lSdhciHostInit.c` | `3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb` |
| Probe header | `package/Gta4lSdhciHostInit.h` | `a91f76e6c0405315e6df3bca79d067ae798f688dac7b80f665d70f569482f0d5` |
| Simulator harness | `scripts/sim-gta4l-sdhci-host-init/harness.c` | `dd8c92092a6c7a609a81f6f202f5aa4470fdf6375cebd50ec49a828041066cdc` |
| Static host test | `scripts/test-gta4l-sdhci-host-init.py` | `6dd2c1d8ca64527c70b8410797a0361341d6b593b127b8a71a44f624f9580f6f` |
| Simulator test | `scripts/test-gta4l-sdhci-host-init-sim.py` | `fe696332841d247b58d81d2a2c1cd32cf59474eccaa5de64c1ab2a059d3de9b9` |
| Access-policy oracle | `scripts/oracle-gta4l-sdhci-host-init.py` | `488e2d0150692569fe20303facd4b7be3e5afa79e680f6c3785cad35bc570990` |
| Count-consistency test | `scripts/test-gta4l-sdhci-evidence-counts.py` | `f219e3cd281040e3d0c5731f626275ab8a943d119c4bfd141d8660079fed7f59` |
| Host-init policy document | `docs/sdhci-host-init.md` | `a50d093c379c10604291bcb135399878a96e83b24141d58ec6171411f7b206af` |
| Decision memo | `docs/sdhc-decision-memo.md` | `f0dc6bb3e83bb41b55cd1eeae222065b71d988581a725001b2d48ae7b6309c1e` |
| Electrical contract | `docs/sdhc-electrical-contract.md` | `dbf24739068f1bb679110e5f7ad189614b713dee2cc3a6c00c4ee90c1082357d` |
| Retention evidence | `docs/power-retention-evidence.md` | `eb91725bd6a63acded9e63ca0673670f36b43a3253da15d8ec17cb67f0387e07` |
| RPM interface | `docs/rpm-power-interface.md` | `4f3fe2e8527a9c334de0478f4a250a49d443b6e552eb4845f75e0cfc44a35e49` |
| PM6125 register map | `docs/pm6125-register-map.md` | `501672b50a86319b7bd095a92b674c162ccc485963e510a708649aa985486c22` |
| RPM LDOA analyzer | `scripts/analyze-gta4l-rpm-ldoa.py` | `d967e640ca8f6f7eb0607d0a51aea9b541b21901ed4bf5a0a3e9be090f2fa4a8` |
| RPM LDOA analyzer self-test | `scripts/test-gta4l-rpm-ldoa-analysis.py` | `c670e2788044bd8b5054b19452dde8080ee9dc93d794ffe8abf3e2890e202afc` |
| Dependency trace | `docs/sdhc-dependency-trace.md` | `67c91e40e6e11e4d128b52ed9f4445a796900f12ca4663ddafa646d407a27984` |
| Firmware handoff | `docs/firmware-handoff.md` | `974855b4177194b2effc3166a42f5c8fc79926a19f5bce14c65c7598675c1ef1` |
| Supplied-analysis audit | `docs/stock-firmware-analysis-audit.md` | `735545c60d47456b28f7fa6c0c50607008dbd1207941386b3bd907c8bff63fe4` |
| Repository README | `README.md` | `b29a6e0729d0b101f17dcd9ce452656daeeeafad7bce7355158e290e7ac1d7f5` |
| eMMC reference | `docs/emmc-reference.md` | `55620c2e45af66a8d3453d949d939da568436cc4c0fa3697b42da17a014f242d` |
| NativeEmmc audit | `docs/native-emmc-reference-audit.md` | `c123577953c3c8625e2119967a112826448dd0a95c861023a9593bca0dc1a215` |
| Porting record | `porting.toml` | `9771164882e042b33875483655c386b85b418e954a17fc838ce507cb0448a128` |
| RAM-root fixture reproduction | `docs/ram-root-fixture-reproducibility.md` | `a1a9f71fb2948dc6ef992b94a2e5e580c8a290ced230577107faf1fcea2fc9ac` |

## 3. Pinned Linux source cache

| Item | Value |
| --- | --- |
| Repository | `https://github.com/limonabisi/linux-gta4l` |
| Branch and revision | `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` |
| Cache path | `/tmp/opencode/linux-verify/` |
| Cache shape | 23 flat files; **no `.git` directory** |
| Sorted `sha256sum *` manifest digest | `f3a52fe3f718fb8b56801e25d2303627d7abbaa98e93037f4fb07b985828cdaa` |

```text
98192e8a5bd656dd68fe624d03e4a48a6964b4066a3453268d32b05440e2ea90  board.dts
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  core.c
9207beba399bf366902fcebf6a57fbb890c9f0289c37a301a3513b4d9d232f97  gcc-sm6115.c
45a71ac0cfb9b9d07bcb15eb93133ba0aa847aea1428ef072f75ba6e971b7bf5  icc-core.c
7e125d9308913c32803b0d482c070fb68356a00d19e0e9a687ac75fd5208fec0  icc-rpm.c
76792d3702e438e44a44ca93d86827940baa764833391319a8f887829e21b83c  icc-rpm.h
56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304  mmc-core.c
3eee0021701861692f738d359e0773af8eb046633b33988edb03c0b90c488777  opp-core.c
5f181cb21628ed3043fbf5329875fc56b74564770b3755ad8799502811f8e95f  pm6125.dtsi
32e2f6b0988eba52a2f662fdc2d7132022fbe980f281b2a82babcf3d7f46e18a  pmdomain-core.c
2da9e5b25ed33eaff3ebd0c8e497c0fc045c019156e686a8d5675e21e6fc9c57  pmi632.dtsi
cba250364921bfff3e8cfe10372906631cf2cba70365b2d75eabb8822084b9bd  qcom-spmi-pmic.c
4148ae42b825fecf124e964e585f775597a77e99df83e274f7236d1640c32370  qcom_smd-regulator.c
09436b8793dc69bf0805f941512b796b0831507d0c7d9d566ce186b6031c0dbc  qcom_spmi-regulator.c
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  regulator-core.c
6c6a0b277d001297d11cddeeb87e9f4367387a622dcfc6ca42c000754e0a722a  regulator.c
4aba9e20b5e68105fb68a378011248ad016b439f064b22fb6e56281c38c459e3  rpmpd.c
3ba6c4baf1adeaa90043f46db08dfc9df2809209b4e29386e9148d26da827a9a  runtime.c
b668e01dfcb6efac44249c287c6b8583b0a3d584f497e82d33df223cee61f53e  sdhci-msm.c
186305e102bd32df419ff80669e6018d6b3a9e4310c3101193413970355b9857  sdhci.c
d4cff8991af9bde1820f872b75bcb8259b8a0e4c87cf976582b61674707d7efb  sm6115.c
5b91a04c3119f2c846b620e1dc9fd50ce6ed61f07cf1ef2674c640c23a432d9c  sm6115.dtsi
320e71b64915eb4e32be90b49fd65f12da2241e877bf4f68f0c0c1eb4ab3e28d  spmi-pmic-arb.c
```

The cache has no git metadata, so the revision is a documented fetch provenance,
not a property that can be independently verified from the cache alone. If the
cache is lost, re-fetch the listed files from the pinned commit and compare the
manifest digest `[S]/[U]`.

## 4. Firmware and boot artifacts

| Artifact | Path or identity | SHA-256 | Status and relationship |
| --- | --- | --- | --- |
| Booted corrected v2 image | `out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img` | `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` | Recorded booted-image hash; artifact absent from this checkout (`out/` is gitignored) `[S]` |
| Local firmware volume | `SILICIUM_UEFI.fd` | `3d5dbd6ff28093e38a16c02af813fe7d2624d8e9b83ff407045e467e86904f5a` | Present; gzip-wrapped `[S]` |
| Local decompressed source | inner extraction of the above | `7c20a5b9165100a2f07edc58d2b348e576b372e368af0ebd6ecdeceac50ba10e` | 3,145,840 bytes; 112-byte prefix plus outer FV `[S]` |
| Local outer FV | inner extraction of the above | `4a8ca2cddcd578391dfd846ae994dafc7590b68e1e7ebde8f86ceca0c904b676` | 3,145,728 bytes; differs from booted outer FV `42f900a3...` `[S]` |
| Local inner FV | inner extraction of the above | `2443e52a31a3208c70d10e5a6a733c285de48dc1b48358f626cc1557f7d5f6b5` | 6,991,872 bytes; 90 PE modules; differs from booted inner FV `3bb7847a...` `[S]` |
| Booted outer FV | recorded in `firmware-handoff.md` | `42f900a3...` | 3,145,728 bytes `[S]` |
| Booted inner FV | recorded in `firmware-handoff.md` | `3bb7847a...` | 8,339,456 bytes `[S]` |
| SdccDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SdccDxe/SdccDxe.efi` | `4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b` | Same bytes in the local volume and the booted image `[S]` |
| PmicDxeLa PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi` | `db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce` | Same bytes in the local volume and the booted image `[S]` |
| SPMIDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SPMIDxe/SPMIDxe.efi` | `50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254` | Same bytes in the local volume and the booted image `[S]` |
| UEFI platform config | `../Mu-Silicium/Binaries/gta4l/RawFiles/uefiplat.cfg` | `1b8c0b8035d8b858dc8c7a763ab3b874dd16f8221295cbab116879bcf08f9c3d` | Present `[S]` |
| RPM image | `../../device-dump/private/gta4l-dump/partitions/rpm.img` | `272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6` | Private device dump; static analysis only `[S]` |
| XBL image | `../../device-dump/private/gta4l-dump/partitions/xbl.img` | `a09c7b62e639a0565c30b7a3b61e2d68f2a07bfe9c3199d38f9af17bdf9e073e` | Private device dump; ELF64 AArch64 `ET_EXEC`, 17 program headers, no sections; 15 `LOAD` segments including `0x5fc00000`; policy not analyzed `[S]/[U]` |
| XBL config image | `../../device-dump/private/gta4l-dump/partitions/xbl_config.img` | `6dbaad63204b761e7d8fb4dba7c861d97b25dc5094aabc0a9fe45f19fb20d99e` | Private device dump; embeds `/pmic_settings.bin` at `0x3054`, size `0x7240`, SHA-256 `410e45ac...f4e97597`; static analysis only `[S]` |
| Supplied stock-firmware analysis | archive from https://github.com/NoNameBuilder; not vendored | `6d750114c4465e894a043ce190ea3a776d25a3bf862d5b50df3d95d73f2368e9` | 214/214 manifest-listed files match; supplied XBL bytes and DT overlays differ from the captured state; audit in `stock-firmware-analysis-audit.md` `[S]/[U]` |
| Live FDT blob | `../../device-dump/private/gta4l-dump/live-fdt.dtb` | `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc` | Captured vendor FDT `[S]` |
| Decompiled live DTS | `../../device-dump/private/gta4l-dump/live.dts` | `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Text used for DT citations; not the booted blob `[S]` |

The local `SILICIUM_UEFI.fd` is **not** the booted image. It packages the same
three driver PEs, but its outer and inner firmware volumes differ and its
`DxeCore` is `f37f7d2b...` rather than the booted `f4aefa94...`
(`firmware-handoff.md`). The booted artifact is absent from this checkout; its
hash is a recorded provenance value, not a reproduction. Do not present the
current tree or the local firmware volume as the previously booted image `[S]`.

Private firmware and device dumps are referenced by path and hash only; their
contents are not duplicated in this repository `[S]`.

## 5. Evidence categories

| Category | What it is | What it is not |
| --- | --- | --- |
| Current host-tested source | Probe, harness, simulator, oracle, counts and policy documents at the hashes above | Not a hardware, electrical or retention result `[S]` |
| Previously booted controller-local image | Recorded v2 image hash and its controller-local result | Not reproduced by this tree and not a power result `[S]` |
| Captured vendor DT | `live-fdt.dtb`/`live.dts` board description | Not proof of the XNU-time rail, pad or regulator state `[S]` |
| Static firmware analysis | RPM LDOA resource tables, XBL boot PMIC settings and Mu-Silicium exit traces | Not runtime behavior; the RPM consumer/policy, PBS semantics, indirect effects and untraced callbacks remain `[U]` |
| Supplied third-party analysis | stock-firmware archive from https://github.com/NoNameBuilder, manifest-checked and audited | Not authenticated, not the captured XBL or live FDT, and not a measurement `[S]/[U]` |
| Physical measurements | None | No voltage, current, transient, pad or retention measurement exists `[U]` |

## 6. Reproduction

Run from `src/UniXNU-gta4l/`:

```sh
git rev-parse HEAD
git status --porcelain
sha256sum package/Gta4lSdhciHostInit.c package/Gta4lSdhciHostInit.h \
  scripts/sim-gta4l-sdhci-host-init/harness.c \
  scripts/test-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-host-init-sim.py \
  scripts/oracle-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-evidence-counts.py \
  docs/sdhci-host-init.md docs/sdhc-decision-memo.md \
  docs/sdhc-electrical-contract.md docs/power-retention-evidence.md \
  docs/rpm-power-interface.md docs/pm6125-register-map.md \
  docs/sdhc-dependency-trace.md docs/firmware-handoff.md \
  docs/stock-firmware-analysis-audit.md README.md \
  docs/emmc-reference.md docs/native-emmc-reference-audit.md \
  docs/ram-root-fixture-reproducibility.md porting.toml \
  scripts/analyze-gta4l-rpm-ldoa.py scripts/test-gta4l-rpm-ldoa-analysis.py
( cd /tmp/opencode/linux-verify && sha256sum * | sort -k2 | sha256sum )
python3.11 -B scripts/test-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-host-init-sim.py
python3.11 -B scripts/oracle-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-evidence-counts.py
python3.11 -B scripts/test-gta4l-rpm-ldoa-analysis.py
python3.11 -B scripts/analyze-gta4l-rpm-ldoa.py \
  ../../device-dump/private/gta4l-dump/partitions/rpm.img --code
```

The analyzer command needs the private `rpm.img`; it refuses a different image
hash unless `--expect-sha256` is given explicitly. `--code` checks the
documented Thumb call sites, literals, destination registers, registration
setup and class registration. The self-test uses only a synthetic image `[S]`.

The `fs-timer` fixture reproduction is recorded in
`docs/ram-root-fixture-reproducibility.md`; it rebuilds the signed fixture,
trust cache and root image byte-for-byte with the pinned cctools-port and
`rcodesign` versions. The hash table above can be checked mechanically:

```sh
python3.11 - <<'PY'
import hashlib, pathlib, re
text = pathlib.Path('docs/evidence-baseline.md').read_text()
rows = re.findall(r'^\| [^|]+ \| `([^`]+)` \| `([0-9a-f]{64})` \|$', text, re.M)
bad = [(path, hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest(), want)
       for path, want in rows
       if hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest() != want]
print(f'rows={len(rows)} mismatches={len(bad)}')
for path, got, want in bad:
    print(f'{path}: {got} != {want}')
raise SystemExit(1 if bad else 0)
PY
```

The host checks are read-only and do not build, flash, boot, power or access a
device `[S]`.

## 7. Limits

- The hashes are a snapshot of a dirty tree; any later edit invalidates them.
- The pinned Linux cache has no `.git`; its revision is documented provenance,
  not a verifiable git checkout `[U]`.
- The booted image artifact is absent; the recorded hash is not a reproduction.
- The local firmware volume is a different build; only the three driver PEs are
  byte-identical to the booted drivers `[S]`.
- No physical measurement exists; the measurement-dependent parts of `E1`–`E9`
  remain `[U]`.
