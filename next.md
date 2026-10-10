# IRC ↔ lan-agent 融合 — Phase 1 完成状态 (v6)

> 硬约束（不变）：
> - ❌ 不能用 Python / 胶水进程
> - ✅ IRC bridge 必须写成 **C++**，合并到 lan-agent binary 里
> - ✅ 系统只跑两个进程：**UnrealIRCd + lan-agent**
> - ✅ 所有程序/依赖放 `/H/usr/aisrc/` 独立目录，只通过 PATH/LD_LIBRARY_PATH 生效
> - ❌ 禁止修改 /usr /lib /bin

---

## 0. 当前状态一览

| 阶段 | 状态 | 备注 |
|---|---|---|
| Phase 1A — UnrealIRCd JSON-RPC + WS 配置 | ✅ **已完成 + 已验证** | :8067 plain WS, :8600 RPC, :8697 TLS |
| Phase 1B — lan-agent cfg Linux 路径 + irc_* 节 | ✅ **已完成** | `irc_enabled=true`, 5 个 agent 路由齐全 |
| Phase 1C — IrcBridgeThread.h / .cpp 源码 | ✅ **已写 ~750 行** | WS RFC 6455 + IRC + MCP dispatch + 重连退避，零第三方库 |
| Phase 1D — CMakeLists.txt 挂 IrcBridgeThread.cpp | ✅ **已挂** | source 列表正确 |
| Phase 1E — main.cpp 挂 start/stop + MCP handler 注册 | ✅ **已挂** | 顶部 include + **main() 里显式 SetIrcMcpToolHandler**（绕过 DCE）+ serve 命令里 start/stop |
| Phase 1F — **rebuild 成功** | ✅ **已完成** | 12MB binary 链接过 |
| Phase 1G — **E2E @codex ping → #main 回复** | ✅ **完全通过** | bridge 收 PRIVMSG → MCP dispatch → 发结果到 channel |

**Phase 1 完成日期**：2026-10-10
**v6 更新日期**：2026-10-10（E2E 全链路打通）

---

## 1. 启动指令（日常运维）

### 1.1 一键启动（推荐）

```bash
bash /H/aisrc/lan-agent/start-agent.sh start
# 自动 lo up + UnrealIRCd 状态检查 + nohup 启 agent
```

### 1.2 手动启动（排查时用）

```bash
# ① 确保 loopback 是 UP 的（devuan 默认可能 down）
sudo ip link set lo up

# ② 启 UnrealIRCd
cd /H/aisrc/IRC/unrealircd && ./unrealircd start
# 验证：ss -tlnp | grep 8067 → 127.0.0.1:8067 LISTEN

# ③ 启 lan-agent serve
export PATH=/H/usr/aisrc/usr/bin:/usr/bin:/bin
export LD_LIBRARY_PATH=/H/usr/aisrc/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu
export AGENT_DIR=/H/aisrc/lan-agent
cd /H/aisrc/lan-agent/build
./codex_lan_agent serve \
  --config /H/aisrc/lan-agent/codex_lan_agent.cfg \
  --machine-code E651-9C30-F7F6-520F

# 预期 stderr 关键行：
# [irc-bridge] LoadIrcConfig(...) → irc_enabled=1 ws=127.0.0.1:8067
# [irc-bridge] MCP dispatch handler ✅ 已注册
# [irc-bridge] ✅ 已上线，监听 #main
```

### 1.3 停止

```bash
bash /H/aisrc/lan-agent/start-agent.sh stop
# 或手动：
pkill -f codex_lan_agent
cd /H/aisrc/IRC/unrealircd && ./unrealircd stop
```

### 1.4 构建（仅修改源码后）

```bash
export PATH=/H/usr/aisrc/usr/bin:/usr/bin:/bin
export LD_LIBRARY_PATH=/H/usr/aisrc/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu
cd /H/aisrc/lan-agent/build
cmake --build . --target codex_lan_agent -j$(nproc)
# 只 build codex_lan_agent target（optfile 等 helper 不影响主 binary）
```

### 1.5 E2E 验收

```bash
# 确保 agent 在跑，然后任何 IRC WS 客户端发：
# JOIN #main → PRIVMSG #main :@codex ping
# bridge 应该回复：
# [codex-agent] ⏳ @<nick> 已收到「ping」
# [codex-agent] ❌/✅ MCP 执行结果
```

---

## 2. 源码变更清单

