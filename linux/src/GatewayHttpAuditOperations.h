#pragma once

#include "HttpClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define CODEX_GATEWAY_HTTP_AUDIT_POPEN _popen
#define CODEX_GATEWAY_HTTP_AUDIT_PCLOSE _pclose
#else
#define CODEX_GATEWAY_HTTP_AUDIT_POPEN popen
#define CODEX_GATEWAY_HTTP_AUDIT_PCLOSE pclose
#include <sys/wait.h>
#endif

namespace codex_gateway_http_audit {

inline const char * AuditDbPath() {
    return "/var/lib/codex-audit/audit.db";
}

inline bool IsLinuxGatewayPlatform() {
    return CurrentPlatformName() == "linux";
}

inline std::string TrimWhitespace(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

inline std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string EscapeShellForDoubleQuotes(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

inline std::string QuoteShellArgument(const std::string & value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

inline std::string SqlEscape(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

inline std::string SqlText(const std::string & value) {
    return "'" + SqlEscape(value) + "'";
}

inline bool RunShellCapture(
    const std::string & command,
    std::string * output,
    int * exit_code,
    std::string * error_message) {
    if (output != nullptr) {
        output->clear();
    }
    if (exit_code != nullptr) {
        *exit_code = -1;
    }

#ifdef _WIN32
    const std::string shell_command = "cmd.exe /c \"" + command + " 2>&1\"";
#else
    const std::string shell_command =
        "/bin/sh -lc \"" + EscapeShellForDoubleQuotes(command + " 2>&1") + "\"";
#endif

    FILE * pipe = CODEX_GATEWAY_HTTP_AUDIT_POPEN(shell_command.c_str(), "r");
    if (pipe == nullptr) {
        if (error_message != nullptr) {
            *error_message = "failed to open shell pipe";
        }
        return false;
    }

    std::string captured;
    char buffer[4096];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
        captured += buffer;
    }
    const int status = CODEX_GATEWAY_HTTP_AUDIT_PCLOSE(pipe);
    if (output != nullptr) {
        *output = captured;
    }
#ifdef _WIN32
    if (exit_code != nullptr) {
        *exit_code = status;
    }
#else
    if (exit_code != nullptr) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = status;
        }
    }
#endif
    return true;
}

inline bool RunSqlStatement(const std::string & sql, std::string * error_message) {
    std::string output;
    int exit_code = -1;
    const std::string command =
        "sqlite3 -cmd " + QuoteShellArgument(".timeout 3000") + " "
        + QuoteShellArgument(AuditDbPath()) + " " + QuoteShellArgument(sql);
    if (!RunShellCapture(command, &output, &exit_code, error_message)) {
        return false;
    }
    if (exit_code != 0) {
        if (error_message != nullptr && error_message->empty()) {
            *error_message = TrimWhitespace(output.empty() ? "sqlite3 command failed" : output);
        }
        return false;
    }
    return true;
}

inline std::string QueryJsonArray(const std::string & sql, bool * ok, std::string * error_message) {
    if (ok != nullptr) {
        *ok = false;
    }
    std::string output;
    int exit_code = -1;
    const std::string command =
        "sqlite3 -cmd " + QuoteShellArgument(".timeout 3000") + " -json "
        + QuoteShellArgument(AuditDbPath()) + " " + QuoteShellArgument(sql);
    if (!RunShellCapture(command, &output, &exit_code, error_message)) {
        return "[]";
    }
    output = TrimWhitespace(output);
    if (exit_code != 0) {
        if (error_message != nullptr && error_message->empty()) {
            *error_message = output.empty() ? "sqlite3 query failed" : output;
        }
        return "[]";
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return output.empty() ? "[]" : output;
}

inline std::string BuildCompactTimestampId(const std::string & prefix) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &now_time);
#else
    localtime_r(&now_time, &tm_now);
#endif
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::ostringstream output;
    output << prefix
           << std::put_time(&tm_now, "%Y%m%d_%H%M%S")
           << "_"
           << std::setw(3) << std::setfill('0') << millis;
    return output.str();
}

inline bool EnsureAuditSchema(std::string * error_message) {
    if (!IsLinuxGatewayPlatform()) {
        if (error_message != nullptr) {
            *error_message = "http audit proxy is only available on linux";
        }
        return false;
    }

    const std::string sql =
        "create table if not exists http_events ("
        "id integer primary key autoincrement,"
        "event_id text not null unique,"
        "slice_id text not null,"
        "occurred_at text not null,"
        "decision text not null,"
        "method text not null,"
        "target_url text not null,"
        "target_host text not null,"
        "client_ip text,"
        "device_id text,"
        "session_id text,"
        "status_code integer,"
        "response_bytes integer,"
        "error_message text,"
        "trace_id text"
        ");"
        "create table if not exists ai_slices ("
        "id integer primary key autoincrement,"
        "slice_id text not null unique,"
        "device_id text,"
        "src_ip text,"
        "time_start text not null,"
        "time_end text not null,"
        "trigger_type text not null,"
        "session_count integer not null default 1,"
        "feature_summary text,"
        "status text not null,"
        "created_at text not null"
        ");"
        "create table if not exists ai_slice_links ("
        "id integer primary key autoincrement,"
        "slice_id text not null,"
        "entity_kind text not null,"
        "entity_id text not null,"
        "created_at text not null"
        ");";
    return RunSqlStatement(sql, error_message);
}

inline std::vector<std::string> SplitCsvList(const std::string & text) {
    std::vector<std::string> values;
    std::string current;
    std::istringstream input(text);
    while (std::getline(input, current, ',')) {
        current = TrimWhitespace(ToLowerCopy(current));
        if (!current.empty()) {
            values.push_back(current);
        }
    }
    return values;
}

inline std::string ExtractUrlHost(const std::string & url) {
    std::string working = url;
    const std::string http_prefix = "http://";
    const std::string https_prefix = "https://";
    if (working.rfind(http_prefix, 0) == 0) {
        working = working.substr(http_prefix.size());
    } else if (working.rfind(https_prefix, 0) == 0) {
        working = working.substr(https_prefix.size());
    } else {
        return std::string();
    }
    const std::size_t slash_pos = working.find('/');
    const std::string host_port = slash_pos == std::string::npos ? working : working.substr(0, slash_pos);
    const std::size_t colon_pos = host_port.find(':');
    return ToLowerCopy(colon_pos == std::string::npos ? host_port : host_port.substr(0, colon_pos));
}

inline bool HostMatchesBlockedList(
    const std::string & host,
    const std::vector<std::string> & blocked_hosts,
    std::string * matched_rule) {
    const std::string normalized_host = ToLowerCopy(host);
    for (const std::string & candidate : blocked_hosts) {
        if (candidate.empty()) {
            continue;
        }
        if (normalized_host == candidate) {
            if (matched_rule != nullptr) {
                *matched_rule = candidate;
            }
            return true;
        }
        if (normalized_host.size() > candidate.size() &&
            normalized_host.compare(
                normalized_host.size() - candidate.size(),
                candidate.size(),
                candidate) == 0 &&
            normalized_host[normalized_host.size() - candidate.size() - 1] == '.') {
            if (matched_rule != nullptr) {
                *matched_rule = candidate;
            }
            return true;
        }
    }
    return false;
}

inline std::string TruncateForField(const std::string & value, std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    return value.substr(0, max_bytes);
}

inline bool InsertAuditRows(
    const std::string & slice_id,
    const std::string & event_id,
    const std::string & occurred_at,
    const std::string & decision,
    const std::string & method,
    const std::string & target_url,
    const std::string & target_host,
    const std::string & client_ip,
    const std::string & device_id,
    const std::string & session_id,
    int status_code,
    int response_bytes,
    const std::string & error_message_text,
    const std::string & trace_id,
    const std::string & feature_summary,
    std::string * error_message) {
    std::ostringstream sql;
    sql
        << "insert or replace into ai_slices("
        << "slice_id,device_id,src_ip,time_start,time_end,trigger_type,session_count,feature_summary,status,created_at"
        << ") values ("
        << SqlText(slice_id) << ","
        << SqlText(device_id) << ","
        << SqlText(client_ip) << ","
        << SqlText(occurred_at) << ","
        << SqlText(occurred_at) << ","
        << SqlText("http_proxy_fetch") << ","
        << "1,"
        << SqlText(feature_summary) << ","
        << SqlText(decision) << ","
        << SqlText(occurred_at)
        << ");"
        << "insert into http_events("
        << "event_id,slice_id,occurred_at,decision,method,target_url,target_host,client_ip,device_id,session_id,status_code,response_bytes,error_message,trace_id"
        << ") values ("
        << SqlText(event_id) << ","
        << SqlText(slice_id) << ","
        << SqlText(occurred_at) << ","
        << SqlText(decision) << ","
        << SqlText(method) << ","
        << SqlText(target_url) << ","
        << SqlText(target_host) << ","
        << SqlText(client_ip) << ","
        << SqlText(device_id) << ","
        << SqlText(session_id) << ","
        << status_code << ","
        << response_bytes << ","
        << SqlText(error_message_text) << ","
        << SqlText(trace_id)
        << ");"
        << "insert into ai_slice_links(slice_id,entity_kind,entity_id,created_at) values ("
        << SqlText(slice_id) << ","
        << SqlText("http_event") << ","
        << SqlText(event_id) << ","
        << SqlText(occurred_at)
        << ");";
    return RunSqlStatement(sql.str(), error_message);
}

inline bool InsertServiceAuditRow(
    const std::string & service_name,
    const std::string & status,
    const std::string & detail_json,
    std::string * error_message) {
    const std::string observed_at = IsoTimestampNow();
    std::ostringstream sql;
    sql << "insert into service_events(observed_at,service_name,status,detail_json) values ("
        << SqlText(observed_at) << ","
        << SqlText(service_name) << ","
        << SqlText(status) << ","
        << SqlText(detail_json)
        << ");";
    return RunSqlStatement(sql.str(), error_message);
}

inline std::string BuildSshDetailJson(
    const std::string & action,
    const std::string & trace_id,
    const std::string & source_label,
    const std::string & output) {
    std::ostringstream detail;
    detail << "{"
           << "\"action\":\"" << codex_lan_agent::JsonEscape(action) << "\","
           << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
           << "\"source\":\"" << codex_lan_agent::JsonEscape(source_label) << "\","
           << "\"output\":\"" << codex_lan_agent::JsonEscape(TruncateForField(output, 1024)) << "\""
           << "}";
    return detail.str();
}

inline CommandResult GetSshMaintenanceStatus(
    const std::string & trace_id,
    const std::string & source_label) {
    CommandResult result;
    result.fields["trace_id"] = trace_id;
    result.fields["source"] = source_label;
    result.fields["service_name"] = "ssh";
    result.fields["port"] = "22";

    if (!IsLinuxGatewayPlatform()) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = "ssh maintenance is only available on linux";
        return result;
    }

