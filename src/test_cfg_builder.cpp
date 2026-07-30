#include "CfGBuilder.h"

#include <iostream>
#include <string>

int main(int argc, char * argv[])
{
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: test_cfg_builder <source_file> [compile_db_dir]" << std::endl;
        return 1;
    }

    codex_lan_agent::ClangIndexerOptions options;
    options.source_file = argv[1];
    if (argc >= 3) {
        options.compile_db_dir = argv[2];
    }

    std::cout << "Building CFG for: " << options.source_file << std::endl;
    if (!options.compile_db_dir.empty()) {
        std::cout << "Compile DB dir: " << options.compile_db_dir << std::endl;
    }

    const codex_lan_agent::CfgBuildResult result =
        codex_lan_agent::RunCfgBuilder(options);

    if (!result.success) {
        std::cerr << "Status: failed" << std::endl;
        std::cerr << "Error: " << result.error << std::endl;
        return 2;
    }

    std::cout << "Status: success" << std::endl;
    std::cout << "Time: " << result.build_time_ms << " ms" << std::endl;
    std::cout << "Functions: " << result.total_functions << std::endl;
    std::cout << "Blocks: " << result.total_blocks << std::endl;
    std::cout << "Edges: " << result.total_edges << std::endl;

    std::cout << std::endl << "Functions:" << std::endl;
    for (const auto & func : result.functions) {
        std::cout << "  " << func.qualified_name
                  << " (complexity: " << func.cyclomatic_complexity
                  << ", blocks: " << func.block_count
                  << ", edges: " << func.edge_count
                  << ")" << std::endl;
    }

    return 0;
}