#pragma once

#include "GatewayHttpAuditOperations.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define CODEX_GATEWAY_POPEN _popen
#define CODEX_GATEWAY_PCLOSE _pclose
#else
#define CODEX_GATEWAY_POPEN popen
#define CODEX_GATEWAY_PCLOSE pclose
#endif

namespace codex_gateway_audit_ui {

inline const char * AuditDbPath() {
    return "/var/lib/codex-audit/audit.db";
}

inline const char * PcapDirPath() {
    return "/var/log/codex-gateway/pcap";
}

inline const char * DnsLogPath() {
    return "/var/log/codex-dnsmasq.log";
}

inline const char * MitmLogDirPath() {
    return "/var/log/mitmproxy";
}

inline bool IsLinuxGatewayPlatform() {
    return CurrentPlatformName() == "linux";
}

inline std::string TrimWhitespace(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

inline std::string EscapeHtml(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
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

inline bool RunShellCapture(
    const std::string & command,
    std::string * output,
    int * exit_code,
    std::string * error_message) {
    if (output) {
        output->clear();
    }
    if (exit_code) {
        *exit_code = -1;
    }

#ifdef _WIN32
    const std::string shell_command = "cmd.exe /c \"" + command + " 2>&1\"";
#else
    const std::string shell_command =
        "/bin/sh -lc \"" + EscapeShellForDoubleQuotes(command + " 2>&1") + "\"";
#endif

    FILE * pipe = CODEX_GATEWAY_POPEN(shell_command.c_str(), "r");
    if (pipe == nullptr) {
        if (error_message) {
            *error_message = "failed to open shell pipe";
        }
        return false;
    }

    std::string captured;
    char buffer[4096];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
        captured += buffer;
    }
    const int status = CODEX_GATEWAY_PCLOSE(pipe);
    if (output) {
        *output = captured;
    }
#ifdef _WIN32
    if (exit_code) {
        *exit_code = status;
    }
#else
    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = status;
        }
    }
#endif
    return true;
}

inline std::string JsonStringOrEmpty(const std::string & value) {
    return value.empty() ? "\"\"" : ("\"" + codex_lan_agent::JsonEscape(value) + "\"");
}

inline std::string ReadJsonArrayFromSqlite(
    const std::string & sql,
    bool * ok,
    std::string * error_message) {
    if (ok) {
        *ok = false;
    }
    if (!IsLinuxGatewayPlatform()) {
        if (error_message) {
            *error_message = "gateway audit queries are only available on linux";
        }
        return "[]";
    }

    std::string output;
    int exit_code = -1;
    const std::string command =
        "sqlite3 -json '" + std::string(AuditDbPath()) + "' \"" + sql + "\"";
    if (!RunShellCapture(command, &output, &exit_code, error_message)) {
        return "[]";
    }
    output = TrimWhitespace(output);
    if (exit_code != 0) {
        if (error_message && error_message->empty()) {
            *error_message = output.empty() ? "sqlite3 query failed" : output;
        }
        return "[]";
    }
    if (ok) {
        *ok = true;
    }
    return output.empty() ? "[]" : output;
}

inline std::string GetServiceState(const std::string & service_name) {
    if (!IsLinuxGatewayPlatform()) {
        return "unsupported";
    }
    std::string output;
    int exit_code = -1;
    const std::string command = "systemctl is-active " + service_name + " || true";
    if (!RunShellCapture(command, &output, &exit_code, nullptr)) {
        return "unknown";
    }
    const std::string trimmed = TrimWhitespace(output);
    return trimmed.empty() ? "unknown" : trimmed;
}

inline bool IsPortListening(const std::string & port_text) {
    if (!IsLinuxGatewayPlatform()) {
        return false;
    }
    std::string output;
    int exit_code = -1;
    const std::string command =
        "ss -lntup | grep -E ':" + port_text + "([^0-9]|$)' || true";
    if (!RunShellCapture(command, &output, &exit_code, nullptr)) {
        return false;
    }
    return !TrimWhitespace(output).empty();
}