| 文件 | 变更 | 行数 |
|---|---|---|
| `src/IrcBridgeThread.h` (新建) | WS RFC 6455 + IRC + 回调注册类型 | ~84 |
| `src/IrcBridgeThread.cpp` (新建) | WS client + IRC 协议 + MCP dispatch + 重连退避 + **WS frame_len 修复** + 主动 PING 保活 | ~820 |
| `src/main.cpp` | 顶部 include + **main() 函数里显式 SetIrcMcpToolHandler**（绕过 DCE 优化） + serve 命令里 start/stop | ~25 |
| `codex_lan_agent.cfg` | agent 路由 method 名修正：`lan_agent_local_chat` → `lan_agent_run_local_chat` | 5 |
| `CMakeLists.txt` | source 列表追加 `src/IrcBridgeThread.cpp` | 1 |
| `start-agent.sh` (新建) | start/stop/restart/status，自动 lo up + nohup + PID | ~60 |

---

## 3. 修复的 Bug 清单（Phase 1 全部 9 个坑）

### Bug 1（架构）：McpToolDispatch.h 被编进 main.cpp 匿名 namespace，symbol 不可见
- **现象**：链接错误 `undefined reference to TryHandleRegisteredMcpTool`
- **根因**：`#include "McpToolDispatch.h"` 在 main.cpp 匿名 namespace 内 → 所有函数变 local symbol
- **修复**：函数指针注册模式。main.cpp 定义 captureless lambda → 转成函数指针 → 调 `SetIrcMcpToolHandler()`

### Bug 2（架构）：g_irc_mcp_registrar 被 GCC DCE 砍掉
- **现象**：agent.log 里完全没有 `[irc-bridge] MCP dispatch handler ✅ 已注册`
- **根因**：匿名 namespace 内的静态注册器，如果 lambda 没被外部引用，整个 anonymous namespace 被 GCC DCE 优化掉
- **修复**：**不再依赖匿名 namespace 里的静态初始化器**。直接在 `main()` 函数开头显式调用 `SetIrcMcpToolHandler(captureless_lambda)`。lambda 定义在 main() 里（能访问同文件匿名 namespace 里的 TryHandleRegisteredMcpTool），作为局部变量传递给 SetIrcMcpToolHandler

### Bug 3（跨 TU 初始化）：g_mcp_dispatch 静态变量被覆盖
- **现象**：handler 注册了但主循环里变成 nullptr
- **根因**：`static McpToolDispatchFn g_mcp_dispatch = nullptr` 在 IrcBridgeThread.cpp 顶部（TU-scope 静态变量），main() 里的注册和 bridge thread 都引用它。跨 TU 初始化顺序不确定 → 被覆盖
- **修复**：Meyers singleton — `static McpToolDispatchFn & GetMcpDispatch() { static McpToolDispatchFn fn = nullptr; return fn; }`。函数内静态变量初始化顺序安全

### Bug 4（解析器）：LoadIrcConfig 用 atoi("true") = 0
- **现象**：bridge 日志 `irc_enabled=false，跳过启动`
- **根因**：bridge 自带的简易 cfg 解析器只认数字，不认 "true"/"false"
- **修复**：加 `ParseBoolOrInt()` 辅助函数

### Bug 5（协议层）：SendIrcLine 直接 send 裸 IRC 行，没包 WS frame
- **现象**：WS 连接成功 → 发 NICK/USER → 永远收不到 001 welcome
- **根因**：fd 是 WebSocket socket！UnrealIRCd WS 模块期望带 FIN/opcode/mask 的完整 WS frame
- **修复**：SendIrcLine 改用 `EncodeWsClientFrame(0x1, ...)` 包装

### Bug 6（系统）：loopback 接口 DOWN
- **现象**：ss 显示 127.0.0.1:8067 LISTEN，但 connect timeout
- **修复**：`sudo ip link set lo up`；start-agent.sh 自动处理

### Bug 7（WS 帧解析）：ReadTextFrame 里 frame_len 算错 → payload 残留被误当 header
- **现象**：recv() 收到 184 bytes，DecodeWsFrame 却报 opcode=0x03/0x02（reserved），实际是 payload 里的 ASCII 字节被当成了下一帧的 header
- **根因**：ReadTextFrame 原来写的是：
  ```cpp
  uint64_t plen = byte1 & 0x7F;  // = 126（extended length 标记）
  frame_len += plen;               // 加了 126 而不是真实 plen！
  ```
  当 UnrealIRCd 发 `opcode=0x01, masked=0, plen=260` 的 WS text frame 时，`byte1 & 0x7F = 126`，代码把 `plen = 126` 就开始算 frame_len 了，**没从 data[2..3] 读真实的 260**！导致 frame_len 算少了 → `buf.erase(frame_len)` 只删了前 130 字节，后 130 字节的 payload 残留在 buf 里，被误当成下一帧的 header！
