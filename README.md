# kukri

Keep personal source comments in your working files while Git stages a version
without them. Kukri runs locally and never rewrites your source files to stage them.

**Notes are plain text, not secrets.** Do not store credentials in them. Git cannot
restore these notes after a reset, restore, checkout, deletion, or fresh clone.

## Build and install

Requires CMake 3.20+, a C++20 compiler, and Git 2.31+ on PATH. Tests additionally
need Python 3.9+; optional Node.js enables JavaScript execution comparisons.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

MSVC produces `build/Release/kukri.exe`. Single-configuration generators normally
produce `build/kukri` (or `.exe`); configure those with `-DCMAKE_BUILD_TYPE=Release`.
Only Windows/MSVC has been validated locally; other platforms need native testing.
The default MSVC build links the Visual C++ runtime statically for portable
Windows packages. Set `-DKUKRI_STATIC_MSVC_RUNTIME=OFF` to use the dynamic runtime.

Windows per-user install, in PowerShell:

```powershell
$prefix = Join-Path $env:LOCALAPPDATA 'Programs\kukri'
cmake --install build --config Release --prefix "$prefix"
```

Add `$prefix\bin` to your user PATH, or invoke the executable by its full path.
In Git Bash, Windows paths look like `/c/Tools/kukri.exe`, not `C:\Tools\kukri.exe`.

POSIX per-user install:

```sh
cmake --install build --config Release --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is on PATH. Manual system-wide POSIX installs conventionally
use `/usr/local`; distribution packages use their own prefix, commonly `/usr`.
CMake installs the executable to `<prefix>/bin` and README/LICENSE to
`<prefix>/share/doc/kukri`. Packagers can override GNUInstallDirs destinations and
use POSIX `DESTDIR` staging. Installation does not edit PATH or enable repositories.
CI produces tested downloadable build artifacts; it does not publish releases.
There are no package-manager listings yet.

## Use

From the repository where you want private comments:

```sh
kukri enable
kukri doctor
# Edit your source files, then:
git add example.cpp
kukri verify
git diff --cached
git show :example.cpp
```

C, C++, Java, Go, JavaScript, and TypeScript:

```cpp
//k A personal note; omitted from the staged file.
/*k A private block, ending at the next k*/
const char* text = "//k This is literal text and stays.";
```

Python:

```python
value = 1  #k A personal note; omitted from the staged file.
text = "#k Literal text stays."
```

Markers are case-sensitive prefixes: `//keep` and `#keep` are private too.
`// k`, `# k`, `//K`, `#K`, and `///k` are ordinary comments. Private blocks do
not nest; Python has no private block syntax. Newline bytes are preserved, and
inline blocks may leave a separating space to prevent source tokens joining.

Enable changes only local Git config and the managed section of `info/attributes`.
It does not change tracked `.gitattributes`, install hooks, or stage anything.
Existing index entries are not automatically cleaned. When ready, reprocess them:

```sh
git add --renormalize .
git diff --cached
kukri verify
```

Renormalization also stages other working changes; review the diff before committing.

| Command | Purpose |
| --- | --- |
| `enable` | Configure the required local filter and supported extensions |
| `disable` | Remove managed local integration without changing source/index contents |
| `status` | Show configuration |
| `doctor` | Check configuration, tracked source filter assignments, and filter execution |
| `verify` | Scan supported regular-file index blobs, including conflict stages |
| `clean` | Read stdin to EOF and write sanitized bytes to stdout |
| `help`, `--help`, `-h` | Show usage |
| `version`, `--version` | Show version |

For direct pipelines, use `kukri clean --path example.py < example.py` or
`kukri clean --language python < example.py`. Bare clean defaults to C++ and
waits for input; Git normally invokes it for you. Language names are `c`, `cpp`,
`java`, `go`, `python`, `javascript`, and `typescript`. Errors go to stderr.
Unknown extensions supplied with `--path` fail rather than guessing.

Commands return 0 on success and 1 on errors. Disabled status is not an error.
Verify reports index leaks or scan failures; configuration warnings alone do not
make it fail. Doctor fails on unhealthy configuration or uncovered tracked source
paths. Neither command audits untracked files or guarantees complete language parsing.

## Supported files and limits

| Language | Extensions |
| --- | --- |
| C | `.c` |
| C++ (also used for shared C headers) | `.h .cc .cpp .cxx .hh .hpp .hxx` |
| Java | `.java` |
| Go | `.go` |
| Python | `.py .pyi` |
| JavaScript | `.js .mjs .cjs` |
| TypeScript | `.ts .mts .cts` |

