# codex-lan-agent

基于 Clang AST 的 C/C++ 代码分析 MCP 工具链，提供 AST 解析、控制流图（CFG）、调用图（Call Graph）、数据流图（DFG）、程序切片（Program Slice）能力，并内置语义网格（Semantic Grid）长文本解构、归纳、检索、溯源与上下文重构能力，通过 MCP 协议（Streamable HTTP）对外服务。

---

## 目录

1. [架构概览](#1-架构概览)
2. [构建与启动](#2-构建与启动)
3. [MCP 协议接入](#3-mcp-协议接入)
4. [工具清单与参数](#4-工具清单与参数)
5. [接入本地模型后的操作语义](#5-接入本地模型后的操作语义)
6. [完整使用案例](#6-完整使用案例)
7. [Artifact 二次查询与分页](#7-artifact-二次查询与分页)
8. [语义网格工具：长文本解构与上下文重构](#8-语义网格工具长文本解构与上下文重构)
9. [测试脚本与一键验证](#9-测试脚本与一键验证)
10. [测试结论](#10-测试结论)
11. [CMM 工具清单](#11-cmm-工具清单)
12. [Clang 分析工具 vs CMM 工具功能对比](#12-clang-分析工具-vs-cmm-工具功能对比)
13. [常见问题排查](#13-常见问题排查)

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
│  SemanticGridOperations.cpp       │  ← 语义网格（解构/归纳/检索/溯源/增量）
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
- 语义网格工具独立于 Clang 工具链，可单独用于长文本/规则文档的解构与检索，为本地模型提供上下文重构能力。

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

### 4.2 语义网格工具（6 个）

语义网格工具负责长文本的解构、五层语义金字塔构建、检索、原文溯源、上下文重构和多轮增量更新。

| 工具 | 功能 | 必需参数 |
|---|---|---|
| `lan_agent_semantic_grid_ingest_text` | 将长文本解构为语义片段 fragments | `source_text` 或 `source_file` |
| `lan_agent_semantic_grid_build` | 从 fragments/source_text 构建 L1-L5 语义金字塔 | `source_text` 或 `fragments_json` 或 `artifact_summary_path` |
| `lan_agent_semantic_grid_query` | 按 layer/keyword/fuzzy/regex 查询语义网格 | `artifact_summary_path` |
| `lan_agent_semantic_grid_trace_source` | 从任意语义节点追溯回原文 fragment | `artifact_summary_path`, `node_id` |
| `lan_agent_semantic_grid_context_bundle` | 根据任务意图生成 LLM 上下文 bundle | `artifact_summary_path` |
| `lan_agent_semantic_grid_incremental_update` | 多轮增量追加，支持 content_hash 去重 | `artifact_summary_path`, `source_text` |

### 4.3 通用参数说明

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

### 4.4 语义网格参数说明

| 参数 | 类型 | 说明 |
|---|---|---|
| `source_text` | string | 内联长文本（ingest/build/incremental） |
| `source_file` | string | 文本/markdown 文件路径（ingest/build/incremental） |
| `source_kind` | string | 来源类型，如 `md`/`txt`/`complex_markdown`/`incremental_markdown` |
| `split_strategy` | string | 切分策略：`markdown`(默认) / `paragraph` / `sentence` / `sliding_window` |
| `max_fragment_chars` | integer | 单个片段软上限字符数，默认 900 |
| `sliding_overlap_chars` | integer | `sliding_window` 模式的重叠字符数 |
| `max_fragments` | integer | 最大片段数，默认 512 |
| `domain` | string | L2 领域提示，默认 `general` |
| `grid_id` | string | 可选显式 grid id |
| `fragments_json` | string | ingest 返回的 fragments JSON 字符串 |
| `artifact_summary_path` | string | summary.json 路径，用于二次查询/增量更新 |
| `artifact_grid_json_path` | string | semantic_grid.json 路径 |
| `artifact_fragments_json_path` | string | fragments.json 路径 |
| `node_id` | string | 语义节点 ID（query/trace_source） |
| `layer` | string | L1_META / L2_DOMAIN / L3_FLOW / L4_ATOM / L5_RAW |
| `keyword` / `query` | string | 搜索关键词 |
| `fuzzy_match` | boolean | 启用 token/子序列模糊匹配 |
| `regex_match` | boolean | 将 keyword 当作正则表达式 |
| `relation_type` | string | contains / source_trace / sequence / synonym / complement / depend / exclude / reference |
| `direction` | string | both / in/up / out/down |
| `offset` / `limit` | integer | 分页参数 |
| `task_intent` | string | 上下文重构的任务意图 |
| `flow_stage` | string | 上下文重构的流程阶段过滤 |
| `max_chars` | integer | 上下文 bundle 最大字符数 |
| `dedupe_existing` | boolean | 增量更新时按 content_hash 去重，默认 true |

### 4.5 compile_commands.json 发现顺序

1. 显式 `compilation_database_path` → 直接使用
2. 显式 `compile_db_dir` → 拼接 `compile_commands.json`
3. `project_root` + 常见构建目录 → 自动搜索 `build/`, `AIbuild/`, `cmake-build-*/`
4. 均未找到 → 降级为无编译数据库模式（`compile_db_mode=none`，复杂文件可能失败）

### 4.6 CMM 工具清单（codebase-memory-mcp 桥接）

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
| SG | `lan_agent_semantic_grid_*` | 长文本语义网格：解构、归纳、检索、溯源、上下文重构、增量 |

### 5.3 模型决策规则

本地模型应遵循以下决策规则：

1. **先 discovery，再 analysis**：先调用 `tools/list` 确认工具可用。
2. **先 AST，再 deep analysis**：先解析 AST 获取文件结构概览，再决定是否构建 CFG/DFG/Slice。
3. **先 build，再 query**：首次分析调用 `build_*` 写入 artifact；后续分页/聚焦查询调用 `query_*_artifact`。
4. **复杂项目必传 `project_root`**：确保 `compile_commands.json` 被发现，否则复杂文件解析失败。
5. **大文件分页**：使用 `offset_*` / `max_*` 控制返回量，避免单次响应过大。
6. **超时意识**：DFG/Slice 对复杂文件可能需要 180-300s，模型应设置足够超时。
7. **语义网格用于非代码文本**：长文本/规则文档/经验框架使用 `semantic_grid_*` 工具链，不走 Clang 工具。
8. **增量更新用链式 summary**：每轮增量使用上一轮返回的 `artifact_summary_json_path` 作为下一轮输入。

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

### 5.5 语义网格对话流程（长文本分析）

```
用户：将这份规则文档解构，并检索其中关于"约束"的语义节点

模型内部决策：
  1. tools/list → 确认 lan_agent_semantic_grid_build 可用
  2. lan_agent_semantic_grid_build(source_text=<规则文档>, domain="rule_doc")
     → 返回 fragment_count, node_count, edge_count, layer_distribution
  3. lan_agent_semantic_grid_query(artifact_summary_path=<build 返回的 path>,
                                    layer="L4_ATOM", keyword="约束", fuzzy_match=true)
     → 返回 matched_nodes_json, match_mode=fuzzy
  4. 从 matched_nodes 取第一个 node_id
  5. lan_agent_semantic_grid_trace_source(artifact_summary_path=<同上>,
                                          node_id=<上一步的 node_id>)
     → 返回 source_fragments_json，包含原文 content_text 和 section_path
  6. lan_agent_semantic_grid_context_bundle(artifact_summary_path=<同上>,
                                            task_intent="约束 规则",
                                            fuzzy_match=true, max_nodes=8)
     → 返回 context_bundle_json + prompt_sections，供模型直接消费

模型输出：
  "该规则文档被解构为 11 个语义片段，构建出 27 个语义节点。
   关于'约束'的语义节点位于 L4_ATOM 层，对应原文片段：
   '禁止把推理约束和经验描述混在同一个原子节点中'。
   上下文 bundle 已生成，包含 8 个相关节点和 prompt 文本..."
```

### 5.6 多轮增量对话流程

```
用户：再追加一段新经验，并确保不重复已有内容

模型内部决策：
  1. 使用上一轮 build 的 artifact_summary_path
  2. lan_agent_semantic_grid_incremental_update(
       artifact_summary_path=<base/summary.json>,
       source_text=<新增经验文本>,
       dedupe_existing=true)
     → 返回 added_fragment_count, skipped_duplicate_fragment_count,
       delta_fragments_json, delta_nodes_json
  3. 如需查询新增内容：
     lan_agent_semantic_grid_query(
       artifact_summary_path=<incremental 返回的新 summary.json>,
       keyword="<新增主题>", fuzzy_match=true)
  4. 如重复提交相同文本：
     added_fragment_count=0, skipped_duplicate_fragment_count>0 → 确认去重生效

模型输出：
  "已追加 4 个新片段，新增 8 个语义节点（delta）。
   如重复提交相同内容，系统会跳过 4 个重复片段，节点数不变。"
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

## 8. 语义网格工具：长文本解构与上下文重构

### 8.1 功能意义

语义网格（Semantic Grid）是一套**独立于 Clang 代码分析**的长文本处理工具链，解决以下问题：

- **长文本不可检索**：将规则文档、经验框架、操作手册等长文本解构为可索引的语义片段。
- **语义层级缺失**：构建 L1-L5 五层语义金字塔，从元认知到原文片段垂直贯通。
- **上下文窗口不足**：本地模型上下文有限，`context_bundle` 按任务意图生成精简上下文。
- **多轮追加无去重**：增量更新时按 `content_hash` 自动去重，避免语义重复。
- **无法溯源到原文**：`trace_source` 从任意语义节点追溯回原始片段，保留 `section_path` 和行号。

### 8.2 五层语义金字塔

```
L1_META        整体语义网格元认知节点（1 个）
  └─ L2_DOMAIN 领域主题节点（1 个，由 domain 参数决定）
       └─ L3_FLOW  流程/类别节点（按 fragment_type 分类）
            └─ L4_ATOM  原子语义节点（每个 fragment 一个）
                 └─ L5_RAW  原文片段节点（保留完整 content_text）
```

**关系类型**：
- `contains`：垂直层级包含关系（L1→L2→L3→L4→L5）
- `source_trace`：L4 原子节点到 L5 原文片段的溯源关系
- `sequence`：同层 L4 节点之间的时序关系

### 8.3 Fragment 结构

每个 fragment 包含：

| 字段 | 说明 |
|---|---|
| `fragment_id` | `frag_1`, `frag_2`, ... |
| `source_file` | 来源文件路径（如有） |
| `source_kind` | `md` / `txt` / `complex_markdown` / `incremental_markdown` |
| `section_path` | Markdown 标题路径，如 `第一层 > 规则边界` |
| `content_hash` | FNV-1a hash，用于增量去重 |
| `source_line_start` / `source_line_end` | 原文行号范围 |
| `fragment_type` | `boundary_rule` / `condition_statement` / `action_step` / `term_definition` |
| `content_text` | 原文片段文本 |
| `keyword_tags` | 自动提取的关键词标签 |

### 8.4 完整流程：基础构建 + 查询 + 溯源 + 上下文重构

```powershell
# 1. 构建语义网格
$buildBody = @{
    jsonrpc = "2.0"
    id = "sg-build"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_build"
        arguments = @{
            source_text = "# 规则文档`n## 第一章`n禁止绕过统一执行核心..."
            source_kind = "complex_markdown"
            split_strategy = "markdown"
            max_fragments = 64
            domain = "rule_doc"
            output_dir = "D:/tmp/sg_base"
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$build = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $buildBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# fragment_count=11, node_count=28, edge_count=34
# layer_distribution: L1_META=1, L2_DOMAIN=1, L3_FLOW=4, L4_ATOM=11, L5_RAW=11
# artifact_summary_json_path exists

# 2. 查询（fuzzy 模式）
$queryBody = @{
    jsonrpc = "2.0"
    id = "sg-query"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_query"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path
            layer = "L4_ATOM"
            keyword = "section priority"
            fuzzy_match = $true
            limit = 12
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$query = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $queryBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success, match_mode=fuzzy, node_count > 0
# matched_nodes_json exists

# 3. 原文溯源
$firstNodeId = ($query.matched_nodes_json | ConvertFrom-Json)[0].node_id
$traceBody = @{
    jsonrpc = "2.0"
    id = "sg-trace"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_trace_source"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path
            node_id = $firstNodeId
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$trace = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $traceBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success, source_fragment_count > 0
# source_fragments_json contains content_text, section_path, content_hash

# 4. 上下文重构
$bundleBody = @{
    jsonrpc = "2.0"
    id = "sg-bundle"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_context_bundle"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path
            task_intent = "约束 规则 priority source trace"
            fuzzy_match = $true
            max_nodes = 12
            max_chars = 5000
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$bundle = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $bundleBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success, node_count > 0
# context_bundle_json contains prompt_text
# context_sections_json contains section_priority, section_weight
# prompt_sections exists
```

### 8.5 多轮增量更新

```powershell
# 第 1 轮增量
$inc1Body = @{
    jsonrpc = "2.0"
    id = "sg-inc1"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_incremental_update"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path  # ← base 轮的 summary
            source_text = "## 新增经验`n跨轮分析需要保持 fragment id 稳定..."
            source_kind = "incremental_markdown"
            split_strategy = "markdown"
            max_fragments = 32
            dedupe_existing = $true
            output_dir = "D:/tmp/sg_inc1"
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$inc1 = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $inc1Body -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# incoming_fragment_count=4, added_fragment_count=4
# new_fragment_count=15 (base 11 + new 4)
# new_node_count=36 (base 28 + delta 8)
# delta_fragments_json exists, delta_nodes_json exists

# 重复提交（去重验证）
$dupBody = @{
    jsonrpc = "2.0"
    id = "sg-dup"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_incremental_update"
        arguments = @{
            artifact_summary_path = $inc1.artifact_summary_json_path  # ← inc1 轮的 summary
            source_text = "## 新增经验`n跨轮分析需要保持 fragment id 稳定..."  # 同一段文本
            source_kind = "incremental_markdown"
            split_strategy = "markdown"
            dedupe_existing = $true
            output_dir = "D:/tmp/sg_dup"
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$dup = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $dupBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# incoming_fragment_count=4, added_fragment_count=0
# skipped_duplicate_fragment_count=4  ← 去重生效
# new_fragment_count=15 (不变), new_node_count=36 (不变)
```

### 8.6 Artifact 文件结构

```
output_dir/
├── semantic_grid.json      ← 完整网格（fragments + nodes + edges）
├── nodes.json              ← 节点列表
├── edges.json              ← 边列表
├── summary.json            ← 摘要（artifact_*_path 指针）
├── delta_fragments.json    ← 增量轮新增片段（仅 incremental_update）
└── delta_nodes.json        ← 增量轮新增节点（仅 incremental_update）
```

**链式 summary 规则**：每轮 `incremental_update` 返回的 `artifact_summary_json_path` 可直接作为下一轮的 `artifact_summary_path` 输入，形成多轮增量链路。

### 8.7 查询模式说明

| 模式 | 参数 | 匹配规则 |
|---|---|---|
| substring（默认） | 无特殊参数 | 关键词子串匹配 |
| fuzzy | `fuzzy_match=true` | token 全匹配 / 子序列匹配 |
| regex | `regex_match=true` | 正则表达式匹配（icase） |

**分页字段**：`offset` / `limit` / `has_more` / `next_offset_or_null` / `pagination_status`

---

## 9. 测试脚本与一键验证

### 9.1 一键脚本 1：模板回归

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_analysis_templates_1_11.ps1
```

覆盖：`tools/list`、AST、CFG、CallGraph、DFG、Slice、artifact query、分页、path-sensitive metadata。

### 9.2 一键脚本 2：复杂项目分析

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\analysis_client_examples.ps1 `
  -SourceFile D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cximage\FastMatch.cpp `
  -ProjectRoot D:\Codex-WorkDir\Sean_WorkDir\cxvisionai `
  -FocusSymbol center_x
```

覆盖：复杂项目（FastMatch.cpp）完整分析链，DFG/Slice 的 `focus_symbol` 邻域提取、过程间绑定验证。

> **注意**：DFG 对复杂文件可能需要 180-300s，脚本默认超时 180s。如超时，手动以 300s 超时重跑。

### 9.3 手动 MCP 调用模板

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

### 9.4 一键脚本 3：语义网格基础 Smoke

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_semantic_grid_smoke.ps1
```

覆盖：`tools/list`（6 工具注册）、`ingest_text`（解构）、`build`（L1-L5 金字塔）、`query`（fuzzy 查询）、`trace_source`（原文溯源）、`context_bundle`（上下文重构）。

### 9.5 一键脚本 4：复杂文本 + 多轮增量 Smoke

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_semantic_grid_complex_incremental_smoke.ps1
```

覆盖：复杂 markdown 基础构建、增量追加（+4 fragments）、fuzzy 查询、重复增量去重（dedupe）、上下文重构（section_priority）。

---

## 10. 测试结论

### 10.1 环境

| 项 | 值 |
|---|---|
| 仓库 | `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent` |
| 构建目录 | `AIbuild\Release` |
| 测试目标文件 | `cximage\FastMatch.cpp`（复杂项目） + `test_simple.cpp`（简单文件） |
| compile_commands.json | `D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build\compile_commands.json` |

### 10.2 工具可用性

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

### 10.3 核心断言结果（Clang 分析工具）

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

### 10.4 语义网格测试结果

#### 基础 Smoke (`run_semantic_grid_smoke.ps1`)

| 断言 | 结果 |
|---|---|
| 6 个 semantic grid 工具全部注册 | PASS |
| `ingest_text` fragment_count > 0 | PASS (11 fragments) |
| `build` node_count > fragment_count | PASS (27 nodes > 11 fragments) |
| `build` edge_count > 0 | PASS (34 edges) |
| `build` layer_distribution 包含 L1-L5 | PASS |
| `query` node_count > 0 | PASS (1 node) |
| `trace_source` source_fragment_count > 0 | PASS (1 fragment) |
| `context_bundle` node_count > 0 | PASS (8 nodes) |
| `context_bundle` 包含 prompt_text | PASS |

#### 复杂增量 Smoke (`run_semantic_grid_complex_incremental_smoke.ps1`)

| 断言 | 结果 |
|---|---|
| 基础 build 成功 | PASS (28 nodes, 34 edges) |
| `semantic_grid_json` 包含 `section_path` | PASS |
| `semantic_grid_json` 包含 `content_hash` | PASS |
| `layer_distribution` 包含 L1_META/L2_DOMAIN/L3_FLOW/L4_ATOM/L5_RAW | PASS |
| 增量追加 added_fragment_count > 0 | PASS (+4 fragments) |
| 增量追加 new_fragment_count > previous | PASS (15 > 11) |
| 增量追加 delta_node_count > 0 | PASS (36 > 28, delta=8) |
| `delta_fragments.json` 存在 | PASS |
| `delta_nodes.json` 存在 | PASS |
| fuzzy 查询 match_mode=fuzzy | PASS |
| fuzzy 查询 node_count > 0 | PASS |
| 重复增量 added_fragment_count=0 | PASS |
| 重复增量 skipped_duplicate_fragment_count > 0 | PASS (4) |
| 重复增量 new_fragment_count 不变 | PASS (15=15) |
| 重复增量 new_node_count 不变 | PASS (36=36) |
| `context_bundle` 包含 section_priority | PASS |
| `prompt_sections` 包含 Semantic Grid Context | PASS |
| artifact_summary_path 链式传递 | PASS (base→inc1→dup) |
| artifact 文件全部存在 | PASS |

### 10.5 已知限制

| 限制 | 详情 | 规避 |
|---|---|---|
| DFG 复杂文件耗时 | FastMatch.cpp DFG build 需 180-300s | 设置 `TimeoutSec=300` |
| 端口占用 | 旧进程残留导致新实例启动失败 | 启动前 `Stop-Process` |
| compile_commands.json 依赖 | 无编译数据库时复杂文件解析失败 | 确保 `project_root` 指向含 `build/compile_commands.json` 的目录 |

### 10.6 最终状态

**[Verified]** — MCP 工具链可用于复杂项目分析，全部核心断言通过。

**[Verified]** — 语义网格工具链支持复杂文本解构、L1-L5 语义金字塔构建、fuzzy/regex 查询、原文溯源、上下文重构、多轮增量追加与 content_hash 去重，全部断言通过。

---

## 11. CMM 工具状态

CMM 工具通过 `codex_lan_agent` 桥接 `codebase-memory-mcp` 服务，已在 MCP 工具列表中注册。使用前需确保：
1. `codebase-memory-mcp` 服务已独立运行。
2. 目标项目已通过 `lan_agent_cmm_index_repository` 完成索引。

CMM 工具状态：`[Implemented]` — Schema 已注册，依赖外部 CMM 服务实际可用性。

---

## 12. Clang 分析工具 vs CMM 工具功能对比

### 12.1 核心差异

| 维度 | Clang 分析工具 (L0-L6) | CMM 工具 (`lan_agent_cmm_*`) |
|---|---|---|
| **数据时效** | 实时解析源文件 | 基于预建索引（需先 `index_repository`） |
| **分析深度** | AST statement-level（语句级） | 图节点/关系级（函数、类、文件级） |
| **适用范围** | 单文件级（`source_file`） | 整个项目级（`project`/`repo`） |
| **底层引擎** | Clang/LLVM AST | codebase-memory-mcp 图数据库 |
| **典型耗时** | 简单文件 1-5s，复杂文件 180-300s | 毫秒级（索引已建） |

### 12.2 功能对照表

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

### 12.3 使用场景对比

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

### 12.4 组合使用建议

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

## 13. 常见问题排查

### 13.1 服务启动失败 / 连接拒绝

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

### 13.2 工具不在 tools/list 中

**原因**：工具 schema 未在 `McpProtocolOperations.h` 的 `BuildMcpToolsListResponse` 中注册。

**解决**：检查 [src/McpProtocolOperations.h](file:///d:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/McpProtocolOperations.h) 中对应工具的 schema 定义是否存在。

### 13.3 复杂文件解析失败

**原因**：`compile_commands.json` 未找到或路径不正确。

**解决**：
1. 确认 `project_root` 参数指向项目根目录。
2. 确认 `<project_root>/build/compile_commands.json` 存在。
3. 如无，用 CMake 生成：`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。

### 13.4 DFG/Slice 超时

**解决**：增大超时到 300s，或减小 `max_nodes` / `max_edges` / `max_interprocedural_bindings`。

### 13.5 MinGW 编译

项目默认使用 MSVC。如需 MinGW：

```powershell
cmake -B AIbuild -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build AIbuild
```

> 注意：MinGW 模式下 Clang Tooling 的头文件路径需要额外配置，建议优先使用 MSVC。

### 13.6 语义网格增量去重不生效

**原因**：`dedupe_existing` 参数未设置为 `true`，或上一轮的 `artifact_summary_path` 不正确。

**解决**：
1. 确认 `dedupe_existing=true`（默认为 true）。
2. 确认 `artifact_summary_path` 指向上一轮 `incremental_update` 返回的 `artifact_summary_json_path`。
3. 检查 `summary.json` 中的 `artifact_semantic_grid_json_path` 指针是否有效。

### 13.7 语义网格 query 返回空结果

**原因**：keyword 未匹配到任何节点，或 layer 过滤过严。

**解决**：
1. 尝试 `fuzzy_match=true` 启用模糊匹配。
2. 尝试不传 `layer` 参数，搜索所有层。
3. 使用 `regex_match=true` 扩展匹配范围。
