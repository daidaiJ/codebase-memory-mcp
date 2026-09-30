# codebase-memory-mcp (fork)

> 代码库知识图谱 MCP 服务的一个精简 fork — MCP 面默认只暴露 4 个经实测验证的工具，常驻资源全部 opt-in，拒绝与诚实性全部显式

上游项目：[DeusData/codebase-memory-mcp](https://github.com/DeusData/codebase-memory-mcp)（C 语言，tree-sitter + mimalloc）。本 fork 维护一条补丁分支（`feat/minimal-tool-surface`），改动原则：

1. **单用户本地工具** —— 常驻资源（文件监听、UI HTTP 服务）和自我激活行为一律 opt-in，不默认开启
2. **不与智能体自带工具重复** —— grep / 符号索引 / 代码导航已有专门工具，MCP 面只保留图谱独有的能力
3. **fail-loud** —— 策略性拒绝必须显式报错退出，禁止加入「静默空结果」家族
4. **策略可下放** —— 全局配置之外，项目目录可以带自己的 `.cbm/config.json` 策略文件

---

## 与上游的差异一览

| 维度 | 上游默认 | 本 fork 默认 | 说明 |
|------|---------|-------------|------|
| MCP 工具面 | 全部 17 个工具 | **4 个**：`search_graph` / `query_graph` / `get_architecture` / `get_graph_schema` | 精确符号发现 + 复杂度排行 + 架构总览 + 属性目录发现；`--tool-profile=all` 恢复全量（fork issue #5：`detect_changes` 换出，待可信度修复；`get_graph_schema` 为 2026-09-30 增补，服务无 skill 客户端的 Cypher 属性发现） |
| `auto_watch` | `true`（会话连接即注册文件监听） | **`false`** | 常驻 watcher 是会话期最大资源户，显式索引工作流下纯冗余 |
| 日志级别 | role-aware（前端 `warn` / daemon `info`） | 库内静态默认 **`error`** | 上游 v0.10+ 已改 role-aware 启动策略，fork 保留更严的库级兜底 |
| 内存预算 | RAM × 25%/35%/50%，无上限 | **封顶 2048 MiB** | 32GB 机器上游默认拿 11.4GB；`CBM_MEM_BUDGET_MB` 仍可显式上调 |
| 图谱 UI | 首次运行自动启用 HTTP 服务 | **关闭**，需显式开启 | 零使用意图不应自我激活环回端口 |
| 工具禁用 | 无 | **`tools_disabled`** 配置键 | MCP/CLI 双侧生效、fail-loud、`--help` 同步隐藏 |
| Windows DACL | 祖先链有宽松 ACL 即拒启 | **默认跳过**该遍历，属主校验保留；`CBM_DACL_HARDENING=1` 开回 | 单用户本地工具不该被共享工具目录的 ACL 挡在门外 |

技术细节与逐文件修改点见 [docs/FORK_PATCHES.md](docs/FORK_PATCHES.md)。

## 设计思路

上面的差异不是散点调参，而是四条互相咬合的原则（每条在 fork issues 里立项、
`docs/FORK_PATCHES.md` 逐文件落实、测试钉契约）：

1. **面上的工具靠对照实验活下来，不是靠存在**。每保留一个工具都先回答：grep /
   codegraph / 客户端自带工具做这件事要多少次调用？只有图谱独有能力（架构概览、
   全仓复杂度排行、符号精确发现、属性目录）才值得占 MCP 面；与 Read/grep 重叠的
   一律换出，还省掉每次会话冷启。`tools/list` 的 description 同理只留触发语 +
   判读契约（三工具 -40% wire 开销），分页/续读等机制细节下沉到配套 skill 按需加载。
2. **常驻资源与自我激活一律 opt-in**。这是单用户本地工具，不是服务：文件监听、
   UI 环回 HTTP、会话自动索引、info 级日志都不该在「零使用意图」时自己转起来。
   fork 还给 opt-in 回来的 watcher 重新定了价：一次 watcher 触发的 delta 接近一次
   全量增量索引，所以「首次看见就索引」是错的成本模型——脏变更要两轮签名确认
   （editor 写文件突发只付一次钱），成功索引后冷却 30s，commit 单轮即触发。
3. **fail-loud + 带内诚实性**。拒绝必须非零退出 + 显式 message；响应里的每个
   「空/零」都必须可判读——`callers_total: 0` 旁边带 `caller_resolution`
   （2/3 resolved，1 unresolved），`impacted: []` 旁边带 `resolution_caveat`
   （CALLS 边是解析启发式，0 影响不等于无影响），`index_status` 的 `freshness`
   块在没有信号时整块缺席（缺席 = 没数据，从不伪装新鲜）。
4. **策略下放 + CLI 全量是根基**。被裁剪的是 MCP 面，不是能力：CLI 保留全部
   17 工具（daemon 内部会话始终 ALL），全局 `_config.db` 之上项目可带自己的
   `.cbm/config.json`；策略拒绝在 daemon 冷启之前完成（不花 5s 才知道工具没开）。

## 修复的上游坏取舍

| 领域 | 上游取舍 | 为什么不好 | fork 的修复 |
|------|---------|-----------|------------|
| 工具面 | 17 个工具全量常驻 `tools/list` | 与客户端 Read/grep 重复的工具白占每次会话的上下文，还稀释真正有用的触发信号 | 默认 minimal 面（4 工具）+ `tools_disabled` 名单 + profile 三处一致（客户端/守护进程/wire 边界） |
| 失败模式 | 枚举参数静默兜底：`scope:"bogus"` 按 files 语义跑完、未知 Cypher 属性静默返回 `total: 0` | **权威假阴性**：调用方要符号爆炸半径拿到的是 changed files，还以为查过了 | 全部改 teaching-error（错误文案列出合法值）；query_graph 执行前做属性目录校验，fail-open 只放过它无法判断的查询 |
| 可信度 | `trace_path` 返回 `callers_total: 0`，「没有调用方」与「调用边解析失败」不可分（框架/DI 派发对静态解析隐形） | 字段读成否定结论，agent 据此下错判断 | 带内 `caller_resolution` 摘要 + 半数未解析时固定教学注记；detect_changes 同型补 `resolution_caveat` |
| 内存 | RAM × 25%-50%，无上限 | 32GB 机器默认拿 11.4GB，而上游自身的泄漏史并未清零 | 默认封顶 2048 MiB；`CBM_MEM_BUDGET_MB` 显式设置仍可上调到物理内存 |
| watcher | 首次看见脏变更就索引 | 一次触发 ≈ 一次全量增量索引的成本，编辑器写文件突发会连着触发多次 | 双轮签名确认 + 成功后 30s 冷却 + commit 单轮例外 + `index_status.freshness` 让新鲜度可观测（新鲜度锚只用聚合口径，禁止 per-file mtime） |
| 存储 | 缓存与 store 只增不减 | 跳过日志无界、崩溃残留 temp/staging、contentless-FTS 死行和孤儿 lsp_surface 永不回收 | 纯减法 retention sweep（常数不参数化）+ `index_status` 的 `maintenance` 计数与 rebuild 建议——数字给维护者，不自动重建 |
| 深遍历 | LIMIT 不省递归 CTE，全深度可达集照样枚举 | 想省只能盲跑或不跑，没有「更深还有一层」的信号 | `depth_frontier` + `deepen_cursor`：更深一层是 agent 主动 PULL（c2 token 把 depth 移出身份哈希，收紧 depth 参数不可能绕过） |
| Windows DACL | 祖先链存在宽松 ACL 即拒绝启动 | 共享工具目录（如 `D:\tool-cli`）继承的 Authenticated Users ACE 把单用户工具挡在门外，且用户无法控制祖先目录 | 默认跳过不可信 ACE 遍历，属主校验两种模式都保留；`CBM_DACL_HARDENING=1` 在多用户主机开回 |
| 工具描述 | 全部机制细节常驻 `tools/list` description | 常驻 wire 开销既稀释触发信号又烧上下文，而细则只有工具被选中后才需要 | 描述 = 触发语 + 判读契约；参数语义只住在 inputSchema；机制与组合配方进配套 skill 渐进披露 |

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

CLI 保留全部 17 个工具（除非被 `tools_disabled` 禁用）——被裁剪的是 **MCP 面**，不是 CLI 面。

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

默认即最小面（4 工具）。要全量：`"args": ["--tool-profile=all"]`；可选 `minimal`（默认）/ `analysis` / `scout`。

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
| `CBM_WATCH_COOLDOWN_S` | `30` | watcher 成功索引后的冷却秒数（fork issue #10）：窗口内不触发，脏变更只暂存；`0` 关闭。commit 单轮即触发，不受双轮确认限制 |
| `CBM_WATCHER_PRUNE_GRACE_S` | `600` | watched 项目根目录持续缺失多久后清理其缓存 DB |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | 索引与配置存储目录 |

## 什么时候用 cbm，什么时候用 grep/符号工具

4 个保留工具各自有 grep/codegraph 复刻不了的证据：

- **`get_architecture`** —— 扇入热点、边界权重、分层、模块聚类，grep 需要十几次调用才能拼出来
- **`query_graph`** —— 全仓复杂度排行（cognitive 维度），LOC 类指标给不出
- **`search_graph`** —— 符号级精确发现（fork issue #5 换入；社区使用量第二），grep 容易漏重名/别名场景
- **`get_graph_schema`** —— 节点/边目录与可查属性发现（2026-09-30 增补）：query_graph 对未知属性静默返回空（实测 `WHERE f.bogus = 1` 得 `total: 0`），无 skill 客户端靠它把「拼错属性」和「真没有」区分开

`detect_changes`（diff 驱动的影响半径）已移出默认面——它是面上可信度最低的工具（上游 #2128「dead verdict」家族），仍保留在 `analysis`/`all` 面，等其可信度修复后再议是否回归。

其余场景（单符号定位、调用链、精确源码、文本搜索）直接用本地工具更快：零冷启、看得到非符号内容。

## 文档导航

各文档均为中文默认，同名 `.en.md` 为英文版。

| 读者 | 文档 | 内容 |
|------|------|------|
| 👤 人类用户 | [docs/HUMAN_GUIDE.md](docs/HUMAN_GUIDE.md) | 安装部署、三层配置全参数、watcher/内存调优、排障表 |
| 🤖 AI Agent | [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) | 省 token 版：何时用/不用 cbm、命令档位、机器契约（诚实性字段）、错误速诊 |
| | [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | 配置参考（含 tool profiles 与三层优先级） |
| | [docs/FORK_PATCHES.md](docs/FORK_PATCHES.md) | fork 补丁清单：动机、修改点、验证矩阵 |
| | [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md) | 上游原版 README（架构、安装、全部工具说明） |
| | [AGENTS.md](AGENTS.md) | 智能体协作指南（目录结构、开发约定、坑位清单） |
| | [skills/cbm/SKILL.md](skills/cbm/SKILL.md) | Agent Skill：MCP 最小面工具细则的渐进披露载体 |