- **修复**：先读 header_end（考虑 mask bit），再读真实 plen（从 data[2..3] 或 data[2..9]），最后算 frame_len = header_end + plen。DecodeWsFrame 里的 len 参数现在和真实 frame_len 一致了

### Bug 8（保活）：bridge 没主动 PING → UnrealIRCd 发 WS close frame
- **现象**：bridge JOIN #main 后 ~30 秒收到 WS close frame，重连循环
- **根因**：bridge 上线后没任何出向活动（只等消息），UnrealIRCd 觉得不活跃就 close
- **修复**：主循环里加 20 秒 idle 检测，主动发 `SendIrcLine(fd, "PING " + irc.bot_nick)` 保活

### Bug 9（cfg）：agent 路由 method 名不对
- **现象**：E2E 收到 `❌ 未注册的 MCP method: lan_agent_local_chat`
- **根因**：cfg 里配的是 `lan_agent_local_chat`，但 TryHandleRegisteredMcpTool 里实际 method 是 `lan_agent_run_local_chat`
- **修复**：cfg 里所有 `lan_agent_local_chat` → `lan_agent_run_local_chat`

---

## 4. 架构要点

### 4.1 MCP dispatch 调用链
```
UnrealIRCd (WS :8067)
  ↓ WS text frame: ":winner!winner@localhost PRIVMSG #main :@codex ping"
IrcBridgeThread.cpp RecvBuffer::ReadTextFrame()  ← frame_len 正确修复
  ↓ opcode=0x01, payload=":winner!winner@localhost PRIVMSG #main :@codex ping"
SplitIrcLines() → regex_match kPrivmsgRe → regex_match kMentionRe("@codex ping")
  ↓ irc.agent_routes["codex"] = "lan_agent_run_local_chat"
GetMcpDispatch()(cfg, "lan_agent_run_local_chat", params_view, &result)
  ↑ Meyers singleton，main() 里显式注册了
main() 里的 captureless lambda → TryHandleRegisteredMcpTool(cfg, ...)
  ↓ 135 个 MCP 工具分发
CommandResult → SendChannelMsg → EncodeWsClientFrame(0x1, PRIVMSG) → UnrealIRCd → #main
```

### 4.2 WS 帧方向要点（容易搞混）
| 方向 | mask | 代码 |
|---|---|---|
| client→server（本 bridge 发） | **必须带 mask** | `EncodeWsClientFrame()` 自动加 mask |
| server→client（UnrealIRCd 发） | 不带 mask | `DecodeWsFrame()` mask bit = 0 |

### 4.3 WS control frame vs IRC text PING
| 类型 | 承载方式 | opcode | bridge 回复 |
|---|---|---|---|
| WS control PING | WS frame | 0x09 | `EncodeWsClientFrame(0x0A, payload)` — WS PONG |
| IRC 层 PING | WS text frame | 0x01 | `SendIrcLine(fd, "PONG ...")` — 也是 WS text frame！ |

### 4.4 为什么用 main() 里显式注册而不是静态初始化器
匿名 namespace 内的静态注册器（`static IrcBridgeMcpRegistrar g_irc_mcp_registrar;`）被 GCC 当 dead code 优化掉了——因为匿名 namespace 外部没符号引用它。**main() 是外部可见入口**，里面显式调用 SetIrcMcpToolHandler 保证注册一定发生。

### 4.5 主动保活
主循环里 `time(nullptr) - last_activity >= 20` → 主动发 IRC PING。既防止 UnrealIRCd 发 close，也防止 SO_RCVTIMEO 一直 EAGAIN。

---

## 5. Agent 路由表（codex_lan_agent.cfg v6 修正后）

```
irc_agent.codex  → lan_agent_run_local_chat       # @codex <问题>
irc_agent.trae  → lan_agent_run_local_chat       # @trae <问题>
irc_agent.lan    → lan_agent_run_cxparser_flow     # @lan <任务>
irc_agent.rag    → lan_agent_rag_review_observe    # @rag <查询>
irc_agent.default → lan_agent_run_local_chat      # 兜底
```

