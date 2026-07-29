#ifndef CODEX_LAN_AGENT_CLANG_INDEXER_ADAPTER_H
#define CODEX_LAN_AGENT_CLANG_INDEXER_ADAPTER_H

#include <string>
#include <vector>
#include <map>

namespace codex_lan_agent
{
struct ClangTypeInfo
{
    std::string spelling;
    std::string qualified_name;
    bool is_const = false;
    bool is_ref = false;
    bool is_ptr = false;
    bool is_builtin = false;
};

struct ClangParamInfo
{
    std::string name;
    ClangTypeInfo type;
    bool has_default = false;
    std::string default_expr;
};

struct ClangMethodInfo
{
    std::string name;
    std::string qualified_name;
    ClangTypeInfo return_type;
    std::vector<ClangParamInfo> params;
    bool is_const = false;
    bool is_static = false;
    bool is_public = false;
    int source_line = 0;
    int source_col = 0;
    std::string source_file;
};

struct ClangClassInfo
{
    std::string name;
    std::string qualified_name;
    std::string namespace_name;
    std::vector<ClangMethodInfo> methods;
    int source_line = 0;
    int source_col = 0;
    std::string source_file;
};

struct ApiSchema
{
    std::string module_name;
    std::vector<ClangClassInfo> classes;
    std::vector<ClangMethodInfo> free_functions;
    std::vector<std::string> namespaces;
};

struct ClangCallRef
{
    std::string caller_name;
    std::string callee_name;
    std::string source_file;
    int source_line = 0;
    int source_col = 0;
};

struct ClangAstParseResult
{
    bool success = false;
    std::string error;
    ApiSchema schema;
    std::vector<ClangCallRef> call_refs;
    std::vector<std::string> target_namespaces;
    int elapsed_ms = 0;
};

typedef void (*ClangAstResultCallback)(
    const ClangAstParseResult & result,
    void * user_data);

struct ClangIndexerOptions
{
    std::string compile_db_dir;
    std::string compilation_database_path;
    std::string source_file;
    std::string output_json_path;
    std::vector<std::string> extra_include_dirs;
    std::vector<std::string> extra_defines;
    std::vector<std::string> target_namespaces;
    std::string project_root;
    bool verbose = false;
};

struct ClangIndexerResult
{
    bool success = false;
    std::string error;
    ApiSchema schema;
    int symbol_count = 0;
    int ref_count = 0;
    int elapsed_ms = 0;
};

bool BuildClangIndexerCommand(
    const std::string & indexer_path,
    const ClangIndexerOptions & options,
    std::string * command_line,
    std::vector<std::string> * args);

ClangIndexerResult ParseClangIndexerOutput(
    const std::string & output_json,
    const std::string & error_message);

std::string FindClangIndexerExecutablePath(
    const std::string & config_dir);

ClangAstParseResult RunClangAstParser(
    const ClangIndexerOptions & options);

void SetClangAstResultCallback(
    ClangAstResultCallback callback,
    void * user_data);

}

#endif
