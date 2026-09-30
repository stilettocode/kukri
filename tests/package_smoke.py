"""Validate the archive allowlist, then test the executable users actually receive."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("package_directory", type=Path)
parser.add_argument("--checksums-only", action="store_true")
args = parser.parse_args()
packages = sorted(p for p in args.package_directory.iterdir()
                  if p.is_file() and (p.suffix in (".zip", ".deb") or p.name.endswith(".tar.gz")))
if not packages:
    raise SystemExit("No packages found")
if args.checksums_only:
    lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in packages]
    (args.package_directory / "SHA256SUMS").write_text("".join(lines), encoding="ascii")
    print(f"Checksums written for {len(packages)} packages")
    raise SystemExit(0)

archives = [p for p in packages if p.suffix != ".deb"]
if not archives:
    raise SystemExit("No archives found to test")
for archive in archives:
    root = Path(tempfile.gettempdir()).resolve()
    extracted = root / ("kukri package ' " + uuid.uuid4().hex)
    extracted.mkdir(mode=0o755)
    try:
        names = []
        def unpack(name, data, mode):
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                raise AssertionError(f"Unsafe archive path: {name}")
            target = extracted.joinpath(*path.parts)
            assert target.resolve().is_relative_to(extracted.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(mode & 0o777)
            names.append(path)
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as package:
                for member in package.infolist():
                    if not member.is_dir():
                        unpack(member.filename, package.read(member), 0o755 if member.filename.endswith(".exe") else 0o644)
        else:
            with tarfile.open(archive, "r:gz") as package:
                for member in package:
                    if member.isdir():
                        continue
                    if not member.isfile():
                        raise AssertionError(f"Unexpected archive entry: {member.name}")
                    with package.extractfile(member) as stream:
                        unpack(member.name, stream.read(), member.mode)
        binary = "kukri.exe" if os.name == "nt" else "kukri"
        expected = {f"bin/{binary}", "share/doc/kukri/README.md", "share/doc/kukri/LICENSE"}
        # CPack archives have a single versioned top-level directory.
        assert len({p.parts[0] for p in names}) == 1
        assert len(names) == len(expected)
        assert {str(PurePosixPath(*p.parts[1:])) for p in names} == expected, names
        exe = extracted / names[0].parts[0] / "bin" / binary
        version = subprocess.check_output([str(exe), "version"], text=True).strip()
        assert archive.name.startswith(f"kukri-{version}-"), (archive.name, version)
        git = shutil.which("git")
        if not git:
            raise SystemExit("Git is required to test the packaged executable")
        subprocess.run([sys.executable, str(Path(__file__).with_name("integration.py")),
                        str(exe), git], check=True)
        print(f"PASS: {archive.name}; exact payload, version, and packaged Git integration")
    finally:
        assert extracted.resolve().parent == root and extracted.name.startswith("kukri package '")
        shutil.rmtree(extracted)
