#pragma once

#include <string>
#include <unordered_map>

#ifndef CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#define CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketErrorResult = SOCKET_ERROR;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSocketErrorResult = -1;
#endif
#endif

struct CommandResult {
    int exit_code = 0;
    bool ok = true;
    std::unordered_map<std::string, std::string> fields;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponseSpec {
    int status_code = 200;
    std::string status_text = "OK";
    std::string body = "{}";
    std::string content_type = "application/json";
    std::unordered_map<std::string, std::string> headers;
};

enum class TaskKind {
    kCliProfile,
    kCxParserRuntime,
    kCase,
    kRagFlow,
    kLocalChat
};

struct TaskRecord {
    std::string task_id;
    TaskKind kind = TaskKind::kCliProfile;
    std::string arg1;
    std::string arg2;
    std::string pending_log_path;
    std::string resource_key;
    int timeout_sec_override = -1;
    int stall_timeout_sec_override = -1;
    std::string status = "queued";
    std::string submitted_at;
    std::string started_at;
    std::string completed_at;
    CommandResult result;
};

struct PatchRequest {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string old_hash;
    std::string new_content;
    std::string reason;
};

struct PatchPreviewResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string patch_audit_id;
};

struct PatchApplyResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string backup_path;
    std::string patch_audit_id;
};

struct PatchRevertResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string backup_path;
    std::string patch_audit_id;
};

struct PatchVerifyResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string verify_id;
    std::string file_path;
    std::string normalized_path;
    std::string expected_hash;
    std::string actual_hash;
    std::string semantic_outcome;
    std::string repair_candidate_id;
};

// ────────────────────────────────────────────────────────────────────────────
// Model-Profile 模型剖面：按 LLM 能力/特性差异化约束网关行为
// ────────────────────────────────────────────────────────────────────────────
enum class ModelProfileId {
    kSmallLlm,   // 弱本地小模型（gemma-4、Llama.cpp）：依赖网关代偿规划，强门禁，大粒度快照
    kLargeLlm,   // 强闭源大模型（Codex）：自主规划能力强，适度放开但加强步骤完整性校验
    kCustom      // 自定义：由外部配置覆盖每项参数
};

enum class GateStrictnessLevel {
    kRelaxed,   // large-llm 基线：仅拦截显式风险
    kStandard,  // 默认
    kStrict     // small-llm 基线：保守强拦截
};

enum class OverviewRouteAggressiveness {
    kConservative,  // small-llm：强代偿，自动路由激进
    kBalanced,      // 默认
    kMinimal        // large-llm：关闭网关规划代偿，仅做安全守卫
};

struct ModelProfileConfig {
    ModelProfileId id = ModelProfileId::kSmallLlm;
    std::string name = "small-llm";

    // ── 开关1：是否允许直接进入 call 模式（绕过overview/route自动推理） ──
    bool allow_direct_call_mode = true;

    // ── 开关2：overview自动路由的激进程度 ──
    OverviewRouteAggressiveness route_aggressiveness = OverviewRouteAggressiveness::kConservative;

    // ── 开关3：四重完成门禁校验严格等级 ──
    GateStrictnessLevel gate_strictness = GateStrictnessLevel::kStrict;

    // ── 开关4：语义网格Bundle输出字符上限（对应不同上下文窗口）──
    int semantic_bundle_max_chars = 6000;
    int semantic_bundle_max_nodes = 16;

    // ── 开关5：单轮最大工具并发数（防止large-llm一次性发射大量碎步骤）──
    int max_concurrent_tools_per_turn = 1;

    // ── 开关6：step快照合并阈值（N步才生成一份可恢复resume快照）──
    int snapshot_merge_every_n_steps = 1;

    // ── 开关7：JSON解析容错等级 ──
    //   true  = 高容错（修补残缺tool-call，收益小模型）
    //   false = 低容错（畸形调用直接拒绝，防止large-llm错误试探流入）
    bool lenient_json_parsing = true;

    // ── 开关8：large-llm专属 - 同一验证工具被反复调用的拦截阈值 ──
    int max_repeated_verification_calls = 3;

    // ── 白名单校验 ──
    bool IsValid() const {
        return semantic_bundle_max_chars >= 256
            && semantic_bundle_max_nodes >= 1
            && max_concurrent_tools_per_turn >= 1
            && snapshot_merge_every_n_steps >= 1
            && max_repeated_verification_calls >= 1;
    }
};

// ── 两套预置默认剖面 ──
inline ModelProfileConfig BuildSmallLlmDefaultProfile() {
    ModelProfileConfig p;
    p.id = ModelProfileId::kSmallLlm;
    p.name = "small-llm";
    p.allow_direct_call_mode = true;
    p.route_aggressiveness = OverviewRouteAggressiveness::kConservative;
    p.gate_strictness = GateStrictnessLevel::kStrict;
    p.semantic_bundle_max_chars = 6000;
    p.semantic_bundle_max_nodes = 16;
    p.max_concurrent_tools_per_turn = 1;
    p.snapshot_merge_every_n_steps = 1;
    p.lenient_json_parsing = true;
    p.max_repeated_verification_calls = 3;
    return p;
}

inline ModelProfileConfig BuildLargeLlmDefaultProfile() {
    ModelProfileConfig p;
    p.id = ModelProfileId::kLargeLlm;
    p.name = "large-llm";
    p.allow_direct_call_mode = true;
    p.route_aggressiveness = OverviewRouteAggressiveness::kMinimal;
    p.gate_strictness = GateStrictnessLevel::kStandard;
    p.semantic_bundle_max_chars = 24000;
    p.semantic_bundle_max_nodes = 48;
    p.max_concurrent_tools_per_turn = 2;
    p.snapshot_merge_every_n_steps = 5;
    p.lenient_json_parsing = false;
    p.max_repeated_verification_calls = 3;
    return p;
}

inline ModelProfileConfig ResolveModelProfileByName(const std::string & name) {
    const std::string lowered = [](const std::string & s) {
        std::string out = s;
        for (auto & ch : out) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch + ('a' - 'A')); }
        }
        return out;
    }(name);

    if (lowered == "large-llm" || lowered == "large_llm" || lowered == "codex"
        || lowered == "big-model" || lowered == "strong") {
        return BuildLargeLlmDefaultProfile();
    }
    if (lowered == "custom") {
        ModelProfileConfig p = BuildSmallLlmDefaultProfile();
        p.id = ModelProfileId::kCustom;
        p.name = "custom";
        return p;
    }
    // 默认 small-llm（保持旧有行为，零退化）
    return BuildSmallLlmDefaultProfile();
}

inline const char * GateStrictnessToString(GateStrictnessLevel l) {
    switch (l) {
        case GateStrictnessLevel::kRelaxed:  return "relaxed";
        case GateStrictnessLevel::kStandard: return "standard";
        case GateStrictnessLevel::kStrict:   return "strict";
    }
    return "standard";
}

inline const char * RouteAggressivenessToString(OverviewRouteAggressiveness l) {
    switch (l) {
        case OverviewRouteAggressiveness::kConservative: return "conservative";
        case OverviewRouteAggressiveness::kBalanced:     return "balanced";
        case OverviewRouteAggressiveness::kMinimal:      return "minimal";
    }
    return "conservative";
}