inline std::string ReadRecentJournalLines(const std::string & unit_name, int line_count) {
    if (!IsLinuxGatewayPlatform()) {
        return std::string();
    }
    std::string output;
    int exit_code = -1;
    std::ostringstream command;
    command << "journalctl -u " << unit_name << " -n " << std::max(1, line_count)
            << " --no-pager 2>/dev/null || true";
    if (!RunShellCapture(command.str(), &output, &exit_code, nullptr)) {
        return std::string();
    }
    return TrimWhitespace(output);
}

inline std::string BuildPcapFilesJson() {
    std::ostringstream json;
    json << "[";
    bool first = true;
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> files;
    if (std::filesystem::exists(PcapDirPath(), ec)) {
        for (const auto & entry : std::filesystem::directory_iterator(PcapDirPath(), ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() != ".pcap") {
                continue;
            }
            files.push_back(entry);
        }
    }
    std::sort(files.begin(), files.end(), [](const auto & left, const auto & right) {
        return left.path().filename().string() > right.path().filename().string();
    });
    for (const auto & entry : files) {
        std::error_code item_ec;
        const auto file_size = entry.file_size(item_ec);
        const auto write_time = entry.last_write_time(item_ec);
        std::time_t write_epoch = 0;
        if (!item_ec) {
            const auto system_now = std::chrono::system_clock::now();
            const auto file_now = std::filesystem::file_time_type::clock::now();
            const auto system_write_time =
                std::chrono::time_point_cast<std::chrono::system_clock::duration>(write_time - file_now + system_now);
            write_epoch = std::chrono::system_clock::to_time_t(system_write_time);
        }
        if (!first) {
            json << ",";
        }
        first = false;
        json << "{"
             << "\"name\":\"" << codex_lan_agent::JsonEscape(entry.path().filename().string()) << "\","
             << "\"path\":\"" << codex_lan_agent::JsonEscape(entry.path().string()) << "\","
             << "\"size_bytes\":" << static_cast<unsigned long long>(item_ec ? 0 : file_size) << ","
             << "\"modified_epoch\":" << static_cast<long long>(write_epoch)
             << "}";
    }
    json << "]";
    return json.str();
}

inline std::string BuildPcapStatsJson() {
    std::uint64_t total_bytes = 0;
    std::size_t file_count = 0;
    std::string oldest_name;
    std::string newest_name;
    std::error_code ec;
    if (std::filesystem::exists(PcapDirPath(), ec)) {
        std::vector<std::filesystem::directory_entry> files;
        for (const auto & entry : std::filesystem::directory_iterator(PcapDirPath(), ec)) {
            if (ec || !entry.is_regular_file() || entry.path().extension() != ".pcap") {
                continue;
            }
            files.push_back(entry);
        }
        std::sort(files.begin(), files.end(), [](const auto & left, const auto & right) {
            return left.path().filename().string() < right.path().filename().string();
        });
        file_count = files.size();
        for (const auto & entry : files) {
            std::error_code size_ec;
            total_bytes += static_cast<std::uint64_t>(entry.file_size(size_ec));
        }
        if (!files.empty()) {
            oldest_name = files.front().path().filename().string();
            newest_name = files.back().path().filename().string();
        }
    }
    std::ostringstream json;
    json << "{"
         << "\"file_count\":" << static_cast<unsigned long long>(file_count) << ","
         << "\"total_bytes\":" << static_cast<unsigned long long>(total_bytes) << ","
         << "\"oldest\":\"" << codex_lan_agent::JsonEscape(oldest_name) << "\","
         << "\"newest\":\"" << codex_lan_agent::JsonEscape(newest_name) << "\""
         << "}";
    return json.str();
}

