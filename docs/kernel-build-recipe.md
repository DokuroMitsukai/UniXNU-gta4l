# gta4l XNU kernel build recipe and stage replay

Status: host-only stage replay taken on 2026-10-30. No device, flash, boot,
power or storage operation was performed. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

This document pins the inputs and stages of the XNU kernel build that produced
the historical build tree's prelinked kernel. It replays the source/patch,
compile, link and prelink stages. The linked kernel is reproduced byte-for-byte
when the historical file and archive-member timestamps and
`RC_ProjectSourceVersion` are restored (§6); a clean build without that
restoration is not byte-identical.

Tags: `[S]` source- or artifact-established, `[C]` conditional inference,
`[U]` not established.

The historical output tree is
`OUT=src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l`; the UniXNU checkout is
`src/UniXNU`, and the device repository is `src/UniXNU-gta4l`.

## 1. Result

| Stage | Replay | Result |
| --- | --- | --- |
| Source and patches | fresh archive of the pinned commit, 59 patches and the four Linux host rewrites | 70/70 tracked modified files match the historical worktree `[S]` |
| Probe archive | pre-`bf2e7d4` sources with the same compile record | 3/3 member objects match; the container differs by recorded timestamps `[S]` |
| Compile | clean rebuild from an empty `ios-obj` and empty external archive directories with the pinned source, patches, Linux rewrites, historical build date and board personality | 1,011/1,011 objects (888 `.o`, 123 `.cpo`) and 1,086/1,086 archive members and symbol tables match the historical tree `[S]` |
| Link | the captured final link command after restoring the 951 object mtimes and 11 in-tree archive-member dates recorded in `N_OSO`, with `RC_ProjectSourceVersion=10063.141.1` | linked kernel `3103e067...` byte-identical, `cmp` 0 bytes `[S]` |
| Prelink | reproduced linked kernel and pinned kernelcache | `kernel.macho` `452026b2...` and `kernel.json` `9f70ad65...` byte-identical `[S]` |

The replay covers the full chain: source/patch → compile → link → prelink. A
clean rebuild from an empty object tree and empty external archive directories
reproduced every object and archive member byte-for-byte, so the result does not
depend on stale build outputs. The linked kernel `3103e067...` is both
reproduced (§6) and used as the prelink input; the prelink outputs are
regenerated from it. The byte-identical link depends only on the mtimes that
`ld64` records in `N_OSO`: the 951 recorded object paths and 11 in-tree archive
members. Without that pin the object bytes still match but the `N_OSO` entries
and the content-derived `LC_UUID` differ `[S]`.

## 2. Pinned inputs

