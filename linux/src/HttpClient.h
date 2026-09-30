#pragma once

#include <string>

namespace codex_lan_agent {

struct HttpResponse {
    bool ok = false;
    int status_code = 0;
    std::string body;
    std::string error_message;
};

bool CheckTcpEndpoint(
    const std::string & url,
    int timeout_ms,
    std::string * detail);

HttpResponse PostJson(
    const std::string & url,
    const std::string & json_body,
    int timeout_ms);

HttpResponse GetUrl(
    const std::string & url,
    int timeout_ms);

std::string JsonEscape(
    const std::string & value);

}  // namespace codex_lan_agent