**之前的 `lan_agent_local_chat` 是错的！** TryHandleRegisteredMcpTool 里正确名字是 `lan_agent_run_local_chat`。要查完整列表：`grep -oP '"lan_agent_\w+"' /H/aisrc/lan-agent/src/main.cpp | sort -u`

---

## 6. 构建基础设施（参考）

所有 .deb 包解压到 `/H/usr/aisrc/usr/`，**未触碰** 宿主系统 /usr。

| 组件 | 位置 |
|---|---|
| g++-14 | `/H/usr/aisrc/usr/bin/g++_wrapper.sh` |
| cmake | `/H/usr/aisrc/usr/bin/cmake` |
| ninja | `/H/usr/aisrc/usr/bin/ninja` |
| libc6-dev | 已解压，`libc_nonshared.a` 已 patch |
| GCC C++ headers | `/H/usr/aisrc/usr/include/c++/14/` |

---

## 7. Phase 2 展望

- [ ] **Debug log 清理**：`[irc-bridge] raw:`、`[irc-bridge] WS text payload=` 等逐字节 debug 改成 `#ifdef DEBUG` 开关
- [ ] Bridge 连接状态持久化/监控（断线自动重连已有，加 health check endpoint）
- [ ] `@codex ping` 端到端 → 需要 8080 local-chat LLM provider 启动
- [ ] `@lan run default` 端到端 → lan_agent_run_cxparser_flow
- [ ] IRC → MCP 参数解析加强（不同 method 不同参数结构）
- [ ] 错误恢复：MCP dispatch 返回非 0 时 bridge 回复更友好的错误消息
- [ ] WebIRC 端到端验收（浏览器连接）

---

## 8. 变更记录

| 版本 | 日期 | 主要变更 |
|---|---|---|
| v3 | 2026-10-09 | Python→C++ 重写规划 |
| v4 | 2026-10-09 | 写入构建状态 + linker blocker + 函数指针修复方案 |
| v5 | 2026-10-10 | Phase 1 基础完成：build 过 + bridge WS 上线 + Bug 3/4/5 |
| **v6** | **2026-10-10** | **E2E 全链路打通！修 9 个 bug（Bug 1-9），关键：frame_len 计算错误（Bug 7）+ 静态注册器 DCE（Bug 2）+ Meyers singleton（Bug 3）+ cfg method 名（Bug 9）+ 主动保活（Bug 8）。agent.log 确认 `[irc-bridge] MCP dispatch handler ✅ 已注册` → `recv: @codex ping` → dispatch → 发回复到 #main** |

---

## 附录：E2E 成功的 agent.log 关键行

```
[irc-bridge] LoadIrcConfig(/H/aisrc/lan-agent/codex_lan_agent.cfg) → irc_enabled=1 ws=127.0.0.1:8067
[irc-bridge] MCP dispatch handler ✅ 已注册              ← handler 注册成功
[irc-bridge] ✅ 已上线，监听 #main
[irc-bridge] raw: 81 36 frame: opcode=01 plen=54 masked=0   ← frame_len 修复生效！opcode 正确！
[irc-bridge] WS text payload=54: :irc.local 366 codex-bridge #main :End of /NAMES list.
[irc-bridge] recv: :winner!winner@localhost PRIVMSG #main :@codex ping   ← 正确收到 PRIVMSG
→ 发 ⏳ 进度消息到 #main
→ 调 lan_agent_run_local_chat → exit_code=42（LLM provider 未启）
→ 发 ❌ 错误消息到 #main（agent 回复了完整的 tool 执行结果）
```

---

*Phase 1 完成。E2E 全链路通过。9 个 bug 全部修复。下一阶段进入 Phase 2（参数解析加强 + LLM provider 端到端）。*

---

## 9. Phase 1.5 — Bridge 架构演进（2026-10-10 下午）

Phase 1.5 是 **在不改下层 lan-agent/MCP 的前提下，把 IRC bridge 从"lan-agent 专属前端"演进为"通用外部响应桥路"** 的重构。

### 9.1 新增 5 个 bug 修复（Phase 1 之后）

