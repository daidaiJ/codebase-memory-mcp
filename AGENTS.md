# AGENTS.md — 智能体协作指南

> 本文件帮助 AI 智能体快速理解这个 fork 的结构、补丁约定和已知坑位，确保长期维护的一致性。

---

## 项目一句话定位

**codebase-memory-mcp（fork）**：上游是 C 语言代码库知识图谱 MCP 服务（tree-sitter 解析 → SQLite 图存储 → Cypher 查询）。fork 把它裁剪为「CLI 全量 + MCP 最小面（3 工具）」形态，默认值全面反转为 opt-in，策略支持项目本地配置文件。

## 技术栈速览

| 项 | 值 |
|---|---|
| 语言 | C11（`-Wall -Wextra -Werror`，GCC/Clang 双编译器） |
| 解析 | tree-sitter（内建 grammar） |
| 存储 | SQLite（`vendored/sqlite3`） |
| 分配器 | mimalloc（vendored，带本地补丁：`-Wdate-time`） |
| JSON | yyjson（vendored） |
| 构建 | `Makefile.cbm`；发布走精简 GitHub Actions（windows-amd64） |
| 平台 | Windows / macOS / Linux（daemon 架构，Windows 有 DACL 硬化层） |

## 目录结构与关键文件

```
src/
├── main.c              # 入口 + 子命令分发（cli/mcp/hook/config/daemon…）+ run_cli
├── mcp/mcp.c           # ★ 工具注册表 TOOLS[]、tool profile、tools/list、dispatch_tool
├── cli/cli.c           # ★ config 存储（_config.db）、本地 .cbm/config.json、工具面校验
├── cli/cli.h           # config 键定义（CBM_CONFIG_*）+ 读取 API
├── daemon/application.c # 守护进程会话管理（context 头含 profile 字节，双侧边界校验）
├── daemon/ipc.c        # Windows DACL 硬化检查（win_file_acl_secure）
├── foundation/log.c    # 日志（默认级别在 g_log_level 静态初始化处）
├── foundation/mem.c    # 内存预算解析（cbm_mem_resolve_budget = 唯一汇聚点）
├── watcher/watcher.c   # git 轮询 watcher：双轮确认 + 冷却 + freshness（fork issue #10，判定逻辑在纯函数 cbm_watcher_decide_poll）
└── ui/config.c         # 图谱 UI 配置（ui_enabled，fork 已去自启）
tests/test_mem.c        # 预算解析测试（fork 语义已同步）
docs/
├── FORK_PATCHES.md     # ★ 补丁清单：动机 + 逐文件修改点（改代码前先读）
├── CONFIGURATION.md    # 配置参考（fork 已更新）
├── BUILD_WINDOWS.md    # Windows 本机构建/测试配方（MSYS2 CLANG64，MSVC 不适用）
└── UPSTREAM_README.md  # 上游原版 README
```

## fork 补丁约定（改代码前必读）

1. **每个行为差异都有 issue 对应**（fork issues #1-#4），动机和验证矩阵记在 `docs/FORK_PATCHES.md`。新增差异 → 先建 issue 再改代码，同步更新该文档。
2. **MCP 默认面 = minimal（3 工具）**：改工具面相关代码时，三处必须一致——`mcp.c` 的 `minimal_tools[]`、`cbm_mcp_parse_tool_profile_args` 默认值、`main.c` MCP client 角色。daemon 内部会话保持 ALL（CLI 全量的根基），不要动。
3. **fail-loud 是硬约束**：拒绝路径必须非零退出 + 明确 message。禁止静默空结果/静默忽略（上游病灶见 FORK_PATCHES「silent-empty family」）。
4. **配置键三处同步**：`cli.h` 的 `#define` + `cli.c` 的 `CONFIG_KEYS[]` 表 + 运行时读取点。`config list/get/set/help` 全部从 CONFIG_KEYS 表驱动，漏了表用户就看不见。
5. **wire protocol**：tool profile 以 uint8 走 context 头，`application.c` 里 client 发送侧和 daemon 校验侧的边界必须同时放宽，否则会话直接被拒。
6. **daemon 会话的 config 句柄**：MCP 面的 denylist 读 `srv->config`（daemon 启动时打开的 `_config.db`，每请求现查 sqlite，无缓存）；本地策略读 `<session_root>/.cbm/config.json`；CLI 面读 cwd 的本地文件 → 全局库。

