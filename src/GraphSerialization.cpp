#include "GraphSerialization.h"

#include <map>
#include <sstream>

namespace codex_lan_agent
{
namespace
{

std::string EscapeDotLabel(const std::string & value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
        case '\r':
            escaped += ' ';
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

std::string EscapeJsonValue(const std::string & value)
{
    std::ostringstream escaped;
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (ch < 0x20) {
                escaped << "\\u";
                const char * digits = "0123456789abcdef";
                escaped << '0' << '0' << digits[(ch >> 4) & 0x0F] << digits[ch & 0x0F];
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

std::string StableNodeId(const std::string & name, int index)
{
    std::ostringstream id;
    id << "n" << index << "_";
    for (char ch : name) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            id << ch;
        } else {
            id << '_';
        }
    }
    return id.str();
}

} // namespace

std::string SerializeCallGraphJson(
    const std::vector<std::string> & nodes,
    const std::vector<CallGraphEdgeInfo> & edges)
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        json << "    {\"id\":\"" << EscapeJsonValue(nodes[i]) << "\",\"name\":\""
             << EscapeJsonValue(nodes[i]) << "\"}";
        if (i + 1 < nodes.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"edges\": [\n";
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto & edge = edges[i];
        json << "    {"
             << "\"caller\":\"" << EscapeJsonValue(edge.caller) << "\","
             << "\"callee\":\"" << EscapeJsonValue(edge.callee) << "\","
             << "\"count\":" << edge.count << ","
             << "\"first_source_file\":\"" << EscapeJsonValue(edge.first_source_file) << "\","
             << "\"first_source_line\":" << edge.first_source_line << ","
             << "\"first_source_col\":" << edge.first_source_col
             << "}";
        if (i + 1 < edges.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ]\n";
    json << "}";
    return json.str();
}

std::string SerializeCallGraphDot(
    const std::vector<std::string> & nodes,
    const std::vector<CallGraphEdgeInfo> & edges)
{
    std::map<std::string, std::string> ids;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ids[nodes[i]] = StableNodeId(nodes[i], static_cast<int>(i));
    }

    std::ostringstream dot;
    dot << "digraph call_graph {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=box];\n";
    for (const auto & node : nodes) {
        dot << "  " << ids[node] << " [label=\"" << EscapeDotLabel(node) << "\"];\n";
    }
    for (const auto & edge : edges) {
        dot << "  " << ids[edge.caller] << " -> " << ids[edge.callee];
        if (edge.count > 1) {
            dot << " [label=\"" << edge.count << "\"]";
        }
        dot << ";\n";
    }
    dot << "}\n";
    return dot.str();
}

std::string SerializeDfgJson(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges)
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        json << "    {\"id\":\"" << EscapeJsonValue(nodes[i]) << "\",\"name\":\""
             << EscapeJsonValue(nodes[i]) << "\"}";
        if (i + 1 < nodes.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"edges\": [\n";
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto & edge = edges[i];
        json << "    {"
             << "\"source\":\"" << EscapeJsonValue(edge.source) << "\","
             << "\"target\":\"" << EscapeJsonValue(edge.target) << "\","
             << "\"kind\":\"" << EscapeJsonValue(edge.kind) << "\","
             << "\"count\":" << edge.count << ","
             << "\"first_source_line\":" << edge.first_source_line
             << "}";
        if (i + 1 < edges.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ]\n";
    json << "}";
    return json.str();
}

std::string SerializeDfgDot(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges)
{
    std::map<std::string, std::string> ids;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ids[nodes[i]] = StableNodeId(nodes[i], static_cast<int>(i));
    }

    std::ostringstream dot;
    dot << "digraph data_flow_graph {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=ellipse];\n";
    for (const auto & node : nodes) {
        dot << "  " << ids[node] << " [label=\"" << EscapeDotLabel(node) << "\"];\n";
    }
    for (const auto & edge : edges) {
        dot << "  " << ids[edge.source] << " -> " << ids[edge.target]
            << " [label=\"" << EscapeDotLabel(edge.kind);
        if (edge.count > 1) {
            dot << ":" << edge.count;
        }
        dot << "\"];\n";
    }
    dot << "}\n";
    return dot.str();
}

std::string SerializeProgramSliceJson(
    const std::string & symbol,
    const std::string & direction,
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges,
    const std::vector<int> & lines)
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"symbol\": \"" << EscapeJsonValue(symbol) << "\",\n";
    json << "  \"direction\": \"" << EscapeJsonValue(direction) << "\",\n";
    json << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        json << "    {\"id\":\"" << EscapeJsonValue(nodes[i]) << "\",\"name\":\""
             << EscapeJsonValue(nodes[i]) << "\"}";
        if (i + 1 < nodes.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"edges\": [\n";
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto & edge = edges[i];
        json << "    {"
             << "\"source\":\"" << EscapeJsonValue(edge.source) << "\","
             << "\"target\":\"" << EscapeJsonValue(edge.target) << "\","
             << "\"kind\":\"" << EscapeJsonValue(edge.kind) << "\","
             << "\"count\":" << edge.count << ","
             << "\"first_source_line\":" << edge.first_source_line
             << "}";
        if (i + 1 < edges.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"lines\": [";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            json << ",";
        }
        json << lines[i];
    }
    json << "]\n";
    json << "}";
    return json.str();
}

std::string SerializeProgramSliceDot(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges)
{
    return SerializeDfgDot(nodes, edges);
}

std::string SerializeSourceLinesJson(
    const std::vector<int> & line_numbers,
    const std::vector<std::string> & source_lines)
{
    std::ostringstream json;
    json << "[\n";
    for (std::size_t i = 0; i < line_numbers.size(); ++i) {
        const int line_number = line_numbers[i];
        std::string text;
        if (line_number > 0 &&
            static_cast<std::size_t>(line_number) <= source_lines.size()) {
            text = source_lines[static_cast<std::size_t>(line_number - 1)];
        }
        json << "  {\"line\":" << line_number
             << ",\"text\":\"" << EscapeJsonValue(text) << "\"}";
        if (i + 1 < line_numbers.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "]";
    return json.str();
}

} // namespace codex_lan_agent
