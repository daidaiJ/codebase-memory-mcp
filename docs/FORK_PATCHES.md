# Fork patches

This branch carries the fork's divergences from upstream
(DeusData/codebase-memory-mcp). Each patch implements one or more fork issues
and follows one principle: **this is a single-user local tool that runs
alongside the agent's own grep/symbol tools — resident resources and
self-activating services are opt-in, policy refusals fail loud, and the tool
surface is what survives side-by-side comparison, not what exists.**

Patches target `feat/minimal-tool-surface`; rebase onto upstream `main` per
release and re-verify the touched call sites (file:line references below are
from the patch branch).

## 1. MCP default tool surface: minimal (fork issue #4)

The MCP server advertises three tools by default — `get_architecture`,
`query_graph`, `detect_changes` — the ones with no grep/codegraph equivalent
(architecture overview with fan-in hotspots and layering, whole-repo cognitive
complexity ranking, diff-driven impact radius). `--tool-profile=all` restores
the full registry; `analysis` / `scout` presets still work.

- `src/mcp/mcp.h` — `CBM_MCP_TOOL_PROFILE_MINIMAL` (wire value 3); parse doc.
- `src/mcp/mcp.c` — `minimal_tools[]` allowlist; profile name/parse (accepts
  `all`/`minimal`); `cbm_mcp_server_new` defaults to MINIMAL;
  `MCP_MINIMAL_SERVER_INSTRUCTIONS` in the initialize response.
- `src/main.c` — MCP client role default MINIMAL; usage text.
- `src/daemon/application.c` — context-header bound checks widened to MINIMAL
  (client `set_context` + daemon validation must agree, else sessions are
  rejected).

Deliberately unchanged: daemon-internal sessions default to ALL, so one-shot
CLI calls and hooks keep the full registry — the CLI is not the constrained
surface. This includes the supervised index worker: `<self> cli --index-worker
...` re-dispatches the parent's tool name through `cbm_mcp_server_new`, whose
fork default is MINIMAL, so the worker explicitly resets its profile to ALL
before dispatch (v0.8.1-fork.2; the first patch build rejected
`index_repository` there — the worker inherited the minimal allowlist its own
parent's tool is not on). Consequence worth knowing: `auto_index` fires only
on ALL-profile sessions (upstream gate kept), so an explicitly-enabled
`auto_index` pairs with `--tool-profile=all`; the minimal workflow refreshes
out-of-band (`cbm cli index_repository`).

## 2. `tools_disabled` denylist (fork issue #4)

`config set tools_disabled search_graph,trace_path,...` hides tools from MCP
`tools/list` and from all `--help` tool lists, and fails loud — non-zero exit
with an explicit "disabled by config" message — when called by name on either
surface. Composes with the profile: a tool must pass the profile allowlist
AND not be denylisted.

Project-local override (fork extension): a checkout can carry its own policy
in `<project>/.cbm/config.json` — a JSON object using the `config` keys, e.g.
`{"tools_disabled": "search_graph,trace_path"}`. Precedence: local file >
`_config.db` > built-in default. The MCP side resolves the local file against
the session root, the CLI side against its working directory. A corrupt local
file is warned about (`config.local.corrupt`) and ignored; stdout is never
polluted.

- `src/cli/cli.h` / `src/cli/cli.c` — `CBM_CONFIG_TOOLS_DISABLED` key;
  `cbm_config_tool_csv_contains` (pure parser, one parser for help + dispatch),
  `cbm_config_tool_disabled` (store read), `cbm_config_local_tool_disabled`
  (local file read), `cbm_config_tools_disabled_readonly`
  (no store creation — `--help` must not have db side effects; local file
  consulted first); `config set` validates names against the registry so
  typos fail at set time.
- `src/mcp/mcp.c` — `mcp_tool_visible` gates `tools/list` and pagination;
  `mcp_tool_disabled` layers local-file over store;
  `dispatch_tool` returns an isError result for denylisted names (covers
  daemon-side CLI execution too, since those sessions carry the config).
- `src/main.c` — `run_cli` rejects before daemon bootstrap (no 5s cold start
  to learn a tool is off); internal `--index-worker` processes are exempt;
  `cli --help` and top-level `--help` render via
  `cbm_mcp_tools_help_list_filtered`.

## 3. Default-value inversion (fork issue #3)

Resident resources and self-activation are opt-in:

- **`auto_watch=false`** (`src/cli/cli.c` key table, `src/daemon/application.c`,
  `src/mcp/mcp.c` — all three readers agree, including the NULL-config
  fallbacks). A resident file watcher is the largest session-long resource
  consumer and is redundant under an explicit indexing workflow. Opt back in
  with `config set auto_watch true`.
- **Log level default `error`** (`src/foundation/log.c`). INFO chattered
  warn-level allocator noise to CLI stderr on every cold start. `CBM_LOG_LEVEL`
  still overrides.
- **Memory budget capped at 2048 MiB** (`src/foundation/mem.c`,
  `cbm_mem_resolve_budget`). Upstream's bare RAM fraction handed 11.4 GB on a
  32 GB machine by default; the leak record (#832/#1084/#45/#1654) does not
  justify that. `CBM_MEM_BUDGET_MB` still raises above the cap explicitly;
  `tests/test_mem.c` asserts the new contract.
- **No UI auto-enable** (`src/ui/config.c`). A missing config file no longer
  turns the loopback HTTP listener on for binaries with embedded assets;
  `config set ui_enabled true` / `--ui=true` is the explicit path.

## 4. Windows DACL check: skipped by default (fork issue #2, amended)

The cache-directory untrusted-ACE walk behind the "cache-private / DACL entry
grants mutation rights to untrusted identity" startup refusal is **OFF by
default** in the fork (`src/daemon/ipc.c`, `win_dacl_hardening_enabled`).
`CBM_DACL_HARDENING=1` opts back in.

Rationale (amended from the original env-switch design): the refusal fires on
ancestor ACLs outside the user's control — a managed profile, or a shared
tools directory (`D:\tool-cli`) inheriting an Authenticated Users ACE — and a
hard startup failure is the wrong default for a single-user local tool whose
cache lives outside the user profile by design. Owner validation
(`win_file_owner_secure`) applies in BOTH modes, so the cache must still be
owned by the current user; only the "no other ACE grants mutation" walk is
dropped. Multi-user / terminal-server hosts should set `CBM_DACL_HARDENING=1`.
Env-only by design: the config store lives inside the cache directory and
cannot affect the run that creates it. Inspired by upstream PR #1649 (config
key + restore logic); this fork ships the inverted-default env form.

Earlier name `CBM_SKIP_DACL_HARDENING` (opt-out while default stayed strict)
existed only in the first patch build and was replaced by the inverted
`CBM_DACL_HARDENING` before any tagged release.

## Verification notes

- `tests/test_mem.c` updated to the capped-default semantics (incl. new
  `resolve_budget_default_cap_exemption`).
- Daemon wire protocol: the context-header profile byte is bounded by
  `CBM_MCP_TOOL_PROFILE_MINIMAL` on BOTH sides; mismatched builds reject the
  session rather than mis-read the value.
- Build: release artifacts via the fork's lean GitHub Actions workflow
  (windows-amd64), replacing `~/tools/codebase-memory-mcp/`.
