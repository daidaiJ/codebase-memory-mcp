# Configuration Reference

This page documents the configuration files that `codebase-memory-mcp` reads or writes today.

## At a Glance

| Purpose | Path | Format | Notes |
|---|---|---|---|
| Global custom extension mapping | `$XDG_CONFIG_HOME/codebase-memory-mcp/config.json` | JSON | Falls back to `~/.config/codebase-memory-mcp/config.json` when `XDG_CONFIG_HOME` is unset. |
| Per-project custom extension mapping | `{repo_root}/.codebase-memory.json` | JSON | Overrides conflicting global `extra_extensions` entries. |
| CLI-managed runtime settings | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db` | SQLite | Written by `codebase-memory-mcp config set/reset`. |
| UI settings | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/config.json` | JSON | Stores `ui_enabled` and `ui_port`. |
| Daemon operation log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/cbm-daemon.log` | Structured log | Durable daemon lifecycle, watcher/indexing, UI, resource, and error events. |
| Admission conflict log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/daemon-conflicts.ndjson` | NDJSON | Exact-build, ABI, and canonical-cache conflicts. |
| Activation log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/activation-events.ndjson` | NDJSON | Install/update/uninstall activation progress and outcomes. |

CBM resolves `CBM_CACHE_DIR` to a canonical per-account path before using any of these locations. The log directory and files are private to the account.

## 1. Custom File Extension Mapping

Two optional JSON files let you map additional file extensions to built-in languages.

### Global config

Default path:

```text
$XDG_CONFIG_HOME/codebase-memory-mcp/config.json
```

Fallback when `XDG_CONFIG_HOME` is unset:

```text
~/.config/codebase-memory-mcp/config.json
```

### Per-project config

Place this file in the repository root:

```text
.codebase-memory.json
```

### Format

```json
{
  "extra_extensions": {
    ".blade.php": "php",
    ".mjs": "javascript",
    ".twig": "html"
  }
}
```

Notes:

- Extension keys must include the leading dot.
- Language names are case-insensitive.
- Unknown language names are skipped.
- Missing files are ignored.
- If the same extension appears in both files, the per-project file wins.

## 2. CLI-Managed Runtime Settings

The `config` subcommand stores runtime settings in a small SQLite database:

```text
${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db
```

Inspect or change values with the CLI:

```bash
codebase-memory-mcp config list
codebase-memory-mcp config get auto_index
codebase-memory-mcp config set auto_index true
codebase-memory-mcp config set auto_index_limit 50000
codebase-memory-mcp config set watcher_enabled false
codebase-memory-mcp config reset auto_index
```

Current keys:

| Key | Default | Meaning |
|---|---|---|
| `auto_index` | `false` | Automatically index new projects when an MCP session starts. |
| `auto_index_limit` | `50000` | Maximum file count allowed for automatic indexing of a new project. |
| `auto_watch` | `false` | Register the session's project with the background git watcher on connect. Fork patch: **default `false`** — a resident file watcher is the largest session-long resource consumer and is redundant under an explicit indexing workflow (SessionStart hook / CI). Set `true` to restore upstream behavior (the watcher still runs for other projects). |
| `watcher_enabled` | `true` | Master switch for the background watcher subsystem. Set `false` to stop the watcher from starting at all — no poll thread and no project registration. Reindex manually with `index_repository` when disabled. |
| `tools_disabled` | *(empty)* | Fork patch: comma-separated denylist of tool names, e.g. `search_graph,trace_path,get_code_snippet`. Named tools are omitted from MCP `tools/list` and from `--help` tool lists, and fail loud (non-zero exit, explicit "disabled by config" message) when called by name — on both the MCP and the CLI surface. Names are validated at `config set` time against the tool registry. |

### Tool profiles

`--tool-profile` selects how many tools the MCP server advertises:

| Profile | Tools |
|---|---|
| `all` | The full registry. |
| `minimal` *(fork default)* | `get_architecture`, `query_graph`, `detect_changes` only — the three tools that survive side-by-side comparison with a grep + symbol-tool baseline (architecture overview, whole-repo complexity ranking, diff-driven impact radius). Everything else duplicates what local tools already do, with a cold-start penalty. |
| `analysis` | Read-only inspection subset. |
| `scout` | Fast positive-discovery subset. |

`tools_disabled` composes with the profile: a tool must pass the profile's
allowlist *and* not be denylisted to be visible or callable. The default
changed in the fork — upstream defaulted to `all`; pass `--tool-profile=all`
explicitly to restore it.

### Project-local config file (fork)

A checkout can carry its own policy in `.cbm/config.json` (relative to the
CLI's working directory, or the MCP session root):

```json
{
  "tools_disabled": "search_graph,trace_path,get_code_snippet"
}
```

- Keys mirror the `config` subcommand; precedence is **local file >
  `_config.db` > built-in default**.
- Currently consumed for `tools_disabled` on both the MCP and CLI surfaces.
- A missing file is a no-op. A file that exists but is unreadable or invalid
  JSON is logged (`config.local.corrupt` / `config.local.unreadable`) and
  ignored — stdout (MCP JSON-RPC, help output) is never polluted.

> **`watcher_enabled` vs `auto_watch`.** `watcher_enabled` controls whether the
> watcher *subsystem* starts at all (the background poll thread). `auto_watch` is
> narrower: it only controls whether a connecting session registers *its own*
> project with an already-running watcher. When `watcher_enabled=false`,
> `auto_watch` has no effect — there is no watcher to register with.
>
> They also differ in **when they are read**, which matters because the watcher
> lives in the background daemon, not in your MCP client:
>
> - `auto_watch` is consulted each time a session would register its project, so
>   a change applies to sessions that connect afterwards.
> - `watcher_enabled` is read **once, when the daemon starts**, because it decides
>   whether the watcher is built at all. The daemon is long-lived and outlives
>   individual MCP sessions, so **reconnecting your client is not enough** — retire
>   the daemon so the next one picks the new value up:
>
> ```bash
> codebase-memory-mcp config set watcher_enabled false
> codebase-memory-mcp daemon stop     # next session starts a daemon without the watcher
> codebase-memory-mcp daemon status   # confirm
> ```
>
> Disabling the watcher does not disable anything else: the daemon still starts,
> `auto_index` still runs, and `index_repository` stays available for manual
> reindexing.

## 3. UI Settings

The optional built-in graph UI stores its settings in:

```text
${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/config.json
```

Current format:

```json
{
  "ui_enabled": false,
  "ui_port": 9749
}
```

Notes:

- Fork patch: the UI no longer auto-enables on first run. A UI-enabled binary used to turn the loopback HTTP listener on whenever no UI config file existed yet; now the UI stays off until explicitly enabled (`config set ui_enabled true` or `--ui=true`). Missing or invalid assets leave the MCP/daemon service available and keep the UI disabled.
- `CBM_CACHE_DIR` changes both the UI config location and the runtime settings database location.
- CBM resolves `CBM_CACHE_DIR` to one canonical per-account cache root. A process configured with a different root fails while any CBM session or command is active; close them before switching roots.

## 4. Environment Variables

These environment variables affect runtime behavior:

| Variable | Default | Description |
|---|---|---|
| `CBM_ALLOWED_ROOT` | *(unset)* | Confine `index_repository` to paths within this directory. When set, a `repo_path` that resolves (after symlink / `..` resolution) outside this root is refused, and the same check now applies to the graph UI's `POST /api/index` route rather than only to the MCP tool. Unset imposes no *containment* restriction — but see the always-on limits below, which apply whether or not this is set. Useful when the server may be driven by an untrusted caller, e.g. agentic or multi-tenant deployments. |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | Override the cache directory used for indexes, `_config.db`, and UI `config.json`. |
| `CBM_DIAGNOSTICS` | `false` | Enable periodic `snapshot.json` and retained `trajectory.ndjson` below a fresh owner-private directory in the system temp directory. The daemon records the randomized paths in the `diagnostics.start` discovery record (a single JSON line) in `${CBM_CACHE_DIR}/logs/cbm-daemon.log`; that one record is emitted even when `CBM_LOG_LEVEL` suppresses ordinary logging, so the paths always remain discoverable. |
| `CBM_DOWNLOAD_URL` | GitHub releases | Override the update download URL. |
| `CBM_LOG_LEVEL` | `error` | Set the log level to `debug`, `info`, `warn`, `error`, or `none` (or `0`-`4`). Fork patch: default changed from `info` to `error` — warn-level allocator chatter hit CLI stderr on every cold start of a single-user local tool. Thin-frontend messages use that session's stderr; detached daemon events use `${CBM_CACHE_DIR}/logs/cbm-daemon.log`. |
| `CBM_RUNTIME_DIR` | `%LOCALAPPDATA%` (Windows), `/private/tmp` (macOS), `/tmp` (other) | Parent directory for the daemon/CLI rendezvous directory, which CBM creates inside it as `cbm-daemon-<uid>` (`cbm-daemon-<key>` on Windows). Set it when the default ancestry cannot pass the private-directory check — see below. `CBM_CACHE_DIR` does **not** move the rendezvous. |
| `CBM_SKIP_DACL_HARDENING` | *(unset)* | Windows only, fork patch: set to `1` to skip the cache-directory untrusted-ACE walk (the "cache-private / DACL entry grants mutation rights to untrusted identity" startup refusal) on hosts where an ancestor ACL outside your control cannot be fixed. Owner validation stays active; this is a deliberate fail-open the operator opts into. Env-only by design: the config store lives inside the cache directory, so a config key cannot affect the first run that creates it. Not recommended on multi-user or terminal-server hosts. |
| `CBM_WORKERS` | auto-detected | Override the indexing worker count. |

### Relocating the daemon rendezvous directory

Before it is used, the rendezvous directory and every ancestor of it are checked:
each ancestor must be owned by you or by root, must not be world-writable (unless
it is the standard root-owned sticky directory such as `/tmp`), and must carry no
allow-ACL — on Windows, no ACE granting mutation rights to another identity. The
rendezvous directory itself is then forced to owner-only (`0700`, no extended ACL
/ an owner-only DACL).

That ancestry is not always acceptable in the default location. A Windows profile
that has acquired a capability-SID ACE with `WRITE_DAC` / `WRITE_OWNER` / `DELETE`
on `%LOCALAPPDATA%` — something an installed packaged app can add — fails the walk,
and so can an unusual `/tmp` or home directory on POSIX. When that happens *every*
command fails, `config list` included, so the settings surface cannot be reached
either:

```text
codebase-memory-mcp: secure daemon endpoint could not be created
```

`CBM_RUNTIME_DIR` points the rendezvous at an ancestry you choose:

```bash
export CBM_RUNTIME_DIR="$HOME/cbm-runtime"   # any directory you own
```

```powershell
$env:CBM_RUNTIME_DIR = "D:\cbm-runtime"
```

The check is not relaxed for the directory you name: it goes through exactly the
same validation as the default, and a value that fails it is refused rather than
silently ignored. Because the rendezvous is how sessions find each other, every
process that should share one daemon must see the same value — set it in the
environment of your MCP client and your shell alike, or a CLI invocation without
it will coordinate through the default location instead.

Environment used by daemon-owned components—such as diagnostics, daemon logging, and process-wide indexing resource limits—is captured from the first daemon-backed session that starts the daemon. Later sessions join the existing process and cannot replace those values. To change them, close every daemon-backed session, update the relevant agent configurations consistently, and restart a session. `CBM_ALLOWED_ROOT` remains session-specific, a conflicting `CBM_CACHE_DIR` is rejected, and one-shot CLI commands use their own current environment without starting the daemon.


### Roots that are always refused

Independently of `CBM_ALLOWED_ROOT`, some directories are refused as an indexing
root because they are too broad or too sensitive to index as a unit:

- a filesystem root, a Windows drive root, or a UNC share root;
- a top-level system tree — `/etc`, `/var`, `/usr`, `/home`, `/Users`, and on
  Windows `C:\Windows`, `C:\Users`, `C:\ProgramData`, `C:\Program Files`;
- your home directory itself (directories *below* it are fine);
- a credential directory at any depth — `.ssh`, `.aws`, `.gnupg`, `.kube`,
  `.docker`, `.netrc`, `.git-credentials`, `.password-store`, macOS `Keychains`.

Two limits are worth stating plainly. This constrains *scope*, not
*sensitivity*: inside a root that is allowed, every file the process can read may
be indexed and later returned. And the credential list is a denylist, so it
raises the cost of a mistake rather than closing the class — a directory it does
not name is permitted.

## 5. Agent and Editor Integration Files

The `install` command can also write MCP entries and instruction blocks into agent/editor config files such as Claude Code, Codex, Gemini, VS Code, Cursor, Zed, and others.

Those target paths vary by tool and platform, so the easiest way to inspect the exact files for your machine is:

```bash
codebase-memory-mcp install --dry-run
```

That prints the specific config files the installer would modify without writing anything.
