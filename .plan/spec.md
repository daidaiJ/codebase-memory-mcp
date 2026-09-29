# Spec：信任修复 + minimal 面换血 + 索引经济与存储滞留治理

来源：`docs/IMPROVEMENT_BACKLOG.md`（社区证据版改进清单）P0 + 维护者追加的
「增量索引不惊群 / 查询驱动下探 / 存储滞留排查」三向需求（2026-09-29）。
执行方式：单分支（`feat/minimal-tool-surface` 或新开 `feat/trust-repair`）串行提交，
每个 Ticket 一个自包含 commit（代码 + 测试 + FORK_PATCHES 同步），最后统一验证。
Phase 1（T0-T4）= 信任修复；Phase 2（T5-T7）= 索引经济与滞留治理。

## 已锁定的决策（2026-09-29 与维护者确认）

| 决策点 | 结论 |
|---|---|
| 本期范围 | 只做 B1 + B2（B2 降级），B3-B6 不动 |
| B2 处置 | 降级为静态 honesty 注记（理由见下） |
| minimal 面 | **本期即换血**：`detect_changes` → `search_graph`，不等 B2 后议 |
| 流程 | 单分支串行提交，末尾统一验证 |
| 顺带修复 | test_mcp.c 两条上游契约旧断言一并改写为新契约 |
| 产出 | 本规格 + 代码 + 文档同步 |
| 惊群防护（Phase 2，已确认） | **双轮确认 + 冷却 + 新鲜度透出** |
| 查询下探（Phase 2，已确认） | **分级 + 延迟按需下探**：tier0 浅层默认，信号诚实，deepen cursor 逐层拉取，每层独立预算，不加查询 deadline |
| 存储滞留（Phase 2，已确认） | **三处全修**：skip 日志上限、启动清扫、FTS/lsp_surface 滞留记账 |

## 调研事实基线（写规格时逐点核实过，file:line 可查）

1. **B2 前半已落地**：`trace_path` 与 `detect_changes` 的非法 `direction` 均已
   fail-loud（`src/mcp/mcp.c:9222`、`src/mcp/mcp.c:15961`），且有测试 pin
   （`tests/test_mcp.c:14399` 附近断言 `"invalid direction"`）。AGENTS.md
   已知坑表中「detect_changes 非法 direction 上游静默返回空」一行已过时，本期更正。
2. **B2 后半不适用**：上游 #2128 的 `dead` verdict 在本 fork 的 detect_changes
   中不存在（输出为 changed files + impacted symbols，`src/mcp/mcp.c:16691-16710`）；
   impact 走 `cbm_store_bfs_multi`（多源、不返回边），逐符号证据标注需要扩 store
   API，违背 backlog 克制原则 → 降级为静态 honesty 注记。
3. **B1 的证据机器已存在**：CALLS 边带 `strategy`/`confidence`
   （`pass_calls.c:355` 写入），公开分类闭集 `cbm_mcp_edge_strategy_class`
   （`src/mcp/mcp.c:8317`，`lsp_unresolved`/`unknown` → `"unresolved"`），
   per-edge 读取器 `trace_edge_evidence`（`src/mcp/mcp.c:8372`），
   per-row 前驱边查找 `trace_predecessor_edge`（`src/mcp/mcp.c:8394` 一带的
   trace_edge_context_t 机制）。B1 纯输出层 diff，不需要碰 store 层。
4. **profile 三处同步现状**：成员表 `mcp.c:859-863 minimal_tools[]`；默认值
   `cbm_mcp_parse_tool_profile_args`（`mcp.c:906`，无 flag → MINIMAL）与
   `main.c:2802` 一致，**本期只改成员表，profile 枚举值与 daemon wire 边界
   （`application.c:2428/3281` 只校验 ≤ MINIMAL=3）不动**。
