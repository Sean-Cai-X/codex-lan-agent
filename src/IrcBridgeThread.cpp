// ============================================================
// IrcBridgeThread.cpp — IRC ↔ lan-agent C++ 内置桥接线程
//
// 依赖：
//   - AgentConfig.h / McpToolDispatch.h / JsonRequestView.h / types.h
//   - Linux syscalls (sys/socket.h, netdb.h, fcntl.h, time.h, etc.)
//   - C++17 stdlib（无其他第三方库）
//
// 设计说明：
//   - 单 std::thread，一个 WS fd
//   - recv loop 用 SO_RCVTIMEO 5s，超时后检查 stop 标志
//   - PRIVMSG 入向 → 正则匹配 @mention → TryHandleRegisteredMcpTool()
//   - MCP result → 同 fd 发 PRIVMSG #main :[agent-label] ✅ summary
//   - 断线自动退避重连（3s → 6s → 12s）
// ============================================================

#include "IrcBridgeThread.h"
#include "AgentConfig.h"
#include "HttpClient.h"
#include "JsonRequestView.h"
#include "types.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace codex_lan_agent {

// ——— 全局互斥锁：保护 SendChannelMsg / WS frame 发送 ———
// bridge 主循环（PING 保活）、dispatch 线程（异步回复）、local intent 都可能写 fd
static std::mutex & GetBridgeMutex() {
    static std::mutex m;
    return m;
}

// ——— MCP dispatch 回调存储（Meyers singleton 避免跨 TU 初始化顺序问题）———
static McpToolDispatchFn & GetMcpDispatch() {
    static McpToolDispatchFn fn = nullptr;
    return fn;
}

// ============================================================
// SHA-1 (RFC 3174, 纯 C++17 实现)
// ============================================================
std::vector<uint8_t> Sha1(const std::string & input) {
    // 512 位 (64 字节) 块大小, 160 位 (20 字节) 摘要
    static const uint32_t K[4] = {
        0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
    };

    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };

    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // 追加 padding
    uint64_t orig_len_bits = (uint64_t)input.size() * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) {
        msg.push_back((orig_len_bits >> (i * 8)) & 0xFF);
    }

    // 处理每个 64 字节块
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)msg[offset + i * 4] << 24) |
                   ((uint32_t)msg[offset + i * 4 + 1] << 16) |
                   ((uint32_t)msg[offset + i * 4 + 2] << 8) |
                   ((uint32_t)msg[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = K[0]; }
            else if (i < 40) { f = b ^ c ^ d;             k = K[1]; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = K[2]; }
            else             { f = b ^ c ^ d;             k = K[3]; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::vector<uint8_t> out(20);
    auto store32 = [&](int idx, uint32_t v) {
        out[idx]     = (v >> 24) & 0xFF;
        out[idx + 1] = (v >> 16) & 0xFF;
        out[idx + 2] = (v >> 8)  & 0xFF;
        out[idx + 3] = v & 0xFF;
    };
    store32(0, h0); store32(4, h1); store32(8, h2);
    store32(12, h3); store32(16, h4);
    return out;
}

// ============================================================
// Base64 (RFC 4648)
// ============================================================
std::string Base64Encode(const uint8_t *data, size_t len) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out += tab[(n >> 18) & 0x3F];
        out += tab[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? tab[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? tab[n & 0x3F] : '=';
    }
    return out;
}

// ============================================================
// WebSocket RFC 6455 helpers
// ============================================================

std::string MakeWsKey16() {
    // 16 字节随机（stdlib rand，够用，不是安全上下文）
    std::string raw(16, '\0');
    for (int i = 0; i < 16; ++i) raw[i] = (char)(rand() & 0xFF);
    return Base64Encode((const uint8_t *)raw.data(), raw.size());
}

std::string ComputeWsAccept(const std::string & ws_key) {
    static const std::string kGUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = ws_key + kGUID;
    auto sha1 = Sha1(combined);
    return Base64Encode(sha1.data(), sha1.size());
}

std::vector<uint8_t> EncodeWsClientFrame(uint8_t opcode,
                                         const std::string & payload) {
    // Client frames MUST be masked (RFC 6455 §5.3)
    const uint8_t MASK_BIT = 0x80;
    std::vector<uint8_t> out;

    uint8_t byte0 = 0x80 | (opcode & 0x0F);  // FIN=1, RSV=0, opcode
    out.push_back(byte0);

    size_t plen = payload.size();
    std::vector<uint8_t> mask_key(4);
    for (auto & b : mask_key) b = (uint8_t)(rand() & 0xFF);

    if (plen < 126) {
        out.push_back((uint8_t)(MASK_BIT | plen));
    } else if (plen < 65536) {
        out.push_back(MASK_BIT | 126);
        out.push_back((uint8_t)((plen >> 8) & 0xFF));
        out.push_back((uint8_t)(plen & 0xFF));
    } else {
        out.push_back(MASK_BIT | 127);
        for (int i = 7; i >= 0; --i) {
            out.push_back((uint8_t)((plen >> (i * 8)) & 0xFF));
        }
    }

    out.insert(out.end(), mask_key.begin(), mask_key.end());

    for (size_t i = 0; i < plen; ++i) {
        out.push_back((uint8_t)payload[i] ^ mask_key[i % 4]);
    }
    return out;
}

bool DecodeWsFrame(const uint8_t *data, size_t len, WsDecoded *out) {
    if (len < 2) return false;
    out->fin = (data[0] & 0x80) != 0;
    out->opcode = data[0] & 0x0F;

    uint8_t byte1 = data[1];
    bool masked = (byte1 & 0x80) != 0;
    uint64_t plen = byte1 & 0x7F;

    size_t header_end = 2;
    if (plen == 126) {
        if (len < 4) return false;
        plen = ((uint64_t)data[2] << 8) | data[3];
        header_end = 4;
    } else if (plen == 127) {
        if (len < 10) return false;
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | data[2 + i];
        header_end = 10;
    }

    if (masked) {
        if (len < header_end + 4) return false;
        const uint8_t *mask = data + header_end;
        const uint8_t *pdata = data + header_end + 4;
        size_t remain = len - header_end - 4;
        size_t copy = std::min((uint64_t)remain, plen);
        out->payload.resize(copy);
        for (size_t i = 0; i < copy; ++i) {
            out->payload[i] = pdata[i] ^ mask[i % 4];
        }
    } else {
        size_t copy = std::min((uint64_t)(len - header_end), plen);
        out->payload.assign((const char *)(data + header_end), copy);
    }
    return true;
}

// ============================================================
// Socket 层
// ============================================================

int TcpConnectTimeout(const std::string & host, int port, int timeout_ms) {
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        fprintf(stderr, "[irc-bridge] getaddrinfo(%s) failed: %s\n",
                host.c_str(), strerror(errno));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        // 非阻塞 connect + select timeout
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(fd, p->ai_addr, p->ai_addrlen);
        if (rc == 0) break;
        if (errno != EINPROGRESS) { close(fd); fd = -1; continue; }

        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        int wr = select(fd + 1, nullptr, &wset, nullptr, &tv);
        if (wr <= 0) { close(fd); fd = -1; continue; }

        int err = 0; socklen_t elen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) { close(fd); fd = -1; continue; }

        // 恢复阻塞
        fcntl(fd, F_SETFL, flags);
        break;
    }
    freeaddrinfo(res);

    if (fd >= 0) {
        struct timeval tv = {5, 0};  // 5s recv timeout
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return fd;
}

// 简单的 buffered recv（HTTP upgrade 阶段一次性读完响应）
static bool RecvAll(int fd, std::string *out, size_t max_bytes = 65536) {
    out->clear();
    while (out->size() < max_bytes) {
        char buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            out->append(buf, n);
            // HTTP upgrade 响应以 \r\n\r\n 结尾
            if (out->find("\r\n\r\n") != std::string::npos) break;
        } else if (n == 0) {
            return false;  // 对端关闭
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // 超时
            return false;
        }
    }
    return true;
}