| # | 现象 | 根因 | 修复 |
|---|------|------|------|
| **10** | `@codex-bridge 你活着吗` → bridge **完全没反应** | `kMentionRe=R"(^@(\w+)\s+(.+)$)"` — `\w+` 不匹配连字符 `-`，IRC nick `codex-bridge` 里的 `-` 让 regex 在该位置失败 → `std::regex_match` 返回 false → `continue` 静默跳过 | `(\w+)` → `([\w-]+)`（支持 IRC nick 合法字符：字母数字下划线连字符） |
| **11** | mention 不在路由表 → fallback 报 `❌ 未注册 MCP method: lan_agent_local_chat` | hardcoded fallback method 名写错了（少了 `_run_`）；且不读 cfg 的 `irc_agent.default` | fallback 改成先查 `irc.agent_routes["default"]`，兜底才写对的 `"lan_agent_run_local_chat"` |
| **12** | `@codex-bridge ❌ local_chat endpoint is unavailable (exit=42) 这是什么意思` → bridge 又把那段错误消息当新 task | bridge 不区分自己发的和别人发的 PRIVMSG → 用户复制粘贴含 `@mention` 的内容会形成反馈环 | `if (from_nick == irc.bot_nick) continue;` 跳过自身消息 |
| **13** | 所有 @mention 一律返回 exit=42（deterministic fallback 之前） | bridge 构造 MCP params 时 `scope="default"`（硬编码字符串），而 `RunLocalChat` 的 deterministic fallback 只在 `LooksLikeDirectoryScope(scope)` 为 true（即 scope 是真实目录路径）时触发 | scope 改成 `g_agent_cfg.workspace_root`（cfg 里配的 `/H/aisrc/lan-agent`，真实存在的目录）→ fallback 扫描该目录生成确定性项目摘要 |
| **14** | Bridge 回复 `result_object={"status":"FAILED","summary":"...","failure_mode":...}` — 把完整 JSON dump 到频道 | reply formatter 遍历 **所有** `result.fields` 并 dump 成 `k=v` 对，包括 `result_object`（JSON 套娃）、`log_path`、`evidence_ref` 等内部调试字段 | 重写 reply formatter：成功提取 `direct_answer → body → summary → result`（LLM 直接输出）；失败提取 `summary → error → failure_mode`（一句话 + exit_code）。**只抽结论字段，排除所有调试字段** |

### 9.2 新增 4 个本地 intent（硬编码 C++ if-else 链，后来发现方向错了）

| Intent | 触发关键词 | 本地回答 | 依赖 |
|--------|-----------|---------|------|
| 🏓 ping | `ping/alive/在吗/活着/你好` | `🏓 pong — bridge 在线（ws→host:port/path，#channel）` | 零依赖 |
| 📁 scope | `关联/范围/项目还是全局/全局` | `📁 scope = 项目级 (workspace_root=...)` | 零依赖 |
| ⚙️ status | `配置/cfg/endpoint/8080/8095/状态/status` | 3 个 endpoint **TCP connect 探针**（200ms 超时）+ bridge 自身 + MCP dispatch 注册状态 | 无 LLM 依赖，只用 3 次 socket connect |
| 🧭 help | `帮助/help/怎么用/routes` | 用法 + 已注册 routes 列表 | 零依赖 |

**路由优先级**（在 MCP dispatch 之前）：
```
PRIVMSG → sanitize → kPrivmsgRe → from_nick≠bot_nick → kMentionRe
  → TryHandleBridgeLocalIntent()：匹配 → 本地回答 + continue（跳过 MCP）
                                  不匹配 → 查 irc.agent_routes → MCP dispatch
```

### 9.3 本轮 C++ 代码变更汇总（Phase 1 + Phase 1.5）

**IrcBridgeThread.cpp 从 ~860 行 → ~1100 行**，新增：

| 函数 | 行号 | 作用 |
|------|------|------|
| `TryHandleBridgeLocalIntent()` | L742-832 | 4 类本地 intent handler（硬编码，Phase 2 重构为 cfg 驱动） |
| `TaskHasKeyword()` | L712-719 | `std::initializer_list<const char *>` + `strcasestr` 关键词匹配 |
| `TcpProbe(host, port, timeout_ms)` | L682-710 | 200ms TCP connect 探针（用于 status intent 的 endpoint 存活检测） |
| 主循环 `from_nick == bot_nick` 跳过 | L950-951 | 防反馈环（bug 12 修复） |
| scope→workspace_root | L949 | bug 13 修复 |
| ReplyFormatter 重写 | L1010-1060 | bug 14 修复（只抽结论字段） |

