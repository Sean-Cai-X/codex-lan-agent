# codex-lan-agent

基于 Clang AST 的 C/C++ 代码分析 MCP 工具链，提供 AST 解析、控制流图（CFG）、调用图（Call Graph）、数据流图（DFG）、程序切片（Program Slice）能力，并内置语义网格（Semantic Grid）长文本解构、归纳、检索、溯源与上下文重构能力，以及面向长会话上下文压力的 **Task Memory（任务记忆）** 工具链 —— 在 MCP 服务端文件对象层持久化任务状态、步骤账本、关键切片、KV 快照、RocksDB 镜像与一致性校验，使全新模型只需读取 `latest_resume_context.json` 即可续接长任务，无需加载完整历史对话。通过 MCP 协议（Streamable HTTP）对外服务。

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
9. [Task Memory 工具：长任务记忆与跨模型续接](#9-task-memory-工具长任务记忆与跨模型续接)
10. [测试脚本与一键验证](#10-测试脚本与一键验证)
11. [测试结论](#11-测试结论)
12. [CMM 工具清单](#12-cmm-工具清单)
13. [Clang 分析工具 vs CMM 工具功能对比](#13-clang-分析工具-vs-cmm-工具功能对比)
14. [常见问题排查](#14-常见问题排查)
15. [项目演进分析报告（8月9日 → 8月11日）](#15-项目演进分析报告8月9日--8月11日)
16. [CLIPS 规则体系详解](#16-clips-规则体系详解)
17. [Fact-Factory 守卫层（LLM ↔ CLIPS 中间防护）](#17-fact-factory-守卫层llm--clips-中间防护)
18. [optfile 原子文件操作工具](#18-optfile-原子文件操作工具)
19. [Fact-Factory 守卫层冒烟测试](#19-fact-factory-守卫层冒烟测试)

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
│  SemanticGridOperations.h         │  ← 语义网格（解构/归纳/检索/溯源/增量）
├───────────────────────────────────┤
│  TaskMemoryOperations.h           │  ← Task Memory（freeze/step/kv/rocksdb/parity）
├───────────────────────────────────┤
│  compile_commands.json            │  ← 编译数据库（项目侧）
│  Clang / LLVM                     │  ← 底层解析引擎
│  logs/task_memory/{goal_id}       │  ← 任务记忆文件对象层（source of truth）
└───────────────────────────────────┘
```

**核心设计原则：**
- 所有代码分析工具共享同一个 Clang Tooling 执行核心。
- 复杂项目通过 `compile_commands.json` 提供编译参数，不硬编码环境。
- 每个工具产出标准化 JSON artifact，支持二次查询（artifact query）。
- 分页（`offset_*` / `max_*`）和邻域提取（`focus_symbol` / `neighborhood_depth`）在 artifact 层完成，无需重跑 Clang。
- 语义网格工具独立于 Clang 工具链，可单独用于长文本/规则文档的解构与检索，为本地模型提供上下文重构能力。
- Task Memory 把"长任务记忆"从模型上下文外移到 MCP 服务端文件对象层：源真（source of truth）始终是 `logs/task_memory/{goal_id}/` 下的文件对象，RocksDB 仅作可选读镜像，需通过 parity check 校验后才允许替换读路径。

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
| `data_root` | **Task Memory 文件对象层根目录**（默认 `logs/`，所有任务记忆写入 `<data_root>/task_memory/{goal_id}/`） |
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
### 4.0 完整工具清单概览（135 个）

`McpProtocolOperations.h` 的 `BuildMcpToolsListResponse` 共注册 **135 个** MCP 工具。默认模式（`UseFullMcpToolSurface()=false`）下 `tools/list` 只返回 `lan_agent_mcp_route` 单一网关入口；设置环境变量 `CODEX_LAN_AGENT_MCP_TOOL_SURFACE=full/all/legacy/153` 后暴露全部 135 个工具。按功能类别分布如下：

| 类别 | 工具数 | 代表工具 | 说明 |
|---|---|---|---|
| 网关路由 | 1 | `lan_agent_mcp_route` | 默认唯一可见入口（overview/route/call） |
| Overview/Discovery | 12 | `lan_agent_mcp_overview` / `lan_agent_runtime_overview` / `lan_agent_rag_overview` / `lan_agent_patch_overview` / `lan_agent_remote_session_semantic_catalog` | 能力发现与运行时概览 |
| Profile/CLI/Case 执行 | 4 | `lan_agent_run_cli_profile` / `lan_agent_run_case` / `lan_agent_enqueue_case` | CLI profile 与 case 编排 |
| 本地模型/RAG/会话 | 14 | `lan_agent_run_local_chat` / `lan_agent_run_rag_flow` / `lan_agent_ventriloquist_reply` / `lan_agent_remote_session_new_turn` / `lan_agent_allocate_remote_chat_session` / `lan_agent_build_semantic_execution_card` / `lan_agent_enqueue_local_chat` | llama.cpp 远程会话与 RAG 集成 |
| OptFile 配置 | 3 | `lan_agent_optfile_read` / `lan_agent_optfile_write_preview` / `lan_agent_optfile_apply_write` | 配置文件读写 |
| Dialog Slice | 2 | `lan_agent_record_dialog_slice` / `lan_agent_analyze_dialog_slices` | 对话切片入库与分析 |
| Semantic Action | 1 | `lan_agent_execute_semantic_action` | 语义动作解析与执行 |
| Basic/Task 运维 | 5 | `lan_agent_basic_comm_smoke` / `lan_agent_get_task` / `lan_agent_task_log` / `lan_agent_snapshot_diff` | 任务查询与 smoke |
| 构建/测试 | 8 | `lan_agent_configure_project` / `lan_agent_build_target` / `lan_agent_run_ctest_target` / `lan_agent_preflight_build_target` / `lan_agent_discover_ctest_tests` | CMake/CTest 执行与预检 |
| CLIPS 决策 | 4 | `lan_agent_clips_decide` / `lan_agent_clips_chain_template` / `lan_agent_rag_clips_meta` / `lan_agent_rag_clips_run` | 规则引擎调用（详见第 16 节） |
| RAG Storage/Review | 3 | `lan_agent_rag_storage_lookup` / `lan_agent_rag_review_observe` / `lan_agent_rag_storage_page` | RAG 存储查询与审查 |
| Remote Events/Logs | 4 | `lan_agent_discover_logs` / `lan_agent_tail_control_events` / `lan_agent_list_recent_remote_events` / `lan_agent_query_remote_task_result_refs` | 远程事件与日志 |
| 文件编辑（安全受控） | 11 | `lan_agent_preview_patch` / `lan_agent_apply_single_file_patch` / `lan_agent_apply_diff_patch` / `lan_agent_write_text_file` / `lan_agent_format_code_file` / `lan_agent_revert_single_file_patch` / `lan_agent_verify_single_file_patch` / `lan_agent_get_patch_audit_trail` / `lan_agent_get_supervision_status` | patch 审计链 + 格式化 |
| CxParser/Clang Indexer | 4 | `lan_agent_run_clang_indexer` / `lan_agent_list_cxparser_flows` / `lan_agent_validate_cxparser_flow` / `lan_agent_run_cxparser_flow` | cxparser 流编排 |
| Task Memory | 14 | `lan_agent_task_memory_freeze` / `lan_agent_task_memory_resume_and_execute` / `lan_agent_task_memory_new_chat_round_selftest` / `lan_agent_task_memory_rocksdb_mirror` / `lan_agent_task_memory_migration_acceptance` | 长任务记忆（详见 4.3） |
| File Access | 17 | `lan_agent_probe_text_file` / `lan_agent_read_text_file` / `lan_agent_tail_text_file` / `lan_agent_list_directory` / `lan_agent_read_directory_files` / `lan_agent_scan_text_ranges` / `lan_agent_delete_text_range_window_atomic` / `lan_agent_prepare_edit_windows` | 文件读取/扫描/原子编辑 |
| Profile Catalog | 1 | `lan_agent_profile_catalog` | profile 目录 |
| CMM 桥接 | 12 | `lan_agent_cmm_index_repository` / `lan_agent_cmm_search_code` / `lan_agent_cmm_query_graph` / `lan_agent_cmm_trace_path` / `lan_agent_cmm_get_architecture` / `lan_agent_cmm_delete_project` | codebase-memory-mcp 桥接（详见 4.11） |
| Semantic Grid | 6 | `lan_agent_semantic_grid_ingest_text` / `lan_agent_semantic_grid_build` / `lan_agent_semantic_grid_query` / `lan_agent_semantic_grid_context_bundle` | 长文本语义网格（详见 4.2/第 8 节） |
| Clang AST/CFG/CallGraph/DFG/Slice | 9 | `lan_agent_run_clang_ast_parser` / `lan_agent_build_cfg` / `lan_agent_build_call_graph` / `lan_agent_build_dfg` / `lan_agent_build_program_slice` + 5 个 `query_*_artifact` | Clang 代码分析（详见 4.1） |
| **合计** | **135** | | |

> **维护约定**：新增或删除工具时，必须同步更新本表工具数与本节合计。工具命名统一前缀 `lan_agent_`，完整工具名清单可通过 `tools/list`（完整模式）或 Python 脚本扫描 `McpProtocolOperations.h` 中的 `"name":"lan_agent_*"` 获得。

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

### 4.3 Task Memory 工具（14 个）

Task Memory 工具负责长任务状态的 MCP 服务端持久化：把模型上下文中的"任务进度/下一步调用/已验证步骤/关键切片"外移到文件对象层，使全新模型只需读取 `latest_resume_context.json` 即可续接。

| 工具 | 功能 | 必需参数 |
|---|---|---|
| `lan_agent_task_memory_freeze` | 冻结一次长任务当前状态：写 `latest_resume_context.json` / `step_ledger.jsonl` / `slices.jsonl` / `index_manifest.json` / `rag_thread_migration/*` | `goal_id` |
| `lan_agent_task_memory_append_step` | 追加一条已验证 continuation step 到 `step_ledger.jsonl`，并刷新 `latest_resume_context.json` | `goal_id` |
| `lan_agent_task_memory_execute_continuation_budget` | 在 MCP 服务端执行 N 步 allowlisted continuation（默认 dry-run 写预算计划，`execute=true` 才真正执行）；预算耗尽时返回 `next_call_json`、`completion_claim_allowed=false` | `goal_id` |
| `lan_agent_task_memory_resume_context` | 读取 `latest_resume_context.json`，作为新模型的首读入口（默认禁止读全历史） | `goal_id` |
| `lan_agent_task_memory_resume_and_execute` | **fresh-chat 一次性续接入口**：读 `latest_resume_context.json` → 在 MCP 内执行 bounded continuation budget → 刷新任务记忆；返回终止验证字段或本工具自身的 `next_call_json`，直到 `terminal_state=true`。比手动 `resume_context` + `execute_continuation_budget` 链式调用更直接 | `goal_id` |
| `lan_agent_task_memory_build_kv_snapshot` | 把 goal/latest/trace/slice/budget 索引到 `kv_snapshot/index.jsonl`，键 schema 与后续 RocksDB 后端一致 | `goal_id` |
| `lan_agent_task_memory_kv_lookup` | 在文件 KV 快照上按 key 或 `kind=latest\|trace\|slice\|budget\|...` 查询 | `goal_id` |
| `lan_agent_task_memory_rocksdb_mirror` | 把文件 KV 快照镜像到可选 RocksDB 读后端（`CODEX_LAN_AGENT_WITH_ROCKSDB=ON` 时启用）；写 `rocksdb_mirror_manifest.json`；**不替换文件对象层源真地位** | `goal_id` |
| `lan_agent_task_memory_rocksdb_lookup` | 在 RocksDB 镜像上按 key 或 selector 查询（仅在 `rocksdb_mirror` 完成后可用） | `goal_id` |
| `lan_agent_task_memory_rocksdb_parity_check` | 对同一 selector 比对文件 KV 与 RocksDB 镜像结果；pass 证明镜像读路径一致 | `goal_id` |
| `lan_agent_task_memory_migration_assess` | 评估某 goal 是否可进入下一后端阶段：报告文件对象/KV 快照/RocksDB 状态、源真策略、是否可换源 | `goal_id` |
| `lan_agent_task_memory_structure_manifest` | 写 `memory_structure.json`，固化"新模型首读 → 二读 → 查询读 → 全历史读禁用"契约 | `goal_id` |
| `lan_agent_task_memory_migration_acceptance` | 在 MCP 内一站式跑完整迁移验收链（freeze → budget → kv → mirror → parity → manifest），返回 `migration_acceptance_status=ACCEPTED/PARTIAL` | — |

### 4.4 MCP 单一网关入口：`lan_agent_mcp_route`

`lan_agent_mcp_route` 是**聊天层唯一可见的 MCP 工具**——把所有内部 MCP 工具（代码分析、语义网格、Task Memory、文件访问等）隐藏在自身之后。本地模型只需看到这 1 个工具，通过三种模式与 MCP 交互。启用条件：`UseFullMcpToolSurface()=false`（默认），此时 `tools/list` 只返回 `lan_agent_mcp_route`。

#### 三种模式

| mode | 行为 | 关键返回字段 |
|---|---|---|
| `overview` | 返回 MCP 能力指引（不执行工具） | `mcp_route_mode=overview`、`tool_use_decision=guidance_only`、`chain_state=no_execution_started`、`visible_tool_count=1`、`visible_tool_name=lan_agent_mcp_route` |
| `route` | CLIPS 规则推断路由目标，构造 `required_tool_arguments_json` | `mcp_route_mode=route`、`route_target`、`required_tool_name`、`required_tool_arguments_json`、`next_call_json`、`chain_state=needs_tool_call`、`semantic_model_clamp=tool_call_only` |
| `call` | 通过内部注册表查找 `target_tool_name` 并执行（递归保护：禁止 `mcp_route` 调自身） | `mcp_route_mode=call`、`routed_tool_name`、`internal_execution_performed=true`、`current_tool_chain_node`，透传内部工具所有 result 字段 |

#### 参数

| 参数 | 类型 | 说明 |
|---|---|---|
| `mode` | string | `overview` / `route` / `call`。省略时：有 `request_text`/`primary_intent`/`file_path`/`source_file` 任一非空 → 默认 `route`，否则 `overview` |
| `request_text` | string | 自然语言请求（route 模式） |
| `primary_intent` | string | 主意图（route 模式） |
| `target_tool_name` | string | call 模式要执行的内部工具名 |
| `arguments` | object | call 模式传给 `target_tool_name` 的参数对象 |
| `arguments_json` | string | call 模式的参数 JSON 字符串（`arguments` 的替代形式） |
| `goal_id` / `trace_id` | string | 任务追踪标识 |
| `max_steps` | integer | 续跑步数上限 |

#### 典型调用流程

```
1. mode=route（或省略 mode + 提供自然语言请求）
   → MCP 返回 required_tool_name=lan_agent_delete_text_range_window_atomic
              required_tool_arguments_json={...}

2. mode=call, target_tool_name=lan_agent_delete_text_range_window_atomic,
   arguments=<上一步的 required_tool_arguments_json>
   → MCP 执行内部工具，返回 result 字段 + chain_state

3. 根据 chain_state / terminal_state / verification_ok 判断继续或终结
   → 非终态：回到步骤 1 或 2 继续调用
   → 终态：terminal_state=true + completion_claim_allowed=true → 可输出自然语言结论
```

#### 安全约束

- **递归保护**：`target_tool_name=lan_agent_mcp_route` 时返回 `recursive_mcp_route_blocked`
- **未注册工具**：返回 `internal_tool_not_found`（404）
- **安全分级**：`safety_class=mcp_gateway_route`、`risk=medium`、`trigger=mcp_route`

### 4.5 文件访问工具组（4 个）

文件访问工具组负责远程工作区/日志目录的文件读取和目录列举，是本地模型 inspect 远程文件系统的主要通道。

| 工具 | 功能 | 必需参数 | risk |
|---|---|---|---|
| `lan_agent_read_text_file` | 分页读文本文件（默认 500 行/页），支持 line 和 byte-offset 分页；`has_more=true` 时自动返回 `next_start_line` 续读 | `file_path` | low |
| `lan_agent_tail_text_file` | 读文件尾部 N 行（默认 120），适合轮询最新构建/看门狗日志 | `file_path` | low |
| `lan_agent_list_directory` | 列目录（默认 200 条）；传 `trace_id` 时自动写目录读取 manifest，返回 `next_batch_tool_name=lan_agent_read_directory_files` | `directory_path` | low |
| `lan_agent_read_directory_files` | 按 `file_extensions_csv` 批读目录内匹配文件，单页+多文件接力直到 `batch_completion=complete`；内部复用 `read_text_file` | `directory_path` | low |

#### 目录批读链

```
lan_agent_list_directory(trace_id=X)
    │  写 manifest → next_batch_tool_name=lan_agent_read_directory_files
    ▼
lan_agent_read_directory_files(file_index=0, trace_id=X)
    │  内部调用 read_text_file → 单文件单页
    │  返回 next_file_index / next_start_line / next_call_json
    ▼
lan_agent_read_directory_files(file_index=N, start_line=M, trace_id=X)
    │  循环直到 batch_completion=complete
    ▼
完成（analysis_allowed=true）
```

> **注意**：当 `analysis_allowed=false` 或 `batch_completion=incomplete` 时，**不要重新列目录**，继续该链直到 manifest 所有文件读完。

#### `lan_agent_read_text_file` 参数

| 参数 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `file_path` | string | — (required) | 文件路径 |
| `max_lines` | integer | 500 | 每页行数 |
| `start_line` | integer | 1 | 起始行 |
| `start_byte_offset` | integer | 0 | 字节偏移（大文件单行体场景） |
| `primary_intent` | string | — | 主意图 |
| `trace_id` / `request_id` | string | — | 追踪标识 |

> 描述中明确推荐：对于注释清理/编辑流，应优先使用 `lan_agent_scan_text_ranges` + `lan_agent_prepare_edit_windows`，而非 `read_text_file`。

### 4.6 Task Memory 自检工具：`lan_agent_task_memory_new_chat_round_selftest`

`lan_agent_task_memory_new_chat_round_selftest` 是**新会话轮续接语义的自检工具**——在单次调用中编排 `freeze` → `resume_and_execute` → `delete_next_text_range_atomic`（通过 continuation budget runner），验证"换新对话"场景下 MCP 仅凭 `goal_id` 就能恢复并完成端到端任务，不依赖旧模型上下文。

#### 参数

| 参数 | 类型 | 说明 |
|---|---|---|
| `goal_id` | string | 可选，缺省生成 `new-chat-round-selftest-<timestamp>` |
| `trace_id` | string | 可选，缺省等于 goal_id |
| `max_steps` | integer | 可选，服务端上限 16，默认 5 |

#### 执行流程

1. 生成 `goal_id` / `mcp_conversation_id` / `mcp_round_id`
2. 写测试样本文件 `<log_root>/task_memory_new_chat_round_selftest/<goal_id>.cpp`（含待删注释）
3. 构造 `next_call_json` 指向 `lan_agent_delete_next_text_range_atomic`
4. 调用 `BuildTaskMemoryFreezeResult` 冻结归档 continuation
5. 调用 `BuildTaskMemoryResumeAndExecuteResult` 用 goal_id-only 语义恢复执行
6. 验证 `terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `verification_ok` / `comment_removed` / `mcp_round_established`

#### 关键返回字段

| 字段 | 含义 |
|---|---|
| `record_model` | `mcp_task_memory_new_chat_round_selftest_response_v1` |
| `selftest_pass` | bool，全部验证通过为 true |
| `llama_cpp_role` | `relay_only`（llama.cpp 被显式标记为中继） |
| `chat_context_reset_required` | `true`（客户端需自行 reset host chat） |
| `chat_context_reset_acknowledged` | `false until client ack` |
| `mcp_context_independence_verified` | `true`（MCP 上下文独立性已验证） |
| `fresh_entry_tool_name` | `lan_agent_task_memory_resume_and_execute` |
| `fresh_entry_arguments_scope` | `goal_id_only_plus_budget_controls` |

#### 产物文件

```
<log_root>/task_memory_new_chat_round_selftest/
├── <goal_id>.cpp                                          ← 测试样本
├── <goal_id>.json                                         ← selftest 报告
└── mcp_conversations/<goal_id>/round_0001.json            ← MCP round manifest
```

### 4.7 通用参数说明

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

### 4.8 语义网格参数说明

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

### 4.9 Task Memory 参数说明

| 参数 | 类型 | 说明 |
|---|---|---|
| `goal_id` | string | 任务目标稳定 ID（仅 `[A-Za-z0-9._-]`，其余字符自动替换为 `_`）；空值降级为 `default_goal` |
| `trace_id` | string | 单次执行追踪 ID（同一 `goal_id` 可有多条 trace） |
| `step_id` | string | 步骤 ID（append_step 写入 `step_ledger.jsonl`） |
| `step_index` | integer | 步骤序号 |
| `step_kind` | string | 步骤类型（如 `tool_call`/`analysis`/`review`） |
| `current_tool` | string | 当前调用的 MCP 工具名 |
| `status` | string | 步骤状态（`success`/`partial`/`failed`/`pending` 等） |
| `summary` | string | 步骤自然语言摘要 |
| `result_ref` | string | 步骤结果引用路径（artifact 路径或 JSON 文件） |
| `evidence_ref` | string | 步骤证据引用路径 |
| `next_call_json` | string | 下一步 MCP 调用 JSON（`{"name":"...","arguments":{...}}`） |
| `has_more` | boolean | 是否还有后续步骤 |
| `terminal_state` | boolean | 是否终态（任务完成） |
| `completion_claim_allowed` | boolean | 是否允许声明任务完成 |
| `compact_summary` | string | 紧凑摘要（写 `latest_resume_context.json`） |
| `remaining_work` | string | 剩余工作描述 |
| `current_state_markdown` | string | 当前状态 Markdown（freeze 用） |
| `key_slices_jsonl` | string | 关键切片 JSONL（freeze 用） |
| `incremental_index_manifest_json` | string | 增量索引清单 JSON |
| `migration_handover_markdown` | string | 迁移交接 Markdown |
| `max_steps` / `step_budget` | integer | continuation budget 步数上限 |
| `dry_run` | boolean | 默认 true，只写预算计划不执行 |
| `execute` | boolean | 必须为 true 且 `dry_run=false` 才真正执行 |
| `key` | string | KV 显式键（kv_lookup / rocksdb_lookup / parity_check） |
| `kind` | string | KV selector：`goal\|latest\|resume_context\|trace\|trace_step\|slice\|budget\|trace_budget` |
| `prefix` | boolean | 前缀匹配 |
| `limit` / `offset` | integer | KV 查询分页 |
| `include_value` | boolean | 是否返回 value（默认 true） |
| `rocksdb_path` | string | 显式 RocksDB 目录（默认 `task_memory/<goal_id>/rocksdb_native`） |
| `max_final_steps` | integer | migration_acceptance 最终 continuation budget，默认 8 |

### 4.10 compile_commands.json 发现顺序

1. 显式 `compilation_database_path` → 直接使用
2. 显式 `compile_db_dir` → 拼接 `compile_commands.json`
3. `project_root` + 常见构建目录 → 自动搜索 `build/`, `AIbuild/`, `cmake-build-*/`
4. 均未找到 → 降级为无编译数据库模式（`compile_db_mode=none`，复杂文件可能失败）

### 4.11 CMM 工具清单（codebase-memory-mcp 桥接）

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
| L0 | `tools/list` | 发现可用工具和能力（默认只返回 `lan_agent_mcp_route` 单一网关入口） |
| GW | `lan_agent_mcp_route` | **单一网关入口**：mode=overview/route/call 三模式收敛所有内部工具 |
| L1 | `lan_agent_run_clang_ast_parser` | 获取文件级 AST 概览（函数列表、类结构、调用引用） |
| L2 | `lan_agent_build_cfg` | 获取函数级控制流（基本块、分支、圈复杂度） |
| L3 | `lan_agent_build_call_graph` | 获取文件级调用关系 |
| L4 | `lan_agent_build_dfg` | 获取数据流（def/use 边、过程间绑定） |
| L5 | `lan_agent_build_program_slice` | 获取符号级切片（backward/forward） |
| L6 | `lan_agent_query_*_artifact` | 从已写入的 artifact 二次查询，无需重跑 Clang |
| SG | `lan_agent_semantic_grid_*` | 长文本语义网格：解构、归纳、检索、溯源、上下文重构、增量 |
| TM | `lan_agent_task_memory_*` | 长任务记忆：freeze / append_step / continuation budget / **resume_and_execute（fresh-chat 一次性续接）** / kv snapshot / rocksdb mirror / parity check / structure manifest / migration acceptance / **new_chat_round_selftest（续接自检）** |
| FA | `lan_agent_read_text_file` / `tail_text_file` / `list_directory` / `read_directory_files` | 文件访问：分页读 / 尾部读 / 列目录 / 批读目录文件 |

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
9. **长任务先 freeze 再 resume**：进入上下文压力或需要换模型时，先 `task_memory_freeze` 落盘当前状态；新模型首读 `task_memory_resume_context` 而不是回放全历史。
10. **continuation 不在模型侧循环**：当 `has_more=true` 且 `completion_claim_allowed=false` 时，调 `task_memory_execute_continuation_budget(execute=true)` 让 MCP 服务端按预算推进，避免模型上下文无限膨胀。
11. **RocksDB 镜像不替换源真**：`rocksdb_mirror` 完成后必须 `rocksdb_parity_check` 通过才允许在 read path 上使用，`safe_to_replace_source_of_truth` 必须保持 `false`。

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

### 5.7 长任务记忆续接流程

```
场景：本地模型已处理 30 步 RAG 长任务，上下文接近窗口上限，需要换一个新模型续接。

旧模型退出前：
  1. lan_agent_task_memory_freeze(
       goal_id="rag-repo-scan-v1",
       trace_id="trace-20260809-001",
       current_state_markdown=<当前状态 markdown>,
       compact_summary="已完成 8/12 个仓库扫描，剩余 4 个待 ingest...",
       remaining_work="ingest repo #9-#12, build global index, run parity check",
       next_call_json='{"name":"lan_agent_semantic_grid_incremental_update","arguments":{...}}',
       terminal_state=false,
       completion_claim_allowed=false)
     → 写入 logs/task_memory/rag-repo-scan-v1/latest_resume_context.json
     → 追加 step_ledger.jsonl / slices.jsonl / index_manifest.json

新模型启动后（无需读全历史）：
  1. lan_agent_task_memory_resume_context(goal_id="rag-repo-scan-v1")
     → 返回 compact_summary, next_call_json, remaining_work, terminal_state
  2. 按 next_call_json 继续调用 MCP 工具
  3. 每完成一步：lan_agent_task_memory_append_step(goal_id=..., step_kind="tool_call",
       status="success", summary="...", has_more=true, completion_claim_allowed=false)
  4. 若需要服务端自动推进 N 步：
     lan_agent_task_memory_execute_continuation_budget(
       goal_id="rag-repo-scan-v1", dry_run=false, execute=true, max_steps=5)
     → MCP 服务端执行 5 步 allowlisted continuation，写 step_ledger，
       返回 completion_claim_allowed=false + 新的 next_call_json
  5. 任务完成后：terminal_state=true, completion_claim_allowed=true
```

> **fresh-chat 简化路径**：若 `goal_id` 已 freeze 过，新会话直接调一次
> `lan_agent_task_memory_resume_and_execute(goal_id=..., max_steps=10)`
> 即可，无需手动走 `resume_context` + `execute_continuation_budget` 链。详见 9.4.1 / 9.9。

### 5.8 local AI 上下文治理（Overview 字段）

`lan_agent_mcp_overview` 工具返回一组 `local_ai_*` 治理字段，约束本地模型在 MCP 协作中的上下文使用、会话关闭与任务交接行为。这些字段与 Task Memory 配套，构成"模型上下文最小化 + 跨会话干净交接"的完整契约。

| 字段 | 作用 |
|---|---|
| `local_ai_context_policy` | 模型上下文必须 task-minimal：只含 current goal / current file / 一个 next MCP call / completion gate / artifact refs；**禁止** paste `tools/list` schemas / 全历史对话 / 全日志 / 全 artifact JSON 进 prompt |
| `local_ai_conversation_close_policy` | 每个 interrupted/finished 会话必须保留 `conversation_close_status`；新会话先调 `next_chat_status_check_arguments_json` 或 `lan_agent_task_memory_resume_context`，再执行续接工作 |
| `local_ai_context_bootstrap_json` | `record_model=mcp_context_bootstrap_v1`，显式列出 `include`（user_goal / goal_id / trace_id / current_file / required_tool_name / required_tool_arguments_json / clean_chat_close_allowed / new_chat_entry_arguments_json / completion_gate / result_ref / evidence_ref）与 `exclude`（full_tools_list / tool_schemas / old_chat_history / full_file_content / full_log_content / full_artifact_json），`lookup_policy=use refs and MCP query tools on demand` |
| `local_ai_long_loop_policy` | 长循环场景：freeze 一次 task_memory；新会话直接调 `lan_agent_task_memory_resume_and_execute` 续接（不再要求手动链式调用） |
| `local_ai_completion_gate` | 只有 `terminal_state=true` + `completion_claim_allowed=true` + `final_answer_allowed=true` + `verification_ok=true` 才能声明完成；`clean_chat_close_allowed=true` 仅代表当前会话可安全交接，**不代表任务完成** |
| `local_ai_guidance_json` | 结构化指引 v1，含 `context` / `file_ops` / `comment_cleanup` / `long_loop` / `fresh_chat_resume` / `clean_handoff` / `conversation_close_status` / `completion_gate` 八个子段 |

**关键语义**：
- `clean_chat_close_allowed=true` ≠ 任务完成 —— 仅代表当前会话可以安全停止并把工作交给下一个会话。
- `new_chat_entry_arguments_json` —— 由 `latest_resume_context.json` 回填，新会话首调用入口（通常指向 `lan_agent_task_memory_resume_and_execute`）。
- `handoff_completion_claim=not_task_complete` —— 交接时禁止使用任务完成的措辞。

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

### 案例五：Task Memory 冻结 + 续接 + KV 快照

```powershell
# 1. 冻结当前长任务状态
$freezeBody = @{
    jsonrpc = "2.0"
    id = "tm-freeze"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_freeze"
        arguments = @{
            goal_id = "rag-repo-scan-v1"
            trace_id = "trace-20260809-001"
            compact_summary = "已完成 8/12 仓库扫描，剩 4 个待 ingest"
            remaining_work = "ingest repo #9-#12, build global index"
            next_call_json = '{"name":"lan_agent_semantic_grid_incremental_update","arguments":{...}}'
            terminal_state = $false
            completion_claim_allowed = $false
        }
    }
} | ConvertTo-Json -Depth 16 -Compress

$freeze = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $freezeBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# latest_resume_context_path=.../task_memory/rag-repo-scan-v1/latest_resume_context.json
# step_ledger_path=.../step_ledger.jsonl

# 2. 新模型首读 resume_context
$resumeBody = @{
    jsonrpc = "2.0"
    id = "tm-resume"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_resume_context"
        arguments = @{ goal_id = "rag-repo-scan-v1" }
    }
} | ConvertTo-Json -Depth 16 -Compress

$resume = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $resumeBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# 返回 compact_summary / next_call_json / remaining_work / terminal_state

# 3. 构建 KV 快照（为 RocksDB 镜像做准备）
$kvBody = @{
    jsonrpc = "2.0"
    id = "tm-kv"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_build_kv_snapshot"
        arguments = @{ goal_id = "rag-repo-scan-v1" }
    }
} | ConvertTo-Json -Depth 16 -Compress

$kv = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $kvBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# kv_snapshot_index_path=.../kv_snapshot/index.jsonl
# kv_record_count>0

# 4. 按 selector 查询 KV
$lookupBody = @{
    jsonrpc = "2.0"
    id = "tm-lookup"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_kv_lookup"
        arguments = @{ goal_id = "rag-repo-scan-v1"; kind = "latest" }
    }
} | ConvertTo-Json -Depth 16 -Compress

$lookup = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $lookupBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success, hit=true, value_json contains latest_resume_context
```

#### 5.a fresh-chat `resume_and_execute` 续接闭环（真实对话·删除 Image.cpp 注释）

以下来自真实 gemma-4-E4B 会话（会话 ID `aef8967e`，任务：删除 `Image.cpp` 中全部注释，4690 行 / 155 KB / 241 条注释，预计超过本地模型上下文窗口）。展示了"首帧探测 → 遇到长循环 → freeze → budget_run(10 步) → 新会话 resume_and_execute"的完整闭环，模型每一步严格遵循 MCP 返回的 `required_tool_arguments_json`，**没有手动构造参数**。

```
Turn 1  User:  删除 Image.cpp 中所有注释
      Model: lan_agent_probe_text_file(file=Image.cpp, primary_intent=delete_comments)
      MCP:   recommended_next_tool=delete_text_range_window_atomic
             next_call_json={
               name:lan_agent_delete_text_range_window_atomic,
               arguments:{start_line:1, max_lines:200, scan_mode:comments, ...}
             }

Turn 2  Model: lan_agent_delete_text_range_window_atomic(按 next_call_json 原样调用)
      MCP:   has_more=true, total_range_count_before=241 (共 241 条注释)
             completion_guard=NON_TERMINAL_RESULT  ← 长循环闸门
             required_tool_name=lan_agent_task_memory_freeze  ← 强制 freeze
             required_tool_arguments_json={
               goal_id:"trace-delete_window-20260810_102404",
               current_goal:"continue comment cleanup until completion gate",
               current_tool:"lan_agent_delete_text_range_window_atomic",
               terminal_state:false, completion_claim_allowed:false,
               next_call_json:"下一个 delete 窗口调用 (start_line:201)"
             }

Turn 3  Model: lan_agent_task_memory_freeze(按 required_tool_arguments_json 原样调用)
      MCP:   semantic_outcome=task_memory_frozen
             task_memory_root=.../logs/task_memory/trace-delete_window-20260810_102404
             latest_resume_context.json, step_ledger.jsonl, slices.jsonl 已写入
             clean_chat_close_allowed=true, conversation_close_status=handoff_ready_not_complete
             ← 注意：conversation_close_allowed≠任务完成
             next_call_json → lan_agent_task_memory_execute_continuation_budget(goal_id=..., max_steps=10)

Turn 4  Model: lan_agent_task_memory_execute_continuation_budget(按 next_call_json)
      MCP:   executed_step_count=10, budget_exhausted=true
             last_verified_step=10  ← 已跑 10 次 bounded 200-line delete 窗口
             last_tool=lan_agent_delete_text_range_window_atomic
             last_status=needs_continue, terminal_state=false
             budget_run_id=budget-73adc8b4aaacc6c4
             ← 新会话续接路径：
             new_chat_entry_tool_name=lan_agent_task_memory_resume_and_execute
             new_chat_entry_arguments_json={
               name:lan_agent_task_memory_resume_and_execute,
               arguments:{goal_id:"trace-delete_window-20260810_102404", max_steps:10, execute:true}
             }
             conversation_close_status=handoff_ready_not_complete
             next_chat_must_verify_fields=[
               terminal_state, completion_claim_allowed, final_answer_allowed,
               verification_ok, clean_chat_close_allowed, conversation_close_status,
               new_chat_entry_arguments_json
             ]

=== 模型切换 / 重启会话（读取上面的 new_chat_entry_arguments_json 即续接，不重读旧对话）===

Turn 5  Model: lan_agent_task_memory_resume_and_execute(按 new_chat_entry_arguments_json)
      MCP:   resume_execute_mode=read_resume_context_and_execute_budget
             inner_record_model=mcp_task_memory_execute_continuation_budget_response_v1
             last_verified_step=20   ← 累计 20 次 delete 调用
             budget_status=blocked, blocked (遇到跨 200-line 边界的长注释块,
             block_reason=NEXT_FLOW_SAFETY_CLASS_NOT_READY,
             提示需用 lan_agent_delete_next_text_range_atomic 精修单步删除)
             terminal_state=false
             required_tool_name=lan_agent_task_memory_resume_and_execute
             required_tool_arguments_json=(相同参数，再次调用本工具)
             ← 模型继续重复调用 resume_and_execute，直到 terminal_state=true
```

**关键点：**
1. 模型**始终按 MCP 返回的 `required_tool_arguments_json` 原样调用**，不需要自己构造任何参数；
2. `budget_run_id` / `last_verified_step` 每轮续接都会累加；
3. `conversation_close_allowed=true` **不代表** `terminal_state=true`，只是当前会话可以安全关闭交接给下一个；
4. `new_chat_entry_arguments_json` 指向 `lan_agent_task_memory_resume_and_execute`，新模型首调即续接，无需读取旧对话历史。

---

### 案例六：RocksDB 镜像 + 一致性校验

```powershell
# 1. 镜像 KV 快照到 RocksDB（需 CODEX_LAN_AGENT_WITH_ROCKSDB=ON 编译）
$mirrorBody = @{
    jsonrpc = "2.0"
    id = "tm-mirror"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_rocksdb_mirror"
        arguments = @{ goal_id = "rag-repo-scan-v1" }
    }
} | ConvertTo-Json -Depth 16 -Compress

$mirror = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $mirrorBody -ContentType "application/json; charset=utf-8" -TimeoutSec 120).result.structuredContent
# status=success
# rocksdb_mirror_manifest_path=.../rocksdb_mirror_manifest.json
# source_of_truth=file_object_store  (始终保持)
# safe_to_replace_source_of_truth=false

# 2. 一致性校验（同一 selector 比对文件 KV vs RocksDB）
$parityBody = @{
    jsonrpc = "2.0"
    id = "tm-parity"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_rocksdb_parity_check"
        arguments = @{ goal_id = "rag-repo-scan-v1"; kind = "latest" }
    }
} | ConvertTo-Json -Depth 16 -Compress

$parity = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $parityBody -ContentType "application/json; charset=utf-8" -TimeoutSec 60).result.structuredContent
# status=success
# parity_status=pass
# safe_to_replace_source_of_truth=false  (校验通过也不允许换源真)
```

### 案例七：一站式迁移验收（MCP-native）

```powershell
# 单次调用跑完整迁移验收链：freeze → budget → kv → mirror → parity → manifest
$accBody = @{
    jsonrpc = "2.0"
    id = "tm-accept"
    method = "tools/call"
    params = @{
        name = "lan_agent_task_memory_migration_acceptance"
        arguments = @{ max_final_steps = 8 }
    }
} | ConvertTo-Json -Depth 16 -Compress

$acc = (Invoke-RestMethod -Uri "http://127.0.0.1:18080/mcp" -Method Post -Body $accBody -ContentType "application/json; charset=utf-8" -TimeoutSec 300).result.structuredContent
# 预期字段：
# migration_acceptance_status=ACCEPTED
# acceptance_status=complete
# semantic_outcome=TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS
# source_of_truth=file_object_store
# active_read_backend=rocksdb_native_mirror
# write_backend=file_object_store
# safe_to_replace_source_of_truth=false
# parity_required_for_native_reads=true
```

### 案例八：真实对话·删除 C++ 源文件多余回车换行（单步短任务闭环）

会话 ID `14ae0da8`（gemma-4-E4B，2 turns）。目标：`Image.cpp` 有多余空行和格式问题，请求"删除多余回车换行"。展示了**短任务的 completion_gate 打开流程**（1 次工具调用 → verification_ok=true → 可直接 claim 完成，无需 Task Memory）。

```
Turn 1  User:  删除 Image.cpp 中多余的回车换行
      Model: lan_agent_format_code_file(source_file=Image.cpp, dry_run=false)
             ← 模型从工具语义直接选定 clang-format，未做多余探测

      MCP  result_envelope (精简):
        - old_hash=866862e0d1e2dfa0         ← 文件原哈希
        - new_hash=071113de9d943dd6         ← 格式化后哈希（不同=实际有改动）
        - source_bytes=140563 → formatted_bytes=132938   ← 减少 7.6 KB (5.4%)
        - would_change=true, changed=true   ← 确认发生真实改动
        - backup_path=.../code_format/Image.cpp_.../Image.cpp.before
        - formatter_path=VS2022 clang-format (fallback_style=LLVM, style=file)
        - CLIPS 后处理（mcp_result_guard.clp）:
            risk=medium → safety_class=controlled → execution_class=controlled
            decision=allow, verification=verified
            matched_rule=default-mcp-result-verified
            2 CLIPS facts asserted, alarm=false

        - 最终闸门（completion_gate）:
            verification_status=verified
            verification_ok=true
            terminal_state=true
            task_done=true
            completion_claim_allowed=true
            final_answer_allowed=true
            outcome=PASS
            supervision_status=closed_loop_complete
            acceptance_status=complete
            ai_conclusion_valid=true

Turn 2  Model: "Image.cpp 已格式化，删除了多余回车换行，文件已验证。"
              ← 模型直接输出自然语言总结（final_answer_allowed=true 放行）
```

**关键不变量：**
1. `verification_ok=true` 是 `final_answer_allowed=true` 的前置条件；
2. 没有 freeze / resume_context / budget，因为 1 步完成且 `terminal_state=true`；
3. `lan_agent_format_code_file` 直接写盘 + 留备份（`backup_path`），可审计回滚；
4. `clips_post_result_chain_clips_required=true` → 所有文件写操作都会经 CLIPS 规则引擎做安全分级，风险 medium 以下且 verification_ok=true 才放行。

### 案例九：真实对话·删除 C++ 源文件全部注释（长任务多会话 Task Memory 续接）

会话 ID `aef8967e`（gemma-4-E4B，29 turns，覆盖 `trace_id=trace-delete_window-20260810_102404`）。目标：4690 行 / 155 KB / 241 条注释的 `Image.cpp`，预期超过本地模型上下文。展示了**"探测 → 长循环闸门触发 → freeze → budget_run(10 步) → fresh-chat resume_and_execute（再跑 10 步）"**的完整闭环。

**执行全景统计（来自对话 turn timing）：**

| 阶段 | 工具名 | Turn | 说明 |
|---|---|---|---|
| 1 探测 | `lan_agent_probe_text_file` | T1 | 推荐使用 `delete_text_range_window_atomic`，推荐窗口 200 行 |
| 2 首窗口 | `lan_agent_delete_text_range_window_atomic` | T2 | `has_more=true`，共 241 条注释待删；**completion_guard=NON_TERMINAL_RESULT**，闸门强制要求 freeze，禁止模型 claim / 输出自然语言 |
| 3 冻结 | `lan_agent_task_memory_freeze` | T3 | 写 `latest_resume_context.json` / `step_ledger.jsonl` / `slices.jsonl` / `rag_thread_migration/*`；`clean_chat_close_allowed=true`（**但** `conversation_close_status=handoff_ready_not_complete` ≠ 任务完成）；返回 `new_chat_entry_tool_name=lan_agent_task_memory_resume_and_execute` |
| 4 Budget(10) | `lan_agent_task_memory_execute_continuation_budget` | T4 | 跑了 10 次 bounded delete 调用，写 `budget_runs/budget-73adc8b4aaacc6c4.json`；`last_verified_step=10`；`budget_exhausted=true` 仍 `terminal_state=false` |
| 5 Resume+Exec | `lan_agent_task_memory_resume_and_execute` | T5 | **fresh-chat 入口**：读 `latest_resume_context` 再跑 10 步，累计 `last_verified_step=20`；写入 `budget_runs/budget-89f4c850fd0031c7.json`；遇到跨窗口边界的长注释块（`block_reason=NEXT_FLOW_SAFETY_CLASS_NOT_READY`），Budget `blocked` → 返回 `required_tool_name=self`（继续调 `resume_and_execute`）→ 预算耗尽 → T6~T29 继续调 `delete_next_text_range_atomic` / `delete_window` 直到 241 条注释全部清除 |
| 6+ 续接 | `delete_next_text_range_atomic` + `delete_window` 交替 | T7~T29 | 边界长注释用单步精修，其余 200 行窗口用批量删除 |

**Task Memory 产物（来自 MCP 返回的实际文件路径）：**

```
logs/task_memory/trace-delete_window-20260810_102404/
├── latest_resume_context.json          ← 每轮 freeze / budget / resume_and_execute 都会刷新
├── step_ledger.jsonl                   ← 每步追加（共 ≥20 条已验证 step 条目）
├── slices.jsonl                        ← freeze 时写入 key slice（summary, trace_id, dedup_hash）
├── index_manifest.json                 ← 增量索引清单
├── memory_structure.json               ← (后续写) 结构契约 bootstrap
├── rag_thread_migration/
│   ├── 1_current_state.md
│   ├── 2_key_slices.jsonl
│   ├── 3_incremental_index_manifest.json
│   └── 4_migration_handover.md
├── budget_runs/
│   ├── budget-73adc8b4aaacc6c4.json    ← Turn 4 execute_continuation_budget: 10 steps
│   └── budget-89f4c850fd0031c7.json    ← Turn 5 resume_and_execute: 10 steps (blocked on boundary)
 kv_snapshot/index.jsonl              ( build_kv_snapshot)
 rocksdb_native/                      ( rocksdb_mirror,  WITH_ROCKSDB=ON)
 rocksdb_mirror_manifest.json
```

** 1:1 **
1. **`completion_guard=NON_TERMINAL_RESULT`** `lan_agent_delete_text_range_window_atomic`  `has_more=true`  MCP  clamp  `tool_call_only`
2. **freeze  budget  resume_and_execute **
   - `freeze` +  next call  budget runner
   - `execute_continuation_budget` MCP  N  allowlisted  10 64 `budget_runs/*.json`****
   - `resume_and_execute``fresh-chat`  =  resume_context +  budget runner / ""
3. **  **`clean_chat_close_allowed=true` **** `terminal_state=true + completion_claim_allowed=true + final_answer_allowed=true + verification_ok=true`  claim 
4. **blocked **budget runner " 200-line " `blocked` `delete_next_text_range_atomic` `NEXT_FLOW_SAFETY_CLASS_NOT_READY` 

---

## 7. Artifact 

### 7.1 

```
                          
                          
build_dfg                        query_dfg_artifact
   dfg.json           
   dfg.dot                         (offset_edges / max_edges)
   summary.json        (focus_symbol / neighborhood_depth)
                                     (output_dir)
```

### 7.2 

```powershell
#  summary.json  DFG
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

### 7.3 

|  |  |
|---|---|
| `offset_functions` | CFG N  |
| `max_functions` | CFG N  |
| `offset_edges` | DFG/CallGraph/Slice N  |
| `max_edges` | DFG/CallGraph/Slice N  |
| `max_nodes` |  N  |

---

## 8. 

### 8.1 

Semantic Grid** Clang **

- ****
- **** L1-L5 
- ****`context_bundle` 
- **** `content_hash` 
- ****`trace_source`  `section_path` 

### 8.2 

```
L1_META        1 
   L2_DOMAIN 1  domain 
        L3_FLOW  / fragment_type 
             L4_ATOM   fragment 
                  L5_RAW   content_text
```

****
- `contains`L1L2L3L4L5
- `source_trace`L4  L5 
- `sequence` L4 

### 8.3 Fragment 

 fragment 

|  |  |
|---|---|
| `fragment_id` | `frag_1`, `frag_2`, ... |
| `source_file` |  |
| `source_kind` | `md` / `txt` / `complex_markdown` / `incremental_markdown` |
| `section_path` | Markdown  ` > ` |
| `content_hash` | FNV-1a hash |
| `source_line_start` / `source_line_end` |  |
| `fragment_type` | `boundary_rule` / `condition_statement` / `action_step` / `term_definition` |
| `content_text` |  |
| `keyword_tags` |  |

### 8.4  +  +  + 

```powershell
# 1. 
$buildBody = @{
    jsonrpc = "2.0"
    id = "sg-build"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_build"
        arguments = @{
            source_text = "# `n## `n..."
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

# 2. fuzzy 
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

# 3. 
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

# 4. 
$bundleBody = @{
    jsonrpc = "2.0"
    id = "sg-bundle"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_context_bundle"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path
            task_intent = "  priority source trace"
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

### 8.5 

```powershell
#  1 
$inc1Body = @{
    jsonrpc = "2.0"
    id = "sg-inc1"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_incremental_update"
        arguments = @{
            artifact_summary_path = $build.artifact_summary_json_path  #  base  summary
            source_text = "## `n fragment id ..."
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

# 
$dupBody = @{
    jsonrpc = "2.0"
    id = "sg-dup"
    method = "tools/call"
    params = @{
        name = "lan_agent_semantic_grid_incremental_update"
        arguments = @{
            artifact_summary_path = $inc1.artifact_summary_json_path  #  inc1  summary
            source_text = "## `n fragment id ..."  # 
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
# skipped_duplicate_fragment_count=4   
# new_fragment_count=15 (), new_node_count=36 ()
```

### 8.6 Artifact 

```
output_dir/
 semantic_grid.json       fragments + nodes + edges
 nodes.json               
 edges.json               
 summary.json             artifact_*_path 
 delta_fragments.json      incremental_update
 delta_nodes.json          incremental_update
```

** summary ** `incremental_update`  `artifact_summary_json_path`  `artifact_summary_path` 

### 8.7 

|  |  |  |
|---|---|---|
| substring |  |  |
| fuzzy | `fuzzy_match=true` | token  /  |
| regex | `regex_match=true` | icase |

****`offset` / `limit` / `has_more` / `next_offset_or_null` / `pagination_status`

---

## 9. Task Memory 

### 9.1 

 RAG

1. ****30+ 
2. ****/
3. ****

Task Memory "" MCP  `latest_resume_context.json` 

### 9.2 

 Task Memory  `<data_root>/task_memory/{goal_id}/`

```
<data_root>/task_memory/{goal_id}/
 latest_resume_context.json    compact_summary + next_call_json + new_chat_entry_arguments_json
 step_ledger.jsonl              JSON
 slices.jsonl                  key slices
 index_manifest.json           
 memory_structure.json         structure_manifest 
 current_state.md               Markdownfreeze 
 handover.md                    Markdown
 rag_thread_migration/         RAG 
    ...
 budget_runs/                  continuation budget execute_continuation_budget / resume_and_execute 
    budget-{checksum}.json     budget run record_model=mcp_continuation_budget_run_v1 step_events[]
 kv_snapshot/                   KV build_kv_snapshot 
    index.jsonl
 rocksdb_native/                RocksDB rocksdb_mirror 
    ...
 rocksdb_mirror_manifest.json  RocksDB 
```

### 9.3  bootstrap 

`structure_manifest`  `memory_structure.json`

|  |  |  |
|---|---|---|
| **** | `latest_resume_context.json` |  `compact_summary` / `next_call_json` / `remaining_work` / `terminal_state` |
| **** | `memory_structure.json` |  |
| **** | `lan_agent_task_memory_rocksdb_lookup`native mirror ready  |  RocksDB  |
| **** | **`forbidden_by_default`** |  `step_ledger.jsonl` |

### 9.4 

#### 9.4.1 fresh-chat  goal_id 

 goal  freeze  `task_memory/{goal_id}/latest_resume_context.json` **** `resume_context` + `execute_continuation_budget`

```json
{
  "name": "lan_agent_task_memory_resume_and_execute",
  "arguments": { "goal_id": "< goal_id>", "max_steps": 10 }
}
```

 `latest_resume_context.json`   bounded continuation budget     `budget_runs/budget-{checksum}.json`

- `continue_required=true`   `required_tool_arguments_json`  `terminal_state=true`
- `budget_requires_frozen_resume_context=true`   freeze `lan_agent_task_memory_freeze`
- `terminal_state=true`   `verification_ok=true`  final claim

#### 9.4.2 

**** [TASK_MEMORY_MIGRATION_ACCEPTANCE.md](TASK_MEMORY_MIGRATION_ACCEPTANCE.md)

1. `lan_agent_task_memory_freeze`   `latest_resume_context.json` / `step_ledger.jsonl` / `slices.jsonl` / `index_manifest.json` / `rag_thread_migration/*`
2. `lan_agent_task_memory_resume_context`  �读（或直接用 9.4.1 的 `resume_and_execute`）
3. `lan_agent_task_memory_execute_continuation_budget` —— 执行 allowlisted bounded continuation；非终态预算耗尽必须返回 `terminal_state=false` / `completion_claim_allowed=false` / `final_answer_allowed=false`
4. `lan_agent_task_memory_build_kv_snapshot` —— 构建文件 KV 快照（源真仍是文件对象层）
5. `lan_agent_task_memory_rocksdb_mirror` —— 镜像 KV 快照到原生 RocksDB（`CODEX_LAN_AGENT_WITH_ROCKSDB=ON` 时）；RocksDB 角色为 `mirror_read_backend`，**不替换源真**
6. `lan_agent_task_memory_rocksdb_parity_check` —— 原生读路径必须通过 parity check，`safe_to_replace_source_of_truth` 必须保持 `false`
7. `lan_agent_task_memory_structure_manifest` —— 写 `memory_structure.json`，固化 bootstrap 契约

### 9.5 不可协商不变量

以下不变量在任何场景下都必须保持，违反即视为迁移失败：

| 不变量 | 值 |
|---|---|
| `source_of_truth` | `file_object_store` |
| `write_backend` | `file_object_store` |
| `native_backend_role` | `mirror_read_backend` |
| `safe_to_replace_source_of_truth` | `false` |
| `parity_required_for_native_reads` | `true` |
| `required_model_read` | `latest_resume_context.json` |

### 9.6 MCP-native 验收（推荐路径）

启动 MCP 服务后，其他 AI 客户端应优先使用 MCP 原生验收工具：

```json
{
  "name": "lan_agent_task_memory_migration_acceptance",
  "arguments": {
    "max_final_steps": 8
  }
}
```

预期 MCP 返回字段：

```text
migration_acceptance_status=ACCEPTED
acceptance_status=complete
semantic_outcome=TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS
source_of_truth=file_object_store
active_read_backend=rocksdb_native_mirror
write_backend=file_object_store
safe_to_replace_source_of_truth=false
parity_required_for_native_reads=true
```

此路径在 MCP 内部运行完整验收链，无需客户端 PowerShell 脚本，是仅通过 MCP 连接的其他 AI 客户端的正确路径。

### 9.7 外部 Smoke 命令（仅 CI/运维用）

PowerShell 脚本仅作为 HTTP MCP 端点和已部署进程的运维/CI smoke 测试：

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\run_task_memory_migration_acceptance.ps1
```

预期终态行：

```text
TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS
```

### 9.8 KV 快照键 schema

`build_kv_snapshot` 按以下键 schema 把文件对象索引到 `kv_snapshot/index.jsonl`，与后续 RocksDB 后端一致：

| key 模式 | 含义 |
|---|---|
| `goal/{goal_id}` | goal 元信息 |
| `latest/{goal_id}` | 最新 resume_context 指针 |
| `trace/{trace_id}/{step_id}` | 单步 trace 记录 |
| `slice/{slice_id}` | 关键切片记录 |
| `budget/{budget_run_id}` | continuation budget 运行记录 |

`kv_lookup` / `rocksdb_lookup` 支持显式 `key` 或 `kind` selector（`goal` / `latest` / `resume_context` / `trace` / `trace_step` / `slice` / `budget` / `trace_budget`）。

### 9.9 fresh-chat 一次性续接：`resume_and_execute` 详解

`lan_agent_task_memory_resume_and_execute` 是 fresh-chat 场景下的单一入口工具，把"读 resume context + 跑 continuation budget + 刷新任务记忆"压缩成一次调用，避免新会话手动链式调用 `resume_context` + `execute_continuation_budget`。

#### 参数

| 参数 | 类型 | 必需 | 默认 | 说明 |
|---|---|---|---|---|
| `goal_id` | string | 是 | — | 已归档任务的 goal ID |
| `trace_id` | string | 否 | 继承自 resume_context | 追踪 ID |
| `max_steps` | integer | 否 | 10 | MCP 续跑步数上限，server policy 上限 64 |
| `step_budget` | integer | 否 | — | `max_steps` 别名 |
| `dry_run` | boolean | 否 | `false` | 仅规划预算（区别于 `execute_continuation_budget` 的默认 `true`） |
| `execute` | boolean | 否 | `true` | 是否真正执行白名单续跑（区别于 `execute_continuation_budget` 的默认 `false`） |

#### 返回字段

| 字段 | 含义 |
|---|---|
| `record_model` | `mcp_task_memory_resume_and_execute_response_v1` |
| `inner_record_model` | 委派的 budget 子结果的 record_model |
| `resume_execute_entry` | 恒为 `"true"` |
| `resume_execute_mode` | `read_resume_context_and_execute_budget`（执行）或 `read_resume_context_and_plan_budget`（dry_run） |
| `max_steps` | 实际生效的步数上限（clamp 到 1~64） |
| `goal_id` / `trace_id` / `budget_run_id` / `budget_status` | 透传自 budget 子结果 |
| `terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `verification_ok` | 终止与验证字段（透传） |
| `required_next_action_type` / `required_tool_name` / `required_tool_arguments_json` / `next_call_json` | 仅 `continue_required=true` 分支出现，`required_tool_name` 为本工具自身 |
| `resume_recovery_status` / `resume_recovery_tool_name` / `resume_recovery_instruction` | 仅 `budget_requires_frozen_resume_context=true` 分支出现，指引先调 `lan_agent_task_memory_freeze` |

#### 三种分支

1. **`continue_required=true`**（非终态预算耗尽）—— 模型按返回的 `required_tool_arguments_json` 再次调用本工具，直到 `terminal_state=true`。**禁止**改写参数或插入其他工具调用。
2. **`budget_requires_frozen_resume_context=true`**（归档缺失）—— 上一任务未 freeze，先调 `lan_agent_task_memory_freeze` 写入 `latest_resume_context.json`，再回到本工具。
3. **`terminal_state=true`**（终态）—— 仍需 `verification_ok=true` 才能 final claim；否则按 `next_action` 字段指引修复验证。

#### 产物文件

每次调用写入 `task_memory/{goal_id}/budget_runs/budget-{checksum}.json`，内容为 `record_model=mcp_continuation_budget_run_v1` 的 budget run 记录（含 `step_events[]` 数组、`executed_step_count`、`budget_exhausted`、`blocked`、`block_reason`）。同时每步追加到 `step_ledger.jsonl`，并刷新 `latest_resume_context.json`。

---

## 10. 测试脚本与一键验证

### 10.1 一键脚本 1：模板回归

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_analysis_templates_1_11.ps1
```

覆盖：`tools/list`、AST、CFG、CallGraph、DFG、Slice、artifact query、分页、path-sensitive metadata。

### 10.2 一键脚本 2：复杂项目分析

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\analysis_client_examples.ps1 `
  -SourceFile D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cximage\FastMatch.cpp `
  -ProjectRoot D:\Codex-WorkDir\Sean_WorkDir\cxvisionai `
  -FocusSymbol center_x
```

覆盖：复杂项目（FastMatch.cpp）完整分析链，DFG/Slice 的 `focus_symbol` 邻域提取、过程间绑定验证。

> **注意**：DFG 对复杂文件可能需要 180-300s，脚本默认超时 180s。如超时，手动以 300s 超时重跑。

### 10.3 手动 MCP 调用模板

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

### 10.4 一键脚本 3：语义网格基础 Smoke

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_semantic_grid_smoke.ps1
```

覆盖：`tools/list`（6 工具注册）、`ingest_text`（解构）、`build`（L1-L5 金字塔）、`query`（fuzzy 查询）、`trace_source`（原文溯源）、`context_bundle`（上下文重构）。

### 10.5 一键脚本 4：复杂文本 + 多轮增量 Smoke

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\run_semantic_grid_complex_incremental_smoke.ps1
```

覆盖：复杂 markdown 基础构建、增量追加（+4 fragments）、fuzzy 查询、重复增量去重（dedupe）、上下文重构（section_priority）。

### 10.6 一键脚本 5：Task Memory 迁移验收 Smoke

```powershell
powershell -ExecutionPolicy Bypass -File D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\run_task_memory_migration_acceptance.ps1
```

覆盖：MCP-native 迁移验收链（freeze → continuation budget → kv snapshot → rocksdb mirror → parity check → structure manifest），预期终态行 `TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS`。

> 也可直接通过 MCP 调用 `lan_agent_task_memory_migration_acceptance` 工具完成同等验收（推荐其他 AI 客户端使用此路径）。

---

## 11. 测试结论

### 11.1 环境

| 项 | 值 |
|---|---|
| 仓库 | `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent` |
| 构建目录 | `AIbuild\Release` |
| 测试目标文件 | `cximage\FastMatch.cpp`（复杂项目） + `test_simple.cpp`（简单文件） |
| compile_commands.json | `D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build\compile_commands.json` |

### 11.2 工具可用性

| 工具 | 状态 | 结论 |
|---|---|---|
| `tools/list` | 可用 | 工具总数已注册（含 Clang/语义网格/CMM/Task Memory 等多组） |
| `lan_agent_run_clang_ast_parser` | 可用 | `status=success`, `compile_db_mode=compile_commands_json` |
| `lan_agent_build_cfg` | 可用 | `status=success`, 50 函数 / 262 块 / 262 边 |
| `lan_agent_query_cfg_artifact` | 可用 | `artifact_json_path_resolved_from=artifact_summary_path` |
| `lan_agent_build_call_graph` | 可用 | 80 节点 / 72 边 / 2427 调用引用 |
| `lan_agent_query_call_graph_artifact` | 可用 | artifact 二次查询成功 |
| `lan_agent_build_dfg` | 可用 | `analysis_level=ast_statement_v1`, 177 readwrite refs |
| `lan_agent_query_dfg_artifact` | 可用 | `artifact_parser_status=success` |
| `lan_agent_build_program_slice` | 可用 | `slice_precision=ast_statement_def_use_cfg_callgraph_v1` |
| `lan_agent_query_program_slice_artifact` | 可用 | artifact + source_lines 二次查询成功 |
| `lan_agent_task_memory_*` (14 工具) | 可用 | freeze/resume/budget/kv/rocksdb/parity/manifest/acceptance/resume_and_execute/selftest 全链通过 |

### 11.3 核心断言结果（Clang 分析工具）

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

### 11.4 语义网格测试结果

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

### 11.5 Task Memory 迁移验收结果

| 断言 | 结果 |
|---|---|
| 12 个 task_memory 工具全部注册 | PASS |
| `freeze` 写入 `latest_resume_context.json` | PASS |
| `freeze` 追加 `step_ledger.jsonl` | PASS |
| `resume_context` 返回 compact_summary + next_call_json | PASS |
| `execute_continuation_budget` (dry_run) 写入预算计划 | PASS |
| `build_kv_snapshot` 生成 `kv_snapshot/index.jsonl` | PASS |
| `kv_lookup` 按 `kind=latest` 命中 | PASS |
| `rocksdb_mirror` 写入 `rocksdb_mirror_manifest.json` | PASS |
| `rocksdb_lookup` 按 selector 命中 | PASS |
| `rocksdb_parity_check` 返回 `parity_status=pass` | PASS |
| `structure_manifest` 写入 `memory_structure.json` | PASS |
| `migration_acceptance` 返回 `migration_acceptance_status=ACCEPTED` | PASS |
| `source_of_truth=file_object_store` 全程保持 | PASS |
| `safe_to_replace_source_of_truth=false` 全程保持 | PASS |
| `parity_required_for_native_reads=true` 全程保持 | PASS |
| 外部 Smoke 脚本终态行 `TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS` | PASS |

### 11.6 已知限制

| 限制 | 详情 | 规避 |
|---|---|---|
| DFG 复杂文件耗时 | FastMatch.cpp DFG build 需 180-300s | 设置 `TimeoutSec=300` |
| 端口占用 | 旧进程残留导致新实例启动失败 | 启动前 `Stop-Process` |
| compile_commands.json 依赖 | 无编译数据库时复杂文件解析失败 | 确保 `project_root` 指向含 `build/compile_commands.json` 的目录 |
| RocksDB 镜像可选 | 默认构建不含 RocksDB 后端 | 编译时加 `-DCODEX_LAN_AGENT_WITH_ROCKSDB=ON` |
| Task Memory 文件增长 | 长任务 `step_ledger.jsonl` 持续增长 | 按 `goal_id` 归档，必要时 `freeze` 后清理旧 trace |

### 11.7 最终状态

**[Verified]** — MCP 工具链可用于复杂项目分析，全部核心断言通过。

**[Verified]** — 语义网格工具链支持复杂文本解构、L1-L5 语义金字塔构建、fuzzy/regex 查询、原文溯源、上下文重构、多轮增量追加与 content_hash 去重，全部断言通过。

**[Verified]** — Task Memory 工具链支持长任务状态冻结、跨模型 resume、bounded continuation budget、文件 KV 快照、RocksDB 镜像、parity check 一致性校验、structure manifest 契约固化、一站式 migration acceptance，全部不变量保持，验收终态 `TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS`。

---

## 12. CMM 工具状态

CMM 工具通过 `codex_lan_agent` 桥接 `codebase-memory-mcp` 服务，已在 MCP 工具列表中注册。使用前需确保：
1. `codebase-memory-mcp` 服务已独立运行。
2. 目标项目已通过 `lan_agent_cmm_index_repository` 完成索引。

CMM 工具状态：`[Implemented]` — Schema 已注册，依赖外部 CMM 服务实际可用性。

---

## 13. Clang 分析工具 vs CMM 工具功能对比

### 13.1 核心差异

| 维度 | Clang 分析工具 (L0-L6) | CMM 工具 (`lan_agent_cmm_*`) |
|---|---|---|
| **数据时效** | 实时解析源文件 | 基于预建索引（需先 `index_repository`） |
| **分析深度** | AST statement-level（语句级） | 图节点/关系级（函数、类、文件级） |
| **适用范围** | 单文件级（`source_file`） | 整个项目级（`project`/`repo`） |
| **底层引擎** | Clang/LLVM AST | codebase-memory-mcp 图数据库 |
| **典型耗时** | 简单文件 1-5s，复杂文件 180-300s | 毫秒级（索引已建） |

### 13.2 功能对照表

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

### 13.3 使用场景对比

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

### 13.4 组合使用建议

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

## 14. 常见问题排查

### 14.1 服务启动失败 / 连接拒绝

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

### 14.2 工具不在 tools/list 中

**原因**：工具 schema 未在 `McpProtocolOperations.h` 的 `BuildMcpToolsListResponse` 中注册。

**解决**：检查 [src/McpProtocolOperations.h](src/McpProtocolOperations.h) 中对应工具的 schema 定义是否存在。

### 14.3 复杂文件解析失败

**原因**：`compile_commands.json` 未找到或路径不正确。

**解决**：
1. 确认 `project_root` 参数指向项目根目录。
2. 确认 `<project_root>/build/compile_commands.json` 存在。
3. 如无，用 CMake 生成：`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。

### 14.4 DFG/Slice 超时

**解决**：增大超时到 300s，或减小 `max_nodes` / `max_edges` / `max_interprocedural_bindings`。

### 14.5 MinGW 编译

项目默认使用 MSVC。如需 MinGW：

```powershell
cmake -B AIbuild -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build AIbuild
```

> 注意：MinGW 模式下 Clang Tooling 的头文件路径需要额外配置，建议优先使用 MSVC。

### 14.6 语义网格增量去重不生效

**原因**：`dedupe_existing` 参数未设置为 `true`，或上一轮的 `artifact_summary_path` 不正确。

**解决**：
1. 确认 `dedupe_existing=true`（默认为 true）。
2. 确认 `artifact_summary_path` 指向上一轮 `incremental_update` 返回的 `artifact_summary_json_path`。
3. 检查 `summary.json` 中的 `artifact_semantic_grid_json_path` 指针是否有效。

### 14.7 语义网格 query 返回空结果

**原因**：keyword 未匹配到任何节点，或 layer 过滤过严。

**解决**：
1. 尝试 `fuzzy_match=true` 启用模糊匹配。
2. 尝试不传 `layer` 参数，搜索所有层。
3. 使用 `regex_match=true` 扩展匹配范围。

### 14.8 Task Memory resume_context 找不到文件

**原因**：`goal_id` 不匹配，或尚未对该 goal 调用过 `task_memory_freeze`。

**解决**：
1. 确认 `goal_id` 与 `freeze` 时使用的一致（仅 `[A-Za-z0-9._-]`，其他字符被替换为 `_`）。
2. 先调用 `lan_agent_task_memory_freeze(goal_id=...)` 创建文件对象层。
3. 检查 `<data_root>/task_memory/{goal_id}/latest_resume_context.json` 是否存在。

### 14.9 RocksDB 镜像不可用

**原因**：未以 `-DCODEX_LAN_AGENT_WITH_ROCKSDB=ON` 编译，或 `rocksdb_mirror` 尚未执行。

**解决**：
1. 重新编译：`cmake -B AIbuild -DCODEX_LAN_AGENT_WITH_ROCKSDB=ON`。
2. 先调用 `build_kv_snapshot` 再调用 `rocksdb_mirror`。
3. 检查 `rocksdb_mirror_manifest.json` 是否生成。
4. 注意：RocksDB 仅为可选读镜像，缺失不影响文件对象层源真和 `kv_lookup` 正常使用。

### 14.10 parity_check 失败

**原因**：文件 KV 快照与 RocksDB 镜像内容不一致，可能是 `rocksdb_mirror` 后又追加了新步骤未重新镜像。

**解决**：
1. 重新执行 `build_kv_snapshot` 刷新文件 KV。
2. 重新执行 `rocksdb_mirror` 同步到 RocksDB。
3. 再次 `rocksdb_parity_check`，应返回 `parity_status=pass`。
4. **切勿**通过修改源真来"迁就"镜像 —— `safe_to_replace_source_of_truth` 必须保持 `false`。


---

## 15. 项目演进分析报告（8月9日 → 8月11日）

> 本节为 2026-08-09 → 2026-08-11 期间 4 个提交的对比分析快照，记录项目从"代码分析 MCP 工具链"演变为"AI Agent 本地操作系统"的架构级变化，供后续维护与架构决策参考。

### 15.1 核心变化总览

两天内 4 个提交，但**架构层面发生了根本性演进**——从"代码分析 MCP 工具链"演变为**完整的 AI Agent 执行平台**。

| 维度 | 8月9日状态 | 8月11日状态 | 变化性质 |
|---|---|---|---|
| MCP 工具表面 | 135 扁平工具列表 | 单网关路由 + 134 个隐藏内部工具 | **架构重构** |
| Task Memory | 12 工具，多步手动编排 | 14 工具，新增一键续接入口 | **能力增强** |
| CLIPS 专家系统 | 隐藏层，源码存在但未暴露 | 正式 MCP 工具，可调用决策 | **从暗到明** |
| 执行能力 | 只读代码分析 | 可执行构建/测试/文件编辑/格式化 | **边界扩展** |
| CI/CD | 无公开流水线 | 两层缓存全自动 Release 流水线 | **工程化落地** |
| 文档 | 工具参数说明 | 新增真实使用对话案例 | **可用性提升** |

### 15.2 架构级变化：MCP 网关路由模式

#### 15.2.1 默认只暴露一个工具

`McpProtocolOperations.h` 中新增了 `UseFullMcpToolSurface()` 控制逻辑：

```cpp
// 默认返回 false，只暴露 lan_agent_mcp_route
// 需要环境变量 CODEX_LAN_AGENT_MCP_TOOL_SURFACE=full/all/legacy/153 才暴露全部工具
```

**默认工具**：`lan_agent_mcp_route`——单聊天入口网关，支持三种模式：

- `mode=overview`：返回指导信息
- `mode=route`：返回 `tool_use_decision` / `current_tool_chain_node` / `required_tool_name` / `required_tool_arguments_json`
- `mode=call`：执行一个内部 MCP 工具，完整内部目录对模型隐藏

#### 15.2.2 设计意图

这是从"模型自由选择 135 个工具"到"网关决策 + 受控执行"的关键转变：

- **降低模型认知负担**：模型只看到一个工具，不需要理解 134 个内部工具的 schema
- **强制决策层**：所有工具调用必须经过路由决策，可插入 CLIPS 规则校验
- **隐藏内部复杂度**：内部工具链对模型不可见，只暴露决策结果
- **完成声明门禁**：`terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `verification_ok` 四个字段控制模型能否宣布任务完成

### 15.3 Task Memory 新增工具

#### 15.3.1 `lan_agent_task_memory_resume_and_execute`（第 13 工具）

**本次更新最有价值的新增**。

```
新对话一键入口：
  读取 latest_resume_context → 执行 bounded continuation budget → 刷新 task memory
  → 返回 terminal verification fields 或 next action
```

关键设计：

- 默认 `dry_run=false` / `execute=true`（与其他工具默认 `dry_run=true` 相反）
- 不需要模型手动编排 freeze → budget → kv → mirror 链路
- 全新模型只需传 `goal_id` 即可续接已归档任务
- 直接替代"重新读取旧对话历史"的传统做法

#### 15.3.2 `lan_agent_task_memory_new_chat_round_selftest`（第 14 工具）

自测工具，验证 MCP-owned continuation semantics：

- 创建 MCP round manifest
- freeze 一个微型归档续接
- 通过 `resume_and_execute` 使用 goal_id-only 入口语义恢复
- 执行 bounded step
- 验证 `terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `verification_ok`
- **不依赖旧模型上下文**，`chat_context_reset_acknowledged` 保持 false 直到客户端确认

#### 15.3.3 Task Memory 演进路径

```
8月9日：freeze → resume → budget → kv_snapshot → rocksdb_mirror → parity → manifest → acceptance
         （8 步手动编排，模型需要理解每一步）

8月11日：resume_and_execute(goal_id) → 一键完成上述链路
         （模型只需知道 goal_id，内部链路由 MCP 服务端自动执行）
```

### 15.4 CLIPS 专家系统正式暴露

上次分析中 CLIPS 是"隐藏的第五层"——源码存在但 README 未提及。现已正式暴露为 MCP 工具：

| 工具 | 功能 |
|---|---|
| `lan_agent_clips_decide` | 基于规则的决策逻辑，输入 MCP request/result facts，返回 allow/block/route + verified/not_verified + 最终答案约束 |
| `lan_agent_clips_chain_template` | 返回标准 CLIPS `mcp_tool_chain` 模板，每个工具共享规则驱动的 pre-call/post-result 链 |
| `lan_agent_rag_clips_meta` | 调用上游 `/rag/clips/meta`，返回 fact_bundle + serialized_assertions |
| `lan_agent_rag_clips_run` | 调用上游 `/rag/clips/run`，返回 request_id/trace_id/query_id + 存储引用 |

**意义**：CLIPS 不再是"预留接口"，而是已经成为**工具调用决策的规则引擎**——在文件操作、长循环、构建、测试、结果验收之前，先经过 CLIPS 规则校验。CLIPS 规则体系的完整说明（目录结构、fact 模板、5 个规则域、49 条 defrule、salience 优先级模型、扩展指南）详见 [第 16 节](#16-clips-规则体系详解)。

### 15.5 执行能力边界扩展

#### 15.5.1 从只读分析到可执行操作

新增完整的执行工具链。

**构建/测试**：

- `lan_agent_configure_project`：CMake 配置
- `lan_agent_build_target`：构建目标（需 preflight_ref）
- `lan_agent_run_ctest_target`：运行 CTest
- `lan_agent_preflight_build_target` / `preflight_run_ctest_target`：预检契约
- `lan_agent_discover_ctest_tests`：CTest 发现
- `lan_agent_prepare_build_dir` / `check_build_dir`

**文件编辑（安全受控）**：

- `lan_agent_write_text_file`：创建/覆盖/追加文本文件
- `lan_agent_preview_patch`：高风险单文件替换预览（不写盘）
- `lan_agent_apply_single_file_patch` / `apply_diff_patch`：应用补丁
- `lan_agent_verify_single_file_patch`：验证补丁结果（hash + 包含/排除文本检查）
- `lan_agent_revert_single_file_patch`：回滚补丁
- `lan_agent_format_code_file`：clang-format 格式化（支持 dry_run）
- `lan_agent_ensure_directory`：目录创建

**编辑安全约束**：

- 明确禁止用 patch 工具做注释清理/文本清理
- 必须用 `scan_text_ranges(max_ranges_per_call=1)` → `prepare_edit_windows(max_windows_per_call=1)` → 一次原子编辑
- 完整审计链：preview → apply → verify → revert，每个 patch_id 可追溯

#### 15.5.2 本地模型/RAG 集成

- `lan_agent_run_local_chat` / `enqueue_local_chat`：项目范围代码分析
- `lan_agent_run_rag_flow` / `enqueue_rag_flow`：RAG 生成请求
- `lan_agent_ventriloquist_reply`：受控本地 AI 代理回复，归一化为 direct_answer/evidence/next_action/confidence
- `lan_agent_remote_session_new_turn` / `append_turn`：llama.cpp 远程会话管理
- `llama.observer_smoke`：观察 CODEX → local MCP → local llama.cpp 链路

### 15.6 CI/CD 工程化落地

#### 15.6.1 两层缓存架构

```
Layer 1 · LLVM/Clang 18.1.8 toolchain（缓存持久化）
  ├─ 从官方源码 llvmorg-18.1.8 构建
  ├─ 静态库，X86 target only，关闭 tests/examples/benchmarks/docs/tools
  ├─ 缓存 key: llvm-18.1.8-static-flat-msvc-ninja-v3
  ├─ 仅当 LLVM 版本/源码/编译标志变化时重建
  └─ save-always: 即使后续步骤失败也保存缓存

Layer 2 · codex_lan_agent 业务构建（增量编译）
  ├─ 恢复缓存的 flat LLVM_ROOT {include, lib}
  ├─ 业务源码变化只编译 codex_lan_agent，不触发 LLVM 重编译
  ├─ 默认启用 CODEX_LAN_AGENT_ENABLE_CLANG_AST=ON
  └─ 默认启用 CODEX_LAN_AGENT_WITH_ROCKSDB=ON
```

#### 15.6.2 新增 RocksDB 11.0.4 静态构建

- 缓存 key：`rocksdb-11.0.4-static-msvc-md-v1`
- 从官方 release tarball 构建，关闭所有可选压缩依赖（snappy/lz4/zlib/zstd/bzip2/tbb）
- `/MD` CRT 匹配，静态库
- Release 构建默认启用 RocksDB，`task_memory_rocksdb_mirror/lookup/parity_check` 工具可用

#### 15.6.3 纯网络构建策略

- **不 vendor 任何第三方库**：LLVM 和 RocksDB 都从网络下载构建
- Release ZIP 只包含 `codex_lan_agent.exe` + config + README，无 `.lib`/`.dll`
- `.gitignore` 白名单：只允许 `CMakeLists.txt` / `src/**` / `.github/workflows/**`
- 禁止 `*.exe *.dll *.lib *.obj *.o *.a` / 图片 / 归档 / `third_party/` / `vendor/`

### 15.7 语义动作调度层

新增完整的语义动作抽象层，把自然语言意图映射到工具调用：

| 工具 | 功能 |
|---|---|
| `semantic_action_map` | 标准语义动作快捷方式表 |
| `semantic_action_resolve` | 自然语言 → 语义动作（不执行） |
| `semantic_action_validate` | 验证参数和副作用风险（不执行） |
| `semantic_action_prepare` | resolve + validate 一次性预检 |
| `semantic_action_tool_call` | 生成非执行的 MCP tools/call JSON 模板 |
| `lan_agent_execute_semantic_action` | 解析并立即执行真实工具，返回 task_id/result_ref/evidence_ref |

配合 `intent_dispatch_prepare`——消费结构化模型意图输出，自动准备下一个 MCP 工具调用，支持 legacy fallback。

### 15.8 与 research-mcp 体系的契合度更新

上次分析指出两个项目"高度契合"，现在契合度进一步提升：

| 设计原则 | research-mcp | codex-lan-agent（更新后） |
|---|---|---|
| 协议层 | MCP over HTTP/stdio | MCP over Streamable HTTP + 网关路由 |
| 分层架构 | L1/L2/L3 三层观测 | 网关路由 + 134 个内部工具 + CLIPS 决策层 |
| 缓存/持久化 | SQLite 统一缓存层 | 文件对象层 + RocksDB 镜像 + parity check |
| 源真分离 | 多源融合 + 降级链 | 文件源真 + RocksDB 镜像 + parity check |
| 实体/关系 | Entity Mapper + 关系图谱 | Semantic Grid + dialog_slice + task_memory |
| CI/CD | GitHub Actions 云端编译 | 两层缓存全自动 Release 流水线 |
| 决策层 | （尚未实现） | CLIPS 专家系统正式暴露 |
| 执行能力 | 只读信息获取 | 只读分析 + 可执行构建/测试/编辑 |

**潜在整合方向**：

1. `resume_and_execute` 模式可以直接复用到 research-mcp 的长任务续接
2. CLIPS 决策层可以统一两个项目的工具调用决策
3. 网关路由模式（单入口 + 隐藏内部工具）可以作为 research-mcp 的演进方向
4. 两层缓存 CI 架构可以直接复用到 research-mcp 的 Release 流水线

### 15.9 技术债务

#### 15.9.1 已有问题持续存在

- 硬编码路径：`TaskMemoryOperations.h` 中仍有 `D:/Codex-WorkDir/Sean_WorkDir/llama.cpp-b8851/...`
- Windows 优先：WinHTTP + MSVC + WebView2
- DFG 性能瓶颈：复杂文件 180-300s

#### 15.9.2 新增隐忧

1. **工具数量爆炸**：完整模式下 135 个工具，维护成本急剧上升
2. **网关路由黑盒**：默认模式下模型只看到一个工具，调试难度增加
3. **CLIPS 规则已文档化**：`clips_rules/` 目录的 8 个 `.clp` 文件、5 个规则域、49 条 defrule 已在第 16 节详述（原"未开源"问题已解决）
4. **执行安全边界**：新增文件编辑/构建/测试能力，但安全约束分散在各工具描述中，缺乏统一的权限模型
5. **README 同步机制**：已通过 4.0 节"完整工具清单概览（135 个）"校准工具表与 `McpProtocolOperations.h` 实际注册一致；新增/删除工具时必须同步更新 4.0 节合计（原"滞后风险"已缓解）

### 15.10 结论

#### 15.10.1 演进判断

`codex-lan-agent` 正在从**"代码理解基础设施"**快速演变为**"AI Agent 本地操作系统"**。

核心标志：

- **工具表面收敛**：135 扁平 → 单网关路由，模型认知负担降低
- **执行闭环形成**：分析 → 决策（CLIPS）→ 执行（构建/测试/编辑）→ 验证 → 记忆（Task Memory）
- **长任务自动化**：`resume_and_execute` 一键续接，模型无需理解内部链路
- **工程化成熟**：两层缓存 CI，纯网络构建，Release 可直接下载使用

#### 15.10.2 最有价值的三个变化

1. **`resume_and_execute`**：把 Task Memory 从"多步手动编排"简化为"一键自动续接"，这是长任务记忆从概念验证到实用化的关键一步
2. **网关路由模式**：单入口 + 隐藏内部工具 + CLIPS 决策校验，这是 AI Agent 工具调用从"模型自由选择"到"受控决策执行"的架构范式转变
3. **两层缓存 CI**：LLVM 工具链与业务构建解耦，业务源码变化不触发 LLVM 重编译，Release 构建时间从 ~2 小时降到 ~分钟级


---

## 16. CLIPS 规则体系详解

CLIPS 专家系统是 codex-lan-agent 的**工具调用决策层**——所有 MCP 工具调用（在完整工具表面模式下）都会经过 CLIPS 规则引擎做 pre-call allow/block/route 决策和 post-result 验证，确保模型不会绕过安全约束声明任务完成。规则源码全部位于 [src/clips_rules/](src/clips_rules/)，共 8 个 `.clp` 文件、5 个规则域、49 条 `defrule`、8 个 `deftemplate`。

### 16.1 目录结构

```
src/clips_rules/
├── templates/
│   ├── mcp_fact_templates.clp      ← 核心 fact 模板（5 个 deftemplate）
│   └── cmm_fact_templates.clp      ← CMM 扩展 fact 模板（3 个 deftemplate）
├── rules/
│   ├── mcp_tool_guard.clp          ← MCP pre-call allow/block/route（13 条 defrule）
│   ├── mcp_result_guard.clp        ← MCP post-result 验证（16 条 defrule）
│   ├── cmm_init_guard.clp          ← CMM 初始化与搜索工作流守卫（15 条 defrule）
│   ├── cxparser_preflight_guard.clp ← cxparser 驱动的 build/test 预检（2 条 defrule）
│   └── slice_ingest_guard.clp      ← slice 入库质量/去重（3 条 defrule）
├─ graphs/
    cmm_init_flow.clp            CMM 7  defrule
    mcp_guard_flow.clp           slice-node  guard 
 profiles/
     default_guard_profile.clp    guard profile 
```

### 16.2 Fact deftemplate

CLIPS pattern matching fact  [templates/mcp_fact_templates.clp](src/clips_rules/templates/mcp_fact_templates.clp)

| deftemplate |  |  slot |
|---|---|---|
| `mcp_tool_request` |  factpre-call  | `tool_name` / `primary_intent` / `file_path` / `probe_ready` / `explicit_user_intent` / `single_step_required` / `max_items_per_call` / `requires_revert_plan` |
| `mcp_tool_result` |  factpost-result  | `tool_name` / `terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `has_more` / `continue_required` / `analysis_allowed` / `batch_completion` / `result_hash` / `schema_version` / `ai_conclusion_valid` / `result_ref` / `evidence_ref` |
| `mcp_tool_chain` |  fact | `chain_phase`pre_call / post_result/ `request_type`file_mutation / analysis_review / generic_mcp_tool/ `risk` / `safety_class` / `execution_class` |
| `slice_ingest_fact` |  fact | `dedup_status` / `canonical_slice_id` / `dup_of` |
| `cxparser_fact` | cxparser  fact | `parse_status` / `symbol_status` / `target_status` / `preflight_status` |
| `clips_decision` | ** fact** assert  | `domain` / `target` / `decision`allow / block / route/ `verification`verified / not_verified/ `reason_code` / `next_action` / `route_target` / `matched_rule` |

CMM  fact[templates/cmm_fact_templates.clp](src/clips_rules/templates/cmm_fact_templates.clp)`cmm_project_state` / `cmm_search_request` / `cmm_workflow_stage`

### 16.3  1`mcp_tool_guard`pre-call allow/block/route

[rules/mcp_tool_guard.clp](src/clips_rules/rules/mcp_tool_guard.clp)13  defrule**** `mcp_tool_request` fact salience  `clips_decision`

####  block 

|  | salience |  | reason_code |
|---|---|---|---|
| `block-single-file-patch-apply-without-explicit-intent` | 89 | apply/revert patch  `explicit_user_intent=false` | `missing_patch_intent` |
| `block-stepwise-file-tool-multi-item-request` | 88 | `single_step_required=true`  `max_items_per_call1` | `multi_item_file_step_not_allowed` |
| `block-broad-file-mutation-for-stepwise-editing-intent` | 88 | write/patch  + `primary_intent=comment_cleanup/text_cleaning/localized_edit/...` | `bulk_file_mutation_not_allowed_for_stepwise_edit` |
| `block-multi-file-patch-in-phase1` | 87 | patch  `file_count1` | `multi_file_patch_not_allowed_phase1` |
| `block-single-file-patch-without-revert-plan` | 86 | apply/revert patch `revert_plan_ready=false` | `missing_patch_revert_plan` |
| `block-high-risk-write-without-path` | 85 | `file_mutation`  + `file_path=""` | `missing_file_path` |

####  route 

|  | salience |  | route_target |
|---|---|---|---|
| `route-code-format-cleanup-to-clang-format` | 85 |  + `primary_intent=code_format` | `lan_agent_format_code_file` |
| `route-file-text-operations-to-probe-first` | 84 |  + `probe_required=true` + `probe_ready=false` | `lan_agent_probe_text_file` |
| `route-read-text-file-to-window-delete-for-comment-cleanup` | 84 | `read_text_file` + `primary_intent=comment_cleanup/remove_comments//...` | `lan_agent_delete_text_range_window_atomic` |
| `route-comment-cleanup-scaffold-to-window-delete` | 83 | `scan_text_ranges`/`prepare_edit_windows` +  | `lan_agent_delete_text_range_window_atomic` |
| `route-read-text-file-to-range-scan-for-editing-intent` | 82 | `read_text_file` + `primary_intent=text_cleaning/localized_edit/source_edit_planning` | `lan_agent_scan_text_ranges` |

#### 

|  | salience |  |
|---|---|---|
| `allow-single-file-patch-preview` | 80 | preview patch allow |
| `default-mcp-tool-allow` | -100 |  allow + verified |

### 16.4  2`mcp_result_guard`post-result 

[rules/mcp_result_guard.clp](src/clips_rules/rules/mcp_result_guard.clp)16  defrule `mcp_tool_result` fact

#### 

|  | salience |  |  |
|---|---|---|---|
| `text-range-delete-result-still-pending-by-has-more` | 49 | delete  `has_more=true` | route`verification=not_verified` |
| `text-range-delete-result-still-pending-by-continuation` | 48 | delete  `continue_required=true` | route |
| `directory-batch-read-still-pending` | 48 | `analysis_allowed=false` + `batch_completion=incomplete` | route |
| `non-terminal-result-forbids-final-answer` | 47 | `terminal_state=false` + `completion_claim_allowed=false` | route final answer |
| `final-answer-disallowed-by-result` | 46 | `final_answer_allowed=false` | route final answer |

> **** `terminal_state=true` + `completion_claim_allowed=true` + `final_answer_allowed=true` + `verification_ok=true` ****CLIPS  post-result 

#### 

|  | salience |  | reason_code |
|---|---|---|---|
| `invalid-direct-answer-json-fragment` | 50 | local_chat/ventriloquist `direct_answer="{"` | `bad_direct_answer_fragment` |
| `invalid-direct-answer-empty` | 45 | `direct_answer=""` | `empty_direct_answer` |
| `invalid-direct-answer-label-token` | 44 | `direct_answer="direct_answer"` | `bad_direct_answer_label_token` |
| `analysis-only-chat-claimed-execution-without-evidence` | 41 | `ai_conclusion_valid=false` + `result_ref/evidence_ref/task_id`  | `analysis_only_execution_claim_without_evidence` |
| `execution-task-result-missing-traceable-ref` | 39 | execute  + `task_id/result_ref/evidence_ref/log_path`  | `execution_result_missing_traceable_ref` |
| `audited-write-result-missing-proof` | 38 | file_mutation +  | `write_result_missing_audit_ref` |
| `invalid-result-missing-hash` | 35 | `result_hash=""` | `result_hash_missing` |
| `invalid-result-missing-schema` | 34 | `schema_version=""` | `schema_version_missing` |
| `incomplete-read-result-requires-continuation` | 33 | read/list/run_cxparser `task_completion=incomplete` | `read_chain_incomplete` |
| `invalid-ai-conclusion-flag` | 40 | `ai_conclusion_valid=false` | `ai_conclusion_invalid` |
| `default-mcp-result-verified` | -100 |  allow + verified |

### 16.5  3`cmm_init_guard`CMM 

[rules/cmm_init_guard.clp](src/clips_rules/rules/cmm_init_guard.clp)15  defrule CMM ""

#### 

- **`block-cmm-search-before-ensure-indexed`**salience 95 search/query/trace/get_architectureroute  `lan_agent_cmm_index_status`
- **`route-cmm-ensure-indexed-as-first-step`**salience 93`probe_ready=false`  `index_status`
- **`block-cmm-search-without-project-parameter`**salience 94 `normalized_project`  `search_code`
- **`allow-cmm-search-on-verified-project`**salience 85
- **`handle-cmm-project-not-found-error`**salience 75 route  `list_projects` + `index_repository`
- **`block-cmm-delete-project-without-intent`**salience 88 `primary_intent=reindex_preparation`

#### CMM [graphs/cmm_init_flow.clp](src/clips_rules/graphs/cmm_init_flow.clp)

```
init  validate  (indexed?)  ready  search  analyze
                 (not indexed)                (error)
              index  validate              error  init (retry)
```

7  defrule  `cmm_state_machine`  `required_tool` / `guard_condition` / `action`

### 16.6  4`cxparser_preflight_guard`build/test 

[rules/cxparser_preflight_guard.clp](src/clips_rules/rules/cxparser_preflight_guard.clp)2  defrule

- **`block-build-without-preflight`**salience 70`build_target`/`run_ctest_target`  `preflight_status=missing/false/blocked`  block `preflight_build_target`/`preflight_run_ctest_target`  `preflight_ref`
- **`default-preflight-allow`**salience -100

### 16.7  5`slice_ingest_guard`slice 

[rules/slice_ingest_guard.clp](src/clips_rules/rules/slice_ingest_guard.clp)3  defrule

- **`duplicate-slice-route-canonical`**salience 60`dedup_status=duplicate` +  `canonical_slice_id`  route  canonical slice 
- **`duplicate-slice-block-status`**salience 55`dedup_status=duplicate`  canonical  block
- **`default-slice-ingest-allow`**salience -100

### 16.8 CLIPS 

CLIPS  4  MCP  4.x 

| MCP  |  |  |
|---|---|---|
| `lan_agent_clips_decide` | / |  5  `domain`  |
| `lan_agent_clips_chain_template` |  | `mcp_tool_chain` fact  |
| `lan_agent_rag_clips_meta` |  RAG  |  `/rag/clips/meta` |
| `lan_agent_rag_clips_run` |  RAG  |  `/rag/clips/run` |

### 16.9 

CLIPS  salience salience 

```
salience 95  cmm_init_guard block
salience 89-85  mcp_tool_guard block/route 
salience 84-80  mcp_tool_guard route 
salience 70  cxparser_preflight_guard block
salience 60-55  slice_ingest_guard 
salience 50-33  mcp_result_guard 
salience -100   default-allow 
```

> ****block  salience > route  > default-allow fact  salience `default-*-allow`salience -100fail-open

### 16.10 

 CLIPS 

1.  `.clp`  `defrule` `<action>-<target>-<condition>`
2.  salienceblock  85-95route  80-84 33-50default -100
3.  `assert`  `clips_decision` fact `domain` / `target` / `decision` / `verification` / `reason_code` / `next_action` / `matched_rule` 
4.  fact slot [templates/mcp_fact_templates.clp](src/clips_rules/templates/mcp_fact_templates.clp)  `deftemplate`  slot `"false"`  `""`
5. C++  `McpToolDispatch.h`  `ClipsDecisionOperations.h`  fact  slot
6. 

> ****CLIPS  MCP  C++ `BuildClipsDecisionResult`  `src/clips_rules/` `.clp` 
---

## 17. Fact-Factory LLM  CLIPS 

### 17.1 

Fact-Factory  LLM  Myrmidon/CLIPS **** slot  CLIPS fact 

```
LLM  JSON  slot 
        
0 C++ NLP 
        
1CppJieba 
        
2marisa-trie
        
3WordNet +  cilin_ext.txt
        
 tag  + is_dirty  + 
        
 CLIPS 
```

### 17.2 

|  |  |
|---|---|
| **** | `is_dirty=true` fact  CLIPS |
| **** |  tag  dirty CLIPS  |
| ** slot** |  SQLite + UUID slot  |
| **/Python/Java** |  +  |

### 17.3 

#### 0ByteSanitizer

- ****[src/fact_factory/ByteSanitizer.h](src/fact_factory/ByteSanitizer.h)
- ** C++  NLP **
- **C-CLIPS  segfault **



|  |  |
|---|---|
|  | slot  32 `is_dirty=true` |
|  |  `\0` |
|  |  ASCII 0-31  |
| CLIPS  |  `" ( ) ;`  |
| UTF-8  |  UTF-8  `is_dirty=true` overlong encoding  |

 API

```cpp
struct ByteSanitizerConfig {
    uint32_t max_field_bytes = 32;
    bool escape_clips_syntax = true;
    bool strip_control_chars = true;
    bool validate_utf8 = true;
};

struct ByteSanitizerResult {
    std::string sanitized;
    bool is_dirty = false;
    std::string reason;
};

ByteSanitizerResult SanitizeSlotValue(std::string_view raw, const ByteSanitizerConfig & config);
```

#### 1CppJieba

- ****[src/fact_factory/JiebaTokenizer.h](src/fact_factory/JiebaTokenizer.h)
- ****[cppjieba](cppjieba/)clone  `https://github.com/Sean-Cai-X/cppjieba`
- **** HMM 
- ****`#undef CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE` ClipsDecisionOperations.h  `extern "C"`  CppJieba  `using std::xxx` 

>  `FactFactoryIntegration.cpp`  `CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE`  `extern "C"`  include CppJieba

#### 2marisa-trie

- ****[src/fact_factory/BusinessTrieMatcher.h](src/fact_factory/BusinessTrieMatcher.h)
- ****marisa-trie `grimoire.cc` Rime 
- ****1

#### 3SemanticNormalizer

- ****[src/fact_factory/SemanticNormalizer.h](src/fact_factory/SemanticNormalizer.h)
- ****[src/fact_factory/resources/cilin_ext.txt](src/fact_factory/resources/cilin_ext.txt)892,620 
  -  HanLP v1.8.6 `data/dictionary/synonym/CoreSynonym.txt`
  - `=` `Aa01A01=    `
  - ** HanLP Java/Python **
- ****WordNet C++ 
- ****[src/fact_factory/resources/business_supplement.txt](src/fact_factory/resources/business_supplement.txt)
- ****[src/fact_factory/resources/business_dict.utf8](src/fact_factory/resources/business_dict.utf8)



1. 
2. ** 40  tag** CLIPS
3.  `is_dirty=true` CLIPS 
4.  `business_supplement.txt` 

### 17.4  Tag 40 

- ****[src/fact_factory/BusinessTagRegistry.h](src/fact_factory/BusinessTagRegistry.h)

|  |  |  tag |
|---|---|---|
| Intent | 10 | `comment_cleanup`, `code_format`, `source_edit`, `code_search`, `refactor_file` |
| RequestType | 8 | `analysis_review`, `read_observe`, `file_mutation`, `execution_task`, `clips_control` |
| SafetyRisk/ | 4 | `write_audited`, `read_only`, `low`, `high` |
| Decision | 3 | `allow`, `block`, `route` |
| Verification | 3 | `verified`, `not_verified`, `invalid` |
| ExecutionClass | 3 | `read`, `write`, `execute` |
| ActionVerb | 9 | `probe`, `scan`, `delete`, `insert`, `replace`, `build`, `test` |

 tag `ResolveBusinessTag()` 

### 17.5 

 `inline`  [src/ClipsDecisionOperations.h](src/ClipsDecisionOperations.h) 

|  |  |  |
|---|---|---|
| `ApplyFactFactoryNormalizePrimaryIntent(raw_intent)` | primary_intent  | ByteSanitizer  BusinessTagRegistry    |
| `ApplyFactFactoryByteSanitizeSlot(raw_value, is_token_slot)` | slot  | ByteSanitizertoken slot 64  /  slot 256  |

### 17.6 Pending Continuation 

****`SemanticIntentLexiconEntry`  `std::initializer_list<const char*>`  dangling  CLIPS pending continuation `pending_continuation_active=true` + `pending_required_arguments_json`  segfault 

****
1.  `std::initializer_list<const char*>`  `std::vector<const char*>`[src/SemanticIntentLexicon.h](src/SemanticIntentLexicon.h)
2.  `EvaluateClipsDecision`  trace 
3.  `route_arguments_json`  raw JSON `route_arguments_json_available=false`, `transport=none`

### 17.7 

```
src/fact_factory/
 ByteSanitizer.h            0
 JiebaTokenizer.h           1CppJieba 
 BusinessTrieMatcher.h      2marisa-trie 
 SemanticNormalizer.h       3
 FactFactory.h              
 BusinessTagRegistry.h      40  tag + 
 resources/
     business_dict.utf8      40 CppJieba + marisa-trie 
     business_supplement.txt 
     cilin_ext.txt           892KB~7
```

### 17.8 

|  |  |  |
|---|---|---|
| `CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE` | `#undef` |  4  CppJieba + marisa-trie |
|  |  |  ByteSanitizer + BusinessTagRegistryheader-only |

> **** `extern "C"` 

---

## 18. optfile 

### 18.1 

`optfile`  Qt  MCP 

- ****[optfile/main.cpp](optfile/main.cpp)
- ****`optfile.exe`
- ****Qt5Core`QSaveFile``QCryptographicHash``QJsonDocument`

### 18.2 

```powershell
optfile.exe --td <target_dir> --tf <test_file> [options]
```

|  |  |
|---|---|
| `--td, --target-dir <path>` |  |
| `--tf, --test-file <name>` |  |
| `--locate-text <text>` | / |
| `--find-line <n>` |  |
| `--insert-after-anchor <text>` |  |
| `--replace-start-line <n>` |  |
| `--replace-end-line <n>` |  |
| `--replacement-text <text>` | / |
| `--delete-line <n>` |  |
| `--delete-content <text>` |  |
| `--expected-anchor-hash <hash>` |  |
| `--expected-line-hash <hash>` |  |
| `--expected-range-hash <hash>` |  |
| `--show-preview` |  locate  |
| `--fuzzy-threshold <0-100>` |  60 |
| `--occurrence <n>` |  N  1 |

### 18.3 

|  |  |  |
|---|---|---|
|  | `locate_text_lines_mcp` | / +  +  |
|  | `find_line_metadata_mcp` |  |
|  | `insert_after_anchor_atomic_mcp` |  |
|  | `replace_line_range_atomic_mcp` |  |
|  | `delete_line_atomic_mcp` |  |
|  | `delete_content_atomic_mcp` |  |

> **** `QSaveFile`  JSON 

### 18.4 

 `std::cout`  JSON  stdout

```json
{"status":"success","operation":"locate","file_path":"...","matched_lines":[...],"file_hash":"..."}
```

 exit code 2

```json
{"status":"error","error":"anchor not found"}
```

### 18.5 

optfile  CMake  qmake 

```powershell
cd D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\optfile
qmake optfile.pro
mingw32-make
#  optfile.exe  Qt5Core.dll  PATH 
```

---

## 19. Fact-Factory 

### 19.1 

|  |  |
|---|---|
|  | `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent` |
|  | `build\Release` |
|  | `codex_lan_agent.exe`38,003,200  |
|  | `test_config.ini` |
|  | 18080 |
| Machine Code | `8EE5-2336-71AE-74DD` |
|  | 2026-08-13 |

### 19.2 

|  |  |  |
|---|---|---|
| TEST 1: Health Check | **PASS** | `ok=true, status=ok, listen_port=18080, outcome=PASS` |
| TEST 2: MCP Overview | **PASS** | `ok=true, tool_count=1, semantic_action_count=78` |
| TEST 3: CLIPS decide - observe | **PASS** | `ok=true, exit_code=0, status=success, decision=allow, terminal_state=true, outcome=PASS` |
| TEST 4: CLIPS decide - pending continuation | **PASS** | `ok=true, exit_code=0, status=success, decision=allow, failure_mode=none, outcome=PASS` |
| TEST 5: Concurrent Stress (8 mixed) | **PASS** | `PASS=8, FAIL=0`5observe + 3pending |

### 19.3 route  raw JSON

|  |  |  |
|---|---|---|
| `route_arguments_json` | `""`len=0 | **PASS** - raw JSON  |
| `route_arguments_json_available` | `false` | **PASS** |
| `route_arguments_json_transport` | `none` | **PASS** |
| `route_arguments_json_ref` | `""` | **PASS** |

### 19.4 Pending Continuation 

 payload 

```json
{
  "primary_intent": "run_build",
  "tool_name": "cmake_build",
  "pending_continuation_active": "true",
  "pending_required_tool": "cmake_build",
  "pending_required_arguments_json": "{\"file_path\":\"D:/test.txt\",\"anchor\":\"line1\",\"new_lines\":\"a\\nb\\nc\"}",
  "pending_trace_id": "trace-pend-test-...",
  "pending_hash": "",
  "pending_trace_match": "true",
  "continuation_takeover_allowed": "true"
}
```

**** `decision=allow, failure_mode=none, outcome=PASS`

>  segfault `SemanticIntentLexiconEntry`  `std::initializer_list`  bug

### 19.5 

 8 5  observe + 3  pending continuation PASS

```
Stress result: PASS=8 FAIL=0 (total=8)
```

### 19.6 Fact-Factory 

|  |  |  |
|---|---|---|
| 0 ByteSanitizer | **** | 5  |
| BusinessTagRegistry | **** | 40  tag +  |
| 1 CppJieba | **** |  |
| 2 marisa-trie | **** |  |
| 3 SemanticNormalizer | **** | cilin_ext.txt 892KB |
| cilin_ext.txt | **** | 892,620 HanLP v1.8.6 CoreSynonym.txt |
| Pending Continuation  | **** | `std::initializer_list`  `std::vector` |
| route  | **** | `route_arguments_json`  |

### 19.7 

1. **optfile stdout **optfile.exe  .NET `Process.StandardOutput`  stdout Qt QCoreApplication 
2. ****CppJieba + marisa-trie ByteSanitizer + BusinessTagRegistry
3. **cilin_ext.txt ** 7 <1s

---

## 20. MCP 61  /  < 5 

> `2026-08-13_05-14-13_conv_94642bb5__d_codex_workdir_sea.jsonl`
> gemma-4-E4B-it-UD-Q5_K-XL
> `D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cxparser`

### 20.1 

 cxparser  **61  C/C++ **

|  |  | primary_intent | flow_id |
|---|---|---|---|
| 1 |  | `comment_cleanup` | `directory_comment_cleanup_bounded_window_v1` |
| 2 |  | `code_format` | `directory_comment_cleanup_bounded_window_v1` |

### 20.2 

 `lan_agent_mcp_route` CLIPS  `lan_agent_list_directory` 

```

  -> lan_agent_mcp_route (route )
    -> CLIPS route_target=lan_agent_list_directory, chain_state=needs_tool_call
      -> lan_agent_mcp_route (call , target_tool_name=lan_agent_list_directory)
        ->  file_count=63, code_file_count=61, file_paths_json=[...61...]
          -> terminal_state=true, completion_claim_allowed=true
```

** JSONL timings **

|  | Turn 1  | Turn 2  |  | LLM  |
|---|---|---|---|---|
| 1 | **144 ms** | **140 ms** | **284 ms** | 28,768 ms |
| 2 | **22 ms** | **36 ms** | **58 ms** | 27,981 ms |
| **** | 166 ms | 176 ms | **342 ms** | 56,749 ms |

> **4  MCP 61  342  5 **  LLM gemma-4-E4B-it-UD-Q5_K-XLMCP 

### 20.3 CLIPS 

 128+ 

```
route_target=lan_agent_list_directory
chain_state=needs_tool_call -> tool_result_returned
completion_guard=NON_TERMINAL_RESULT: do not claim completion; execute the required next MCP tool call
terminal_state=true (2)
verification_ok=true
audit_field_count=128 (route) / 149 (list)
```

### 20.4 

 PowerShell 

|  |  |
|---|---|
|  | 61 |
|  `//`  `/* */`  | **0** |
| `#include` / `namespace` / `class` / `#define`  | **** |
|  `"<="`, `">="` | **** |
|  ->  ->  ->  | **PASS** |

### 20.5 

|  | ++ | MCP  |
|---|---|---|
|  I/O | 61  |  |
|  |  | CLIPS 2 turn  |
|  | ~ | **< 350 ms** |
|  |  | 128+ / |

---

## 21. 

> codex-lan-agent ** MCPC/C++  Agent **** MCP **
>
> "61  342ms"
> **LLM MCP Runtime **

### 21.1 

|  |  |  |
|---|---|---|
| ** MCP ** |  /  LLM | stdio clangd-mcp mcp |
| ** MCP** | Clang AST /  | **codex-lan-agent** |
| ** MCP Runtime** |  Agent |  |



> codex-lan-agent = ** MCP  & **

### 21.2  MCP Runtime 

```
 ============================================================================
 |                       MCP Runtime                             |
 ============================================================================
 |                                                                           |
 |  []  stdio  |  streamable-http  |  (future) WebSocket           |
 |       |              |                        |                           |
 |       +--------------+------------------------+                           |
 |                      v                                                    |
 |  []  lan_agent_mcp_route  ( Schema, )            |
 |                      |                                                    |
 |       +--------------+--------------+--------------+--------------+       |
 |       |              |              |              |              |       |
 |       v              v              v              v              v       |
 |  [1]         [2]         [3]         [4]         [N]   |
 |  codex-code      document       hardware       ops-deploy      ...       |
 |  Clang AST       Markdown       /JTAG    CI/CD                     |
 |  CFG/        PDF                                   |
 |  compile_cmd     RAG                                 |
 |       |              |              |              |              |       |
 |       +--------------+--------------+--------------+--------------+       |
 |                      |                                                    |
 |  ===================|==== |=================== |
 |                      v                                                    |
 |  +------------------------------------------------------------------+     |
 |  |  CLIPS ,                                 |     |
 |  |  -  ( /  / )             |     |
 |  |  -  ( /  diff )          |     |
 |  |  -  ( "")                     |     |
 |  |  :  LLM ,                     |     |
 |  +------------------------------------------------------------------+     |
 |  |   (Task Memory + step_ledger)                       |     |
 |  |  -  ( C++/Clang)                            |     |
 |  |  -  vs                                    |     |

## 25. 2026-08-14 optfile  agent_server_stdout 

### 25.1 

 `optfile.exe` `--replacement-text`  C/C++ 



|  |  |
|---|---|
| `optfile/main.cpp` |  replacement text  base64  |
| `optfile.exe` |  `C:\Qt\Qt5.14.2\Tools\mingw730_64`  |
| `src/comm.h` |  `agent_server_stdout.log`  |
| `src/RemoteInteractionOperations.h` | `/tools`  `/mcp`  `[tool_event]` |
| `src/main.cpp` | `serve`  |

`optfile` 

|  |  |
|---|---|
| `--replacement-text <text>` |  `\n`  |
| `--decode-escapes` |  `\n` / `\r` / `\t`  |
| `--replacement-text-base64 <base64>` | C++ JSON  shell  |

 `optfile`  `optfile/main.cpp` `optfile.exe`  `optfile.exe` `--replacement-text-base64`

### 25.2  optfile.exe



```powershell
$env:PATH='C:\Qt\Qt5.14.2\Tools\mingw730_64\bin;C:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin;' + $env:PATH
cd D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\optfile
& 'C:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe' optfile.pro -o Makefile
& 'C:\Qt\Qt5.14.2\Tools\mingw730_64\bin\mingw32-make.exe' release
Copy-Item .\Release\optfile.exe D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\optfile.exe -Force
```



|  |  |
|---|---|
| `optfile/Release/optfile.exe` |  |
| `optfile.exe` |  |

### 25.3 agent_server_stdout.log 

 `serve` 

```text
--agent-server-stdout-log
--agent-server-stdout-log-path <path>
```



```powershell
$STAGE_EXE='D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\build_release_x64\stage_x64\bin\codex_lan_agent.exe'
$CFG='D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\codex_lan_agent.cfg'
& $STAGE_EXE --config $CFG serve --machine-code 8EE5-2336-71AE-74DD --agent-server-stdout-log
```

 `codex_lan_agent.cfg`  `log_root` 

```text
D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\agent_server_stdout.log
```



```powershell
& $STAGE_EXE --config $CFG serve --machine-code 8EE5-2336-71AE-74DD --agent-server-stdout-log-path D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\agent_server_stdout.log
```



|  |  |  |
|---|---|---|
|  | `[] server_event=...` |  |
|  | `[tool_event] {...}` |  `/tools`  `/mcp`  |

`[tool_event]` 

|  |  |
|---|---|
| `timestamp` |  |
| `event_type` |  `tool_call` |
| `tool_name` |  `tool_name` / `visible_tool_name` |
| `command_name` |  |
| `entry_name` | HTTP  `POST /tools` |
| `method` / `path` | HTTP  |
| `status` / `http_status` |  HTTP  |
| `duration_ms` |  |
| `trace_id` / `goal_id` |  ID |
| `mcp_route_mode` | `lan_agent_mcp_route`  route  `overview` |

`/tools`  `/mcp`  JSON  `name` PowerShell  `curl.exe -d`  JSON  `--data-binary @file` 

### 25.4 

`optfile.exe` 

|  |  |  |
|---|---|---|
| `--replacement-text 'A\nB'` |  `A\nB` | PASS |
| `--replacement-text 'A\nB' --decode-escapes` |  `A` / `B` | PASS |
| `--replacement-text-base64`  `output << line << "\n";` | C++  | PASS |

`codex_lan_agent` 

```powershell
$env:CODEX_LLVM_ROOT='D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\libtorch_module\llvm_x64_install'
cmake --build D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\build_release_x64 --config Release --target codex_lan_agent
Copy-Item D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\build_release_x64\Release\codex_lan_agent.exe D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\build_release_x64\stage_x64\bin\codex_lan_agent.exe -Force
```

Release  `stage_x64\bin\codex_lan_agent.exe` SHA256 

HTTP 

```powershell
$bodyPath=Join-Path $env:TEMP 'codex_lan_agent_health_body.json'
[System.IO.File]::WriteAllText($bodyPath,'{"name":"lan_agent_health"}',[System.Text.UTF8Encoding]::new($false))
curl.exe -s -X POST -H 'Content-Type: application/json' --data-binary "@$bodyPath" http://127.0.0.1:18080/tools

$routeBody=Join-Path $env:TEMP 'codex_lan_agent_route_body.json'
[System.IO.File]::WriteAllText($routeBody,'{"name":"lan_agent_mcp_route","arguments":{"mode":"overview"}}',[System.Text.UTF8Encoding]::new($false))
curl.exe -s -X POST -H 'Content-Type: application/json' --data-binary "@$routeBody" http://127.0.0.1:18080/tools
```



|  |  |
|---|---|
| `lan_agent_health` | `ok:true` |
| `lan_agent_mcp_route` overview | PASS `request_model_profile=small-llm`  `required_tool_name` |
| `agent_server_stdout.log`  |  `agent_server_stdout_file_log_enabled`  `startup_complete` |
| `agent_server_stdout.log`  |  `tool_name":"lan_agent_health"`  `tool_name":"lan_agent_mcp_route"` |



```text
[tool_event] {"timestamp":"2026-08-14T14:14:34+08:00","tool_name":"lan_agent_mcp_route","event_type":"tool_call","command_name":"lan_agent_mcp_route","entry_name":"POST /tools","http_status":"200","mcp_route_mode":"overview","result":"mcp_overview"}
```

---
 |  +------------------------------------------------------------------+     |
 |  |   (Semantic Grid )                          |     |
 |  |  -  /  /  Bundle  ()        |     |
 |  +------------------------------------------------------------------+     |
 |  |   IO ()                                   |     |
 |  |  - : QSaveFile  +  +                     |     |
 |  |  - (future)  /  /                     |     |
 |  +------------------------------------------------------------------+     |
 |  |   (Artifact Store)                                |     |
 |  |  - ,  +  +               |     |
 |  +------------------------------------------------------------------+     |
 |  ========================================================================= |
 |                                                                           |
 |  []  SQLite ()  |  RocksDB (KV )  |            |
 |                                                                           |
 ============================================================================
```

### 21.3 

```
 LLM        (route)      CLIPS                TaskMemory
     |               |                |               |               |
     |  1.JSON   |                |               |               |
     |-------------->|                |               |               |
     |               | 2.      |               |               |
     |               |  (slot    |               |               |
     |               |   )     |               |               |
     |               |--------------->|               |               |
     |               |                | 3.     |               |
     |               |                |  (//   |               |
     |               |                |   )  |               |
     |               |  4.:       |               |               |
     |               |<---------------|               |               |
     |               |  route_target= |               |               |
     |               |  codex-code    |               |               |
     |               |                |               |               |
     |               | 5.      |               |               |
     |               |------------------------------->|               |
     |               |                |               | 6. |
     |               |                |               |  (Clang AST /  |
     |               |                |               |    / ) |
     |               |                |               |               |
     |               |                |               | 7.step    |
     |               |                |               |-------------->|
     |               |                |               |  ()    |
     |               |  8.+|               |               |
     |               |<-------------------------------|               |
     |               |                |               |               |
     |               | 9.CLIPS  |               |               |
     |               |       |               |               |
     |               |--------------->|               |               |
     |               |  10.    |               |               |
     |               |<---------------|               |               |
     |  11.  |                |               |               |
     |  (128+)|                |               |               |
     |<--------------|                |               |               |
     |               |                |               |               |
     |    pending_continuation:    |               |               |
     |    LLM 1    |               |               |
     |    TaskMemory       |               |               |
```

### 21.4   

|  |  |  |
|---|---|---|
| `lan_agent_mcp_route`  | **** |  MCP  stdio + streamable-http |
| Task Memory + step_ledger | **** |  C++ / |
| CLIPS  | ** & ** | **** |
| Semantic Grid  | **** |  RAG  Agent  Bundle  |
|  IO  | **** |  |

> ** LLM ** Prompt 

### 21.5  MCP   MCP 

#### 1. 

`compile_commands.json`Clang ASTC/C++ CMM 

```
 MCP Runtime
 
 
 
 CLIPS 
  IO / 
 
      [1] codex-code-plugin   Clang 
      [2] document-agent-plugin
      [3] hardware-debug-plugin
```

****

#### 2. 

- **** CLIPS 
- ****

#### 3.  / 

 MCP 

-  LLM 
- A  B 
- CPU Clang 

#### 4.  MCP 

-  streamable-http MCP  stdio
-  SDK

#### 5. 

-  Artifact  ASTCFGDOT 
- 

### 21.6 

|  |  |  |  |
|---|---|---|---|
| ** 1** | task memory IOCLIPS  |  |    |
| ** 2** | Clang / |  MCP  C++  +  |    ABI  |
| ** 3** | HTTP  API |  |    |

### 21.7  research-mcp 

|  |  |
|---|---|
| / | **research-mcp** |
|  | ** MCP Runtime** |

 MCP  +  Agent 

### 21.8 

 MCP 

- 
- 
- ** MCP **

 C/C++  Agent ** MCP ** stdio 

---

# 22.  MCP 

> ****

---

## 0. 

 AI AgentMCP  **LLM **

 **   MCP Runtime** AI  **LLM **  **** 

 LLM 

|  |  |
|---|---|
|  |  |
|  |  |
|  |  |
|  |  |
|  |  +  |
|  |  |

 AI  ****

---

## 1. 

### 1.1 

#### Primary Brain
> 

|  |  |
|---|---|
| **** |  |
| **** |  |

#### Secondary Brain
>  MCP  LLM 

|  |  |
|---|---|
| **** |  |
| **** | AI  /  **** |

---

### 1.2 



####  1
> 

LLM ****

####  2
>  LLM 

AI  ** Prompt **

####  3
>  ****



####  4
>  ****

 ****

####  5
> ****
- 
- 
- 



---

## 2. 

**** MCP 

|  |  |  |
|---|---|---|
| L1 |  |    |
| L2 |  |    +  |
| L3 |  |    |
| L4 |  |    |
| L5 |  |    |

---

### L1  1

 LLMAI  MCP 

- ****Stdio  / Streamable-HTTP 
- **** LLM /  /  Agent  / 
- ****

---

### L2  2

  ** MCP **

|  |  |
|---|---|
| **** |  LLM  Token  |
| **** |  **** + ****  |
| ****  | ** +  +  + ** |
|  |  |
|  |  LLM Prompt  |
|  |  |

---

### L3  3

****



|  |  |  |
|---|---|---|
| **1. ** |  |  /  |
| **2. ** | +  |  |
| **3. ** |  Bundle  |  ** RAG ** |
| **4.  IO ** |  |  |

---

### L4  4

****

#### 

|  |  |  |
|---|---|---|
|   |  | Clang AST / CFG / DFG |
|   |  +  |  |
|   |  |  |
|   |  |  |

****
-  /  /  /  / 
-  / 

---

### L5  5

 **** AI 

|  |  |  |
|---|---|---|
| **** | JSON / JSONL  | **** |
| **** | RocksDB / SQLite  |  |
| **** |  |  |

---

## 3. 

### 3.1 MCP 

|  |  |
|---|---|
| **** |  |
| **** |  /  /  |
| **** |  LLM  |

---

### 3.2 

|  |  |
|---|---|
| **** |  |
| **** | ** LLM** |
| **** |  |

> **** LLM 

---

### 3.3 

|  |  |
|---|---|
| **** |  |
| **** | +  |
| **** |  |

---

### 3.4 

|  |  |
|---|---|
| **** |  |
| **** | L1-L5  |
| **** |  RAG  |

---

### 3.5  IO 

|  |  |
|---|---|
| **** |  |
| **** |  |
| **** |  AI  |

---

## 4. 

 ** MCP **

---

### 4.1 

|  |  |  |
|---|---|---|
| 1 | **INIT**  |  |
| 2 | **RUNNING**  |  |
| 3 | **SUSPENDED**  |  |
| 4 | **CHECK**  |  |
| 5 | **COMPLETE**  |  |
| 6 | **FAILED / ROLLBACK**  |  /  |



`
INIT  RUNNING  SUSPENDED 
                                
                 CHECK  COMPLETE
                            
                             FAILED/ROLLBACK
`

> ** CHECK  COMPLETE **

---

### 4.2 



|  |  |
|---|---|
| 	ask_id |  |
| session_id |  |
| status | 6  |
| step_index |  |
| udget_quota |  /  |
| snapshot_hash |  /  |
| check_result |  |
| ledger_path |  |

---

### 4.3 

1. ****
2. **CHECK  COMPLETE ** CHECK ****  COMPLETE 
3. **SUSPENDED **
4. ****

---

## 5. 

### 5.1 

|  |  |
|---|---|
| LLM  |  +  LLM  |
|  |  +  +  |
|  /  |  |
| MCP  |  +  +  |
|  /  /  |  +  |
|  /  |  IO  +  +  |

**AI **

---

### 5.2 

****LangGraph / AutoGen  LLM  RAG  MCP  

|  |  |  |
|---|---|---|
|  MCP  |  | ** AI ** |
|  | Prompt  | **AI ** |
|  |  | **** |

---

### 5.3 



-  ** AI **
-  ****
-  ****

> **** AI  +  **** 

---

## 6. 

1. ****  
    ****

2. ****  
    ****

3. ****  
    ****

4. ****  
   

5. ****  
    ****

---

## 7. 

** -  MCP **  AI Prompt  MCP ** AI **

 **** 

>  ** AI**  ** AI** 

---
---

# 23. MCP 

> **** codex-lan-agent  Codex  MCP 

---

## 23.1 

|  |  |
|---|---|
|  
eeds_continue  |  10+  reason_code |
| lan_agent_mcp_route  | 	erminal_state=false  |
| lan_agent_list_directory  |  150  |
|  |  |
| pending continuation  | logs/mcp_pending_continuations/ 30  |
|  | codex  |

8  13  16:14 ~ 16:42

`
status=needs_continue  10+  ()
status=failed            8  ()
status=success           3  ()
`

---

## 23.2 

### Step 1

 logs/mcp_trace_audit_events.jsonl  post_guard_reason_code Top 3 

| reason_code |  |  CLIPS  |
|---|---|---|
| 
on_terminal_result_forbids_final_answer | 10 | 
on-terminal-result-forbids-final-answer |
| directory_batch_pending | 6 | directory-batch-read-still-pending |
| ead_chain_incomplete | 5 | incomplete-read-result-requires-continuation |

### Step 2

 src/clips_rules/rules/mcp_result_guard.clp slot 

** 1
on_terminal_result_forbids_final_answer **

mcp_result_guard.clp L160~L174
`clips
(defrule non-terminal-result-forbids-final-answer
  (declare (salience 47))
  (mcp_tool_result (tool_name ?tool)
                   (terminal_state "false")          ;  
                   (completion_claim_allowed "false"));  
  =>
  (decision "route")                          ;  
`

C++ [main.cpp#L1547](file:///D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/main.cpp#L1547-L1549) 	ool_call_only  	erminal_state=false + completion_claim_allowed=false** decision=route**codex ""    ****

** 2directory_batch_pending **

mcp_result_guard.clp L398~L417 lan_agent_list_directory lan_agent_final_answercodex " 150 "

** 3ead_chain_incomplete **

mcp_result_guard.clp L380~L396 lan_agent_list_directory ""	ask_completion=incomplete codex 

** 4directory-list/read-result-requires-declared-continuation **

mcp_result_guard.clp L208~L272 atch_completion=incomplete 
ext_call_json""

** 5inal_answer_disallowed_by_result **

 1  inal_answer_allowed=false  C++ ""

** 6pending continuation **

logs/mcp_pending_continuations/  30  .kv  8  11  oute-mismatched-pending-continuation  pending_continuation_active=true  continuation 

---

## 23.3 

****** 
ext_call_json**"codex " LLM 

|  |  |  |
|---|---|---|
| 
on-terminal-result-forbids-final-answer | 	erminal_state=false + completion_claim_allowed=false   | ** (next_call_json ?next&:(neq ?next "")) ** |
| inal-answer-disallowed-by-result | inal_answer_allowed=false   | ** (next_call_json ?next&:(neq ?next "")) ** |
| directory-list-result-requires-declared-continuation | atch_completion=incomplete   | ** (next_call_json ?next&:(neq ?next "")) ** |
| directory-read-result-requires-declared-continuation | atch_completion=incomplete   | ** (next_call_json ?next&:(neq ?next "")) ** |
| incomplete-read-result-requires-continuation |  lan_agent_list_directory | ** lan_agent_list_directory** |
| directory-batch-read-still-pending |  lan_agent_list_directory + lan_agent_final_answer | **** |


-  logs/mcp_pending_continuations/ 30  0 continuation 
-  src/ClipsDecisionOperations.h  **embedded fallback ** .clp  fallback 

---

## 23.4 

|  |  |  |
|---|---|---|
| lan_agent_mcp_route  |  decision=route |  default-mcp-result-verifiedsalience=-100decision=allowcodex  |
| lan_agent_list_directory  |  150  +  |  allowcodex  |
|  
ext_call_json |  |  |
|  pending continuation  |  continuation takeover  |  |

---

## 23.5 

|  |  |
|---|---|
| src/clips_rules/rules/mcp_result_guard.clp | **5  + ** |
| src/ClipsDecisionOperations.h | **embedded fallback ** |
| logs/mcp_pending_continuations/*.kv | 30  0 |
| codex_lan_agent.exe |  header  |

Release CLIPS .clp  LoadClipsFileIfExists  10+  reason_code  needs_continue 

---

## 23.6 

>  4  6 

1. ** 
ext_call_json **  
   	erminal_state=false / inal_answer_allowed=false / completion_claim_allowed=false  C++ """" 
ext_call_json 

2. ****  
   atch_completion=incomplete lan_agent_read_text_file / lan_agent_read_directory_files / lan_agent_run_cxparser_flowlan_agent_list_directorylan_agent_final_answer

3. **Fallback **  
   CLIPS  raw string fallback  file_rules ""

4. **pending continuation  TTL**  
    continuation  24 

---
# 24. MCP  CLIPS 

> ****codex-lan-agent  Codex  MCP Codex 

---

## 24.1 

|  |  |
|---|---|
| Get-Process codex_lan_agent | **** |
| logs/agent.out.log |  |
| logs/server_state.json |  |
| logs/server.lock |  |
| logs/remote_control_events.jsonl | **393 MB**411,590,492 bytes |
| logs/mcp_trace_audit_events.jsonl | 7.3 MB |
| logs/mcp_pending_continuations/ | 0  |

****MCP HTTP  18080 Codex 

---

## 24.2 

###  A IO 

emote_control_events.jsonl  **393 MB** MCP 
- 
- 
-  OOM 

###  BCLIPS Run(env, -1) 

[ClipsDecisionOperations.h](file:///D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/ClipsDecisionOperations.h) 
`cpp
Run(env, -1);  // -1 
`

 CLIPS  A  B B  ARun(-1) 
-  EvaluateClipsDecision 
- HTTP 
-  Environment 
- 

###  CCLIPS  fact 

CLIPS  fact (assert ...)  RHS  fact
- ** fact-factory ** fact
- ****
- ****fact 

###  D fact 

C++  ssert_fact lambda  fact  domain  fact  fact  CLIPS  fact 

---

## 24.3 

****fact-factory  factCLIPS  fact decision=allow

###  fact 

|  |  |
|---|---|
|  | CLIPS_MAX_FACTS_PER_SESSION = 500 |
|  | ssert_fact GetNumberOfFacts(env) >= 500   circuit_breaker_facts_exceeded |
|  | Run act_count_after_run > 500   |
|  | [ClipsDecisionOperations.h L2254-2257](file:///D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/ClipsDecisionOperations.h#L2254-L2257) () / L2303-2306 () |

###  fact 

|  |  |
|---|---|
|  | std::set<std::string> asserted_signatures fact  256  |
|  |   ++duplicate_facts_blocked |
|  | [ClipsDecisionOperations.h L2240-2253](file:///D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/ClipsDecisionOperations.h#L2240-L2253) |

### 

|  |  |
|---|---|
|  | CLIPS_MAX_RULE_FIRINGS = 200 |
|  | Run(env, -1)  Run(env, CLIPS_MAX_RULE_FIRINGS) |
|  |  >= 200   circuit_breaker_rules_exceeded |
| �码位置 | [ClipsDecisionOperations.h L2296-2300](file:///D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/ClipsDecisionOperations.h#L2296-L2300) |

### 熔断触发时行为

`
任一熔断器触发
    ↓
decision.decision = "allow"              // 强制放行，不阻塞调用方
decision.verification = "circuit_breaker_triggered"
decision.reason_code = "clips_internal_circuit_breaker"
decision.next_action = "circuit_breaker: clips internal limit exceeded"
    ↓
DestroyEnvironment(env)                  // 立即销毁 Environment 释放内存
return decision                          // 返回安全结果
`

### 审计输出新增字段

每个 CLIPS decision 结果新增 6 个审计字段，可在 mcp_trace_audit_events.jsonl 中观测：

| 字段 | 说明 |
|---|---|
| act_count_before_run | Run 前已存在 fact 数 |
| act_count_after_run | Run 后总 fact 数 |
| ule_firings_actual | Run 实际触发的规则数 |
| duplicate_facts_blocked | 被去重拦截的 fact 数 |
| circuit_breaker_facts_exceeded | 熔断①是否触发 |
| circuit_breaker_rules_exceeded | 熔断③是否触发 |

### 附带修复：日志膨胀治理

- emote_control_events.jsonl：393 MB → 0（清理）
- mcp_trace_audit_events.jsonl：7.3 MB → 0（清理）
- 后续应增加日志轮转机制（单文件超 10 MB 自动截断）

---

## 24.4 验证结论

修复后重新编译部署 codex_lan_agent.exe，验证结果：

| 验证项 | 结果 |
|---|---|
| 进程状态 | **运行中** PID=25436，内存 8.7 MB |
| exe 编译时间 | 17:12:27（晚于源码修改 17:11:47，确认熔断器代码已编译进 exe） |
| pending continuations | 0 文件 |
| 日志文件大小 | 全部 0 MB |
| MCP 	ools/list 响应 | **正常**（protocol=2.0，tools count=1） |

**结论：崩溃问题已解决。** 三层熔断器确保：

1. **CLIPS 内部 fact 爆炸** → 熔断①在 500 fact 时停止断言，强制返回
2. **相同 fact 重复插入** → 熔断②在签名命中时拦截，计数但不断言
3. **规则互相触发死循环** → 熔断③在 200 次规则触发后停止 Run，强制返回
4. **日志膨胀** → 已清理；后续应增加日志轮转

任一熔断触发时，服务不会崩溃，而是返回 decision=allow + circuit_breaker_triggered 标记，保证 Codex 工作流不中断。

---

## 24.5 熔断器与 fact-factory 守卫层的关系

`
外部 LLM / Codex 输出
        ↓
┌─────────────────────────────┐
│  fact-factory 守卫层（外部）  │  ← 管住从外部流入推理引擎的 fact
│  第0层: 字节硬过滤            │     字节消毒、长度检查、CLIPS 转义
│  第1层: CppJieba 分词         │
│  第2层: marisa-trie 检索      │
│  第3层: 语义同义归一           │
└──────────┬──────────────────┘
           ↓ 消毒后的 fact 进入 CLIPS
┌─────────────────────────────┐
│  CLIPS 推理引擎（内部）        │  ← 内部规则执行产生的新 fact 不经过守卫层
│  规则匹配 → assert 新 fact    │
│  ┌───────────────────────┐   │
│  │ 熔断①: fact 数量 ≤ 500 │   │  ← 运行时硬上限
│  │ 熔断②: 签名去重        │   │  ← C++ 侧断言去重
│  │ 熔断③: 规则触发 ≤ 200  │   │  ← Run 有界替代无限
│  └───────────────────────┘   │
└──────────┬──────────────────┘
           ↓
     decision 结果返回
`

> **明确边界**：fact-factory 守卫层无法管住 CLIPS 内部自生 fact。三层熔断器是独立于守卫层的运行时安全机制，二者互补但职责分离。

---

---

## Headless 命令被白名单拒绝的处理方法

### 问题现象

当 Headless 或验收程序通过 `local_cli` / `lan_agent_run_command` 被当作普通命令直接执行时，可能返回：

```text
unsupported local_cli command
```

这表示请求已经进入 `codex-lan-agent`，但命中了本地 CLI 网关的命令白名单拦截。它不是 Headless 程序本身崩溃，也不是 Windows 找不到 exe，而是调用方式不在 `local_cli` 允许的命令集合内。

`local_cli` 只接受固定的内置命令，例如 `health`、`chat-status`、`task-latest`、`task`、`log-latest`、`diff`、`run-light`、`build-target`、`test-result`、`thread-report`、`mkdir`。不要把任意 exe、`cmd.exe`、`powershell.exe` 或 `Headless` 字符串直接塞进 `command` 字段。

### 推荐方案：用 CLI profile 加入白名单

需要运行 `D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/build01/Release/cxvision_imgui_acceptance.exe` 时，推荐把它注册为 `profile`，然后通过 `lan_agent_run_cli_profile` 调用。

1. 确认程序路径在 `allowed_roots` 覆盖范围内。当前 `cxvisionai` 应在允许根目录中，例如：

```ini
allowed_roots=D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent;D:/Codex-WorkDir/Sean_WorkDir/cxvisionai
```

2. 在 `codex_lan_agent.cfg` 中加入 profile 配置：

```ini
profile.cxvision_imgui_acceptance="D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/build01/Release/cxvision_imgui_acceptance.exe"
profile_timeout.cxvision_imgui_acceptance=1800
profile_stall_timeout.cxvision_imgui_acceptance=120
```

路径建议使用 `/`，避免 Windows 反斜杠转义问题。如果使用 `\`，需要确认配置解析不会把它当作转义字符处理。

3. 重启 `codex-lan-agent serve`，让配置生效。

4. 通过 MCP 网关调用 profile：

```json
{
  "name": "lan_agent_mcp_route",
  "arguments": {
    "mode": "call",
    "target_tool_name": "lan_agent_run_cli_profile",
    "arguments": {
      "profile": "cxvision_imgui_acceptance",
      "args": "",
      "timeout_sec": 1800,
      "stall_timeout_sec": 120
    }
  }
}
```

如果 Headless 需要参数，把参数放入 `args`，例如：

```json
{
  "profile": "cxvision_imgui_acceptance",
  "args": "--headless --timeout 300",
  "timeout_sec": 1800,
  "stall_timeout_sec": 120
}
```

上线前可先做 dry-run，确认实际命令行和白名单解析结果：

```json
{
  "name": "lan_agent_mcp_route",
  "arguments": {
    "mode": "call",
    "target_tool_name": "lan_agent_run_cli_profile",
    "arguments": {
      "profile": "cxvision_imgui_acceptance",
      "args": "--headless",
      "dry_run": true
    }
  }
}
```

### 兼容方案：配置化 local_cli run-light action

如果旧线程或旧客户端只能调用 `local_cli` / `lan_agent_run_command`，仍然不要新增任意命令透传。当前支持把固定 `action_id` 通过配置映射到已注册的 CLI profile。

配置格式：

```ini
profile.cxvision_imgui_acceptance="D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/build01/Release/cxvision_imgui_acceptance.exe"
profile_timeout.cxvision_imgui_acceptance=1800
profile_stall_timeout.cxvision_imgui_acceptance=120
local_cli_run_light.run_cxvision_imgui_acceptance=cxvision_imgui_acceptance
```

含义：

- `profile.cxvision_imgui_acceptance` 定义真正允许执行的程序入口。
- `local_cli_run_light.run_cxvision_imgui_acceptance` 把旧调用里的 `action_id` 映射到该 profile。
- 修改这些配置后需要重启 `codex-lan-agent serve`，但不需要重新
-  shell `run-light`  cfg 



```json
{
  "name": "lan_agent_mcp_route",
  "arguments": {
    "mode": "call",
    "target_tool_name": "lan_agent_run_command",
    "arguments": {
      "command": "run-light",
      "action_id": "run_cxvision_imgui_acceptance",
      "args_text": "--headless",
      "dry_run": true
    }
  }
}
```

 `dry_run`  `false`


2026-08-27 

- `profile.<name>``profile_timeout.<name>``profile_stall_timeout.<name>``local_cli_run_light.<action_id>`  `codex_lan_agent.cfg` Headless/
-  `codex_lan_agent serve`  `local_cli_run_light` allowlist cfg  serve
-  `action_id is not in run-light allowlist`



```ini
profile.verify_repo_upload_policy=powershell -ExecutionPolicy Bypass -File "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/scripts/verify_repo_upload_policy.ps1"
profile_timeout.verify_repo_upload_policy=120
profile_stall_timeout.verify_repo_upload_policy=30
local_cli_run_light.verify_repo_upload_policy=verify_repo_upload_policy
```



```json
{
  "name": "lan_agent_mcp_route",
  "arguments": {
    "mode": "call",
    "target_tool_name": "lan_agent_run_command",
    "arguments": {
      "command": "run-light",
      "action_id": "verify_repo_upload_policy",
      "args_text": "",
      "dry_run": false
    }
  }
}
```


### 

-  `unsupported local_cli command` `command`  `local_cli`  `command=run-light`  `action_id` `lan_agent_run_cli_profile`
-  `action_id is not in run-light allowlist` run-light  `local_cli_run_light.<action_id>`
-  `run-light action maps to unknown profile` action  `profile.<name>`
-  `unknown profile` `codex_lan_agent.cfg`  profile serve 
- / exe  `allowed_roots` 
-  `profile_timeout.<name>` / `profile_stall_timeout.<name>` `logs/agent_server_stdout.log`  task ledger

 Headless/ `allowed_roots`  `profile.<name>`  `local_cli run-light`  `local_cli_run_light.<action_id>` 

### 小模型/大模型都必须遵守的桌面能力发现规则

`codex-lan-agent` 默认只向 MCP 客户端暴露一个网关工具 `lan_agent_mcp_route`。因此，`tools/list` 中没有单独的 `screenshot`、`click`、`keyboard` 工具，并不代表桌面截图、鼠标控制或图像分析能力不存在。

桌面能力的稳定入口是二级动作面：

```text
lan_agent_mcp_route -> lan_agent_run_command -> command=run-light -> action_id=<desktop action>
```

当前桌面动作：

```text
ui_screenshot, ui_cursor, ui_move, ui_click, ui_key, ui_key_press, ui_type, ui_hotkey, ui_activate_window, ui_get_focused_control, ui_analyze, ui_screenshot_analyze
```

键盘动作参数：

```text
ui_key       args_text="--key F4"
ui_key_press args_text="--key F4"
ui_hotkey    args_text="--keys CTRL+SHIFT+F4"
ui_type      args_text="--text hello"
```

`ui_*` 桌面动作会在 gateway 中自动补齐 bridge 的 `--action` 参数；例如 `action_id=ui_key,args_text="--key F4"` 会实际执行 `--action key --key F4`。如果调用方已经显式传入 `--action` 或 `-a`，gateway 不覆盖。

窗口闭环控制推荐顺序：

```text
1. ui_activate_window      定位并激活目标窗口
2. ui_get_focused_control  读取当前前台窗口和焦点控件
3. ui_screenshot           按窗口或控件 rect 截图
4. ui_click                按 rect 中心点或分析结果点击
5. ui_key/ui_hotkey/ui_type 对已聚焦控件发送键盘输入
6. ui_get_focused_control 或 ui_screenshot_analyze 再读状态，确认操作结果
```

`ui_activate_window` 支持 `--hwnd <native-handle>`、`--pid <pid>`、`--title <substring>` 和 `--exact`。推荐优先级是：已有 `hwnd` 时用 `--hwnd`；只有进程信息时用 `--pid`；只知道窗口标题时用 `--title`，必要时加 `--exact`。

`ui_get_focused_control` 返回 `foreground_window` 与 `focused_control` 的 `hwnd/title/class_name/pid/thread_id/rect`，还会返回 `caret_rect/caret_hwnd/flags`。后续截图、点击和键盘输入都应基于这些返回字段，而不是凭模型猜坐标。

最小调用模板：

```json
{
  "mode": "call",
  "target_tool_name": "lan_agent_run_command",
  "arguments": {
    "command": "run-light",
    "action_id": "ui_get_focused_control",
    "args_text": "",
    "dry_run": false
  }
}
```

窗口激活示例：

```json
{
  "mode": "call",
  "target_tool_name": "lan_agent_run_command",
  "arguments": {
    "command": "run-light",
    "action_id": "ui_activate_window",
    "args_text": "--title cxvision",
    "dry_run": false
  }
}
```

如果 `ui_get_focused_control` 返回：

```json
{
  "foreground_window": {
    "hwnd": "656614",
    "title": "codex-lan-agent",
    "class_name": "CASCADIA_HOSTING_WINDOW_CLASS",
    "pid": 6200,
    "rect": {"left": 221, "top": 68, "width": 1499, "height": 609}
  },
  "focused_control": {
    "hwnd": "525620",
    "class_name": "Windows.UI.Input.InputSite.WindowClass"
  }
}
```

则下一步应使用确定性参数继续闭环：

```text
ui_activate_window args_text="--hwnd 656614"
ui_screenshot      args_text="--x 221 --y 68 --width 1499 --height 609"
ui_click           args_text="--x 970 --y 372"
ui_key             args_text="--key F4"
ui_hotkey          args_text="--keys CTRL+SHIFT+F4"
ui_type            args_text="--text hello"
```

关键约束：

- `ui_*` 桌面动作会在 gateway 中自动补齐 bridge 的 `--action` 参数；例如 `action_id=ui_activate_window,args_text="--hwnd 656614"` 会实际执行 `--action activate-window --hwnd 656614`。
- `ui_get_focused_control` 和 `ui_activate_window` 的 bridge 原始 JSON 写入对应 `logs/cxvision_ui_bridge_*.log`；run-light 返回体里的 `log_path` 是读取证据的入口。
- 控制闭环必须至少包含一次“读状态”：操作前读 `ui_get_focused_control`，操作后读 `ui_get_focused_control` 或 `ui_screenshot_analyze`，不能只发点击/键盘后直接声称成功。

小模型优先使用这个最小 JSON，不要自行改成 `powershell`、`cmd.exe`、exe 路径或 `Headless` 作为 `command`：

```json
{
  "mode": "call",
  "target_tool_name": "lan_agent_run_command",
  "arguments": {
    "command": "run-light",
    "action_id": "ui_screenshot",
    "args_text": "",
    "dry_run": true
  }
}
```



错误处理约定：

- `unsupported local_cli command`：`command` 写错了；把 `command` 改成 `run-light`，把具体动作写到 `action_id`。
- `action_id is not in run-light allowlist`：`action_id` 不在当前进程白名单中；读取返回体里的 `safe_action_allowlist`，或重启 serve 让 cfg 中新增的 `local_cli_run_light.*` 生效。
- 小模型不要因为 `tools/list` 只有 `lan_agent_mcp_route` 就回复“没有截图工具”；必须先读 `mcp_overview` 的 `desktop_ui_available` / `desktop_ui_actions_csv` / `desktop_ui_call_example_json` 字段。

## cxvision_ui_bridge Qt desktop interface

Initial desktop image operations are implemented as a small Qt console interface program, matching the `optfile` layout instead of embedding UI control directly in `codex_lan_agent.exe`.

Source layout:

```text
cxvision_ui_bridge/
  cxvision_ui_bridge.pro
  main.cxx
```

Build with Qt 5.14.2 / MinGW:

```bat
cd /d D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\cxvision_ui_bridge
C:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe cxvision_ui_bridge.pro -spec win32-g++ "CONFIG+=release"
C:\Qt\Qt5.14.2\Tools\mingw730_64\bin\mingw32-make.exe release
copy /Y Release\cxvision_ui_bridge.exe D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\cxvision_ui_bridge.exe
```

Runtime config, loaded by `codex_lan_agent.cfg` after serve restart:

```ini
profile.cxvision_ui_bridge="D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/cxvision_ui_bridge.exe"
profile_timeout.cxvision_ui_bridge=1800
profile_stall_timeout.cxvision_ui_bridge=120
local_cli_run_light.ui_screenshot=cxvision_ui_bridge
local_cli_run_light.ui_cursor=cxvision_ui_bridge
local_cli_run_light.ui_move=cxvision_ui_bridge
local_cli_run_light.ui_click=cxvision_ui_bridge
local_cli_run_light.ui_analyze=cxvision_ui_bridge
local_cli_run_light.ui_screenshot_analyze=cxvision_ui_bridge
```

Direct bridge actions:

```bat
cxvision_ui_bridge.exe --action screenshot --output D:\temp\screen.png
cxvision_ui_bridge.exe --action cursor
cxvision_ui_bridge.exe --action move --x 500 --y 300
cxvision_ui_bridge.exe --action click --x 500 --y 300 --button left --clicks 1
```

Analysis is intentionally parameter-template based because `cxvision_imgui_acceptance.exe` may change its CLI contract. The bridge replaces `{image}` and `{output}` before starting cxvision:

```bat
cxvision_ui_bridge.exe --action analyze ^
  --image D:\temp\screen.png ^
  --cxvision-exe D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build01\Release\cxvision_imgui_acceptance.exe ^
  --cxvision-workdir D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo ^
  --analysis-output D:\temp\screen.analysis.json ^
  --cxvision-args "--headless --cxscript-headless --image {image} --script D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cxparser\cxscript\module\cximage\find_circle_direct_test.cxsc --case-name codex_ui_bridge_smoke --out {output} --max-steps 10000 --unified-log D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxscript_runs\_shared\cxvision_imgui_acceptance.jsonl" ^
  --timeout-ms 120000
```

Combined capture plus analysis:

```bat
cxvision_ui_bridge.exe --action screenshot-analyze ^
  --output D:\temp\screen.png ^
  --cxvision-exe D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\build01\Release\cxvision_imgui_acceptance.exe ^
  --cxvision-workdir D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo ^
  --analysis-output D:\temp\screen.analysis.json ^
  --cxvision-args "--headless --cxscript-headless --image {image} --script D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cxparser\cxscript\module\cximage\find_circle_direct_test.cxsc --case-name codex_ui_bridge_smoke --out {output} --max-steps 10000 --unified-log D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxscript_runs\_shared\cxvision_imgui_acceptance.jsonl"
```

All actions print one compact JSON object to stdout. `screenshot` returns `image_path`, `image_mime`, and capture rectangle. Mouse actions return the final coordinates. Analysis actions return the cxvision process exit code, stdout/stderr preview, and expected analysis output path.

After the config-driven run-light allowlist is active and `codex_lan_agent serve` has been restarted, the bridge can also be built through the auditable gateway action:

```json
{
  "name": "local_cli",
  "arguments": {
    "command": "run-light",
    "action_id": "build_cxvision_ui_bridge"
  }
}
```

The build action runs `build_cxvision_ui_bridge.bat`, which generates the qmake Makefile, runs `mingw32-make release`, copies `cxvision_ui_bridge/Release/cxvision_ui_bridge.exe` to `D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/cxvision_ui_bridge.exe`, and deploys the required Qt GUI runtime files under the same directory.


### cxvision_ui_bridge restart verification - 2026-08-27

After restarting `codex_lan_agent serve`, the gateway health check passed:

```json
{"ok":true,"status":"success","outcome":"PASS","current_tool_chain_node":"lan_agent_health"}
```

The active `codex_lan_agent.cfg` was read through the gateway and contains the expected external run-light mappings:

```ini
profile.cxvision_ui_bridge="D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/cxvision_ui_bridge.exe"
local_cli_run_light.ui_screenshot=cxvision_ui_bridge
local_cli_run_light.ui_cursor=cxvision_ui_bridge
local_cli_run_light.ui_move=cxvision_ui_bridge
local_cli_run_light.ui_click=cxvision_ui_bridge
local_cli_run_light.ui_analyze=cxvision_ui_bridge
local_cli_run_light.ui_screenshot_analyze=cxvision_ui_bridge
local_cli_run_light.build_cxvision_ui_bridge=build_cxvision_ui_bridge
```

However, the restarted live service still rejects the configured action:

```json
{
  "ok": false,
  "exit_code": 45,
  "error": "action_id is not in run-light allowlist",
  "action_id": "build_cxvision_ui_bridge",
  "safe_action_allowlist": "check_remote_online,check_local_chat,read_latest_log,get_git_diff,read_test_result"
}
```

Conclusion: the Qt bridge executable itself has already been built and previously validated, including screenshot, cursor, mouse move, and a real `cxvision_imgui_acceptance.exe` headless analysis returning `cxscript_headless_ok=true`. The remaining failure is not in `cxvision_ui_bridge`; it is that the live `codex_lan_agent serve` process is still using the old built-in run-light allowlist. To complete gateway execution, deploy the rebuilt `codex_lan_agent.exe` that includes `local_cli_run_light.*` config parsing to the actual serve path, then restart serve with `D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/codex_lan_agent.cfg`.


### cxvision_ui_bridge gateway verification - second pass 2026-08-27

After deploying the rebuilt `codex_lan_agent.exe` to the actual `stage_x64/bin` serve path and restarting, the external run-light allowlist is now active.

Health check:

```json
{"ok":true,"status":"success","outcome":"PASS","current_tool_chain_node":"lan_agent_health"}
```

Dry-run for `build_cxvision_ui_bridge` now succeeds and shows the configured actions in the live allowlist:

```text
check_remote_online,check_local_chat,read_latest_log,get_git_diff,read_test_result,
run_cxvision_imgui_acceptance,ui_click,ui_screenshot,ui_analyze,ui_cursor,ui_move,
ui_screenshot_analyze,build_cxvision_ui_bridge
```

`ui_screenshot` dry-run also succeeds and resolves to profile `cxvision_ui_bridge` through `local_cli_run_light.ui_screenshot`.

Real execution of `ui_cursor` currently fails at process start:

```json
{
  "ok": false,
  "exit_code": 4,
  "error": "failed to start process: D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/cxvision_ui_bridge.exe --action cursor",
  "action_id": "ui_cursor",
  "profile": "cxvision_ui_bridge",
  "execution_backend": "configured_run_light_profile"
}
```

Code analysis: `RunConfiguredCommand()` starts profile processes with `working_directory=config.workspace_root`. The current cfg uses a semicolon-separated multi-root value:

```ini
workspace_root=D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent;D:/Codex-WorkDir/Sean_WorkDir/cxvisionai
```

That string is not a valid Windows directory for `CreateProcessA`, so the process cannot start even though the allowlist and profile mapping are now correct.

Immediate config-only fix:

```ini
workspace_root=D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent
allowed_roots=D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent;D:/Codex-WorkDir/Sean_WorkDir/cxvisionai;D:\Codex-WorkDir\Sean_WorkDir\AItest\muparser-master\src
```

Recommended code hardening: profile execution should not pass a multi-root string directly as the child process working directory. `RunConfiguredCommand()` should use `config.config_dir` or the first valid workspace root as `CreateProcessA` working directory, while path authorization continues to use `workspace_root` / `allowed_roots` separately.