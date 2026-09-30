> English · [中文](AGENT_GUIDE.md)

# codebase-memory-mcp fork — Agent Guide

Architecture-level code knowledge graph tool (CLI with all 17 tools / MCP minimal surface of 4 tools). **Only use it for what grep and symbol tools can't do or do uneconomically; never use it instead of grep / codegraph / direct Read.**

> Human edition: [HUMAN_GUIDE.en.md](HUMAN_GUIDE.en.md) · Fork design trade-offs: [README](../README.md#设计思路) · MCP minimal-surface details: [skills/cbm/SKILL.md](../skills/cbm/SKILL.md)

## When NOT to use cbm

- Single-symbol lookup, exact source, text search → Read / grep (zero cold start, sees non-symbol content)
- Single-symbol impact, call chains, find-definition → codegraph (`impact` nails it in 0.7s)
- Structure questions on small repos → `ls` + `wc -l` in two seconds

cbm earns its keep only for: architecture overview (fan-in hotspots/boundaries/layers/clusters — grep needs a dozen calls to assemble), whole-repo complexity ranking (cognitive dimension; LOC can't), commit blast radius (the only diff-driven form), and holistic understanding of large/unknown repos.

## Get it and self-check

```bash
# 1. Release binary (windows-amd64, single file)
gh release download --repo daidaiJ/codebase-memory-mcp
# 2. Build from source: make -f Makefile.cbm

# 30-second self-check: version + tool list + smoke query
codebase-memory-mcp --version
codebase-memory-mcp cli --help              # tool list visible = install OK
codebase-memory-mcp cli list_projects       # indexed projects; empty = run L0-0 first
```

Ground facts: project name = path separators `/`→`-` (`C:\dev\web-app` → `C-dev-web-app`); each call pays ~5s daemon cold start — **batch your questions**; fork defaults are already inverted, no compensation needed (`auto_watch=false`, log level error, memory capped at 2048, no UI auto-start); apart from `CBM_CACHE_DIR` and a few others there are no extra environment variables.

## Command tiers

### L0 — default starting point (get the low tier working before moving up)

```bash
# 0. Index the repo (fast mode suffices for daily use)
codebase-memory-mcp cli index_repository --repo-path . --mode fast
# 1. First stop in a large/unknown repo
codebase-memory-mcp cli get_architecture '{"project":"<P>","aspects":["hotspots","boundaries","layers","clusters"]}'
# 2. Complexity ranking (review priority; verify the top hits with Read before concluding)
codebase-memory-mcp cli query_graph '{"project":"<P>","query":"MATCH (f:Function) WHERE f.complexity IS NOT NULL RETURN f.qualified_name, f.complexity, f.cognitive ORDER BY f.complexity DESC LIMIT 10"}'
```

Reading the output: `hotspots` fan-in ranking = hub symbols; the `overview` node/edge counts are dead weight — skip them.

### L1 — combinations and trust gates

```bash
# Commit blast radius (no ^ ~ suffixes; full SHA required; invalid direction/scope errors explicitly)
PARENT=$(git rev-parse <sha>^)
codebase-memory-mcp cli detect_changes "{\"project\":\"<P>\",\"since\":\"$PARENT\",\"scope\":\"impact\"}"
# Run before trusting results at scale: parse_partial files can be locally blind in the graph
codebase-memory-mcp cli check_index_coverage '{"project":"<P>","scopes":["."]}'
# One-hop callers (the MCP minimal surface has no trace_path — solve it with Cypher directly)
codebase-memory-mcp cli query_graph '{"project":"<P>","query":"MATCH (c:Function)-[:CALLS]->(f:Function) WHERE f.qualified_name = \"<QN>\" RETURN c.qualified_name"}'
```

### L2 — advanced (constrained clients / policy)

```bash
# MCP surface profile: minimal (default 4 tools) / analysis / scout / all
codebase-memory-mcp cli --tool-profile=all
# Fine-grained disable (effective on both MCP and CLI, fails loud, hidden from --help)
codebase-memory-mcp config set tools_disabled trace_path,get_code_snippet
# Project-local policy: write {"tools_disabled": "..."} into <project>/.cbm/config.json — highest priority
```

## Parameter/tool quick reference

| Need | Tool/command | Key parameters |
|------|----------|---------|
| Find definitions/callers | `search_graph` | `query` (keyword) and `semantic_query` (embedding) are mutually exclusive |
| Multi-hop/aggregation/ranking | `query_graph` | Misspelled properties **error explicitly** and point to `get_graph_schema` |
| Architecture overview | `get_architecture` | Omitting aspects = languages/packages/entry_points; `cycles` is always opt-in |
| Property catalog discovery | `get_graph_schema` | `diagnostics=full` adds the list of queryable properties |
| Commit blast radius | `detect_changes` (CLI/analysis surface) | `since` takes full SHAs, `scope: impact` |
| Freshness | `index_status` (CLI) | Read the `freshness` and `maintenance` blocks |

## Machine contract (in-response honesty fields)

| Field | Meaning | Reading rule |
|------|------|---------|
| `caller_resolution` | `{"resolved":2,"total":3,"unresolved":1}` | Teaching note attached when `total=0` or unresolved ≥ half; **0 ≠ no callers** |
| `resolution_caveat` (detect_changes) | CALLS edges are resolution heuristics | `impacted: []` does not mean no impact — cross-check with `trace_path` + `include_evidence=true` |
| `next_cursor` (query_graph) | Continuation token | Keep query/project/graph unchanged, vary only max_rows/cursor; totals are exact or a lower bound with a truncation flag |
| `graph=missed` | Coverage-gap file tree | **Absent ≠ complete**; gap files are outside the graph, not symbol-free |
| `freshness` (index_status) | `indexed_at`/`workspace_dirty`/`index_pending`/`stale` | The whole block is **omitted** without watcher signal: absent = no data, never faked freshness |
| `depth_frontier` / `deepen_cursor` (trace_path) | PULL token for the next depth tier | On `limited`, pass the cursor back as `cursor` to pull the next tier; if the probe fails the field is omitted |

## Error triage

| Error string (searchable verbatim) | Nature | Action |
|------|------|------|
| `disabled by config` | Tool is denylisted or outside the current profile | Check with `config list`; `config reset tools_disabled` |
| `Unknown property` (query_graph) | The fork's catalog validation rejected an unknown property | Follow the pointer to `get_graph_schema`; stop guessing property names |
| `Invalid direction/scope/mode` | Teaching error; the message lists legal values | Fix the parameter per the message |
| `stale_cursor` | Another cursor parameter changed | Restart from the original query and mint a fresh cursor |
| `not available in the minimal tool profile` | The MCP minimal surface lacks that tool (trace_path/detect_changes/index_status, …) | Switch to the CLI surface or `--tool-profile=all` |
| `secure daemon endpoint could not be created` | The rendezvous ancestry failed security checks | Set `CBM_RUNTIME_DIR` to a directory you own |

## Hard rules (common pitfalls)

1. **Empty result ≠ true negative**: ① invalid params (SHA format/project name/enum value) → ② `check_index_coverage` blind spots → ③ grep re-check. Only after all three gates may you claim "no callers / no impact".
2. When the graph conflicts with code files, **code wins**, then re-index: `cli index_repository --repo-path . --mode fast`.
3. Mutation-management tools (`index_repository` / `delete_project` / `manage_adr` / `ingest_traces`) go through the CLI; avoid unless necessary.
4. The MCP minimal surface never auto-indexes (auto-index fires on ALL profiles only) — constrained clients should index via the CLI first.
5. Verify ranking/hotspot conclusions with Read before acting; the graph gives leads, not verdicts.
