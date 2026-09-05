---
name: cbm
description: 当需要架构概览/模块聚类/复杂度热点排行（codegraph 无架构视图）、commit 影响半径（diff 驱动，codegraph impact 只能符号驱动）、大型或陌生仓库整体理解时，用 codebase-memory-mcp CLI 查询预索引知识图谱。小项目或单符号查询直接用 grep/codegraph，不要动 cbm。
---

# cbm（codebase-memory-mcp fork）— 架构级图谱查询（低频重型工具）

定位：**只做 codegraph 和 grep 做不了/不划算的事**。使用 fork 补丁版 CLI（命令名 `codebase-memory-mcp`，安装见下节）；MCP 注册保持移除，全部能力走 CLI。
实测依据（2026-09-05，websearch-mcpserver 三场对照实验）：

- **赢的场景**：架构概览（fan-in 热点/边界权重/分层/聚类，grep 需十几次调用）、全仓函数复杂度排行（cognitive 维度 LOC 给不出）、commit 级爆炸半径（唯一 diff 驱动形态）
- **输的场景**（别用 cbm）：单符号影响查询（codegraph impact 0.7s 全对）、单函数复杂度验真（`grep -c` 更快）、小仓库结构（`ls`+`wc -l` 两秒八成）

## 安装（一次性）

CLI 是单文件可执行程序，从 fork release 下载放进 PATH 即可：

- release 地址：https://github.com/daidaiJ/codebase-memory-mcp/releases
- Windows amd64：下载资产 `codebase-memory-mcp-windows-amd64.exe`，改名为 `codebase-memory-mcp.exe` 放入 PATH 目录（或 `gh release download --repo daidaiJ/codebase-memory-mcp`）
- 其他平台：fork release 目前仅构建 windows-amd64；可从源码构建（msys2 CLANG64 环境，`scripts/build.sh`），或改用上游版（可用，但下方配方依赖的 fork 行为差异见 fork README 的 fork-patch 章节）
- 验证：`codebase-memory-mcp --version`，`codebase-memory-mcp cli --help` 可见工具列表

## 前置

- 索引由 SessionStart hook 自动维护（fast 模式：跳过嵌入/相似度/githistory）；图谱只保证**会话开始时新鲜**，会话中途的变更不追同步
- project 名派生规则：路径分隔符 `/`→`-`，如 `C:\dev\web-app` → `C-dev-web-app`、`/home/me/work/web-app` → `-home-me-work-web-app`；拿不准跑 `codebase-memory-mcp cli list_projects`
- fork 默认值已反转，无需手动补偿：`auto_watch=false`（不自动注册 watcher）、日志默认 `error`、内存预算封顶 2048MB、UI 不自启；Windows 缓存目录 DACL 检查**默认跳过**（属主校验保留；多用户主机才需 `CBM_DACL_HARDENING=1` 开回）
- 缓存/配置根：默认 `~/.cache/codebase-memory-mcp`，可用 `CBM_CACHE_DIR` 重定向（如指到本地快速盘）；除它外没有别的 CBM 环境变量
- 每次调用冷启临时 daemon 约 5 秒——**把问题攒一批问，别一条一条聊**
- 原始 JSON 传参有 deprecation 警告（未来版本可能移除），届时 `codebase-memory-mcp cli <tool> --help` 查 flag 形式

## 命令配方（实测可用语法）

### 1. 架构概览（陌生/大型仓库第一站）

```bash
codebase-memory-mcp cli get_architecture '{"project":"<PROJECT名>","aspects":["hotspots","boundaries","layers","clusters"]}'
```

输出判读：`hotspots`（fan-in 排行=枢纽符号）、`boundaries`（跨模块调用权重）、`layers`（core/internal/entry 分层）、`clusters`（Leiden 聚类+cohesion）。overview 里的 node/edge 计数和 packages 清单是废物，跳过。

### 2. 复杂度热点排行（review 优先级）

```bash
codebase-memory-mcp cli query_graph '{"project":"<PROJECT名>","query":"MATCH (f:Function) WHERE f.complexity IS NOT NULL RETURN f.qualified_name, f.complexity, f.cognitive, f.loop_depth ORDER BY f.complexity DESC LIMIT 10"}'
```

可用属性：`complexity`（圈复杂度）、`cognitive`（认知复杂度）、`loop_depth`、`transitive_loop_depth`、`linear_scan_in_loop`（循环内线性扫描=隐藏 O(n²)）。排序选中后**用 Read 验真**再下结论。

### 3. commit 影响半径（唯一 diff 驱动形态）

```bash
PARENT=$(git rev-parse <commit>^)   # ⚠️ 不接受 ^ ~ 后缀，必须传完整 SHA
codebase-memory-mcp cli detect_changes "{\"project\":\"<PROJECT名>\",\"since\":\"$PARENT\",\"scope\":\"impact\"}"
```

⚠️ `direction` 合法值仅 `inbound|outbound|both`——传非法值（如 "impact"）**静默返回空**（上游 #480 同款病）。`changed_files` 里的非代码文件是噪声，`impacted` 按 hop 距离排序。

### 4. 信任前置闸门

```bash
codebase-memory-mcp cli check_index_coverage '{"project":"<PROJECT名>","scopes":["."]}'
```

大范围采信图谱结果前先跑：`parse_partial` 文件的图谱可能局部失明（cbm 索引自己源码时 81 个文件解析失败）。

## 硬规则

1. **空结果 ≠ 真阴性**：依次排查 ① 参数非法（direction/项目名/SHA 格式）→ ② coverage 盲区 → ③ grep 复核，三关过了才准信"没有调用方/没有影响"。
2. 图谱与代码文件冲突时，以代码文件为准，会话开头重跑 hook 或 `codebase-memory-mcp cli index_repository --repo-path . --mode fast`。
3. 变异管理类（index_repository / delete_project / manage_adr / ingest_traces）同样走 CLI，非必要不碰。
4. 若要恢复 MCP 形态（受限客户端嵌入场景）：fork 的 MCP 默认只暴露 **3 个工具**（get_architecture / query_graph / detect_changes），`--tool-profile=all` 恢复全量，`--tool-profile=minimal|analysis|scout` 可选；全局 `tools_disabled` 名单与项目本地 `.cbm/config.json` 可细粒度禁用（双侧生效、fail-loud）。配置键见 `codebase-memory-mcp config list`。
