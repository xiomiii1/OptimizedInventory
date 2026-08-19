import argparse
import json
import zipfile
from pathlib import Path


def write_package(library: Path, icon: Path, output: Path) -> None:
    if not library.is_file():
        raise FileNotFoundError(library)
    if not icon.is_file():
        raise FileNotFoundError(icon)
    manifest = {
        "type": "preload-native",
        "name": "FastInventory",
        "author": "xiomi",
        "description": "Optimizes container inventory rendering by caching duplicate slot lookups within a UI frame.",
        "version": "1.0.0",
        "entry": "libFastInventory.so",
        "icon": "icon.png",
        "overwrite_files": ["icon.png"],
        "overwrite_folders": [],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        zf.write(library, "libFastInventory.so")
        zf.write(icon, "icon.png")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--library", required=True, type=Path)
    p.add_argument("--icon", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    write_package(a.library.resolve(), a.icon.resolve(), a.output.resolve())
    print(a.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
