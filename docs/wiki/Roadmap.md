# Roadmap（2026-09-29）

目标：把 fork 从「裁剪正确」推进到「**结果可信 + 索引经济可持续**」。
完整规格见仓库 `feat/minimal-tool-surface` 分支 `.plan/spec.md`；社区证据版
backlog 见 `docs/IMPROVEMENT_BACKLOG.md`。

设计红线（贯穿全部阶段）：**不加新 MCP 工具**（注册表已过 LLM 工具选择质量
退化点）；fail-loud（拒绝路径与查询路径都不许静默空结果）；每项改动先建
fork issue、后改代码、同步 `docs/FORK_PATCHES.md`。

## Phase 1 — 信任修复 + minimal 面换血（T0-T4）

| Ticket | 内容 | 上游证据 |
|---|---|---|
| T0 | 基线：全量构建 + 测试失败清单 + 建 fork issue #5-#7 | — |
| T1 | minimal 面换血：`detect_changes` → `search_graph`；**前置项**：先修 BM25 label 过滤静默漏报（上游 issue #2386 / 修复 PR #2423）；断言 minimal 三工具 `readOnlyHint:true` | #2386/#2148, #2118 |
| T2 | `trace_path` 默认响应带 `caller_resolution` 摘要（"23/31 resolved, 8 unresolved"）+ 高未解析率教学注记；字段只加响应根部（规避与上游 trace_path PR 冲突） | #1682, #1548, #514, #2061 |
| T3 | `detect_changes` 静态 honesty 注记（CALLS 是启发式解析、0 impacted ≠ 无影响） | #2128 精神的 fork 形态 |
| T4 | 收尾：文档同步、全量验证、单 PR | — |

## Phase 2 — 索引经济与滞留治理（T5-T7，2026-09-29 追加）

| Ticket | 内容 | 关键事实 |
|---|---|---|
| T5 | 存储滞留三处全修（纯减法）：① skip 日志保留上限；② daemon 启动清扫崩溃残留（孤儿 staging / supervisor 临时文件 / scratch）；③ FTS 死行与 lsp_surface 滞留记账进 `index_status`，churn 超阈值提示全量重建；**并入**：COUNT 失败返回 0 的语义修正（fail-loud） | contentless FTS 死行随 churn 单调膨胀且 live 库零 VACUUM；#2012 在本基线仍存在 |
| T6 | **分级 + 延迟按需深度下探**：tier0 浅层默认（depth≤2），响应带 `depth_frontier: expanded\|limited` 诚实信号，agent 持 `c2.` deepen cursor 逐层拉取（depth 移出 cursor 参数身份、只增不减），每层独立行/边预算。深层计算延迟到 agent 主动拉取那刻——下探是 pull，不是后台爬取 | 递归 CTE 全深度枚举同价，「没被要求就不跑深层」是唯一省钱手段 |
| T7 | 惊群防护 + 新鲜度透出：脏签名连续 2 轮 poll 稳定才触发索引（HEAD 提交单轮即触发）+ 每项目 30s 冷却；`index_status` 暴露 `freshness`（fresh/dirty/pending）。freshness 锚必须是 generation/indexed_at 聚合口径 | #867（增量≈全量，冷却的定量依据）、#1714（禁 per-file mtime 语义）、#1213（禁 Branch.head_sha 锚）、#520 |

## Rebase watchlist（上游开放 PR，落点与本计划重叠）

| 上游 PR | 主题 | 影响 |
|---|---|---|
| [issue #2386](https://github.com/DeusData/codebase-memory-mcp/issues/2386) → [PR #2423](https://github.com/DeusData/codebase-memory-mcp/pull/2423) | search_graph BM25 label 过滤移入候选窗口 | T1 前置项：上游合入则摘取，未合入则 fork 自修 |
| [#2374](https://github.com/DeusData/codebase-memory-mcp/pull/2374) | trace_path 暴露 possible_callees（OVERRIDE 边） | T2 冲突区：我们的字段只在响应根部 |
| [#2425](https://github.com/DeusData/codebase-memory-mcp/pull/2425) | trace_path QN fallback 内存泄漏 | 同上 |
| [#2402](https://github.com/DeusData/codebase-memory-mcp/pull/2402) | NestJS DI 支持（修 #514） | T2 未解析率阈值依据复核 |

## 已知边界（明确不做）

- 不加任何新 MCP 工具；embedding/向量检索、TOON 压缩、动态工具集继续不做
  （见 IMPROVEMENT_BACKLOG「不做清单」）。
- 不引入内核 FS 事件层（inotify / ReadDirectoryChangesW）——git 轮询 + 双轮
  确认已覆盖，事件层是上游 sustained-load 投诉（discussion #1187）的又一形态。
- 不改 FTS 表结构、不上 auto_vacuum——先记账 + 提示，全量重建交给维护者决策。
- 不给图查询加 deadline/取消——CTE 有 5000 节点硬顶。
- 不做后台自动下探/预取——下探是 agent 拉取。
- Windows staging DB 4 GiB 封顶（#2399）：已知边界记录，本期不做。

## 证据质量说明

GitHub issue/PR 上述编号均于 2026-09-29 全文核对，并逐条在 fork 基线
（v0.11 rebase 后）验证存在性（如 #2386 缺陷在 `mcp.c:3882`、#2012 在
`store.c:3077` 仍复现）。上游讨论区 #2025（agent 完全拒绝调用本工具）是
后续 B4/B5（工具描述重写 + agent 指引）采用率工作的最硬社区证据。
