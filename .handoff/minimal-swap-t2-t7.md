---
handoff_id: minimal-swap-t2-t7
status: done
---

## 1. 目标

按 `.plan/spec.md` 完成 fork 的信任修复 Phase 1（T2-T4）与索引经济
Phase 2（T5-T7），外加一份 Windows 本机构建/测试说明文档。

## 2. 当前进度与状态

- **状态：done（2026-09-29 完成）。** T0-T7 全部落地：
  T2 `c9f5bcbf` / T3 `3037dd6f` / T4 `f31f9a87` / T5 `410648ef` /
  T6 `da13a3d1` / **T7 `e02fec1a`**（双轮确认 + 冷却 + freshness，
  FORK_PATCHES §9）；收尾 `2f674dff`（AGENTS.md 摘要块摘除 → 完成状态 +
  待验证清单）。Windows 构建说明：`docs/BUILD_WINDOWS.md`。

## 3. 遗留（唯一）

- [ ] 维护者本机实跑全量构建 + `make -f Makefile.cbm test`（T2-T7 全部
      代码/测试从未构建或执行过，agent 不跑测试）。重点 suite：`mcp`
      （T6 cursor 契约、T7 freshness）、`watcher`（T7 双轮确认/冷却，
      dirty 路径测试已改为「stage + confirm」两轮节奏）。
- [ ] FTS `COUNT(*)` contentless 行为（T5，实现带 fail-loud 兜底）由
      `index_status_maintenance_accounts_fts_and_lsp_orphans` 实测确认。

以下为原始交接记录（历史参考）。

## 4. 关键证据

- 本会话五个 commit（`git log` 可复核，内容均经 diff 自查）：
  - `c9f5bcbf` T2 trace_path caller_resolution（#6）
  - `3037dd6f` T3 detect_changes honesty 注记（#7）+ AGENTS.md 坑表两行更正
  - `f31f9a87` T4 backlog 决议标注 + **修复 T1 遗留的 RUN_TEST 注册断链**
  - `410648ef` T5 存储滞留三处（#8，新增 src/foundation/cache_sweep.c/.h）
  - `da13a3d1` T6 depth frontier probe + c2 deepen cursor（#9）
- 开场核验通过：HEAD 双跑一致、`git show 7399e8f0` T1 内容复核、工作区仅 `?? .qwen/`
- 每个Ticket 均同步 FORK_PATCHES（§5-§8）+ tests/test_mcp.c 新测试

## 5. 排查方向

无新环境事故。本会话开场按既定流程做了双跑比对核验，全部一致。

## 6. 验收标准

- [x] T2-T6 每个 Ticket 一个自包含 commit（代码+测试+FORK_PATCHES 同步，message 带 issue 号）
- [ ] T7 一个自包含 commit（#10：双轮确认 + 冷却 + freshness 透出）
- [ ] 用户本机实跑全量构建+测试全绿（用户自己跑，agent 不跑）
- [ ] Windows 构建/测试说明文档成文（docs/，参照 `.github/workflows/fork-win64.yml`）
- [ ] AGENTS.md 摘要块在任务完成后摘除

## 7. 后续待完成的任务和步骤

1. **T7 — 惊群防护 + 新鲜度透出（fork issue #10）**，spec 锚点：
   - 双轮确认：`poll_project`（`src/watcher/watcher.c:1418`）发现 dirty 不立即
     index_fn；记 pending_since + 本轮 dirty_sig（`check_changes` 已把观察值暂存在
     `pending_dirty_sig`/`pending_head`，`watcher.c:1297-1344`），下一轮签名仍相等
     才触发；中间再变则重置。**HEAD 移动例外：单轮即触发**（commit 是显式动作）。
   - 冷却：成功索引后每项目 cooldown，常量默认 30s，env `CBM_WATCH_COOLDOWN_S`
     覆盖（watcher 风格：常量+env，不新增 config 键）；冷却期内 dirty 只刷新 pending。
   - freshness 透出：watcher 暴露 per-project（last_index_success、indexed_sig vs
     last_dirty_sig、pending_since）→ `index_status` 加
     `freshness: {indexed_at, workspace_dirty, index_pending, stale}`；server 已有
     watcher 句柄（`cbm_mcp_server_set_watcher`）；auto_watch=off 时字段缺省。
   - **约束：** freshness 锚必须 generation/indexed_at 聚合口径，禁止 per-file mtime
     对比（#1714）、禁止 Branch.head_sha 锚（#1213）。
   - **测试要求：** 抽 poll 判定为纯函数——两轮相等→触发、中间变化→重置、冷却期
     不触发、HEAD 变更单轮触发；index_status 三态（fresh/dirty/pending）断言。
   - **风险（明码标价）：** 双轮确认把变更→索引拉长一个 poll 周期（5-60s），
     FORK_PATCHES 与 docs/CONFIGURATION.md 文案如实写。
