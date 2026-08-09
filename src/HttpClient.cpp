#include "HttpClient.h"

#include <cstring>
#include <cerrno>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOWINUSER
#define NOWINUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace codex_lan_agent {
namespace {

struct ParsedUrl {
#ifdef _WIN32
    std::wstring host;
    INTERNET_PORT port = 0;
#else
    std::string host;
    int port = 0;
#endif
    std::string path;
    bool secure = false;
    bool valid = false;
};

class WsaSession {
public:
    WsaSession() {
#ifdef _WIN32
        valid_ = WSAStartup(MAKEWORD(2, 2), &data_) == 0;
#else
        valid_ = true;
#endif
    }

    ~WsaSession() {
#ifdef _WIN32
        if (valid_) {
            WSACleanup();
        }
#endif
    }

    bool valid() const {
        return valid_;
    }

private:
#ifdef _WIN32
    WSADATA data_{};
#endif
    bool valid_ = false;
};

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string & text) {
    if (text.empty()) {
        return std::wstring();
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return std::wstring();
    }

    std::wstring converted(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, converted.data(), length);
    return converted;
}

std::string WideToUtf8(const std::wstring & text) {
    if (text.empty()) {
        return std::string();
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return std::string();
    }

    std::string converted(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, converted.data(), length, nullptr, nullptr);
    return converted;
}
#endif

std::string NormalizePathString(const std::string & path) {
    if (path.empty()) {
        return "/";
    }
    return path;
}

ParsedUrl ParseUrl(const std::string & url) {
    ParsedUrl parsed;
#ifdef _WIN32
    const std::wstring wide_url = Utf8ToWide(url);
    if (wide_url.empty()) {
        return parsed;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        return parsed;
    }

    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path = WideToUtf8(std::wstring(components.lpszUrlPath, components.dwUrlPathLength));
    if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo) {
        parsed.path.append(WideToUtf8(std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength)));
    }
    if (parsed.path.empty()) {
        parsed.path = "/";
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.valid = !parsed.host.empty() && parsed.port != 0;
#else
    const std::string http_prefix = "http://";
    const std::string https_prefix = "https://";
    std::size_t start = std::string::npos;
    if (url.rfind(http_prefix, 0) == 0) {
        start = http_prefix.size();
        parsed.secure = false;
        parsed.port = 80;
    } else if (url.rfind(https_prefix, 0) == 0) {
        start = https_prefix.size();
        parsed.secure = true;
        parsed.port = 443;
    } else {
        return parsed;
    }

    const std::size_t slash_pos = url.find('/', start);
    const std::string host_port = slash_pos == std::string::npos
        ? url.substr(start)
        : url.substr(start, slash_pos - start);
    const std::size_t colon_pos = host_port.find(':');
    parsed.host = colon_pos == std::string::npos ? host_port : host_port.substr(0, colon_pos);
    if (colon_pos != std::string::npos) {
        parsed.port = std::atoi(host_port.c_str() + colon_pos + 1);
    }
    parsed.path = slash_pos == std::string::npos ? "/" : url.substr(slash_pos);
    parsed.valid = !parsed.host.empty() && parsed.port > 0;
#endif
    return parsed;
}

bool CheckSocketConnectResult(
#ifdef _WIN32
    SOCKET socket_handle
#else
    int socket_handle
#endif
) {
    int socket_error = 0;
#ifdef _WIN32
    int option_length = sizeof(socket_error);
    if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socket_error), &option_length) != 0) {
        return false;
    }
#else
    socklen_t option_length = static_cast<socklen_t>(sizeof(socket_error));
    if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR, &socket_error, &option_length) != 0) {
        return false;
    }
#endif
    return socket_error == 0;
}

int SendFlagsPortable() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

#ifdef _WIN32
std::string BuildWinHttpErrorMessage(DWORD error_code) {
    std::ostringstream buffer;
    buffer << "winhttp error " << error_code;
    return buffer.str();
}
#endif

#ifndef _WIN32
int CloseSocketPortable(int socket_handle) {
    return close(socket_handle);
}
#else
int CloseSocketPortable(SOCKET socket_handle) {
    return closesocket(socket_handle);
}
#endif

