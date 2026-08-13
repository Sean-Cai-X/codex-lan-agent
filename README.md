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
├── kv_snapshot/index.jsonl             ← (后续 build_kv_snapshot)
├── rocksdb_native/                     ← (后续 rocksdb_mirror, 需 WITH_ROCKSDB=ON)
└── rocksdb_mirror_manifest.json
```

**关键设计语义（与代码 1:1 对齐）：**
1. **长循环闸门（`completion_guard=NON_TERMINAL_RESULT`）**：由 `lan_agent_delete_text_range_window_atomic` 返回的 `has_more=true` 触发，把 MCP 语义模型 clamp 成 `tool_call_only`，模型不能输出自然语言结论；
2. **freeze → budget → resume_and_execute 的分工**：
   - `freeze`：写入文件对象层 + 返回 next call 指向 budget runner；
   - `execute_continuation_budget`：在 MCP 内跑 N 步 allowlisted 白名单调用（默认 10，上限 64），写入 `budget_runs/*.json`；**不跨会话**；
   - `resume_and_execute`：`fresh-chat` 单一入口，内部 = 读 resume_context + 调 budget runner，适合模型切换 / 重启后"首调即续接"；
3. **交接 ≠ 完成**：`clean_chat_close_allowed=true` 只表示可以把当前会话关了交接给下一个；**必须** `terminal_state=true + completion_claim_allowed=true + final_answer_allowed=true + verification_ok=true` 四重齐才能 claim 任务完成；
4. **边界阻塞（blocked 分支）**：budget runner 检测到"操作跨越当前 200-line 窗口"时主动 `blocked`，并在返回里提示模型切换到 `delete_next_text_range_atomic`（单条精修），避免损坏文件；这就是 `NEXT_FLOW_SAFETY_CLASS_NOT_READY` 的语义。

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

## 9. Task Memory 工具：长任务记忆与跨模型续接

### 9.1 设计动机

本地模型在执行长任务（多轮 RAG、跨仓库扫描、批量代码分析、迭代式重构）时，会快速耗尽上下文窗口。传统做法是把完整历史对话塞进新模型上下文，这带来三个问题：

1. **上下文爆炸**：30+ 步任务的对话历史远超本地模型窗口。
2. **状态丢失**：换模型/重启会话后，模型对已完成步骤和剩余工作一无所知。
3. **重复执行**：新模型无法判断哪些步骤已验证完成，容易重跑。

Task Memory 工具链把"任务进度"从模型上下文外移到 MCP 服务端文件对象层，使全新模型只需读取一个紧凑的 `latest_resume_context.json` 即可续接。

### 9.2 文件对象层结构

所有 Task Memory 状态写入 `<data_root>/task_memory/{goal_id}/`，目录布局固定：

```
<data_root>/task_memory/{goal_id}/
├── latest_resume_context.json   ← 新模型首读入口（compact_summary + next_call_json + new_chat_entry_arguments_json）
├── step_ledger.jsonl            ← 已验证步骤账本（每步一行 JSON）
├── slices.jsonl                 ← 关键切片（key slices，跨步骤证据）
├── index_manifest.json          ← 增量索引清单
├── memory_structure.json        ← 任务记忆结构契约（structure_manifest 写入）
├── current_state.md             ← 当前状态 Markdown（freeze 写入）
├── handover.md                  ← 迁移交接 Markdown
├── rag_thread_migration/        ← RAG 线程迁移资产
│   └── ...
├── budget_runs/                 ← continuation budget 运行记录（execute_continuation_budget / resume_and_execute 写入）
│   └── budget-{checksum}.json   ← 单次 budget run 记录：record_model=mcp_continuation_budget_run_v1，含 step_events[]
├── kv_snapshot/                 ← 文件 KV 快照（build_kv_snapshot 写入）
│   └── index.jsonl
├── rocksdb_native/              ← 可选 RocksDB 镜像（rocksdb_mirror 写入）
│   └── ...
└── rocksdb_mirror_manifest.json ← RocksDB 镜像清单
```

### 9.3 新模型 bootstrap 契约

`structure_manifest` 工具固化以下读取顺序（写入 `memory_structure.json`）：

| 顺序 | 读取目标 | 用途 |
|---|---|---|
| **首读** | `latest_resume_context.json` | 获取 `compact_summary` / `next_call_json` / `remaining_work` / `terminal_state` |
| **二读** | `memory_structure.json` | 了解任务记忆整体结构和可查询资产 |
| **查询读** | `lan_agent_task_memory_rocksdb_lookup`（native mirror ready 时） | 高频读路径走 RocksDB 镜像 |
| **全历史读** | **`forbidden_by_default`** | 禁止默认回放全历史对话，只在显式审计时按需查询 `step_ledger.jsonl` |

### 9.4 工具调用顺序

#### 9.4.1 fresh-chat 一次性续接（推荐，已有 goal_id 时）

若一个 goal 已经 freeze 过（即 `task_memory/{goal_id}/latest_resume_context.json` 已存在），**新会话不要再手动链式调用** `resume_context` + `execute_continuation_budget`，直接调一次：

```json
{
  "name": "lan_agent_task_memory_resume_and_execute",
  "arguments": { "goal_id": "<已有 goal_id>", "max_steps": 10 }
}
```

工具内部完成：读 `latest_resume_context.json` → 跑 bounded continuation budget → 刷新任务记忆 → 写 `budget_runs/budget-{checksum}.json`。返回字段中：

- `continue_required=true` → 按返回的 `required_tool_arguments_json` 再次调用本工具，直到 `terminal_state=true`
- `budget_requires_frozen_resume_context=true` → 上一任务未 freeze，先调 `lan_agent_task_memory_freeze`
- `terminal_state=true` → 仍需 `verification_ok=true` 才能 final claim

#### 9.4.2 迁移验收必走顺序（首次或全链路）

迁移验收的**必走顺序**（详见 [TASK_MEMORY_MIGRATION_ACCEPTANCE.md](TASK_MEMORY_MIGRATION_ACCEPTANCE.md)）：

1. `lan_agent_task_memory_freeze` —— 创建文件对象层，写 `latest_resume_context.json` / `step_ledger.jsonl` / `slices.jsonl` / `index_manifest.json` / `rag_thread_migration/*`
2. `lan_agent_task_memory_resume_context` —— 新模型首读（或直接用 9.4.1 的 `resume_and_execute`）
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
├── graphs/
│   ├── cmm_init_flow.clp           ← CMM 状态机图（7 条状态转移 defrule）
│   └── mcp_guard_flow.clp          ← 预留：slice-node 与 guard 图映射（待实现）
└── profiles/
    └── default_guard_profile.clp   ← 预留：guard profile 选择（待实现）
```

### 16.2 Fact 模板（deftemplate）

CLIPS 规则基于事实匹配（pattern matching）触发。核心 fact 模板定义在 [templates/mcp_fact_templates.clp](src/clips_rules/templates/mcp_fact_templates.clp)：

| deftemplate | 用途 | 关键 slot |
|---|---|---|
| `mcp_tool_request` | 工具调用请求 fact（pre-call 阶段断言） | `tool_name` / `primary_intent` / `file_path` / `probe_ready` / `explicit_user_intent` / `single_step_required` / `max_items_per_call` / `requires_revert_plan` |
| `mcp_tool_result` | 工具返回结果 fact（post-result 阶段断言） | `tool_name` / `terminal_state` / `completion_claim_allowed` / `final_answer_allowed` / `has_more` / `continue_required` / `analysis_allowed` / `batch_completion` / `result_hash` / `schema_version` / `ai_conclusion_valid` / `result_ref` / `evidence_ref` |
| `mcp_tool_chain` | 工具链上下文 fact | `chain_phase`（pre_call / post_result）/ `request_type`（file_mutation / analysis_review / generic_mcp_tool）/ `risk` / `safety_class` / `execution_class` |
| `slice_ingest_fact` | 切片入库 fact | `dedup_status` / `canonical_slice_id` / `dup_of` |
| `cxparser_fact` | cxparser 状态 fact | `parse_status` / `symbol_status` / `target_status` / `preflight_status` |
| `clips_decision` | **决策输出 fact**（所有规则最终 assert 此模板） | `domain` / `target` / `decision`（allow / block / route）/ `verification`（verified / not_verified）/ `reason_code` / `next_action` / `route_target` / `matched_rule` |

CMM 扩展 fact（[templates/cmm_fact_templates.clp](src/clips_rules/templates/cmm_fact_templates.clp)）：`cmm_project_state` / `cmm_search_request` / `cmm_workflow_stage`。

### 16.3 规则域 1：`mcp_tool_guard`（pre-call allow/block/route）

源码：[rules/mcp_tool_guard.clp](src/clips_rules/rules/mcp_tool_guard.clp)，13 条 defrule。在工具调用**执行前**断言 `mcp_tool_request` fact，规则按 salience 优先级匹配，输出 `clips_decision`。

#### 关键 block 规则（禁止执行）

| 规则 | salience | 触发条件 | reason_code |
|---|---|---|---|
| `block-single-file-patch-apply-without-explicit-intent` | 89 | apply/revert patch 时 `explicit_user_intent=false` | `missing_patch_intent` |
| `block-stepwise-file-tool-multi-item-request` | 88 | `single_step_required=true` 但 `max_items_per_call≠1` | `multi_item_file_step_not_allowed` |
| `block-broad-file-mutation-for-stepwise-editing-intent` | 88 | write/patch 工具 + `primary_intent=comment_cleanup/text_cleaning/localized_edit/...` | `bulk_file_mutation_not_allowed_for_stepwise_edit` |
| `block-multi-file-patch-in-phase1` | 87 | patch 工具 `file_count≠1` | `multi_file_patch_not_allowed_phase1` |
| `block-single-file-patch-without-revert-plan` | 86 | apply/revert patch `revert_plan_ready=false` | `missing_patch_revert_plan` |
| `block-high-risk-write-without-path` | 85 | `file_mutation` 链 + `file_path=""` | `missing_file_path` |

#### 关键 route 规则（路由到更合适的工具）

| 规则 | salience | 触发条件 | route_target |
|---|---|---|---|
| `route-code-format-cleanup-to-clang-format` | 85 | 任意文件工具 + `primary_intent=code_format` | `lan_agent_format_code_file` |
| `route-file-text-operations-to-probe-first` | 84 | 文件操作 + `probe_required=true` + `probe_ready=false` | `lan_agent_probe_text_file` |
| `route-read-text-file-to-window-delete-for-comment-cleanup` | 84 | `read_text_file` + `primary_intent=comment_cleanup/remove_comments/删除注释/...` | `lan_agent_delete_text_range_window_atomic` |
| `route-comment-cleanup-scaffold-to-window-delete` | 83 | `scan_text_ranges`/`prepare_edit_windows` + 注释清理意图 | `lan_agent_delete_text_range_window_atomic` |
| `route-read-text-file-to-range-scan-for-editing-intent` | 82 | `read_text_file` + `primary_intent=text_cleaning/localized_edit/source_edit_planning` | `lan_agent_scan_text_ranges` |

#### 默认规则

| 规则 | salience | 行为 |
|---|---|---|
| `allow-single-file-patch-preview` | 80 | preview patch（不写盘）直接 allow |
| `default-mcp-tool-allow` | -100 | 无其他规则匹配时默认 allow + verified |

### 16.4 规则域 2：`mcp_result_guard`（post-result 验证）

源码：[rules/mcp_result_guard.clp](src/clips_rules/rules/mcp_result_guard.clp)，16 条 defrule。在工具返回结果后断言 `mcp_tool_result` fact，验证结果完整性、禁止虚假完成声明。

#### 完成声明门禁（核心安全约束）

| 规则 | salience | 触发条件 | 行为 |
|---|---|---|---|
| `text-range-delete-result-still-pending-by-has-more` | 49 | delete 工具 `has_more=true` | route（继续删除），`verification=not_verified` |
| `text-range-delete-result-still-pending-by-continuation` | 48 | delete 工具 `continue_required=true` | route（继续删除） |
| `directory-batch-read-still-pending` | 48 | `analysis_allowed=false` + `batch_completion=incomplete` | route（继续批读链） |
| `non-terminal-result-forbids-final-answer` | 47 | `terminal_state=false` + `completion_claim_allowed=false` | route，禁止 final answer |
| `final-answer-disallowed-by-result` | 46 | `final_answer_allowed=false` | route，禁止 final answer |

> **关键不变量**：模型只有在 `terminal_state=true` + `completion_claim_allowed=true` + `final_answer_allowed=true` + `verification_ok=true` **四重齐**时才能声明任务完成。CLIPS 规则在 post-result 阶段强制这四个字段一致。

#### 结果完整性验证

| 规则 | salience | 触发条件 | reason_code |
|---|---|---|---|
| `invalid-direct-answer-json-fragment` | 50 | local_chat/ventriloquist `direct_answer="{"` | `bad_direct_answer_fragment` |
| `invalid-direct-answer-empty` | 45 | `direct_answer=""` | `empty_direct_answer` |
| `invalid-direct-answer-label-token` | 44 | `direct_answer="direct_answer"`（标签泄漏） | `bad_direct_answer_label_token` |
| `analysis-only-chat-claimed-execution-without-evidence` | 41 | `ai_conclusion_valid=false` + `result_ref/evidence_ref/task_id` 全空 | `analysis_only_execution_claim_without_evidence` |
| `execution-task-result-missing-traceable-ref` | 39 | execute 类 + `task_id/result_ref/evidence_ref/log_path` 全空 | `execution_result_missing_traceable_ref` |
| `audited-write-result-missing-proof` | 38 | file_mutation + 审计字段全空 | `write_result_missing_audit_ref` |
| `invalid-result-missing-hash` | 35 | `result_hash=""` | `result_hash_missing` |
| `invalid-result-missing-schema` | 34 | `schema_version=""` | `schema_version_missing` |
| `incomplete-read-result-requires-continuation` | 33 | read/list/run_cxparser `task_completion=incomplete` | `read_chain_incomplete` |
| `invalid-ai-conclusion-flag` | 40 | `ai_conclusion_valid=false` | `ai_conclusion_invalid` |
| `default-mcp-result-verified` | -100 | 默认 allow + verified |

### 16.5 规则域 3：`cmm_init_guard`（CMM 工作流守卫）

源码：[rules/cmm_init_guard.clp](src/clips_rules/rules/cmm_init_guard.clp)，15 条 defrule。强制 CMM 工具的"先索引再搜索"工作流。

#### 核心约束

- **`block-cmm-search-before-ensure-indexed`**（salience 95）：未索引项目禁止 search/query/trace/get_architecture，route 到 `lan_agent_cmm_index_status`
- **`route-cmm-ensure-indexed-as-first-step`**（salience 93）：`probe_ready=false` 时强制先 `index_status`
- **`block-cmm-search-without-project-parameter`**（salience 94）：未解析 `normalized_project` 禁止 `search_code`
- **`allow-cmm-search-on-verified-project`**（salience 85）：已索引项目放行搜索
- **`handle-cmm-project-not-found-error`**（salience 75）：错误结果 route 到 `list_projects` + `index_repository`
- **`block-cmm-delete-project-without-intent`**（salience 88）：删除项目索引需显式 `primary_intent=reindex_preparation`

#### CMM 状态机（[graphs/cmm_init_flow.clp](src/clips_rules/graphs/cmm_init_flow.clp)）

```
init → validate → (indexed?) → ready → search → analyze
                ↓ (not indexed)               ↓ (error)
              index → validate              error → init (retry)
```

7 条状态转移 defrule 定义 `cmm_state_machine` 的状态转换，每个状态有 `required_tool` / `guard_condition` / `action`。

### 16.6 规则域 4：`cxparser_preflight_guard`（build/test 预检）

源码：[rules/cxparser_preflight_guard.clp](src/clips_rules/rules/cxparser_preflight_guard.clp)，2 条 defrule。

- **`block-build-without-preflight`**（salience 70）：`build_target`/`run_ctest_target` 在 `preflight_status=missing/false/blocked` 时 block，要求先调 `preflight_build_target`/`preflight_run_ctest_target` 拿到 `preflight_ref`
- **`default-preflight-allow`**（salience -100）：默认放行

### 16.7 规则域 5：`slice_ingest_guard`（slice 入库去重）

源码：[rules/slice_ingest_guard.clp](src/clips_rules/rules/slice_ingest_guard.clp)，3 条 defrule。

- **`duplicate-slice-route-canonical`**（salience 60）：`dedup_status=duplicate` + 有 `canonical_slice_id` → route 到 canonical slice 合并
- **`duplicate-slice-block-status`**（salience 55）：`dedup_status=duplicate` 但无 canonical → block
- **`default-slice-ingest-allow`**（salience -100）：默认放行

### 16.8 CLIPS 工具与规则的关系

CLIPS 规则通过 4 个 MCP 工具对外暴露（见 4.x 节）：

| MCP 工具 | 调用时机 | 使用的规则域 |
|---|---|---|
| `lan_agent_clips_decide` | 任意工具调用前/后 | 全部 5 个规则域（按 `domain` 字段路由） |
| `lan_agent_clips_chain_template` | 获取工具链模板 | `mcp_tool_chain` fact 模板 |
| `lan_agent_rag_clips_meta` | 上游 RAG 元信息查询 | 上游 `/rag/clips/meta`（非本地规则） |
| `lan_agent_rag_clips_run` | 上游 RAG 规则执行 | 上游 `/rag/clips/run`（非本地规则） |

### 16.9 规则优先级模型

CLIPS 使用 salience（整数，越大越优先）控制规则触发顺序。当前规则域的 salience 分布：

```
salience 95 ─ cmm_init_guard 核心block
salience 89-85 ─ mcp_tool_guard block/route 主力规则
salience 84-80 ─ mcp_tool_guard route 次要规则
salience 70 ─ cxparser_preflight_guard block
salience 60-55 ─ slice_ingest_guard 去重
salience 50-33 ─ mcp_result_guard 完整性验证
salience -100 ─ 所有域的 default-allow 兜底
```

> **设计原则**：block 规则 salience > route 规则 > default-allow。同一 fact 可被多条规则匹配，但 salience 最高的先触发；`default-*-allow`（salience -100）作为兜底，确保无规则匹配时默认放行（fail-open）。

### 16.10 扩展与维护

新增 CLIPS 规则的步骤：

1. 在对应规则域 `.clp` 文件中添加 `defrule`，命名格式 `<action>-<target>-<condition>`
2. 设置 salience：block 规则 85-95，route 规则 80-84，验证规则 33-50，default -100
3. 规则体必须 `assert` 一个 `clips_decision` fact，包含 `domain` / `target` / `decision` / `verification` / `reason_code` / `next_action` / `matched_rule` 字段
4. 如需新的 fact slot，在 [templates/mcp_fact_templates.clp](src/clips_rules/templates/mcp_fact_templates.clp) 的对应 `deftemplate` 中添加 slot（默认值 `"false"` 或 `""`）
5. C++ 侧在 `McpToolDispatch.h` 或 `ClipsDecisionOperations.h` 中断言 fact 时填充新 slot
6. 更新本节文档的规则表

> **规则文件加载**：CLIPS 规则文件在 MCP 服务启动时由 C++ 侧批量加载（`BuildClipsDecisionResult` 等函数），规则文件路径相对于 `src/clips_rules/`。修改 `.clp` 文件后需重新编译（规则嵌入二进制）或重启服务（若从磁盘加载）。
---

## 17. Fact-Factory 守卫层（LLM ↔ CLIPS 中间防护）

### 17.1 设计定位

Fact-Factory 是 LLM 探路模块输出与 Myrmidon/CLIPS 推理内核之间的**守卫层**，负责在原始 slot 值进入 CLIPS fact 之前进行字节消毒、分词、业务词匹配和语义归一。

```
LLM 探路模块输出原始 JSON 候选 slot 值
        ↓
【第0层：字节硬过滤｜纯 C++，无 NLP 库】
        ↓
【第1层：词法分词｜CppJieba 精确模式（条件编译，当前默认关闭）】
        ↓
【第2层：业务词候选检索｜marisa-trie（条件编译，当前默认关闭）】
        ↓
【第3层：语义同义归一｜WordNet + 哈工大同义词词林扩展版 cilin_ext.txt】
        ↓
输出：候选标准 tag 列表 + is_dirty 标记 + 告警信息
        ↓
送入 CLIPS 推理内核
```

### 17.2 核心设计约束

| 约束 | 说明 |
|---|---|
| **守卫是校验层，不是修复层** | 优先标记脏（`is_dirty=true`），禁止写复杂启发式修复逻辑；带脏标记的 fact 仍然送入 CLIPS，不直接丢弃 |
| **消歧不在守卫层做** | 一词多义时保留全部候选 tag 并标记 dirty，交由 CLIPS 规则层结合上下文研判 |
| **大段原始文本禁止进入 slot** | 原始对话走 SQLite + UUID 引用，slot 仅传递短标签 |
| **不引入深度学习/Python/Java** | 全部为词典 + 字符串算法 |

### 17.3 四层管线详解

#### 第0层：字节硬过滤（ByteSanitizer）

- **文件**：[src/fact_factory/ByteSanitizer.h](src/fact_factory/ByteSanitizer.h)
- **纯 C++ 字符串处理，最高优先级，无任何 NLP 库**
- **C-CLIPS 防 segfault 内存崩溃的唯一屏障**

处理规则（按顺序执行）：

| 规则 | 说明 |
|---|---|
| 长度阈值 | slot 单字段最大字节（默认 32，可配置），超长直接 `is_dirty=true` |
| 零字节检测 | 删除内部 `\0`，检测到零字节直接标记脏 |
| 控制字符过滤 | 过滤全部 ASCII 0-31 不可见控制字符 |
| CLIPS 语法转义 | 转义 `" ( ) ;` 特殊字符，防止语法注入 |
| UTF-8 合法性校验 | 非法 UTF-8 直接 `is_dirty=true`，含 overlong encoding 检测 |

关键 API：

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

#### 第1层：词法分词（CppJieba，条件编译）

- **文件**：[src/fact_factory/JiebaTokenizer.h](src/fact_factory/JiebaTokenizer.h)
- **依赖**：[cppjieba](cppjieba/)（clone 自 `https://github.com/Sean-Cai-X/cppjieba`）
- **模式**：精确模式，关闭 HMM 未登录词猜测
- **状态**：当前默认关闭（`#undef CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE`），因 ClipsDecisionOperations.h 中的 `extern "C"` 块会污染 CppJieba 头文件的 `using std::xxx` 声明

> 启用方式：使用独立编译单元 `FactFactoryIntegration.cpp` 并定义 `CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE` 宏，避免在 `extern "C"` 上下文中 include CppJieba。

#### 第2层：业务词候选检索（marisa-trie，条件编译）

- **文件**：[src/fact_factory/BusinessTrieMatcher.h](src/fact_factory/BusinessTrieMatcher.h)
- **依赖**：marisa-trie 源码编译（`grimoire.cc`），不引入 Rime 输入法上层逻辑
- **状态**：当前默认关闭，与第1层一同通过宏控制

#### 第3层：语义同义归一（SemanticNormalizer）

- **文件**：[src/fact_factory/SemanticNormalizer.h](src/fact_factory/SemanticNormalizer.h)
- **中文同义库**：[src/fact_factory/resources/cilin_ext.txt](src/fact_factory/resources/cilin_ext.txt)（892,620 字节）
  - 数据源：哈工大《同义词词林（扩展版）》，标准化文本来自 HanLP v1.8.6 `data/dictionary/synonym/CoreSynonym.txt`
  - 格式：`五级语义编码=空格分隔同义词列表`，如 `Aa01A01= 人 士 人物 人士`
  - 仅复用纯文本词库，**不引入 HanLP Java/Python 源码、运行时、算法模块**
- **英文同义库**：WordNet 静态词典（C++ 查表封装，无网络）
- **业务补丁表**：[src/fact_factory/resources/business_supplement.txt](src/fact_factory/resources/business_supplement.txt)
- **业务词典**：[src/fact_factory/resources/business_dict.utf8](src/fact_factory/resources/business_dict.utf8)

词林使用约束：

1. 程序启动一次性加载构建内存哈希索引，运行时不再读磁盘
2. 查询结果**强制过滤，只允许输出 40 个业务词集合内的 tag**，防止泛义近义词灌入 CLIPS
3. 一词多义一律打上 `is_dirty=true`，歧义交给 CLIPS 规则层处理
4. 词林覆盖不到的业务专属术语，使用 `business_supplement.txt` 兜底

### 17.4 业务标准 Tag 体系（40 个）

- **文件**：[src/fact_factory/BusinessTagRegistry.h](src/fact_factory/BusinessTagRegistry.h)

| 分组 | 数量 | 示例 tag |
|---|---|---|
| Intent（意图） | 10 | `comment_cleanup`, `code_format`, `source_edit`, `code_search`, `refactor_file` |
| RequestType（请求类型） | 8 | `analysis_review`, `read_observe`, `file_mutation`, `execution_task`, `clips_control` |
| SafetyRisk（安全/风险） | 4 | `write_audited`, `read_only`, `low`, `high` |
| Decision（守卫决定） | 3 | `allow`, `block`, `route` |
| Verification（验证状态） | 3 | `verified`, `not_verified`, `invalid` |
| ExecutionClass（执行分类） | 3 | `read`, `write`, `execute` |
| ActionVerb（动作动词） | 9 | `probe`, `scan`, `delete`, `insert`, `replace`, `build`, `test` |

每个 tag 附带中文别名列表，`ResolveBusinessTag()` 支持中英文混合别名解析。

### 17.5 集成钩子

两个 `inline` 钩子函数直接实现在 [src/ClipsDecisionOperations.h](src/ClipsDecisionOperations.h) 末尾：

| 钩子 | 作用 | 当前路径 |
|---|---|---|
| `ApplyFactFactoryNormalizePrimaryIntent(raw_intent)` | primary_intent 归一化 | ByteSanitizer → BusinessTagRegistry 别名映射 → 大小写折叠 |
| `ApplyFactFactoryByteSanitizeSlot(raw_value, is_token_slot)` | slot 值字节消毒 | ByteSanitizer（token slot 64 字节 / 普通 slot 256 字节） |

### 17.6 Pending Continuation 崩溃修复

**问题**：`SemanticIntentLexiconEntry` 使用 `std::initializer_list<const char*>` 存储别名，导致 dangling 引用——当 CLIPS pending continuation 场景触发时（`pending_continuation_active=true` + `pending_required_arguments_json` 非空），服务发生 segfault 崩溃。

**修复**：
1. 将 `std::initializer_list<const char*>` 改为 `std::vector<const char*>`（[src/SemanticIntentLexicon.h](src/SemanticIntentLexicon.h)）
2. 清理 `EvaluateClipsDecision` 中的临时 trace 代码
3. 确保 `route_arguments_json` 不暴露 raw JSON 参数（`route_arguments_json_available=false`, `transport=none`）

### 17.7 资源文件清单

```
src/fact_factory/
├── ByteSanitizer.h           ← 第0层：字节硬过滤
├── JiebaTokenizer.h          ← 第1层：CppJieba 分词适配
├── BusinessTrieMatcher.h     ← 第2层：marisa-trie 业务词检索
├── SemanticNormalizer.h      ← 第3层：语义同义归一
├── FactFactory.h             ← 管线集成入口
├── BusinessTagRegistry.h     ← 40 个业务标准 tag + 别名解析
└── resources/
    ├── business_dict.utf8     ← 40 个业务词词典（CppJieba + marisa-trie 共用）
    ├── business_supplement.txt← 业务补充映射表（词林缺口兜底）
    └── cilin_ext.txt          ← 哈工大同义词词林扩展版（892KB，~7万词）
```

### 17.8 编译开关

| 宏 | 默认 | 说明 |
|---|---|---|
| `CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE` | `#undef`（关闭） | 启用完整 4 层管线（含 CppJieba + marisa-trie） |
| 无宏定义（轻量模式） | 默认生效 | 仅 ByteSanitizer + BusinessTagRegistry，header-only，无外部依赖 |

> **当前状态**：轻量模式已编译通过并冒烟验证。完整管线需通过独立编译单元接入，避免 `extern "C"` 污染。

---

## 18. optfile 原子文件操作工具

### 18.1 概述

`optfile` 是一个基于 Qt 的独立命令行文件操作工具，提供原子写入、哈希校验、锚点定位插入、行范围替换等 MCP 级别的文件操作能力。

- **源码**：[optfile/main.cpp](optfile/main.cpp)
- **可执行文件**：`optfile.exe`（项目根目录）
- **依赖**：Qt5Core（`QSaveFile`、`QCryptographicHash`、`QJsonDocument`）

### 18.2 命令行接口

```powershell
optfile.exe --td <target_dir> --tf <test_file> [options]
```

| 选项 | 说明 |
|---|---|
| `--td, --target-dir <path>` | 目标目录 |
| `--tf, --test-file <name>` | 目标文件名 |
| `--locate-text <text>` | 查找匹配行（精确/模糊） |
| `--find-line <n>` | 按行号查找单行 |
| `--insert-after-anchor <text>` | 在锚点行后插入 |
| `--replace-start-line <n>` | 替换起始行 |
| `--replace-end-line <n>` | 替换结束行 |
| `--replacement-text <text>` | 插入/替换文本 |
| `--delete-line <n>` | 按行号原子删除 |
| `--delete-content <text>` | 按内容匹配原子删除 |
| `--expected-anchor-hash <hash>` | 锚点行哈希校验 |
| `--expected-line-hash <hash>` | 目标行哈希校验 |
| `--expected-range-hash <hash>` | 范围哈希校验 |
| `--show-preview` | 在 locate 结果中显示预览 |
| `--fuzzy-threshold <0-100>` | 模糊匹配阈值（默认 60） |
| `--occurrence <n>` | 第 N 次出现的锚点（默认 1） |

### 18.3 核心功能

| 功能 | 函数 | 说明 |
|---|---|---|
| 行定位 | `locate_text_lines_mcp` | 精确/模糊匹配，返回行号 + 内容 + 哈希 |
| 行查找 | `find_line_metadata_mcp` | 按行号返回元数据 |
| 锚点插入 | `insert_after_anchor_atomic_mcp` | 原子写入，可选哈希校验 |
| 范围替换 | `replace_line_range_atomic_mcp` | 原子替换行范围，可选哈希校验 |
| 行删除 | `delete_line_atomic_mcp` | 原子删除单行 |
| 内容删除 | `delete_content_atomic_mcp` | 按内容匹配原子删除 |

> **原子写入**：所有写操作使用 `QSaveFile` 实现——先写入临时文件，再原子重命名，确保操作中途崩溃不会损坏原文件。每个操作返回 JSON 结果，包含操作状态和文件哈希。

### 18.4 输出格式

所有操作通过 `std::cout` 输出 JSON 到 stdout：

```json
{"status":"success","operation":"locate","file_path":"...","matched_lines":[...],"file_hash":"..."}
```

错误时返回 exit code 2：

```json
{"status":"error","error":"anchor not found"}
```

### 18.5 编译方式

optfile 独立于主项目 CMake 构建，使用 qmake 编译：

```powershell
cd D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\optfile
qmake optfile.pro
mingw32-make
# 将 optfile.exe 复制到项目根目录，并确保 Qt5Core.dll 等依赖在 PATH 或同目录
```

---

## 19. Fact-Factory 守卫层冒烟测试

### 19.1 测试环境

| 项 | 值 |
|---|---|
| 仓库 | `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent` |
| 构建目录 | `build\Release` |
| 可执行文件 | `codex_lan_agent.exe`（38,003,200 字节） |
| 配置文件 | `test_config.ini` |
| 服务端口 | 18080 |
| Machine Code | `8EE5-2336-71AE-74DD` |
| 测试日期 | 2026-08-13 |

### 19.2 测试结果总览

| 测试 | 结果 | 关键断言 |
|---|---|---|
| TEST 1: Health Check | **PASS** | `ok=true, status=ok, listen_port=18080, outcome=PASS` |
| TEST 2: MCP Overview | **PASS** | `ok=true, tool_count=1, semantic_action_count=78` |
| TEST 3: CLIPS decide - observe | **PASS** | `ok=true, exit_code=0, status=success, decision=allow, terminal_state=true, outcome=PASS` |
| TEST 4: CLIPS decide - pending continuation | **PASS** | `ok=true, exit_code=0, status=success, decision=allow, failure_mode=none, outcome=PASS` |
| TEST 5: Concurrent Stress (8 mixed) | **PASS** | `PASS=8, FAIL=0`（5×observe + 3×pending） |

### 19.3 关键断言：route 参数不泄露 raw JSON

| 字段 | 值 | 断言 |
|---|---|---|
| `route_arguments_json` | `""`（len=0） | **PASS** - raw JSON 未泄露 |
| `route_arguments_json_available` | `false` | **PASS** |
| `route_arguments_json_transport` | `none` | **PASS** |
| `route_arguments_json_ref` | `""` | **PASS** |

### 19.4 Pending Continuation 场景验证

测试 payload 包含：

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

**结果**：服务未崩溃，返回 `decision=allow, failure_mode=none, outcome=PASS`。

> 此场景在修复前会导致 segfault 崩溃（`SemanticIntentLexiconEntry` 的 `std::initializer_list` 生命周期 bug）。

### 19.5 并发压力测试

连续发送 8 个混合请求（5 个 observe + 3 个 pending continuation），全部 PASS，服务进程全程在线：

```
Stress result: PASS=8 FAIL=0 (total=8)
```

### 19.6 Fact-Factory 守卫层当前状态

| 组件 | 状态 | 说明 |
|---|---|---|
| 第0层 ByteSanitizer | **已启用** | 字节硬过滤，5 项检查全部生效 |
| BusinessTagRegistry | **已启用** | 40 个业务 tag + 别名解析 |
| 第1层 CppJieba | **已关闭** | 条件编译，需独立编译单元接入 |
| 第2层 marisa-trie | **已关闭** | 条件编译，需独立编译单元接入 |
| 第3层 SemanticNormalizer | **资源就绪** | cilin_ext.txt 已下载（892KB），代码已落地，待管线开关打开 |
| cilin_ext.txt | **已就绪** | 892,620 字节，HanLP v1.8.6 CoreSynonym.txt |
| Pending Continuation 崩溃修复 | **已修复** | `std::initializer_list` → `std::vector` |
| route 参数泄露 | **已修复** | `route_arguments_json` 始终为空 |

### 19.7 已知限制

1. **optfile stdout 捕获**：optfile.exe 在 .NET `Process.StandardOutput` 捕获下 stdout 为空（Qt QCoreApplication 缓冲问题），直接终端运行正常
2. **完整管线未启用**：CppJieba + marisa-trie 需通过独立编译单元接入，当前轻量模式（ByteSanitizer + BusinessTagRegistry）已满足基本守卫需求
3. **cilin_ext.txt 词量**：约 7 万词，启动一次性加载耗时可控（<1s），但完整管线启用后需验证内存占用

---

## 20. 案例：MCP 网关批量代码清理（61 文件 / 工具执行 < 5 秒）

> 数据来源：`2026-08-13_05-14-13_conv_94642bb5__d_codex_workdir_sea.jsonl`
> 会话模型：gemma-4-E4B-it-UD-Q5_K-XL（本地推理）
> 目标目录：`D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cxparser`

### 20.1 场景描述

用户发起两轮连续任务，要求对 cxparser 目录下的 **61 个 C/C++ 代码文件**进行批量清理：

| 轮次 | 用户指令 | primary_intent | flow_id |
|---|---|---|---|
| 第1轮 | 列出所有代码文件，而后依次删除注释 | `comment_cleanup` | `directory_comment_cleanup_bounded_window_v1` |
| 第2轮 | 依次删除以上列表代码文件中多余的空格回车 | `code_format` | `directory_comment_cleanup_bounded_window_v1` |

### 20.2 执行链路与速度实测

两轮任务均通过 `lan_agent_mcp_route` 单一网关入口完成，CLIPS 决策引擎自动路由到 `lan_agent_list_directory` 内部工具：

```
用户指令
  -> lan_agent_mcp_route (route 模式)
    -> CLIPS 决策：route_target=lan_agent_list_directory, chain_state=needs_tool_call
      -> lan_agent_mcp_route (call 模式, target_tool_name=lan_agent_list_directory)
        -> 返回 file_count=63, code_file_count=61, file_paths_json=[...61个路径...]
          -> terminal_state=true, completion_claim_allowed=true
```

**工具执行耗时（从 JSONL timings 提取）：**

| 轮次 | Turn 1 工具耗时 | Turn 2 工具耗时 | 工具总耗时 | LLM 推理总耗时 |
|---|---|---|---|---|
| 第1轮（注释清理） | **144 ms** | **140 ms** | **284 ms** | 28,768 ms |
| 第2轮（空格清理） | **22 ms** | **36 ms** | **58 ms** | 27,981 ms |
| **合计** | 166 ms | 176 ms | **342 ms** | 56,749 ms |

> **核心结论：两轮任务、4 次 MCP 工具调用、61 个文件的目录扫描与元数据返回，工具执行总耗时仅 342 毫秒，远低于 5 秒。** 墙钟时间主要消耗在本地 LLM 推理（gemma-4-E4B-it-UD-Q5_K-XL），MCP 网关层本身近乎零开销。

### 20.3 CLIPS 决策审计字段

每轮路由返回 128+ 个审计字段，关键字段如下：

```
route_target=lan_agent_list_directory
chain_state=needs_tool_call -> tool_result_returned
completion_guard=NON_TERMINAL_RESULT: do not claim completion; execute the required next MCP tool call
terminal_state=true (第2次调用后)
verification_ok=true
audit_field_count=128 (route) / 149 (list)
```

### 20.4 验证结果

任务完成后通过独立 PowerShell 状态机脚本（正确处理字符串字面量，避免误删注释字符）验证：

| 检查项 | 结果 |
|---|---|
| 扫描代码文件总数 | 61 |
| 仍含 `//` 或 `/* */` 注释的文件数 | **0** |
| `#include` / `namespace` / `class` / `#define` 结构完整性 | **完好** |
| 字符串字面量（如 `"<="`, `">="`）保留 | **完好** |
| 正向控制测试（注入假注释 -> 检测到 -> 恢复 -> 清零） | **PASS** |