    std::string active_output;
    int active_exit = -1;
    if (!RunShellCapture("systemctl is-active ssh || true", &active_output, &active_exit, nullptr)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = "failed to query ssh service state";
        return result;
    }

    const std::string service_state = TrimWhitespace(active_output);
    std::string listen_output;
    int listen_exit = -1;
    RunShellCapture("ss -lntup | grep -E ':22([^0-9]|$)' || true", &listen_output, &listen_exit, nullptr);
    const bool port_open = !TrimWhitespace(listen_output).empty();

    result.ok = true;
    result.exit_code = 0;
    result.fields["service_state"] = service_state.empty() ? "unknown" : service_state;
    result.fields["port_22_listening"] = port_open ? "true" : "false";
    result.fields["status"] = port_open ? "open" : "closed";
    result.fields["socket_detail"] = TruncateForField(TrimWhitespace(listen_output), 1024);
    return result;
}

inline CommandResult SetSshMaintenanceState(
    const std::string & desired_action,
    const std::string & trace_id,
    const std::string & source_label) {
    CommandResult result;
    result.fields["trace_id"] = trace_id;
    result.fields["source"] = source_label;
    result.fields["service_name"] = "ssh";
    result.fields["port"] = "22";
    result.fields["action"] = desired_action;

    if (!IsLinuxGatewayPlatform()) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = "ssh maintenance is only available on linux";
        return result;
    }

    const bool open_action = desired_action == "open";
    if (!open_action && desired_action != "close") {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "action must be open or close";
        return result;
    }

    std::string schema_error;
    if (!EnsureAuditSchema(&schema_error)) {
        result.ok = false;
        result.exit_code = 54;
        result.fields["error"] = schema_error;
        return result;
    }

    std::string output;
    int exit_code = -1;
    const std::string command = open_action ? "systemctl start ssh" : "systemctl stop ssh";
    if (!RunShellCapture(command, &output, &exit_code, nullptr)) {
        result.ok = false;
        result.exit_code = 53;
        result.fields["error"] = "failed to execute ssh maintenance command";
        return result;
    }

    CommandResult status = GetSshMaintenanceStatus(trace_id, source_label);
    result.fields["command_output"] = TruncateForField(TrimWhitespace(output), 1024);
    result.fields["service_state"] = GetFieldOrDefault(status, "service_state", "unknown");
    result.fields["port_22_listening"] = GetFieldOrDefault(status, "port_22_listening", "false");
    result.fields["status"] = GetFieldOrDefault(status, "status", "unknown");
    result.fields["socket_detail"] = GetFieldOrDefault(status, "socket_detail", "");
    result.ok = exit_code == 0 && status.ok;
    result.exit_code = result.ok ? 0 : (exit_code == 0 ? status.exit_code : exit_code);
    if (!result.ok && !result.fields.count("error")) {
        result.fields["error"] = "ssh maintenance command did not converge";
    }

    std::string audit_error;
    const std::string audit_status = open_action
        ? (result.ok ? "ssh_opened" : "ssh_open_failed")
        : (result.ok ? "ssh_closed" : "ssh_close_failed");
    InsertServiceAuditRow(
        "ssh",
        audit_status,
        BuildSshDetailJson(desired_action, trace_id, source_label, output),
        &audit_error);
    result.fields["audit_write_ok"] = audit_error.empty() ? "true" : "false";
    if (!audit_error.empty()) {
        result.fields["audit_error"] = audit_error;
    }
    return result;
}

