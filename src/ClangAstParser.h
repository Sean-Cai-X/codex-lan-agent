#ifndef CODEX_LAN_AGENT_CLANG_AST_PARSER_H
#define CODEX_LAN_AGENT_CLANG_AST_PARSER_H

#include "ClangIndexerAdapter.h"

#include <string>

namespace codex_lan_agent
{

std::string SerializeAstParseResultToJson(
    const ClangAstParseResult & result);

ClangAstParseResult DeserializeAstParseResultFromJson(
    const std::string & json);

std::string BuildCxScriptFromSchema(
    const ApiSchema & schema);

struct CxScriptValidationResult {
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> allowed_constructs;
    std::vector<std::string> forbidden_constructs;
    std::string summary;
};

CxScriptValidationResult ValidateCxScriptSyntax(
    const std::string & script);

std::string SerializeCxScriptValidationToJson(
    const CxScriptValidationResult & result);

}

#endif