5. **两条旧契约测试当前必挂**（fork CI 只构建不跑全量测试所以未爆）：
   - `server_handle_tools_list_defaults_to_all_tools_and_accepts_cursor`
     （`tests/test_mcp.c:1913`）：断言默认 tools/list 含 `index_repository`/
     `manage_adr`/`ingest_traces` —— 与 `cbm_mcp_server_new` 默认 MINIMAL
     （`src/mcp/mcp.c:1721`）冲突。
   - `analysis_profile_arguments_fail_closed_and_disable_http`
     （`tests/test_mcp.c:2058`）：断言无 flag → ALL —— 与 `mcp.c:914`
     （无 flag → MINIMAL）冲突。
   按 fork 约定改写为断言新契约。

---

## 社区对齐核查（2026-09-29，逐条核对本 fork 基线）

上游 `DeusData/codebase-memory-mcp` 开放 issue/PR 全量扫描 + 本 fork 基线逐条
验证后，对计划的规避调整如下（已合入下方 Ticket 描述）：

| 上游 # | 主题 | 对计划的影响与调整 |
|---|---|---|
| #2386（issue）/ #2148 | search_graph BM25 的 label 过滤在 2000 行候选窗口**之外**生效，稀有 label 静默漏报（本 fork 基线实证存在，`mcp.c:3882`）；修复 PR 为开放的 #2423 | **T1 增补前置项**：search_graph 进 minimal 面之前，先修 label 过滤（上游已开 PR #2423，rebase 时若已合入则摘取），带测试——否则默认面带着静默漏报上线，直接违背本期「信任修复」主题 |
| #2012 | `cbm_store_count_nodes` 失败时返回 0，损坏库被 `index_status` 报成 `status:"empty"`（本 fork 基线实证存在，`store.c:3077`） | **T5 增补**：maintenance 记账的 COUNT 语义必须 fail-loud（错误 ≠ 空库），这是 fork #4 原则在读路径的补漏；上游已关闭该 issue，rebase 时核对修复形态 |
| #2374（开放 PR） | trace_path 增加 `possible_callees`（OVERRIDE 边展开） | T2 rebase watchlist：两个开放 PR 都在改 trace_path 输出；T2 字段**只加响应根部**（caller_resolution 汇总），不加 per-row 列，规避冲突 |
| #2425（开放 PR） | trace_path QN fallback 内存泄漏修复 | 同上，watchlist |
| #2402（开放 PR） | NestJS DI 支持（修 #514） | T2 的 unresolved 阈值依据（NestJS ~70%）随上游修复过时——rebase 后复核 `caller_resolution_note` 的文案与阈值依据 |
| #1714 | 全量索引后 freshness 立刻全体 metadata_changed（Windows FILETIME 口径） | 修复已在本 fork 基线（`mcp.c:6407` 注释同源 FILETIME）；**T7 约束**：freshness 锚必须是 generation/indexed_at 聚合口径，禁止引入 per-file mtime 对比语义 |
| #1213 | Branch.head_sha 重索引后冻结在创建时 HEAD | **T7 约束**：freshness 禁止用 Branch.head_sha 做锚 |
| #867 | 增量重索引 ≈ 全量成本（artifact bootstrap 三成因，已关闭） | T7 冷却设计的定量依据：watcher 触发的每次 delta 可能接近全量成本，冷却不是可选项而是必要项；wiki 记录 |
| #2118 | 13/15 工具 annotations 共用默认值（readOnly 全错，已关闭） | 本 fork 基线已是 per-tool 字段（`mcp.c:834-835`）；**T1 测试增补**：断言 minimal 三工具 `readOnlyHint:true`，防回归 |
| #333 | 静默索引退化（72k LOC 报 ~500 nodes，已关闭） | T5 maintenance 记账顺带暴露 nodes/文件数比值，可捕捉同类退化；不做自动判定，仅透出 |
| #2399 | Windows staging DB 4 GiB 封顶 → persist_failed | 已知边界记录，本期不做 |
| 讨论 #2025 | 「Claude 和 Pi 完全拒绝调用本工具」 | B4/B5（后续周期）采用率问题的最硬社区证据，补充进 backlog |
| 讨论 #2018 | 社区维护的工具定义历史记录 | B4 改描述时的对照基准，避免 fork 文案与上游漂移无据可查 |

