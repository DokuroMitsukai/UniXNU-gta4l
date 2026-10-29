# QEMU virt bring-up fixture

This fixture runs the gta4l XNU kernel, UniXNU and DarwinBoot on QEMU
`virt`. It is an offline checkpoint tool, not an SM-T505N emulator: QEMU does
not model the tablet panel, UFS, GPU, power controllers or Samsung firmware.
A result here says what generic early XNU code did on virtual hardware.
It does not prove the same result on the tablet.

The fixture exists because a timed hardware reset only says whether a
checkpoint was crossed. It cannot distinguish "the function returned" from
"the kernel never reached the function". QEMU can stop at the symbol and read
the return address.

## Layout

The runner stages this ESP:

| Path | Contents |
| --- | --- |
| `EFI/BOOT/BOOTAA64.EFI` | UniXNU application |
| `EFI/UniXNU/Payloads/DarwinBoot.efi` | DarwinBoot UEFI driver |
| `EFI/UniXNU/Payloads/kernel.macho` | Prelinked XNU kernel |
| `EFI/UniXNU/Payloads/kernel.macho.adt` | QEMU-specific Apple-format ADT |
| `EFI/UniXNU/unixnu.toml` | `xnu` menu entry and kernel arguments |

The ADT comes from `src/UniXNU/scripts/qemu-entry/make-dt.py`. The TCG ADT
uses the QEMU PL011/GICv3 description and a 62.5 MHz timebase. The HVF ADT
uses the Mac host counter frequency (24 MHz on the test M4) and a generic ARM
CPU identity. Both use QEMU 1 GiB RAM, two CPUs and a 256 MiB DarwinBoot
window.

The ESP is presented read-only through QEMU `vvfat`; QEMU does not modify the
tablet inputs or the staged files.

## Local TCG

Stage and run:

```sh
python3 src/UniXNU-gta4l/scripts/qemu-smoke.py \
  --accel tcg \
  --kernel out/gta4l/kernel/kernel.macho \
  --out out/qemu/gta4l-smoke \
  --run-timeout 40 --screenshot \
  --expect 'DARWINBOOT: inputs validated'
```

Run the early checkpoints against the staged ESP:

```sh
python3 src/UniXNU-gta4l/scripts/qemu-smoke.py \
  --accel tcg \
  --kernel out/gta4l/kernel/kernel.macho \
  --out out/qemu/gta4l-smoke \
  --check-only --checkpoints _arm_init,_arm_vm_init,_serial_init,_PE_create_console
```

The runner writes `manifest.json`, `serial.log`, `screen.png`, per-checkpoint
JSON and `checkpoints.json`. Checkpoints that use `--finish` report `RETURNED`
when the function reached its caller.

To make a kernel panic visible on the QEMU framebuffer, stage a copy with the
existing panic-screen probe. This does not change the source kernel:

```sh
python3 src/UniXNU-gta4l/scripts/qemu-smoke.py \
  --accel tcg \
  --kernel out/gta4l/kernel/kernel.macho \
  --kernel-label unmodified-panic-screen \
  --panic-screen \
  --out out/qemu/gta4l-panic-screen \
  --run-timeout 25 --screenshot --screenshot-delay 16
```

The staged copy is written as `kernel-panic-screen.macho`; the manifest records
both the source and staged hashes.

`--osenvironment ramdisk|recoveryos|main` adds `/chosen/osenvironment` to the
generated ADT. It does not change the AppleImage4 failure described below.
Use `--adt PATH` to supply a different Apple-format ADT.

To test the PAN-NOP diagnostic kernel, pass it explicitly and label the run:

```sh
python3 src/UniXNU-gta4l/scripts/qemu-smoke.py \
  --accel tcg \
  --kernel /tmp/opencode/kernel-pan-nop.macho \
  --kernel-label pan-nop \
  --out out/qemu/gta4l-pan-nop
```

## Mac HVF

HVF requires an arm64 Mac, macOS and QEMU from Homebrew:

```sh
brew install qemu
```

The same runner works with `--accel hvf`; it switches QEMU to `-cpu host`.
Stage the ESP on the Linux host, copy it to the Mac, then run the checkpoints
there:

