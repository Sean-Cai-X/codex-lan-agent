# codex-lan-agent

基于 Clang AST 的 C/C++ 代码分析 MCP 工具链，提供 AST 解析、控制流图（CFG）、调用图（Call Graph）、数据流图（DFG）和程序切片（Program Slice）能力，通过 MCP 协议（Streamable HTTP）对外服务。

---

## 目录

1. [架构概览](#1-架构概览)
2. [构建与启动](#2-构建与启动)
3. [MCP 协议接入](#3-mcp-协议接入)
4. [工具清单与参数](#4-工具清单与参数)
5. [接入本地模型后的操作语义](#5-接入本地模型后的操作语义)
6. [完整使用案例](#6-完整使用案例)
7. [Artifact 二次查询与分页](#7-artifact-二次查询与分页)
8. [测试脚本与一键验证](#8-测试脚本与一键验证)
9. [测试结论](#9-测试结论)
10. [CMM 工具清单](#10-cmm-工具清单)
11. [Clang 分析工具 vs CMM 工具功能对比](#11-clang-分析工具-vs-cmm-工具功能对比)
12. [常见问题排查](#12-常见问题排查)

---

## 1. 架构概览

```
AI Agent / IDE / 本地模型
        │  JSON-RPC over HTTP (POST /mcp)
        ▼
┌───────────────────────────────────┐
│        codex_lan_agent.exe        │
│        (MCP Server :18080)        │
├───────────────────────────────────┤
│  McpProtocolOperations.h          │  ← 工具列表 / Schema 暴露
│  McpToolDispatch.h                │  ← 工具分发
│  ClangIndexerAdapter.cpp          │  ← 适配层
├───────────────────────────────────┤
│  ClangAstParser.cpp               │  ← AST 解析
│  CfGBuilder.cpp                   │  ← CFG 构建
│  GraphSerialization.cpp           │  ← CallGraph / DFG / Slice
├───────────────────────────────────┤
│  compile_commands.json            │  ← 编译数据库（项目侧）
│  Clang / LLVM                     │  ← 底层解析引擎
└───────────────────────────────────┘
```

**核心设计原则：**
- 所有代码分析工具共享同一个 Clang Tooling 执行核心。
- 复杂项目通过 `compile_commands.json` 提供编译参数，不硬编码环境。
- 每个工具产出标准化 JSON artifact，支持二次查询（artifact query）。
- 分页（`offset_*` / `max_*`）和邻域提取（`focus_symbol` / `neighborhood_depth`）在 artifact 层完成，无需重跑 Clang。

---

## 2. 构建与启动

### 2.1 前置条件

- **编译器**：MSVC（推荐）或 MinGW
- **依赖**：Clang/LLVM（随项目构建链集成）
- **构建系统**：CMake

### 2.2 编译

```powershell
cd D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent
cmake -B AIbuild -G "Visual Studio 17 2022" -A x64
cmake --build AIbuild --config Release
```

### 2.3 启动服务

```powershell
# 清理可能残留的旧进程（端口 18080 占用是启动失败最常见原因）
Get-Process codex_lan_agent -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

# 启动
cd AIbuild\Release
.\codex_lan_agent.exe --config "..\..\codex_lan_agent.cfg" serve --machine-code 8EE5-2336-71AE-74DD
```

### 2.4 配置文件 (`codex_lan_agent.cfg`)

关键字段：

| 字段 | 说明 |
|---|---|
| `listen_host` / `listen_port` | 监听地址，默认 `0.0.0.0:18080` |
| `workspace_root` | 工作区根目录 |
| `generation_endpoint` | 本地模型 chat completions 端点 |
| `embedding_endpoint` | 本地模型 embeddings 端点 |
| `local_chat_endpoint` | 本地对话端点 |
| `task_timeout_sec` | 任务超时（秒），默认 1800 |

---

## 3. MCP 协议接入

### 3.1 传输方式

- **协议**：JSON-RPC 2.0
- **传输**：Streamable HTTP（`POST /mcp`）
- **无需 OAuth**：本地部署，`auth_required=false`

### 3.2 基础请求格式

```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "tools/list",
  "params": {}
}
```

```json
{
  "jsonrpc": "2.0",
  "id": "2",
  "method": "tools/call",
  "params": {
    "name": "lan_agent_build_cfg",
    "arguments": {
      "source_file": "D:/path/to/file.cpp",
      "project_root": "D:/path/to/project"
    }
  }
}
```

### 3.3 IDE / Agent 接入

在支持 MCP 的 IDE 或 Agent 中配置：

```json
{
  "mcpServers": {
    "codex-lan-agent": {
      "url": "http://127.0.0.1:18080/mcp",
      "transport": "streamable-http"
    }
  }
}
```

### 3.4 PowerShell 快速验证

```powershell
# 验证服务存活 + 工具列表
$body = '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8"
$response.result.tools.Count  # 应输出工具总数
```

---

## 4. 工具清单与参数

### 4.1 代码分析工具（核心 10 个）

| 工具 | 功能 | 必需参数 |
|---|---|---|
| `lan_agent_run_clang_ast_parser` | Clang AST 解析，返回函数/类/调用引用 | `source_file` |
| `lan_agent_build_cfg` | 控制流图构建（基本块+边+圈复杂度） | `source_file` |
| `lan_agent_query_cfg_artifact` | CFG artifact 二次查询 | `artifact_json_path` 或 `artifact_summary_path` |
| `lan_agent_build_call_graph` | 调用图构建 | `source_file` |
| `lan_agent_query_call_graph_artifact` | CallGraph artifact 二次查询 | `artifact_json_path` 或 `artifact_summary_path` |
| `lan_agent_build_dfg` | 数据流图构建（AST statement-level def/use） | `source_file` |
| `lan_agent_query_dfg_artifact` | DFG artifact 二次查询 | `artifact_json_path` 或 `artifact_summary_path` |
| `lan_agent_build_program_slice` | 程序切片（backward/forward） | `source_file`, `symbol` |
| `lan_agent_query_program_slice_artifact` | Slice artifact 二次查询 | `artifact_json_path` 或 `artifact_summary_path` |

### 4.2 通用参数说明

| 参数 | 类型 | 说明 |
|---|---|---|
| `source_file` | string | 目标 C/C++ 源文件路径（正斜杠） |
| `project_root` | string | 项目根目录，用于自动发现 `compile_commands.json` |
| `compile_db_dir` | string | `compile_commands.json` 所在目录 |
| `compilation_database_path` | string | `compile_commands.json` 完整路径 |
| `extra_include_dirs` | string | JSON 数组字符串，额外 include 目录 |
| `extra_defines` | string | JSON 数组字符串，额外宏定义 |
| `output_dir` | string | artifact 输出目录（写 `*.json` / `*.dot` / `summary.json`） |
| `include_dot` | boolean | 是否在响应中包含 DOT 文本 |
| `focus_symbol` | string | 聚焦符号，提取邻域子图 |
| `neighborhood_depth` | integer | 邻域遍历深度，默认 1 |
| `neighborhood_direction` | string | `incoming` / `outgoing` / `both`，默认 `both` |
| `max_nodes` | integer | 返回最大节点数，0=不限 |
| `max_edges` | integer | 返回最大边数，0=不限 |
| `offset_functions` / `offset_edges` | integer | 分页偏移 |
| `max_functions` | integer | CFG 返回最大函数数 |
| `include_path_metadata` | boolean | 是否计算 path-sensitive 元数据（CFG 分支/环） |
| `max_interprocedural_bindings` | integer | 过程间绑定候选最大数，默认 512 |

### 4.3 compile_commands.json 发现顺序

1. 显式 `compilation_database_path` → 直接使用
2. 显式 `compile_db_dir` → 拼接 `compile_commands.json`
3. `project_root` + 常见构建目录 → 自动搜索 `build/`, `AIbuild/`, `cmake-build-*/`
4. 均未找到 → 降级为无编译数据库模式（`compile_db_mode=none`，复杂文件可能失败）

### 4.4 CMM 工具清单（codebase-memory-mcp 桥接）

CMM（Codebase Memory MCP）工具通过 `codex_lan_agent` 桥接到独立的 `codebase-memory-mcp` 服务，提供基于**预建索引**的项目级代码图查询能力。使用前需先调用 `lan_agent_cmm_index_repository` 建立索引。

| 工具 | 功能 | 必需参数 |
|---|---|---|
| `lan_agent_cmm_list_projects` | 列出 CMM 已索引的所有项目 | — |
| `lan_agent_cmm_index_status` | 查询指定项目的索引状态 | `project` |
| `lan_agent_cmm_index_repository` | 索引一个仓库到 CMM | `repo_path`, `name` |
| `lan_agent_cmm_delete_project` | 从 CMM 删除已索引项目 | `project` |
| `lan_agent_cmm_search_code` | 代码文本搜索（支持正则、文件过滤） | `project` |
| `lan_agent_cmm_search_graph` | 图节点搜索（按 label/kind/relationship） | `project` |
| `lan_agent_cmm_query_graph` | 图查询（类 Cypher 查询） | `project`, `query` |
| `lan_agent_cmm_trace_path` | 调用链/依赖链路径追踪 | `project` |
| `lan_agent_cmm_get_code_snippet` | 按 qualified_name 获取代码片段 | `project` |
| `lan_agent_cmm_get_graph_schema` | 获取项目图数据库 schema | `project` |
| `lan_agent_cmm_get_architecture` | 项目架构分析（模块依赖、分层） | `project` |
| `lan_agent_cmm_detect_changes` | 分支变更检测与影响分析 | `project` |

#### CMM 通用参数说明

| 参数 | 类型 | 说明 |
|---|---|---|
| `project` | string | CMM 项目名称或绝对路径（自动归一化） |
| `query` / `pattern` | string | 搜索查询字符串或正则模式 |
| `file_pattern` | string | 文件 Glob 过滤（如 `*.cpp`） |
| `path_filter` | string | 路径正则过滤（如 `cximage/`） |
| `limit` | integer | 返回结果数量上限，默认 10 |
| `offset` | integer | 分页偏移 |
| `context` | integer | 代码上下文行数 |
| `mode` | string | `compact` / `full` / `files` |
| `depth` | integer | 路径追踪深度 |
| `direction` | string | 路径方向 |

#### CMM 索引建立示例

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "cmm-index"
    method = "tools/call"
    params = @{
        name = "lan_agent_cmm_index_repository"
        arguments = @{
            repo_path = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxvision_repo"
            name = "cxvision"
            include = @("*.cpp", "*.h", "*.hpp")
            exclude = @("third_party/", "build/")
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 300
```

#### CMM 代码搜索示例

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "cmm-search"
    method = "tools/call"
    params = @{
        name = "lan_agent_cmm_search_code"
        arguments = @{
            project = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxvision_repo"
            query = "center_x"
            path_filter = "cximage/"
            file_pattern = "*.cpp"
            limit = 20
            context = 3
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 60
```

---

## 5. 接入本地模型后的操作语义

### 5.1 语义模型

本地模型（LLM）作为**调用方**，通过 MCP 协议与 `codex_lan_agent` 交互。完整的操作语义链：

```
用户意图
  → 本地模型理解意图，选择 MCP 工具
  → 构造 JSON-RPC tools/call 请求
  → codex_lan_agent 执行 Clang 分析
  → 返回 structuredContent（JSON + 元数据）
  → 本地模型解读结果，生成自然语言报告或下一步操作
```

### 5.2 操作语义分层

| 层级 | 操作 | 语义 |
|---|---|---|
| L0 | `tools/list` | 发现可用工具和能力 |
| L1 | `lan_agent_run_clang_ast_parser` | 获取文件级 AST 概览（函数列表、类结构、调用引用） |
| L2 | `lan_agent_build_cfg` | 获取函数级控制流（基本块、分支、圈复杂度） |
| L3 | `lan_agent_build_call_graph` | 获取文件级调用关系 |
| L4 | `lan_agent_build_dfg` | 获取数据流（def/use 边、过程间绑定） |
| L5 | `lan_agent_build_program_slice` | 获取符号级切片（backward/forward） |
| L6 | `lan_agent_query_*_artifact` | 从已写入的 artifact 二次查询，无需重跑 Clang |

### 5.3 模型决策规则

本地模型应遵循以下决策规则：

1. **先 discovery，再 analysis**：先调用 `tools/list` 确认工具可用。
2. **先 AST，再 deep analysis**：先解析 AST 获取文件结构概览，再决定是否构建 CFG/DFG/Slice。
3. **先 build，再 query**：首次分析调用 `build_*` 写入 artifact；后续分页/聚焦查询调用 `query_*_artifact`。
4. **复杂项目必传 `project_root`**：确保 `compile_commands.json` 被发现，否则复杂文件解析失败。
5. **大文件分页**：使用 `offset_*` / `max_*` 控制返回量，避免单次响应过大。
6. **超时意识**：DFG/Slice 对复杂文件可能需要 180-300s，模型应设置足够超时。

### 5.4 典型模型对话流程

```
用户：分析 FastMatch.cpp 中 center_x 的数据流

模型内部决策：
  1. tools/list → 确认 lan_agent_build_dfg 可用
  2. lan_agent_run_clang_ast_parser(source_file=FastMatch.cpp, project_root=cxvisionai)
     → 确认文件可解析，获取函数列表
  3. lan_agent_build_dfg(source_file=FastMatch.cpp, project_root=cxvisionai,
                         focus_symbol=center_x, neighborhood_depth=2)
     → 获取 center_x 的数据流子图
  4. 解读 dfg_json 中的节点和边，生成自然语言报告

模型输出：
  "center_x 在 FastMatch.cpp 中被定义于第 45 行（learn 阶段），
   在第 78 行被使用（match 阶段），
   过程间绑定：learn → match 通过参数传递..."
```

---

## 6. 完整使用案例

### 案例一：简单文件 CFG 构建

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "cfg-simple"
    method = "tools/call"
    params = @{
        name = "lan_agent_build_cfg"
        arguments = @{
            source_file = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/test_simple.cpp"
            include_dot = $false
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 60
$content = $response.result.structuredContent
Write-Host "status=$($content.status)"                    # success
Write-Host "total_functions=$($content.total_functions)"  # 2
Write-Host "total_blocks=$($content.total_blocks)"        # 7
Write-Host "total_edges=$($content.total_edges)"          # 7
```

### 案例二：复杂项目 AST 解析

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "ast-fastmatch"
    method = "tools/call"
    params = @{
        name = "lan_agent_run_clang_ast_parser"
        arguments = @{
            source_file = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxvision_repo/cximage/FastMatch.cpp"
            project_root = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai"
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 120
$content = $response.result.structuredContent
# status=success
# compile_db_mode=compile_commands_json
# resolved_compile_db_dir=D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build
# filtered_to_source_file=true
# function_count=2600+
```

### 案例三：DFG 构建 + 过程间绑定

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "dfg-fastmatch"
    method = "tools/call"
    params = @{
        name = "lan_agent_build_dfg"
        arguments = @{
            source_file = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxvision_repo/cximage/FastMatch.cpp"
            project_root = "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai"
            focus_symbol = "center_x"
            neighborhood_depth = 2
            neighborhood_direction = "both"
            include_dot = $false
            max_nodes = 80
            max_edges = 120
            max_interprocedural_bindings = 256
            output_dir = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/lan_agent_analysis_client_bundle"
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 300
$content = $response.result.structuredContent
# status=success
# analysis_level=ast_statement_v1
# dfg_precision=ast_statement_def_use_v1
# ast_readwrite_ref_count=177
# interprocedural_binding_status=callsite_argument_return_candidates_available
# interprocedural_binding_count=256
```

### 案例四：程序切片 + Path-sensitive 元数据

```powershell
$body = @{
    jsonrpc = "2.0"
    id = "slice-path"
    method = "tools/call"
    params = @{
        name = "lan_agent_build_program_slice"
        arguments = @{
            source_file = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/test_simple.cpp"
            symbol = "result"
            direction = "backward"
            include_path_metadata = $true
            include_dot = $false
            max_nodes = 20
            max_edges = 40
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 120
$content = $response.result.structuredContent
# status=success
# slice_precision=ast_statement_def_use_cfg_callgraph_v1
# path_sensitive_status=cfg_branch_metadata_available
# path_sensitive_precision=cfg_branch_successor_candidate_v1
# path_condition_candidate_count=4
# control_dependency_candidate_count=4
# cyclic_function_candidate_count=1
```

---

## 7. Artifact 二次查询与分页

### 7.1 工作流

```
首次分析                          后续查询
───────                          ────────
build_dfg                        query_dfg_artifact
  ├─ dfg.json          ◄────────── 读取
  ├─ dfg.dot                       ├─ 分页 (offset_edges / max_edges)
  └─ summary.json     ◄────────── ├─ 聚焦 (focus_symbol / neighborhood_depth)
                                   └─ 输出 (output_dir)
```

### 7.2 二次查询示例

```powershell
# 通过 summary.json 二次查询 DFG
$body = @{
    jsonrpc = "2.0"
    id = "dfg-query"
    method = "tools/call"
    params = @{
        name = "lan_agent_query_dfg_artifact"
        arguments = @{
            artifact_summary_path = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/lan_agent_analysis_client_bundle/summary.json"
            focus_symbol = "center_x"
            neighborhood_depth = 2
            include_dot = $false
            max_nodes = 60
            max_edges = 80
        }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec 60
$content = $response.result.structuredContent
# status=success
# artifact_json_path_resolved_from=artifact_summary_path
# artifact_parser_status=success
```

### 7.3 分页参数

| 参数 | 作用 |
|---|---|
| `offset_functions` | CFG：跳过前 N 个函数 |
| `max_functions` | CFG：最多返回 N 个函数 |
| `offset_edges` | DFG/CallGraph/Slice：跳过前 N 条边 |
| `max_edges` | DFG/CallGraph/Slice：最多返回 N 条边 |
| `max_nodes` | 所有图：最多返回 N 个节点 |

---

## 8. 测试脚本与一键验证

### 8.1 一键脚本 1：模板回归

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_analysis_templates_1_11.ps1
```

覆盖：`tools/list`、AST、CFG、CallGraph、DFG、Slice、artifact query、分页、path-sensitive metadata。

### 8.2 一键脚本 2：复杂项目分析

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\analysis_client_examples.ps1 `
  -SourceFile D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cximage\FastMatch.cpp `
  -ProjectRoot D:\Codex-WorkDir\Sean_WorkDir\cxvisionai `
  -FocusSymbol center_x
```

覆盖：复杂项目（FastMatch.cpp）完整分析链，DFG/Slice 的 `focus_symbol` 邻域提取、过程间绑定验证。

> **注意**：DFG 对复杂文件可能需要 180-300s，脚本默认超时 180s。如超时，手动以 300s 超时重跑。

### 8.3 手动 MCP 调用模板

```powershell
function Invoke-McpTool {
    param([string]$Name, [hashtable]$Arguments, [int]$TimeoutSec = 120)
    $body = @{
        jsonrpc = "2.0"
        id = $Name
        method = "tools/call"
        params = @{ name = $Name; arguments = $Arguments }
    } | ConvertTo-Json -Depth 16 -Compress
    Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $body -ContentType "application/json; charset=utf-8" -TimeoutSec $TimeoutSec
}
```

---

## 9. 测试结论

### 9.1 环境

| 项 | 值 |
|---|---|
| 仓库 | `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent` |
| 构建目录 | `AIbuild\Release` |
| 测试目标文件 | `cximage\FastMatch.cpp`（复杂项目） + `test_simple.cpp`（简单文件） |
| compile_commands.json | `D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build\compile_commands.json` |

### 9.2 工具可用性

| 工具 | 状态 | 结论 |
|---|---|---|
| `tools/list` | 可用 | 123 个工具已注册 |
| `lan_agent_run_clang_ast_parser` | 可用 | `status=success`, `compile_db_mode=compile_commands_json` |
| `lan_agent_build_cfg` | 可用 | `status=success`, 50 函数 / 262 块 / 262 边 |
| `lan_agent_query_cfg_artifact` | 可用 | `artifact_json_path_resolved_from=artifact_summary_path` |
| `lan_agent_build_call_graph` | 可用 | 80 节点 / 72 边 / 2427 调用引用 |
| `lan_agent_query_call_graph_artifact` | 可用 | artifact 二次查询成功 |
| `lan_agent_build_dfg` | 可用 | `analysis_level=ast_statement_v1`, 177 readwrite refs |
| `lan_agent_query_dfg_artifact` | 可用 | `artifact_parser_status=success` |
| `lan_agent_build_program_slice` | 可用 | `slice_precision=ast_statement_def_use_cfg_callgraph_v1` |
| `lan_agent_query_program_slice_artifact` | 可用 | artifact + source_lines 二次查询成功 |

### 9.3 核心断言结果

| 断言 | 结果 |
|---|---|
| `status=success` | PASS |
| `compile_db_mode=compile_commands_json` | PASS |
| `resolved_compile_db_dir` 非空 | PASS |
| `filtered_to_source_file=true` | PASS |
| DFG `analysis_level=ast_statement_v1` | PASS |
| DFG `dfg_precision=ast_statement_def_use_v1` | PASS |
| DFG `ast_readwrite_ref_count >= 0` | PASS (177) |
| DFG `interprocedural_binding_status=callsite_argument_return_candidates_available` | PASS |
| `interprocedural_bindings_json` 可解析 | PASS |
| binding 包含 `argument_symbols` / `argument_bindings` / `result_symbols` / `callee_return_symbols` | PASS |
| Slice `analysis_level=ast_statement_v1` | PASS |
| Slice `slice_precision=ast_statement_def_use_cfg_callgraph_v1` | PASS |
| `source_lines_json` 可解析 | PASS |
| `path_sensitive_status=cfg_branch_metadata_available` | PASS |
| `path_sensitive_precision=cfg_branch_successor_candidate_v1` | PASS |
| `path_conditions_json` / `control_dependencies_json` / `cyclic_functions_json` 可解析 | PASS |
| artifact query `artifact_json_path_resolved_from=artifact_summary_path` | PASS |
| artifact query `artifact_parser_status=success` | PASS |

### 9.4 已知限制

| 限制 | 详情 | 规避 |
|---|---|---|
| DFG 复杂文件耗时 | FastMatch.cpp DFG build 需 180-300s | 设置 `TimeoutSec=300` |
| 端口占用 | 旧进程残留导致新实例启动失败 | 启动前 `Stop-Process` |
| compile_commands.json 依赖 | 无编译数据库时复杂文件解析失败 | 确保 `project_root` 指向含 `build/compile_commands.json` 的目录 |

### 9.5 最终状态

**[Verified]** — MCP 工具链可用于复杂项目分析，全部核心断言通过。

---

## 10. CMM 工具状态

CMM 工具通过 `codex_lan_agent` 桥接 `codebase-memory-mcp` 服务，已在 MCP 工具列表中注册。使用前需确保：
1. `codebase-memory-mcp` 服务已独立运行。
2. 目标项目已通过 `lan_agent_cmm_index_repository` 完成索引。

CMM 工具状态：`[Implemented]` — Schema 已注册，依赖外部 CMM 服务实际可用性。

---

## 11. Clang 分析工具 vs CMM 工具功能对比

### 11.1 核心差异

| 维度 | Clang 分析工具 (L0-L6) | CMM 工具 (`lan_agent_cmm_*`) |
|---|---|---|
| **数据时效** | 实时解析源文件 | 基于预建索引（需先 `index_repository`） |
| **分析深度** | AST statement-level（语句级） | 图节点/关系级（函数、类、文件级） |
| **适用范围** | 单文件级（`source_file`） | 整个项目级（`project`/`repo`） |
| **底层引擎** | Clang/LLVM AST | codebase-memory-mcp 图数据库 |
| **典型耗时** | 简单文件 1-5s，复杂文件 180-300s | 毫秒级（索引已建） |

### 11.2 功能对照表

| 功能需求 | Clang 工具 | CMM 工具 | 说明 |
|---|---|---|---|
| **工具/项目发现** | `tools/list` (L0) | `cmm_list_projects` | 发现可用工具 vs 发现已索引项目 |
| **AST 解析** | `run_clang_ast_parser` (L1) | — | Clang 独有：函数列表、类结构、调用引用 |
| **控制流图 (CFG)** | `build_cfg` (L2) | — | Clang 独有：基本块、分支边、圈复杂度 |
| **调用图** | `build_call_graph` (L3) | `cmm_search_graph` / `cmm_trace_path` | Clang 实时单文件；CMM 项目级预建图 |
| **数据流图 (DFG)** | `build_dfg` (L4) | — | Clang 独有：def/use 边、过程间绑定 |
| **程序切片** | `build_program_slice` (L5) | — | Clang 独有：backward/forward 符号级切片 |
| **Path-sensitive 元数据** | `build_dfg/slice` + `include_path_metadata` | — | Clang 独有：CFG 分支条件、控制依赖 |
| **代码搜索** | — | `cmm_search_code` | CMM 独有：文本/正则搜索、文件过滤 |
| **图查询** | — | `cmm_query_graph` | CMM 独有：Cypher-like 图查询 |
| **路径追踪** | — | `cmm_trace_path` | CMM 独有：调用链/依赖链追踪 |
| **代码片段获取** | — | `cmm_get_code_snippet` | CMM 独有：按 qualified_name 定位代码 |
| **架构分析** | — | `cmm_get_architecture` | CMM 独有：模块依赖、分层分析 |
| **变更检测** | — | `cmm_detect_changes` | CMM 独有：对比分支差异 |
| **索引管理** | — | `cmm_index_repository` / `cmm_delete_project` | CMM 独有：项目索引生命周期 |
| **Artifact 二次查询** | `query_*_artifact` (L6) | — | Clang 独有：分页/聚焦查询无需重跑 |

### 11.3 使用场景对比

| 场景 | 推荐工具 | 原因 |
|---|---|---|
| 分析单个函数的控制流 | `build_cfg` (L2) | AST 级精确 CFG，含基本块和分支 |
| 分析变量 `center_x` 的数据流 | `build_dfg` (L4) | statement-level def/use，含过程间绑定 |
| 做程序切片（找符号影响范围） | `build_program_slice` (L5) | 精确到语句的 backward/forward 切片 |
| 跨文件查找谁调用了 `learn()` | `cmm_search_graph` / `cmm_trace_path` | 项目级调用链，无需逐个文件解析 |
| 搜索代码中的 TODO/FIXME | `cmm_search_code` | 文本搜索，支持正则和文件过滤 |
| 了解项目整体架构分层 | `cmm_get_architecture` | 模块依赖、分层、入口点分析 |
| 对比两个分支的变更影响 | `cmm_detect_changes` | 基于 git diff + 图分析 |
| 快速查询已分析结果（分页） | `query_*_artifact` (L6) | 毫秒级，无需重跑 Clang |

### 11.4 组合使用建议

```
复杂分析任务典型工作流：

1. 项目级定位（CMM）
   cmm_search_code(query="center_x") → 找到涉及的文件

2. 文件级深度分析（Clang）
   build_dfg(source_file=FastMatch.cpp, focus_symbol="center_x")
   → 获取精确的数据流和过程间绑定

3. 结果复用（Artifact Query）
   query_dfg_artifact(artifact_summary_path=...)
   → 分页查看、聚焦邻域，无需重跑
```

**互补关系**：CMM 适合**项目级快速定位**，Clang 工具适合**单文件深度语义分析**。两者结合可覆盖从宏观架构到微观语句的完整分析链路。

---

## 12. 常见问题排查

### 12.1 服务启动失败 / 连接拒绝

```
错误：Could not establish connection
```

**原因**：端口 18080 被旧进程占用。

**解决**：
```powershell
Get-Process codex_lan_agent -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
# 重新启动
```

### 12.2 工具不在 tools/list 中

**原因**：工具 schema 未在 `McpProtocolOperations.h` 的 `BuildMcpToolsListResponse` 中注册。

**解决**：检查 [src/McpProtocolOperations.h](file:///d:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/McpProtocolOperations.h) 中对应工具的 schema 定义是否存在。

### 12.3 复杂文件解析失败

**原因**：`compile_commands.json` 未找到或路径不正确。

**解决**：
1. 确认 `project_root` 参数指向项目根目录。
2. 确认 `<project_root>/build/compile_commands.json` 存在。
3. 如无，用 CMake 生成：`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。

### 12.4 DFG/Slice 超时

**解决**：增大超时到 300s，或减小 `max_nodes` / `max_edges` / `max_interprocedural_bindings`。

### 12.5 MinGW 编译

项目默认使用 MSVC。如需 MinGW：

```powershell
cmake -B AIbuild -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build AIbuild
```

> 注意：MinGW 模式下 Clang Tooling 的头文件路径需要额外配置，建议优先使用 MSVC。
