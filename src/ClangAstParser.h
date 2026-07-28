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

}

#endif