**结论**：规划方向与社区证据一致，无需推翻；三处实质调整——T1 前置
search_graph label 过滤修复、T5 增加 COUNT fail-loud 语义、T2/T7 增加 rebase
watchlist 与 freshness 锚约束。

## Ticket 拆解

### T0 — 基线验证（不改产品代码）

- `make -f Makefile.cbm` 全量构建，确认绿。
- 跑 `test_mcp`（以及可行的全量测试），记录失败清单；预期含上述两条旧契约测试，
  若还有别的失败先归因再动 T1-T3。
- 产出：基线失败清单（写进 T1 的 commit message 或本文件追加段）。
- 同时按 fork 约定建 fork issue（gh 或手动）：#5 profile 换血、#6 B1、
  #7 B2'（编号顺延）。**先有 issue 再动代码。**

### T1 — minimal 面换血：`detect_changes` → `search_graph`

改动最小但契约变化最大，先做（它决定后续测试基线）。

- `src/mcp/mcp.c:859` `minimal_tools[]`：`detect_changes` → `search_graph`，
  顺序建议按 agent 使用频率：`search_graph` / `query_graph` / `get_architecture`。
  同步更新 `mcp.c:854` 的注释理由（新保留理由：search_graph 是符号级精确发现，
  社区使用量第二（210 issue mentions）；detect_changes 是最不可信工具（#2128），
  保留于 analysis/all 面，待其可信度修复后再议是否回 minimal）。
- `src/mcp/mcp.h:122-127` 枚举处注释同步（那里列了三个工具名）。
- `src/mcp/mcp.c:906` parse 默认值、`src/main.c:2802`、daemon wire 校验：**不动**。
- 测试（改写为新契约）：
  - `tests/test_mcp.c:1913` 改名 + 改断言：默认 tools/list **恰好**包含
    `search_graph`/`query_graph`/`get_architecture`，且不含 `detect_changes`/
    `index_repository`（补 `mcp_response_tool_count == 3`）。
  - `tests/test_mcp.c:2058`：无 flag 断言改为 `CBM_MCP_TOOL_PROFILE_MINIMAL`
    且 `cbm_mcp_tool_profile_allows_http == false`；explicit `--tool-profile=all`
    → ALL 的用例保留。
  - 新增一条显式 pin：`minimal_tools[]` 成员集合 == 三个名字（防再次漂移）。
- 文档同步：`docs/FORK_PATCHES.md` §1（证据与动机重写）、`docs/CONFIGURATION.md:103`、
  `README.md:18`、`README.md:119-121` 速查段落。
- 注意：`detect_changes` 仍留在 analysis/all 面与 `mcp.c:788` 的注册表，
  denylist 机制（fork #4）不受影响。

### T2 — B1：`trace_path` 默认响应带 caller 解析健康摘要

目标：不传 `include_evidence` 也能看出「0 callers 是查无还是没查到」。
只做 inbound 侧，不新增参数，不做 store 层改动。

- 统计口径：与 `callers_total` 同一过滤集（`include_tests` 过滤后的 `view_in`，
  `src/mcp/mcp.c:9498-9506`），逐行用现有 `trace_predecessor_edge` +
  `trace_edge_evidence` 取边证据：class 存在且 != `"unresolved"` 记 resolved，
  class == `"unresolved"` 或 CALLS 边缺 strategy 记 unresolved。
- 输出（两套 emitter 都加，字段名两侧一致）：
  - tree 路径（`callers_total` 旁，`src/mcp/mcp.c:9532`）：
    `caller_resolution: "23/31 resolved, 8 unresolved"`。
  - legacy json 路径（`src/mcp/mcp.c:9608` 旁）：
    `"caller_resolution": {"resolved": 23, "total": 31, "unresolved": 8}`。
  - 注记行（两类路径同文案常量）：当 `total == 0` 或
    `unresolved/total >= 0.5` 时输出
    `caller_resolution_note: "unresolved callers are common with framework/DI dispatch — 0 callers does not mean no callers; retry with include_evidence=true"`。
    阈值 0.5 写成命名常量 + 注释说明依据（NestJS DI ~70% 未解析，upstream #514）。
