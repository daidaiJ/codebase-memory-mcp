# Improvement backlog (community-grounded)

Prioritized, deliberately restrained improvements for this fork, derived from a
survey of how the community actually uses code-map MCP servers (upstream
issues/discussions, competitor designs, MCP token-cost practice; surveyed
2026-09-29). Each item names the evidence that motivates it, the intended
change shape, and where it lands in this tree. Per fork convention, every item
needs a fork issue **before** code changes and an update to
`docs/FORK_PATCHES.md` after.

Design rule for this backlog: **no new MCP tools.** The registry is already 17
tools — past the ~10-tool point where LLM tool-selection quality starts to
degrade (cited in MCP SEP-1300) — and three of them (`compare_graphs`,
`manage_adr`, `ingest_traces`) have zero observable community usage. Improvements
only target the three ways tools in this class die in practice:

1. **Untrustworthy results** (worse than no tool: silent misses read as true
   negatives),
2. **Stale indexes** that mislead,
3. **Agents never calling the tools** (built-in Read/Grep is a hardcoded
   baseline — upstream maintainers confirmed this pattern themselves).

---

## P0 — Trust repair (hardest evidence, smallest diffs)

### B1. `trace_path`: caller-resolution health summary in the default response

- Evidence: upstream #1682 ("the misses are silent, `trace_path(direction=
  "inbound")` returns `callers_total: 0`... the failure is indistinguishable
  from a true negative"), #1548 (nested Python defs drop the HTTP layer from
  the graph), #514 (NestJS DI defeats ~70% of caller resolution), #2061
  (overload folding produces fake callers). This fork's AGENTS.md lists the
  CALLS-edge gap as a known limitation, but today that defense is docs-only.
- Change: even when `include_evidence=false`, append one summary field to the
  response, e.g. `"caller_resolution": "23/31 resolved, 8 unresolved"`, plus a
  note when the unresolved ratio is high: `"0 callers does not mean no callers
  — retry with include_evidence"`. The per-hop strategy/confidence values
  already exist in the traversal result (tree columns behind
  `include_evidence`); this is an output-layer diff only.
- Lands in: `trace_path` tree/json response builders (`src/mcp/mcp.c`).
- Fork alignment: extends the fail-loud principle (fork issue #4) from
  rejection paths to query results.

### B2. `detect_changes`: `unknown` semantics + fail loud on invalid direction

- Evidence: upstream #2128 (open, high): measured dead-code precision 52% —
  247 of 359 verdicts were framework dispatch entries; reporter: "Reporting
  `unknown` when the graph is unresolved would be honest and far more useful
  than a false negative." Separately, this fork's AGENTS.md already flags
  upstream's silent-empty on invalid `direction` as pending fail-loud repair.
- Change: invalid `direction` exits non-zero with an explicit message (hard
  fork constraint); verdicts whose graph support is unresolved return
  `dead: "unknown"` instead of a bare boolean. Tests first.
- Lands in: `detect_changes` handler + `tests/`.

## P1 — Index freshness made visible

### B3. Carry a freshness header on query-class tools

- Evidence: upstream discussion #345 — maintainers confirm "saving a file does
  not guarantee that background indexing has already finished" (staleness is
  by design, but invisible). Stale indexes are the top community deduction for
  this tool class ("drift and stale indexes"). The fork already has the
  building block: `coverage_path_freshness()` runs inside
  `check_index_coverage` — it is just not exposed where agents actually look.
- Change: add a lightweight header field to `search_graph` / `query_graph` /
  `trace_path` responses, e.g. `"index": {"indexed_at": ..., "dirty_files":
  12}`; when `dirty_files` exceeds a threshold, prepend one warning line
  ("index may be stale — consider reindex or read files directly"). This is
  the restrained version of CodeGraph's staleness banner: no watcher, no
  reconciliation pass, just count mtimes newer than the index at response time.
- Lands in: daemon session response assembly — one change benefits all three
  tools.

## P2 — Adoption rate (largest UX lever, zero protocol change)

### B4. Tool descriptions: first sentence answers "when to call me"

- Evidence: upstream discussion #226 — maintainers: "Agents use their default
  behavior which is kinda hardcoded into themselves more than respecting
  additionally defined behavior/skills." A sibling tool in this class measured
  a 0% autonomous adoption rate; a 180-run community benchmark found real
  token savings far below the advertised ones. The description is the only
  lever the server has over tool selection.
- Change: rewrite each `TOOLS[]` description so the first sentence states when
  to call the tool instead of what it is, e.g. `trace_path`: "Use BEFORE
  reading files to find who calls X or what X calls; cheaper than grepping
  callers." Keep descriptions compact — measurement (arXiv:2602.14878) shows
  compact, purpose-clear descriptions raise selection success while verbose
  ones burn tokens.
- Lands in: `TOOLS[]` strings in `src/mcp/mcp.c` (text-only diff).

### B5. `config agent-instructions`: paste-ready agent guidance, no new MCP tool

- Evidence: same adoption problem; upstream users resort to hand-written
  CLAUDE.md blocks and third-party hook plugins (discussion #1029) just to
  make agents call the tools.
- Change: a `config` subcommand that prints a paste-ready Markdown block
  (when to use graph tools vs plain Read, what staleness warnings mean,
  current tool profile respected). Pure stdout, no writes, no new MCP tool.
- Lands in: `src/main.c` subcommand dispatch + `src/cli/cli.c`, same pattern
  as the existing config surface.

## P3 — Monetize the graph we already have

### B6. `get_architecture`: ranked digest + token budget (aider-style repo map, no new tool)

- Evidence: aider's repo map (PageRank over tree-sitter tags, binary-searched
  into a token budget) is the reference implementation of
  overview-then-drill-down. This fork's graph is already in SQLite, so ranking
  is cheaper to build here than it was for aider.
- Change: extend `get_architecture` with a top-N symbols section ranked by
  reference count/PageRank, sized by a `map_tokens` parameter. No 18th tool.
- Lands in: `get_architecture` output builder + one ranking query.

---

## Decision point (config, not code)

The minimal profile is currently `get_architecture` + `query_graph` +
`detect_changes`. Community usage ranks `search_graph` second among query
tools (210 issue mentions), while `detect_changes` is the least trustworthy
one (#2128). Revisit whether `detect_changes` stays in the minimal face —
ideally after B2 lands, or swap it for `search_graph`. This is a profile-list
decision (`minimal_tools[]` in `src/mcp/mcp.c`, plus the documented
three-place sync), not a code-architecture change.

## Explicitly not doing (restraint list)

| Not doing | Why |
|---|---|
| Any new MCP tool (cross-repo, commit-lineage, REPL, standalone repo-map) | Registry already past the degradation point; competitor features ≠ our evidence-backed pain |
| Embedding / vector retrieval | `search_graph` already has `semantic_query`; exact-symbol grounding is this fork's differentiator |
| TOON or other compact wire formats | Benchmarks show unstable gains on irregular nested data; the existing tree format already does the work |
| Tool search / dynamic toolsets / Code Mode | Definition overhead is far below Anthropic's own threshold (>10K tokens); sandbox cost dominates |
| File watcher / auto incremental index | B3's cheap banner covers most of the value; watchers brought upstream sustained-load complaints (discussion #1187) |

## Evidence quality notes

GitHub issues/discussions cited above were verified in full; Reddit threads
(0% adoption post, 180-run benchmark, staleness consensus) were reachable only
as search snapshots and are treated as indicative, not citable verbatim.
X/Twitter yielded no usable evidence. The 180-run benchmark's tool list is
unconfirmed to include this project.
