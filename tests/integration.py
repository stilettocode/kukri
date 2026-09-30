"""End-to-end tests; all mutations occur in an automatically removed temporary tree."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import contextlib
import uuid

EXE = pathlib.Path(sys.argv[1]).resolve()
GIT = sys.argv[2]
ENV = os.environ.copy()
for key in list(ENV):
    if key.startswith("GIT_"):
        del ENV[key]
ENV.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
           GIT_AUTHOR_NAME="Kukri Test", GIT_AUTHOR_EMAIL="test@example.invalid",
           GIT_COMMITTER_NAME="Kukri Test", GIT_COMMITTER_EMAIL="test@example.invalid")
checks = 0

def call(args, cwd, data=None, ok=True):
    global checks
    p = subprocess.run([str(x) for x in args], cwd=cwd, input=data,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENV)
    checks += 1
    if ok and p.returncode:
        raise AssertionError(f"{args}: {p.returncode}\n{p.stdout!r}\n{p.stderr!r}")
    if not ok and not p.returncode:
        raise AssertionError(f"Expected failure: {args}")
    return p

@contextlib.contextmanager
def temporary_tree():
    # Python 3.13's Windows mode=0700 DACL excludes some sandbox identities.
    root = pathlib.Path(tempfile.gettempdir()).resolve()
    tree = root / ("kukri integration " + uuid.uuid4().hex)
    tree.mkdir(mode=0o755)
    try:
        yield tree
    finally:
        assert tree.resolve().parent == root and tree.name.startswith("kukri integration ")
        def remove_readonly(function, path, exc):
            candidate = pathlib.Path(path).resolve()
            assert candidate.is_relative_to(tree.resolve())
            os.chmod(path, 0o700)
            function(path)
        shutil.rmtree(tree, onerror=lambda fn, path, exc: remove_readonly(fn, path, exc[1]))

with temporary_tree() as tmp:
    base = pathlib.Path(tmp)
    # Exercise both native argv and Git's shell parsing, including a literal apostrophe/$.
    bindir = base / "tools space ' dollar$ unicode-é"
    bindir.mkdir()
    exe = bindir / EXE.name
    shutil.copy2(EXE, exe)
    repo = base / "repository space é"
    repo.mkdir()
    def git(*args, **kw): return call([GIT, *args], repo, **kw)
    def cli(*args, **kw): return call([exe, *args], repo, **kw)
    git("init")
    git("config", "core.autocrlf", "false")
    git("config", "user.test-preserve", "unchanged")
    attrs = repo / ".git" / "info" / "attributes"
    attrs.write_bytes(b"*.txt linguist-language=Text\r\n")
    before = attrs.read_bytes()
    cli("status")
    cli("doctor", ok=False)
    source = b'#include <string>\r\n// normal\r\n//k private line\r\nint main() {\r\n  std::string s = "//k literal";\r\n  /*k\r\n  private block\r\n  k*/\r\n  return 0;\r\n}\r\n'
    name = "example space é.cpp"
    file = repo / name
    file.write_bytes(source)
    for _ in range(3): cli("enable")
    assert attrs.read_bytes().count(b"# BEGIN kukri") == 1
    assert git("config", "--local", "filter.kukri.required").stdout.strip() == b"true"
    assert not (repo / ".gitattributes").exists()
    git("add", "--", name)
    assert file.read_bytes() == source
    staged = git("show", ":" + name).stdout
    assert b"private line" not in staged and b"private block" not in staged
    assert b'"//k literal"' in staged and b"// normal" in staged
    assert staged.count(b"\r\n") == source.count(b"\r\n")
    cli("verify")
    cli("doctor")
    cli("status")
    # Migrate the old generic filter/extension block without changing unrelated rules.
    new_command = git("config", "--local", "filter.kukri.clean").stdout.strip().decode()
    assert new_command.endswith(" clean --path %f")
    git("config", "--local", "filter.kukri.clean", new_command.removesuffix(" --path %f"))
    attrs.write_bytes(attrs.read_bytes().replace(b"# END kukri managed rules", b"*.rs filter=kukri\r\n*.jsx filter=kukri\r\n# END kukri managed rules"))
    cli("enable")
    assert b"*.rs " not in attrs.read_bytes() and b"*.jsx " not in attrs.read_bytes()
    assert b"*.py filter=kukri" in attrs.read_bytes()
    # Every supported family goes through filename-aware Git clean, verify and doctor.
    languages = {
        "family.c": (b"/\\\n/k secret\nint c;\n", b"\n\nint c;\n"),
        "family.go": (b"`raw\\` //k note\n", b"`raw\\` \n"),
        "Family.java": (br"\u002f\u002fk note" + b"\nclass Family {}\n", b"\nclass Family {}\n"),
        "notes ' dollar$.py": (b'f"{value #k note\n}"\n', b'f"{value \n}"\n'),
        "family.pyi": (b"#k note\nx: int\n", b"\nx: int\n"),
        "family.js": (b"if (ok) /[/*k]/.test(x); //k note\n", b"if (ok) /[/*k]/.test(x); \n"),
        "family.ts": (b"`text ${value /*k note k*/}`\n", b"`text ${value }`\n"),
    }
    for path, (local, canonical) in languages.items():
        (repo / path).write_bytes(local)
        git("add", "--", path)
        assert (repo / path).read_bytes() == local
        assert git("show", ":" + path).stdout == canonical
    for ext in ("mjs", "cjs", "mts", "cts"):
        path = "module." + ext
        (repo / path).write_bytes(b"//k module note\n")
        git("add", path)
        assert git("show", ":" + path).stdout == b"\n"
    for ext in ("rs", "cs", "m", "mm", "jsx", "tsx"):
        path = "excluded." + ext
        (repo / path).write_bytes(b"//k intentionally unfiltered\n")
        git("add", path)
        assert git("show", ":" + path).stdout == b"//k intentionally unfiltered\n"
    cli("verify")
    # A leaked Python comment must be detected even with a clean working file.
    py_oid = git("hash-object", "-w", "--stdin", "--no-filters", data=b"#k python leak\n").stdout.strip().decode()
    git("update-index", "--add", "--cacheinfo", "100644", py_oid, "leaked.py")
    (repo / "leaked.py").write_bytes(b"x=1\n")
    assert b"leaked.py" in cli("verify", ok=False).stderr
    git("rm", "--cached", "-f", "leaked.py")
    # Unsupported/ambiguous syntax fails before output and blocks staging.
    (repo / "ambiguous.js").write_bytes(b"if (x) {} /[/*k]/.test(x);\n")
    git("add", "ambiguous.js", ok=False)
    (repo / "ambiguous.js").unlink()
    cli("clean", "--path", "unsupported.rs", data=b"//k note", ok=False)
    assert cli("clean", "--language", "python", data=b"#k note\n").stdout == b"\n"
    # Root probes still pass while path-specific overrides leave real files unprotected.
    nested = repo / "src"
    nested.mkdir()
    audited_name = "src/special space.cpp"
    (repo / audited_name).write_bytes(b"int special;\n")
    (repo / "ignored.txt").write_bytes(b"ordinary text\n")
    git("add", "--", audited_name, "ignored.txt")
    healthy_attrs = attrs.read_bytes()
    index_before = git("ls-files", "--stage", "-z").stdout
    for assignment in ("-filter", "!filter", "filter=another", "filter"):
        attrs.write_bytes(healthy_attrs + ('"' + audited_name + '" ' + assignment + '\n').encode())
        result = cli("doctor", ok=False)
        assert audited_name.encode() in result.stderr
        assert b"1 not assigned to kukri" in result.stdout
        assert git("ls-files", "--stage", "-z").stdout == index_before
        assert (repo / audited_name).read_bytes() == b"int special;\n"
    # Overrides of unsupported extensions do not count as unprotected source.
    attrs.write_bytes(healthy_attrs + b"*.txt -filter\n")
    cli("doctor")
    attrs.write_bytes(healthy_attrs)
    cli("doctor")
    file.write_bytes(source.replace(b"private line", b"changed private text"))
    git("diff", "--exit-code", "--", name)
    file.write_bytes(source)
    # Raw strings, ordinary comments, and non-ASCII bytes survive real staging.
    raw = b'auto s = u8R"TAG(/*k preserved k*/ //k literal)TAG";\n/* normal */\n//k removed\n'
    (repo / "raw.cpp").write_bytes(raw)
    git("add", "raw.cpp")
    assert b'R"TAG(/*k preserved k*/ //k literal)TAG"' in git("show", ":raw.cpp").stdout
    # Malformed input emits no partial stdout; required filter prevents staging.
    malformed = b"int x;\n/*k unfinished\n"
    p = cli("clean", data=malformed, ok=False)
    assert not p.stdout and b"line 2" in p.stderr
    (repo / "bad.cpp").write_bytes(malformed)
    git("add", "bad.cpp", ok=False)
    (repo / "bad.cpp").unlink()
    command = git("config", "--local", "filter.kukri.clean").stdout.strip()
    git("config", "--local", "filter.kukri.clean", "kukri-nonexistent-command-123 clean")
    file.write_bytes(source + b"//k changed\n")
    git("add", "--", name, ok=False)
    assert git("show", ":" + name).stdout == staged
    cli("doctor", ok=False)
    git("config", "--local", "filter.kukri.clean", command.decode())
    # Insert an unfiltered blob directly: verify must read the index, not disk.
    oid = git("hash-object", "-w", "--stdin", "--no-filters", data=b"//k leaked\n").stdout.strip().decode()
    git("update-index", "--add", "--cacheinfo", "100644", oid, "leak.cpp")
    (repo / "leak.cpp").write_bytes(b"// clean working copy\n")
    p = cli("verify", ok=False)
    assert b"leak.cpp" in p.stderr
    git("rm", "--cached", "-f", "leak.cpp")
    cli("verify")
    # Linked worktree uses Git's actual common info/attributes location.
    git("commit", "-m", "fixture")
    wt = base / "linked worktree"
    git("worktree", "add", "-b", "test-linked", str(wt))
    call([exe, "enable"], wt)
    wf = wt / "worktree.cpp"
    wf.write_bytes(b"//k worktree private\nint x;\n")
    call([GIT, "add", "worktree.cpp"], wt)
    assert wf.read_bytes().startswith(b"//k")
    assert b"private" not in call([GIT, "show", ":worktree.cpp"], wt).stdout
    call([exe, "doctor"], wt)
    call([exe, "verify"], wt)
    # Re-enabling from a relocated binary repairs both driver paths.
    relocated = base / ("relocated-" + EXE.name)
    shutil.copy2(EXE, relocated)
    call([relocated, "enable"], repo)
    call([relocated, "doctor"], repo)
    cli("enable")
    # Explicit stable paths reject mistakes before changing repository configuration.
    saved_command = git("config", "--local", "filter.kukri.clean").stdout
    cli("enable", "--executable", "relative/kukri", ok=False)
    cli("enable", "--executable", str(base / "missing.exe"), ok=False)
    cli("enable", "--executable", str(relocated), ok=False)
    cli("status", "--executable", str(exe), ok=False)
    assert git("config", "--local", "filter.kukri.clean").stdout == saved_command
    cli("enable", "--executable", str(exe))
    cli("doctor")
    # A stable entry survives replacement of a versioned binary and deletion of the old one.
    v1, v2 = base / ("v1-" + EXE.name), base / ("v2-" + EXE.name)
    stable = base / ("stable ' $-" + EXE.name)
    shutil.copy2(EXE, v1)
    shutil.copy2(EXE, v2)
    def link_version(version):
        if os.name == "nt":
            os.link(version, stable)  # No Windows symlink privilege needed.
        else:
            stable.symlink_to(version)
    link_version(v1)
    call([stable, "enable", "--executable", str(stable)], repo)
    stable_command = git("config", "--local", "filter.kukri.clean").stdout
    assert str(stable).replace("\\", "/").split("stable")[0].encode() in stable_command
    call([stable, "enable"], repo)
    assert git("config", "--local", "filter.kukri.clean").stdout == stable_command
    stable.unlink()
    link_version(v2)
    v1.unlink()
    call([stable, "doctor"], repo)
    (repo / "upgrade.cpp").write_bytes(b"//k upgrade note\nint upgraded;\n")
    git("add", "upgrade.cpp")
    assert b"upgrade note" not in git("show", ":upgrade.cpp").stdout
    call([stable, "verify"], repo)
    assert git("config", "--local", "filter.kukri.clean").stdout == stable_command
    cli("enable")
    if len(sys.argv) >= 6:
        prefix = base / "installed space ' unicode-\u00e9"
        call([sys.argv[3], "--install", sys.argv[4], "--config", sys.argv[5],
              "--prefix", prefix], base)
        installed = prefix / "bin" / EXE.name
        assert installed.is_file()
        assert list(prefix.rglob("LICENSE")) and list(prefix.rglob("README.md"))
        assert not list(prefix.rglob("testing.md"))
        assert not list(prefix.rglob("architecture.md"))
        call([installed, "enable", "--executable", installed], repo)
        call([installed, "doctor"], repo)
        call([installed, "verify"], repo)
        cli("enable")
    # Preserve both sides of managed section through repeated disable.
    attrs.write_bytes(attrs.read_bytes() + b"*.other -text\r\n")
    cli("disable")
    cli("disable")
    assert attrs.read_bytes() == before + b"*.other -text\r\n"
    git("config", "--local", "--get", "filter.kukri.clean", ok=False)
    git("config", "--local", "--get", "filter.kukri.required", ok=False)
    git("config", "--local", "--get", "filter.kukri.smudge", ok=False)
    assert git("config", "user.test-preserve").stdout.strip() == b"unchanged"
    assert file.read_bytes() == source + b"//k changed\n"
    # Refuse malformed attributes without making configuration changes.
    attrs.write_bytes(b"# BEGIN kukri managed rules\n")
    cli("enable", ok=False)
    git("config", "--local", "--get", "filter.kukri.clean", ok=False)
    # Clean remains usable outside a repository and keeps stdout pipeline-safe.
    p = call([exe, "clean"], base, data=b"//k secret\r\nint x;\r\n")
    assert p.stdout == b"\r\nint x;\r\n" and not p.stderr
    call([exe, "status"], base, ok=False)
    call([exe, "unknown"], base, ok=False)
print(f"{checks} subprocess checks and all integration assertions passed")