// 找 header 里 Sec-WebSocket-Accept 的值
static std::string ExtractHeader(const std::string & resp, const std::string & name) {
    std::string needle = name + ": ";
    size_t pos = resp.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = resp.find("\r\n", pos);
    if (end == std::string::npos) end = resp.size();
    // trim 前后空白
    while (pos < end && std::isspace((unsigned char)resp[pos])) ++pos;
    while (end > pos && std::isspace((unsigned char)resp[end - 1])) --end;
    return resp.substr(pos, end - pos);
}

int WsConnect(const std::string & host, int port, const std::string & path, int timeout_ms) {
    int fd = TcpConnectTimeout(host, port, timeout_ms);
    if (fd < 0) return -1;

    std::string key = MakeWsKey16();
    // path 必须以 / 开头（HTTP 规范），nginx proxy_pass 需要显式 path
    std::string use_path = path.empty() ? "/" : path;
    char req[1024];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        use_path.c_str(), host.c_str(), port, key.c_str());

    ssize_t sent = send(fd, req, strlen(req), 0);
    if (sent <= 0) { close(fd); return -1; }

    std::string resp;
    if (!RecvAll(fd, &resp)) { close(fd); return -1; }

    if (resp.find("101") == std::string::npos ||
        resp.find("Switching Protocols") == std::string::npos) {
        fprintf(stderr, "[irc-bridge] WS upgrade failed: %s\n",
                resp.substr(0, resp.find("\r\n")).c_str());
        close(fd);
        return -1;
    }

    std::string accept = ExtractHeader(resp, "Sec-WebSocket-Accept");
    std::string expected = ComputeWsAccept(key);
    if (accept != expected) {
        fprintf(stderr, "[irc-bridge] Sec-WebSocket-Accept mismatch\n");
        close(fd);
        return -1;
    }
    return fd;
}

// ============================================================
// IRC 文本过滤器：乱码 / 异常编码 / 控制字符 / NULL 字节
// ============================================================

// UTF-8 序列校验：返回有效码元数，遇到非法序列就停
static bool IsUtf8Valid(const char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x80) { i++; continue; }   // ASCII
        int len;
        if      ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;                // 非起始字节
        if (i + len > n) return false;     // 不完整
        // continuation 检查
        for (int k = 1; k < len; ++k) {
            if (((unsigned char)p[i + k] & 0xC0) != 0x80) return false;
        }
        // 禁止 overlong（如 C0 80 是 NUL 的 overlong 编码）
        if (len == 2 && c >= 0xC0 && c <= 0xC1) return false;
        if (len == 3 && c == 0xE0 && (unsigned char)p[i+1] < 0xA0) return false;
        if (len == 4 && c == 0xF0 && (unsigned char)p[i+1] < 0x90) return false;
        if (len == 4 && c >= 0xF4) return false;  // > U+10FFFF
        i += len;
    }
    return true;
}

// 清洗 IRC 文本：UTF-8 合法 → 保留；非法字节 → '?'；控制字符 → 删除；\r \n → 保留
static std::string SanitizeIrcText(const std::string & in, int max_chars = 0) {
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ) {
        unsigned char c = (unsigned char)in[i];

        // NULL 字节（IRC 不允许）→ 删除
        if (c == 0x00) { i++; continue; }

        // 保留 \r \n \t（IRC 协议需要）
        if (c == 0x0D || c == 0x0A || c == 0x09) {
            out.push_back((char)c); i++; continue;
        }

        // 其他控制字符 0x01-0x1F 和 0x7F → 删除
        if ((c >= 0x01 && c <= 0x1F) || c == 0x7F) {
            i++; continue;
        }

        // ASCII 可打印 → 保留
        if (c < 0x80) {
            out.push_back((char)c); i++; continue;
        }

        // 多字节 UTF-8 → 校验
        size_t seq_len;
        if      ((c & 0xE0) == 0xC0) seq_len = 2;
        else if ((c & 0xF0) == 0xE0) seq_len = 3;
        else if ((c & 0xF8) == 0xF0) seq_len = 4;
        else { out.push_back('?'); i++; continue; }  // 孤立 continuation

        if (i + seq_len <= in.size() &&
            IsUtf8Valid(in.c_str() + i, seq_len)) {
            out.append(in, i, seq_len);
            i += seq_len;
        } else {
            out.push_back('?');
            i++;
        }
    }

    // 长度截断（按字符粗略）
    if (max_chars > 0 && (int)out.size() > max_chars) {
        out.resize((size_t)max_chars);
    }
    return out;
}

// ============================================================
// IRC protocol helpers
// ============================================================

// 通过 WebSocket text frame 发一条 IRC 协议行
static bool SendIrcLine(int fd, const std::string & line) {
    std::string full = line + "\r\n";
    auto frame = EncodeWsClientFrame(0x1, full);  // opcode=1 = text
    size_t total = 0;
    while (total < frame.size()) {
        ssize_t n = send(fd, frame.data() + total, frame.size() - total, 0);
        if (n <= 0) return false;
        total += (size_t)n;
    }
    return true;
}

static std::vector<std::string> SplitIrcLines(const std::string & raw) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < raw.size()) {
        size_t nl = raw.find("\r\n", start);
        if (nl == std::string::npos) nl = raw.size();
        if (nl > start) out.push_back(raw.substr(start, nl - start));
        start = nl + 2;
        if (nl == raw.size()) break;
    }
    return out;
}

// ============================================================
// WS frame recv loop（简单版：一次读一个 frame）
// 单线程，同时支持 recv 和 send
// ============================================================

struct RecvBuffer {
    std::vector<uint8_t> buf;

    // 返回一个完整 frame 的 payload（不含控制帧），空字符串表示无新 frame
    bool ReadTextFrame(int fd, std::string *out_text) {
        out_text->clear();
        // 先尝试从 socket 读更多数据进 buf
        uint8_t tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            fprintf(stderr, "[irc-bridge] recv %zd bytes\n", n);
            buf.insert(buf.end(), tmp, tmp + n);
        } else if (n == 0) {
            fprintf(stderr, "[irc-bridge] recv=0 (close)\n");
            return false;  // close
        } else {
            fprintf(stderr, "[irc-bridge] recv=-1 errno=%d(%s)\n", errno, strerror(errno));
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // 超时，无新数据
            return false;
        }

        // 尝试解码第一个 frame
        while (buf.size() >= 2) {
            uint8_t byte1 = buf[1];
            uint64_t plen = byte1 & 0x7F;
            size_t header_end = 2;
            if (plen == 126) header_end = 4;
            else if (plen == 127) header_end = 10;
            if (byte1 & 0x80) header_end += 4;  // mask key

            if (buf.size() < header_end) return true;  // header 还没收齐

            // 读真实 plen
            if (plen == 126) {
                uint16_t p16 = ((uint16_t)buf[2] << 8) | buf[3];
                plen = p16;
            } else if (plen == 127) {
                uint64_t p64 = 0;
                for (int i = 0; i < 8; ++i) p64 = (p64 << 8) | buf[2 + i];
                plen = p64;
            }
            size_t frame_len = header_end + plen;
            if (buf.size() < frame_len) return true;  // 整个 frame 还没收齐

            WsDecoded d;
            if (!DecodeWsFrame(buf.data(), frame_len, &d)) {
                // 坏 frame，跳过一个字节
                fprintf(stderr, "[irc-bridge] decode FAIL, buf_size=%zu first=%02x %02x skip 1\n",
                        buf.size(), buf[0], buf[1]);
                buf.erase(buf.begin());
                continue;
            }
            fprintf(stderr, "[irc-bridge] raw: %02x %02x frame: opcode=%02x plen=%zu masked=%d\n",
                    buf[0], buf[1], d.opcode, d.payload.size(), (int)(byte1 & 0x80 ? 1 : 0));
            buf.erase(buf.begin(), buf.begin() + frame_len);

            if (d.opcode == 0x9) {
                fprintf(stderr, "[irc-bridge] WS PING → reply PONG\n");
                // PING → 回 PONG
                auto pong = EncodeWsClientFrame(0xA, d.payload);
                send(fd, pong.data(), pong.size(), 0);
                continue;
            } else if (d.opcode == 0x8) {
                fprintf(stderr, "[irc-bridge] WS close frame\n");
                // close
                return false;
            } else if (d.opcode == 0x1) {
                fprintf(stderr, "[irc-bridge] WS text payload=%zu: %s\n",
                        d.payload.size(),
                        std::string(d.payload.begin(),
                                    std::min(d.payload.end(),
                                             d.payload.begin() + 200)).c_str());
                // text — 合并到 out_text（可能多个）
                out_text->append(d.payload);
            }
        }
        return true;
    }
};

