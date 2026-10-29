#!/usr/bin/env python3
"""Verify the gta4l board inputs.

The firmware step runs this from out/<device>/tree, where kits/gta4l links to
the device package.  Resolve that link back to the device repository so the
check works both in-tree and from an assembled build tree.
"""
import pathlib
import sys


def main() -> int:
    tree = pathlib.Path(__file__).resolve().parent.parent
    device = tree
    kit_link = tree / "kits" / "gta4l"
    if kit_link.exists():
        # kits/gta4l -> <device>/package
        device = kit_link.resolve().parent

    board_toml = device / "board.toml"
    defconfig = device / "configs" / "gta4l_defconfig"
    missing = [str(p) for p in (board_toml, defconfig) if not p.exists()]
    if missing:
        print(f"[check-gta4l] Missing: {', '.join(missing)}", file=sys.stderr)
        return 1
    print("[check-gta4l] SM6115 (gta4l) board layout verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