```sh
# Linux host
scp -r out/qemu/gta4l-smoke/esp user@mac:/path/to/qemu-esp-xnu

# Mac
python3 qemu-smoke.py \
  --accel hvf \
  --esp /path/to/qemu-esp-xnu \
  --kernel /path/to/kernel.macho \
  --gdb-stop /path/to/gdb-stop.py \
  --firmware /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
  --out /path/to/qemu-hvf \
  --check-only --checkpoints _arm_init,_arm_vm_init,_serial_init,_PE_create_console
```

When staging on the Mac, regenerate the ADT with HVF values; the runner does
this automatically if `--accel hvf` is used. Do not reuse the TCG ADT for HVF
unless the timebase mismatch is an intentional part of the experiment.

## Observations from the first run

These are observations, not a boot claim:

- TCG and HVF both reach `_arm_init`, `_arm_vm_init`,
  `_PE_consistent_debug_inherit`, `_serial_init`,
  `_kernel_startup_initialize_upto`, `_PE_create_console`,
  `_PE_init_printf`, the second `_PE_init_platform`, `_machine_startup` and
  `_kernel_bootstrap`.
- `_serial_init` returns `0`. The QEMU PL011 is not the Apple UART that this
  XNU serial layer looks for, so kernel serial output is absent. This is
  expected for the current ADT.
- `_PE_consistent_debug_inherit` and `_PE_create_console` both return. The
  hardware reset probes that appeared to stop at those functions did not
  establish that the functions hung.
- At `_PE_create_console`, `PE_state.video` is 800x600, 32 bpp, with
  `v_display=1` and the physical framebuffer base marked in bit 0. The panic
  screen draws through the same framebuffer path, so a blank normal console
  means the kernel has not emitted console output there, not that video
  handoff failed.
- The PAN-NOP kernel enters `_panic` with the string `context switch with PAN
  disabled`. That is the diagnostic patch's effect: the unmodified kernel runs
  the same early checkpoints with PAN enabled and does not take that panic.
  The unmodified kernel's pre-`_bsd_init` abort was traced to the
  `com.apple.security.AppleImage4` `/options` lookup. The synthetic ADT had
  no root `/options` node, so the result stored at the site instance was
  null and the later nonce-magazine read dereferenced it at
  `ldr x8, [x0]`. Adding an empty `/options`, a `/chosen` CHRP NVRAM proxy,
  `crypto-hash-method`, and `amfi-only-platform-code` removed that abort;
  the kernel then reached `_bsd_init` and `_IOKitBSDInit`. AMFI then stopped
  at the missing `ApplePMGRNub` service, which is a platform-provider gap.
  Do not add fake nonces or skip the AMFI check to get past it.
- The PMGR-linked kernel was run with
  `--kernel-args "-v serial=3 debug=0x14e keepsym=1 sheng-pmgr=2"` and no
  `amfi_get_out_of_my_way`/`cs_enforcement_disable`. It reached `_bsd_init`,
  `_sheng_pmgr_run` and `_IOKitBSDInit` with no AMFI "No PMGR" panic and no
  `_panic` checkpoint. The run timed out before `_vfs_mountroot` with the PC
  at `_ml_get_timebase` (`0xfffffff00741a110`). The QEMU serial layer still
  has no Apple-compatible UART, so this is checkpoint evidence only. The
  bridge's publication was not independently read back.
- The raw `qemu-entry` flat fixture reaches `_arm_init` but faults before
  `_arm_vm_init`. That is a flat-loader/prelinked-segment mapping artifact.
  Use the ESP/DarwinBoot route for kernel conclusions.

## Limitations

- QEMU `virt` does not validate SM6115 devices, the panel, UFS, PMIC, GPU or
  Samsung firmware.
- HVF changes the CPU identity, counter frequency and timing relative to TCG.
  Use TCG when deterministic timing matters and HVF when iteration speed
  matters.
- The current QEMU ADT has no Apple-compatible serial node, so serial output
  from XNU is not available unless the kernel is changed or a compatible
  device is added.
- The current QEMU ADT does not provide the `ApplePMGRNub` service that AMFI
  requires after `_IOKitBSDInit`; this is a fixture gap, not a tablet
  checkpoint result.
- The framebuffer capture is the QEMU `ramfb` surface. A menu or green-bar
  frame is DarwinBoot's last frame, not evidence that the kernel console is
  drawing.