// ============================================================
// IRC bridge config 读取（轻量，只认 irc_* 键）
// ============================================================
struct IrcCfg {
    std::string ws_host = "127.0.0.1";
    int ws_port = 8067;
    std::string ws_path = "/";          // nginx proxy 走 /ws，直连 UnrealIRCd 走 /
    std::string bot_nick = "seedcode-bridge";
    std::string bot_user = "seedcode-bridge";
    std::string channel = "#main";
    int irc_enabled = 0;
    int output_enabled = 1;
    int output_max_chars = 450;
    int push_rate_limit_per_min = 15;
    std::string line_continuation = " \xe2\x86\xaa";  // ↪
    std::string cfg_path;                  // 配置文件自身路径（codex_cli route 需要）
    std::map<std::string, std::string> agent_routes;  // @mention → MCP method (legacy)

    // Phase 2: cfg 驱动路由表
    // irc_route.<name>.<param>=<val> → routes[name] = {type, params{param→val}}
    struct Route {
        std::string type;                          // local | mcp | http | llm | shell | codex_cli
        std::map<std::string, std::string> params; // keywords, reply, method, scope, command, ...
        // —— 预留认证接口（当前不做校验，字段为空=放行）——
        // 未来: exec_key 非空时，需要 IRC 消息带 auth 码才能执行 shell/codex_cli
        std::string exec_key;
        int exec_perm = 0;                        // 0=只读, 1=可执行(未来用)
    };
    std::map<std::string, Route> routes;           // name → Route
};

// 辅助：把 "true"/"false"/"1"/"0"/"yes"/"no" 解析成 int
static int ParseBoolOrInt(const std::string & val) {
    if (val.empty()) return 0;
    std::string lower;
    lower.reserve(val.size());
    for (char c : val) lower.push_back((char)std::tolower((unsigned char)c));
    if (lower == "true" || lower == "yes" || lower == "on") return 1;
    if (lower == "false" || lower == "no" || lower == "off") return 0;
    return atoi(val.c_str());
}

static IrcCfg LoadIrcConfig(const std::string & cfg_path) {
    IrcCfg cfg;
    cfg.cfg_path = cfg_path;
    FILE *f = fopen(cfg_path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "[irc-bridge] ⚠️ cfg %s 打不开，用默认值\n", cfg_path.c_str());
        return cfg;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // trim
        char *p = line;
        while (*p && std::isspace((unsigned char)*p)) ++p;
        if (*p == '#' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        std::string key(p, eq);
        std::string val(eq + 1);
        // trim val
        while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();
        while (!val.empty() && std::isspace((unsigned char)val.front())) val.erase(0, 1);

        if (key == "irc_enabled")          cfg.irc_enabled = ParseBoolOrInt(val);
        else if (key == "irc_ws_host")     cfg.ws_host = val;
        else if (key == "irc_ws_port")     cfg.ws_port = atoi(val.c_str());
        else if (key == "irc_ws_path")     cfg.ws_path = val;
        else if (key == "irc_bot_nick")    cfg.bot_nick = val;
        else if (key == "irc_bot_user")    cfg.bot_user = val;
        else if (key == "irc_default_channel")  cfg.channel = val;
        else if (key == "irc_output_enabled")    cfg.output_enabled = ParseBoolOrInt(val);
        else if (key == "irc_output_max_chars") cfg.output_max_chars = atoi(val.c_str());
        else if (key == "irc_line_continuation") {
            cfg.line_continuation = val;
        }
        // irc_agent.codex=lan_agent_run_local_chat → agent_routes["codex"] = "lan_agent_run_local_chat"
        else if (key.rfind("irc_agent.", 0) == 0) {
            std::string mention = key.substr(10);
            cfg.agent_routes[mention] = val;
        }
        // irc_route.<name>.<param>=<val> → cfg.routes[name].params[param] = val
        // 同时支持 irc_route.<name>.type=<type>（直接写到 Route.type）
        else if (key.rfind("irc_route.", 0) == 0) {
            // irc_route. = 10 字符；剩下的是 "<name>.<param>"
            std::string rest = key.substr(10);
            size_t dot2 = rest.find('.');
            if (dot2 != std::string::npos) {
                std::string route_name = rest.substr(0, dot2);
                std::string param = rest.substr(dot2 + 1);
                auto & r = cfg.routes[route_name];  // map 默认构造
                if (param == "type") {
                    r.type = val;
                } else if (param == "exec_key") {
                    r.exec_key = val;          // 认证码（预留，空=放行）
                } else if (param == "exec_perm") {
                    r.exec_perm = atoi(val.c_str());  // 0=只读, 1=可执行
                } else {
                    r.params[param] = val;
                }
            }
        }
    }
    fclose(f);
    fprintf(stderr, "[irc-bridge] LoadIrcConfig(%s) → irc_enabled=%d ws=%s:%d%s\n",
            cfg_path.c_str(), cfg.irc_enabled, cfg.ws_host.c_str(), cfg.ws_port, cfg.ws_path.c_str());
    return cfg;
}

// ============================================================
// AgentConfig 需要从 cfg 里读出来传进 TryHandleRegisteredMcpTool
// 最简单的方式：直接从 cfg_path 重新 parse AgentConfig
// ============================================================
static AgentConfig g_agent_cfg;

static void LoadAgentConfigForBridge(const std::string & cfg_path) {
    std::string err;
    if (!LoadAgentConfig(cfg_path, &g_agent_cfg, &err)) {
        fprintf(stderr, "[irc-bridge] ⚠️ LoadAgentConfig(%s) 失败: %s\n", cfg_path.c_str(), err.c_str());
    }
}

// ============================================================
// 工具：把长消息切到 IRC 上限
// ============================================================
static std::vector<std::string> SplitIrcOutput(
    const std::string & text, int max_chars,
    const std::string & continuation) {
    std::vector<std::string> out;
    std::string buf;
    // 先按 \n 切，再硬切
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if ((int)(buf.size() + 1 + line.size()) <= max_chars) {
            if (!buf.empty()) buf += "\n";
            buf += line;
        } else {
            if (!buf.empty()) out.push_back(buf);
            while ((int)line.size() > max_chars) {
                out.push_back(line.substr(0, max_chars));
                line = line.substr(max_chars);
            }
            buf = line;
        }
    }
    if (!buf.empty()) out.push_back(buf);
    if (out.size() > 1) {
        for (size_t i = 0; i + 1 < out.size(); ++i) out[i] += continuation;
    }
    return out;
}

// ============================================================
// 主循环
// ============================================================

static std::atomic<bool> g_stop_flag{false};
static std::thread *g_bridge_thread = nullptr;

// ============================================================
// Bridge 本地 status 查询处理（不走 MCP dispatch，完全离线可用）
// 覆盖：ping/alive、scope/范围、配置/endpoint 状态、帮助/routes
// ============================================================

// 最小 tcp 探针：200ms 超时，只 connect 不 send
static bool TcpProbe(const std::string & host, int port, int timeout_ms = 200) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        // DNS 解析 fallback
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0
            || res == nullptr) {
            close(fd);
            return false;
        }
        memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

// 判断 task 是否是**操作类问题**（有明确动作意图）
// 这类问题即使关键词里有 scope/status/help 也应该走 MCP dispatch（LLM 回答），
// 不应该被 local intent 硬编码回复截获
static bool IsOperationalQuery(const std::string & task) {
    static const char * ops[] = {
        "如何", "怎么", "怎样", "咋办",
        "切换", "更改", "修改", "设置", "配置", "调整", "改变",
        "启动", "停止", "重启", "运行", "执行", "打开", "关闭",
        "安装", "部署", "下载", "上传", "创建", "删除", "清理",
        "添加", "移除", "更新", "升级",
        "能", "可以", "能够", "行不行", "能不能",
        "workspace", "project", "目录", "路径",
        "scope", "endpoint", "模型", "endpoint"
    };
    for (const char * op : ops) {
        if (strcasestr(task.c_str(), op) != nullptr) return true;
    }
    return false;
}

