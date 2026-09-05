# codebase-memory-mcp (fork)

> 代码库知识图谱 MCP 服务的一个精简 fork — 默认只暴露 3 个经实测验证的工具，其余默认关闭或可配置禁用

上游项目：[DeusData/codebase-memory-mcp](https://github.com/DeusData/codebase-memory-mcp)（C 语言，tree-sitter + mimalloc）。本 fork 维护一条补丁分支（`feat/minimal-tool-surface`），改动原则：

1. **单用户本地工具** —— 常驻资源（文件监听、UI HTTP 服务）和自我激活行为一律 opt-in，不默认开启
2. **不与智能体自带工具重复** —— grep / 符号索引 / 代码导航已有专门工具，MCP 面只保留图谱独有的能力
3. **fail-loud** —— 策略性拒绝必须显式报错退出，禁止加入「静默空结果」家族
4. **策略可下放** —— 全局配置之外，项目目录可以带自己的 `.cbm/config.json` 策略文件

---

## 与上游的差异一览

| 维度 | 上游默认 | 本 fork 默认 | 说明 |
|------|---------|-------------|------|
| MCP 工具面 | 全部 15 个工具 | **3 个**：`get_architecture` / `query_graph` / `detect_changes` | 经 grep + codegraph 基线对照实测保留；`--tool-profile=all` 恢复全量 |
| `auto_watch` | `true`（会话连接即注册文件监听） | **`false`** | 常驻 watcher 是会话期最大资源户，显式索引工作流下纯冗余 |
| 日志级别 | `info` | **`error`** | info/warn 级噪声每次冷启都打到 CLI stderr |
| 内存预算 | RAM × 25%/35%/50%，无上限 | **封顶 2048 MiB** | 32GB 机器上游默认拿 11.4GB；`CBM_MEM_BUDGET_MB` 仍可显式上调 |
| 图谱 UI | 首次运行自动启用 HTTP 服务 | **关闭**，需显式开启 | 零使用意图不应自我激活环回端口 |
| 工具禁用 | 无 | **`tools_disabled`** 配置键 | MCP/CLI 双侧生效、fail-loud、`--help` 同步隐藏 |
| Windows DACL | 祖先链有宽松 ACL 即拒启 | **默认跳过**该遍历，属主校验保留；`CBM_DACL_HARDENING=1` 开回 | 单用户本地工具不该被共享工具目录的 ACL 挡在门外 |

技术细节与逐文件修改点见 [docs/FORK_PATCHES.md](docs/FORK_PATCHES.md)。

## 快速开始

### 构建

CI 构建走 `.github/workflows/`（windows-amd64 精简 workflow），产物放 `~/tools/codebase-memory-mcp/`。源码构建：

```bash
make -f Makefile.cbm        # 目标与上游一致，见上游 README（docs/UPSTREAM_README.md）
```

### CLI 用法（推荐入口）

```bash
codebase-memory-mcp cli --help              # 列出可用工具（已过滤被禁工具）
codebase-memory-mcp cli get_architecture --help   # 单工具参数说明
codebase-memory-mcp cli query_graph --tool complex-ranking --limit 20
codebase-memory-mcp cli detect_changes      # diff 驱动的影响半径
```

CLI 保留全部 15 个工具（除非被 `tools_disabled` 禁用）——被裁剪的是 **MCP 面**，不是 CLI 面。

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

默认即最小面（3 工具）。要全量：`"args": ["--tool-profile=all"]`；可选 `minimal`（默认）/ `analysis` / `scout`。

## 核心配置

### 三层配置与优先级

| 层级 | 位置 | 作用域 |
|------|------|--------|
| 1. 项目本地 | `<项目>/.cbm/config.json` | 仅该项目，**优先级最高** |
| 2. 全局存储 | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db` | 当前用户所有项目 |
| 3. 内置默认 | 编译进二进制 | 兜底 |

**`.cbm/config.json` 示例**（手写即可，JSON 对象，键与 `config` 子命令一致）：

```json
{
  "tools_disabled": "search_graph,trace_path,get_code_snippet"
}
```

- 命中的工具从 MCP `tools/list`、`--help` 工具列表中消失；按名调用时 CLI 非零退出、MCP 返回显式错误（绝不静默）
- 文件不存在 → 跳过此层；文件损坏 → warn 日志并忽略（stdout 不受污染）
- `config set tools_disabled xxx` 会校验工具名，拼错直接报错

### `config` 子命令

```bash
codebase-memory-mcp config list                       # 全部键 + 当前值 + 默认值
codebase-memory-mcp config set auto_watch true        # 恢复上游的 watcher 行为
codebase-memory-mcp config set tools_disabled search_graph,trace_path
codebase-memory-mcp config set ui_enabled true        # 开启图谱 UI
codebase-memory-mcp config reset tools_disabled       # 恢复默认
```

| 键 | fork 默认 | 说明 |
|----|----------|------|
| `tools_disabled` | *(空)* | 逗号分隔的禁用工具名单，双侧生效 |
| `auto_watch` | `false` | 会话连接时注册 git 文件监听 |
| `auto_index` | `false` | MCP 会话启动时自动索引新项目 |
| `watcher_enabled` | `true` | watcher 子系统总开关（守护进程启动时读取） |
| `ui_enabled` / `ui_port` | `false` / `9749` | 图谱 UI 环回 HTTP 服务 |

完整键表与环境变量见 [docs/CONFIGURATION.md](docs/CONFIGURATION.md)。

### 环境变量速查（fork 相关）

| 变量 | 默认 | 说明 |
|------|------|------|
| `CBM_LOG_LEVEL` | `error` | `debug`/`info`/`warn`/`error`/`none` 或 `0`-`4` |
| `CBM_MEM_BUDGET_MB` | RAM 分数，**封顶 2048** | 显式设置可超过封顶（上限为物理内存） |
| `CBM_DACL_HARDENING` | *(未设，即跳过)* | Windows 专用，fork：默认跳过缓存目录的不可信 ACE 遍历（`D:\tool-cli` 这类祖先目录带宽松 ACL 也照常启动，属主校验仍生效）。多用户/终端服务器主机设 `=1` 开回严格检查 |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | 索引与配置存储目录 |

## 什么时候用 cbm，什么时候用 grep/符号工具

3 个保留工具各自有 grep/codegraph 复刻不了的证据：

- **`get_architecture`** —— 扇入热点、边界权重、分层、模块聚类，grep 需要十几次调用才能拼出来
- **`query_graph`** —— 全仓复杂度排行（cognitive 维度），LOC 类指标给不出
- **`detect_changes`** —— 唯一 diff 驱动的影响半径形态

其余场景（单符号定位、调用链、精确源码、文本搜索）直接用本地工具更快：零冷启、看得到非符号内容。

## 文档导航

| 文档 | 内容 |
|------|------|
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | 配置参考（含 tool profiles 与三层优先级） |
| [docs/FORK_PATCHES.md](docs/FORK_PATCHES.md) | fork 补丁清单：动机、修改点、验证矩阵 |
| [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md) | 上游原版 README（架构、安装、全部工具说明） |
| [AGENTS.md](AGENTS.md) | 智能体协作指南（目录结构、开发约定、坑位清单） |
| [skills/cbm/SKILL.md](skills/cbm/SKILL.md) | Agent Skill：何时用/不用 cbm、安装方法、三个实测有效的命令配方与硬规则 |
