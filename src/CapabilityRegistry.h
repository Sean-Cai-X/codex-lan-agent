#pragma once

#include <vector>

struct SemanticActionSpec {
    const char * action_id;
    const char * description;
    const char * tool;
    const char * arguments_schema;
    const char * success_rule;
    const char * fallback;
    const char * result_fields;
    const char * risk_level;
    const char * dry_run_supported;
    const char * side_effect;
    const char * requires_preview = "";
    const char * requires_approval = "";
    const char * requires_revert_plan = "";
    const char * requires_post_verify = "";
};

const std::vector<SemanticActionSpec> & GetSemanticActionSpecs();

struct McpCapabilitySpec {
    const char * capability_id;
    const char * provider_id;
    const char * provider;
    const char * input_contract;
    const char * output_contract;
    const char * storage;
    const char * return_format;
    const char * role;
    const char * notes;
};

const std::vector<McpCapabilitySpec> & GetMcpCapabilitySpecs();