## CI 约定（2026-09-05 精简）

workflow 只保留 `fork-win64.yml`（构建 + tag 发 release）。上游遗产 CI（DCO 签名检查、Scorecard、CodeQL、上游 build/lint/test/security/PR 系列、上游 release.yml、pages、cache-warm、soak/repro 类、issue 管理 stale/label 系列）已全部删除——本仓库无外部贡献者，DCO 不再要求 `git commit -s`；发版只走 `fork-win64`（打 `v*fork*` tag），不要重新引入上游 release.yml。

## 开发约定

### 修改配置项

- 加键：`cli.h` define → `CONFIG_KEYS[]` 表 → 读取点（含 NULL-cfg 默认值）→ `docs/CONFIGURATION.md` → `README.md` 速查表
- 反转默认值：搜索该键的**所有** `cbm_config_get_bool/int` 调用点逐个改，包括 `!cfg` 分支的字面量默认（三处 auto_watch 就是教训）

### 本地策略文件

- 路径固定 `<dir>/.cbm/config.json`，JSON 对象，键与 config 子命令一致
- 损坏文件 → `cbm_log_warn` + 忽略，绝不污染 stdout（MCP JSON-RPC / help 输出是 stdout）
- 只加读取，不做写入——手写文件，`config set` 只管全局库（会校验工具名）

### 测试

- `tests/` 与上游同一套 harness（`Makefile.cbm` 的 test 目标）；预算语义测试在 `tests/test_mem.c`
- fork 改了默认值语义的，测试断言**一起改**（断言新契约，不是旧契约）

## 能力边界与已知坑（智能体需 aware）

| 区域 | 现状 | 结论 |
|------|------|------|
| 图谱查询静默漏报 | CALLS 边丢失时 trace_path 返回 callers_total:0，与真无调用不可分（fork #6/#7 已把防御升级为带内：trace_path 带 caller_resolution，detect_changes 带 resolution_caveat） | 「没有调用方」类否定结论仍建议先 grep 复核；带内注记是提示不是证明 |
| `detect_changes` 非法 direction | 已 fail-loud（上游 commit `60390aff` 修复，fork 基线已含，tests/test_mcp.c 有断言 pin） | 无需再修；rebase 时确认该 teaching error 未被冲掉 |
| Windows DACL 检查 | fork 默认**跳过**不可信 ACE 遍历；`CBM_DACL_HARDENING=1` 才开回 | 属主校验两种模式都保留，别动它；多用户主机文档要提示开回 |
| 内存预算 | cap 只封默认分数，显式 env 永远可上调（`default_capped` 标志随之清零） | 改 resolve_budget 时保持这个单语义 |
| watcher 触发时序 | fork issue #10：脏变更需**两轮**签名相等才索引（HEAD 移动单轮即触发），成功索引后有 30s 冷却（`CBM_WATCH_COOLDOWN_S`，0 关闭）；`index_status` 带 `freshness` 块 | dirty 路径的 watcher 测试都是「stage + confirm」两轮——rebase 时别「简化」回单轮；freshness 锚是聚合口径，禁止 per-file mtime / Branch.head_sha |
| MinGW 分配器 wrap | `--wrap=_msize` 经 `mi_is_in_heap_region` 判归属，并发扩页映射时会误判（`charge_size` 内 RtlSizeHeap 陷阱，2026-09-29 实测） | mem_core 的 usable-size 在 MinGW 直接走 `mi_usable_size`，别改回 `_msize` |
| 守护进程冷启 | ~5s（Windows + DACL + 指纹哈希） | CLI 面的策略拒绝必须在 bootstrap **之前**（run_cli 已前置，别挪到 daemon 执行后） |

