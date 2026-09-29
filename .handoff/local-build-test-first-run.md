---
handoff_id: local-build-test-first-run
status: active
---

## 1. 目标

在本机首次实跑维护者配方「全量构建 + 测试」（AGENTS.md 收尾状态里的「唯二未完成项」），暴露并记录测试面问题，供维护者接续跑完后半部分并修复。

## 2. 当前进度与状态

- **当前阶段：** 构建完成 ✅；测试编译阻塞已修复并推送（`fd89f6a3`）；聚焦测试集已跑完，结果已记录；**全量测试只跑了约 45%（~6400/144 suites 中断）**，剩余由维护者继续。
- **状态：** in-progress（测试执行未跑完 + 失败项待修）
- **进度：**
  - [x] MSYS2 CLANG64 工具链装好（`D:\tool\msys64`，便携自包含）
  - [x] 生产二进制构建通过（`make -f Makefile.cbm cbm`，clang 22.1.8，-Werror 全绿）
  - [x] `tests/test_mcp.c` 4 处编译错误修复（commit `fd89f6a3`，已推送）
  - [x] 聚焦测试集：`mcp watcher go_lsp c_lsp py_lsp rust_lsp ts_lsp java_lsp java_lsp_coverage` → **2336 PASS / 173 FAIL / 11 SKIP**
  - [x] 语言 suite（用户关心的 java/rust/js/ts/go/c-cpp/python）**全部 PASS，0 失败**
  - [ ] 全量测试剩余 ~135 个 suite 未跑（用户回自己跑后半部分）
  - [ ] 失败项修复（见 §6，优先级见 §11）

## 3. 关键证据

- 聚焦集完整日志：`D:\tool\msys64\tmp\cbm-focused.log`（MSYS2 `/tmp/cbm-focused.log`）
- 全量中止前日志：`D:\tool\msys64\tmp\cbm-test3.log`
- 失败分布：`mcp` suite 172 个 + `watcher` 1 个；其余 8 个语言 suite 0 失败（c_lsp 762 PASS、rust_lsp 522、ts_lsp 304、java_lsp_coverage 284、java_lsp 95、watcher 91、py_lsp 78、go_lsp 52）
- minimal 面唯一真实缺陷：`tests/test_mcp.c:1983` 附近，T6 测试 `server_handle_tools_list_defaults_to_minimal_surface_and_accepts_cursor` 末尾断言「cursor=0 页必须带 `nextCursor`」，但 MINIMAL 面只有 3 个工具 < `MCP_TOOLS_PAGE_SIZE=8`（`src/mcp/mcp.c:36`），任何页都装得下全部工具，`end < allowed_count` 永假 → **该断言在 pin 死的 minimal 面下永不通过**。需决策：此子断言改用 ALL profile 的 server 验证分页语义（推荐），或删除断言。

## 4. 排查方向（仅 debug 任务）

- **已定位（无需再查）：** 其余 ~171 个 mcp 失败的共同根因 = 这些测试用 `cbm_mcp_server_new(NULL)`（默认 `CBM_MCP_TOOL_PROFILE_MINIMAL`，`src/mcp/mcp.c:1708` 起），随后调用**非 minimal 工具**（detect_changes、index_repository、compare_graphs、check_index_coverage、get_file_outline、get_code_snippet、list_projects、lean defaults、issue403 系列等），在 `mcp.c:17926` 被 deny，响应是 deny 文本而非测试期待的错误消息/JSON → strstr/doc 断言失败。这是 fork issue #4 默认反转后测试面没跟上，不是产品代码回归。
- **已排除：** clang 一次性 ICE（`grammar_glsl.c` 在 ASan 标志下 Lex 段崩溃）——原样重试通过，环境性偶发，非代码问题。
- **待查（低优先）：** 全量中止前发现的 4 个孤立失败（mcp suite 之外）：
  - `log_level_default`（tests/test_log.c:59）期望 `cbm_log_get_level()==1(INFO)`，实际 3 —— 疑似断言的还是上游「默认 info」旧契约，而 fork issue #3 已改默认；按 fork 约定测试断言应跟新语义走。
  - `index_policy_worker_rejects_missing_parent_policy` / `index_policy_mcp_rejects_forged_override_and_preserves_serving_index`（tests/test_index_policy.c:285/382）。
  - `subprocess_windows_job_object_enforces_memory_limit`（tests/test_subprocess.c:785）—— Windows job object 内存限制，可能是本机环境限制。

## 5. 验收标准

- [ ] 全量 `make -f Makefile.cbm test`（或分批 test-focused/test-par）在 MSYS2 CLANG64 下跑完一遍
- [ ] minimal 面相关失败（T6 cursor 断言）修复且 `test-focused TEST_SUITES="mcp"` 全绿
- [ ] 非 minimal 工具的 mcp 测试面与 fork issue #4 语义对齐（补 `cbm_mcp_server_set_tool_profile(srv, CBM_MCP_TOOL_PROFILE_ALL)` 或改断言）
- [ ] watcher suite 全绿
- [ ] log/index_policy/subprocess 三个孤立失败归因并修复或记录豁免理由

## 6. 后续待完成的任务和步骤