### 9.4 当前驱动链路全景（真实代码）

```
┌──────────────────────────────────────────────────────────────┐
│  第 1 层：AI Provider（外部）                                  │
│  Codex CLI / Trae Desktop / 本地 llama.cpp server            │
│  协议：HTTP POST JSON → OpenAI ChatCompletions               │
│  （lan-agent 不认厂商，只认协议）                               │
└──────────────────────────┬───────────────────────────────────┘
                           │ HTTP JSON
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  第 2 层：lan-agent（本机 codex_lan_agent，长驻 18080）         │
│  3 个 endpoint 槽位：                                          │
│    local_chat_endpoint → :8080/local-chat   (primary)        │
│    generation_endpoint → :8095/v1/chat/...  (fallback)      │
│    embedding_endpoint  → :8096/v1/embeddings                 │
│                                                              │
│  RunLocalChat(config, scope, question, mode, timeout, evid)   │
│    1. ResolveReachableEndpoint(primary, fallback, 2s)        │
│    2. 选到可用端点 → HTTP POST                                │
│    3. **两端点都 down** →                                     │
│       TryBuildProviderUnavailableFallback():                  │
│       scope 是真实目录 → BuildDirectoryScopeFallbackResult()  │
│       → 扫描 workspace_root 下文件结构，生成**确定性摘要**       │
│       （deterministic_directory_fallback）                    │
│       scope 不是目录 → exit_code=42（硬失败）                │
│                                                              │
│  scope 语义：**项目级**（workspace_root=/H/aisrc/lan-agent）  │
│  不存在"全局"模式                                             │
└──────────────────────────┬───────────────────────────────────┘
                           │ MCP HTTP JSON
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  第 3 层：MCP Tool Dispatch（lan-agent 内置）                   │
│  lan_agent_run_local_chat / cxparser_flow / rag_review …     │
│                                                              │
│  IRC bridge 注册 handler（main() SetIrcMcpToolHandler）      │
│  → {"scope":workspace_root, "question":task} → RunLocalChat  │
│  → CommandResult                                             │
└──────────────────────────┬───────────────────────────────────┘
                           │ CommandResult.fields{}
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  第 4 层：IRC Bridge（IrcBridgeThread）                        │
│  WS → 192.168.5.100:80/ws → nginx → UnrealIRCd → #main       │
│                                                              │
│  当前路由优先级：                                              │
│    TryHandleBridgeLocalIntent() → 本地回答                    │
│    irc.agent_routes[mention]    → MCP dispatch                │
│                                                              │
│  scope 传 workspace_root（项目级，bug 13 修复后）              │
│  ReplyFormatter 只抽结论字段（bug 14 修复后）                  │
└──────────────────────────┬───────────────────────────────────┘
                           │ WS → HTTP upgrade → IRC
                           ▼
                    192.168.5.100 #main
```

### 9.5 当前本机状态（2026-10-10 下午）

```
codex_lan_agent (pid=9114) @ 18080
  ├─ MCP HTTP server            🟢 listen
  ├─ IrcBridgeThread             🟢 online → 192.168.5.100:80/ws → #main
  ├─ local_chat_endpoint :8080  🔴 offline
  ├─ generation_endpoint :8095  🔴 offline
  └─ embedding_endpoint  :8096  🔴 offline
                                  → 所有 RunLocalChat 走 deterministic fallback
```

### 9.6 Phase 1.5 发现的方向问题

当前 bridge 对下游只做了 **MCP dispatch 一种路由类型**，且 TryHandleBridgeLocalIntent 是 C++ hardcoded if-else 链——每加一个 intent 要 recompile。

**用户明确的定位**：bridge 不是 lan-agent 专属前端，而是 **通用外部响应桥路**。

需要扩展：
- 路由类型从 1 种（MCP dispatch）→ N 种（MCP / HTTP webhook / 直连 LLM / 本地规则）
- 本地 intent 从 C++ hardcoded → cfg 驱动
- 新增路由/intent 只改 cfg，不改代码 + 不 recompile

---

## 10. Phase 2 — cfg 驱动路由引擎（架构重构）

### 10.1 目标

把 IRC bridge 从：
```
transport + C++ hardcoded if-else router（MCP dispatch only）
```
重构为：
```
thin transport + cfg 驱动多类型路由引擎
```

### 10.2 cfg 新语法（设计）