- `direction=outbound` 不加对称字段（本项动机全部在 callers 侧），在
  FORK_PATCHES 里写明这个不对称是故意的。
- 测试：fixture 图直接经 store API 插入带 `"strategy":"..."` 的 CALLS 边
  （一条 resolved 策略 + 一条 `lsp_unresolved`），断言 tree/json 两个形态的
  计数字段与注记触发；再断言 `include_evidence=true` 时字段不重复、不冲突。

### T3 — B2'：`detect_changes` 静态 honesty 注记

降级后的形态：常量注记字段，始终输出，不造假统计。

- 在 detect_changes 的 tree 与 json 两套 emitter 的响应根部加：
  `"graph_support": "heuristic-calls"` + 固定文案
  `"resolution_caveat": "CALLS edges are resolution heuristics; 0 impacted does not mean no impact — framework/DI dispatch may be invisible. Cross-check with trace_path(direction=..., include_evidence=true)"`。
  （文案 T2 实施时定稿，两处共用同一常量。）
- 顺带更正 `AGENTS.md` 已知坑表两行：
  「detect_changes 非法 direction」行（已修复，注明修复 commit）；
  「图谱查询静默漏报」行补充「trace_path 现在带 caller_resolution，
  detect_changes 带 resolution_caveat，文档防御升级为带内防御」。
- 测试：两种 format 下响应均含上述字段；`--tool-profile=minimal` 下
  detect_changes 被拒的路径不受影响（复用 T1 的拒绝用例）。

### T4 — 收尾

- `docs/FORK_PATCHES.md`：三个 issue 各补一节（动机 + 逐文件修改点 + 验证矩阵），
  引用 `IMPROVEMENT_BACKLOG.md` 的证据编号；backlog 文档给 B1/B2 标注
  landed-in-fork 形态（B2 记录降级决策）。
- `IMPROVEMENT_BACKLOG.md` 的 Decision point 段更新为已决议 + 依据。
- 全量构建 + 全量测试 + AGENTS.md「快速上手命令」的配置试验三连
  （minimal 面下 `search_graph` 可见可调、`detect_changes` 显式报错非零退出）。
- 单 PR（若走 gh）；commit 顺序 T0→T1→T2→T3→T4，每个 commit 信息注明 issue 号。

## 不做（本期 restraint，延续 backlog）

- 不扩 store API 做 detect_changes 逐符号证据（bfs_multi 无边返回，成本不匹配）。
- 不给 outbound 侧加对称 resolution 字段。
- B3 已并入 T7（降级为 index_status 单点透出，不再做 per-query header）；
  B4-B6 本期不动。daemon 会话的 ALL 面、`--tool-profile` 枚举与 wire 值不动。
- Phase 2 增补的不做：
  - 不引入内核 FS 事件层（inotify/ReadDirectoryChangesW）——git 轮询 + 双轮
    确认已覆盖，事件层是上游 sustained-load 投诉（#1187）的又一形态。
  - 不做后台自动下探/预取——下探是 agent 拉取，不是守护进程爬取。
  - 不改 FTS 表结构、不上 auto_vacuum——先记账 + 提示，全量重建是唯一回收
    途径，交给维护者决策。
  - 不给图查询加 deadline/取消——CTE 有 5000 节点/行预算硬顶，search_code
    已有 30s deadline 模板，图查询侧引入取消是跨层 diff。
  - Phase 2 不新增 config 键（冷却/双轮/日志上限用常量 + env 覆盖，
    与 watcher 现状风格一致）。

## Phase 2 — 索引经济与滞留治理（T5-T7，2026-09-29 追加，维护者已确认）

事实基线来自三路侦察（2026-09-29，file:line 可查）：

- **惊群**：无内核 FS 事件，watcher 是 git 轮询制（5s 基数自适应到 60s，
  `src/watcher/watcher.c:122-124,186-190`；#937 dirty 签名去重 `watcher.c:609-810,1324-1340`；
  单项目单 git 子进程 `watcher.c:407-418`；daemon job 按 project_key 去重
  `application.c:1700-1712`，物理上限 4 `application.c:57`）。fork 默认
  `auto_watch=false`（三处读取一致，FORK_PATCHES §相关小节）。缺口：poll 发现变化
  **立即**重索引（at-least-once，`watcher.c:1503-1506` 自注），无静默期；dirty
  信息锁在 watcher 私有状态（`watcher.c:76`），不透出。
