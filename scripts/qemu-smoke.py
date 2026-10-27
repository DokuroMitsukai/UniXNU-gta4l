#!/usr/bin/env python3
"""Stage and exercise the gta4l XNU kernel on QEMU virt.

This is an offline bring-up fixture, not a gta4l hardware emulator. It uses
the real UniXNU/DarwinBoot handoff and a QEMU-specific Apple-format ADT, then
can stop at early kernel symbols with the existing QEMU gdbstub helper.

TCG is the deterministic default. HVF is supported on macOS arm64 and needs
QEMU from Homebrew. The same ESP and kernel are used for both accelerators.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UNIXNU = ROOT / "src" / "UniXNU"
BOARD = ROOT / "src" / "UniXNU-gta4l"
DEFAULT_KERNEL = ROOT / "out" / "gta4l" / "kernel" / "kernel.macho"
DEFAULT_UNIXNU = ROOT / "out" / "gta4l" / "package" / "Build" / "Out" / "AARCH64-DEBUG" / "UniXNU.efi"
DEFAULT_DARWINBOOT = ROOT / "src" / "Mu-Silicium" / "Build" / "gta4lPkg" / "DEBUG_CLANGPDB" / "AARCH64" / "DarwinBoot.efi"
DEFAULT_BASE_ESP = UNIXNU / "package" / "UniXNUPkg" / "Test" / "Qemu" / "esp"
MAKE_DT = UNIXNU / "scripts" / "qemu-entry" / "make-dt.py"
GDB_STOP = UNIXNU / "scripts" / "qemu-entry" / "gdb-stop.py"

EARLY_SYMBOLS = [
    "_arm_init",
    "_arm_vm_init",
    "_PE_consistent_debug_inherit",
    "_serial_init",
    "_kernel_startup_initialize_upto",
    "_PE_create_console",
]
LATE_SYMBOLS = [
    "_PE_init_printf",
    "_machine_startup",
    "_kernel_bootstrap",
    "_bsd_init",
    "_vfs_mountroot",
    "_panic",
]
DEFAULT_ARGS = ("-v serial=3 debug=0x14e keepsym=1 "
                "amfi_get_out_of_my_way=1")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command, **kwargs):
    print("+", " ".join(str(value) for value in command), flush=True)
    return subprocess.run([str(value) for value in command], **kwargs)


def find_qemu(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise SystemExit(f"QEMU not found: {path}")
        return path
    found = shutil.which("qemu-system-aarch64")
    if not found:
        raise SystemExit("qemu-system-aarch64 not found; install QEMU first")
    return Path(found)


def find_firmware(qemu: Path, explicit_code: str | None, explicit_vars: str | None):
    if explicit_code:
        code = Path(explicit_code).resolve()
        if not code.is_file():
            raise SystemExit(f"UEFI code image not found: {code}")
    else:
        candidates = [
            qemu.resolve().parent.parent / "share" / "qemu" / "edk2-aarch64-code.fd",
            Path("/opt/homebrew/share/qemu/edk2-aarch64-code.fd"),
            Path("/usr/share/edk2/aarch64/QEMU_EFI.fd"),
            Path("/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"),
            Path("/usr/share/AAVMF/AAVMF_CODE.fd"),
        ]
        code = next((path for path in candidates if path.is_file()), None)
        if code is None:
            raise SystemExit("no AArch64 UEFI firmware found; pass --firmware")
    if explicit_vars:
        variables = Path(explicit_vars).resolve()
        if not variables.is_file():
            raise SystemExit(f"UEFI variables image not found: {variables}")
    else:
        variables = None
        for name in ("edk2-arm-vars.fd", "QEMU_VARS.fd", "AAVMF_VARS.fd"):
            candidate = code.with_name(name)
            if candidate.is_file():
                variables = candidate
                break
    return code, variables


def require_accel(accel: str) -> str:
    if accel == "hvf":
        if platform.system() != "Darwin" or platform.machine() != "arm64":
            raise SystemExit("HVF requires macOS arm64; use --accel tcg elsewhere")
        return "host"
    if accel != "tcg":
        raise SystemExit(f"unsupported accelerator: {accel}")
    return "cortex-a710"


def make_adt(args, output: Path) -> None:
    command = [
        sys.executable, args.make_dt,
        "--armvirt", "--armcpu", "--smp", str(args.smp),
        "--dram-size", hex(args.memory_mib * 1024 * 1024),
        "--owned-memory-size", "0x10000000",
        "--nvram-proxy",
        "--crypto-hash-method", "sha2-384",
        "--amfi-platform-only",
        "--output", output,
    ]
    if args.accel == "hvf":
        command += ["--qemu-accel", "hvf", "--qemu-vmm-present"]
    if args.osenvironment:
        command += ["--osenvironment", args.osenvironment]
    run(command, check=True)


def write_config(esp: Path, args) -> None:
    config = f"""version = 1