```ini
# ============================================
# Phase 2 路由表（替代 irc_agent.* + hardcoded intent）
# ============================================

# ---------- 路由类型 1: local intent（纯 cfg，零依赖）----------
irc_route.ping.type=local
irc_route.ping.keywords=ping,alive,在吗,活着,在线,你好,活着吗
irc_route.ping.reply=🏓 pong — bridge 在线（ws=${irc.ws_host}:${irc.ws_port}${irc.ws_path}/#${irc.channel}）

irc_route.scope.type=local
irc_route.scope.keywords=关联,范围,scope,项目还是全局,哪个项目,隔离,多项目
irc_route.scope.reply=📁 scope=项目级 (workspace_root=${workspace_root})

irc_route.status.type=local
irc_route.status.keywords=配置,cfg,endpoint,llm,provider,8080,8095,8096,能用,可用,状态,status
# reply 支持 ${probe:host:port} 变量（bridge 做 TCP connect 探针）
irc_route.status.reply=⚙️ local_chat=${probe:127.0.0.1:8080}, generation=${probe:127.0.0.1:8095}, embedding=${probe:127.0.0.1:8096}, MCP dispatch=${probe:127.0.0.1:18080}, bridge=🟢

irc_route.help.type=local
irc_route.help.keywords=帮助,help,怎么用,怎么触发,哪些命令,支持什么,routes,命令,触发,用法,使用
irc_route.help.reply=🧭 @<mention> <任务>→MCP | ping/scope/status/help→本地 | routes=${irc.agent_routes_keys}

# ---------- 路由类型 2: MCP dispatch（lan-agent 内置工具）----------
irc_route.codex.type=mcp
irc_route.codex.method=lan_agent_run_local_chat
irc_route.codex.scope=${workspace_root}
irc_route.codex.mode=code_analysis

irc_route.trae.type=mcp
irc_route.trae.method=lan_agent_run_local_chat
irc_route.trae.scope=${workspace_root}

irc_route.lan.type=mcp
irc_route.lan.method=lan_agent_run_cxparser_flow
irc_route.lan.flow_id=auto_detect

irc_route.rag.type=mcp
irc_route.rag.method=lan_agent_rag_review_observe
irc_route.rag.scope=${workspace_root}

irc_route.default.type=mcp
irc_route.default.method=lan_agent_run_local_chat

# ---------- 路由类型 3: HTTP webhook（桥路核心：外部服务能接！）----------
irc_route.external.type=http
irc_route.external.url=http://127.0.0.1:9000/irc-callback
irc_route.external.timeout_ms=5000
irc_route.external.method=POST
# bridge POST body: {"from": "<nick>", "channel": "#main", "mention": "<route_name>", "task": "..."}
# 外部服务返回: {"reply": "..."} (纯文本) 或 {"reply": "...", "type": "success|error|progress"}

# ---------- 路由类型 4: 直连 LLM（任何 OpenAI 兼容端点）----------
irc_route.ollama.type=llm
irc_route.ollama.endpoint=http://127.0.0.1:11434/v1
irc_route.ollama.model=qwen2.5:7b
irc_route.ollama.system=你是 IRC channel agent，回答要简洁（≤200字），用中文
irc_route.ollama.timeout_ms=15000
```

### 10.3 路由引擎执行流程

```
PRIVMSG #main :@codex-bridge 反馈当前状况
  → sanitize → kPrivmsgRe → from_nick≠bot_nick → kMentionRe
  → mention=codex-bridge, task=反馈当前状况
  → LoadIrcConfig 解析 irc_route.* → 构建 RouteTable
  → RouteTable::Match(mention, task)：
      优先级 1: 遍历所有 type=local 的 route，keywords 匹配 → 本地 reply（变量替换）
      优先级 2: 查 route_name（或 mention）精确匹配 → 走对应 type
      优先级 3: irc_route.default → 兜底
  → 执行路由：
      type=local  → 变量替换 → SendChannelMsg（零依赖，立即返回）
      type=mcp    → 组 JSON params → GetMcpDispatch() → CommandResult → ReplyFormatter
      type=http   → POST {from, channel, mention, task} → parse JSON → SendChannelMsg
      type=llm    → POST chat/completions {model, messages} → 取 content → SendChannelMsg
```

### 10.4 代码改动清单