| Item | Value |
| --- | --- |
| Host | AlmaLinux 9.8, x86_64, Linux 5.14; Python 3.11.13 |
| Device repository | `src/UniXNU-gta4l`, branch `bringup/sdhc-evidence`, HEAD `3c3d586dd1feb1d63ade4452800a4ccc7cbcee31` at the replay, with the replay edits uncommitted; the replay used the historical HostInit source as an uncommitted working-tree revert (§5), while the active tree now carries the committed `bf2e7d4` source |
| UniXNU | `src/UniXNU`, HEAD `6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f`; the tree's copies of the kernel scripts match this checkout |
| XNU source | `src/UniXNU/out/feeds/xnu-b7545ced`, commit `d8b80295118ef25ac3a784134bcf95cd8e88109f`, clean |
| Patch list | `OUT/xnu-patches.list`, 59 patches, SHA-256 `12b5e9e20ffd7462edebe46f6c7ea73d4a758a177519dac1e5c863d0fc94996d` |
| Patch records | `OUT/xnu-inputs.json`, SHA-256 `df050293e57008c3a166c9dfa4e12d236d80430de322fce1a5f4735a1d3c8bf5`; 59/59 hashes match |
| Configuration | `OUT/.config`, SHA-256 `c12e37c968020f71c72af350cba662c8a2d0b4b0795a726d1d3b55928881fc6a` |
| Defconfig | `src/UniXNU-gta4l/configs/gta4l_defconfig`, SHA-256 `ef3fb50875897b5a02a18d58faa4b1b07ee3b3707c9810543f5781a747d1195a` |
| OS profile | `src/UniXNU/profiles/os/ipados-17.7.11-21H461/profile.toml`, SHA-256 `93661d78eda49c4076f1ec2ced391e6cc418ff72f30dcf17f5f30d92b6141ac1` |
| Board inputs | `board.toml` SHA-256 `505bef50b59b1cd9324ea88244891d91db9b6f157c19702a1a29e8b511fb31ed`; `feeds.toml` SHA-256 `3be886c449404f678c0e52034bf5d0b3232f07227be7e15af082c2e377528aca`; `unixnu.lock` SHA-256 `fc1bf57c489ba2809d471fde20a550424e35789a78a95977e917e84255e89550` |
| Compiler | `/home/sara/slopcode/toolchain/llvm/bin/clang-24`; clang 24.0.0git, llvm-project `58c46bae21181fb4240cf964461bea738d96d50e`, assertions; target `arm64-apple-ios17.0` |
| Linker | cctools-port `1030.6.3-ld64-956.6`; `ld64-956.6`; `CommandLineTools/usr/bin/ld` |
| Mach-O tools | cctools `1030.6.3`; `ar`, `libtool`, `nm`, `lipo`, `otool` in `CommandLineTools/usr/bin` |
| Host tools | `xnu-host-cc` uses host clang 21.1.8; `migcom` in `CommandLineTools/usr/libexec`; `xcrun` shim `src/UniXNU/scripts/linux/xcrun`, SHA-256 `8da8a494abd47a6d484c217ac074c1759792732f048ffce3f65c114c5d641576` |
| Build script | `OUT/tree/scripts/build-xnu-4k-baseline.sh`, SHA-256 `34b202e27b45199711809aa8dd7ad73e5b8feb5c2fa7e2a6bd82677ea69c7e7f` |
| Timestamp pinner | `src/UniXNU-gta4l/scripts/pin-gta4l-xnu-mtimes.py`, SHA-256 `149120c8ff0c7365c0b9e6661923561e76abf5e8d6616c9f0a535b88d9de7946` |
| Pinner tests | `src/UniXNU-gta4l/scripts/test-gta4l-xnu-mtimes.py`, SHA-256 `1581a1703f1b0b2f1f587ee3825a63a246f623c72b6f6fd0688b7ce63ae2632c` |
| Linux rewrites | `OUT/tree/scripts/linux/prepare-xnu.py`, SHA-256 `914958fd7f92ce17b0ebe15aaff3194dd267ee7482f9d45dcf7e876c643dc780` |
| Toolchain setup | `src/UniXNU/scripts/linux/setup-toolchain.py`, SHA-256 `9334e7994a64a0a5f59b40b8f36e08fd9eac08684c7ae8bce8d4afedd279c963` |
| SDHCI archive builder | `OUT/tree/scripts/build-gta4l-sdhci-snapshot.py`, SHA-256 `b69ebf6223341c751c012a4bac843c2794af92f8801cb43009fc34344e59ae00` |
| Prelink tool | `OUT/tree/kits/XnuPrelink/emit_prelink.py`, SHA-256 `4cbd97a7ca231cb56d8d7c6a02d09b86af33b5af3d05dba41edd00e51cd3a36a` |
| Compile record | `OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/bsd_init.o.json`, SHA-256 `800047ce9150b39e1b8a0ea82c6894fa335a90a5d5304bd73b6709d4e6adc2bb`, 417 arguments |
| Link input list | `OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/link.filelist`, 1006 entries |
| Replay evidence | `OUT/kernel-replay-evidence/`: `reference/oso-manifest.tsv`, `reference/historical-objects.sha256`, `reference/historical-archive-members.sha256`, `pinner/` logs, `link-command.sh`, `make-args.txt`, `clean-rebuild.log` and `SHA256SUMS` |
| Original kernelcache | `src/UniXNU/out/ipsw/21H461/kernelcache.decompressed`, SHA-256 `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7` |
| Prelink parameters | slide `-0x1000000`; early security init `com.apple.driver.AppleMobileFileIntegrity=0xfffffff005ab4ae0`; 31 roots from the OS profile |
| Linked kernel | `OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf`, SHA-256 `3103e067575d94d878485de464ee0173b07ab2f02215d3abae6fb962c1c59064` |
| Prelinked kernel | `OUT/kernel/kernel.macho`, SHA-256 `452026b2d8bbfaaa5b6ebc97ce235f396a8929b50d3957eff4c655eddd30ca86`; `OUT/kernel/kernel.json`, SHA-256 `9f70ad6594f539f447029d5714c0cd065b14b3c5e84b54dfd7c8d5b5c22572e8` |
| Downstream records | At the replay, `OUT/payload/payload.json` recorded kernel `452026b2...`, payload kernel `ed19095452f1e2aa1b56edd1746cbbf25063dd01e6d34d97bc2982faa5718a89` and payload `f1054a26b1222e90c8d6efce73acbd9aeb653509271c34c33a26bb3838f8d6db`; `OUT/boot/boot.json` records image `08d2af9f2f24f38706da66785556b0c764f9a9d75400d8734b6a924dd9c8a05f` and kernel section `1686d6974146b1e13c6809a445377854673ca3c974f5a07d0b2568eacd70d73d`. The mutable `OUT/payload/` output has since been replaced by reproduced-kernel diagnostic payloads: audit `0eadb4d4...` (workspace `out/gta4l/ram-root-sdhci-repro-kernel-evidence/`) and host-init `a256b6d7...` (workspace `out/gta4l/ram-root-sdhci-repro-kernel-host-init-evidence/`); it currently holds the host-init payload |