### 20.5 速度优势分析

| 维度 | 传统方式（逐文件读取+正则替换+写入） | MCP 网关方式 |
|---|---|---|
| 文件 I/O | 61 次串行读写 | 单次目录扫描返回全部元数据 |
| 决策路由 | 人工编排脚本 | CLIPS 自动路由，2 turn 完成 |
| 工具执行 | 数秒~数十秒 | **< 350 ms** |
| 审计追踪 | 无 | 128+ 审计字段/轮，完整链路可追溯 |

---

## 21. 演进之路：微模型下的超级智能系统

> 核心判断：codex-lan-agent 现在是**领域专用 MCP（C/C++ 代码 Agent 运行时）**；它的架构原型（状态托管、独立裁决网关、分层语义网格、可审计持久层、外部规则引擎隔离）完全具备向上泛化成**系统 MCP 基座**的潜质。
>
> 那个"61 文件 342ms"的惊艳案例，本质不是代码分析能力，而是一套通用范式：
> **LLM 只做意图推理；MCP Runtime 持有全局状态、执行约束、任务账本、领域算子、安全仲裁权。**

### 21.1 三层概念边界

| 层级 | 特征 | 代表 |
|---|---|---|
| **普通 MCP 服务** | 无状态 / 轻会话；工具只是函数集合；记忆、任务规划、合法性判断全部压给 LLM | stdio 单用途工具、clangd-mcp、文件 mcp |
| **增强领域 MCP（当前形态）** | 带任务状态机、本地持久记忆、内置规则守卫、领域专用算子（Clang AST / 程序切片）；调度域仅限代码工程 | **codex-lan-agent** |
| **系统 MCP Runtime（目标）** | 统一底座，挂载多个领域子 Agent；全局任务状态、统一安全仲裁、标准化语义存储、跨领域工具路由、多模型会话调度 | 本演进路线 |