// 判断 task 里是否包含任一关键词（大小写不敏感，中文直接匹配）
static bool TaskHasKeyword(const std::string & task,
                           std::initializer_list<const char *> keywords) {
    for (const char * kw : keywords) {
        if (strcasestr(task.c_str(), kw) != nullptr) return true;
    }
    return false;
}

// 发送一条 IRC PRIVMSG 到 channel（经 WS frame）
static bool SendChannelMsg(int fd, const std::string & channel,
                           const std::string & text, int max_chars,
                           const std::string & continuation) {
    // 全局锁保护：bridge 主循环 PING 保活 + dispatch 线程异步回复 + local intent
    std::lock_guard<std::mutex> lk(GetBridgeMutex());
    // 输出方向：sanitize 乱码 / 控制字符 / 非法 UTF-8
    std::string clean = SanitizeIrcText(text, max_chars * 3);
    auto chunks = SplitIrcOutput(clean, max_chars, continuation);
    for (const auto & c : chunks) {
        std::string irc_line = "PRIVMSG " + channel + " :" + c;
        auto frame = EncodeWsClientFrame(0x1, irc_line);
        size_t total = 0;
        while (total < frame.size()) {
            ssize_t n = send(fd, frame.data() + total,
                             frame.size() - total, 0);
            if (n <= 0) return false;
            total += (size_t)n;
        }
    }
    return true;
}

// ============================================================
// Phase 2: cfg 驱动路由引擎（Step 1 — ExecLocal + 变量替换）
// ============================================================

// 变量替换器：${var} 替换成实时值，${probe:h:p} 做 TCP connect 探针
static std::string SubstituteRouteVars(const std::string & tmpl,
                                       const IrcCfg & irc) {
    std::string out;
    out.reserve(tmpl.size());
    size_t i = 0;
    while (i < tmpl.size()) {
        if (i + 1 < tmpl.size() && tmpl[i] == '$' && tmpl[i + 1] == '{') {
            size_t end = tmpl.find('}', i + 2);
            if (end == std::string::npos) { out += tmpl[i++]; continue; }
            std::string expr = tmpl.substr(i + 2, end - i - 2);
            std::string val;
            if (expr.rfind("probe:", 0) == 0) {
                std::string rest = expr.substr(6);
                size_t colon = rest.rfind(':');
                if (colon != std::string::npos) {
                    int port = atoi(rest.substr(colon + 1).c_str());
                    val = TcpProbe(rest.substr(0, colon), port) ? "🟢" : "🔴";
                }
            } else if (expr == "workspace_root") val = g_agent_cfg.workspace_root;
            else if (expr == "irc.ws_host")   val = irc.ws_host;
            else if (expr == "irc.ws_port")   val = std::to_string(irc.ws_port);
            else if (expr == "irc.ws_path")   val = irc.ws_path;
            else if (expr == "irc.channel")   val = irc.channel;
            else if (expr == "irc.bot_nick")  val = irc.bot_nick;
            else if (expr == "mcp.dispatch")  val = GetMcpDispatch() ? "🟢" : "🔴";
            else val = "${" + expr + "}";  // 未知变量保留原文
            out += val;
            i = end + 1;
        } else {
            out += tmpl[i++];
        }
    }
    return out;
}

static bool MatchLocalRoute(const IrcCfg::Route & route, const std::string & task) {
    auto kw_it = route.params.find("keywords");
    if (kw_it == route.params.end()) return false;
    std::stringstream ss(kw_it->second);
    std::string kw;
    while (std::getline(ss, kw, ',')) {
        while (!kw.empty() && std::isspace((unsigned char)kw.front())) kw.erase(0, 1);
        while (!kw.empty() && std::isspace((unsigned char)kw.back())) kw.pop_back();
        if (!kw.empty() && TaskHasKeyword(task, {kw.c_str()})) return true;
    }
    return false;
}

static bool ExecLocalRoute(const IrcCfg::Route & route, int fd, const IrcCfg & irc) {
    auto rp_it = route.params.find("reply");
    if (rp_it == route.params.end()) return false;
    SendChannelMsg(fd, irc.channel, SubstituteRouteVars(rp_it->second, irc),
                   irc.output_max_chars, irc.line_continuation);
    return true;
}

static const IrcCfg::Route * MatchRouteTable(
    const IrcCfg & irc, const std::string & mention, const std::string & task) {
    if (irc.routes.empty()) return nullptr;
    // 优先级 1: 所有 routes 按 keywords 匹配（cfg 里写的顺序）
    // local / shell / codex_cli / llm 都走 keywords 触发
    for (const auto & [name, route] : irc.routes) {
        if (route.type.empty()) continue;
        if (MatchLocalRoute(route, task)) return &route;
    }
    // 优先级 2: mention 精确匹配 route name（如 @shell → irc_route.shell）
    auto it = irc.routes.find(mention);
    if (it != irc.routes.end()) return &it->second;
    // 优先级 3: default 兜底
    auto def = irc.routes.find("default");
    if (def != irc.routes.end()) return &def->second;
    return nullptr;
}

// ============================================================
// ExecLlm: Phase 2 Step 4 — bridge 直连 OpenAI ChatCompletions
// 完全绕开 lan-agent 的 RunLocalChat/MCP dispatch，bridge 自己当 agent loop
// payload 格式和 Lan-agent 的 BuildLocalChatFallbackBody() 保持一致
// ============================================================
static bool ExecLlmRoute(const IrcCfg::Route & route, int fd, const IrcCfg & irc,
                         const std::string & agent_label, const std::string & task,
                         const std::string & scope) {
    auto endpoint_it = route.params.find("endpoint");
    auto model_it    = route.params.find("model");
    auto system_it   = route.params.find("system");
    if (endpoint_it == route.params.end() || model_it == route.params.end()) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ llm route 缺少 endpoint 或 model 配置",
            irc.output_max_chars, irc.line_continuation);
        return false;
    }
    std::string endpoint = endpoint_it->second;
    std::string model    = model_it->second;
    std::string system_prompt = (system_it != route.params.end() && !system_it->second.empty())
        ? system_it->second
        : "You are seedcode-bridge IRC agent. Answer concisely (≤200 chars). Use Chinese if the question is in Chinese.";
    int timeout_ms = 15000;
    auto to_it = route.params.find("timeout_ms");
    if (to_it != route.params.end()) timeout_ms = atoi(to_it->second.c_str());

    // 组 OpenAI ChatCompletions payload（和 Lan-agent BuildLocalChatFallbackBody 格式一致）
    std::ostringstream body;
    body << "{"
         << "\"model\":\"" << codex_lan_agent::JsonEscape(model) << "\","
         << "\"temperature\":0,"
         << "\"messages\":["
         << "{\"role\":\"system\",\"content\":\"" << codex_lan_agent::JsonEscape(system_prompt) << "\"},"
         << "{\"role\":\"user\",\"content\":\"scope=" << codex_lan_agent::JsonEscape(scope)
         << "\\nquestion=" << codex_lan_agent::JsonEscape(task) << "\"}"
         << "]"
         << "}";
    std::string body_str = body.str();

    fprintf(stderr, "[irc-bridge] [route-llm] POST %s model=%s scope=%s\n",
            endpoint.c_str(), model.c_str(), scope.c_str());

    // —— 异步 worker（和 MCP dispatch 同样的模式，不阻塞主循环）——
    std::atomic<bool> llm_done(false);
    codex_lan_agent::HttpResponse llm_resp;
    auto t0 = std::chrono::steady_clock::now();

    std::thread llm_worker([&]() {
        try {
            llm_resp = codex_lan_agent::PostJson(endpoint, body_str, timeout_ms);
        } catch (...) {
            llm_resp.ok = false;
            llm_resp.error_message = "exception in llm POST";
        }
        llm_done = true;
    });

    // 主线程每 5s 发进度（和 MCP dispatch 同样的节流）
    int elapsed_s = 0;
    while (!llm_done) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        elapsed_s += 5;
        if (!llm_done && elapsed_s <= 30) {
            SendChannelMsg(fd, irc.channel,
                "[" + agent_label + "] ⏳ LLM 执行中…（已用时 " + std::to_string(elapsed_s) + "s）",
                irc.output_max_chars, irc.line_continuation);
        }
    }
    llm_worker.join();

    auto t1 = std::chrono::steady_clock::now();
    int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    fprintf(stderr, "[irc-bridge] [route-llm] done in %dms ok=%d status=%d\n",
            total_ms, (int)llm_resp.ok, llm_resp.status_code);

    if (!llm_resp.ok) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ LLM endpoint unreachable (" + endpoint + "): "
            + (llm_resp.error_message.empty() ? "timeout" : llm_resp.error_message),
            irc.output_max_chars, irc.line_continuation);
        return false;
    }

    // 从 OpenAI 响应里抽 choices[0].message.content
    // 用简单的字符串扫描（bridge 不依赖完整 JSON parser，保持 zero-dependency）
    std::string content;
    const std::string & rb = llm_resp.body;
    // 找 "content":"..."（可能跨多行，找第一个 "content" 的值）
    size_t ci = rb.find("\"content\"");
    if (ci != std::string::npos) {
        size_t colon = rb.find(':', ci);
        size_t qstart = rb.find('"', colon + 1);
        if (qstart != std::string::npos) {
            // 扫描到配对的未转义引号
            size_t i = qstart + 1;
            while (i < rb.size()) {
                char c = rb[i];
                if (c == '\\' && i + 1 < rb.size()) {
                    switch (rb[i+1]) {
                        case 'n': content += '\n'; break;
                        case 't': content += '\t'; break;
                        case 'r': content += '\r'; break;
                        case '"': content += '"';  break;
                        case '\\': content += '\\'; break;
                        default: content += rb[i+1]; break;
                    }
                    i += 2;
                } else if (c == '"') {
                    break;  // 配对结束
                } else {
                    content += c;
                    i++;
                }
            }
        }
    }
    if (content.empty()) content = "(LLM 返回空)";
    if (content.size() > 200) content = content.substr(0, 200) + "…";

    SendChannelMsg(fd, irc.channel,
        "[" + agent_label + "] ✅ " + content,
        irc.output_max_chars, irc.line_continuation);
    return true;
}

