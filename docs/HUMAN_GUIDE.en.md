> English · [中文](HUMAN_GUIDE.md)

# HUMAN_GUIDE — Install · Configure · Tune · Troubleshoot (human edition)

This guide is for **human users**: installing the codebase-memory-mcp fork, the full three-layer configuration reference, tuning, and troubleshooting. The token-saving edition for AI agents is [AGENT_GUIDE.en.md](AGENT_GUIDE.en.md); the project overview and fork design trade-offs are in the [README](../README.md).

## Table of contents

- [Installation](#installation)
  - [Option A: Release binary (recommended)](#option-a-release-binary-recommended)
  - [Option B: Build from source](#option-b-build-from-source)
- [Quick start](#quick-start)
- [Full parameter reference](#full-parameter-reference)
  - [Three config layers and precedence](#three-config-layers-and-precedence)
  - [Full `config` key table](#full-config-key-table)
  - [Project-local policy file](#project-local-policy-file)
  - [Environment variables](#environment-variables)
  - [Tool profiles](#tool-profiles)
- [Tuning](#tuning)
- [Troubleshooting](#troubleshooting)
- [Testing and development](#testing-and-development)

---

## Installation

### Option A: Release binary (recommended)

Fork releases currently ship a **windows-amd64** single-file executable with no runtime dependencies:

```bash
# Direct download (prefix with a proxy, e.g. HTTPS_PROXY=... curl ..., if the CDN times out)
curl -L -o codebase-memory-mcp.exe https://github.com/daidaiJ/codebase-memory-mcp/releases/latest/download/codebase-memory-mcp-windows-amd64.exe
# Or via gh: gh release download --repo daidaiJ/codebase-memory-mcp
./codebase-memory-mcp.exe --version    # verify
```

Drop it into a PATH directory and call it as `codebase-memory-mcp` everywhere.

Other platforms: build from source (below), or use the [upstream build](https://github.com/DeusData/codebase-memory-mcp) — it works, but the fork defaults described here (minimal tool surface, `auto_watch=false`, memory cap, …) do not exist upstream.

### Option B: Build from source

On Windows use MSYS2 CLANG64 (MSVC is not supported); the full recipe is in [BUILD_WINDOWS.md](BUILD_WINDOWS.md). On POSIX simply:

```bash
make -f Makefile.cbm          # build all targets
make -f Makefile.cbm test     # full test suite (test-par for parallel)
```

Artifacts land in the repository root. Toolchain: C11 + tree-sitter (bundled grammars) + SQLite + mimalloc (all vendored, no external dependencies).

## Quick start

```bash
# 1. Index the repo (fast mode skips embeddings/similarity/git-history; fine for daily refresh)
codebase-memory-mcp cli index_repository --repo-path . --mode fast

# 2. Architecture overview (first stop in a large/unknown repo: fan-in hotspots, boundaries, layers, clusters)
codebase-memory-mcp cli get_architecture '{"project":"<PROJECT>","aspects":["hotspots","boundaries","layers","clusters"]}'

# 3. Whole-repo complexity ranking (review priority)
codebase-memory-mcp cli query_graph '{"project":"<PROJECT>","query":"MATCH (f:Function) WHERE f.complexity IS NOT NULL RETURN f.qualified_name, f.complexity ORDER BY f.complexity DESC LIMIT 10"}'

# 4. Commit blast radius (no ^ ~ suffixes; pass full SHAs)
PARENT=$(git rev-parse HEAD^)
codebase-memory-mcp cli detect_changes "{\"project\":\"<PROJECT>\",\"since\":\"$PARENT\",\"scope\":\"impact\"}"
```

- Project name derivation: path separators `/` → `-` (`C:\dev\web-app` → `C-dev-web-app`); when unsure run `cli list_projects`.
- The CLI keeps all 17 tools; only the MCP surface is trimmed. Each CLI call pays ~5s daemon cold start — batch your questions.
- Before trusting graph results at scale, run `cli check_index_coverage`: files with `parse_partial` status can be locally blind in the graph.

### MCP integration

```json
{
  "mcpServers": {
    "codebase-memory-mcp": {
      "command": "codebase-memory-mcp",
      "args": []
    }
  }
}
```

The default is the minimal surface (`search_graph` / `query_graph` / `get_architecture` / `get_graph_schema`). For everything: `"args": ["--tool-profile=all"]`.

## Full parameter reference

### Three config layers and precedence

| Layer | Location | Scope |
|------|------|--------|
| 1. Project-local | `<project>/.cbm/config.json` | That project only, **highest priority** |
| 2. Global store | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db` | All projects of the current user |
| 3. Built-in default | Compiled into the binary | Fallback |

### Full `config` key table

```bash
codebase-memory-mcp config list               # all keys + current value + default
codebase-memory-mcp config set <key> <value>  # write the global store (tool names are validated)
codebase-memory-mcp config reset <key>        # restore the default
```

| Key | Fork default | Meaning |
|----|----------|------|
| `tools_disabled` | *(empty)* | Comma-separated denylist. Listed tools vanish from MCP `tools/list` and `--help`; calling one by name exits non-zero on the CLI and returns an explicit MCP error (never silent). Effective on both MCP and CLI surfaces |
| `auto_watch` | **`false`** | Register the session's project with the background git watcher on connect (upstream defaults to true) |
| `auto_index` | `false` | Auto-index new projects when an MCP session starts; **fires on ALL-profile sessions only** — the minimal workflow refreshes out-of-band via `cli index_repository` |
| `auto_index_limit` | `50000` | Max file count allowed for automatic indexing |
| `watcher_enabled` | `true` | Master switch for the watcher subsystem, **read once at daemon start** — run `daemon stop` after changing it |
| `index_max_files` / `index_max_source_mb` | `off` | Per-run caps on file count / source size (apply to explicit and automatic indexing) |
| `ui_enabled` / `ui_port` | **`false`** / `9749` | Graph UI loopback HTTP service; the fork never auto-enables it |

`watcher_enabled` vs `auto_watch`: the former decides whether the watcher subsystem exists at all (read once at daemon start), the latter only whether a given session registers its project with an already-running watcher. When `watcher_enabled=false`, `auto_watch` has no effect.

### Project-local policy file

The path is fixed at `<project>/.cbm/config.json` — a JSON object whose keys mirror the `config` subcommand:

```json
{
  "tools_disabled": "search_graph,trace_path,get_code_snippet"
}
```

- Hand-written; there is no writer command (`config set` only manages the global store).
- Missing file → layer skipped; corrupt/unreadable → warn log and ignored, **stdout is never polluted** (MCP JSON-RPC and help output live on stdout).
- Resolved against the CLI's working directory (CLI) or the session root (MCP).

### Environment variables

| Variable | Default | Meaning |
|------|------|------|
| `CBM_LOG_LEVEL` | library `error` | `debug`/`info`/`warn`/`error`/`none` (or `0`-`4`). Frontends default to warn, the daemon to info; the fork tightens the in-library fallback to error so cold starts stop spraying warn noise to stderr |
| `CBM_MEM_BUDGET_MB` | RAM fraction, **capped at 2048** | An explicit value may exceed the cap (up to physical memory) |
| `CBM_DACL_HARDENING` | *(unset = skip)* | Windows only. The fork skips the untrusted-ACE walk of the cache directory by default (loose ancestor ACLs of shared tool directories no longer block startup); owner validation stays on in both modes. Set `=1` on multi-user/terminal-server hosts |
| `CBM_WATCH_COOLDOWN_S` | `30` | Cooldown seconds after a successful watcher index; re-read on every use, `0` disables. Commits are held too, restaged and fired once the window ends (nothing is lost) |
| `CBM_WATCHER_PRUNE_GRACE_S` | `600` | How long a missing watched root stays missing before its cached DB is pruned |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | Storage root for indexes, `_config.db`, logs, and UI config |
| `CBM_RUNTIME_DIR` | platform default | Parent directory for the daemon rendezvous; redirect here when the default ancestry fails the security checks |
| `CBM_ALLOWED_ROOT` | *(unset)* | Confine `index_repository` to this directory; set when driven by untrusted callers |
| `CBM_WORKERS` | auto-detected | Indexing worker count |

### Tool profiles

`--tool-profile` selects what the MCP surface advertises:

| Profile | Tools |
|---------|------|
| `minimal` (**fork default**) | `search_graph` / `query_graph` / `get_architecture` / `get_graph_schema` |
| `analysis` | Read-only inspection subset (includes `detect_changes` / `trace_path`) |
| `scout` | Fast positive-discovery subset |
| `all` | All 17 (upstream default behavior) |

`tools_disabled` composes with the profile: a tool must **both** pass the profile allowlist and not be denylisted to be visible or callable.

## Tuning

Look at data first: `cli index_status` is the observation entry point for every tuning decision.

| Lever | Advice |
|------|------|
| Memory pressure | The 2048 MiB default cap is already priced conservatively given the leak history; raise explicitly with `CBM_MEM_BUDGET_MB` only if it truly isn't enough |
| Watcher too noisy/frequent | Read `freshness` from `index_status` first; raise `CBM_WATCH_COOLDOWN_S` (e.g. 120), or fall back to explicit indexing with `config set auto_watch false` |
| 5s cold start annoying | Batch queries; or wire a SessionStart hook so the index is warm before the session starts |
| Oversized indexes | Set `index_max_files` / `index_max_source_mb`; exceeding either fails the whole request and preserves the previously serving DB |
| Disk grows forever | The fork ships retention sweeps (skip logs / temp files / FTS dead-row reclamation); the `maintenance` block of `index_status` gives counts and a rebuild hint when `fts_rows > 3x nodes_rows` — full re-index is the only reclamation path, you decide when |
| UI or not | Enable with `config set ui_enabled true` if you use it; otherwise ignore it — it won't start by itself |

## Troubleshooting

| Symptom / error | Nature | Action |
|----------|------|------|
| `secure daemon endpoint could not be created` | The rendezvous directory's ancestry failed the security checks (capability-SID ACE, loose `/tmp`, …) | Point `CBM_RUNTIME_DIR` at a directory you own; set the same value in the environment of every process that should share the daemon |
| CLI exits non-zero with `disabled by config` | Tool is denylisted or not in the current profile | Check with `config list`; `config reset tools_disabled` or `--tool-profile=all` on the MCP side |
| `query_graph` rejects an unknown property | The fork's directory validation intercepts before execution (upstream silently returned `total: 0`) | Follow the error's pointer and run `get_graph_schema`; the fork is helping you, not breaking |
| `detect_changes` rejects `direction`/`scope` | Teaching error (fork-added validation) | Use one of the legal values listed in the message |
| Trace shows `caller_resolution` with unresolved rows | Framework/DI dispatch is invisible to static resolution — an inherent graph boundary, not a fault | Read the accompanying note; cross-check with `include_evidence=true`; don't read 0 as "no callers" |
| Graph disagrees with the code | Stale index or failed parses | Re-index: `cli index_repository --repo-path . --mode fast`; check `parse_partial` via `check_index_coverage` first |
| Watcher is slow to index | Two-round confirmation + cooldown are working (dirty changes lag 5-60s) | By design; commits trigger in one round. Index manually when in a hurry |
| `watcher_enabled` change has no effect | The key is read once at daemon start | `daemon stop` so the next session starts a fresh daemon |
| All config commands fail, logs mention `config.local.corrupt` | Corrupt `.cbm/config.json` | Fix or delete the file (corruption is ignored, so total failure means something else — check `${CBM_CACHE_DIR}/logs/cbm-daemon.log` first) |

Overall rule: **explicit errors are the fork refusing ambiguous input — fix the parameter per the message. The genuinely dangerous thing is a silent empty result — on "empty but no error", check `freshness`/`coverage` first, then re-verify with grep, and only then trust the graph.**

## Testing and development

- Build and test recipes: [BUILD_WINDOWS.md](BUILD_WINDOWS.md) (Windows) / `make -f Makefile.cbm test-par` (POSIX)
- Tests asserting a changed default **must** be updated together (assert the new contract); per-file patch inventory and verification matrices are in [FORK_PATCHES.md](FORK_PATCHES.md)
- Agent collaboration conventions (layout, pitfall list, rebase checkpoints) live in [AGENTS.md](../AGENTS.md)
