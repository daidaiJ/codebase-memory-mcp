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

## 1. MCP default tool surface: minimal (fork issue #4; reshuffled by fork issue #5)

The MCP server advertises three tools by default — `search_graph`,
`query_graph`, `get_architecture`. Issue #4's original cut kept
`detect_changes` (diff-driven impact radius) in place of `search_graph`; the
2026-09 trust repair (fork issue #5) swapped them: `search_graph` is
symbol-level exact discovery and the second-most-used tool upstream (~210
issue mentions), while `detect_changes` is the least trustworthy tool on the
surface (upstream #2128 "dead verdict" family — see §"detect_changes honesty
note") and stays on analysis/all only until its credibility is repaired.
`get_architecture` / `query_graph` keep their #4 rationale: no
grep/codegraph equivalent. `--tool-profile=all` restores the full registry;
`analysis` / `scout` presets still work.

- `src/mcp/mcp.h` — `CBM_MCP_TOOL_PROFILE_MINIMAL` (wire value 3); parse doc.
- `src/mcp/mcp.c` — `minimal_tools[]` allowlist; profile name/parse (accepts
  `all`/`minimal`); `cbm_mcp_server_new` defaults to MINIMAL;
  `MCP_MINIMAL_SERVER_INSTRUCTIONS` in the initialize response.
- `src/main.c` — MCP client role default MINIMAL; usage text.
- `src/daemon/application.c` — context-header bound checks widened to MINIMAL
  (client `set_context` + daemon validation must agree, else sessions are
  rejected). Wire values and the profile enum were NOT touched by #5 — only
  the membership table changed.

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

## 5. trace_path caller-resolution health summary (fork issue #6)

Upstream pain point: `trace_path(direction=inbound)` returning
`callers_total: 0` is indistinguishable from "the graph has no callers"
from "every caller edge failed to resolve" — framework/DI dispatch is
invisible to static resolution (NestJS-style containers resolve ~70% of call
sites, upstream #514), and a field agent reads the zero as a negative result.
The fix puts the distinction IN the response (fork issue #6): every inbound
trace now carries

- `caller_resolution` — tree: `"2/3 resolved, 1 unresolved"`;
  json: `{"resolved":2,"total":3,"unresolved":1}`. Counted over the exact
  rows `callers_total` counts (same test-file filter), classified per row by
  the existing canonical-predecessor machinery (`trace_predecessor_edge` +
  `trace_edge_evidence` → closed class vocabulary): non-`unresolved` class =
  resolved; `unresolved` class or a CALLS edge without strategy = unresolved.
- `caller_resolution_note` — fixed teaching text, emitted when `total == 0`
  or `unresolved/total >= 0.5` (named constant
  `TRACE_CALLER_UNRESOLVED_NOTE_RATIO`; the 0.5 threshold follows from the
  ~70% container-dispatch figure — past half, the list is more noise than
  signal). Same constant on both emitters.

- `src/mcp/mcp.c` — stats helper + both emitters (`trace_emit_caller_resolution_*`);
  the inbound BFS now collects edge properties even when `include_evidence`
  is off (`in_edge_data_limit`), because without them every row would
  misclassify as unresolved. Outbound leg unchanged.
- `tests/test_mcp.c` — `tool_trace_path_caller_resolution_summary`: counts on
  tree+json, note absent at 1/3, note fires at exactly 1/2 and at total==0,
  `include_evidence=true` keeps the summary once and consistent with the
  evidence columns, raw strategy names never leak.

Deliberately asymmetric: `direction=outbound` gets NO symmetric
`callee_resolution` — the motivating ambiguity is entirely on the caller side
(callees of a known function resolve by reading its body; callers may be
container-dispatched). Revisit only with field evidence.

Known skew, by design: when the inbound edge collection saturates
(`edge_data_saturated: true`, 5000-edge ceiling) some predecessor edges are
absent and their rows classify as unresolved — the response already flags the
saturation loudly; the summary counts the evidence actually available.

## 6. detect_changes honesty note (fork issue #7)

detect_changes maps a git diff onto the CALLS graph, whose edges are
resolution heuristics — framework/DI dispatch is invisible, so an empty
impacted set reads as "no impact" to a trusting agent (the upstream #2128
"dead verdict" family that got detect_changes demoted from the minimal face,
§1). Both emitters now carry the same static annotation at the response root:

- `graph_support: "heuristic-calls"` — names what the impact walk actually is.
- `resolution_caveat` — fixed text: "CALLS edges are resolution heuristics;
  0 impacted does not mean no impact — framework/DI dispatch may be invisible.
  Cross-check with trace_path(direction=..., include_evidence=true)". The
  cross-check now has something to find: trace_path carries the per-row
  `caller_resolution` summary (§5).

Deliberately a constant, not a statistic (downgraded from the backlog B2
per-symbol evidence plan): impact traverses `cbm_store_bfs_multi`, which
returns no edges, so per-symbol resolution evidence would need a store API
extension — cost out of proportion for a single-user tool. The annotation is
honest about the limit instead of faking precision.

- `src/mcp/mcp.c` — `DETECT_GRAPH_SUPPORT` / `DETECT_RESOLUTION_CAVEAT`
  shared constants; emitted at the root of the tree and legacy-json emitters
  (the output-budget floor reuses the same emitters inline, so every path
  carries the note).
- `tests/test_mcp.c` — `tool_detect_changes_honesty_note_in_both_formats`:
  fixture drives the misleading case (changed file, zero seeded symbols →
  `impacted` empty) and pins both fields in both formats.
- `AGENTS.md` known-pitfalls table updated: the "silent graph under-report"
  row now points at the in-band defenses; the "invalid direction" row is
  marked fixed (upstream commit `60390aff`, in the fork baseline, pinned by
  the `invalid_rejected` assertion in test_mcp.c).

## 7. Cache retention sweeps + index_status maintenance accounting (fork issue #8)

The cache and store previously only ever grew: one degraded-index skip log
per run (`<cache>/logs/<project>-<epoch>.log`, unbounded), supervisor temp
files (`.worker-*-XXXXXX`) and search scratch dirs (`<tmp>/cbm-search-*`)
left behind by crashes, staging DBs from killed index runs (the per-run sweep
only fired on the next run of the SAME final path), and two retention leaks
inside the store — contentless-FTS dead rows (delta deletes touch only
`nodes`) and `lsp_surface` rows for files that no longer exist (cleared only
by a full index). Phase 2's "storage retention" ticket fixes all three zones
with pure subtraction: no config keys, constants only (solve "unbounded"
first; parameterize on real usage feedback).

- `src/foundation/cache_sweep.c/.h` (new) — `cbm_cache_sweep_skip_logs`
  (per project keep 10, drop >30 days), `cbm_cache_sweep_worker_temp` and
  `cbm_cache_sweep_search_scratch` (remove older than 1h — a live run's
  files are young by construction), `cbm_cache_sweep_run` = all three.
- `src/pipeline/pipeline.c` + `pipeline.h` — the per-run orphan-stage sweep
  generalized to `cbm_pipeline_sweep_orphan_stages_dir(dir)`: same flock-dead
  judgment, now callable against the whole cache directory.
- Hooks: daemon bootstrap (`cbm_daemon_application_new` — cache sweep + full
  stage sweep), first supervised worker spawn (CLI runs that never open a
  daemon session still sweep), and after each skip-log write (prune while the
  directory is hot; `CBM_INDEX_LOG` overrides are never touched).
- `src/store/store.c/.h` — `cbm_store_maintenance_stats`: exact whole-store
  counts of `nodes` / `nodes_fts` / `lsp_surface` plus a bounded orphan probe
  (lsp rows whose file has no node; `LIMIT`-capped, reported as `gte` when
  the cap is hit). FAIL-LOUD per upstream #2012: any failing count is
  `CBM_STORE_ERR`, never a silent 0 — index_status renders
  `maintenance_status: "unavailable"` instead of faking a clean store.
- `src/mcp/mcp.c` — index_status gains a `maintenance` section: the counts,
  `lsp_surface_orphans` + `_relation`, and — when `fts_rows > 3x nodes_rows`
  (named constant) — `rebuild_recommended: true` with a one-line note that a
  full re-index is the contentless FTS index's only reclamation path. No
  auto-rebuild, no auto_vacuum: the numbers surface, the maintainer decides.
- Tests (`tests/test_mcp.c`):
  `cache_sweep_prunes_skip_logs_worker_temp_and_scratch` (residue + young
  files → sweep → old removed, young kept; non-skip logs untouched) and
  `index_status_maintenance_accounts_fts_and_lsp_orphans` (reproduces the
  retention mechanism for real — delete-by-file leaves the FTS row, churn
  accumulates it — and pins counts, orphan probe and rebuild recommendation).

## 8. trace_path depth frontier + deepen cursor (fork issue #9)

The recursive CTE enumerates the full depth-bounded reachable set regardless
of LIMIT, so the only way to save a deep traversal is not to run it — and the
only honest way to do THAT is to tell the caller a deeper tier exists and let
them pull it. trace_path responses now distinguish "the frontier was fully
expanded" from "there is more beyond the depth cap":

- `depth_frontier: "expanded" | "limited"` — when neither leg saturated the
  5000-node engine ceiling, a bounded EXISTS probe (`cbm_store_frontier_has_more`,
  temp-table id sets, LIMIT 1) checks whether any max-hop node has a
  traversal edge to a node outside the materialized set. A failed probe
  omits the field — "probe unavailable" must never read as "expanded".
- `deepen_cursor` — a c2 token, minted only when the frontier is limited,
  the engine did not saturate, and the depth ceiling leaves room, with the
  teaching note (`depth_frontier_note`) telling the agent to pass it back as
  `cursor`. Depth pulls are agent-driven PULLS — no background crawling, no
  query deadline; each pull gets its own fresh page/row budget.

Cursor generations (c1 unchanged and still accepted):

- c1: `c1.<leg>.<generation>.<qhash>.<hop>.<id>` — depth part of the identity
  hash, exactly as before.
- c2: `c2.<leg>.<depth>.<generation>.<qhash>.<hop>.<id>` — depth moved OUT of
  the hash into the token, so pulling tier N+1 does not invalidate the
  identity. Deeper-only is enforced by CONSTRUCTION: the server mints
  depth+1 and, on a c2 replay, ignores the depth argument — a narrowed depth
  can never sneak past the depthless hash. Every other param still
  invalidates. hop=0/id=0 is a fresh anchor (previous tier had no rows;
  decode accepts it, the handler resumes from the top — never produced by a
  plain mint today, kept for the 0-row chain case). Exactly-once carries
  over: the (hop,id) watermark guarantees tier0 rows are never emitted twice.
  Generation staleness (`stale_cursor`) unchanged for both generations.
- Deepened pulls keep minting c2 pagination cursors; fresh and c1 pulls
  keep minting c1.

- `src/store/store.c/.h` — `cbm_store_frontier_has_more` (bounded EXISTS,
  temp tables `probe_frontier`/`probe_known`).
- `src/mcp/mcp.c` — `trace_params_hash_ex` (depth-optional), c2 encode/decode,
  frontier probe, deepen mint, both emitters.
- Tests: `tool_trace_path_deepen_cursor_pulls_next_tier` (4-tier fixture:
  tier0 limited, deepened pull honors the cursor's depth over a narrowed
  depth argument, tier0 rows not repeated, expanded at the end) and
  `tool_trace_path_cursor_generation_matrix` (c1 depth-change rejects; c2
  depth-change accepted-and-ignored; c2 other-param change rejects; tampered
  c2 rejects).

Known skew: with `include_tests=false`, nodes hidden by the test filter are
absent from the probe's known set — a chain THROUGH a test file can report
`limited` and mint a cursor whose pull yields no new visible rows. The chain
still terminates at the depth ceiling; the totals and watermark stay honest.

## 9. Watcher thundering-herd guard + freshness (fork issue #10)

Watcher-triggered deltas cost close to a full incremental index (upstream
#867), so "react to the first sighting" was the wrong price model. Three
changes, all per project, no new config keys (constants + env, matching the
watcher's existing style):

1. **Two-round confirmation.** A dirty-state change is indexed only when the
   next poll observes the SAME whole-tree signature again; a different
   signature mid-confirmation restarts the window; a tree returning to the
   committed baseline discards it. The decision is a pure exported function
   (`cbm_watcher_decide_poll`) so the state machine is unit-tested directly.
   **Priced honestly: a dirty change reaches the index one poll cycle late
   (5–60s).**
2. **Post-success cooldown.** A successful index opens a per-project window
   (default 30s, `CBM_WATCH_COOLDOWN_S`, read on every use — `0` disables
   live, including deadlines an earlier setting created). During the window
   NOTHING triggers — HEAD moves included; baselines stay uncommitted so the
   change fires once the window ends (at-least-once preserved, #937).
3. **HEAD-move exception.** Commits are explicit actions: they index in a
   single round, no confirmation.

**Freshness exposure** (absorbs backlog B3 in its downgraded form — query
tools already carry the honesty fields from #6/#7, so this is index_status's
job alone): `cbm_watcher_get_freshness` exposes per-project state and
`index_status` renders it as

```json
"freshness": {"indexed_at": "...", "workspace_dirty": bool,
              "index_pending": bool, "stale": bool}
```

Anchors are aggregate-scope only: `indexed_at` is the project row's generation
timestamp, staleness compares the watcher's whole-tree signatures — never
per-file mtime (upstream #1714) and never `Branch.head_sha` (upstream #1213).
The block is OMITTED when there is no signal (no watcher, `auto_watch=off`
→ project unwatched, non-git root, no completed observation): absent means
"no data", never "fresh".

- `src/watcher/watcher.h` — decision enum + pure `cbm_watcher_decide_poll`,
  `cbm_watcher_freshness_t` + `cbm_watcher_get_freshness`.
- `src/watcher/watcher.c` — `confirm_*`/`pending_since_ms`/`cooldown_until_ms`/
  `last_index_success_ms`/`observed_once` per-project state; `watch_cooldown_s`
  env reader; `check_changes` reduced to observation (decision moved out);
  `poll_project` applies the decision and starts the cooldown on success.
- `src/mcp/mcp.c` — `handle_index_status` freshness block.
- Tests: `tests/test_watcher.c` — the seven `watch_decide_*` pure-matrix tests
  (two equal rounds trigger; mid-window change restarts; revert-to-baseline
  clears; cooldown holds even a stable signature and restages new ones; HEAD
  triggers in one round; cooldown out-ranks the HEAD exception), the
  cooldown integration test (`CBM_WATCH_COOLDOWN_S` blocks then disables
  live), the freshness accessor test, and every dirty-path flow test rewritten
  to the two-round contract. `tests/test_mcp.c` —
  `tool_index_status_freshness_fresh_pending_reindexed_and_absent` (fresh /
  pending / re-indexed / unwatched-absent matrix). The suite disables the
  cooldown suite-wide (`CBM_WATCH_COOLDOWN_S=0`); only the dedicated cooldown
  test re-enables it.

## Verification notes

- `tests/test_mem.c` updated to the capped-default semantics (incl. new
  `resolve_budget_default_cap_exemption`).
- Daemon wire protocol: the context-header profile byte is bounded by
  `CBM_MCP_TOOL_PROFILE_MINIMAL` on BOTH sides; mismatched builds reject the
  session rather than mis-read the value.
- Build: release artifacts via the fork's lean GitHub Actions workflow
  (windows-amd64), replacing `~/tools/codebase-memory-mcp/`.
- Trust repair (2026-09, issues #5/#6/#7) verification matrix:
  - #5 — `server_handle_tools_list_defaults_to_minimal_surface_and_accepts_cursor`
    (exact membership + count pin), no-flag parse asserts MINIMAL, explicit
    `--tool-profile=all` keeps ALL.
  - #6 — `tool_trace_path_caller_resolution_summary`: counts on tree+json,
    note absent at 1/3, note fires at exactly 1/2 and at total==0,
    `include_evidence=true` keeps the summary once and consistent.
  - #7 — `tool_detect_changes_honesty_note_in_both_formats`: both fields in
    both formats on the zero-impacted case.
  - #10 — the `watch_decide_*` pure-decision matrix, cooldown integration
    (env blocks then disables live), watcher freshness accessor states, and
    `tool_index_status_freshness_fresh_pending_reindexed_and_absent`.
    Phase-2 caveat: dirty-path flow tests now need TWO polls (stage + confirm)
    — any future upstream rebase must not "simplify" them back to one.
  - Full-suite note: `make -f Makefile.cbm test` on a maintainer machine is
    the acceptance gate (fork CI builds only); see docs/BUILD_WINDOWS.md for
    the Windows recipe.

## 10. Test-debt repayment + merge-gap repairs (2026-09-29)

The maintainer's first full Windows test run (MSYS2 CLANG64 + ASan) found the
suite out of sync with the fork's inverted defaults; the repayment followed the
test-debt design in `plans/depth-tiering-toolset.md` §5.1 (fixtures opt into
the legacy ALL surface, default-contract tests assert fork semantics). The run
also exposed four real defects that only a test run can catch — all fixed:

- **MinGW usable-size query** (`src/foundation/mem_core.c`): under the linker
  wrap (`--wrap=malloc/calloc/_msize`), `charge_size` asked `_msize`, which
  re-derived block ownership via `mi_is_in_heap_region` and misrouted while
  worker threads expanded the allocator page map — `RtlSizeHeap` trap on a
  fresh block. MinGW builds now call `mi_usable_size` directly (every block
  this file measures was served by the wrap).
- **c2 deepen cursor decode** (`src/mcp/mcp.c`, fork issue #9): decode read
  depth before the leg, but the wire order is `c2.<leg>.<depth>...` — every
  minted deepen cursor was rejected as unrecognized. Decode now matches
  encode; `tool_trace_path_deepen_cursor_pulls_next_tier` and the generation
  matrix prove the round trip.
- **Job Object memory evidence** (`src/foundation/subprocess.c`): upstream
  added the `job_memory_*` result fields and asserting tests without the query
  that fills them. `cbm_win_capture_job_memory` now records limit/peak/available
  at the terminal transition; the enforcing test's cap moved to 256 MiB (the
  probe commits 512 MiB, so a 1 GiB cap can never trip).
- **detect_changes budget floor** (`src/mcp/mcp.c`): the floor response omitted
  the change totals; a budget-strapped agent needs them most. The floor now
  carries `changed_total/changed_returned/changed_has_more` plus the
  continuation-reason key, mirroring the normal emitter.

Test-side alignment: fixtures use `test_server_all_surface()` (ALL surface);
`log_level_default` asserts the fork's error default; auto_watch default
rewritten to "unset registers nothing" + explicit-true registers; issue403 /
autoindex probes run on the ALL surface because auto-index is gated to it by
design; the FTS maintenance fixture drives `cbm_store_fts_rebuild` the way the
pipeline does. Verified: `test-focused TEST_SUITES="mcp watcher log
index_policy subprocess"` → 458 PASS / 0 FAIL / 29 SKIP (gcc 13.2, SANITIZE=
because the local MinGW gcc ships no ASan runtime; the recipe toolchain stays
MSYS2 CLANG64).

## 11. Minimal-face tool descriptions: trigger + interpretation lines, mechanics pushed to the skill (2026-09-30)

**动机**：`tools/list` 的 description 是每个会话的常驻 wire 开销，而其中的
分页/续读/诚实性细则只有工具被选中后才需要——常驻既稀释触发信号又烧上下文。
fork 配套 skill（`skills/cbm/SKILL.md`）以渐进披露承载细则：skill description
常驻（负责触发），body 按需加载（负责机制、配方与判读边界）。顺带修掉一个死
建议：原 query_graph 描述里的 `get_graph_schema(diagnostics=full)` 路由指针在
MCP 最小面不可达（该工具不在面上），这类跨工具路由只属于 skill。

**新增不变量（描述分层契约）**：

- description = 触发语 + 结果判读契约。search_graph 的 degree 列五族边枚举是
  判读契约（度列只统计 CALLS/USAGE/CALL_REFERENCE/INHERITS/IMPLEMENTS），留在
  描述里，`mcp_tools_list_latest_metadata` 断言看守。
- inputSchema = 参数契约。aspect 枚举、path 语义、max_rows/cursor 规则等只在
  schema 出现一次，描述不再复述。
- `skills/cbm/SKILL.md` = 机制 + 组合配方 + 诚实性边界（next_cursor 续读规则、
  graph=missed 缺席≠完备、auto-index 仅 ALL profile）。

**逐工具修改点（`src/mcp/mcp.c` TOOLS[]，字符数含标点）**：

- `search_graph` 158→125：匹配机制措辞降为触发级（BM25→keyword；query/
  name_pattern/semantic_query 的分工 schema 已表达）；保留五族边枚举。
- `query_graph` 336→125：下推 200 行默认/精确或下界总数/next_cursor 续读规则、
  graph=missed 诚实性注记、get_graph_schema 路由。
- `get_architecture` 178→150：aspect 全枚举与 path 语义与 schema 重复，删；
  保留默认输出形态（languages/packages/entry_points）。

三段合计 672→400（-40%）。顶部 `--help` 的 Tools: 块由同一注册表渲染
（`cbm_mcp_tools_help_list`），同步变短——描述只有这一处事实源。

**测试**：`tests/test_mcp.c` `mcp_tools_list_latest_metadata` 断言改写为新契约：
三工具新触发语 + 五族边枚举必须出现；`absence is not proof` /
`continue safely with next_cursor` 必须缺席（细则已离开 wire 的回归护栏）。

**验证**：`mcp` 套件 321 PASS / 0 FAIL（gcc 13.2，SANITIZE= 本机无 ASan 运行时；
权威配方仍见 docs/BUILD_WINDOWS.md）。`cli` 套件另有 12 个**预存**失败
（hook/README-surface 系列，stash 基线对照确认与本节改动无关，属于全量跑批
遗留债，不在此节修复）。

## 12. Minimal face grows get_graph_schema (2026-09-30)

**动机**（v0.11.0-fork.2 实测）：最小面缺属性目录发现通道。实测 query_graph
对未知属性在 WHERE 里静默返回 `total: 0`（权威假阴性）、RETURN 里返回空值
行。Grok 会话有 cbm skill 兜底属性目录；MCP-only 客户端（2026-09-30 起 Qwen
Code 也接入了最小面）读不到 skill，写错属性即得无法诊断的假阴性。
get_graph_schema 是现成的发现通道：默认输出紧凑（json 约 1KB），
diagnostics=full 约 5.6KB，只读零信任风险，描述已是触发语形态（89 字符），
且 §11 删掉的 `get_graph_schema(diagnostics=full)` 死指针由此复活。

**逐文件修改点**：

- `src/mcp/mcp.c` — `minimal_tools[]` 增补 `get_graph_schema`（3→4）；
  `MCP_MINIMAL_SERVER_INSTRUCTIONS` 同步（three→four，补 schema 用途句）。
- `tests/test_mcp.c` — `server_handle_tools_list_defaults_to_minimal_surface_and_accepts_cursor`
  断言新契约：计数钉死 4U（两处）、schema 必须在面、负面名单不变
  （detect_changes / index_repository / manage_adr / ingest_traces 仍不在）。
- `README.md` / `AGENTS.md` / `skills/cbm/SKILL.md` — 计数与细则同步。

**不升面的候选（评估记录，2026-09-30）**：`trace_path`（baseline 特许 +
instructions 委托本地工具；一跳 callers 经 Cypher 实测可解，§6 健康摘要修的是
安全而非升面）、`detect_changes`（#2128 dead-verdict + #480 direction 静默空
未修，修复是升面前提）、`index_status` / `check_index_coverage`（新鲜度带外
维护，instructions 已教 out-of-band 路径）、`search_code` / `get_code_snippet` /
`get_file_outline`（与客户端 Read/grep 重叠）。后续候选（非工具）：
query_graph 未知属性 fail-loud 校验——同一 silent-empty 家族（#480 同型），
做了之后 schema 的必要性再降半档。

**验证**：`mcp` 套件全绿（gcc 13.2，SANITIZE=）；`cli` 套件 12 个预存失败
不变（同 §11 基线）。