2. **Windows 构建/测试说明**（docs/ 下成文）：CI 权威依据
   `.github/workflows/fork-win64.yml`（MSYS2 CLANG64）；本机有 MSVC 但
   Makefile.cbm 是 GCC/Clang 体系，cl 不可直接用——如实写「用 MSYS2 CLANG64，
   MSVC 不适用」。
3. T7 完成后：FORK_PATCHES 补 §9（#10）+ 全量文档核对 + AGENTS.md 摘要块摘除。

## 8. 资产与使用说明

| 资产 | 路径 / 引用 | 用途 |
|------|------------|------|
| 规格 | `.plan/spec.md` | T0-T7 全部锚点/契约/测试要求（唯一权威源） |
| 补丁清单 | `docs/FORK_PATCHES.md` §1-§8 | 已落地补丁的动机+逐文件+验证矩阵 |
| Fork issues | daidaiJ/codebase-memory-mcp #5-#10 | 每 Ticket 对应（#10 待做） |
| CI 参照 | `.github/workflows/fork-win64.yml` | Windows 构建说明权威依据 |

## 9. 注意事项

- **已踩过的坑（本会话新增）：**
  - tree 标量值含空格会被 `append_value` 加引号（`compact_out.c` needs_quotes）——
    测试断言 tree 字段值时必须带引号（T2/T3 已踩过并修正）。
  - trace_path 默认 `edge_data_limit=0`（不收边）——任何依赖
    `trace_predecessor_edge` 的默认路径功能必须先强制 inbound 收边（T2 的
    `in_edge_data_limit` 模式）。
  - `MCP_DEFAULT_DEPTH = 3`（spec 里写 2 是笔误），limit 默认 100。
  - 改名 TEST 必须同步 RUN_TEST 注册行（T1 断链在 T4 对账时才发现）。
- **用户认同的稳定执行流程：** agent 不跑任何构建/测试；每 Ticket 完成
  git diff 自查后 commit；测试由维护者本机跑。
- **环境限制：** 本机无 gcc/make/msys2/WSL；codegraph 对本仓库无索引（直接
  grep/Read）；本会话 codegraph 调用已报"不要再调"。
- **cursor 语义速查（T6 后）：** c1=c1.<leg>.<gen>.<qhash含depth>.<hop>.<id>；
  c2=c2.<leg>.<depth>.<gen>.<depthless qhash>.<hop>.<id>，c2 回放忽略 depth 参数
  （deeper-only by construction），hop=0/id=0 为 fresh anchor（防御性保留）。

## 10. 用户偏好

- 「不要用实机测试验证，我都说我回去弄」
- 「先开发后面记录下怎么在 win 上构建测试，然后我回去再跑测试」
- 本会话新增：「先就完成到 t6 然后交接一下下个会话继续」

## 11. 建议使用的 Skill 和 MCP

- **Skill：** `sdd-implement`（继续按 spec Ticket 推进）；`handoff`（完成后再交接）
- **MCP：** codegraph 对本仓库无索引，直接用 Grep/Read + `git show` 交叉验证

## 12. 用户下个会话聚焦重点

- 从 T7 开始（fork issue #10，watcher.c 时序改造，动 poll 判定时序建议抽纯函数
  便于测试），然后写 Windows 构建/测试说明，最后全量文档核对 + 摘要块摘除。

## 13. 明确标注为验证的事项（未完成勿删）

> ⚠️ 以下事项**尚未完成验证**，下个会话不得默认其为已完成。

### 已执行、待验证
- [ ] T2-T6 全部代码/测试改动 — 从未跑过构建或测试（用户明确要求），一切待
      维护者本机验证；T6 涉及 cursor 契约，务必实跑 test_mcp 全量
- [ ] FTS `COUNT(*)` 在 contentless FTS5 上的行为（T5 maintenance 记账）——
      实现带了 fail-loud 兜底（失败→maintenance_status: unavailable），测试
      `index_status_maintenance_accounts_fts_and_lsp_orphans` 会验证
- [ ] 本会话 python 内联脚本批量改过 tests/test_mcp.c 的引号断言（LF 写回）——
      git diff 已自查，但建议 `git show f31f9a87..da13a3d1 --stat` 快速过目

### 已完成且已验证
- [x] 开场核验 — HEAD 双跑一致 + T1 commit `git show` 内容复核
- [x] T2-T6 五个 commit — 每个 Ticket 提交前做了 git diff 逐段自查
- [x] TEST/RUN_TEST 全量对账 — 仅 T1 一处断链（已修于 f31f9a87）
