#!/usr/bin/env python3
"""Regenerate Thunder's deterministic first-party file and SHA-256 manifests."""

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = (
    ".github",
    "assets",
    "cmake",
    "docs",
    "shaders",
    "src",
    "tools",
)
ROOT_FILES = (
    ".gitattributes",
    ".gitignore",
    "CHANGELOG.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    "SECURITY.md",
    "THIRD_PARTY.md",
)


def first_party_paths() -> list[Path]:
    paths: set[Path] = set()
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if base.is_dir():
            paths.update(path for path in base.rglob("*") if path.is_file())
    paths.update(ROOT / name for name in ROOT_FILES if (ROOT / name).is_file())
    return sorted(paths, key=lambda path: path.relative_to(ROOT).as_posix())


def main() -> None:
    paths = first_party_paths()
    relative = [path.relative_to(ROOT).as_posix() for path in paths]
    (ROOT / "SOURCE_FILES_1_0.txt").write_text(
        "\n".join(relative) + "\n", encoding="utf-8", newline="\n"
    )
    hashes = [
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}"
        for path, name in zip(paths, relative, strict=True)
    ]
    (ROOT / "SOURCE_SHA256_1_0.txt").write_text(
        "\n".join(hashes) + "\n", encoding="utf-8", newline="\n"
    )
    print(f"Updated manifests for {len(paths)} first-party files.")


if __name__ == "__main__":
    main()