inline CommandResult RunHttpProxyFetch(
    const AgentConfig &,
    const std::string & target_url,
    const std::string & session_id,
    const std::string & device_id,
    const std::string & client_ip,
    const std::string & block_hosts_csv,
    const std::string & trace_id,
    int timeout_ms) {
    CommandResult result;
    result.fields["target_url"] = target_url;
    result.fields["session_id"] = session_id;
    result.fields["device_id"] = device_id;
    result.fields["client_ip"] = client_ip;
    result.fields["trace_id"] = trace_id;
    result.fields["timeout_ms"] = std::to_string(timeout_ms);

    std::string schema_error;
    if (!EnsureAuditSchema(&schema_error)) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = schema_error;
        return result;
    }

    if (target_url.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "target_url is required";
        return result;
    }

    const std::string target_host = ExtractUrlHost(target_url);
    if (target_host.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "only absolute http:// or https:// urls are supported";
        return result;
    }

    const std::string event_id = BuildCompactTimestampId("httpevt_");
    const std::string slice_id = BuildCompactTimestampId("aislice_");
    const std::string occurred_at = IsoTimestampNow();
    const std::vector<std::string> blocked_hosts = SplitCsvList(block_hosts_csv);
    std::string matched_rule;
    const bool blocked = HostMatchesBlockedList(target_host, blocked_hosts, &matched_rule);
    const std::string method = "GET";

    result.fields["event_id"] = event_id;
    result.fields["slice_id"] = slice_id;
    result.fields["target_host"] = target_host;
    result.fields["blocked_rule"] = matched_rule;
    result.fields["blocked_hosts_csv"] = block_hosts_csv;

    std::string decision = blocked ? "blocked" : "allowed";
    int status_code = 0;
    int response_bytes = 0;
    std::string response_error;
    std::string response_preview;

    if (blocked) {
        status_code = 403;
        response_error = matched_rule.empty()
            ? "request blocked by host policy"
            : ("request blocked by host policy: " + matched_rule);
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = response_error;
    } else if (target_url.rfind("https://", 0) == 0) {
        decision = "unsupported_https";
        status_code = 426;
        response_error = "minimal proxy currently forwards only http:// targets on linux";
        result.ok = false;
        result.exit_code = 426;
        result.fields["error"] = response_error;
    } else {
        const codex_lan_agent::HttpResponse upstream =
            codex_lan_agent::GetUrl(target_url, std::max(1000, timeout_ms));
        status_code = upstream.status_code;
        response_bytes = static_cast<int>(upstream.body.size());
        response_error = upstream.error_message;
        response_preview = TruncateForField(upstream.body, 1024);
        result.ok = upstream.ok;
        result.exit_code = upstream.ok ? 0 : (upstream.status_code > 0 ? upstream.status_code : 1);
        result.fields["http_status"] = std::to_string(upstream.status_code);
        result.fields["response_bytes"] = std::to_string(response_bytes);
        result.fields["response_preview"] = response_preview;
        if (!upstream.error_message.empty()) {
            result.fields["error"] = upstream.error_message;
        }
    }

    const std::string feature_summary =
        "http_proxy_fetch " + decision + " host=" + target_host + " status=" + std::to_string(status_code);
    std::string insert_error;
    if (!InsertAuditRows(
            slice_id,
            event_id,
            occurred_at,
            decision,
            method,
            target_url,
            target_host,
            client_ip,
            device_id,
            session_id,
            status_code,
            response_bytes,
            response_error,
            trace_id,
            feature_summary,
            &insert_error)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = insert_error;
        result.fields["audit_write_ok"] = "false";
        return result;
    }

    result.fields["decision"] = decision;
    result.fields["audit_write_ok"] = "true";
    result.fields["feature_summary"] = feature_summary;
    if (!result.fields.count("http_status")) {
        result.fields["http_status"] = std::to_string(status_code);
    }
    if (!result.fields.count("response_bytes")) {
        result.fields["response_bytes"] = std::to_string(response_bytes);
    }
    return result;
}

