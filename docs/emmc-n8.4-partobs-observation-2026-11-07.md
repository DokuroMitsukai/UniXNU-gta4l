# gta4l N8.4 partition-observation first boot (2026-11-07)

Status: one owner-controlled first cold boot of the N8.3 candidate was
performed on 2026-11-07 and returned to TWRP. The RAM-root fixture reached its
intentional success panic; the eMMC `PARTOBS` report was not captured, so the
N8.4 observation is **inconclusive**. The boot budget is consumed. No retry,
reflash, second boot, commit or push was performed. This is not an eMMC,
partition-completeness or reliability result.

The candidate identity and flash record are in
`out/gta4l/ram-root-emmc-n8-candidate-evidence/` and
`out/gta4l/ram-root-emmc-n8-hardware-evidence/`: prelinked kernel
`4dcbf24d…`, linked archive `f9f83526…`, packaged kernel `023c8ff8…`, payload
`5f6759f2…` (11/11 contract), BOOT image `3d7e71af…` (18/18 gate). The
candidate was written to p69 only after the pre-flash p69 `b19c59b8…` was
backed up and verified as `boot-before-n8.img.xz` (`8b37f41c…`). The full p69
readback `7c22e499…` matches the candidate prefix `3d7e71af…` plus the
unchanged tail `a28b4ea2…`; RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged.

## 1. Boot transcript

The owner-supplied panic screen, reconstructed across OCR line wraps:

```
userspace panic: RAMROOT-FST PASS: pid=1 page=4096 fs=read+seek+eof timer=poll
frames ffffffff0079f0da8 ffffffff0079fdb40 ffffffff00773103c ffffffff077595e8
       ffffffff0078827ec fffffff007415f88 fffffff0074155d8 fffffff0074155a0
pages=0x00000000000079e89
free=0x00000000000078211
```

The raw transcription, including its line wrapping, is preserved in
`out/gta4l/ram-root-emmc-n8-hardware-evidence/observation-1/`. The frame
values are recorded as supplied and are not decoded.

## 2. Result

Established: the userspace RAM-root fixture passed (`pid=1 page=4096
fs=read+seek+eof timer=poll`) and the tablet returned to TWRP.

Not established: no `GTA4L-EMMC P5 REPORT`, `GTA4L-EMMC P5 PARTOBS`, controlled
two-/three-block read or `PART1..3` line was captured. No partition count,
stability, timeout or error field exists for this boot. The transcript cannot
distinguish a report thread that had not started, one still waiting for media,
and one that lost the terminal race to the fixture. No eMMC, read-path or
completeness claim follows.

The fixture deliberately panics immediately after its checks and a 50 ms poll.
The eMMC report thread independently waits for media (up to 20,000 ms), reads
LBA 1, performs the controlled reads and then observes the partition count for
up to 4,000 ms. The two terminal paths are independent, so the fixture can win
before the eMMC report exists. That is a test-harness race, not an eMMC
failure.

## 3. Corrective host batch: `fs-timer-hold`

An additive `fs-timer-hold` diagnostic was implemented in the shared UniXNU
fixture builder and assembly source. It performs the same checks, records
stage 7, sets `hold_after_success=true` and blocks in a one-second
`poll(NULL, 0, 1000)` loop instead of panicking, so the kernel eMMC report
thread or its existing watchdog owns termination. Every failure path keeps its
immediate panic. The original `fs-timer` fixture is unchanged: the rebuilt
`user.o` is still `6984333d…`, and the recorded signed fixture
`b316e44f…`, trust cache `e146717a…`, root image `77d5eba0…` and
`fixture.json` `37cbdd0b…` still match byte-for-byte.

The hold variant builds and signs on the host: `user.o` `54d771ac…`, static
Mach-O `b679b571…`, signed Mach-O `e6546715…` (CDHash `71f32329…`), trust cache
`30a72f78…`, root image `327ac826…`, nine signed pages verified; `fixture.json`
records `diagnostic=fs-timer-hold` and `hold_after_success=true`. The object
disassembly shows the stage-7 store, the zero-argument 1000 ms poll and a
backward branch to that poll, while the failure block still issues
`reboot(RB_PANIC | RB_NOSYNC)`. The staged hold artifacts (root image, trust
cache, signed fixture, `fixture.json` and build record) are preserved in
`out/gta4l/ram-root-fixture-hold-evidence/` for the next candidate.

The gta4l diagnostic payload builder now accepts `--data-ramdisk-manifest` and
refuses any `emmc-block-1-partobs`/`emmc-block-3-partobs` payload unless that
manifest names the `fs-timer-hold` fixture (and any declared
`hold_after_success` agrees with the diagnostic). The payload manifest records
the fixture path, hash, diagnostic and hold state. The old early-panic fixture
is therefore rejected for the observation instead of being silently
repackaged. Host tests cover the original object hash, the hold loop, the
retained failure panic, the missing-manifest rejection, the early-panic
rejection and the hold/diagnostic mismatch.

## 4. Limits and next decision

The first cold boot is consumed. No eMMC report, controlled read or partition
observation was captured; initialization reliability remains `[U]`, and F2,
p5b/F5, N5 and N6 verdicts are unchanged. A stable count or `count=80` would
still not be a completeness claim without an independent validated
GPT-to-`IOMedia` comparison.

A new candidate built with the hold fixture requires a separate candidate-build
approval, followed by a separate BOOT-only flash approval and one new
owner-controlled boot. The current p69 can be restored from
`boot-before-n8.img.xz` (decompressed `b19c59b8…`) if needed; the older N5
image rollback remains in the N6 evidence directory.

Follow-up: N8.5 built payload `28eafd24…` and BOOT image `b4f4f1ea…` with the
hold fixture (11/11 contract, 18/18 gate), backed up the N8.3 p69
`7c22e499…` as `boot-before-n8.5.img.xz` (`e5b43228…`), flashed p69 only and
read back full p69 `9d51fe80…` (candidate prefix `b4f4f1ea…` + unchanged tail
`a28b4ea2…`). The host did not reboot; one owner-controlled boot remains. See
`docs/emmc-n8.5-hold-fixture-candidate-2026-11-07.md`.
