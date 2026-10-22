#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Prepare an isolated, pinned workspace from already resolved read-only sources."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

CORES = {"upstream": "5b51501fead672c41b5cfb396f3dafe0894bf4e9",
         "dya": "e5c9b6915b56801193e359dd9bad4a167ce0d1b8"}
PINS = {"zephyr": "10ba6d0cb38bc3d258775d27982f707599320085",
        "cmsis": "d1b8b20b6278615b00e136374540eb1c00dcabe7",
        "hal_nordic": "37ca068d7b013fb65a2acc9306bffa48a3e72839",
        "nanopb": "7307ce399b81ddcb3c3a5dc862c52d4754328d38"}
STUDIO = {"upstream": "6cb4c283e76209d59c45fbcb218800cd19e9339d",
          "dya": "89b81d2e587fce807b668dff2a6967a40beef421"}
CUSTOM_ARCHIVE = "3e815475a9756ef1b69d5352bc5d0284fec2b4cf20e1c402a474978010e83ff1"

def run(*args, cwd=None):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()

def identity(path, expected):
    actual = run("git", "-C", str(path), "rev-parse", "HEAD")
    if actual != expected:
        raise ValueError(f"wrong dependency revision: {path}: {actual}, expected {expected}")
    if run("git", "-C", str(path), "status", "--porcelain", "--untracked-files=all"):
        raise ValueError(f"dependency has local changes: {path}")
    return actual

def export_core(source, target):
    # ZMK CMake derives library names from application and resolved module
    # paths, so its app cannot be a symlink. A local shared clone keeps the
    # checked source unchanged and retains Git metadata for west imports.
    expected = run("git", "-C", str(source), "rev-parse", "HEAD")
    subprocess.run(["git", "clone", "--shared", "--no-checkout", str(source), str(target)],
                   check=True, stdout=sys.stderr, stderr=sys.stderr)
    subprocess.run(["git", "-C", str(target), "checkout", "--detach", expected],
                   check=True, stdout=sys.stderr, stderr=sys.stderr)
    # West reads imported manifests from this revision ref. It is the same
    # validated commit; no update, network request or source edit is involved.
    subprocess.run(["git", "-C", str(target), "update-ref", "refs/heads/manifest-rev", expected], check=True)
    identity(target, expected)

def validate_layout(source, projects, output):
    paths = []
    for relative, _ in projects.values():
        if relative == Path('.') or any(relative == reserved or reserved in relative.parents
                                        for reserved in (Path('config'), Path('.west'))):
            raise ValueError(f"unsafe project path: {relative}")
        for other in paths:
            if relative == other or relative in other.parents or other in relative.parents:
                raise ValueError(f"overlapping project paths: {other}, {relative}")
        paths.append(relative)
    destination = output.resolve()
    sources = [source, *(path.resolve() for _, path in projects.values())]
    if any(destination == path or path in destination.parents for path in sources):
        raise ValueError(f"output must be outside resolved sources: {output}")


def prepare(source, variant, manifest, output):
    source, manifest, output = Path(source).resolve(), Path(manifest).resolve(), Path(output).absolute()
    if variant not in CORES:
        raise ValueError("variant must be upstream or dya")
    if output.exists() or output.is_symlink():
        raise ValueError(f"output already exists: {output}")
    if not (source / ".west/config").is_file() or not manifest.is_file():
        raise ValueError("resolved west workspace and fixture manifest are required")
    rows = run("west", "list", "-f", "{name}|{path}|{abspath}", cwd=source).splitlines()
    projects = {}
    for row in rows:
        name, relative, absolute = row.split("|", 2)
        if name != "manifest":
            relative = Path(relative)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"unsafe project path: {relative}")
            projects[name] = (relative, Path(absolute))
    validate_layout(source, projects, output)
    pins = dict(PINS, zmk=CORES[variant], **{"zmk-studio-messages": STUDIO[variant]})
    identities = {}
    for name, pin in pins.items():
        if name not in projects:
            raise ValueError(f"missing required dependency: {name}")
        identities[name] = {"path": str(projects[name][1]), "revision": identity(projects[name][1], pin)}
    custom = "zmk-feature-custom-settings" in manifest.read_text()
    archive = None
    if custom:
        archive = Path(os.environ.get("ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE", ""))
        if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != CUSTOM_ARCHIVE:
            raise ValueError("provide the pinned custom-settings c6a7fef archive via ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE")
    # Atomic admission: an existing output is never reused, including races.
    output.mkdir(parents=True, exist_ok=False)
    workspace = output / "workspace"
    (workspace / ".west").mkdir(parents=True)
    (workspace / ".west/config").write_text("[manifest]\npath = config\nfile = west.yml\n[build]\nbuild-opt = -j2\n")
    (workspace / "config").mkdir()
    shutil.copyfile(manifest, workspace / "config/west.yml")
    for name, (relative, path) in projects.items():
        if name == "zmk-feature-custom-settings":
            continue
        target = workspace / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if name == "zmk":
            export_core(path, target)
        else:
            target.symlink_to(path, target_is_directory=True)
    if archive is not None:
        target = workspace / "zmk-feature-custom-settings"
        target.mkdir()
        with tarfile.open(archive) as bundle:
            bundle.extractall(target, filter="data")
        identities["zmk-feature-custom-settings"] = {"revision": "c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f", "archive_sha256": CUSTOM_ARCHIVE}
    (output / "dependencies.json").write_text(json.dumps({"variant": variant, "manifest": str(manifest), "projects": identities}, indent=2) + "\n")
    return workspace

if __name__ == "__main__":
    try:
        print(prepare(*sys.argv[1:]))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"offline admission failed: {error}")
