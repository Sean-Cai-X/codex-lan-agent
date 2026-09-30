#pragma once

#include "HttpClient.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

std::string NormalizeControllerUrl(std::string url) {
    url = Trim(url);
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string ResolveControllerUrl(const std::string & explicit_url = std::string()) {
    const std::string normalized_explicit = NormalizeControllerUrl(explicit_url);
    if (!normalized_explicit.empty()) {
        return normalized_explicit;
    }
    const char * env_url = std::getenv("CODEX_LAN_AGENT_CONTROLLER_URL");
    if (env_url != nullptr && env_url[0] != '\0') {
        return NormalizeControllerUrl(env_url);
    }
    return std::string();
}

bool ReadReportContent(
    const AgentConfig & config,
    const std::string & report_file,
    const std::string & inline_text,
    std::string * content,
    std::string * error_message) {
    if (!inline_text.empty()) {
        *content = inline_text;
        return true;
    }
    if (report_file.empty()) {
        if (error_message != nullptr) {
            *error_message = "report text or report file is required";
        }
        return false;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, report_file, &normalized, &path_error)) {
        if (error_message != nullptr) {
            *error_message = path_error;
        }
        return false;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open report file";
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *content = buffer.str();
    return true;
}

CommandResult BuildBusinessPlaneStatusResult(const AgentConfig & config) {
    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["business_plane"] = "linux_development_monitoring_plane";
    result.fields["network_role"] = "outbound_client_only";
    result.fields["serve_allowed"] = "false";
    result.fields["service_ports"] = "[]";
    result.fields["controller_url"] = ResolveControllerUrl();
    result.fields["workspace_root"] = config.workspace_root;
    result.fields["log_root"] = config.log_root;
    result.fields["platform"] = CurrentPlatformName();
    result.fields["allowed_outbound_endpoints"] = "GET /health,GET /tools,POST /tools,POST /mcp";
    result.fields["forbidden"] =
        "serve,linux_mcp_server_port,inbound_control_requests,production_dnsmasq,production_mitm";
    return result;
}

CommandResult BuildControllerHealthClientResult(const std::string & controller_url) {
    CommandResult result;
    const std::string base_url = ResolveControllerUrl(controller_url);
    result.fields["business_plane"] = "linux_development_monitoring_plane";
    result.fields["network_role"] = "outbound_client_only";
    result.fields["controller_url"] = base_url;
    result.fields["method"] = "GET";
    result.fields["endpoint"] = "/health";

    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 2;
        result.fields["error"] = "controller url is required; set CODEX_LAN_AGENT_CONTROLLER_URL or pass --controller-url";
        return result;
    }

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::GetUrl(base_url + "/health", 10000);
    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 1;
    result.fields["http_status"] = std::to_string(response.status_code);
    result.fields["response_body"] = response.body;
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    return result;
}