Extensions are case-sensitive. JSX/TSX, Rust, C#, Objective-C, and other unlisted
languages are not filtered. Scanners handle common literals and interpolations,
but are not full language parsers. The following cases fail before emitting output:

- Unterminated literals, block comments, and interpolations.
- C/C++ trigraph backslashes and line splices inside C++ raw strings.
- Malformed Java Unicode escapes.
- Python `t` strings and interpolation nesting beyond 128 levels.
- Ambiguous JS/TS slash contexts, including slash after `}`, `++`, `--`, `await`,
  `yield`, or `of`; division-like tokens followed by a newline and slash; and
  TypeScript slash after `!` or `>`. Parenthesizing can disambiguate some cases.
- JS/TS `for await`, nested regex character classes, escaped identifiers, and
  legacy HTML-style comments.

C/C++ macros are not expanded; inactive preprocessor branches are still scanned.
Python comments in f-string expressions require Python 3.12+. The cleaner buffers
input and output in memory. New or unsupported language constructs may need further
scanner work; inspect staged output when adopting kukri for a new codebase.

## Updates and removal

Install updates at the same path. For a stable package-manager symlink, run the
installed binary with its stable absolute path (example):

```sh
/opt/homebrew/bin/kukri enable --executable /opt/homebrew/bin/kukri
```

The path must refer to the running executable. Symlinks and hard links are accepted
and stored without resolving them. Later enable calls retain a working configured
link. Without a selected or previously configured link, OS executable discovery
can select a version-specific path.

After updating from the old generic scanner, run `kukri enable`, `kukri doctor`,
and `kukri verify` in each repository. This migrates the command and extension
rules; it does not rewrite the index. Removed-language files are now unfiltered:
move their private notes elsewhere before staging them.

Run `kukri disable` in affected repositories before removing the executable.
Missing required filters intentionally block staging and may block checkout.
Linked worktrees share local config and common attributes, so enable/disable affects
them together. Avoid concurrent configuration edits. Malformed managed attribute
blocks require manual repair; empty attribute files are left in place on disable.

## Safety

The filter strips recognized tokens during normal Git staging. Verify is an
additional index check, not protection for commits already made. Disabling or
bypassing the filter, unsupported syntax, manual file copies, or clients that write
raw index blobs can leak notes. Backups, editors, filesystem sync, local users,
and malware can also read or copy them.

`git restore`, `git checkout --`, and `git reset --hard` can erase local notes.
Kukri has no note storage or recovery. Its internal identity smudge command copies
bytes unchanged for checkout compatibility; it cannot reconstruct notes.

MIT licensed. See [LICENSE](LICENSE).

## CI and preview packages

The GitHub Actions workflow runs on pushes, pull requests, and manual dispatch.
It targets Windows x64 (MSVC on Windows Server 2022), Ubuntu 24.04 x64 (GCC and
Clang), and macOS 15 ARM64. These are validation targets, not a claim that remote
runs have already passed or that every newer OS is supported. Windows 11 desktop
installation still needs a manual smoke test; Linux packages do not promise
compatibility with older glibc/libstdc++ versions or every distribution. macOS
archives are unsigned and not notarized, so they remain developer previews.

CI requires Python, Git, and Node, runs all four CTest suites, then tests extracted
archives using the real Git integration suite. Ubuntu/GCC additionally builds,
installs, tests, and removes a Debian package. Existing integration tests simulate
stable-path upgrades; a genuine old-release-to-new-release package upgrade test
will be added once a previous published package exists.

Successful package jobs upload artifacts under **Actions > workflow run > Artifacts**:
Windows ZIP, Linux/macOS tarballs, Ubuntu `.deb`, and SHA-256 checksums. Artifacts
expire after 14 days and are not permanent release URLs. The workflow has read-only
repository permissions and does not publish releases, push commits, or modify
package-manager repositories.

To build and inspect an archive locally after building and testing:

```sh
cd build
cpack -C Release
cd ..
python tests/package_smoke.py build/packages
python tests/package_smoke.py build/packages --checksums-only
```

On Ubuntu with `dpkg-dev` installed, use `cpack -C Release -G DEB` from the build
directory. Dependency discovery adds the required runtime libraries; Git is an
explicit dependency. Archive users must install Git separately. Package generation
uses CMake installation rules and ships only the executable, README, and LICENSE.
The ignored learning docs, tests, and build files are excluded. Source archive
generation is disabled pending a separately reviewed source allowlist.

`project(... VERSION ...)` in CMakeLists.txt is the version authority; both the
executable header and CPack metadata are generated from it. CI-style dependency
checks can be enabled locally with `-DKUKRI_REQUIRE_TEST_DEPENDENCIES=ON`.