// ============================================================
// ExecShellRoute: ctx-cli loop 核心实现
// popen 执行 cfg 配好的 shell 命令模板（grep/find/cat/git/ls 等）
// 完全零依赖：不调 LLM、不走 MCP、不连任何网络服务
// IRC 查询 → 提取关键词 → 替换 ${keywords} → popen 执行 → 格式化回频道
// ============================================================
static std::string ExtractKeywordsFromTask(const std::string & task) {
    static const char * kStopWords[] = {
        "的","了","在","是","我","你","他","她","它","这","那","有","和","与","或",
        "不","没","也","都","就","才","要","想","会","能","可","可以","怎么","如何",
        "什么","哪些","哪个","哪里","为什么","因为","但是","如果","虽然","不过",
        "how","what","why","where","when","who","which","and","or","the","a","an",
        "is","are","was","were","be","been","being","do","does","did","have","has",
        "can","could","should","would","may","might","will","shall","to","of","in",
        "for","on","with","at","by","from","as","into","about","up","down","out",
        nullptr
    };
    std::set<std::string> stop_set;
    for (int i = 0; kStopWords[i]; i++) stop_set.insert(kStopWords[i]);

    std::vector<std::string> tokens;
    std::string cur;
    for (char c : task) {
        if (std::isalnum((unsigned char)c) || (unsigned char)c >= 0x80) {
            cur += c;
        } else if (!cur.empty()) {
            if (cur.size() >= 2 && stop_set.find(cur) == stop_set.end()) {
                tokens.push_back(cur);
            }
            cur.clear();
        }
    }
    if (cur.size() >= 2 && stop_set.find(cur) == stop_set.end()) tokens.push_back(cur);

    std::string result;
    int count = 0;
    for (auto & t : tokens) {
        if (count++ >= 5) break;
        if (!result.empty()) result += " ";
        result += t;
    }
    return result.empty() ? task.substr(0, 20) : result;
}

static bool ExecShellRoute(const IrcCfg::Route & route, int fd, const IrcCfg & irc,
                           const std::string & agent_label, const std::string & task,
                           const std::string & scope) {
    // —— 分层权限控制 ——
    // exec_perm: 0=只读(grep/find/cat), 1=可执行(make/test), 2=危险(write/rm/chmod)
    // 安全策略：
    //   1. exec_perm=0  → 直接放行（默认，只读命令全开放）
    //   2. exec_perm≥1  → 需要 exec_key 非空 + task 里带 "!<key>" 或 cfg 留空手动确认
    //   3. exec_perm≥2  → 额外需要二次确认（IRC 回复确认，用户回复 yes 才执行）
    if (route.exec_perm >= 2) {
        // 危险操作：先回复 warning + 等待二次确认
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ⚠️ 危险操作（exec_perm=2）: " + task + " — 需要二次确认 (回复 yes 执行)",
            irc.output_max_chars, irc.line_continuation);
        // TODO: 后续实现 timeout + 从主循环 state 里读用户确认
        return true;  // 本次先不执行（框架预留）
    }
    if (route.exec_perm >= 1 && !route.exec_key.empty()) {
        // 可执行操作：校验 task 里的 auth key
        bool ok = false;
        // 格式 1: "!<key> <task>"
        if (task.rfind("!", 0) == 0) {
            auto sp = task.find(' ');
            std::string given = (sp == std::string::npos) ? task.substr(1) : task.substr(1, sp - 1);
            ok = (given == route.exec_key);
        }
        // 格式 2: "key=<key>"
        if (!ok) {
            auto pos = task.find("key=");
            if (pos != std::string::npos) {
                auto sp = task.find(' ', pos);
                std::string given = (sp == std::string::npos) ? task.substr(pos + 4) : task.substr(pos + 4, sp - pos - 4);
                ok = (given == route.exec_key);
            }
        }
        if (!ok) {
            SendChannelMsg(fd, irc.channel,
                "[" + agent_label + "] ❌ auth 失败（需要 !<key> 或 key=<key>）",
                irc.output_max_chars, irc.line_continuation);
            return true;
        }
    }
    // exec_perm=0 或 exec_perm≥1 但 exec_key 为空 → 放行（便于测试）

    auto cmd_it = route.params.find("command");
    if (cmd_it == route.params.end()) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ shell route 缺少 command 配置",
            irc.output_max_chars, irc.line_continuation);
        return false;
    }
    std::string cmd_tpl = cmd_it->second;
    bool kw_from_task = true;
    auto kw_it = route.params.find("keywords_from_task");
    if (kw_it != route.params.end()) kw_from_task = (kw_it->second != "false");

    std::string command = cmd_tpl;
    std::string kw = kw_from_task ? ExtractKeywordsFromTask(task) : "";
    std::string kw_safe;
    for (char c : kw) {
        if (std::string("`$\\;|&\"'").find(c) != std::string::npos) kw_safe += '\\';
        kw_safe += c;
    }
    // 额外生成正则 OR 版本（grep -E 用 | 拼接多关键词）
    std::string kw_regex;
    {
        std::string cur_kw;
        for (char c : kw) {
            if (c == ' ') {
                if (!cur_kw.empty()) {
                    if (!kw_regex.empty()) kw_regex += "|";
                    kw_regex += cur_kw;
                    cur_kw.clear();
                }
            } else {
                // grep -E 特殊字符转义
                if (std::string(".+*?[]{}()^$|\\\\").find(c) != std::string::npos) cur_kw += '\\';
                cur_kw += c;
            }
        }
        if (!cur_kw.empty()) {
            if (!kw_regex.empty()) kw_regex += "|";
            kw_regex += cur_kw;
        }
    }
    auto replace = [&](const std::string & from, const std::string & to) {
        size_t pos = 0;
        while ((pos = command.find(from, pos)) != std::string::npos) {
            command.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace("${scope}", scope);
    replace("${keywords}", kw_safe.empty() ? "''" : kw_safe);
    replace("${keywords_regex}", kw_regex.empty() ? "." : kw_regex);
    // ${keywords_glob} — 原始空格分隔关键词（find -name glob 用，不做 grep 正则转义）
    std::string kw_glob;
    for (char c : kw) {
        // glob 需要转义的只有：\ 和 "
        if (std::string("\\").find(c) != std::string::npos) kw_glob += '\\';
        kw_glob += c;
    }
    replace("${keywords_glob}", kw_glob.empty() ? "*" : kw_glob);
    replace("${task}", task);

    fprintf(stderr, "[irc-bridge] [route-shell] cmd=%s\n", command.c_str());

    std::string output;
    FILE * pipe = popen(command.c_str(), "r");
    if (!pipe) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ shell popen 失败",
            irc.output_max_chars, irc.line_continuation);
        return false;
    }
    char buf[4096];
    size_t total = 0;
    size_t max_bytes = 4096;
    while (fgets(buf, sizeof(buf), pipe) && total < max_bytes) {
        size_t n = strlen(buf);
        if (total + n > max_bytes) n = max_bytes - total;
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!output.empty() && output.back() != ' ' && output.size() < max_bytes - 3) {
                    output += " | ";
                }
            } else {
                output += c;
            }
        }
        total += n;
    }
    int status = pclose(pipe);

    if (output.empty()) output = "(命令无输出)";
    if (output.size() > (size_t)irc.output_max_chars) {
        output = output.substr(0, irc.output_max_chars - 3) + "...";
    }

    SendChannelMsg(fd, irc.channel,
        "[" + agent_label + "] ✅ " + output,
        irc.output_max_chars, irc.line_continuation);
    fprintf(stderr, "[irc-bridge] [route-shell] done status=%d output_chars=%zu\n",
            status, output.size());
    return true;
}