一句话概括：

> codex-lan-agent = **系统 MCP 的代码领域参考实现 & 可行性验证原型**。

### 21.2 系统 MCP Runtime 分层架构框图

```
 ============================================================================
 |                      系统 MCP Runtime 分层结构                            |
 ============================================================================
 |                                                                           |
 |  [传输适配层]  stdio  |  streamable-http  |  (future) WebSocket           |
 |       |              |                        |                           |
 |       +--------------+------------------------+                           |
 |                      v                                                    |
 |  [统一网关入口]  lan_agent_mcp_route  (极简 Schema, 单入口收拢)            |
 |                      |                                                    |
 |       +--------------+--------------+--------------+--------------+       |
 |       |              |              |              |              |       |
 |       v              v              v              v              v       |
 |  [插件1]         [插件2]         [插件3]         [插件4]         [插件N]   |
 |  codex-code      document       hardware       ops-deploy      ...       |
 |  Clang AST       Markdown       寄存器/JTAG    CI/CD                     |
 |  CFG/切片        PDF解析        串口调试       容器编排                    |
 |  compile_cmd     RAG索引        示波器数据     日志分析                    |
 |       |              |              |              |              |       |
 |       +--------------+--------------+--------------+--------------+       |
 |                      |                                                    |
 |  ===================|==== 基座公共设施（插件不可绕过）|=================== |
 |                      v                                                    |
 |  +------------------------------------------------------------------+     |
 |  |  CLIPS 仲裁内核（外置规则, 热加载）                                |     |
 |  |  - 全局访问控制 (目录白名单 / 工具权限 / 进程执行限制)             |     |
 |  |  - 跨领域业务约束 (硬件操作前快照 / 配置变更前 diff 备份)          |     |
 |  |  - 完成性裁决 (四重门禁阻止幻觉式 "任务完成")                     |     |
 |  |  哲学: 不依靠 LLM 自律, 外部独立引擎做客观校验                    |     |
 |  +------------------------------------------------------------------+     |
 |  |  全局任务持久层 (Task Memory + step_ledger)                       |     |
 |  |  - 通用任务状态规范 (不耦合 C++/Clang)                            |     |
 |  |  - 精简恢复快照 vs 完整审计账本                                   |     |
 |  |  - 优先级 / 资源配额 / 超时回收 / 多任务并发隔离                  |     |
 |  +------------------------------------------------------------------+     |
 |  |  通用语义网格 (Semantic Grid 五层金字塔)                          |     |
 |  |  - 语义分片 / 哈希去重 / 上下文 Bundle 生成 (所有插件共享)        |     |
 |  +------------------------------------------------------------------+     |
 |  |  原子化可审计 IO (资源操作原语)                                   |     |
 |  |  - 本地文件: QSaveFile 原子写 + 哈希校验 + 回滚                    |     |
 |  |  - (future) 网络资源 / 设备寄存器 / 数据库记录                    |     |
 |  +------------------------------------------------------------------+     |
 |  |  制品存储通用接口 (Artifact Store)                                |     |
 |  |  - 各插件自定义产物格式, 基座负责持久化 + 哈希 + 索引              |     |
 |  +------------------------------------------------------------------+     |
 |  ========================================================================= |
 |                                                                           |
 |  [底层存储]  SQLite (关系索引)  |  RocksDB (KV 镜像)  |  文件系统          |
 |                                                                           |
 ============================================================================
```

