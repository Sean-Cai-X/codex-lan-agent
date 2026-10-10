#include "CfGBuilder.h"
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_cfg <source_file>\n";
        return 1;
    }
    
    codex_lan_agent::ClangIndexerOptions options;
    options.source_file = argv[1];
    
    auto result = codex_lan_agent::RunCfgBuilder(options);
    
    if (result.success) {
        std::cout << "SUCCESS: " << result.total_functions << " functions, "
                  << result.total_blocks << " blocks, "
                  << result.total_edges << " edges, "
                  << result.build_time_ms << " ms\n";
        for (const auto & func : result.functions) {
            std::cout << "  Function: " << func.name 
                      << ", blocks=" << func.block_count 
                      << ", edges=" << func.edge_count
                      << ", complexity=" << func.cyclomatic_complexity << "\n";
        }
    } else {
        std::cout << "FAILED: " << result.error << "\n";
        return 2;
    }
    
    return 0;
}