HttpResponse SendHttpPlain(
    const ParsedUrl & parsed,
    const std::string & method,
    const std::string & content_type,
    const std::string & request_body,
    int timeout_ms) {
    HttpResponse response;
    if (parsed.secure) {
        response.error_message = "https is not supported in the portable fallback transport";
        return response;
    }

    WsaSession wsa;
    if (!wsa.valid()) {
        response.error_message = "socket startup failed";
        return response;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo * result = nullptr;
#ifdef _WIN32
    const std::string host = WideToUtf8(parsed.host);
#else
    const std::string host = parsed.host;
#endif
    const std::string port = std::to_string(parsed.port);
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        response.error_message = "getaddrinfo failed";
        return response;
    }

#ifdef _WIN32
    SOCKET socket_handle = INVALID_SOCKET;
#else
    int socket_handle = -1;
#endif
    for (addrinfo * current = result; current != nullptr; current = current->ai_next) {
#ifdef _WIN32
        socket_handle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_handle == INVALID_SOCKET) {
#else
        socket_handle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_handle < 0) {
#endif
            continue;
        }
        if (connect(
#ifdef _WIN32
                socket_handle,
#else
                socket_handle,
#endif
                current->ai_addr,
                static_cast<int>(current->ai_addrlen)) == 0) {
            break;
        }
        CloseSocketPortable(
#ifdef _WIN32
            socket_handle
#else
            socket_handle
#endif
        );
        socket_handle =
#ifdef _WIN32
            INVALID_SOCKET
#else
            -1
#endif
        ;
    }
    freeaddrinfo(result);

    if (
#ifdef _WIN32
        socket_handle == INVALID_SOCKET
#else
        socket_handle < 0
#endif
    ) {
        response.error_message = "failed to connect";
        return response;
    }

    const std::string path = NormalizePathString(parsed.path);
    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << parsed.port << "\r\n";
    if (!content_type.empty()) {
        request << "Content-Type: " << content_type << "\r\n";
    }
    if (!request_body.empty()) {
        request << "Content-Length: " << request_body.size() << "\r\n";
    }
    request << "Connection: close\r\n\r\n"
            << request_body;
    const std::string raw = request.str();

    int sent_total = 0;
    while (sent_total < static_cast<int>(raw.size())) {
#ifdef _WIN32
        const int sent = send(socket_handle, raw.data() + sent_total, static_cast<int>(raw.size()) - sent_total, 0);
#else
        const int sent = static_cast<int>(send(
            socket_handle,
            raw.data() + sent_total,
            raw.size() - sent_total,
            SendFlagsPortable()));
#endif
        if (sent <= 0) {
            response.error_message = "failed to send request";
            CloseSocketPortable(
#ifdef _WIN32
                socket_handle
#else
                socket_handle
#endif
            );
            return response;
        }
        sent_total += sent;
    }

    std::string raw_response;
    char buffer[4096];
    while (true) {
#ifdef _WIN32
        const int received = recv(socket_handle, buffer, sizeof(buffer), 0);
#else
        const int received = static_cast<int>(recv(socket_handle, buffer, sizeof(buffer), 0));
#endif
        if (received <= 0) {
            break;
        }
        raw_response.append(buffer, buffer + received);
    }
    CloseSocketPortable(
#ifdef _WIN32
        socket_handle
#else
        socket_handle
#endif
    );

    const std::size_t status_end = raw_response.find("\r\n");
    if (status_end == std::string::npos) {
        response.error_message = "invalid http response";
        return response;
    }
    const std::string status_line = raw_response.substr(0, status_end);
    std::istringstream status_stream(status_line);
    std::string http_version;
    status_stream >> http_version >> response.status_code;

    const std::size_t header_end = raw_response.find("\r\n\r\n");
    response.body = header_end == std::string::npos ? std::string() : raw_response.substr(header_end + 4);
    response.ok = response.status_code >= 200 && response.status_code < 300;
    return response;
}

HttpResponse PostJsonHttpPlain(
    const ParsedUrl & parsed,
    const std::string & json_body,
    int timeout_ms) {
    return SendHttpPlain(parsed, "POST", "application/json", json_body, timeout_ms);
}

HttpResponse GetHttpPlain(
    const ParsedUrl & parsed,
    int timeout_ms) {
    return SendHttpPlain(parsed, "GET", std::string(), std::string(), timeout_ms);
}

}  // namespace

bool CheckTcpEndpoint(
    const std::string & url,
    int timeout_ms,
    std::string * detail) {
    const ParsedUrl parsed = ParseUrl(url);
    if (!parsed.valid) {
        if (detail) {
            *detail = "invalid url";
        }
        return false;
    }

    WsaSession wsa;
    if (!wsa.valid()) {
        if (detail) {
            *detail = "winsock startup failed";
        }
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo * result = nullptr;
#ifdef _WIN32
    const std::string host = WideToUtf8(parsed.host);
#else
    const std::string host = parsed.host;
#endif
    const std::string port = std::to_string(parsed.port);
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        if (detail) {
            *detail = "getaddrinfo failed";
        }
        return false;
    }

    bool connected = false;
    for (addrinfo * current = result; current != nullptr && !connected; current = current->ai_next) {
#ifdef _WIN32
        SOCKET socket_handle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_handle == INVALID_SOCKET) {
            continue;
        }

        u_long non_blocking = 1;
        ioctlsocket(socket_handle, FIONBIO, &non_blocking);

        const int connect_result = connect(socket_handle, current->ai_addr, static_cast<int>(current->ai_addrlen));
        if (connect_result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(socket_handle, &write_set);

            timeval timeout{};
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            const int select_result = select(0, nullptr, &write_set, nullptr, &timeout);
            connected = select_result > 0 && CheckSocketConnectResult(socket_handle);
        } else {
            connected = connect_result == 0;
        }

        closesocket(socket_handle);
#else
        int socket_handle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_handle < 0) {
            continue;
        }

        const int flags = fcntl(socket_handle, F_GETFL, 0);
        fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK);

        const int connect_result = connect(socket_handle, current->ai_addr, static_cast<int>(current->ai_addrlen));
        if (connect_result != 0 && errno == EINPROGRESS) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(socket_handle, &write_set);

            timeval timeout{};
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            const int select_result = select(socket_handle + 1, nullptr, &write_set, nullptr, &timeout);
            connected = select_result > 0 && CheckSocketConnectResult(socket_handle);
        } else {
            connected = connect_result == 0;
        }

        close(socket_handle);