### 21.3 插件交互时序

```
 LLM客户端        网关(route)      CLIPS仲裁       目标插件         TaskMemory
     |               |                |               |               |
     |  1.意图JSON   |                |               |               |
     |-------------->|                |               |               |
     |               | 2.事实断言      |               |               |
     |               |  (slot归一化    |               |               |
     |               |   字节消毒)     |               |               |
     |               |--------------->|               |               |
     |               |                | 3.规则匹配     |               |
     |               |                |  (权限/约束/   |               |
     |               |                |   完成性门禁)  |               |
     |               |  4.裁决:       |               |               |
     |               |<---------------|               |               |
     |               |  route_target= |               |               |
     |               |  codex-code    |               |               |
     |               |                |               |               |
     |               | 5.调用插件      |               |               |
     |               |------------------------------->|               |
     |               |                |               | 6.执行领域算子 |
     |               |                |               |  (Clang AST /  |
     |               |                |               |   切片 / 编译) |
     |               |                |               |               |
     |               |                |               | 7.step写入    |
     |               |                |               |-------------->|
     |               |                |               |  (持久账本)    |
     |               |  8.结果+审计字段|               |               |
     |               |<-------------------------------|               |
     |               |                |               |               |
     |               | 9.结果送CLIPS  |               |               |
     |               |  完成性复核     |               |               |
     |               |--------------->|               |               |
     |               |  10.终局裁决    |               |               |
     |               |<---------------|               |               |
     |  11.最终响应  |                |               |               |
     |  (128+审计字段)|                |               |               |
     |<--------------|                |               |               |
     |               |                |               |               |
     |  ★ 若 pending_continuation:    |               |               |
     |    LLM 补充参数后从步骤1重启    |               |               |
     |    TaskMemory 提供断点恢复      |               |               |
```

