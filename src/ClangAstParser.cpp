#include "ClangAstParser.h"

#include "ClangAstVisitor.h"
#include "comm.h"
#include "StructuredJsonOperations.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

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

void WriteDataFlowRefJson(
    std::ostringstream & oss,
    const ClangDataFlowRef & ref,
    int indent)
{
    WriteIndent(oss, indent);
    oss << "{\n";
    WriteIndent(oss, indent + 1);
    oss << "\"symbol\": \"" << EscapeJsonString(ref.symbol) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"access_kind\": \"" << EscapeJsonString(ref.access_kind) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"function_name\": \"" << EscapeJsonString(ref.function_name) << "\",\n";
    WriteIndent(oss, indent + 1);
    oss << "\"stmt_kind\": \"" << EscapeJsonString(ref.stmt_kind) << "\",\n";
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

    oss << "  \"data_flow_refs\": [\n";
    for (size_t i = 0; i < result.data_flow_refs.size(); ++i) {
        WriteDataFlowRefJson(oss, result.data_flow_refs[i], 2);
        if (i + 1 < result.data_flow_refs.size()) {
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
    oss << "// Module: " << schema.module_name << "\n";
    oss << "// Namespaces: ";
    for (size_t i = 0; i < schema.namespaces.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << schema.namespaces[i];
    }
    oss << "\n\n";

    oss << "// === Object Declarations ===\n";
    for (const auto & cls : schema.classes) {
        std::string var_name = cls.name;
        if (!var_name.empty()) {
            var_name[0] = static_cast<char>(std::tolower(var_name[0]));
        }
        oss << cls.name << " " << var_name << ";\n";
    }
    oss << "\n";

    oss << "// === Global Inputs ===\n";
    oss << "// int global_roi_x0 = 0;\n";
    oss << "// int global_roi_y0 = 0;\n";
    oss << "// int global_roi_x1 = 0;\n";
    oss << "// int global_roi_y1 = 0;\n";
    oss << "// double global_threshold = 0.0;\n";
    oss << "// double global_gap = 0.0;\n\n";

    oss << "// === Method Calls ===\n";
    for (const auto & cls : schema.classes) {
        std::string var_name = cls.name;
        if (!var_name.empty()) {
            var_name[0] = static_cast<char>(std::tolower(var_name[0]));
        }

        for (const auto & method : cls.methods) {
            oss << "// " << var_name << "." << method.name << "(";
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << method.params[i].type.spelling;
                if (!method.params[i].name.empty()) {
                    oss << " " << method.params[i].name;
                }
            }
            oss << ") -> " << method.return_type.spelling << "\n";
        }
    }
    oss << "\n";

    oss << "// === Free Functions ===\n";
    for (const auto & func : schema.free_functions) {
        oss << "// " << func.qualified_name << "(";
        for (size_t i = 0; i < func.params.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << func.params[i].type.spelling;
            if (!func.params[i].name.empty()) {
                oss << " " << func.params[i].name;
            }
        }
        oss << ") -> " << func.return_type.spelling << "\n";
    }

    oss << "\n// === Contract ===\n";
    oss << "// contract.reset();\n";
    oss << "// if (result) { contract.pass(); } else { contract.fail(); }\n";
    oss << "// return;\n";

    return oss.str();
}

CxScriptValidationResult ValidateCxScriptSyntax(
    const std::string & script)
{
    CxScriptValidationResult result;

    static const std::vector<std::string> kForbiddenPatterns = {
        "\\bauto\\b",
        "\\bstd::vector\\b",
        "\\bstd::map\\b",
        "\\bnew\\s+",
        "\\bdelete\\b",
        "\\bfor\\s*\\(",
        "\\bwhile\\s*\\(",
        "\\bswitch\\s*\\(",
        "\\belse\\s+if\\b",
        "&&.*&&|\\|\\|.*\\|\\|",
        "\\breturn\\s+[^;]",
        "\\.open\\s*\\(",
        "\\.read\\s*\\(",
        "\\.write\\s*\\(",
        "cv::",
        "#include",
        "\\btemplate\\b",
        "\\bnamespace\\b",
        "\\bclass\\s+\\w+\\s*\\{",
        "\\bstruct\\s+\\w+\\s*\\{",
    };

    static const std::vector<std::string> kAllowedPatterns = {
        ";",
        "\\bint\\s+\\w+",
        "\\bdouble\\s+\\w+",
        "\\b[a-zA-Z]\\w*\\s+\\w+\\s*;",
        "\\w+\\.\\w+\\(",
        "//",
        "/\\*",
        "\\*/",
        "\\bif\\s*\\(",
        "\\breturn\\s*;",
        "\\bcontract\\.",
        "\\bglobal_\\w+",
        "=",
        "\\{",
        "\\}",
    };

    size_t forbidden_count = 0;
    for (const auto & pattern : kForbiddenPatterns) {
        std::regex re(pattern);
        auto begin = std::sregex_iterator(script.begin(), script.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            result.forbidden_constructs.push_back(
                "Forbidden pattern '" + pattern + "' found");
            forbidden_count++;
        }
    }

    if (forbidden_count > 0) {
        result.errors.push_back(
            "Found " + std::to_string(forbidden_count) + " forbidden CxScript patterns");
        result.valid = false;
    } else {
        result.valid = true;
        result.allowed_constructs.push_back("No forbidden patterns detected");
    }

    bool has_object_decl = std::regex_search(script, std::regex(R"([A-Z]\w*\s+\w+\s*;)"));
    if (has_object_decl) {
        result.allowed_constructs.push_back("Object declarations present");
    }

    bool has_contract = std::regex_search(script, std::regex(R"(contract\.)"));
    if (has_contract) {
        result.allowed_constructs.push_back("Contract API references present");
    }

    bool has_return = std::regex_search(script, std::regex(R"(\breturn\s*;)"));
    if (has_return) {
        result.allowed_constructs.push_back("Return statement present");
    }

    bool has_method_call = std::regex_search(script, std::regex(R"(\w+\.\w+\()"));
    if (has_method_call) {
        result.allowed_constructs.push_back("Method call references present");
    }

    bool has_global = std::regex_search(script, std::regex(R"(\bglobal_\w+)"));
    if (has_global) {
        result.allowed_constructs.push_back("Global variable references present");
    }

    result.summary = result.valid
        ? "CxScript syntax validation PASSED: " +
            std::to_string(result.allowed_constructs.size()) + " allowed constructs verified"
        : "CxScript syntax validation FAILED: " +
            std::to_string(forbidden_count) + " forbidden patterns found";

    return result;
}

std::string SerializeCxScriptValidationToJson(
    const CxScriptValidationResult & result)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"valid\":" << (result.valid ? "true" : "false") << ",";
    oss << "\"summary\":\"" << EscapeJsonString(result.summary) << "\",";

    auto write_array = [&oss](const std::string & key,
                              const std::vector<std::string> & items) {
        oss << "\"" << key << "\":[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << EscapeJsonString(items[i]) << "\"";
        }
        oss << "]";
    };

    write_array("errors", result.errors);
    oss << ",";
    write_array("warnings", result.warnings);
    oss << ",";
    write_array("allowed_constructs", result.allowed_constructs);
    oss << ",";
    write_array("forbidden_constructs", result.forbidden_constructs);
    oss << "}";

    return oss.str();
}

}