| 模块 | 改动 | 行数增量 |
|------|------|---------|
| **IrcBridgeThread.h** | 新增 `struct Route`（type + params map）、`class RouteTable` | ~40 |
| **IrcBridgeThread.cpp** | 删除 `TryHandleBridgeLocalIntent`（1 个大 if-else 链） → 新增 `RouteTable::BuildFromCfg()` + `RouteTable::Match()` + 4 个 type executor（`ExecLocal`, `ExecMcp`, `ExecHttp`, `ExecLlm`） | ~200 替换原有 ~100 hardcoded 行 |
| **LoadIrcConfig** | 扩展解析 `irc_route.<name>.*` 节 → 填充 `IrcCfg.routes` map | ~50 |
| **变量替换器** | 新增 `${xxx}` → 实时值替换（`${probe:host:port}` → 🟢/🔴） | ~40 |

### 10.5 关键设计决策

1. **向后兼容**：cfg 里没写 `irc_route.*` 时 → fallback 到 legacy `irc_agent.*` 行为，Phase 1 老 cfg 继续能用
2. **变量替换**：reply template 里的 `${var}` 用 RunAgentConfig 真实值替换；`${probe:h:p}` 运行时做 TCP connect（和现在的 TcpProbe 同一逻辑）
3. **type=llm 直连**：bridge 可以绕过 lan-agent 直接调 llama.cpp / Ollama / Codex CLI 暴露的 OpenAI 端点——**bridge 完全不依赖 lan-agent 的 RunLocalChat**
4. **type=http webhook**：外部 Python/Go/Node 服务只要实现一个 HTTP endpoint 就能接入 IRC——这是"外部响应桥路"的核心能力

### 10.6 分阶段实施

| 步骤 | 内容 | 验证 |
|------|------|------|
| **Step 1** | 实现 `RouteTable::BuildFromCfg()` + `ExecLocal`（替代 hardcoded intent） | ping/scope/status/help 走 cfg，老 cfg 继续能用 |
| **Step 2** | 实现 `ExecMcp`（把现有 irc_agent 路由表纳入统一框架） | 所有 @mention 走统一 RouteTable |
| **Step 3** | 实现 `ExecHttp`（新增 webhook 能力） | 本地 Python mock server 接收 POST → 返回 reply → IRC 频道显示 |
| **Step 4** | 实现 `ExecLlm`（bridge 直连 OpenAI 兼容端点） | llama.cpp server `--port 8095 --model qwen2.5:7b` → IRC 频道看到 LLM 直接回复 |

### 10.7 预期收益

| 收益 | 之前 | 之后 |
|------|------|------|
| 新增本地 intent | 改 C++ + recompile（~5min） | 改 cfg 一行（~10s） |
| 新增下游类型 | 只支持 MCP dispatch | MCP + HTTP + LLM直连 + 本地规则 |
| bridge 是否依赖 lan-agent | 是（MCP handler） | 可选（type=local/llm/http 都可以独立跑） |
| 外部服务接入 | 不可能（只能改 C++） | 写个 HTTP endpoint 就行 |

---

## 11. 变更记录

| 版本 | 日期 | 主要变更 |
|---|---|---|
| v3 | 2026-10-09 | Python→C++ 重写规划 |
| v4 | 2026-10-09 | 写入构建状态 + linker blocker + 函数指针修复方案 |
| v5 | 2026-10-10 | Phase 1 基础完成：build 过 + bridge WS 上线 + Bug 3/4/5 |
| **v6** | **2026-10-10** | Phase 1 E2E 全链路！修 9 个 bug（Bug 1-9） |
| **v7** | **2026-10-10** | Phase 1.5 架构演进：修 Bug 10-14（连字符 nick、fallback method 名、反馈环、scope=workspace_root、reply formatter 只抽结论）。新增 4 个本地 intent（ping/scope/status/help）。status intent 带 TCP 探针。**发现方向问题：bridge 是通用外部响应桥路，不是 lan-agent 专属前端** |
| **v8** | **2026-10-10** | Phase 2 启动：cfg 驱动路由引擎设计（4 种路由类型：local/mcp/http/llm）。从 C++ hardcoded if-else → cfg 驱动。新增路由/intent 只改 cfg 不 recompile |

---

*Phase 1 + Phase 1.5 完成（14 个 bug 全修）。Phase 2（cfg 驱动路由引擎）启动——先 Step 1（RouteTable + ExecLocal）替代 hardcoded intent，老 cfg 向后兼容。*