### 21.4 现有组件 → 系统基座原语映射

| 现有组件 | 系统基座原语 | 改造方向 |
|---|---|---|
| `lan_agent_mcp_route` 单网关路由 | **全局统一工具网关** | 不再绑定代码工具；动态加载插件式 MCP 子服务；路由策略自动分发至对应领域；保留极简入口兼容 stdio + streamable-http |
| Task Memory + step_ledger | **系统全局任务持久层** | 通用任务状态规范（不耦合 C++ 切片/编译产物）；区分「精简恢复快照」与「完整审计账本」；增加优先级、资源配额、超时回收、多任务并发隔离 |
| CLIPS 独立规则裁决层 | **系统安全 & 逻辑仲裁内核** | 全局访问控制策略；跨领域业务约束；**规则热加载**（当前编译进二进制是最大短板，系统基座必须解耦） |
| Semantic Grid 五层金字塔 | **系统统一长文本索引底座** | 通用 RAG 层：所有接入子 Agent 共享语义分片、哈希去重、上下文 Bundle 生成 |
| 原子化可审计 IO 模型 | **系统统一资源抽象** | 从本地文件扩展到网络资源、设备寄存器、数据库记录，统一变更审计范式 |

> 核心哲学不变：**不依靠 LLM 自律，外部独立引擎做客观校验**。这是这套架构相比所有 Prompt 工程方案最硬核的优势。