- **下探**：SQLite 递归 CTE 全深度枚举同价，trace_path 一次调用已物化全深度结果
  （`mcp.c:9336-9345`）；cursor 是输出分页非深度续探，depth 绑进 cursor 身份
  （`trace_params_hash` `mcp.c:8694-8738`，改深度即废 cursor）；响应已有诚实截断
  词汇表（`truncated/engine_saturated/edges_truncated` `mcp.c:9442-9445`）但无
  「depth 到顶还是查完了」之区分；图查询无 deadline（仅 search_code 有 30s）。
- **滞留**：① `nodes_fts` contentless FTS5，delta 只 `DELETE FROM nodes`
  （`pipeline_delta.c:20-29,210-247,562`），FTS 死行随 churn 单调膨胀，live 库
  零 VACUUM/auto_vacuum（`store.c:545-605`）；② skip 日志
  `<cache>/logs/<project>-<epoch>.log` 每次退化索引+1 文件，无任何清理
  （`mcp.c:10377-10424`）；③ 崩溃残留无启动清扫（supervisor 临时文件
  `index_supervisor.c:567-590,627-822`、search scratch `mcp.c:14378-14473`、
  孤儿 staging 仅按 final_path 单项目扫 `pipeline.c:1900-1969,3174-3332`）；
  已删文件的 lsp_surface 行滞留到下次全量（`store.c:4874` 仅全量清）。
  RAM 侧健康：cursor 无状态密封 token、store 连接单槽 60s 逐出、会话随断连释放。

### T5 — 存储滞留三处全修（fork issue #8，纯减法优先）

1. **skip 日志上限**（写入点 `mcp.c:10377-10424`）：每项目保留最近 10 个、
   删除 30 天以上；写入后顺带 prune + daemon 启动时再 prune。实现为一个
   小 helper，不新增 config 键。
2. **启动清扫**（daemon bootstrap + index supervisor 启动）：
   - 孤儿 staging：把 `sweep_orphan_stages` 从「同项目下次 run 才触发」扩展为
     daemon 启动时全缓存目录扫描，判据不变（flock 死亡即孤儿）。
   - supervisor 临时文件（`logs/.worker-*-*.log|response`）与 search scratch
     （`<tmp>/cbm-search-*`）：启动时删除 mtime > 1h 的（阈值避开活跃 run）。
   - 测试：预置残留 + 年轻文件 → 启动 → 断言删旧留新。
3. **FTS/lsp_surface 滞留记账**（只记账 + 提示，不改表结构）：
   - `index_status` 加 `maintenance` 段：`fts_rows` vs `nodes_rows` 比值、
     lsp_surface 孤儿文件行估计（LIMIT 探针查询）；count 若在大库上贵则
     采样，实现时实测再定口径。
   - `fts_rows > 3x nodes_rows` → `rebuild_recommended: true` + 一行文案：
     全量重建是 contentless FTS 唯一回收途径。

### T6 — 分级 + 延迟按需深度下探（fork issue #9）

设计原则：递归 CTE 全深度枚举同价 → 省钱的唯一手段是「没被要求就不跑深层」。
下探是 **agent 拉取（pull）**，不是后台爬取；深层计算延迟到 agent 主动拉取那刻。

- **tier 模型**：tier0 = 现状默认（depth≤2、limit 100）；tier1+ = agent 持
  deepen cursor 显式拉取，depth+1 前进一层；每层独立行/边预算（复用现有
  limit 与 5000 引擎顶），深探不会变成 context 炸弹。
- **诚实信号**：响应加 `depth_frontier: "expanded" | "limited"`——对 max-hop
  前沿节点跑一条有界 EXISTS 探针（有无出边指向已物化集合之外）；仅未截断时
  跑。`limited` 时附教学文案：deeper tiers exist — pass back the deepen
  cursor。