The compile record's first argument is the clang-24 path and its arguments
include `--target=arm64-apple-ios17.0`, `-mkernel`, `-O2` and
`-mios-version-min=17.0` `[S]`.

## 3. Build sequence

The equivalent command sequence, from `src/UniXNU`, is:

```sh
python3.11 ./unixnu defconfig gta4l_defconfig -D ../UniXNU-gta4l
python3.11 ./unixnu fetch
python3.11 ./unixnu tree
python3.11 ./unixnu build kernel -D ../UniXNU-gta4l -j "$(nproc)"
python3.11 ./unixnu build prelink -D ../UniXNU-gta4l
```

`./unixnu` needs Python 3.11 for `tomllib`. The historical tree records the
results of this sequence; the commands are the equivalent recipe, not a
transcript of the historical invocation. `build kernel` runs three stages `[S]`:

1. If `startup.o.json` is absent, a bootstrap pass compiles the kernel with
   `XNU_BOOTSTRAP=1` and links without the external archives, so the compile
   records and `link.filelist` exist.
2. The storage-header preparation runs, then `build_archives` runs the ordered
   `board.toml [kernel] archives` plan, including
   `build-gta4l-sdhci-snapshot.py`.
3. The final `make` links the kernel with the external archives, including
   `CONFIG_XNU_EXTRA_LD_ARCHIVES="-Wl,-force_load,{tree}/build/gta4l-sdhci-snapshot/libGta4lSdhciSnapshot.a"`.

`build prelink` reads the OS profile's `[kernel.prelink]` and runs
`emit_prelink.py` against the linked kernel and the pinned kernelcache `[S]`.

The SDHCI archive builder writes a no-op `FirstPassStub` when
`bsd_init.o.json` is absent (`scripts/build-gta4l-sdhci-snapshot.py:48-72`).
The bootstrap kernel therefore contains no-op probes; only the final build
links the real probes. The historical linked kernel contains the three real
symbols `_gta4l_sdhci_snapshot_run`, `_gta4l_sdhci_handoff_audit_run` and
`_gta4l_sdhci_host_init_run`, so it is a post-bootstrap kernel `[S]`.

## 4. Source and patch replay

The 59 patches were applied to a fresh `git archive` of
`d8b80295118ef25ac3a784134bcf95cd8e88109f` in a temporary directory:

```sh
OUT=src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l
TMP=$(mktemp -d /tmp/opencode/xnu-patch-replay.XXXXXX)
git -C src/UniXNU/out/feeds/xnu-b7545ced archive \
  d8b80295118ef25ac3a784134bcf95cd8e88109f | tar -x -C "$TMP"
cd "$TMP"
while IFS= read -r patch; do
  git apply --whitespace=nowarn "$patch"
done < "$OUT/xnu-patches.list"
```

All 59 patches applied without fuzz or rejection `[S]`.

The historical `.cache/xnu-work` has four further Linux host rewrites that are
not in the patch list. They are applied by
`scripts/linux/prepare-xnu.py source`:

| File | Rewrite |
| --- | --- |
| `bsd/kern/makesyscalls.sh` | drop the `\(sy_call_t *\)` escapes that mawk keeps |
| `osfmk/arm64/corecrypto/arm64_isa_compatibility.h` | use the standard vector syntax for non-Apple clang |
| `SETUP/config/doconf` | replace the `#!/bin/csh -f` shebang with the available `csh`/`tcsh` |
| `SETUP/kextsymboltool/kextsymboltool.c` | treat `lseek()` on `/dev/null` as success |