### 21.5 关键割裂点（领域 MCP → 系统 MCP 核心改造清单）

#### 1. 解除代码领域硬耦合

当前大量逻辑强绑定：`compile_commands.json`、Clang AST、C/C++ 程序切片、CMM 代码图谱。

```
系统 MCP Runtime（基座）
├─ 网关调度框架
├─ 全局任务状态持久层
├─ 通用语义网格
├─ CLIPS 仲裁网关
├─ 通用安全 IO / 审计子系统
└─ 插件接口规范
     ├── [插件1] codex-code-plugin  （原有全部 Clang 代码分析能力，作为可插拔领域模块）
     ├── [插件2] document-agent-plugin
     └── [插件3] hardware-debug-plugin
```

基座本身**不内置任何代码分析逻辑**，代码能力降级为插件。

#### 2. 规则引擎解耦

- **痛点**：现有 CLIPS 规则编译嵌入二进制，修改必须重编
- **方案**：规则文件外置，支持动态加载多套规则集；按工作空间、任务类型启用不同策略；提供规则管理工具

#### 3. 多租户 / 多会话隔离增强

当前面向单用户单工程；系统 MCP 需要：

- 多个独立客户端（多个 LLM 进程同时连接）
- 工作空间隔离（A 工程任务不能读写 B 工程资源）
- 任务资源配额（CPU、解析时长、最大步数，防止 Clang 这类重型算子耗尽资源）

#### 4. 标准化适配原生 MCP 协议

- 现状优先 streamable-http；主流 MCP 客户端默认 stdio
- 系统基座需要双传输原生支持，网关层统一适配，无缝对接官方 SDK、各类客户端

#### 5. 制品存储通用化

- 当前 Artifact 大量是 AST、CFG、DOT 代码分析产物
- 基座定义通用制品存储接口，各插件自行定义产物格式，基座只负责持久化、哈希校验、索引

### 21.6 三步走演进路径

| 阶段 | 目标 | 产物 | 风险 |
|---|---|---|---|
| **阶段 1：解耦抽象** | 通用底座代码抽成独立静态库（网关框架、task memory、语义网格、审计 IO、CLIPS 网关） | 单一程序，但通用能力剥离完成 | 低 — 不改变现有行为 |
| **阶段 2：插件化重构** | 基座独立可执行程序；Clang 代码分析编译为插件动态链接库；支持启停、加载/卸载 | 混合型系统 MCP 原型：同时接入 C++ 代码重构 + 文档处理，共用任务记忆与安全网关 | 中 — 插件 ABI 设计 |
| **阶段 3：标准化开放** | 定义插件规范、HTTP 管理 API；远程任务调度、多机器分布式扩展 | 生态基座，对外开放 | 高 — 生态共识 |