[boot]
timeout = 1
default = "xnu"

[kits]
autoload = false

[[driver]]
path = "Payloads/DarwinBoot.efi"

[[entry]]
id = "xnu"
title = "XNU QEMU {args.kernel_label}"
type = "xnu"
path = "Payloads/kernel.macho"
args = "{args.kernel_args}"

[[entry]]
id = "reset"
title = "Restart"
type = "action"
action = "reset"

[[entry]]
id = "shutdown"
title = "Power off"
type = "action"
action = "shutdown"
"""
    (esp / "EFI" / "UniXNU" / "unixnu.toml").write_text(config)


def stage(args) -> Path:
    kernel = Path(args.kernel).resolve()
    if not kernel.is_file():
        raise SystemExit(f"kernel not found: {kernel}")
    if kernel.read_bytes()[:4] != b"\xcf\xfa\xed\xfe":
        raise SystemExit("kernel is not a little-endian 64-bit Mach-O")
    unixnu = Path(args.unixnu).resolve()
    darwinboot = Path(args.darwinboot).resolve()
    if not unixnu.is_file():
        raise SystemExit(f"UniXNU.efi not found: {unixnu}")
    if not darwinboot.is_file():
        raise SystemExit(f"DarwinBoot.efi not found: {darwinboot}")

    out = Path(args.out).resolve()
    esp = Path(args.esp).resolve() if args.esp else out / "esp"
    if esp.exists():
        shutil.rmtree(esp)
    base = Path(args.base_esp).resolve()
    if not base.is_dir():
        raise SystemExit(f"base QEMU ESP not found: {base}")
    shutil.copytree(base, esp)
    (esp / "EFI" / "BOOT").mkdir(parents=True, exist_ok=True)
    (esp / "EFI" / "UniXNU" / "Payloads").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(unixnu, esp / "EFI" / "BOOT" / "BOOTAA64.EFI")
    shutil.copyfile(darwinboot, esp / "EFI" / "UniXNU" / "Payloads" / "DarwinBoot.efi")
    staged_kernel = kernel
    if args.panic_screen:
        staged_kernel = out / "kernel-panic-screen.macho"
        shutil.copyfile(kernel, staged_kernel)
        patch_script = Path(args.patch_script).resolve()
        if not patch_script.is_file():
            raise SystemExit(f"panic-screen patch script not found: {patch_script}")
        uefi = Path(args.uefi).resolve()
        if not (uefi / "Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h").is_file():
            raise SystemExit(f"panic-screen font not found under {uefi}")
        run([sys.executable, patch_script, staged_kernel, "--panic-screen", "--no-reset"],
            check=True, env=dict(os.environ, UNIXNU_UEFI=str(uefi)))
    shutil.copyfile(staged_kernel, esp / "EFI" / "UniXNU" / "Payloads" / "kernel.macho")
    args.check_kernel = str(staged_kernel)

    adt = esp / "EFI" / "UniXNU" / "Payloads" / "kernel.macho.adt"
    if args.adt:
        shutil.copyfile(Path(args.adt).resolve(), adt)
    else:
        make_adt(args, adt)
    write_config(esp, args)

    manifest = {
        "fixture": "qemu-virt",
        "accel": args.accel,
        "cpu": require_accel(args.accel),
        "memory_mib": args.memory_mib,
        "smp": args.smp,
        "gic_version": args.gic_version,
        "kernel": str(kernel),
        "kernel_sha256": sha256(staged_kernel),
        "kernel_source": str(kernel),
        "kernel_source_sha256": sha256(kernel),
        "panic_screen": bool(args.panic_screen),
        "unixnu_sha256": sha256(unixnu),
        "darwinboot_sha256": sha256(darwinboot),
        "adt_sha256": sha256(adt),
        "esp": str(esp),
        "kernel_args": args.kernel_args,
        "kernel_label": args.kernel_label,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"ESP: {esp}")
    print(f"ADT: {adt} ({manifest['adt_sha256']})")
    return esp


def ppm_to_png(ppm: Path, png: Path) -> None:
    data = ppm.read_bytes()
    separator = rb"(?:[ \t\r\n\v\f]|#[^\r\n]*(?:\r\n|[\r\n]))+"
    header = re.match(rb"P6" + separator + rb"([0-9]+)" + separator +
                      rb"([0-9]+)" + separator + rb"([0-9]+)[ \t\r\n\v\f]", data)
    if header is None:
        raise ValueError("invalid PPM")
    width, height, maxval = map(int, header.groups())
    if maxval != 255:
        raise ValueError("PPM maxval is not 255")
    pixels = data[header.end():]
    if len(pixels) != width * height * 3:
        raise ValueError("PPM raster length mismatch")
    raw = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3]
                   for y in range(height))

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    png.write_bytes(b"\x89PNG\r\n\x1a\n" +
                    chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def qmp_connect(path: Path, timeout: float = 10):
    deadline = time.time() + timeout
    while True:
        try:
            connection = socket.socket(socket.AF_UNIX)
            connection.connect(str(path))
            break
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(0.1)
    stream = connection.makefile("rw")
    stream.readline()
    def command(name, **arguments):
        message = {"execute": name}
        if arguments:
            message["arguments"] = arguments
        stream.write(json.dumps(message) + "\n")
        stream.flush()
        while True:
            reply = json.loads(stream.readline())
            if "return" in reply or "error" in reply:
                return reply
    command("qmp_capabilities")
    return connection, stream, command


def run_qemu(args, esp: Path, code: Path, variables: Path | None) -> int:
    qemu = find_qemu(args.qemu)
    cpu = require_accel(args.accel)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    serial = Path(args.serial).resolve() if args.serial else out / "serial.log"
    serial.parent.mkdir(parents=True, exist_ok=True)
    serial.unlink(missing_ok=True)
    variables_copy = None
    if variables:
        variables_copy = out / "vars.fd"
        shutil.copyfile(variables, variables_copy)

    qmp = None
    qmp_path = None
    if args.screenshot:
        qmp_path = out / "qmp.sock"
        qmp_path.unlink(missing_ok=True)

    command = [
        qemu, "-M", f"virt,gic-version={args.gic_version},virtualization=off,secure=off",
        "-accel", args.accel, "-cpu", cpu,
        "-m", str(args.memory_mib), "-smp", str(args.smp),
        "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
    ]
    if variables_copy:
        command += ["-drive", f"if=pflash,format=raw,file={variables_copy}"]
    command += [
        "-drive", f"if=none,id=esp,format=raw,readonly=on,file=fat:{esp.resolve()}",
        "-device", "virtio-blk-pci,drive=esp",
    ]
    if args.ramfb:
        command += ["-device", "ramfb"]
    command += [
        "-device", "qemu-xhci", "-device", "usb-kbd",
        "-nic", "none", "-display", "none", "-no-reboot",
        "-serial", f"file:{serial}",
    ]
    if qmp_path:
        command += ["-qmp", f"unix:{qmp_path},server=on,wait=off"]

    print("QEMU:", " ".join(str(value) for value in command), flush=True)
    error_path = out / "qemu.err"
    with error_path.open("w") as errors:
        process = subprocess.Popen([str(value) for value in command],
                                   stdout=errors, stderr=subprocess.STDOUT)
        screenshot_done = False
        screenshot_at = max(1.0, args.screenshot_delay)
        started = time.monotonic()
        qmp_objects = None
        try:
            if qmp_path:
                try:
                    qmp_objects = qmp_connect(qmp_path, timeout=10)
                except Exception as error:
                    print(f"screenshot disabled: {error}", file=sys.stderr)
            while process.poll() is None:
                elapsed = time.monotonic() - started
                if qmp_objects and not screenshot_done and elapsed >= screenshot_at:
                    _, _, command_qmp = qmp_objects
                    ppm = out / "screen.ppm"
                    reply = command_qmp("screendump", filename=str(ppm))
                    if "return" in reply:
                        try:
                            ppm_to_png(ppm, out / "screen.png")
                            print(f"screenshot: {out / 'screen.png'}", flush=True)
                        except Exception as error:
                            print(f"screenshot conversion failed: {error}", file=sys.stderr)
                    else:
                        print(f"screendump failed: {reply}", file=sys.stderr)
                    screenshot_done = True
                if elapsed >= args.run_timeout:
                    break
                time.sleep(0.1)
        finally:
            if qmp_objects:
                connection, stream, command_qmp = qmp_objects
                if process.poll() is None:
                    try:
                        command_qmp("quit")
                    except Exception:
                        pass
                try:
                    stream.close()
                except OSError:
                    pass
                try:
                    connection.close()
                except OSError:
                    pass
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = serial.read_text(errors="replace") if serial.exists() else ""
    print(f"serial: {serial}")
    print("\n".join(text.splitlines()[-80:]))
    failed = 0
    for pattern in args.expect:
        matched = re.search(pattern, text) is not None
        print(f"{'PASS' if matched else 'FAIL'} expect {pattern}")
        failed += not matched
    return 1 if failed else 0


def symbols(kernel: Path):
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if not nm:
        raise SystemExit("no llvm-nm or nm found")
    output = subprocess.check_output([nm, "-n", str(kernel)], text=True)
    table = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        table.setdefault(address, parts[-1])
    return table


def checkpoint(args, esp: Path, kernel: Path, code: Path, names) -> int:
    table = symbols(kernel)
    by_name = {name: address for address, name in table.items()}
    out = Path(args.out).resolve()
    runtime = Path(args.out).resolve() / "gdb-run"
    runtime.mkdir(parents=True, exist_ok=True)
    results = {}
    failed = 0
    for raw_name in names:
        name = raw_name.strip()
        if not name:
            continue
        if name.startswith("0x"):
            address = int(name, 16)
            label = name
        else:
            if name not in by_name:
                raise SystemExit(f"symbol not found: {name}")
            address = by_name[name]
            label = name
        print(f"=== {label} {address:#x}", flush=True)
        gdb_stop = Path(args.gdb_stop).resolve()
        command = [
            sys.executable, gdb_stop, hex(address),
            "--esp", esp, "--kernel", kernel,
            "--fixture", runtime,
            "--qemu-accel", args.accel,
            "--qemu-memory-mib", str(args.memory_mib),
            "--gic-version", str(args.gic_version),
            "--smp", str(args.smp),
            "--system-registers",
            "--timeout", str(args.gdb_timeout),
        ]
        if args.firmware:
            command += ["--firmware", args.firmware]
        if args.ramfb:
            command += ["--qemu-ramfb"]
        if name in args.finish:
            command += ["--finish-function"]
        log_path = out / f"gdb-{label.replace('/', '_')}.log"
        process = subprocess.run([str(value) for value in command],
                                 text=True, capture_output=True,
                                 timeout=args.gdb_timeout + 30)
        log_path.write_text(process.stdout +
                            (f"\n--- STDERR ---\n{process.stderr}" if process.stderr else ""))
        report_path = runtime / "gdb-stop.json"
        failure_path = runtime / "gdb-failure.json"
        result = {"symbol": label, "address": hex(address)}
        if report_path.exists():
            report = json.loads(report_path.read_text())
            shutil.copyfile(report_path, out / f"checkpoint-{label.replace('/', '_')}.json")
            returned = (name in args.finish and
                        report.get("pc") == report.get("x30") and
                        not report.get("bounded_timeout_interrupted"))
            result.update(
                entered=report.get("pc") == hex(address),
                returned=returned,
                reached=(report.get("pc") == hex(address)) or returned,
                pc=report.get("pc"),
                timeout=report.get("bounded_timeout_interrupted"),
                x0=report.get("x0"),
                x1=report.get("x1"),
                x30=report.get("x30"),
                elr=report.get("system_registers", {}).get("ELR_EL1"),
                esr=report.get("system_registers", {}).get("ESR_EL1"),
                far=report.get("system_registers", {}).get("FAR_EL1"),
            )
            if name == "_panic" and result["reached"]:
                result["panic_arguments"] = report.get("argument_memory", {})
        elif failure_path.exists():
            failure = json.loads(failure_path.read_text())
            shutil.copyfile(failure_path, out / f"failure-{label.replace('/', '_')}.json")
            result.update(error=failure.get("error"),
                          partial_pc=failure.get("partial_report", {}).get("pc"))
        else:
            result.update(error="no gdb report", returncode=process.returncode,
                          stderr=process.stderr[-1000:])
        if result.get("error"):
            failed += 1
        results[label] = result
        print(json.dumps(result, ensure_ascii=False), flush=True)
        for path in (report_path, failure_path):
            path.unlink(missing_ok=True)
    (out / "checkpoints.json").write_text(json.dumps(results, indent=2) + "\n")
    print("\nCHECKPOINT SUMMARY")
    for name, result in results.items():
        if result.get("returned"):
            state = "RETURNED"
        elif result.get("reached"):
            state = "REACHED"
        else:
            state = "ERROR" if result.get("error") else "NOT-REACHED"
        print(f"{state:11} {name:36} pc={result.get('pc')} "
              f"x0={result.get('x0')} x30={result.get('x30')} "
              f"ELR={result.get('elr')} ESR={result.get('esr')}")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kernel", default=str(DEFAULT_KERNEL), help="prelinked XNU Mach-O")
    parser.add_argument("--unixnu", default=str(DEFAULT_UNIXNU), help="UniXNU.efi")
    parser.add_argument("--darwinboot", default=str(DEFAULT_DARWINBOOT), help="DarwinBoot.efi")
    parser.add_argument("--base-esp", default=str(DEFAULT_BASE_ESP), help="QEMU ESP skeleton")
    parser.add_argument("--esp", help="existing/staged ESP directory")
    parser.add_argument("--adt", help="existing Apple-format ADT; generated when omitted")
    parser.add_argument("--out", default=str(ROOT / "out" / "qemu" / "gta4l-smoke"))
    parser.add_argument("--make-dt", default=str(MAKE_DT))
    parser.add_argument("--gdb-stop", default=str(GDB_STOP))
    parser.add_argument("--panic-screen", action="store_true",
                        help="patch a staged copy with the framebuffer panic screen")
    parser.add_argument("--patch-script", default=str(UNIXNU / "scripts" / "patch-xnu-entry-probe.py"),
                        help="entry-probe/panic-screen patch script")
    parser.add_argument("--uefi", default=str(ROOT / "src" / "Mu-Silicium"),
                        help="Mu-Silicium tree with the panic-screen font")
    parser.add_argument("--firmware", help="AArch64 UEFI code image")
    parser.add_argument("--firmware-vars", help="AArch64 UEFI variables image")
    parser.add_argument("--qemu", help="qemu-system-aarch64")
    parser.add_argument("--accel", choices=["tcg", "hvf"], default="tcg")
    parser.add_argument("--memory-mib", type=int, default=1024)
    parser.add_argument("--smp", type=int, default=2)
    parser.add_argument("--gic-version", choices=["2", "3"], default="3")
    parser.add_argument("--kernel-args", default=DEFAULT_ARGS)
    parser.add_argument("--kernel-label", default="unmodified")
    parser.add_argument("--osenvironment", choices=["ramdisk", "recoveryos", "main"],
                        help="optional /chosen/osenvironment for the generated ADT")
    parser.add_argument("--run-timeout", type=float, default=45.0)
    parser.add_argument("--gdb-timeout", type=float, default=30.0)
    parser.add_argument("--serial", help="serial log path")
    parser.add_argument("--expect", action="append", default=[],
                        help="regex expected in the serial log")
    parser.add_argument("--screenshot", action="store_true",
                        help="capture the ramfb framebuffer after --screenshot-delay")
    parser.add_argument("--screenshot-delay", type=float, default=10.0)
    parser.add_argument("--no-ramfb", dest="ramfb", action="store_false")
    parser.add_argument("--checkpoints", default=",".join(EARLY_SYMBOLS),
                        help="comma-separated symbols or 0x addresses")
    parser.add_argument("--all-checkpoints", action="store_true",
                        help="use early and late checkpoint lists")
    parser.add_argument("--finish", default="",
                        help="comma-separated checkpoints to run with --finish-function")
    parser.add_argument("--stage-only", action="store_true")
    parser.add_argument("--run-only", action="store_true")
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--skip-check", action="store_true")
    parser.set_defaults(ramfb=True)
    args = parser.parse_args()
    if args.all_checkpoints:
        args.checkpoints = ",".join(EARLY_SYMBOLS + LATE_SYMBOLS)
    args.finish = {part.strip() for part in args.finish.split(",") if part.strip()}
    require_accel(args.accel)
    if args.run_only and not args.esp:
        raise SystemExit("--run-only requires --esp")
    if args.check_only and not args.esp:
        raise SystemExit("--check-only requires --esp")
    if args.memory_mib < 512:
        raise SystemExit("--memory-mib must be at least 512")
    if args.screenshot and not args.ramfb:
        raise SystemExit("--screenshot requires ramfb")

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    esp = Path(args.esp).resolve() if args.esp else out / "esp"
    if not args.run_only and not args.check_only:
        esp = stage(args)
    if args.stage_only:
        return 0
    code, variables = find_firmware(find_qemu(args.qemu), args.firmware, args.firmware_vars)
    args.firmware = str(code)
    if not args.check_only:
        status = run_qemu(args, esp, code, variables)
        if status:
            return status
    if not args.skip_check:
        kernel = Path(getattr(args, "check_kernel", args.kernel)).resolve()
        return checkpoint(args, esp, kernel, code, args.checkpoints.split(","))
    return 0


if __name__ == "__main__":
    sys.exit(main())