After those four rewrites, the 70 files reported by
`git -C "$OUT/tree/.cache/xnu-work" diff --name-only` match the temporary tree
byte-for-byte `[S]`. Before the rewrites, 66/70 match; the four differences are
exactly the table above `[S]`.

This replay covers the tracked source modifications. The historical build tree
also contains untracked files copied or fetched from the pinned reference
sources: inside `.cache/xnu-work`, `EXTERNAL_HEADERS/TrustCache/`,
`bsd/kern/source_pthread_bootstrap.c`, `osfmk/kern/boot_progress.h` and
`iokit/DriverKit/*`; alongside it, the `refer/libplatform` and
`refer/libdispatch` trees. Those copies were not replayed here `[U]`.

## 5. Probe archive replay

`build-gta4l-sdhci-snapshot.py` compiles the three probes with the
`bsd_init.o` compile record. The historical tree's build report records `[S]`:

| Source | Source SHA-256 | Object SHA-256 |
| --- | --- | --- |
| `Gta4lSdhciSnapshot.c` | `b2fba80eb08a6e6cb543fd5ff7cebd9cbde1ad1d33922631537050368b08d8b3` | `8ad8df74c2952e26b9db3fc208a75a668c7d30657ed5e280bef7d839d6910dba` |
| `Gta4lSdhciHandoffAudit.c` | `cbbb087d1e1b36f4869066a9b98681f3d71503234cdc2ab50d0d5391c2b659e3` | `d38ed5e4a3767e166d721514a6f41d88c2ea20417dd085b01f7c35b2f7eda1f9` |
| `Gta4lSdhciHostInit.c` | `bc228be74eedcda5289cc14b99e62471bbef92671b329f6120584f82e046c60c` | `b2ac1e5d1787c81f9a261b71ca389b8a0206a1576cc5045ec3b0f27fee71f78d` |

The archive is `8db3f178725bfdede748b7dab210883905de981af94a2b097441ea765a3989b8`,
has three members and was built on 2026-10-29 01:29 `[S]`. The linked kernel
contains the three probe symbols, so this is the archive the historical kernel
used `[S]`.

At the replay the working tree carried the historical source
`bc228be74eedcda5289cc14b99e62471bbef92671b329f6120584f82e046c60c` (an
uncommitted revert of `bf2e7d4`). The committed source at `bf2e7d4` is
`3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb`, changed on
2026-10-30 00:30, after the historical build. Rebuilding with the `bf2e7d4`
source changes only `Gta4lSdhciHostInit.o` to
`f51dd942d892f3d38d26c92fcad5787fcc286cc794721d22e8c872c721f0b682` and changes the
archive container `[S]`.

Rebuilding with the pre-`bf2e7d4` source (`bc228be7...`) reproduces all three
member objects exactly `[S]`. The container still differs because cctools `ar`
records wall-clock time in every header: the `__.SYMDEF SORTED` header records
the archive creation time (`1793217563`) and the three member headers record
their objects' mtimes (`1793217557`, `1793217557`, `1793217558`) `[S]`.
Touching the objects to those mtimes and rebuilding in the historical member
order still leaves the symbol-table header at the replay time `[S]`.
`ZERO_AR_DATE=1` zeroes all four timestamps and is deterministic across runs
(`935fa9a52a1b9cfa60bc16b5817119aadb2fe8b52e22d40a1f444bc1a0f61791`), but it
produces a different container than the historical one `[S]`. The member
objects are the reproducible unit; the container is not.

The audit-evidence archive `ef6646ce0a2deb55d7e63bb403638e00734a13154b1d478cfd695cd447b8f56a`
is a separate two-member archive. Its build report lists only `Snapshot` and
`HandoffAudit`, names a compile record under `/run/media/...` (a different
checkout), and its member objects match that report
(`493009d3...` and `93085df5...`) `[S]`. The audit prelinked kernel contains
only `_gta4l_sdhci_snapshot_run` and `_gta4l_sdhci_handoff_audit_run`, so the
two-member archive is the one that audit kernel used `[S]`. Its two-member set
is consistent with a builder version before the HostInit probe was added; the
build report does not record the script hash `[C]`.

The archive difference is therefore accounted for by the post-build HostInit
edit, the archive container timestamps, and a different checkout and probe
set. It is not an unexplained source drift `[S]`.

## 6. Compile and link replay