// ============================================================
// ExecCodexCliRoute: codex_lan_agent CLI 模式
// popen 调 codex_lan_agent run-rag-flow / run-cli-profile / health
// 参数完全来自 cfg route.params
// ============================================================
static bool ExecCodexCliRoute(const IrcCfg::Route & route, int fd, const IrcCfg & irc,
                              const std::string & agent_label, const std::string & task,
                              const std::string & scope) {
    // —— 认证检查（预留）—— 同 ExecShellRoute
    (void)route.exec_key;
    (void)route.exec_perm;

    auto sub_it = route.params.find("command");
    if (sub_it == route.params.end()) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ codex_cli route 缺少 command 配置",
            irc.output_max_chars, irc.line_continuation);
        return false;
    }
    auto bin_it = route.params.find("binary");
    std::string binary = (bin_it != route.params.end())
        ? bin_it->second
        : "/H/aisrc/lan-agent/build/codex_lan_agent";
    std::string cfg_path = irc.cfg_path;
    std::string subcmd = sub_it->second;
    std::string arg_template;
    auto arg_it = route.params.find("args");
    if (arg_it != route.params.end()) arg_template = arg_it->second;

    std::string kw = ExtractKeywordsFromTask(task);
    std::string args = arg_template;
    if (args.empty() && subcmd == "run-rag-flow") args = "${keywords} code_analysis";

    auto replace = [&](const std::string & from, const std::string & to) {
        size_t pos = 0;
        while ((pos = args.find(from, pos)) != std::string::npos) {
            args.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace("${scope}", scope);
    replace("${keywords}", kw);
    replace("${task}", task);

    std::string command = binary + " --config " + cfg_path + " " + subcmd;
    if (!args.empty()) command += " " + args;
    command += " 2>/dev/null";

    fprintf(stderr, "[irc-bridge] [route-codex-cli] cmd=%s\n", command.c_str());

    std::string output;
    FILE * pipe = popen(command.c_str(), "r");
    if (!pipe) {
        SendChannelMsg(fd, irc.channel,
            "[" + agent_label + "] ❌ codex_cli popen 失败",
            irc.output_max_chars, irc.line_continuation);
        return false;
    }
    char buf[4096];
    size_t total = 0;
    size_t max_bytes = 4096;
    while (fgets(buf, sizeof(buf), pipe) && total < max_bytes) {
        size_t n = strlen(buf);
        if (total + n > max_bytes) n = max_bytes - total;
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!output.empty() && output.back() != ' ' && output.size() < max_bytes - 3) {
                    output += " | ";
                }
            } else {
                output += c;
            }
        }
        total += n;
    }
    int status = pclose(pipe);

    if (output.empty()) output = "(命令无输出)";
    if (output.size() > (size_t)irc.output_max_chars) {
        output = output.substr(0, irc.output_max_chars - 3) + "...";
    }

    SendChannelMsg(fd, irc.channel,
        "[" + agent_label + "] ✅ " + output,
        irc.output_max_chars, irc.line_continuation);
    fprintf(stderr, "[irc-bridge] [route-codex-cli] done status=%d\n", status);
    return true;
}

// ============================================================
// Bridge 本地 status 查询处理（不走 MCP dispatch，完全离线可用）
// 覆盖：ping/alive、scope/范围、配置/endpoint 状态、帮助/routes
// ============================================================
static bool TryHandleBridgeLocalIntent(
    int fd, const IrcCfg & irc, const std::string & from_nick,
    const std::string & mention, const std::string & task) {

    std::string agent_label = mention + "-agent";

    // ---- Intent 1: ping / alive ----
    if (TaskHasKeyword(task, {"ping", "alive", "hello", "hi",
                              "在吗", "活着", "在线", "你好", "活着吗"})) {
        std::string reply = "[" + agent_label + "] 🏓 pong — bridge 在线（ws→"
                          + irc.ws_host + ":" + std::to_string(irc.ws_port)
                          + irc.ws_path + "，#" + irc.channel + "）";
        SendChannelMsg(fd, irc.channel, reply, irc.output_max_chars, irc.line_continuation);
        return true;
    }

    // ---- Intent 2: scope / 关联范围 ----
    if (TaskHasKeyword(task, {"关联", "范围", "scope", "项目还是全局",
                              "针对", "哪个项目", "项目", "全局", "隔离", "多项目"})) {
        std::ostringstream reply;
        reply << "[" << agent_label << "] 📁 scope = 项目级 (workspace_root="
              << g_agent_cfg.workspace_root << ")";
        SendChannelMsg(fd, irc.channel, reply.str(), irc.output_max_chars, irc.line_continuation);
        return true;
    }

    // ---- Intent 3: 配置 / endpoint 状态 ----
    // 注意："模型"作为单独关键词太泛（用户问"本地模型效果"也会触发），
    // 必须搭配更明确的限定词
    if (TaskHasKeyword(task, {"配置", "cfg", "endpoint", "llm", "provider",
                              "本地chat", "本地模型可用", "本地模型状态",
                              "8080", "8095", "8096",
                              "能用", "可用", "状态", "status"})) {
        std::ostringstream reply;
        reply << "[" << agent_label << "] ⚙️ 运行状态:";
        // LLM endpoints
        struct EpInfo { const char * name; const std::string & url; };
        std::vector<EpInfo> eps = {
            {"local_chat", g_agent_cfg.local_chat_endpoint},
            {"generation", g_agent_cfg.generation_endpoint},
            {"embedding", g_agent_cfg.embedding_endpoint},
        };
        for (const auto & ep : eps) {
            if (ep.url.empty()) continue;
            std::string host = "127.0.0.1";
            int port = 80;
            size_t p1 = ep.url.find("://");
            size_t p2 = ep.url.find('/', p1 == std::string::npos ? 0 : p1 + 3);
            std::string host_port = (p1 == std::string::npos ? ep.url : ep.url.substr(p1 + 3,
                (p2 == std::string::npos ? ep.url.size() : p2) - (p1 + 3)));
            size_t colon = host_port.find(':');
            if (colon != std::string::npos) {
                host = host_port.substr(0, colon);
                port = atoi(host_port.substr(colon + 1).c_str());
            } else {
                host = host_port;
            }
            bool up = TcpProbe(host, port);
            // IRC PRIVMSG 单行友好：用 " | " 分隔而非 "\n"
            reply << " | " << ep.name << "(" << host_port << "):"
                  << (up ? "🟢" : "🔴");
        }
        reply << " | bridge(ws://" << irc.ws_host << ":" << irc.ws_port
              << irc.ws_path << "/#" << irc.channel << "):🟢";
        reply << " | MCP dispatch:" << (GetMcpDispatch() ? "🟢" : "🔴");
        SendChannelMsg(fd, irc.channel, reply.str(), irc.output_max_chars, irc.line_continuation);
        return true;
    }

    // ---- Intent 4: help / 怎么用 / routes ----
    if (TaskHasKeyword(task, {"帮助", "help", "怎么用", "怎么触发",
                              "哪些命令", "支持什么", "routes", "命令",
                              "触发", "用法", "使用"})) {
        std::ostringstream reply;
        reply << "[" << agent_label << "] 🧭 用法 | @<mention> <任务>→MCP | ping/scope/status/help→本地 | routes:";
        bool first = true;
        for (const auto & [mention_kw, method] : irc.agent_routes) {
            if (reply.str().size() > 350) break;
            reply << (first ? " " : ", ") << "@" << mention_kw;
            first = false;
        }
        SendChannelMsg(fd, irc.channel, reply.str(), irc.output_max_chars, irc.line_continuation);
        return true;
    }

    // ---- 未命中本地 intent ----
    return false;
}