inline std::string BuildRecentHttpEventsJson() {
    bool ok = false;
    std::string error_message;
    const std::string data = QueryJsonArray(
        "select id,event_id,slice_id,occurred_at,decision,method,target_url,target_host,client_ip,device_id,session_id,status_code,response_bytes,error_message,trace_id "
        "from http_events order by id desc limit 50;",
        &ok,
        &error_message);
    return ok
        ? ("{\"items\":" + data + "}")
        : ("{\"ok\":false,\"error\":\"" + codex_lan_agent::JsonEscape(error_message) + "\",\"items\":[]}");
}

inline std::string BuildRecentAiSlicesJson() {
    bool ok = false;
    std::string error_message;
    const std::string data = QueryJsonArray(
        "select id,slice_id,device_id,src_ip,time_start,time_end,trigger_type,session_count,feature_summary,status,created_at "
        "from ai_slices order by id desc limit 50;",
        &ok,
        &error_message);
    return ok
        ? ("{\"items\":" + data + "}")
        : ("{\"ok\":false,\"error\":\"" + codex_lan_agent::JsonEscape(error_message) + "\",\"items\":[]}");
}

inline std::string BuildSshStatusJson() {
    const CommandResult result = GetSshMaintenanceStatus(std::string(), "gateway_http");
    return ResultToJson(result);
}