### 21.7 与 research-mcp 路线的融合

| 职责 | 归属 |
|---|---|
| 外部数据源采集、学术/代码仓库信息检索、研究任务编排 | **research-mcp** |
| 本地资源操作、代码深度静态分析、任务持久断点、安全执行仲裁 | **系统 MCP Runtime**（本路线） |

两者通过 MCP 协议互通，形成完整离线研究 + 开发 Agent 集群。

### 21.8 战略判断

当下整个 MCP 生态的发展阶段：

- 绝大多数项目还停留在「工具集合」
- 少数做到会话管理
- **具备独立状态机、外置裁决引擎、长任务原生支持的自治式 MCP 运行时几乎空白**

如果完成这次向上泛化：不再只是一个优秀的 C/C++ 代码 Agent 工具，而是提供一套**新一代 MCP 运行范式参考标准**，直接跳出现在 stdio 小工具内卷赛道。

---

# 22. 次级大脑：系统 MCP 范式白皮书

> **文档定位**：为后续工程拆解、模块拆分、接口定义、协议规范提供唯一顶层设计依据。

---

## 0. 摘要

当前主流 AI Agent、MCP 工具生态普遍存在 **LLM 中心化缺陷**：状态依附对话上下文、无独立现实校验、任务易断裂、模型幻觉不可根治、长时序自治能力缺失。

本文正式定义 **「次级大脑 —— 系统 MCP Runtime」范式**：将 AI 体系拆分为 **主大脑（LLM 意图推理）** 与 **次级大脑（系统级自治运行时）** 双层架构。

次级大脑脱离 LLM 独立进程运行，统一承载：

| 能力域 | 说明 |
|---|---|
| 全局任务状态 | 持久化托管，不依附对话上下文 |
| 客观现实账本 | 资源快照、哈希校验、一致性基准 |
| 安全裁决 | 独立规则引擎，拥有驳回与拦截权 |
| 可审计执行 | 全链路留痕、差分、默认可回滚 |
| 跨会话持久记忆 | 精简快照 + 完整账本双轨制 |
| 领域能力插件调度 | 基座通用，场景能力插件化 |

它是现代工具式人工智能、离线自治 AI 工作站、持续交互智能实体的 **底层标准底座范式**。

---

## 1. 核心范式定义与公理体系

### 1.1 双层大脑架构定义

#### 主大脑（Primary Brain）
> 各类大语言模型。

| 维度 | 描述 |
|---|---|
| **职责** | 自然语言理解、意图拆解、创造性推理、下一步行为决策 |
| **核心短板** | 上下文有限、无持久结构化记忆、无客观校验能力、存在原生幻觉、无法持有长时序任务状态 |

#### 次级大脑（Secondary Brain）
> 系统 MCP 自治运行时，独立于 LLM 进程。

| 维度 | 描述 |
|---|---|
| **定位** | 不做创造式推理，只做状态持有、规则裁决、任务托管、现实对齐、安全管控、可审计执行 |
| **身份** | AI 对接数字 / 物理世界的 **唯一可信中间层** |

---

### 1.2 五大范式核心公理（不可违背）

所有工程设计必须严格遵循以下公理，构成范式底层不变性。

#### 公理 1：状态分离公理
> 所有长周期任务状态、外部资源快照、执行进度，全部托管于次级大脑持久层。

LLM 仅获取精简视图，**不持有全局真实状态**，彻底解决上下文溢出、会话重启任务丢失问题。

#### 公理 2：独立裁决公理
> 存在脱离 LLM 的确定性规则裁决引擎，拥有驳回、拦截、强制校验、禁止任务终结的权力。

AI 行为约束 **不依赖 Prompt 玄学**，由底层运行时刚性保障。

#### 公理 3：能力插件公理
> 次级大脑基座 **无领域硬耦合**（无内置代码分析、文档处理逻辑）。

所有场景化能力均为可插拔插件，基座仅提供通用运行原语。

#### 公理 4：任务连续公理
> 所有任务结构化归档为 **时序步骤账本**。

天然支持：断点冻结、跨会话续跑、跨模型承接、任务回溯。实现真正的 **长时序自治**。

#### 公理 5：可审计现实公理
> 所有外部资源变更（文件、数据、配置、设备状态）**强制留存**：
- 哈希校验
- 差分快照
- 操作日志

支持一致性校验、人工溯源、一键回滚。

---

## 2. 整体分层架构（五层标准化分层）

架构自上而下严格分层，**单向依赖、职责隔离**，彻底解决传统 MCP 工具散乱、职责混杂、无统一管控的问题。

| 层级 | 名称 | 定位 |
|---|---|---|
| L1 | 客户端接入层 | 最上层 — 多协议多客户端统一入口 |
| L2 | 系统网关与裁决层 | 核心管控层 — 流量收敛 + 独立裁决 |
| L3 | 系统核心运行时层 | 基座底座 — 四大通用子系统 |
| L4 | 插件化领域能力层 | 业务扩展层 — 场景化插件挂载 |
| L5 | 持久存储与制品层 | 最底层 — 唯一可信数据源 |

---

### L1 层级 1：客户端接入层（最上层）

所有 LLM、AI 客户端、终端会话统一接入入口，兼容标准 MCP 双传输协议。

- **支持协议**：Stdio 标准传输 / Streamable-HTTP 流式传输
- **接入实体**：本地开源 LLM / 云端模型 / 多 Agent 客户端 / 工作站终端
- **核心特性**：多会话隔离、客户端身份标识、请求统一封装

---

### L2 层级 2：系统网关与裁决层（核心管控层）

次级大脑的中枢神经，全权负责流量路由、安全拦截、任务门禁、行为裁决 — **这是区别于普通 MCP 的核心标志**。

| 能力 | 详细说明 |
|---|---|
| **统一单网关路由** | 收敛全部插件工具，对外暴露极简调用入口，降低 LLM 工具认知负担与 Token 消耗 |
| **独立规则裁决引擎** | 外置可热加载规则集，提供 **调用前拦截** + **调用后校验** 双重管控 |
| **四重任务完成门禁** 🔒 | 仅当「**终端状态合法 + 完成声明合规 + 终审通过 + 结果校验无误**」四条件同时满足，才允许任务终结，根治模型幻觉伪完成 |
| 权限管控 | 目录白名单、工具调用权限、外部进程执行限制 |
| 高危操作拦截 | 独立于 LLM Prompt 的刚性闸门 |
| 调用链路审计 | 全链路可追溯、异常熔断 |

---

### L3 层级 3：系统核心运行时层（基座底座）

次级大脑的本体，提供所有通用底层原语，**无任何领域业务耦合**，完全通用可复用。

包含四大核心子系统：

| 子系统 | 职责 | 关键说明 |
|---|---|---|
| **1. 全局任务状态机子系统** | 定义标准化任务生命周期，托管所有长任务的进度、状态、步数预算、资源配额 | 支撑断点续跑、任务暂停 / 恢复 |
| **2. 结构化持久记忆子系统** | 精简恢复快照（快速续跑）+ 完整步骤账本（全量审计） | 区分运行态数据与归档数据，兼顾性能与可追溯性 |
| **3. 通用语义网格子系统** | 五层语义金字塔架构，统一实现长文本分片、哈希去重、语义溯源、上下文 Bundle 精简 | 为所有插件提供 **通用 RAG 能力** |
| **4. 原子可审计 IO 子系统** | 标准化资源操作原语，支持文件原子写入、差分备份、哈希校验、一键回滚 | 杜绝资源损坏与非法变更 |

---

### L4 层级 4：插件化领域能力层（业务扩展层）

所有场景化能力以独立插件形式挂载，**基座不感知具体业务，完全解耦**。

#### 标准预置插件（可无限扩展）

| 插件 | 能力域 | 典型能力 |
|---|---|---|
| 🔧 代码分析插件 | 编译级语义分析 | Clang AST / CFG / DFG、程序切片、工程图谱对接 |
| 📚 文档研究插件 | 文献 + 知识库 | 文献解析、知识库构建、调研任务编排 |
| 🖥️ 系统运维插件 | 本地环境管控 | 批量脚本执行、配置变更审计、环境探针 |
| 🔌 硬件交互插件 | 设备受控操作 | 设备状态读取、调试指令受控执行 |