- **deepen cursor**：新增 `c2.` 版本编码（复用 c1 encode/decode 骨架
  `mcp.c:8740-8806`），把 depth 移出参数身份哈希、改为 cursor 明文字段且
  **只增不减**（deeper-only 约束）；c1 仍接受（兼容），不带 deepen。generation
  失效语义沿用 stale_cursor。
- **测试**：三层 fixture 图 → tier0 信号 limited；deepen 后拿到新层、tier0 行
  不重复；depth 只能增不能减（防窄化绕过 qhash）；每层预算独立计页。

### T7 — 惊群防护 + 新鲜度透出（fork issue #10，吸收 backlog B3 轻量版）

1. **双轮确认**：`poll_project`（`watcher.c:1476-1543`）发现 dirty 变化不立即
   `index_fn`，记 pending_since + 本轮 dirty_sig；下一轮 poll
   （`watcher.c:1560-1599`）签名仍相等才触发；中间再变则重置 pending。
   **HEAD 移动例外**：提交是显式动作，单轮即触发（dirty 工作区等双轮，
   commit 立即）。
2. **冷却**：每项目成功索引后 cooldown，常量默认 30s，env
   `CBM_WATCH_COOLDOWN_S` 可覆盖（与 watcher 现有常量+env 风格一致，
   不新增 config 键）；冷却期内 dirty 变化只刷新 pending。
3. **新鲜度透出**：watcher 暴露 per-project freshness（last_index_success、
   indexed_sig vs last_dirty_sig、pending_since）；server 已有 watcher 句柄
   （`cbm_mcp_server_set_watcher` `mcp.c:1842-1850`）→ `index_status` 加
   `freshness: {indexed_at, workspace_dirty, index_pending, stale}`。
   auto_watch=off 时字段缺省（诚实：没有 watcher 就没有此信号）。
   **backlog B3 偏差记录**：B3 原案（query 工具 per-response header）降级为
   index_status 单点——T2/T3 已把 honesty 字段放进 trace/detect 响应，
   查询侧不再重复。
4. **测试**：抽 poll 判定为纯函数——两轮相等→触发、中间变化→重置、冷却期
   不触发、HEAD 变更单轮触发；index_status 三态（fresh/dirty/pending）断言。

**Phase 2 顺序**：T5（纯减法、独立）→ T6（动 cursor 契约）→ T7（动 watcher
时序，最后做，复用 T2/T3 的 honesty 文案风格）。每个 Ticket 独立 commit +
FORK_PATCHES 同步，同 Phase 1 约定。

## 风险与既知坑

- T2 统计口径若与 `callers_total` 的 test-file 过滤不一致，会出现
  「23/31 resolved」与「callers_total 31」对不上的观感问题 —— 实现时必须
  复用同一过滤后的 view（见 `mcp.c:9498` 注释里的历史教训）。
- `include_evidence=true` 时 evidence 列已在行内，摘要字段仍保留但注意
  optional_fields_omitted 预算路径（`mcp.c:9557` 一带）不要把摘要算进可省略列。
- test_mcp.c 里 T1 要改的两条测试同时被 fork CI 的 smoke 覆盖不到，
  改完后本地必须实跑一次 test_mcp 而不是只靠构建。

Phase 2 追加风险：

- c1/c2 两代 trace cursor 并存，测试必须覆盖两代的接受/拒绝矩阵
  （c1 无 deepen、c2 depth 只增），否则 cursor 兼容层是静默回归温床。
- frontier 探针（EXISTS 出边查询）在 5000 物化顶的大图上成本需实测；
  有回归就降级为「仅 depth < max_depth 时跑探针」。
- 双轮确认把「变更→索引」整体拉长一个 poll 周期（5-60s）——这是用延迟换
  稳定的明码标价，FORK_PATCHES 与 CONFIGURATION 文案必须如实写。
- FTS COUNT 口径在超大库的成本未知：先实测耗时，贵则降级为采样估计 +
  文案注明「估计值」。
- 清扫阈值（1h mtime、保留 10 个、30 天）都是常量起步，先解决「无上限」，
  参数化留给真实使用反馈。