1. 维护者本机继续跑全量测试（剩余 ~135 suite）。建议分批，避免单次超长：
   ```bash
   # MSYS2 CLANG64 shell 内，仓库根目录
   make -f Makefile.cbm test-par CC=clang CXX=clang++   # 每 suite 独立进程
   # 或分批聚焦：
   make -f Makefile.cbm test-focused TEST_SUITES="<suite 名>" CC=clang CXX=clang++
   # suite 全集：build/c/test-runner --list-suites（需在 CLANG64 shell 里跑，ASan DLL 依赖 PATH）
   # 压力/基准 suite 可用 CBM_SKIP_PERF=1 跳过
   ```
2. 修 T6 cursor 断言（tests/test_mcp.c ~1983）：该子断言换 ALL profile server 或删除，最小面语义不受影响。
3. 批量对齐非 minimal 工具测试：给 §4 列出的测试加 `cbm_mcp_server_set_tool_profile(srv, CBM_MCP_TOOL_PROFILE_ALL)`（多数在其 TEST 开头一行即可）。
4. 归因 log/index_policy/subprocess 三个孤立失败。
5. 全绿后按 AGENTS.md 约定把本 handoff 标记 completed。

## 7. 资产与使用说明

| 资产 | 路径 / 引用 | 用途 |
|------|------------|------|
| MSYS2 便携环境 | `D:\tool\msys64` | 自包含，不写注册表；删目录即卸载 |
| 运行配方 | `docs/BUILD_WINDOWS.md` | 权威构建/测试步骤（本次验证有效） |
| 测试日志 | `D:\tool\msys64\tmp\cbm-focused.log` / `cbm-test3.log` | 失败明细 grep `FAIL tests` |
| 测试框架宏 | `tests/test_framework.h:101` | `ASSERT_*` 失败时 `return 1`，只能用于 int 返回的 TEST() |

## 8. 注意事项

- **已踩过的坑：**
  - 裸 `make -f Makefile.cbm` 的默认目标是文件内第一条规则 `$(BUILD_DIR):`（只 mkdir，退出 0 什么都不编）——必须显式 `make cbm` / `make test`。
  - `test-runner.exe` 链接 ASan，必须在 CLANG64 shell 内跑，否则报 `libclang_rt.asan_dynamic-x86_64.dll` 缺失。
  - Git Bash 的 `-oD:\tool` 参数转义会把 MSYS2 sfx 解压到错误层级；解压后 `msys64` 目录应位于 `D:\tool\msys64`。
  - `/tmp` 是 MSYS2 的 `/tmp`（= `D:\tool\msys64\tmp`），外层 Git Bash 看不到。
  - clang 一次性 ICE：重试即可，不要急着归因代码。
- **环境限制：** MSVC/cl 不可用（GNU make + bash 脚本 + GCC/Clang 方言 flag，`docs/BUILD_WINDOWS.md` 定论，用户现场确认过）；本机无 winget/choco，MSYS2 走 sfx 手动装。
- **用户认同的稳定执行流程：** 先聚焦跑用户关心语言的 suite，全量后台分批补。

## 9. 用户偏好

- 「我重视 minimal 部分的工具集，其他就那样」→ minimal 三工具面（search_graph/query_graph/get_architecture）相关问题是最高优先级；非 minimal 工具的测试失败降级处理。
- 「我只在乎 java rust js ts go c/cpp（+python）」→ 语言 suite 是验收重点，本次已全部 PASS。
- MSVC 曾被提出可用，已确认不可用后用户同意装 MSYS2。

## 10. 建议使用的 Skill 和 MCP

- **Skill：** 无特殊依赖；修测试时遵循 AGENTS.md「fork 补丁约定」（fail-loud、断言新契约）。

## 11. 用户下个会话聚焦重点

- 先修 minimal 面唯一真实遗留（T6 cursor 断言，§3）；其余 mcp 失败批量对齐 profile 即可。
- 语言 suite 已全绿，回归时保持 `TEST_SUITES="mcp watcher go_lsp c_lsp py_lsp rust_lsp ts_lsp java_lsp java_lsp_coverage"` 作为快速门。

## 12. 明确标注为验证的事项（未完成勿删）

> ⚠️ 以下事项**尚未完成验证**，下个会话不得默认其为已完成。

### 已执行、待验证
- [ ] 全量 144 suite 测试 — 仅完成 ~45%（聚焦 9 个 suite + 全量前 6400 用例），剩余待维护者跑
- [ ] `log_level_default` 等 3 个孤立失败 — 未归因（§4 待查清单）
- [ ] 索引冒烟检查（BUILD_WINDOWS.md 的 SMOKE 配方）— 因用户叫停未执行

### 已完成且已验证
- [x] 生产二进制构建 — `make -f Makefile.cbm cbm` 退出 0，`build/c/codebase-memory-mcp` 产出，clang 22.1.8 -Werror 全绿
- [x] test_mcp.c 编译修复 — test-runner 链接成功（521MB ASan 构建），commit `fd89f6a3` 已推送 origin
- [x] 聚焦集 9 suite — 2336 PASS / 173 FAIL / 11 SKIP，失败全部定位（mcp 172 + watcher 1，语言 suite 0 失败）
