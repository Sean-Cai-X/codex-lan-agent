#pragma once

#include "AgentConfig.h"
#include "HttpClient.h"
#include "JsonRequestView.h"
#include "StructuredJsonOperations.h"
#include "types.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace codex_lan_agent {
namespace remote_mcp_bridge {

inline std::vector<std::string> ExtractTopLevelArrayItemsLocal(const std::string & array_text) {
    std::vector<std::string> items;
    std::size_t start = array_text.find('[');
    if (start == std::string::npos) {
        return items;
    }

    bool in_string = false;
    bool escaping = false;
    int depth = 0;
    std::size_t item_start = std::string::npos;
    for (std::size_t index = start + 1; index < array_text.size(); ++index) {
        const char ch = array_text[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '{' || ch == '[') {
            if (depth == 0) {
                item_start = index;
            }
            ++depth;
            continue;
        }
        if (ch == '}' || ch == ']') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && item_start != std::string::npos) {
                    items.push_back(array_text.substr(item_start, index - item_start + 1));
                    item_start = std::string::npos;
                    continue;
                }
            } else if (ch == ']') {
                break;
            }
        }
    }
    return items;
}

inline std::string RemoteEndpoint() {
    const char * endpoint = std::getenv("CODEX_LAN_AGENT_REMOTE_MCP_ENDPOINT");
    const std::string value = endpoint == nullptr ? std::string() : Trim(endpoint);
    return value.empty() ? "http://127.0.0.1:8765/mcp" : value;
}

inline std::string RemotePrefix() {
    const char * prefix = std::getenv("CODEX_LAN_AGENT_REMOTE_MCP_PREFIX");
    const std::string value = prefix == nullptr ? std::string() : Trim(prefix);
    return value.empty() ? "remote_" : value;
}

inline std::string BuildToolsListRequest() {
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{}}";
}

inline std::string BuildToolsCallRequest(
    const std::string & remote_tool_name,
    const std::string & arguments_json) {
    std::ostringstream request;
    request << "{"
            << "\"jsonrpc\":\"2.0\","
            << "\"id\":1,"
            << "\"method\":\"tools/call\","
            << "\"params\":{"
            << "\"name\":\"" << JsonEscape(remote_tool_name) << "\","
            << "\"arguments\":" << (arguments_json.empty() ? "{}" : arguments_json)
            << "}}";
    return request.str();
}

inline std::vector<std::string> ListRemoteToolNames(const std::string & endpoint, std::string * error) {
    const HttpResponse response = PostJson(endpoint, BuildToolsListRequest(), 5000);
    if (!response.ok) {
        if (error != nullptr) {
            *error = response.error_message.empty() ? "remote tools/list failed" : response.error_message;
        }
        return {};
    }
    const std::string result_object = ExtractJsonObjectRaw(response.body, "result");
    const std::string tools_array = ExtractJsonObjectRaw(result_object.empty() ? response.body : result_object, "tools");
    std::vector<std::string> names;
    for (const std::string & item : ExtractTopLevelArrayItemsLocal(tools_array)) {
        const std::string name = ExtractJsonString(item, "name");
        if (!name.empty()) {
            names.push_back(name);
        }
    }
    return names;
}

inline CommandResult BuildRemoteMcpOverviewResult(const AgentConfig &) {
    CommandResult result;
    result.fields["capability_id"] = "remote_mcp_bridge";
    result.fields["result"] = "remote_mcp_overview";
    result.fields["bridge_mode"] = "internal_proxy_hidden_from_chat_tools_list";
    result.fields["remote_prefix"] = RemotePrefix();
    const std::string endpoint = RemoteEndpoint();
    result.fields["remote_endpoint"] = endpoint;
    if (endpoint.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "not_configured";
        result.fields["error"] = "CODEX_LAN_AGENT_REMOTE_MCP_ENDPOINT is not set";
        return result;
    }

    std::string error;
    const std::vector<std::string> tools = ListRemoteToolNames(endpoint, &error);
    if (!error.empty()) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["status"] = "failed";
        result.fields["error"] = error;
        return result;
    }

    std::ostringstream csv;
    std::ostringstream local_csv;
    for (std::size_t index = 0; index < tools.size(); ++index) {
        if (index != 0) {
            csv << ",";
            local_csv << ",";
        }
        csv << tools[index];
        local_csv << RemotePrefix() << tools[index];
    }
    result.fields["status"] = "success";
    result.fields["remote_tool_count"] = std::to_string(tools.size());
    result.fields["remote_tools_csv"] = csv.str();
    result.fields["local_proxy_tools_csv"] = local_csv.str();
    result.fields["summary"] = "remote MCP tools discovered through internal proxy";
    return result;
}

inline bool TryHandleRemoteMcpTool(
    const AgentConfig &,
    const std::string & local_tool_name,
    const JsonRequestView & params,
    CommandResult * result) {
    const std::string endpoint = RemoteEndpoint();
    const std::string prefix = RemotePrefix();
    if (endpoint.empty() || local_tool_name.rfind(prefix, 0) != 0 || local_tool_name.size() <= prefix.size()) {
        return false;
    }

    const std::string remote_tool_name = local_tool_name.substr(prefix.size());
    const std::string arguments_json = params.body().empty() ? "{}" : params.body();
    const HttpResponse response = PostJson(endpoint, BuildToolsCallRequest(remote_tool_name, arguments_json), 30000);
    if (result == nullptr) {
        return true;
    }

    result->fields["capability_id"] = "remote_mcp_bridge";
    result->fields["result"] = "remote_mcp_call_forwarded";
    result->fields["status"] = response.ok ? "success" : "failed";
    result->fields["bridge_mode"] = "internal_proxy_hidden_from_chat_tools_list";
    result->fields["local_proxy_tool_name"] = local_tool_name;
    result->fields["remote_tool_name"] = remote_tool_name;
    result->fields["remote_endpoint"] = endpoint;
    result->fields["remote_status_code"] = std::to_string(response.status_code);
    result->fields["remote_response_json"] = response.body;
    if (!response.ok) {
        result->ok = false;
        result->exit_code = response.status_code > 0 ? response.status_code : 502;
        result->fields["error"] = response.error_message.empty() ? "remote MCP tools/call failed" : response.error_message;
        return true;
    }

    const std::string structured = ExtractJsonObjectRaw(response.body, "structuredContent");
    const std::string source = structured.empty() ? response.body : structured;
    const JsonRequestView remote_result(source);
    const std::vector<std::string> pass_through_keys = {
        "status",
        "result",
        "summary",
        "error",
        "has_more",
        "next_call_json",
        "required_tool_name",
        "required_tool_arguments_json",
        "terminal_state",
        "completion_claim_allowed",
        "final_answer_allowed",
        "verification_ok",
        "continue_required",
        "result_ref",
        "evidence_ref"
    };
    for (const std::string & key : pass_through_keys) {
        const std::string value = remote_result.GetString(key);
        if (!value.empty()) {
            result->fields["remote_" + key] = value;
        }
    }
    if (response.body.find("\"error\"") != std::string::npos && structured.empty()) {
        result->ok = false;
        result->exit_code = response.status_code > 0 ? response.status_code : 502;
    }
    return true;
}

}  // namespace remote_mcp_bridge
}  // namespace codex_lan_agent
