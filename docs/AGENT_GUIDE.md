> [English](AGENT_GUIDE.en.md) · 中文

# codebase-memory-mcp fork — Agent Guide

架构级代码知识图谱工具（CLI 全量 17 工具 / MCP 最小面 4 工具）。**只做 grep 和符号工具做不了或做不划算的事；不要用它替代 grep / codegraph / 直接 Read。**

> 人类阅读版：[HUMAN_GUIDE.md](HUMAN_GUIDE.md) · fork 设计取舍：[README](../README.md#设计思路) · MCP 最小面细则：[skills/cbm/SKILL.md](../skills/cbm/SKILL.md)

## 何时不要用 cbm

- 单符号定位、精确源码、文本搜索 → Read / grep（零冷启，看得到非符号内容）
- 单符号影响查询、调用链、找定义 → codegraph（`impact` 0.7s 全对）
- 小仓库结构问题 → `ls` + `wc -l` 两秒搞定

值得用 cbm 的只有：架构概览（fan-in 热点/边界/分层/聚类，grep 要十几次调用拼）、全仓复杂度排行（cognitive 维度 LOC 给不出）、commit 爆炸半径（唯一 diff 驱动形态）、大型陌生仓库整体理解。

## 获取与自检

```bash
# 1. Release 二进制（windows-amd64，单文件）
gh release download --repo daidaiJ/codebase-memory-mcp
# 2. 源码构建：make -f Makefile.cbm

# 30 秒自检：版本 + 工具列表 + 冒烟查询
codebase-memory-mcp --version
codebase-memory-mcp cli --help              # 工具列表可见 = 安装成功
codebase-memory-mcp cli list_projects       # 已索引项目；空 = 先跑 L0-0 建索引
```

前置事实：project 名 = 路径分隔符 `/`→`-`（`C:\dev\web-app` → `C-dev-web-app`）；每次调用冷启 ~5s，**攒一批问**；fork 默认值已反转无需补偿（`auto_watch=false`、日志 error、内存封顶 2048、UI 不自启）；除 `CBM_CACHE_DIR` 等少数变量外没有别的环境变量。

## 命令档位

### L0 — 默认起步（先低档跑通再升）

```bash
# 0. 建索引（fast 模式日常够用）
codebase-memory-mcp cli index_repository --repo-path . --mode fast
# 1. 陌生/大型仓库第一站
codebase-memory-mcp cli get_architecture '{"project":"<P>","aspects":["hotspots","boundaries","layers","clusters"]}'
# 2. 复杂度排行（review 优先级；排序选中后用 Read 验真再下结论）
codebase-memory-mcp cli query_graph '{"project":"<P>","query":"MATCH (f:Function) WHERE f.complexity IS NOT NULL RETURN f.qualified_name, f.complexity, f.cognitive ORDER BY f.complexity DESC LIMIT 10"}'
```

判读：`hotspots` fan-in 排行 = 枢纽符号；`overview` 的 node/edge 计数是废物，跳过。

### L1 — 组合与信任闸门

```bash
# commit 影响半径（不接受 ^ ~，必须完整 SHA；非法 direction/scope 会显式报错）
PARENT=$(git rev-parse <sha>^)
codebase-memory-mcp cli detect_changes "{\"project\":\"<P>\",\"since\":\"$PARENT\",\"scope\":\"impact\"}"
# 大范围采信前先跑：parse_partial 文件的图谱可能局部失明
codebase-memory-mcp cli check_index_coverage '{"project":"<P>","scopes":["."]}'
# 一跳调用链（MCP 最小面没有 trace_path，用 Cypher 直接解）
codebase-memory-mcp cli query_graph '{"project":"<P>","query":"MATCH (c:Function)-[:CALLS]->(f:Function) WHERE f.qualified_name = \"<QN>\" RETURN c.qualified_name"}'
```

### L2 — 高级（受限客户端 / 策略）

```bash
# MCP 面 profile：minimal（默认 4 工具）/ analysis / scout / all
codebase-memory-mcp cli --tool-profile=all
# 细粒度禁用（MCP/CLI 双侧生效、fail-loud、--help 同步隐藏）
codebase-memory-mcp config set tools_disabled trace_path,get_code_snippet
# 项目本地策略：<项目>/.cbm/config.json 写 {"tools_disabled": "..."}，优先级最高
```

## 参数/工具速查

| 需求 | 工具/命令 | 关键参数 |
|------|----------|---------|
| 定位定义/调用方 | `search_graph` | `query`（关键词）与 `semantic_query`（嵌入）互斥 |
| 多跳/聚合/排行 | `query_graph` | 属性拼错**显式报错**并指引 `get_graph_schema` |
| 架构总览 | `get_architecture` | 省略 aspects = languages/packages/entry_points；`cycles` 永远 opt-in |
| 属性目录发现 | `get_graph_schema` | `diagnostics=full` 追加可查属性清单 |
| commit 影响半径 | `detect_changes`（CLI/analysis 面） | `since` 完整 SHA，`scope: impact` |
| 新鲜度 | `index_status`（CLI） | 读 `freshness` 与 `maintenance` 块 |

## 机器契约（响应内诚实性字段）

| 字段 | 含义 | 判读规则 |
|------|------|---------|
| `caller_resolution` | `{"resolved":2,"total":3,"unresolved":1}` | `total=0` 或未解析 ≥ 半数时附教学注记；**0 ≠ 没有调用方** |
| `resolution_caveat`（detect_changes） | CALLS 边是解析启发式 | `impacted: []` 不等于无影响，用 `trace_path` + `include_evidence=true` 交叉核对 |
| `next_cursor`（query_graph） | 续读令牌 | 保持 query/project/graph 不变，只换 max_rows/cursor；总数是精确值或下界并带截断标记 |
| `graph=missed` | 覆盖盲区文件树 | **缺席 ≠ 完备**；盲区文件不在图谱里，不是没有符号 |
| `freshness`（index_status） | `indexed_at`/`workspace_dirty`/`index_pending`/`stale` | 无 watcher 信号时整块**缺席**：缺席 = 没数据，从不伪装新鲜 |
| `depth_frontier` / `deepen_cursor`（trace_path） | 更深层 PULL 令牌 | `limited` 时把 cursor 传回 `cursor` 参数拉下一层；probe 失败则字段缺席 |

## 错误速诊

| 报错串（原文可搜） | 性质 | 处置 |
|------|------|------|
| `disabled by config` | 工具被 denylist 或不在当前 profile | `config list` 查；`config reset tools_disabled` |
| `Unknown property`（query_graph） | fork 目录校验拦截未知属性 | 按指引跑 `get_graph_schema`；别再猜属性名 |
| `Invalid direction/scope/mode` | teaching-error，文案列合法值 | 照文案改参数 |
| `stale_cursor` | cursor 的其他参数变了 | 从原始查询重新发起，拿到新 cursor |
| `not available in the minimal tool profile` | MCP 最小面没有该工具（trace_path/detect_changes/index_status 等） | 换 CLI 面或 `--tool-profile=all` |
| `secure daemon endpoint could not be created` | rendezvous 目录祖先没过安全检查 | 设 `CBM_RUNTIME_DIR` 指向你拥有的目录 |

## 硬规则（常见坑）

1. **空结果 ≠ 真阴性**：① 参数非法（SHA 格式/项目名/枚举值）→ ② `check_index_coverage` 盲区 → ③ grep 复核，三关过了才准说「没有调用方/没有影响」。
2. 图谱与代码文件冲突时，**以代码文件为准**，然后 `cli index_repository --repo-path . --mode fast` 重索引。
3. 变异管理类（`index_repository` / `delete_project` / `manage_adr` / `ingest_traces`）走 CLI，非必要不碰。
4. MCP 最小面不自动建索引（auto-index 仅 ALL profile）——受限客户端先用 CLI 建索引。
5. 排行/热点结论落地前用 Read 验真；图谱给的是线索不是判决。