inline std::string BuildAuditSummaryJson(const AgentConfig & config) {
    const CommandResult health = BuildLivenessResult(config);
    std::string schema_error;
    codex_gateway_http_audit::EnsureAuditSchema(&schema_error);
    (void)schema_error;
    const bool db_exists = std::filesystem::exists(AuditDbPath());
    const bool pcap_dir_exists = std::filesystem::exists(PcapDirPath());
    const bool dns_log_exists = std::filesystem::exists(DnsLogPath());
    const bool mitm_log_dir_exists = std::filesystem::exists(MitmLogDirPath());
    const std::string mitmweb_journal_tail = ReadRecentJournalLines("codex-mitmweb", 12);

    std::string sqlite_counts_error;
    bool sqlite_counts_ok = false;
    const std::string sqlite_counts = ReadJsonArrayFromSqlite(
        "select 'service_events' as table_name, count(*) as row_count from service_events "
        "union all select 'dns_log_events' as table_name, count(*) as row_count from dns_log_events "
        "union all select 'proxy_probe_events' as table_name, count(*) as row_count from proxy_probe_events "
        "union all select 'capture_sessions' as table_name, count(*) as row_count from capture_sessions "
        "union all select 'http_events' as table_name, count(*) as row_count from http_events "
        "union all select 'ai_slices' as table_name, count(*) as row_count from ai_slices "
        "union all select 'ai_slice_links' as table_name, count(*) as row_count from ai_slice_links;",
        &sqlite_counts_ok,
        &sqlite_counts_error);

    std::string latest_pcap_name;
    std::error_code ec;
    if (pcap_dir_exists) {
        for (const auto & entry : std::filesystem::directory_iterator(PcapDirPath(), ec)) {
            if (ec || !entry.is_regular_file() || entry.path().extension() != ".pcap") {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (latest_pcap_name.empty() || name > latest_pcap_name) {
                latest_pcap_name = name;
            }
        }
    }

    std::ostringstream json;
    json << "{"
         << "\"ok\":true,"
         << "\"plane\":\"kvm_gateway_audit_plane\","
         << "\"platform\":\"" << codex_lan_agent::JsonEscape(CurrentPlatformName()) << "\","
         << "\"listen_host\":\"" << codex_lan_agent::JsonEscape(config.listen_host) << "\","
         << "\"listen_port\":" << config.listen_port << ","
         << "\"healthz_status\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(health, "status", "unknown")) << "\","
         << "\"workspace_root\":\"" << codex_lan_agent::JsonEscape(config.workspace_root) << "\","
         << "\"log_root\":\"" << codex_lan_agent::JsonEscape(config.log_root) << "\","
         << "\"observed_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
         << "\"services\":{"
         << "\"dnsmasq\":\"" << codex_lan_agent::JsonEscape(GetServiceState("dnsmasq")) << "\","
         << "\"sing_box\":\"" << codex_lan_agent::JsonEscape(GetServiceState("sing-box")) << "\","
         << "\"ssh\":\"" << codex_lan_agent::JsonEscape(GetServiceState("ssh")) << "\","
         << "\"mitmweb\":\"" << codex_lan_agent::JsonEscape(GetServiceState("codex-mitmweb")) << "\","
         << "\"nftables\":\"" << codex_lan_agent::JsonEscape(GetServiceState("nftables")) << "\","
         << "\"capture\":\"" << codex_lan_agent::JsonEscape(GetServiceState("codex-gateway-capture")) << "\","
         << "\"snapshot_timer\":\"" << codex_lan_agent::JsonEscape(GetServiceState("codex-gateway-snapshot.timer")) << "\","
         << "\"codex_lan_agent\":\"" << codex_lan_agent::JsonEscape(GetServiceState("codex-lan-agent")) << "\""
         << "},"
         << "\"ports\":{"
         << "\"22\":" << (IsPortListening("22") ? "true" : "false") << ","
         << "\"53\":" << (IsPortListening("53") ? "true" : "false") << ","
         << "\"2080\":" << (IsPortListening("2080") ? "true" : "false") << ","
         << "\"8083\":" << (IsPortListening("8083") ? "true" : "false") << ","
         << "\"8084\":" << (IsPortListening("8084") ? "true" : "false") << ","
         << "\"18080\":" << (IsPortListening("18080") ? "true" : "false")
         << "},"
         << "\"paths\":{"
         << "\"audit_db\":\"" << codex_lan_agent::JsonEscape(AuditDbPath()) << "\","
         << "\"dns_log\":\"" << codex_lan_agent::JsonEscape(DnsLogPath()) << "\","
         << "\"mitm_log_dir\":\"" << codex_lan_agent::JsonEscape(MitmLogDirPath()) << "\","
         << "\"pcap_dir\":\"" << codex_lan_agent::JsonEscape(PcapDirPath()) << "\""
         << "},"
         << "\"artifacts\":{"
         << "\"audit_db_exists\":" << (db_exists ? "true" : "false") << ","
         << "\"dns_log_exists\":" << (dns_log_exists ? "true" : "false") << ","
         << "\"mitm_log_dir_exists\":" << (mitm_log_dir_exists ? "true" : "false") << ","
         << "\"pcap_dir_exists\":" << (pcap_dir_exists ? "true" : "false") << ","
         << "\"latest_pcap\":\"" << codex_lan_agent::JsonEscape(latest_pcap_name) << "\""
         << "},"
         << "\"pcap\":"
         << BuildPcapStatsJson()
         << ","
         << "\"mitm\":{"
         << "\"journal_tail\":\"" << codex_lan_agent::JsonEscape(mitmweb_journal_tail) << "\""
         << "},"
         << "\"sqlite\":{"
         << "\"query_ok\":" << (sqlite_counts_ok ? "true" : "false") << ","
         << "\"error\":\"" << codex_lan_agent::JsonEscape(sqlite_counts_error) << "\","
         << "\"counts\":" << sqlite_counts
         << "}"
         << "}";
    return json.str();
}

inline std::string BuildGatewayApiEnvelope(
    const std::string & key,
    const std::string & raw_json_array_or_object) {
    return std::string("{\"ok\":true,\"observed_at\":\"")
        + codex_lan_agent::JsonEscape(IsoTimestampNow())
        + "\",\"" + key + "\":" + raw_json_array_or_object + "}";
}

inline std::string BuildGatewayActionResultJson(
    bool ok,
    const std::string & action,
    const std::string & detail) {
    return std::string("{\"ok\":") + (ok ? "true" : "false")
        + ",\"action\":\"" + codex_lan_agent::JsonEscape(action)
        + "\",\"detail\":\"" + codex_lan_agent::JsonEscape(detail)
        + "\",\"observed_at\":\"" + codex_lan_agent::JsonEscape(IsoTimestampNow()) + "\"}";
}

inline std::string ReadDownloadablePcap(
    const std::string & file_name,
    bool * ok,
    std::string * error_message) {
    if (ok) {
        *ok = false;
    }
    if (file_name.empty() || file_name.find('/') != std::string::npos || file_name.find('\\') != std::string::npos) {
        if (error_message) {
            *error_message = "invalid pcap file name";
        }
        return std::string();
    }
    const std::filesystem::path target = std::filesystem::path(PcapDirPath()) / file_name;
    std::ifstream input(target, std::ios::binary);
    if (!input.is_open()) {
        if (error_message) {
            *error_message = "failed to open pcap file";
        }
        return std::string();
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (ok) {
        *ok = true;
    }
    return buffer.str();
}

inline std::string BuildGatewayAuditConsoleHtml() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Codex KVM Gateway Audit</title>
  <style>
    :root { color-scheme: dark; --bg:#0b1020; --panel:#121933; --panel2:#182142; --text:#e6ebff; --muted:#94a0c6; --ok:#4ade80; --warn:#fbbf24; --bad:#f87171; --line:#27325f; }
    * { box-sizing:border-box; }
    body { margin:0; font-family:Segoe UI, Arial, sans-serif; background:var(--bg); color:var(--text); }
    .wrap { max-width:1440px; margin:0 auto; padding:20px; }
    h1,h2 { margin:0; font-weight:600; }
    h1 { font-size:24px; }
    h2 { font-size:16px; color:#cdd6f8; margin-bottom:12px; }
    p { color:var(--muted); margin:8px 0 0; }
    .hero, .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; }
    .hero { padding:18px; display:flex; justify-content:space-between; gap:16px; align-items:flex-start; }
    .hero-meta { text-align:right; color:var(--muted); font-size:12px; min-width:240px; }
    .grid { display:grid; grid-template-columns:repeat(12, minmax(0,1fr)); gap:16px; margin-top:16px; }
    .span-3 { grid-column:span 3; } .span-4 { grid-column:span 4; } .span-6 { grid-column:span 6; } .span-8 { grid-column:span 8; } .span-12 { grid-column:span 12; }
    .panel { padding:16px; min-height:120px; }
    .cards { display:grid; grid-template-columns:repeat(3, minmax(0,1fr)); gap:10px; }
    .card { background:var(--panel2); border:1px solid var(--line); border-radius:6px; padding:10px; min-height:84px; }
    .label { color:var(--muted); font-size:12px; margin-bottom:8px; }
    .value { font-size:16px; word-break:break-word; }
    .ok { color:var(--ok); } .warn { color:var(--warn); } .bad { color:var(--bad); }
    .toolbar { display:flex; flex-wrap:wrap; gap:10px; }
    button { background:#20305f; color:var(--text); border:1px solid #3551a4; border-radius:6px; padding:10px 12px; cursor:pointer; }
    button:hover { background:#29407d; }
    table { width:100%; border-collapse:collapse; font-size:13px; }
    th, td { text-align:left; padding:8px 10px; border-bottom:1px solid var(--line); vertical-align:top; }
    th { color:#b5c0e8; font-weight:600; }
    .table-wrap { overflow:auto; max-height:420px; }
    pre { margin:0; white-space:pre-wrap; word-break:break-word; font-size:12px; color:#d8e0ff; }
    @media (max-width: 1100px) { .span-3,.span-4,.span-6,.span-8,.span-12 { grid-column:span 12; } .cards { grid-template-columns:1fr; } .hero { flex-direction:column; } .hero-meta{text-align:left;} }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div>
        <h1>Codex KVM Gateway Audit</h1>
        <p>Edge-side audit console for DNS, proxy, capture, SQLite, and control actions over port 18080.</p>
      </div>
      <div class="hero-meta">
        <div id="stamp">Waiting for data</div>
        <div id="endpoint">/gateway/api/summary</div>
      </div>
    </div>

    <div class="grid">
      <section class="panel span-6">
        <h2>Overview</h2>
        <div class="cards" id="overview-cards"></div>
      </section>

      <section class="panel span-6">
        <h2>Actions</h2>
        <div class="toolbar">
          <button data-action="refresh">Refresh</button>
          <button data-action="snapshot">Run Snapshot</button>
          <button data-action="ssh-open">Open SSH</button>
          <button data-action="ssh-close">Close SSH</button>
          <button data-restart="dnsmasq">Restart dnsmasq</button>
          <button data-restart="sing-box">Restart sing-box</button>
          <button data-restart="codex-gateway-capture">Restart capture</button>
          <button data-restart="nftables">Restart nftables</button>
        </div>
        <div style="margin-top:12px"><pre id="action-output">No action yet.</pre></div>
      </section>

      <section class="panel span-12">
        <h2>DNS Audit</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Observed</th><th>Phase</th><th>Type</th><th>Name</th><th>Client</th></tr></thead><tbody id="dns-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>Proxy Audit</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Observed</th><th>Target</th><th>Exit</th><th>HTTP</th></tr></thead><tbody id="proxy-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>HTTP Events</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Occurred</th><th>Decision</th><th>Host</th><th>Status</th><th>Client</th></tr></thead><tbody id="http-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>AI Slices</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Created</th><th>Status</th><th>Source IP</th><th>Device</th><th>Summary</th></tr></thead><tbody id="slice-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>Capture Sessions</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Observed</th><th>Interface</th><th>Output</th><th>Status</th></tr></thead><tbody id="capture-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>PCAP Files</h2>
        <div class="table-wrap"><table><thead><tr><th>Name</th><th>Size</th><th>Modified</th><th>Download</th></tr></thead><tbody id="pcap-body"></tbody></table></div>
      </section>

      <section class="panel span-6">
        <h2>Service Events</h2>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Observed</th><th>Service</th><th>Status</th></tr></thead><tbody id="service-body"></tbody></table></div>
      </section>
    </div>
  </div>
  <script>
    const el = (id) => document.getElementById(id);
    const setRows = (id, rows) => { el(id).innerHTML = rows.join(""); };
    const badge = (value) => {
      const text = String(value || "");
      const lower = text.toLowerCase();
      const cls = /active|ok|true|listening|running/.test(lower) ? "ok" : (/failed|inactive|false|error|dead|missing/.test(lower) ? "bad" : "warn");
      return `<span class="${cls}">${text}</span>`;
    };
    const esc = (value) => String(value ?? "").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
    const fmtBytes = (v) => {
      const n = Number(v || 0);
      if (n > 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB";
      if (n > 1024) return (n / 1024).toFixed(1) + " KB";
      return n + " B";
    };
    const fmtEpoch = (v) => {
      const n = Number(v || 0);
      if (!n) return "-";
      return new Date(n * 1000).toLocaleString();
    };

    async function getJson(path, options) {
      const response = await fetch(path, options);
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error || payload.detail || JSON.stringify(payload));
      return payload;
    }

    async function loadAll() {
      const [summary, dns, proxy, httpAudit, aiSlices, capture, pcap, service] = await Promise.all([
        getJson("/gateway/api/summary"),
        getJson("/gateway/api/dns/recent"),
        getJson("/gateway/api/proxy/recent"),
        getJson("/gateway/api/http/recent"),
        getJson("/gateway/api/ai-slices/recent"),
        getJson("/gateway/api/capture/sessions"),
        getJson("/gateway/api/pcap/files"),
        getJson("/gateway/api/service-events/recent")
      ]);

      el("stamp").textContent = summary.observed_at || "unknown";
      const counts = ((summary.sqlite || {}).counts || []).reduce((acc, item) => { acc[item.table_name] = item.row_count; return acc; }, {});
      el("overview-cards").innerHTML = [
        ["dnsmasq", badge((summary.services || {}).dnsmasq)],
        ["sing-box", badge((summary.services || {}).sing_box)],
        ["ssh", badge((summary.services || {}).ssh)],
        ["mitmweb", badge((summary.services || {}).mitmweb)],
        ["nftables", badge((summary.services || {}).nftables)],
        ["capture", badge((summary.services || {}).capture)],
        ["22", badge((summary.ports || {})["22"])],
        ["18080", badge((summary.ports || {})["18080"])],
        ["2080", badge((summary.ports || {})["2080"])],
        ["8083", badge((summary.ports || {})["8083"])],
        ["8084", badge((summary.ports || {})["8084"])],
        ["pcap files", esc(((summary.pcap || {}).file_count ?? "-"))],
        ["pcap size", esc(fmtBytes((summary.pcap || {}).total_bytes ?? 0))],
        ["dns rows", esc(counts.dns_log_events ?? "-")],
        ["proxy rows", esc(counts.proxy_probe_events ?? "-")],
        ["capture rows", esc(counts.capture_sessions ?? "-")],
        ["http rows", esc(counts.http_events ?? "-")],
        ["slice rows", esc(counts.ai_slices ?? "-")]
      ].map(([label, value]) => `<div class="card"><div class="label">${label}</div><div class="value">${value}</div></div>`).join("");
      const mitmTail = (((summary.mitm || {}).journal_tail) || "").trim();
      if (mitmTail) {
        el("action-output").textContent = mitmTail;
      }

      setRows("dns-body", (dns.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.observed_at)}</td><td>${esc(row.phase)}</td><td>${esc(row.qtype)}</td><td>${esc(row.qname)}</td><td>${esc(row.client_addr)}</td></tr>`));
      setRows("proxy-body", (proxy.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.observed_at)}</td><td>${esc(row.target_url)}</td><td>${esc(row.exit_code)}</td><td>${esc(row.http_code)}</td></tr>`));
      setRows("http-body", (httpAudit.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.occurred_at)}</td><td>${badge(row.decision)}</td><td>${esc(row.target_host)}</td><td>${esc(row.status_code)}</td><td>${esc(row.client_ip)}</td></tr>`));
      setRows("slice-body", (aiSlices.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.created_at)}</td><td>${badge(row.status)}</td><td>${esc(row.src_ip)}</td><td>${esc(row.device_id)}</td><td>${esc(row.feature_summary)}</td></tr>`));
      setRows("capture-body", (capture.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.observed_at)}</td><td>${esc(row.interface_name)}</td><td>${esc(row.output_path)}</td><td>${badge(row.status)}</td></tr>`));
      setRows("pcap-body", (pcap.items || []).map((row) =>
        `<tr><td>${esc(row.name)}</td><td>${esc(fmtBytes(row.size_bytes))}</td><td>${esc(fmtEpoch(row.modified_epoch))}</td><td><a href="/gateway/api/pcap/download?name=${encodeURIComponent(row.name)}">download</a></td></tr>`));
      setRows("service-body", (service.items || []).map((row) =>
        `<tr><td>${esc(row.id)}</td><td>${esc(row.observed_at)}</td><td>${esc(row.service_name)}</td><td>${badge(row.status)}</td></tr>`));
    }

    async function runAction(path, body) {
      try {
        const payload = await getJson(path, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(body || {})
        });
        el("action-output").textContent = JSON.stringify(payload, null, 2);
        await loadAll();
      } catch (error) {
        el("action-output").textContent = String(error);
      }
    }

    document.addEventListener("click", (event) => {
      const action = event.target.getAttribute("data-action");
      const restart = event.target.getAttribute("data-restart");
      if (action === "refresh") loadAll().catch((err) => { el("action-output").textContent = String(err); });
      if (action === "snapshot") runAction("/gateway/api/ops/run-snapshot", {});
      if (action === "ssh-open") runAction("/gateway/api/ssh/open", {});
      if (action === "ssh-close") runAction("/gateway/api/ssh/close", {});
      if (restart) runAction("/gateway/api/ops/restart-service", { service_name: restart });
    });

    loadAll().catch((err) => { el("action-output").textContent = String(err); });
    setInterval(() => loadAll().catch(() => {}), 15000);
  </script>
</body>
</html>)HTML";
}

inline bool HandleGatewayAuditBinaryRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    const bool is_head = request.method == "HEAD";
    const bool is_read = request.method == "GET" || is_head;

    if (is_read && (request.path == "/" || request.path == "/index.html" || request.path == "/gateway" || request.path == "/gateway/")) {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "text/html; charset=utf-8";
        response->body = is_head ? std::string() : BuildGatewayAuditConsoleHtml();
        return true;
    }

    if (is_read && request.path == "/gateway/api/summary") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : BuildAuditSummaryJson(config);
        return true;
    }

    if (is_read && request.path == "/gateway/api/dns/recent") {
        bool ok = false;
        std::string error_message;
        const std::string items = ReadJsonArrayFromSqlite(
            "select id, observed_at, phase, qtype, qname, client_addr "
            "from dns_log_events order by id desc limit 50;",
            &ok,
            &error_message);
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : (ok
            ? BuildGatewayApiEnvelope("items", items)
            : BuildGatewayActionResultJson(false, "dns_recent", error_message));
        return true;
    }

    if (is_read && request.path == "/gateway/api/proxy/recent") {
        bool ok = false;
        std::string error_message;
        const std::string items = ReadJsonArrayFromSqlite(
            "select id, observed_at, target_url, exit_code, http_code "
            "from proxy_probe_events order by id desc limit 50;",
            &ok,
            &error_message);
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : (ok
            ? BuildGatewayApiEnvelope("items", items)
            : BuildGatewayActionResultJson(false, "proxy_recent", error_message));
        return true;
    }

    if (is_read && request.path == "/gateway/api/http/recent") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : codex_gateway_http_audit::BuildRecentHttpEventsJson();
        return true;
    }

    if (is_read && request.path == "/gateway/api/ai-slices/recent") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : codex_gateway_http_audit::BuildRecentAiSlicesJson();
        return true;
    }

    if (is_read && request.path == "/gateway/api/ssh/status") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : codex_gateway_http_audit::BuildSshStatusJson();
        return true;
    }

    if (!is_head && request.method == "POST" && request.path == "/gateway/api/ssh/open") {
        const CommandResult result = codex_gateway_http_audit::SetSshMaintenanceState(
            "open",
            GetQueryParamValue(request, "trace_id"),
            "gateway_ui");
        response->status_code = result.ok ? 200 : 500;
        response->status_text = result.ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = ResultToJson(result);
        return true;
    }

    if (!is_head && request.method == "POST" && request.path == "/gateway/api/ssh/close") {
        const CommandResult result = codex_gateway_http_audit::SetSshMaintenanceState(
            "close",
            GetQueryParamValue(request, "trace_id"),
            "gateway_ui");
        response->status_code = result.ok ? 200 : 500;
        response->status_text = result.ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = ResultToJson(result);
        return true;
    }

    if (!is_head && request.method == "POST" && request.path == "/gateway/api/http/fetch") {
        const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_ms");
        CommandResult result = codex_gateway_http_audit::RunHttpProxyFetch(
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

    if (is_read && request.path == "/gateway/api/capture/sessions") {
        bool ok = false;
        std::string error_message;
        const std::string items = ReadJsonArrayFromSqlite(
            "select id, observed_at, interface_name, output_path, status "
            "from capture_sessions order by id desc limit 50;",
            &ok,
            &error_message);
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : (ok
            ? BuildGatewayApiEnvelope("items", items)
            : BuildGatewayActionResultJson(false, "capture_sessions", error_message));
        return true;
    }

    if (is_read && request.path == "/gateway/api/service-events/recent") {
        bool ok = false;
        std::string error_message;
        const std::string items = ReadJsonArrayFromSqlite(
            "select id, observed_at, service_name, status "
            "from service_events order by id desc limit 50;",
            &ok,
            &error_message);
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : (ok
            ? BuildGatewayApiEnvelope("items", items)
            : BuildGatewayActionResultJson(false, "service_events", error_message));
        return true;
    }

    if (is_read && request.path == "/gateway/api/pcap/files") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = is_head ? std::string() : BuildGatewayApiEnvelope("items", BuildPcapFilesJson());
        return true;
    }

    if (is_read && request.path == "/gateway/api/pcap/download") {
        bool ok = false;
        std::string error_message;
        const std::string file_name = GetQueryParamValue(request, "name");
        const std::string payload = ReadDownloadablePcap(file_name, &ok, &error_message);
        response->status_code = ok ? 200 : 404;
        response->status_text = ok ? "OK" : "Not Found";
        response->content_type = ok ? "application/octet-stream" : "application/json";
        if (is_head) {
            response->body.clear();
        } else if (ok) {
            response->headers["Content-Disposition"] = "attachment; filename=\"" + file_name + "\"";
            response->body = payload;
        } else {
            response->body = BuildGatewayActionResultJson(false, "pcap_download", error_message);
        }
        return true;
    }

    if (request.method == "POST" && request.path == "/gateway/api/ops/run-snapshot") {
        std::string output;
        int exit_code = -1;
        const bool started = RunShellCapture(
            "systemctl start codex-gateway-snapshot.service",
            &output,
            &exit_code,
            nullptr);
        const bool ok = started && exit_code == 0;
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = BuildGatewayActionResultJson(ok, "run_snapshot", TrimWhitespace(output));
        return true;
    }

    if (request.method == "POST" && request.path == "/gateway/api/ops/restart-service") {
        const std::string service_name = ExtractJsonString(request.body, "service_name");
        const std::vector<std::string> allowed = {
            "dnsmasq",
            "sing-box",
            "nftables",
            "codex-gateway-capture",
            "codex-lan-agent"
        };
        if (std::find(allowed.begin(), allowed.end(), service_name) == allowed.end()) {
            response->status_code = 400;
            response->status_text = "Bad Request";
            response->content_type = "application/json";
            response->body = BuildGatewayActionResultJson(false, "restart_service", "service is not allowed");
            return true;
        }
        std::string output;
        int exit_code = -1;
        const bool started = RunShellCapture(
            "systemctl restart " + service_name,
            &output,
            &exit_code,
            nullptr);
        const bool ok = started && exit_code == 0;
        response->status_code = ok ? 200 : 500;
        response->status_text = ok ? "OK" : "Error";
        response->content_type = "application/json";
        response->body = BuildGatewayActionResultJson(ok, "restart_" + service_name, TrimWhitespace(output));
        return true;
    }

    return false;
}

}  // namespace codex_gateway_audit_ui