CommandResult BuildControllerReportClientResult(
    const AgentConfig & config,
    const std::string & controller_url,
    const std::string & session_id,
    const std::string & title,
    const std::string & inline_text,
    const std::string & report_file) {
    CommandResult result;
    const std::string base_url = ResolveControllerUrl(controller_url);
    result.fields["business_plane"] = "linux_development_monitoring_plane";
    result.fields["network_role"] = "outbound_client_only";
    result.fields["controller_url"] = base_url;
    result.fields["method"] = "POST";
    result.fields["endpoint"] = "/tools";
    result.fields["tool"] = "lan_agent_record_dialog_slice";

    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 2;
        result.fields["error"] = "controller url is required; set CODEX_LAN_AGENT_CONTROLLER_URL or pass --controller-url";
        return result;
    }

    std::string content;
    std::string content_error;
    if (!ReadReportContent(config, report_file, inline_text, &content, &content_error)) {
        result.ok = false;
        result.exit_code = 3;
        result.fields["error"] = content_error;
        return result;
    }

    const std::string resolved_session_id = session_id.empty() ? "dev-plane" : session_id;
    const std::string resolved_title = title.empty() ? "linux development-plane report" : title;
    const std::string turn_id = "linux-report-" + TimeStampForFileName();
    std::ostringstream arguments;
    arguments
        << "{"
        << "\"session_id\":\"" << codex_lan_agent::JsonEscape(resolved_session_id) << "\","
        << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
        << "\"slice_type\":\"linux_dev_plane_report\","
        << "\"slice_summary\":\"" << codex_lan_agent::JsonEscape(resolved_title) << "\","
        << "\"tags\":\"linux,dev-plane,outbound-client,business-plane\","
        << "\"provider_id\":\"" << codex_lan_agent::JsonEscape(GetHostNamePortable()) << "\","
        << "\"source_type\":\"linux_dev_plane\","
        << "\"assistant_text\":\"" << codex_lan_agent::JsonEscape(content) << "\","
        << "\"write_mode\":\"append\","
        << "\"reasoning_level\":\"observation\","
        << "\"primary_intent\":\"development_monitoring_report\","
        << "\"confidence\":\"0.90\""
        << "}";

    std::ostringstream payload;
    payload
        << "{"
        << "\"tool\":\"lan_agent_record_dialog_slice\","
        << "\"params\":" << arguments.str()
        << "}";

    codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(base_url + "/tools", payload.str(), 20000);
    std::string used_endpoint = "/tools";
    bool mcp_fallback_used = false;
    if (!response.ok ||
        response.status_code == 404 ||
        response.body.find("tool not found") != std::string::npos) {
        std::ostringstream mcp_payload;
        mcp_payload
            << "{"
            << "\"jsonrpc\":\"2.0\","
            << "\"id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
            << "\"method\":\"tools/call\","
            << "\"params\":{"
            << "\"name\":\"lan_agent_record_dialog_slice\","
            << "\"arguments\":" << arguments.str()
            << "}}";
        response = codex_lan_agent::PostJson(base_url + "/mcp", mcp_payload.str(), 20000);
        used_endpoint = "/mcp";
        mcp_fallback_used = true;
    }
    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 1;
    result.fields["http_status"] = std::to_string(response.status_code);
    result.fields["response_body"] = response.body;
    result.fields["session_id"] = resolved_session_id;
    result.fields["turn_id"] = turn_id;
    result.fields["title"] = resolved_title;
    result.fields["used_endpoint"] = used_endpoint;
    result.fields["mcp_fallback_used"] = mcp_fallback_used ? "true" : "false";
    if (!report_file.empty()) {
        result.fields["report_file"] = report_file;
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    return result;
}

CommandResult BuildControllerProfileReportClientResult(
    const AgentConfig & config,
    const std::string & controller_url,
    const std::string & profile_name,
    const std::string & profile_arguments,
    const std::string & session_id,
    const std::string & title) {
    CommandResult result;
    result.fields["business_plane"] = "linux_development_monitoring_plane";
    result.fields["network_role"] = "outbound_client_only";
    result.fields["profile"] = profile_name;
    result.fields["profile_arguments"] = profile_arguments;

    if (profile_name.empty()) {
        result.ok = false;
        result.exit_code = 2;
        result.fields["error"] = "profile name is required";
        return result;
    }

    const CommandResult profile_result = RunCliProfile(config, profile_name, profile_arguments);
    const std::string report_title = title.empty()
        ? ("linux profile report: " + profile_name)
        : title;
    const std::string report_body = ResultToJson(profile_result);
    CommandResult report_result = BuildControllerReportClientResult(
        config,
        controller_url,
        session_id,
        report_title,
        report_body,
        std::string());

    report_result.fields["local_profile"] = profile_name;
    report_result.fields["local_profile_exit_code"] = std::to_string(profile_result.exit_code);
    report_result.fields["local_profile_ok"] = profile_result.ok ? "true" : "false";
    report_result.fields["local_profile_reported"] = report_result.ok ? "true" : "false";
    if (!profile_result.ok) {
        report_result.fields["local_profile_error"] =
            GetFieldOrDefault(profile_result, "error", "profile execution failed");
    }
    return report_result;
}
