# Building and Testing on Windows (maintainer recipe)

The fork's CI (`.github/workflows/fork-win64.yml`) is the authoritative
definition of a working Windows build: **MSYS2 CLANG64 + Clang + make**.
This page reproduces that environment on a local machine so you can run the
full build and test suite that fork CI deliberately does not run.

## Toolchain: MSYS2 CLANG64, not MSVC

`Makefile.cbm` is a GCC/Clang build system (GNU make, POSIX-ish shell steps in
`scripts/build.sh`, `-Werror` flag sets GCC/Clang understand). **MSVC (`cl`)
cannot build this tree** — do not try to adapt it; use MSYS2 instead.

1. Install [MSYS2](https://www.msys2.org/).
2. In a **CLANG64** shell (launch `clang64.exe`, or `msys2_shell.cmd -clang64`),
   install the packages the CI workflow installs:

   ```bash
   pacman -S --needed \
     mingw-w64-clang-x86_64-clang \
     mingw-w64-clang-x86_64-zlib \
     make \
     python
   ```

3. Git must be on `PATH` (the daemon watcher and several test suites shell out
   to `git`); any Windows Git works.

## Build

Inside the CLANG64 shell, from the repository root:

```bash
# Release binary — same entry point CI uses
scripts/build.sh CC=clang CXX=clang++

# Or straight make (build/c/codebase-memory-mcp)
make -f Makefile.cbm CC=clang CXX=clang++
```

Smoke check — the workflow asserts the index round-trip, not just `--version`
(the v0.8.1-fork.1 regression was invisible to `--version`):

```bash
./build/c/codebase-memory-mcp.exe --version
SMOKE=$(mktemp -d) && cd "$SMOKE" && git init -q \
  && printf 'int main(void) { return 0; }\n' > main.c \
  && git add main.c \
  && git -c user.email=s@e.c -c user.name=s commit -qm init && cd -
./build/c/codebase-memory-mcp.exe cli index_repository --repo-path "$SMOKE" --mode fast
./build/c/codebase-memory-mcp.exe cli list_projects
```

## Full test suite (the acceptance gate)

Fork CI builds only; `make -f Makefile.cbm test` on a maintainer machine is
what actually gates. It builds `build/c/test-runner` and runs every suite
in-process:

```bash
make -f Makefile.cbm test CC=clang CXX=clang++
```

Useful variants:

| Command | What it does |
|---|---|
| `make -f Makefile.cbm test-par` | Every suite as its own process (union guard against `--list-suites`). |
| `make -f Makefile.cbm test-focused TEST_SUITES="watcher mcp"` | Only the named suites — e.g. after touching the watcher. |
| `CBM_SKIP_PERF=1` | Exclude throughput/scale suites from the run (what CI PR legs set). |

After any cursor/trace change, run the `mcp` suite in full (`test-focused
TEST_SUITES="mcp"`), not just the build — cursor contracts are asserted only
there. After any watcher change, run `watcher` plus `daemon*` suites.

## Notes and gotchas

- Run the whole toolchain from the CLANG64 shell. Mixing the MSYS2 `clang`
  with a cmd.exe `make` (e.g. the Chocolatey one) fails on flag and path
  differences.
- `scripts/build.sh` always clean-builds `build/c`; its compiler cache
  (`ccache`, content-verified) only affects speed. `CBM_NO_CCACHE=1` skips it.
- The test suite creates temp git repos under `/tmp` (MSYS2 maps this to the
  CLANG64 temp dir) and needs `git init/commit/worktree` to work non-
  interactively; the tests pass explicit `-c user.name/-c user.email` so no
  global git config is required.
- Releasing stays CI-only: push a `v*fork*` tag and let `fork-win64` build and
  attach `codebase-memory-mcp-windows-amd64.exe` to the release. Do not
  reintroduce upstream's release.yml.