The compile was replayed with the historical build date and the board's
personality from `OUT/.config`, not the script defaults:

```sh
cd src/UniXNU
export PATH=/tmp/opencode/py311/bin:$PATH
export UNIXNU_TOOLCHAIN=/home/sara/slopcode/toolchain
export KERNEL_BUILD_DATE='Thu Oct 29 01:23:04 IST 2026'
python3.11 ./unixnu build kernel -D ../UniXNU-gta4l -j 8
```

The personality is `XNU_CRYPTO_BOOTSTRAP=prelinked`, `XNU_NATIVE_AES=1`,
`XNU_SOURCE_CORECRYPTO14=1`, `XNU_SOURCE_PTHREAD=1`, `XNU_SOURCE_MOCKFS=1`,
`XNU_SHENG_PMGR=1`, `XNU_NATIVE_PAN=0`, `XNU_DT_PANIC_REGION=1`,
`XNU_NATIVE_CS_HASH_CACHE=1`, `XNU_ARM_DT_PLATFORM=1`, `XNU_ARMCPU=1`,
`XNU_NATIVE_ARCH_TIMER=1` and the SDHCI snapshot archive. The first pass is
the bootstrap pass described in §3; its link fails on the external archive
symbols by design. The second pass compiles and links the final kernel `[S]`.

The clean rebuild reproduced every generated object and every archive member:
1,011/1,011 objects (888 `.o`, 123 `.cpo`) and 1,086/1,086 archive members and
symbol tables are byte-identical, and the `.CFLAGS` and `.LDFLAGS` records are
byte-identical `[S]`. The historical conditional objects (`startup.o`,
`prng_random.o`, `bsd_init.o`, `vfs_conf.o`, `ubc_subr.o`, `pe_init.o`,
`start.o`, `KernelConfigTables.cpo`) are regenerated by this personality; a
default-personality pass deletes them and cannot link `[S]`. The 32 non-object
differences in the clean tree are explained by the random `/tmp` scratch names
in the compile records and the unpinned mtimes in the link artifacts; none
affect the pinned final kernel `[S]`.

Matching object bytes are not sufficient for a matching kernel. `ld64` writes
an `N_OSO` symbol for every object and archive member, and that entry records
the input file's mtime. The reference kernel records 963 `N_OSO` entries: 951
object paths under `ios-obj` and 12 archive members, 11 of them in the ten
external `.a` archives under `tree/build` and one in the prebuilt fakeroot
`libfirehose_kernel.a` outside the tree. Only those 951 object mtimes and 11
in-tree archive-member header dates need to be restored; the mtimes of other
files in `ios-obj` do not affect the link `[S]`.

The host-only pinner reads the reference kernel and either verifies or restores
those values:

```sh
cd src/UniXNU-gta4l
python3.11 -B scripts/pin-gta4l-xnu-mtimes.py \
  --reference-kernel /tmp/opencode/kernel-replay-20261030/original-xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf \
  --object-root "$OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837" \
  --archive-root "$OUT/tree/build" --apply
```

`--verify` is the default and exits 1 on a mismatch, 2 on a missing or
ambiguous input; `--apply` validates the whole plan before changing anything
and is idempotent. `scripts/test-gta4l-xnu-mtimes.py` covers the verify, apply,
idempotence, relocation, missing-input, path-escape and malformed-input cases
`[S]`.

Finally, `ld64` takes `LC_SOURCE_VERSION` from the environment variable
`RC_ProjectSourceVersion`. `make` exports command-line variables to recipes,
so the real build passes it implicitly; a direct link replay must set
`RC_ProjectSourceVersion=10063.141.1` (and `RC_DARWIN_KERNEL_VERSION=23.6.0`)
explicitly. Without it the load command is zero and the content-derived
`LC_UUID` changes. The two variables were restored together; the necessity of
`RC_DARWIN_KERNEL_VERSION` alone was not isolated `[U]`.

The captured final link command is the `make -n` line for the
`mach.development.bcm2837.unstripped.noctf` target (line 3615 of the dry run);
it is preserved as `OUT/kernel-replay-evidence/link-command.sh`. Running it
from `OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837` with the pinned
mtimes, the environment of `scripts/unixnu/kernel.py:linux_env`, and the two
`RC_Project*` variables produced `3103e067...`; `cmp` reports zero differing
bytes against the historical kernel `[S]`.