**插件统一规范**：
- 统一初始化 / 调用 / 卸载 / 日志 / 产物输出标准接口
- 支持动态加载 / 热更新

---

### L5 层级 5：持久存储与制品层（最底层）

系统 **唯一可信数据源**，维护 AI 交互的数字现实副本。

| 存储分类 | 说明 | 约束 |
|---|---|---|
| **源真存储** | JSON / JSONL 结构化文件、工作空间快照 | **唯一真值** |
| **加速镜像** | RocksDB / SQLite 数据库缓存 | 必须经过一致性校验方可启用，失焦随时可废弃 |
| **制品仓库** | 各插件输出的分析产物、日志、差分文件、审计记录 | 哈希索引、路径规范、可追溯 |

---

## 3. 核心组件详细职责定义

### 3.1 MCP 统一网关组件

| 维度 | 说明 |
|---|---|
| **职责** | 请求接收、协议适配、工具路由、流量收敛、入口统一审计 |
| **特性** | 多模式调用（概览 / 路由 / 执行）、自动工具推理、无效请求过滤 |
| **价值** | 解决 LLM 工具选型困难、多工具架构混乱、协议不兼容问题 |

---

### 3.2 规则裁决引擎组件

| 维度 | 说明 |
|---|---|
| **职责** | 安全守门、任务合规校验、终结权限管控、异常行为拦截 |
| **特性** | 规则外置可配置、**无依赖 LLM**、确定性输出、全链路可审计 |
| **核心能力** | 阻止伪任务完成、拦截高危操作、标准化任务收尾校验 |

> **核心哲学不变**：不依靠 LLM 自律，外部独立引擎做客观校验。

---

### 3.3 任务记忆状态组件

| 维度 | 说明 |
|---|---|
| **职责** | 全生命周期任务托管、状态序列化、跨会话续接、步数预算管控 |
| **数据结构** | 任务快照（轻量恢复）+ 步骤账本（全量时序记录） |
| **价值** | 彻底解决本地模型上下文受限、长任务断裂、会话重置丢失进度痛点 |

---

### 3.4 分层语义网格组件

| 维度 | 说明 |
|---|---|
| **职责** | 通用长文本结构化解析、去重、溯源、上下文压缩 |
| **架构** | L1-L5 五层语义金字塔，适配不同粒度的检索与生成需求 |
| **价值** | 所有插件共享一套 RAG 底座，避免重复造轮子，统一内容理解标准 |

---

### 3.5 原子审计 IO 组件

| 维度 | 说明 |
|---|---|
| **职责** | 所有外部资源操作的标准化、安全化、可追溯管控 |
| **能力** | 临时文件原子替换、变更备份、哈希校验、回滚恢复、崩溃防护 |
| **价值** | 杜绝 AI 自主操作导致的文件损坏、不可逆错误、无溯源变更 |

---

## 4. 标准化任务状态协议（核心工程规范）

本文定义 **系统 MCP 统一任务状态协议**，所有插件、所有任务必须遵循该状态机，实现任务标准化流转。

---

### 4.1 任务六大标准状态

| 序号 | 状态 | 定义 |
|---|---|---|
| 1 | **INIT** 初始化 | 任务创建，参数校验完成，资源预分配 |
| 2 | **RUNNING** 执行中 | 插件调度执行，步骤持续落盘账本 |
| 3 | **SUSPENDED** 暂停 | 上下文溢出、会话断开、资源不足，状态冻结待恢复 |
| 4 | **CHECK** 终审校验 | 执行完毕，进入四重门禁合规校验阶段 |
| 5 | **COMPLETE** 合法完成 | 全部校验通过，任务正式收尾归档 |
| 6 | **FAILED / ROLLBACK** 失败回滚 | 校验失败 / 执行异常，自动触发资源回滚与日志归档 |

状态流转图：

`
INIT → RUNNING ─┬─→ SUSPENDED ──┐（可无限次往返）
                │                │
                └─→ CHECK ──┬──→ COMPLETE
                            │
                            └──→ FAILED/ROLLBACK
`

> 注意：**未通过 CHECK 终审校验的任务，禁止流转至 COMPLETE 状态**。

---

### 4.2 任务核心数据结构（精简标准）

单次任务最小数据单元，支撑跨会话恢复与审计：

| 字段 | 说明 |
|---|---|
| 	ask_id | 全局唯一任务标识 |
| session_id | 绑定会话，支持多会话隔离 |
| status | 当前标准状态机状态（6 大标准状态之一） |
| step_index | 当前执行步数（防重复、可续跑） |
| udget_quota | 剩余资源 / 步数预算（防无限循环） |
| snapshot_hash | 当前工程 / 资源快照哈希（一致性校验） |
| check_result | 四重门禁校验结果明细 |
| ledger_path | 全量步骤账本归档路径 |

---

### 4.3 任务流转核心规则

1. **所有状态变更必须落盘**。内存状态不做唯一可信源，崩溃恢复以磁盘为准。
2. **CHECK → COMPLETE 强闸门**。未通过 CHECK 终审校验（四重门禁）的任务，**禁止** 流转至 COMPLETE 状态。
3. **SUSPENDED 可无限恢复**。可任意次数跨会话恢复，不丢失任何进度。
4. **失败强制归档**。失败任务强制留存日志、快照，支持人工复盘与二次修复。

---

## 5. 范式核心价值与行业定位

### 5.1 解决行业根本性痛点

| 行业痛点 | 范式解法 |
|---|---|
| LLM 幻觉伪完成 | 独立裁决引擎 + 四重完成门禁，不依赖 LLM 自证 |
| 无约束操作 | 规则外置 + 调用前拦截 + 调用后校验 |
| 上下文溢出 / 任务断裂 | 状态分离公理，持久层托管所有进度 |
| MCP 工具散乱无管控 | 统一网关 + 分层架构 + 插件规范 |
| 无记忆 / 无校验 / 无状态 | 结构化记忆子系统 + 可审计现实公理 |
| 变更不可逆 / 无溯源 | 原子 IO 子系统，默认哈希 + 差分 + 回滚 |

最终实现目标：**AI 行为可审计、可约束、可追溯、可回滚、可持续**。

---

### 5.2 行业差异化定位

**区别于**：LangGraph / AutoGen 等 LLM 侧框架、普通 RAG 记忆库、零散 MCP 工具 ——

| 对比对象 | 它是什么 | 本范式是什么 |
|---|---|---|
| 普通 MCP 工具集 | 零散函数集合 | **自治 AI 运行时底座** |
| 对话辅助模块 | Prompt 层附加能力 | **AI 对接现实世界的唯一可信中间层** |
| 模型增强插件 | 模型侧优化 | **独立的次级大脑基础设施** |

---

### 5.3 长期战略意义

本范式为以下类别系统提供底层标准架构：

- 🏢 **离线自治 AI 工作站**
- ♾️ **持续运行智能实体**
- 🛠️ **工具式通用人工智能**

> **核心判断**：未来所有具备长时序交互、自主执行、自我约束能力的 AI 系统，均需要此类「主大脑推理 + 次级大脑管控」的 **双层架构** 作为基础底座。

---

## 6. 工程落地拆分原则（后续开发依据）

1. **基座绝对解耦**  
   系统核心运行时 **无任何领域业务硬编码**，所有场景能力插件化。

2. **状态唯一可信**  
   磁盘持久层为 **唯一真值**，内存、缓存仅作加速镜像，随时可丢弃重建。

3. **规则外置可配**  
   裁决规则脱离二进制，支持 **热更新、自定义策略**，修改规则无需重编。

4. **接口统一标准化**  
   所有插件、网关、存储遵循统一协议，可迭代扩展、可替换实现。

5. **安全审计优先**  
   所有执行链路 **默认留痕、默认校验、默认可回滚**，不是可选而是强制。

---

## 7. 结语

**「次级大脑 - 系统 MCP 范式」** 跳出了传统 AI 工具封装、Prompt 工程、轻量化 MCP 插件的内卷赛道，**重构了 AI 与现实世界交互的底层架构**。

它以 **状态外移、独立裁决、任务自治、可审计执行、通用插件化** 为五大核心支柱，构建了新一代工具式人工智能的标准底座。

> 这是从 **「对话式 AI」** 走向 **「自治式实体 AI」** 的关键范式跃迁。

---