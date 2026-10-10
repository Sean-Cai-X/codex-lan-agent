#pragma once

// ============================================================
// IrcBridgeThread.h — IRC ↔ lan-agent C++ 内置桥接线程
// 设计约束：
//   - 零第三方库，纯 C++17 + Linux syscalls
//   - WebSocket RFC 6455 客户端（raw socket）
//   - 入向：WS PRIVMSG → TryHandleRegisteredMcpTool()
//   - 出向：MCP result → WS PRIVMSG #main
// 系统上只跑两个进程：UnrealIRCd + lan-agent
// ============================================================

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "types.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace codex_lan_agent {

// ——— MCP dispatch 回调注册 ———
// 因为 McpToolDispatch.h 被 main.cpp include 在匿名 namespace 里，
// TryHandleRegisteredMcpTool 成了 local symbol，IrcBridgeThread.cpp 这个 TU 访问不到。
// 解决方案：main.cpp 在 startIrcBridgeThread() 之前，把一个 captureless lambda 转成
// 函数指针，注册到这里。lambda 会 emit 成可链接的全局符号。
typedef bool (*McpToolDispatchFn)(
    const AgentConfig & config,
    const std::string & tool_name,
    const JsonRequestView & params,
    CommandResult * result);

// 在 startIrcBridgeThread 之前调用一次即可
void SetIrcMcpToolHandler(McpToolDispatchFn fn);

// 在 RunServer() 里 listen socket 建立后调用
// 返回后线程已在后台运行
void startIrcBridgeThread(const std::string & cfg_path);

// 进程退出前调用（可选，进程退出自动清理）
void stopIrcBridgeThread();

// ——— WebSocket 协议函数（RFC 6455） ———

// 生成 16 字节随机 Sec-WebSocket-Key（base64 编码后 24 字符）
std::string MakeWsKey16();

// Sec-WebSocket-Accept = base64(SHA1(key + GUID))
// GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
std::string ComputeWsAccept(const std::string & ws_key);

// 编码 client→server frame（必须带 MASK，RFC 6455 §5.3）
// opcode: 0x1=text, 0x8=close, 0x9=ping, 0xA=pong
std::vector<uint8_t> EncodeWsClientFrame(uint8_t opcode,
                                         const std::string & payload);

// 解码 server→client frame（不带 mask）
struct WsDecoded {
    bool fin = true;
    uint8_t opcode = 0;
    std::string payload;
    // 只有在 fin=false 且需要分片重组时才用到
};
bool DecodeWsFrame(const uint8_t *data, size_t len, WsDecoded *out);

// ——— Socket 层 ———

// TCP connect，带超时
// 返回 fd (>=0) 或 -1 on error
int TcpConnectTimeout(const std::string & host, int port, int timeout_ms);

// 完整 WebSocket 连接（TCP + HTTP Upgrade + 握手验证）
// 返回 fd (>=0) 或 -1 on error
int WsConnect(const std::string & host, int port, int timeout_ms = 5000);

// ——— SHA-1 + base64（RFC 标准，自实现） ———

std::vector<uint8_t> Sha1(const std::string & input);
std::string Base64Encode(const uint8_t *data, size_t len);

}  // namespace codex_lan_agent
