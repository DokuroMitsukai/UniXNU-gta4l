#!/usr/bin/env python3
"""Qualify the gta4l storage ABI on Linux/QEMU.

This is the Linux/QEMU qualification path from
``docs/gta4l-storage-publication-design.md`` section 2.2.1.  It does not link
or run the block driver.  It produces, for the actual prelinked gta4l
candidate:

* a bounded QEMU stop at ``_bsd_init`` with the live ``IOBlockStorageDevice``
  and ``IOService`` metaclass memory plus system registers;
* ``derive.json`` with the candidate vtable/metaclass addresses;
* ``interface-audit.json`` from the parameterized public-source audit;
* ``provenance.json`` with the hashes that pin the inputs and outputs.

The stop uses the PMGR-linked kernel arguments that reach ``_bsd_init`` in the
existing QEMU fixture.  No AMFI bypass and no device operation is involved.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
UNIXNU = WORKSPACE / "src" / "UniXNU"
PROFILE = "ipados-17.7.11-21H461"
KERNEL = UNIXNU / "out" / "gta4l" / PROFILE / "gta4l" / "kernel" / "kernel.macho"
TREE = UNIXNU / "out" / "gta4l" / PROFILE / "gta4l" / "tree"
FEED = UNIXNU / "out" / "feeds" / "IOStorageFamily-67f9003c"
COMPILE_RECORD = TREE / "build" / "xnu" / "ios-obj" / "DEVELOPMENT_ARM64_BCM2837" / \
    "iokit" / "DEVELOPMENT" / "IOPlatformExpert.cpo.json"
METACLASS_OBJECT = TREE / "build" / "xnu" / "ios-obj" / "DEVELOPMENT_ARM64_BCM2837" / \
    "libkern" / "DEVELOPMENT" / "OSMetaClass.cpo"
OSFMK_INCLUDE = TREE / ".cache" / "xnu-work" / "osfmk"
SDK_HEADER = UNIXNU / "scripts" / "linux" / "TargetConditionals.h"
AUDIT = UNIXNU / "scripts" / "audit-storage-interface.py"
GDB_STOP = UNIXNU / "scripts" / "qemu-entry" / "gdb-stop.py"
QEMU_SMOKE = ROOT / "scripts" / "qemu-smoke.py"
UNIXNU_EFI = WORKSPACE / "out" / "gta4l" / "package" / "Build" / "Out" / \
    "AARCH64-DEBUG" / "UniXNU.efi"
DARWINBOOT = WORKSPACE / "src" / "Mu-Silicium" / "Build" / "gta4lPkg" / \
    "DEBUG_CLANGPDB" / "AARCH64" / "DarwinBoot.efi"
BASE_ESP = UNIXNU / "package" / "UniXNUPkg" / "Test" / "Qemu" / "esp"
DEFAULT_ARGS = "-v serial=3 debug=0x14e keepsym=1 sheng-pmgr=2"
FIRMWARE_CANDIDATES = [
    Path("/usr/share/edk2/aarch64/QEMU_EFI-pflash.raw"),
    Path("/usr/share/AAVMF/AAVMF_CODE.fd"),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command, **kwargs):
    print("+", " ".join(str(value) for value in command), flush=True)
    return subprocess.run([str(value) for value in command], **kwargs)


def require(value, message):
    if not value:
        raise SystemExit(message)


def find_firmware(explicit: Path | None) -> Path:
    if explicit:
        path = explicit.resolve()
        require(path.is_file(), f"firmware not found: {path}")
        return path
    for path in FIRMWARE_CANDIDATES:
        if path.is_file():
            return path
    raise SystemExit("no 64 MiB AArch64 pflash image found; pass --firmware")


def find_bsd_init(kernel: Path) -> int:
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    require(nm, "no nm/llvm-nm on PATH")
    output = subprocess.check_output([nm, "-U", str(kernel)], text=True)
    match = re.search(r"^([0-9a-fA-F]+)\s+\S\s+_bsd_init$", output, re.M)
    require(match, "candidate has no _bsd_init symbol")
    return int(match.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kernel", type=Path, default=KERNEL,
                        help="prelinked gta4l candidate kernel")
    parser.add_argument("--out", type=Path, default=ROOT / "out" / "gta4l" / "storage-abi",
                        help="artifact directory")
    parser.add_argument("--qemu", default=None, help="qemu-system-aarch64")
    parser.add_argument("--firmware", type=Path,
                        help="64 MiB AArch64 UEFI pflash image")
    parser.add_argument("--dwarfdump", default="llvm-dwarfdump")
    parser.add_argument("--timeout", type=float, default=240.0,
                        help="QEMU gdb stop timeout in seconds")
    parser.add_argument("--kernel-args", default=DEFAULT_ARGS)
    parser.add_argument("--skip-qemu", action="store_true",
                        help="reuse an existing gdb-stop.json in --out")
    args = parser.parse_args()

    kernel = args.kernel.resolve()
    plan = kernel.with_suffix(".json")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for path in (kernel, plan, FEED, COMPILE_RECORD, METACLASS_OBJECT, SDK_HEADER,
                 AUDIT, GDB_STOP, QEMU_SMOKE, UNIXNU_EFI, DARWINBOOT, BASE_ESP):
        require(path.exists(), f"missing qualification input: {path}")
    require(FEED.is_dir(), f"missing public IOStorageFamily feed: {FEED}")
    require((FEED / ".git").exists(), f"feed is not a git checkout: {FEED}")

    kernel_sha = sha256(kernel)
    plan_data = json.loads(plan.read_text())
    require(kernel_sha == plan_data["candidate_sha256"],
            "candidate hash does not match its plan")
    require(plan_data["original_sha256"] ==
            "f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7",
            "unexpected original kernel provenance")

    audit_work = out / "audit"
    qemu_version = ""
    qemu = args.qemu or shutil.which("qemu-system-aarch64")
    if qemu:
        qemu_version = subprocess.check_output([qemu, "--version"], text=True).splitlines()[0]
    derive_path = audit_work / "derive.json"
    if not args.skip_qemu or not derive_path.exists():
        command = [
            sys.executable, str(AUDIT), "--derive-only",
            "--candidate", str(kernel), "--plan", str(plan),
            "--work-dir", str(audit_work), "--source", str(FEED),
            "--compile-record", str(COMPILE_RECORD),
            "--sdk-header", str(SDK_HEADER), "--sdk-version", "17.0",
            "--dwarfdump", args.dwarfdump,
            "--osfmk-include", str(OSFMK_INCLUDE),
            "--output", str(audit_work / "interface-audit.json"),
        ]
        result = run(command, text=True)
        require(result.returncode == 0, "audit address derivation failed")
    derive = json.loads(derive_path.read_text())
    meta = int(derive["metaclass"], 16)
    service_meta = int(derive["service_metaclass"], 16)
    require(derive["candidate_sha256"] == kernel_sha, "derive candidate mismatch")

    snapshot_path = out / "gdb-stop.json"
    run_path = out / "run.json"
    if not args.skip_qemu:
        firmware = find_firmware(args.firmware)
        esp_root = out / "esp"
        stage = [
            sys.executable, str(QEMU_SMOKE), "--stage-only",
            "--kernel", str(kernel), "--kernel-label", "storage-abi",
            "--kernel-args", args.kernel_args,
            "--unixnu", str(UNIXNU_EFI), "--darwinboot", str(DARWINBOOT),
            "--base-esp", str(BASE_ESP), "--out", str(esp_root),
        ]
        result = run(stage, text=True)
        require(result.returncode == 0, "ESP staging failed")
        esp = esp_root / "esp"
        manifest = json.loads((esp_root / "manifest.json").read_text())
        require(manifest["kernel_sha256"] == kernel_sha,
                "staged kernel hash changed")

        bsd_init = find_bsd_init(kernel)
        gdb_run = out / "gdb-run"
        if gdb_run.exists():
            shutil.rmtree(gdb_run)
        gdb_run.mkdir(parents=True)
        command = [
            sys.executable, str(GDB_STOP), hex(bsd_init),
            "--esp", str(esp), "--kernel", str(kernel),
            "--fixture", str(gdb_run),
            "--qemu-accel", "tcg", "--qemu-memory-mib", "1024",
            "--gic-version", "3", "--smp", "2",
            "--system-registers",
            "--read-memory", f"{meta:x}:50",
            "--read-memory", f"{service_meta:x}:50",
            "--firmware", str(firmware),
            "--timeout", str(args.timeout),
        ]
        result = run(command, text=True)
        require(result.returncode == 0, "QEMU gdb stop failed")
        produced = gdb_run / "gdb-stop.json"
        require(produced.exists(), "QEMU did not produce gdb-stop.json")
        shutil.copyfile(produced, snapshot_path)
        run_record = dict(
            candidate_sha256=kernel_sha,
            snapshot_sha256=sha256(snapshot_path),
            bsd_init=hex(bsd_init),
            kernel_args=args.kernel_args,
            qemu_version=qemu_version,
            firmware_sha256=sha256(firmware),
            firmware_name=firmware.name,
        )
        run_path.write_text(json.dumps(run_record, indent=2) + "\n")
    else:
        require(run_path.exists(),
                "missing run.json; cannot preserve the original run provenance")
        run_record = json.loads(run_path.read_text())
        require(run_record.get("candidate_sha256") == kernel_sha,
                "run.json candidate mismatch")
        require(run_record.get("snapshot_sha256") == sha256(snapshot_path),
                "run.json snapshot mismatch")
        bsd_init = find_bsd_init(kernel)
        require(run_record.get("bsd_init") == hex(bsd_init),
                "run.json stop address mismatch")
        require(args.kernel_args == run_record.get("kernel_args"),
                "kernel arguments differ from the recorded run")

    require(snapshot_path.exists(), "missing live storage metadata snapshot")
    snapshot = json.loads(snapshot_path.read_text())
    require(snapshot.get("kernel_sha256") == kernel_sha,
            "snapshot kernel hash mismatch")
    require(not snapshot.get("bounded_timeout_interrupted"),
            "snapshot was interrupted by the bounded timeout")
    require(snapshot.get("pc") == hex(bsd_init),
            "snapshot did not stop at _bsd_init")
    for address in (meta, service_meta):
        require(f"{address:x}:50" in snapshot.get("memory", {}),
                f"snapshot is missing live metadata at {address:#x}")

    interface_audit = out / "interface-audit.json"
    command = [
        sys.executable, str(AUDIT),
        "--candidate", str(kernel), "--plan", str(plan),
        "--snapshot", str(snapshot_path),
        "--derive", str(derive_path),
        "--expect-pc", hex(bsd_init),
        "--metaclass-object", str(METACLASS_OBJECT),
        "--output", str(interface_audit),
        "--work-dir", str(audit_work), "--source", str(FEED),
        "--compile-record", str(COMPILE_RECORD),
        "--sdk-header", str(SDK_HEADER), "--sdk-version", "17.0",
        "--dwarfdump", args.dwarfdump,
        "--osfmk-include", str(OSFMK_INCLUDE),
    ]
    result = run(command, text=True)
    require(result.returncode == 0, "storage ABI audit failed")
    audit = json.loads(interface_audit.read_text())
    require(audit["candidate_sha256"] == kernel_sha, "audit candidate mismatch")
    require(audit["method_slots"] == 190 and audit["kernel_bindings"] == 176,
            "unexpected storage dispatch shape")
    require(audit["live_native_4k"] is True, "live translation is not native 4K")
    require(audit["live_instance_sizes"] ==
            {"IOBlockStorageDevice": 0x90, "IOService": 0x88},
            "unexpected live instance sizes")
    require(audit["original_executable_code_unchanged"] is True,
            "original storage executable changed")
    require(audit["disk_read_tested"] is False and
            audit["transport_implemented"] is False and
            audit["hardware_tested"] is False,
            "audit must remain host-only")

    provenance = dict(
        kind="gta4l-storage-abi-linux-qemu",
        date="2026-11-05",
        candidate_sha256=kernel_sha,
        candidate_plan_sha256=sha256(plan),
        original_sha256=plan_data["original_sha256"],
        kernel_size=kernel.stat().st_size,
        source_revision=derive["source_revision"],
        source_sha256=derive["source_sha256"],
        compile_record_sha256=sha256(COMPILE_RECORD),
        reference_object_sha256=derive["reference_object_sha256"],
        metaclass_object_sha256=sha256(METACLASS_OBJECT),
        snapshot_sha256=sha256(snapshot_path),
        interface_audit_sha256=sha256(interface_audit),
        vtable=derive["vtable"],
        metaclass=derive["metaclass"],
        service_metaclass=derive["service_metaclass"],
        bsd_init=hex(bsd_init),
        kernel_args=run_record["kernel_args"],
        qemu_version=run_record["qemu_version"],
        firmware_sha256=run_record["firmware_sha256"],
        firmware_name=run_record.get("firmware_name", ""),
        run_json_sha256=sha256(run_path),
        harness_sha256=sha256(Path(__file__).resolve()),
        audit_script_sha256=sha256(AUDIT),
        gdb_stop_sha256=sha256(GDB_STOP),
        qemu_smoke_sha256=sha256(QEMU_SMOKE),
        unixnu_efi_sha256=sha256(UNIXNU_EFI),
        darwinboot_sha256=sha256(DARWINBOOT),
        host_system=platform.system(),
        host_machine=platform.machine(),
        disk_read_tested=False,
        transport_implemented=False,
        hardware_tested=False,
    )
    (out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print("PASS: gta4l storage ABI qualified on Linux/QEMU")
    print(f"  candidate {kernel_sha}")
    print(f"  vtable {derive['vtable']} metaclass {derive['metaclass']} "
          f"service {derive['service_metaclass']}")
    print(f"  snapshot {snapshot_path}")
    print(f"  audit {interface_audit}")
    print("  No driver is linked; block I/O and hardware remain unqualified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