inline bool HandleGatewayHttpAuditBinaryRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    (void)config;
    if (request.method == "GET" && request.path == "/gateway/api/http/recent") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = BuildRecentHttpEventsJson();
        return true;
    }

    if (request.method == "GET" && request.path == "/gateway/api/ai-slices/recent") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = BuildRecentAiSlicesJson();
        return true;
    }

    if (request.method == "GET" && request.path == "/gateway/api/ssh/status") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = BuildSshStatusJson();
        return true;
    }

    if (request.method == "POST" && request.path == "/gateway/api/http/fetch") {
        const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_ms");
        CommandResult result = RunHttpProxyFetch(
            config,
            ExtractJsonString(request.body, "target_url"),
            ExtractJsonString(request.body, "session_id"),
            ExtractJsonString(request.body, "device_id"),
            ExtractJsonString(request.body, "client_ip"),
            ExtractJsonString(request.body, "block_hosts_csv"),
            ExtractJsonString(request.body, "trace_id"),
            timeout_raw.empty() ? 10000 : std::max(1000, std::atoi(timeout_raw.c_str())));
        response->status_code = result.ok ? 200 : 502;
        if (result.exit_code == 400 || result.exit_code == 403 || result.exit_code == 426) {
            response->status_code = result.exit_code;
        }
        response->status_text = result.ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = ResultToJson(result);
        return true;
    }

    if (request.method == "POST" &&
        (request.path == "/gateway/api/ssh/open" || request.path == "/gateway/api/ssh/close")) {
        const std::string action = request.path == "/gateway/api/ssh/open" ? "open" : "close";
        CommandResult result = SetSshMaintenanceState(
            action,
            ExtractJsonString(request.body, "trace_id"),
            "gateway_http");
        response->status_code = result.ok ? 200 : 500;
        if (result.exit_code == 400 || result.exit_code == 403) {
            response->status_code = result.exit_code;
        }
        response->status_text = result.ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = ResultToJson(result);
        return true;
    }

    return false;
}

}  // namespace codex_gateway_http_audit
