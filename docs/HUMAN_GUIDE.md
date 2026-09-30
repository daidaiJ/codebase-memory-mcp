> [English](HUMAN_GUIDE.en.md) · 中文

# HUMAN_GUIDE — 安装 · 配置 · 调优 · 排障（人类阅读版）

本指南面向**人类用户**，覆盖 codebase-memory-mcp fork 的安装部署、三层配置全参数、性能调优与问题排查。给 AI Agent 看的省 token 版见 [AGENT_GUIDE.md](AGENT_GUIDE.md)；项目概览与 fork 设计取舍见 [README](../README.md)。

## 目录

- [安装部署](#安装部署)
  - [方式 A：Release 二进制（推荐）](#方式-a-release-二进制推荐)
  - [方式 B：源码构建](#方式-b-源码构建)
- [快速开始](#快速开始)
- [全参数参考](#全参数参考)
  - [三层配置与优先级](#三层配置与优先级)
  - [`config` 全键表](#config-全键表)
  - [项目本地策略文件](#项目本地策略文件)
  - [环境变量](#环境变量)
  - [tool profiles](#tool-profiles)
- [调优](#调优)
- [排障](#排障)
- [测试与开发](#测试与开发)

---

## 安装部署

### 方式 A：Release 二进制（推荐）

fork release 目前只构建 **windows-amd64** 单文件可执行程序，免运行时依赖：

```bash
# 直链下载（GitHub CDN 超时时加代理前缀，如 HTTPS_PROXY=... curl ...）
curl -L -o codebase-memory-mcp.exe https://github.com/daidaiJ/codebase-memory-mcp/releases/latest/download/codebase-memory-mcp-windows-amd64.exe
# 或用 gh：gh release download --repo daidaiJ/codebase-memory-mcp
./codebase-memory-mcp.exe --version    # 验证
```

放进 PATH 目录后，各处统一用 `codebase-memory-mcp` 调用。

其他平台：从源码构建（见下），或改用[上游版](https://github.com/DeusData/codebase-memory-mcp)——上游可用，但本指南描述的 fork 默认值（minimal 工具面、`auto_watch=false`、内存封顶等）在上游不存在。

### 方式 B：源码构建

Windows 本机走 MSYS2 CLANG64（MSVC 不适用），完整配方见 [BUILD_WINDOWS.md](BUILD_WINDOWS.md)；POSIX 环境直接：

```bash
make -f Makefile.cbm          # 构建全部目标
make -f Makefile.cbm test     # 全量测试（test-par 并行）
```

产物在仓库根目录。工具链：C11 + tree-sitter（内建 grammar）+ SQLite + mimalloc（均 vendored，无外部依赖）。

## 快速开始

```bash
# 1. 建索引（fast 模式：跳过嵌入/相似度/githistory，日常刷新够用）
codebase-memory-mcp cli index_repository --repo-path . --mode fast

# 2. 架构概览（陌生/大型仓库第一站：fan-in 热点、边界权重、分层、聚类）
codebase-memory-mcp cli get_architecture '{"project":"<PROJECT名>","aspects":["hotspots","boundaries","layers","clusters"]}'

# 3. 全仓复杂度排行（review 优先级）
codebase-memory-mcp cli query_graph '{"project":"<PROJECT名>","query":"MATCH (f:Function) WHERE f.complexity IS NOT NULL RETURN f.qualified_name, f.complexity ORDER BY f.complexity DESC LIMIT 10"}'

# 4. commit 影响半径（不接受 ^ ~ 后缀，必须传完整 SHA）
PARENT=$(git rev-parse HEAD^)
codebase-memory-mcp cli detect_changes "{\"project\":\"<PROJECT名>\",\"since\":\"$PARENT\",\"scope\":\"impact\"}"
```

- project 名派生规则：路径分隔符 `/` → `-`（`C:\dev\web-app` → `C-dev-web-app`）；拿不准跑 `cli list_projects`。
- CLI 保留全部 17 个工具；被裁剪的是 MCP 面。CLI 每次调用冷启临时 daemon 约 5 秒——把问题攒一批问。
- 大范围采信图谱结果前先跑 `cli check_index_coverage`：`parse_partial` 文件的图谱可能局部失明。

### MCP 接入

```json
{
  "mcpServers": {
    "codebase-memory-mcp": {
      "command": "codebase-memory-mcp",
      "args": []
    }
  }
}
```

默认即 minimal 面（`search_graph` / `query_graph` / `get_architecture` / `get_graph_schema`）。要全量：`"args": ["--tool-profile=all"]`。

## 全参数参考

### 三层配置与优先级

| 层级 | 位置 | 作用域 |
|------|------|--------|
| 1. 项目本地 | `<项目>/.cbm/config.json` | 仅该项目，**优先级最高** |
| 2. 全局存储 | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db` | 当前用户所有项目 |
| 3. 内置默认 | 编译进二进制 | 兜底 |

### `config` 全键表

```bash
codebase-memory-mcp config list               # 全部键 + 当前值 + 默认值
codebase-memory-mcp config set <key> <value>  # 写全局库（工具名会校验，拼错报错）
codebase-memory-mcp config reset <key>        # 恢复默认
```

| 键 | fork 默认 | 说明 |
|----|----------|------|
| `tools_disabled` | *(空)* | 逗号分隔禁用名单。命中的工具从 MCP `tools/list` 与 `--help` 消失，按名调用时 CLI 非零退出、MCP 显式报错（绝不静默）；MCP/CLI 双侧生效 |
| `auto_watch` | **`false`** | 会话连接时把项目注册给后台 git watcher（上游默认 true） |
| `auto_index` | `false` | MCP 会话启动时自动索引新项目；**仅在 ALL profile 生效**，minimal 工作流用 `cli index_repository` 带外刷新 |
| `auto_index_limit` | `50000` | 自动索引允许的最大文件数 |
| `watcher_enabled` | `true` | watcher 子系统总开关，**守护进程启动时读取一次**——改完要 `daemon stop` 重启才生效 |
| `index_max_files` / `index_max_source_mb` | `off` | 单次索引的文件数/源码体积上限（显式与自动索引都受限） |
| `ui_enabled` / `ui_port` | **`false`** / `9749` | 图谱 UI 环回 HTTP 服务；fork 不自启，显式开启 |

`watcher_enabled` 与 `auto_watch` 的区别：前者决定 watcher 子系统是否存在（daemon 启动时读一次），后者只决定某个会话要不要把自己注册给已存在的 watcher。`watcher_enabled=false` 时 `auto_watch` 无效果。

### 项目本地策略文件

路径固定 `<项目>/.cbm/config.json`，JSON 对象，键与 `config` 子命令一致：

```json
{
  "tools_disabled": "search_graph,trace_path,get_code_snippet"
}
```

- 手写文件，没有写入命令（`config set` 只管全局库）
- 文件不存在 → 跳过此层；损坏/不可读 → warn 日志并忽略，**stdout 永不被污染**（MCP JSON-RPC 与 help 输出都在 stdout 上）
- CLI 面相对工作目录解析，MCP 面相对会话根解析

### 环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `CBM_LOG_LEVEL` | 库级 `error` | `debug`/`info`/`warn`/`error`/`none`（或 `0`-`4`）。前端默认 warn、daemon info；fork 把库内兜底收紧到 error，冷启不再向 CLI stderr 喷告警噪声 |
| `CBM_MEM_BUDGET_MB` | RAM 分数，**封顶 2048** | 显式设置可超过封顶（上限为物理内存） |
| `CBM_DACL_HARDENING` | *(未设 = 跳过)* | Windows 专用。fork 默认跳过缓存目录的不可信 ACE 遍历（共享工具目录的宽松祖先 ACL 不再挡启动），属主校验两种模式都生效。多用户/终端服务器主机设 `=1` 开回 |
| `CBM_WATCH_COOLDOWN_S` | `30` | watcher 成功索引后的冷却秒数，每次使用现读，`0` 关闭。窗口内连 commit 也不触发，变更暂存、窗口结束再触发（不丢） |
| `CBM_WATCHER_PRUNE_GRACE_S` | `600` | 被监听项目根持续缺失多久后清理其缓存 DB |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | 索引、`_config.db`、日志与 UI 配置的存储根 |
| `CBM_RUNTIME_DIR` | 平台默认 | daemon  rendezvous 父目录；默认路径的祖先过不了安全检查时用它重定向 |
| `CBM_ALLOWED_ROOT` | *(未设)* | 把 `index_repository` 圈定在该目录内；被不可信调用方驱动时设置 |
| `CBM_WORKERS` | 自动探测 | 索引 worker 数 |

### tool profiles

`--tool-profile` 决定 MCP 面 advertised 的工具集：

| Profile | 工具 |
|---------|------|
| `minimal`（**fork 默认**） | `search_graph` / `query_graph` / `get_architecture` / `get_graph_schema` |
| `analysis` | 只读检查子集（含 `detect_changes` / `trace_path`） |
| `scout` | 快速正向发现子集 |
| `all` | 全部 17 个（上游默认行为） |

`tools_disabled` 与 profile 组合：工具必须**同时**通过 profile 白名单且不在名单里才可见/可调。

## 调优

先看数据再动手：`cli index_status` 是所有调优的观测入口。

| 手段 | 建议 |
|------|------|
| 内存吃紧 | 默认 2048 MiB 封顶已按泄漏史保守定价；真不够再用 `CBM_MEM_BUDGET_MB` 显式上调 |
| watcher 太吵/太频繁 | 先看 `index_status` 的 `freshness`；调 `CBM_WATCH_COOLDOWN_S`（如 120），或直接 `config set auto_watch false` 退回显式索引工作流 |
| 冷启 5s 烦人 | 攒批问；或给 agent 装 SessionStart hook 让索引在会话开始前就绪 |
| 索引太大 | `index_max_files` / `index_max_source_mb` 设上限；超限整个索引请求失败并保留原服务 DB |
| 磁盘只增不减 | fork 已带 retention sweep（跳过日志/临时文件/FTS 死行回收）；`index_status` 的 `maintenance` 块给计数，`fts_rows > 3x nodes_rows` 时提示重建——重建只能靠全量重索引，自己决定时机 |
| UI 用不用 | 用就 `config set ui_enabled true`，不用就不用管——它不会自己起来 |

## 排障

| 报错/现象 | 性质 | 处置 |
|----------|------|------|
| `secure daemon endpoint could not be created` | rendezvous 目录祖先没过安全检查（capability-SID ACE、宽松 /tmp 等） | `CBM_RUNTIME_DIR` 指到一个你拥有的目录；所有要共享 daemon 的进程环境都要设同一值 |
| CLI 调工具报 `disabled by config` 非零退出 | 该工具在 `tools_disabled` 名单或不在当前 profile | `config list` 查名单；`config reset tools_disabled` 或 MCP 侧 `--tool-profile=all` |
| `query_graph` 报未知属性错误 | fork 的目录校验在执行前拦截（上游会静默返回 `total: 0`） | 按报错提示跑 `get_graph_schema` 查可用属性；这是 fork 在帮你，不是坏了 |
| `detect_changes` 报非法 `direction`/`scope` | teaching-error（fork 补齐的校验） | 用报错文案里列出的合法值 |
| trace 结果 `caller_resolution` 显示 unresolved | 框架/DI 派发对静态解析隐形，是图谱固有边界不是故障 | 读注记文本；用 `include_evidence=true` 交叉核对，别把 0 当「没有调用方」 |
| 图谱结果与代码对不上 | 索引过期或文件解析失败 | `cli index_repository --repo-path . --mode fast` 重索引；先 `check_index_coverage` 看 `parse_partial` |
| watcher 迟迟不索引 | 双轮确认 + 冷却在起作用（脏变更晚 5-60s） | 设计如此；commit 单轮即触发。赶时间直接手动索引 |
| 改了 `watcher_enabled` 不生效 | 该键 daemon 启动时读一次 | `daemon stop` 让下个会话起新 daemon |
| config 命令全挂且日志报 `config.local.corrupt` | `.cbm/config.json` 损坏 | 修或删该文件（损坏会被忽略，不至于挂；挂是别的原因，先查 daemon 日志 `${CBM_CACHE_DIR}/logs/cbm-daemon.log`） |

总原则：**显式报错都是 fork 在拒绝含糊输入，按文案修参数即可；真正危险的是静默空结果——遇到「空但没报错」，先查 `freshness`/`coverage`，再 grep 复核，最后才信图谱。**

## 测试与开发

- 构建与测试配方：[BUILD_WINDOWS.md](BUILD_WINDOWS.md)（Windows）/ `make -f Makefile.cbm test-par`（POSIX）
- 改默认值语义的测试断言**必须**同步改（断言新契约）；fork 补丁逐文件清单与验证矩阵见 [FORK_PATCHES.md](FORK_PATCHES.md)
- 智能体协作约定（目录结构、坑位清单、rebase 检查点）见 [AGENTS.md](../AGENTS.md)