## 快速上手命令

```bash
# 全量构建
make -f Makefile.cbm

# 语法级快速检查单个文件（无完整依赖链时）
gcc -fsyntax-only -std=c11 -Isrc -Ivendored -Ivendored/sqlite3 \
  -Ivendored/mimalloc/include src/foundation/mem.c

# 配置试验
./codebase-memory-mcp config set tools_disabled search_graph
./codebase-memory-mcp cli --help        # 确认 search_graph 已从列表消失
./codebase-memory-mcp cli search_graph  # 确认非零退出 + 显式报错
```

## 给智能体的提示

1. **改任何工具面/配置行为前**，先读 `docs/FORK_PATCHES.md` 对应小节，理解「为什么」而不仅是「改哪里」
2. **上游 rebase 后**：重点核对 `application.c` 的 profile 边界校验、`CONFIG_KEYS[]` 表、`mcp_tool_allowed` 三处是否被上游改动冲掉
3. **不要「顺手」恢复任何上游默认**（auto_watch=true / log=info / UI 自启 / 无预算上限）——那是被 fork issue #3 明确否决的设计
4. **新增 MCP 工具时**：注册进 `TOOLS[]` 后必须同时决定它进不进 `minimal_tools[]`，并更新 FORK_PATCHES 的对照证据

## ✅ Phase 1 + Phase 2 收尾状态（minimal-swap-t2-t7，2026-09-29 完成）

- T0-T7 全部落地，各自独立 commit（T7 = `e02fec1a`，fork issue #10；T2-T6 见
  `git log` c9f5bcbf..da13a3d1）。Windows 构建说明见 [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md)。
- 补丁动机与验证矩阵：`docs/FORK_PATCHES.md` §5-§9 + Verification notes。
- **唯二未完成项：维护者本机实跑全量构建 + 测试（agent 从不跑测试）。**
  - 务必实跑 `make -f Makefile.cbm test`（或 `test-par`）；重点 suite：`mcp`
    （T6 cursor 契约 + T7 freshness 矩阵）、`watcher`（T7 双轮确认/冷却）。
  - dirty 路径 watcher 测试是「stage + confirm」两轮节奏；首次跑挂了先看
    FORK_PATCHES §9 与 handoff 文档 §8 的踩坑记录再归因。
- 历史任务源：[`.handoff/minimal-swap-t2-t7.md`](.handoff/minimal-swap-t2-t7.md)
  （已标记 done）与 `.plan/spec.md`。

## 🔄 Handoff 摘要

### local-build-test-first-run — in-progress（2026-09-29）

- **当前状态：** 本机首次实跑构建+测试：构建 ✅、test_mcp.c 编译阻塞已修（`fd89f6a3` 已推送）、
  聚焦 9 suite 已跑（2336 PASS / 173 FAIL，**语言 suite 全绿**，mcp 172 + watcher 1 失败已定位根因）；
  全量测试仅跑 ~45%，剩余由维护者继续。
- **关键证据：** minimal 面唯一真实遗留 = T6 测试末尾 cursor 断言与 pinned minimal=3 工具 <
  页大小 8 矛盾（`tests/test_mcp.c:1983`），永不通过；其余 mcp 失败均为非 minimal 工具在默认
  MINIMAL profile 下被 deny 的旧断言。详见 handoff §3/§4。
- **验收标准：** 全量测试跑完 + minimal 面与 watcher suite 全绿（见 handoff §5）。
- **详情指针：** [`.handoff/local-build-test-first-run.md`](.handoff/local-build-test-first-run.md)
- **未验证事项：**
  - [ ] 全量 144 suite 测试（剩余 ~135 个未跑）
  - [ ] log/index_policy/subprocess 三个孤立失败归因
  - [ ] 索引冒烟检查（BUILD_WINDOWS.md SMOKE 配方）
