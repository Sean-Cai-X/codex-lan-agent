#include "ClangAstParser.h"

#include "ClangAstVisitor.h"
#include "comm.h"
#include "StructuredJsonOperations.h"

#include <sstream>
#include <algorithm>

namespace codex_lan_agent
{
namespace
{

ClangAstResultCallback g_ast_result_callback = nullptr;
void * g_ast_result_user_data = nullptr;

std::string EscapeJsonString(const std::string & input)
{
    std::string result;
    result.reserve(input.size() + 16);

    for (char ch : input) {
        switch (ch) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\0': result += "\\0"; break;
            default:   result += ch; break;
        }
    }
    return result;
}

void WriteIndent(std::ostringstream & oss, int indent)
{
    for (int i = 0; i < indent; ++i) {
        oss << "  ";
    }
}

void WriteTypeInfoJson(
    std::ostringstream & oss,
    const ClangTypeInfo & type,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"spelling\": \"" << EscapeJsonString(type.spelling) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"qualified_name\": \"" << EscapeJsonString(type.qualified_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_const\": " << (type.is_const ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_ref\": " << (type.is_ref ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_ptr\": " << (type.is_ptr ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_builtin\": " << (type.is_builtin ? "true" : "false") << "\n";
    WriteIndent(oss, indent);
    oss << "}";
}

void WriteParamInfoJson(
    std::ostringstream & oss,
    const ClangParamInfo & param,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"name\": \"" << EscapeJsonString(param.name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"type\": ";
    WriteTypeInfoJson(oss, param.type, indent + 2);
    oss << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"has_default\": " << (param.has_default ? "true" : "false") << ",\n";
    if (param.has_default) {
        WriteIndent(oss, indent + 1);
        oss << "\"default_expr\": \"" << EscapeJsonString(param.default_expr) << "\"\n";
    } else {
        WriteIndent(oss, indent + 1);
        oss << "\"default_expr\": null\n";
    }
    WriteIndent(oss, indent);
    oss << "}";
}

void WriteMethodInfoJson(
    std::ostringstream & oss,
    const ClangMethodInfo & method,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"name\": \"" << EscapeJsonString(method.name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"qualified_name\": \"" << EscapeJsonString(method.qualified_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"return_type\": ";
    WriteTypeInfoJson(oss, method.return_type, indent + 2);
    oss << ",\n";

    WriteIndent(oss, indent + 1);
    oss << "\"params\": [\n";
    for (size_t i = 0; i < method.params.size(); ++i) {
        WriteParamInfoJson(oss, method.params[i], indent + 2);
        if (i + 1 < method.params.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    WriteIndent(oss, indent + 1);
    oss << "],\n";

    WriteIndent(oss, indent + 1);
    oss << "\"is_const\": " << (method.is_const ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_static\": " << (method.is_static ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"is_public\": " << (method.is_public ? "true" : "false") << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_line\": " << method.source_line << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_col\": " << method.source_col << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_file\": \"" << EscapeJsonString(method.source_file) << "\"\n";
    WriteIndent(oss, indent);
    oss << "}";
}

void WriteClassInfoJson(
    std::ostringstream & oss,
    const ClangClassInfo & cls,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"name\": \"" << EscapeJsonString(cls.name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"qualified_name\": \"" << EscapeJsonString(cls.qualified_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"namespace_name\": \"" << EscapeJsonString(cls.namespace_name) << "\",\n";

    WriteIndent(oss, indent + 1);
    oss << "\"methods\": [\n";
    for (size_t i = 0; i < cls.methods.size(); ++i) {
        WriteMethodInfoJson(oss, cls.methods[i], indent + 2);
        if (i + 1 < cls.methods.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    WriteIndent(oss, indent + 1);
    oss << "],\n";

    WriteIndent(oss, indent + 1);
    oss << "\"source_line\": " << cls.source_line << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_col\": " << cls.source_col << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_file\": \"" << EscapeJsonString(cls.source_file) << "\"\n";
    WriteIndent(oss, indent);
    oss << "}";
}

void WriteCallRefJson(
    std::ostringstream & oss,
    const ClangCallRef & ref,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"caller_name\": \"" << EscapeJsonString(ref.caller_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"callee_name\": \"" << EscapeJsonString(ref.callee_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_file\": \"" << EscapeJsonString(ref.source_file) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_line\": " << ref.source_line << ",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"source_col\": " << ref.source_col << "\n";
    WriteIndent(oss, indent);
    oss << "}";
}

}

ClangAstParseResult RunClangAstParser(
    const ClangIndexerOptions & options)
{
    ClangAstParseResult result = RunClangAstParserImpl(options);

    if (g_ast_result_callback && result.success) {
        g_ast_result_callback(result, g_ast_result_user_data);
    }

    return result;
}

void SetClangAstResultCallback(
    ClangAstResultCallback callback,
    void * user_data)
{
    g_ast_result_callback = callback;
    g_ast_result_user_data = user_data;
}

std::string SerializeAstParseResultToJson(
    const ClangAstParseResult & result)
{
    std::ostringstream oss;
    oss << "{\n";

    oss << "  \"success\": " << (result.success ? "true" : "false") << ",\n";
    oss << "  \"error\": \"" << EscapeJsonString(result.error) << "\",\n";

    oss << "  \"schema\": {\n";
    oss << "    \"module_name\": \"" << EscapeJsonString(result.schema.module_name) << "\",\n";

    oss << "    \"classes\": [\n";
    for (size_t i = 0; i < result.schema.classes.size(); ++i) {
        WriteClassInfoJson(oss, result.schema.classes[i], 4);
        if (i + 1 < result.schema.classes.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "    ],\n";

    oss << "    \"free_functions\": [\n";
    for (size_t i = 0; i < result.schema.free_functions.size(); ++i) {
        WriteMethodInfoJson(oss, result.schema.free_functions[i], 4);
        if (i + 1 < result.schema.free_functions.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "    ],\n";

    oss << "    \"namespaces\": [";
    for (size_t i = 0; i < result.schema.namespaces.size(); ++i) {
        oss << "\"" << EscapeJsonString(result.schema.namespaces[i]) << "\"";
        if (i + 1 < result.schema.namespaces.size()) {
            oss << ", ";
        }
    }
    oss << "]\n";

    oss << "  },\n";

    oss << "  \"call_refs\": [\n";
    for (size_t i = 0; i < result.call_refs.size(); ++i) {
        WriteCallRefJson(oss, result.call_refs[i], 2);
        if (i + 1 < result.call_refs.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "  ],\n";

    oss << "  \"target_namespaces\": [";
    for (size_t i = 0; i < result.target_namespaces.size(); ++i) {
        oss << "\"" << EscapeJsonString(result.target_namespaces[i]) << "\"";
        if (i + 1 < result.target_namespaces.size()) {
            oss << ", ";
        }
    }
    oss << "],\n";

    oss << "  \"elapsed_ms\": " << result.elapsed_ms << "\n";
    oss << "}\n";

    return oss.str();
}

ClangAstParseResult DeserializeAstParseResultFromJson(
    const std::string & json)
{
    ClangAstParseResult result;

    result.success = false;

    std::string success_str = ::ExtractJsonString(json, "success");
    result.success = (success_str == "true");

    result.error = ::ExtractJsonString(json, "error");

    result.schema.module_name = ::ExtractJsonString(json, "module_name");

    result.elapsed_ms = 0;
    std::string elapsed = ::ExtractJsonString(json, "elapsed_ms");
    if (!elapsed.empty()) {
        try {
            result.elapsed_ms = std::stoi(elapsed);
        } catch (...) {
        }
    }

    return result;
}

std::string BuildCxScriptFromSchema(const ApiSchema & schema)
{
    std::ostringstream oss;

    oss << "// Generated CxScript from Clang AST Schema\n";
    oss << "// Module: " << schema.module_name << "\n\n";

    for (const auto & ns : schema.namespaces) {
        oss << "// Namespace: " << ns << "\n";
    }

    oss << "\n";

    for (const auto & cls : schema.classes) {
        oss << "// Class: " << cls.qualified_name << "\n";
        oss << "// Methods: " << cls.methods.size() << "\n";

        for (const auto & method : cls.methods) {
            oss << "//   " << method.name << "(";
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (i > 0) {
                    oss << ", ";
                }
                oss << method.params[i].type.spelling;
                if (!method.params[i].name.empty()) {
                    oss << " " << method.params[i].name;
                }
            }
            oss << ") -> " << method.return_type.spelling << "\n";
        }
        oss << "\n";
    }

    for (const auto & func : schema.free_functions) {
        oss << "// Free Function: " << func.qualified_name << "\n";
    }

    return oss.str();
}

}