The clean-tree mismatch sequence is now exact. Without mtime restoration the
clean kernel was `c3958d00...` and differed from the historical kernel in
2,886 bytes, all in `__LINKEDIT` (`N_OSO` timestamps). Applying the 951 object
mtimes and 11 archive-member dates and relinking with the `RC_Project*`
variables produced the byte-identical kernel. An end-to-end pinner test
perturbed all 962 recorded values, applied the tool, and relinked to
`3103e067...` with `cmp` zero `[S]`.

The archive containers are therefore not reproducible from wall-clock builds
without the historical member dates. `ZERO_AR_DATE=1` makes a deterministic
container but records zero dates in `N_OSO`, which changes the linked kernel
`[S]`.

## 7. Prelink replay

The prelink replay used the reproduced linked kernel `3103e067...`, the pinned
kernelcache `f18b4df8...` and the profile's slide, roots and early security
init:

```sh
OUT=src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l
REPLAY=$(mktemp -d /tmp/opencode/prelink-replay.XXXXXX)
python3.11 "$OUT/tree/kits/XnuPrelink/emit_prelink.py" \
  --kernel "$OUT/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf" \
  --prelinked src/UniXNU/out/ipsw/21H461/kernelcache.decompressed \
  --slide=-0x1000000 \
  --early-security-init com.apple.driver.AppleMobileFileIntegrity=0xfffffff005ab4ae0 \
  --output "$REPLAY/kernel.macho" \
  --root <one of the 31 roots from the OS profile>  # repeat for all 31 roots
```

The replay output is `452026b2d8bbfaaa5b6ebc97ce235f396a8929b50d3957eff4c655eddd30ca86`,
and `cmp` reports it byte-identical to `OUT/kernel/kernel.macho` `[S]`. The
generated `kernel.json` is also byte-identical to `OUT/kernel/kernel.json`
`[S]`.

`kernel.json` records `native_sha256` `3103e067...`, `candidate_sha256`
`452026b2...`, `candidate_bytes` 51,060,736, `slide` -16,777,216 and
`original_sha256` `f18b4df8...` `[S]`.

This establishes the prelink stage on the reproduced linked kernel. The
prelink output is byte-identical whether the input kernel is the historical
file or the replayed file, because the two kernels are byte-identical `[S]`.

## 8. What is not established

- Byte reproduction of the historical archive container `8db3f178...` from a
  wall-clock build; its member objects are reproduced and its member dates are
  required inputs to the byte-identical link `[S]`.
- Reproduction of the untracked copied reference files in `.cache/xnu-work`
  and `refer/` `[U]`.
- Reproduction of the evidence-image kernels: v5 `10a2b5b9...`, audit
  prelinked `17295157...` and host-init `ba52b650...`. These are different
  builds or payload variants and were not replayed here `[U]`.
- Reproduction of the historical tree's 192 MiB payload `f1054a26...` or boot
  image `08d2af9f...` `[U]`. The payload kernel `ed190954...` is reproduced:
  patching the reproduced prelink `452026b2...` with the same panic-screen
  patch yields it byte-for-byte, and the diagnostic test packet embeds it
  (workspace `out/gta4l/ram-root-sdhci-repro-kernel-evidence/`). The packaging
  step was replayed for the audit, host-init and reproduced-kernel diagnostic
  images (`diagnostic-payload-input-inventory.md` §6).

## 9. Next host-only step

The remaining boot-image reproduction work is the firmware step, then the
payload and boot-image step, in
`diagnostic-payload-input-inventory.md` §9. The SDHC decision remains NO-GO;
no device operation follows from this document.

## 10. References

- `docs/diagnostic-payload-input-inventory.md` — payload, firmware and
  boot-image inputs and the packaging replay
- `docs/evidence-baseline.md` — tracked-file hashes and limits
- `docs/build-revisions.md` — earlier build pins and host checks
- `docs/firmware-handoff.md` — tested firmware provenance
- `docs/ram-root-fixture-reproducibility.md` — fixture replay, a different
  compiler pin
- `src/UniXNU/scripts/unixnu/kernel.py` — kernel, archive and prelink stages
- `src/UniXNU/scripts/build-xnu-4k-baseline.sh` — XNU make invocation
- `src/UniXNU/scripts/linux/prepare-xnu.py` — Linux host rewrites
- `src/UniXNU-gta4l/scripts/pin-gta4l-xnu-mtimes.py` — `N_OSO` timestamp verifier and pinner
- `OUT/kernel-replay-evidence/` — preserved replay logs, manifests and link command
