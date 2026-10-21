#!/usr/bin/env python3
"""Prepare pinned Ubuntu 22.04 x86-64 tools locally, without sudo or OS changes."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shlex
import shutil
import ssl
import subprocess
import urllib.request
import zipfile

SOURCE = Path(__file__).resolve().parent
SOURCES = {
    "klayout.deb": ("https://www.klayout.org/downloads/Ubuntu-22/klayout_0.30.5-1_amd64.deb",
                    "18235fb899620079b2ab449f92908cbb53d921456ae09438ca722570cd154f87"),
    "openroad.deb": ("https://vaultlink.precisioninno.com/api/releases/26Q2-1164-g08f67ee5ec/"
                     "openroad_26Q2-1164-g08f67ee5ec_amd64-ubuntu-22.04.deb/download",
                     "1fe0e084dfc67a807f31a67a8f941e8d6dc4c3242cae0bdb0a67e1dae6f1c4a6"),
    "sv2v.zip": ("https://github.com/zachjs/sv2v/releases/download/v0.0.13/sv2v-Linux.zip",
                 "552799a1d76cd177b9b4cc63a3e77823a3d2a6eb4ec006569288abeff28e1ff8"),
    "rcx_patterns.rules": ("https://raw.githubusercontent.com/The-OpenROAD-Project/OpenROAD-flow-scripts/"
                           "7bcaf475c884e15f674e428aa04dbf92bd0426ed/flow/platforms/ihp-sg13g2/IHP_rcx_patterns.rules",
                           "ffda02d57a6ecb39b02268be9511d89ef072911f17bd2dcd095f3f3cbeb5a905"),
}


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(file.read()).hexdigest()


def verify(path, expected):
    if digest(path) != expected:
        raise ValueError(f"SHA256 mismatch: {path}")


def wrappers(destination):
    root = destination / "root"
    environment = {
        "LD_LIBRARY_PATH": ":".join(str(root / path) for path in
                                    ("usr/lib/x86_64-linux-gnu", "opt/or-tools/lib", "usr/lib/klayout")),
        "RUBYLIB": ":".join(str(root / path) for path in
                           ("usr/lib/ruby/3.0.0", "usr/lib/x86_64-linux-gnu/ruby/3.0.0")),
        "QT_QPA_PLATFORM": "offscreen",
    }
    prefix = "#!/bin/sh\n" + "".join(f"export {name}={shlex.quote(value)}\n" for name, value in environment.items())
    binary = destination / "bin"
    binary.mkdir(exist_ok=True)
    for name, target in {
        "openroad": root / "usr/bin/openroad", "klayout": root / "usr/bin/klayout",
        "yosys": root / "usr/bin/yosys", "abc": root / "usr/bin/berkeley-abc",
        "iverilog": root / "usr/bin/iverilog", "vvp": root / "usr/bin/vvp",
        "sv2v": destination / "sv2v-Linux/sv2v",
    }.items():
        # Icarus records VPI paths in compiled output; they must be absolute.
        extra = " -B " + shlex.quote(str(root / "usr/lib/x86_64-linux-gnu/ivl")) if name == "iverilog" else ""
        path = binary / name
        path.write_text(prefix + "exec " + shlex.quote(str(target)) + extra + ' "$@"\n')
        path.chmod(0o755)


def prepare(destination):
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise ValueError("these pinned tools require Linux x86-64; provide host tools for another platform")
    for program in ("apt-get", "dpkg-deb"):
        if not shutil.which(program):
            raise ValueError(f"missing {program}; this setup targets Ubuntu 22.04")
    destination.mkdir(parents=True, exist_ok=True)
    root = destination / "root"
    ca = Path("/etc/ssl/certs/ca-certificates.crt")
    context = ssl.create_default_context(cafile=str(ca) if ca.is_file() else None)
    for name, (url, sha256) in SOURCES.items():
        archive = destination / name
        if not archive.exists():
            print("Downloading", name, flush=True)
            temporary = archive.with_suffix(archive.suffix + ".partial")
            with urllib.request.urlopen(url, context=context, timeout=60) as response, temporary.open("wb") as file:
                shutil.copyfileobj(response, file)
            verify(temporary, sha256)
            temporary.rename(archive)
        verify(archive, sha256)
        if name.endswith(".deb"):
            subprocess.run(["dpkg-deb", "-x", str(archive), str(root)], check=True)
        elif name.endswith(".zip"):
            with zipfile.ZipFile(archive) as file:
                file.extractall(destination)
            (destination / "sv2v-Linux/sv2v").chmod(0o755)
    packages = []
    for line in (SOURCE / "tool_packages.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        package, version, sha256 = line.split("\t")
        matches = list(destination.glob(package + "_*.deb"))
        if not matches:
            packages.append(package + "=" + version)
    if packages:
        subprocess.run(["apt-get", "download", *packages], cwd=destination, check=True)
    for line in (SOURCE / "tool_packages.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        package, version, sha256 = line.split("\t")
        matches = list(destination.glob(package + "_*.deb"))
        if len(matches) != 1:
            raise ValueError("ambiguous/missing package: " + package)
        verify(matches[0], sha256)
        subprocess.run(["dpkg-deb", "-x", str(matches[0]), str(root)], check=True)
    wrappers(destination)
    for name, argument in (("klayout", "-v"), ("openroad", "-version"), ("yosys", "-V"), ("sv2v", "--version")):
        subprocess.run([str(destination / "bin" / name), argument], check=True)
    print("Prepared local tools in", destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=SOURCE.parents[1] / "out/repeater-toolchain")
    args = parser.parse_args()
    try:
        prepare(args.destination.resolve())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")