#endif
    }

    freeaddrinfo(result);

    if (detail) {
        *detail = connected ? "tcp connect ok" : "tcp connect failed";
    }
    return connected;
}

HttpResponse PostJson(
    const std::string & url,
    const std::string & json_body,
    int timeout_ms) {
    HttpResponse response;
    const ParsedUrl parsed = ParseUrl(url);
    if (!parsed.valid) {
        response.error_message = "invalid url";
        return response;
    }

#ifndef _WIN32
    return PostJsonHttpPlain(parsed, json_body, timeout_ms);
#else
    if (!parsed.secure) {
        return PostJsonHttpPlain(parsed, json_body, timeout_ms);
    }

    const std::wstring wide_path = Utf8ToWide(parsed.path);

    HINTERNET session = WinHttpOpen(
        L"codex-lan-agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        response.error_message = "failed to open winhttp session";
        return response;
    }

    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    if (!connection) {
        response.error_message = "failed to connect";
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        wide_path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!request) {
        response.error_message = "failed to open request";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    WinHttpSetTimeouts(request, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    const wchar_t * headers = L"Content-Type: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1L),
        const_cast<char *>(json_body.data()),
        static_cast<DWORD>(json_body.size()),
        static_cast<DWORD>(json_body.size()),
        0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        response.error_message = BuildWinHttpErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_code_size,
        WINHTTP_NO_HEADER_INDEX);
    response.status_code = static_cast<int>(status_code);

    std::string body;
    DWORD available_size = 0;
    while (WinHttpQueryDataAvailable(request, &available_size) && available_size > 0) {
        std::string chunk(static_cast<std::size_t>(available_size), '\0');
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available_size, &downloaded)) {
            response.error_message = BuildWinHttpErrorMessage(GetLastError());
            break;
        }
        chunk.resize(downloaded);
        body += chunk;
        available_size = 0;
    }

    response.ok = response.error_message.empty() && response.status_code >= 200 && response.status_code < 300;
    response.body = body;

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
#endif
}

HttpResponse GetUrl(
    const std::string & url,
    int timeout_ms) {
    HttpResponse response;
    const ParsedUrl parsed = ParseUrl(url);
    if (!parsed.valid) {
        response.error_message = "invalid url";
        return response;
    }

#ifndef _WIN32
    return GetHttpPlain(parsed, timeout_ms);
#else
    if (!parsed.secure) {
        return GetHttpPlain(parsed, timeout_ms);
    }

    const std::wstring wide_path = Utf8ToWide(parsed.path);

    HINTERNET session = WinHttpOpen(
        L"codex-lan-agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        response.error_message = "failed to open winhttp session";
        return response;
    }

    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    if (!connection) {
        response.error_message = "failed to connect";
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        wide_path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!request) {
        response.error_message = "failed to open request";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    WinHttpSetTimeouts(request, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    const BOOL sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        response.error_message = BuildWinHttpErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_code_size,
        WINHTTP_NO_HEADER_INDEX);
    response.status_code = static_cast<int>(status_code);

    std::string body;
    DWORD available_size = 0;
    while (WinHttpQueryDataAvailable(request, &available_size) && available_size > 0) {
        std::string chunk(static_cast<std::size_t>(available_size), '\0');
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available_size, &downloaded)) {
            response.error_message = BuildWinHttpErrorMessage(GetLastError());
            break;
        }
        chunk.resize(downloaded);
        body += chunk;
        available_size = 0;
    }

    response.ok = response.error_message.empty() && response.status_code >= 200 && response.status_code < 300;
    response.body = body;

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
#endif
}

std::string JsonEscape(const std::string & value) {
    std::ostringstream buffer;
    for (char character : value) {
        switch (character) {
        case '\\':
            buffer << "\\\\";
            break;
        case '"':
            buffer << "\\\"";
            break;
        case '\n':
            buffer << "\\n";
            break;
        case '\r':
            buffer << "\\r";
            break;
        case '\t':
            buffer << "\\t";
            break;
        default:
            buffer << character;
            break;
        }
    }
    return buffer.str();
}

}  // namespace codex_lan_agent