// PRIVMSG 正则匹配
// 形如 :alice!user@host PRIVMSG #main :@codex 分析 main.cpp 注释
static const std::regex kPrivmsgRe(
    R"(^:([^!]+)!\S+\s+PRIVMSG\s+(\S+)\s+:(.*)$)");
static const std::regex kMentionRe(
    R"(^@([\w-]+)\s+(.+)$)");

static void thread_main(const std::string & cfg_path) {
    // 1. 读配置
    IrcCfg irc = LoadIrcConfig(cfg_path);
    if (!irc.irc_enabled) {
        fprintf(stderr, "[irc-bridge] irc_enabled=false，跳过启动\n");
        return;
    }

    // 2. 读 AgentConfig（MCP dispatch 需要）
    LoadAgentConfigForBridge(cfg_path);

    fprintf(stderr,
        "[irc-bridge] 启动 → ws://%s:%d nick=%s user=%s channel=%s\n",
        irc.ws_host.c_str(), irc.ws_port,
        irc.bot_nick.c_str(), irc.bot_user.c_str(), irc.channel.c_str());

    int backoff = 3;
    while (!g_stop_flag) {
        // ——— 连接阶段 ———
        fprintf(stderr, "[irc-bridge] 尝试连接 ws://%s:%d ...\n",
                irc.ws_host.c_str(), irc.ws_port);
        int fd = WsConnect(irc.ws_host, irc.ws_port, irc.ws_path, 5000);
        if (fd < 0) {
            fprintf(stderr, "[irc-bridge] WS 连接失败，%ds 后重连\n", backoff);
            sleep(backoff);
            backoff = std::min(backoff * 2, 60);
            continue;
        }
        backoff = 3;

        // ——— IRC 握手 ———
        SendIrcLine(fd, "NICK " + irc.bot_nick);
        SendIrcLine(fd, "USER " + irc.bot_user + " 0 * :" + irc.bot_user);

        // 等 welcome (001) 再 JOIN
        RecvBuffer rb;
        std::string text_buf;
        bool got_welcome = false;
        time_t t0 = time(nullptr);
        while (!got_welcome && !g_stop_flag &&
               (time(nullptr) - t0) < 10) {
            if (!rb.ReadTextFrame(fd, &text_buf)) break;
            auto lines = SplitIrcLines(text_buf);
            for (auto & l : lines) {
                // PING 单独处理（纯 IRC line，不走 WS frame 内容）
                // 实际上 ReadTextFrame 已经把 frame payload 拿出来了，
                // payload 就是 IRC 协议行，PING 会表现为 "PING :server"
                if (l.rfind("PING ", 0) == 0) {
                    SendIrcLine(fd, "PONG " + l.substr(5));
                }
                if (l.find(" 001 ") != std::string::npos) {
                    got_welcome = true;
                    break;
                }
            }
        }

        if (!got_welcome) {
            fprintf(stderr, "[irc-bridge] 没收到 welcome，断开重试\n");
            close(fd);
            sleep(backoff);
            backoff = std::min(backoff * 2, 60);
            continue;
        }

        SendIrcLine(fd, "JOIN " + irc.channel);
        fprintf(stderr,
            "[irc-bridge] ✅ 已上线，监听 %s\n", irc.channel.c_str());

        // 暂时注释掉 system 公告和 WHO，先测试保活
        // SendChannelMsg(fd, irc.channel, ...);

        // ——— 主 recv loop ———
        time_t last_activity = time(nullptr);
        while (!g_stop_flag) {
            if (!rb.ReadTextFrame(fd, &text_buf)) {
                // close 或错误
                fprintf(stderr, "[irc-bridge] WS 断开，重连...\n");
                break;
            }
            fprintf(stderr, "[irc-bridge] ReadTextFrame got %zu bytes\n", text_buf.size());
            time_t now = time(nullptr);
            // 主动保活：20s 没活动就发 IRC PING
            if (now - last_activity >= 20) {
                fprintf(stderr, "[irc-bridge] → 主动 PING 保活\n");
                SendIrcLine(fd, "PING " + irc.bot_nick);
                last_activity = now;
            }
            if (text_buf.empty()) continue;
            last_activity = now;  // 收到数据 = 有活动

            auto lines = SplitIrcLines(text_buf);
            for (auto & l : lines) {
                // PING 保活（IRC 层）
                if (l.rfind("PING ", 0) == 0) {
                    SendIrcLine(fd, "PONG " + l.substr(5));
                    continue;
                }
                // DEBUG：打印所有收到的 IRC 行（被 split 后）
                fprintf(stderr, "[irc-bridge] recv: %s\n", l.c_str());
                // 只处理目标频道的 PRIVMSG
                std::smatch m;
                if (!std::regex_match(l, m, kPrivmsgRe)) continue;
                if (m[2] != irc.channel) continue;

                // 输入方向：sanitize 乱码 / 控制字符 / 非法 UTF-8 后再处理
                std::string from_nick = SanitizeIrcText(m[1]);
                std::string message   = SanitizeIrcText(m[3], 1024);

                // 跳过自己发的消息（防反馈环：自己发的回复不会再被处理）
                if (from_nick == irc.bot_nick) continue;

                // 找 @mention
                std::smatch mm;
                if (!std::regex_match(message, mm, kMentionRe)) continue;
                std::string mention = mm[1];
                std::string task    = mm[2];

                // ---- Phase 2: RouteTable 优先（cfg 驱动，新路径）----
                // cfg 里写了 irc_route.* 时，RouteTable 优先级最高
                // 没命中 → 向后兼容：fallback 到 legacy 路径
                // ⚠️ 操作类问题（如何/怎么/切换/修改...）**跳过所有 local intent**，
                //    即使关键词匹配也走 MCP dispatch——操作类问题需要 LLM 回答，
                //    不是 bridge 硬编码能处理的
                bool skip_local_intent = IsOperationalQuery(task);

                // 提前算好（RouteTable 分支里 ExecLlmRoute 也需要）
                std::string agent_label = mention + "-agent";
                std::string scope_dir = g_agent_cfg.workspace_root;

                if (!irc.routes.empty()) {
                    const IrcCfg::Route * route = MatchRouteTable(irc, mention, task);
                    if (route) {
                        if (route->type == "local") {
                            ExecLocalRoute(*route, fd, irc);
                            fprintf(stderr,
                                "[irc-bridge] [route-local] @%s ←「%s」\n",
                                from_nick.c_str(), task.substr(0, 40).c_str());
                            continue;
                        } else if (route->type == "llm") {
                            SendChannelMsg(fd, irc.channel,
                                "[" + agent_label + "] ⏳ @" + from_nick + " 已收到「" + task + "」（LLM 直连）",
                                irc.output_max_chars, irc.line_continuation);
                            ExecLlmRoute(*route, fd, irc, agent_label, task, scope_dir);
                            fprintf(stderr,
                                "[irc-bridge] [route-llm] @%s ←「%s」\n",
                                from_nick.c_str(), task.substr(0, 40).c_str());
                            continue;
                        } else if (route->type == "shell") {
                            ExecShellRoute(*route, fd, irc, agent_label, task, scope_dir);
                            fprintf(stderr,
                                "[irc-bridge] [route-shell] @%s ←「%s」\n",
                                from_nick.c_str(), task.substr(0, 40).c_str());
                            continue;
                        } else if (route->type == "codex_cli") {
                            ExecCodexCliRoute(*route, fd, irc, agent_label, task, scope_dir);
                            fprintf(stderr,
                                "[irc-bridge] [route-codex-cli] @%s ←「%s」\n",
                                from_nick.c_str(), task.substr(0, 40).c_str());
                            continue;
                        }
                        // Step 2-3: type=mcp / http 暂 fallback 到 legacy 路径
                        // （RouteTable 已匹配但 type 未实现 → 用 mention 查 agent_routes）
                    }
                }

                // ---- Legacy: hardcoded local intent handler ----
                // 老 cfg（没写 irc_route.*）+ RouteTable 未命中时走这里
                if (!skip_local_intent && TryHandleBridgeLocalIntent(fd, irc, from_nick, mention, task)) {
                    fprintf(stderr,
                        "[irc-bridge] [local-intent] @%s ←「%s」→ 本地回答\n",
                        from_nick.c_str(), task.substr(0, 40).c_str());
                    continue;
                }

                // 路由到 MCP method（先查 agent_routes 精确匹配，找不到用 "default"）
                auto route_it = irc.agent_routes.find(mention);
                std::string mcp_method;
                if (route_it != irc.agent_routes.end()) {
                    mcp_method = route_it->second;
                } else {
                    auto def_it = irc.agent_routes.find("default");
                    mcp_method = (def_it != irc.agent_routes.end())
                                 ? def_it->second
                                 : "lan_agent_run_local_chat";
                }

                fprintf(stderr,
                    "[irc-bridge] [%s] @%s ←「%s」 → %s\n",
                    agent_label.c_str(), from_nick.c_str(),
                    task.substr(0, 60).c_str(), mcp_method.c_str());

                // ACK
                SendChannelMsg(fd, irc.channel,
                    "[" + agent_label + "] ⏳ @" + from_nick +
                    " 已收到「" + task.substr(0, 40) + "」",
                    irc.output_max_chars, irc.line_continuation);

                // 构造 MCP params（不同 method 不同字段）
                // scope 用 g_agent_cfg.workspace_root（真实目录路径），这样当 LLM endpoint
                // 不可用时能走 deterministic directory fallback，不至于直接 exit=42
                // scope_dir 已在 RouteTable 分支前定义
                std::string params_json;
                if (mcp_method == "lan_agent_run_local_chat") {
                    params_json = "{\"scope\":\"" + scope_dir
                                  + "\",\"question\":\"" + task + "\"}";
                } else if (mcp_method == "lan_agent_run_cxparser_flow") {
                    params_json = "{\"flow_id\":\"auto_detect\",\"task\":\""
                                  + task + "\"}";
                } else if (mcp_method == "lan_agent_rag_review_observe") {
                    params_json = "{\"scope\":\"" + scope_dir
                                  + "\",\"query\":\"" + task + "\"}";
                } else {
                    params_json = "{\"task\":\"" + task + "\"}";
                }

                // 调用 MCP（异步 dispatch + 主线程定期发进度提示）
                if (GetMcpDispatch() == nullptr) {
                    SendChannelMsg(fd, irc.channel,
                        "[" + agent_label + "] ❌ MCP dispatch handler 未注册",
                        irc.output_max_chars, irc.line_continuation);
                    continue;
                }

                // —— 异步 dispatch：worker 线程执行，主线程每 5s 发进度 ——
                std::atomic<bool> dispatch_ready(false);
                CommandResult result;
                bool handled = false;
                JsonRequestView params_view(params_json);
                auto t0 = std::chrono::steady_clock::now();

                std::thread dispatch_worker([&]() {
                    try {
                        handled = GetMcpDispatch()(
                            g_agent_cfg, mcp_method, params_view, &result);
                    } catch (...) {
                        handled = false;
                    }
                    dispatch_ready = true;
                });

                // 主线程：每 5s 检查一次，dispatch 完成后退出
                int elapsed_s = 0;
                while (!dispatch_ready) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    elapsed_s += 5;
                    if (!dispatch_ready && elapsed_s <= 30) {  // 最多发 6 次（5/10/15/20/25/30）
                        std::string progress = "[" + agent_label + "] ⏳ @"
                            + from_nick + " 执行中…（已用时 " + std::to_string(elapsed_s) + "s）";
                        SendChannelMsg(fd, irc.channel, progress,
                                       irc.output_max_chars, irc.line_continuation);
                    }
                }
                dispatch_worker.join();

                // dispatch 完成后发总耗时提示（>5s 才发，否则太吵）
                auto t1 = std::chrono::steady_clock::now();
                int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                fprintf(stderr, "[irc-bridge] [%s] dispatch done in %dms (handled=%d)\n",
                        agent_label.c_str(), total_ms, (int)handled);

                if (!handled) {
                    SendChannelMsg(fd, irc.channel,
                        "[" + agent_label + "] ❌ 未注册的 MCP method: " + mcp_method,
                        irc.output_max_chars, irc.line_continuation);
                    continue;
                }

                // 构造回复 —— 只提取结论性字段，排除内部调试细节
                // 成功 (exit_code==0 && ok==true)
                //   → 优先 direct_answer / body / summary（LLM 直接输出）
                //   → 其次 summary / result（结论标签）
                // 失败
                //   → summary / error（一句话错误）
                //   → 附带 exit_code
                auto get_field = [&](const char * k) -> std::string {
                    auto it = result.fields.find(k);
                    if (it == result.fields.end()) return {};
                    return it->second;
                };

                const bool success = result.ok && result.exit_code == 0;
                std::ostringstream summary;
                summary << "[" << agent_label << "] "
                        << (success ? "✅ " : "❌ ");

                if (success) {
                    // 成功：优先 LLM 直接输出，其次结论摘要
                    std::string body = get_field("direct_answer");
                    if (body.empty()) body = get_field("body");
                    if (body.empty()) body = get_field("summary");
                    if (body.empty()) body = get_field("result");
                    if (body.empty()) body = "ok";

                    // 截取首 200 字符作为 IRC 回复（足够看结论）
                    if (body.size() > 200) {
                        summary << body.substr(0, 200) << "…";
                    } else {
                        summary << body;
                    }

                    // 附加简短 next_action（如果存在且够短）
                    std::string next = get_field("next_action");
                    if (!next.empty() && next.size() < 60) {
                        summary << " → " << next;
                    }
                } else {
                    // 失败：最核心的一句话 + exit_code
                    std::string err = get_field("summary");
                    if (err.empty()) err = get_field("error");
                    if (err.empty()) err = get_field("failure_mode");
                    if (err.empty()) err = "unknown failure";

                    // 只取第一个分句（到句号或换行前）
                    size_t cut = err.find_first_of("\n。.！!");
                    if (cut != std::string::npos && cut > 3) err = err.substr(0, cut);

                    summary << err;
                    if (result.exit_code != 0) {
                        summary << " (exit=" << result.exit_code << ")";
                    }
                }

                SendChannelMsg(fd, irc.channel, summary.str(),
                               irc.output_max_chars, irc.line_continuation);
            }
        }
        close(fd);
    }
    fprintf(stderr, "[irc-bridge] 退出\n");
}

// ============================================================
// 入口
// ============================================================

void SetIrcMcpToolHandler(McpToolDispatchFn fn) {
    GetMcpDispatch() = fn;
    fprintf(stderr, "[irc-bridge] MCP dispatch handler %s\n",
            fn ? "✅ 已注册" : "⚠️ 注册为 null");
}

void startIrcBridgeThread(const std::string & cfg_path) {
    if (g_bridge_thread) return;
    g_stop_flag = false;
    g_bridge_thread = new std::thread(thread_main, cfg_path);
}

void stopIrcBridgeThread() {
    g_stop_flag = true;
    if (g_bridge_thread) {
        g_bridge_thread->join();
        delete g_bridge_thread;
        g_bridge_thread = nullptr;
    }
}

}  // namespace codex_lan_agent
