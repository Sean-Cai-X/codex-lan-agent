cmake_minimum_required(VERSION 3.21)

project(codex_lan_agent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
  CODEX_LAN_AGENT_COPY_TO_ROOT
  "Copy the built codex_lan_agent executable into the source root after build"
  OFF
)

option(
  CODEX_LAN_AGENT_WITH_ROCKSDB
  "Enable optional native RocksDB mirror backend for task_memory KV snapshots"
  OFF
)
set(CODEX_LAN_AGENT_ROCKSDB_INCLUDE_DIR
  "D:/Codex-WorkDir/Sean_WorkDir/analysis_workspace/rocksdb-11.0.4/include"
  CACHE PATH "RocksDB include directory")
set(CODEX_LAN_AGENT_ROCKSDB_LIBRARY
  "D:/Codex-WorkDir/Sean_WorkDir/rocksdb-build/Release/rocksdb.lib"
  CACHE FILEPATH "RocksDB import/static library")

file(GLOB CODEX_LAN_AGENT_CLIPS_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/clips_core/*.c"
)
list(FILTER CODEX_LAN_AGENT_CLIPS_CORE_SOURCES EXCLUDE REGEX ".*/clips_standalone_main\\.c$")
# Intentionally do not add standalone CLIPS entry/demo sources. They are
# guarded in-source and must stay out of codex-lan-agent.

add_executable(codex_lan_agent
  src/AgentConfig.cpp
  src/CapabilityRegistry.cpp
  src/CmmBridge.cpp
  src/CmmToolResults.cpp
  src/ClangIndexerAdapter.cpp
  src/SemanticGridOperations.cpp
  src/ClangAstVisitor.cpp
  src/ClangAstParser.cpp
  src/CfGBuilder.cpp
  src/GraphSerialization.cpp
  src/ProcessRunner.cpp
  src/HttpClient.cpp
  src/TaskMemoryRocksDbOperations.cpp
  src/main.cpp
  ${CODEX_LAN_AGENT_CLIPS_CORE_SOURCES}
)

target_include_directories(codex_lan_agent PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# --- fact-factory 守卫层依赖 ---
# CppJieba (header-only, INTERFACE library)
set(CODEX_LAN_AGENT_CPPJIEBA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cppjieba"
  CACHE PATH "CppJieba source directory (header-only)")
if(EXISTS "${CODEX_LAN_AGENT_CPPJIEBA_DIR}/include/cppjieba/Jieba.hpp")
  target_include_directories(codex_lan_agent PRIVATE
    "${CODEX_LAN_AGENT_CPPJIEBA_DIR}/include"
  )
  target_compile_definitions(codex_lan_agent PRIVATE CODEX_LAN_AGENT_WITH_CPPJIEBA=1)
endif()

# marisa-trie (header + static lib)
set(CODEX_LAN_AGENT_MARISA_INCLUDE_DIR ""
  CACHE PATH "marisa-trie include directory (parent of marisa/)")
set(CODEX_LAN_AGENT_MARISA_LIBRARY ""
  CACHE FILEPATH "marisa-trie static library file")
if(NOT CODEX_LAN_AGENT_MARISA_INCLUDE_DIR)
  set(_default_marisa_inc "D:/weasel-latest/librime/deps/marisa-trie/include")
  if(EXISTS "${_default_marisa_inc}/marisa/trie.h")
    set(CODEX_LAN_AGENT_MARISA_INCLUDE_DIR "${_default_marisa_inc}")
  endif()
endif()
if(NOT CODEX_LAN_AGENT_MARISA_LIBRARY)
  set(_default_marisa_lib "D:/weasel-latest/librime/lib/marisa.lib")
  if(EXISTS "${_default_marisa_lib}")
    set(CODEX_LAN_AGENT_MARISA_LIBRARY "${_default_marisa_lib}")
  endif()
endif()
if(CODEX_LAN_AGENT_MARISA_INCLUDE_DIR AND EXISTS "${CODEX_LAN_AGENT_MARISA_INCLUDE_DIR}/marisa/trie.h")
  target_include_directories(codex_lan_agent PRIVATE
    "${CODEX_LAN_AGENT_MARISA_INCLUDE_DIR}"
  )
  target_compile_definitions(codex_lan_agent PRIVATE CODEX_LAN_AGENT_WITH_MARISA_TRIE=1)
  # 源码编译 marisa-trie (避免预编译 lib CRT /MT 冲突)
  set(_marisa_src "${CODEX_LAN_AGENT_MARISA_INCLUDE_DIR}/../lib")
  if(EXISTS "${_marisa_src}/marisa/grimoire.cc")
    target_sources(codex_lan_agent PRIVATE "${_marisa_src}/marisa/grimoire.cc")
  endif()
endif()

find_package(Threads REQUIRED)

target_link_libraries(codex_lan_agent PRIVATE
  Threads::Threads
)

if(CODEX_LAN_AGENT_WITH_ROCKSDB)
  if(NOT EXISTS "${CODEX_LAN_AGENT_ROCKSDB_INCLUDE_DIR}/rocksdb/db.h")
    message(FATAL_ERROR
      "CODEX_LAN_AGENT_WITH_ROCKSDB=ON but rocksdb/db.h was not found at "
      "${CODEX_LAN_AGENT_ROCKSDB_INCLUDE_DIR}")
  endif()
  if(NOT EXISTS "${CODEX_LAN_AGENT_ROCKSDB_LIBRARY}")
    message(FATAL_ERROR
      "CODEX_LAN_AGENT_WITH_ROCKSDB=ON but RocksDB library was not found at "
      "${CODEX_LAN_AGENT_ROCKSDB_LIBRARY}")
  endif()
  target_compile_features(codex_lan_agent PRIVATE cxx_std_20)
  target_compile_definitions(codex_lan_agent PRIVATE CODEX_LAN_AGENT_WITH_ROCKSDB=1)
  target_include_directories(codex_lan_agent PRIVATE
    "${CODEX_LAN_AGENT_ROCKSDB_INCLUDE_DIR}"
  )
  target_link_libraries(codex_lan_agent PRIVATE
    "${CODEX_LAN_AGENT_ROCKSDB_LIBRARY}"
  )
  if(MSVC)
    target_link_libraries(codex_lan_agent PRIVATE
      shlwapi
      rpcrt4
      iphlpapi
    )
  endif()
endif()

if(WIN32)
  target_compile_definitions(codex_lan_agent PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )

  target_link_libraries(codex_lan_agent PRIVATE
    ws2_32
    winhttp
    version
  )
endif()

option(
  CODEX_LAN_AGENT_BUNDLE_CMM
  "Copy the codebase-memory-mcp binary next to codex_lan_agent after build"
  OFF
)
set(CODEX_LAN_AGENT_CMM_PREBUILT_DIR ""
  CACHE PATH "Directory containing prebuilt codebase-memory-mcp binary")
set(CODEX_LAN_AGENT_CMM_SOURCE_DIR ""
  CACHE PATH "Source directory of codebase-memory-mcp (ignored when prebuilt exists)")

if(CODEX_LAN_AGENT_BUNDLE_CMM)
  set(CMM_PREBUILT_CANDIDATE "")
  if(NOT CODEX_LAN_AGENT_CMM_PREBUILT_DIR STREQUAL "")
    set(CMM_PREBUILT_CANDIDATE
      "${CODEX_LAN_AGENT_CMM_PREBUILT_DIR}/codebase-memory-mcp.exe")
  endif()
  if(NOT EXISTS "${CMM_PREBUILT_CANDIDATE}" AND NOT CODEX_LAN_AGENT_CMM_SOURCE_DIR STREQUAL "")
    set(CMM_PREBUILT_CANDIDATE
      "${CODEX_LAN_AGENT_CMM_SOURCE_DIR}/codebase-memory-mcp.exe")
  endif()
  if(EXISTS "${CMM_PREBUILT_CANDIDATE}")
    add_custom_command(TARGET codex_lan_agent POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMM_PREBUILT_CANDIDATE}"
        "$<TARGET_FILE_DIR:codex_lan_agent>/codebase-memory-mcp.exe"
    )
  else()
    message(WARNING
      "CODEX_LAN_AGENT_BUNDLE_CMM is ON but no codebase-memory-mcp.exe was found; "
      "set CODEX_LAN_AGENT_CMM_PREBUILT_DIR or CODEX_LAN_AGENT_CMM_SOURCE_DIR")
  endif()
endif()

if(CODEX_LAN_AGENT_COPY_TO_ROOT)
  add_custom_command(TARGET codex_lan_agent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:codex_lan_agent>"
      "${CMAKE_CURRENT_SOURCE_DIR}/codex_lan_agent.exe"
  )
endif()

option(
  CODEX_LAN_AGENT_BUILD_CLANG_INDEXER
  "Build cxparser_clang_indexer alongside codex_lan_agent (requires LLVM/Clang)"
  OFF
)

option(
  CODEX_LAN_AGENT_ENABLE_CLANG_AST
  "Enable Clang AST parsing (requires LLVM/Clang libraries)"
  OFF
)

if(CODEX_LAN_AGENT_ENABLE_CLANG_AST)
  set(CODEX_LLVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llvm" CACHE PATH "LLVM/Clang root directory (with include/ and lib/)")
  
  if(NOT EXISTS "${CODEX_LLVM_ROOT}/include/clang/AST/ASTConsumer.h")
    message(FATAL_ERROR
      "CODEX_LAN_AGENT_ENABLE_CLANG_AST=ON but LLVM/Clang headers not found at ${CODEX_LLVM_ROOT}. "
      "Please copy LLVM/Clang headers and libraries to third_party/llvm/")
  endif()
  
  if(NOT EXISTS "${CODEX_LLVM_ROOT}/lib/clangTooling.lib")
    message(FATAL_ERROR
      "CODEX_LAN_AGENT_ENABLE_CLANG_AST=ON but clangTooling.lib not found at ${CODEX_LLVM_ROOT}/lib/. "
      "Please copy LLVM/Clang .lib files to third_party/llvm/lib/.")
  endif()

  file(GLOB CODEX_LLVM_LIBS CONFIGURE_DEPENDS
    "${CODEX_LLVM_ROOT}/lib/*.lib"
  )

  target_include_directories(codex_lan_agent PRIVATE
    "${CODEX_LLVM_ROOT}/include"
  )

  target_link_libraries(codex_lan_agent PRIVATE
    ${CODEX_LLVM_LIBS}
  )

  target_compile_definitions(codex_lan_agent PRIVATE
    CODEX_LAN_AGENT_HAS_CLANG_AST=1
    _CRT_SECURE_NO_WARNINGS
  )

  message(STATUS "Clang AST parsing enabled (third_party/llvm)")
  message(STATUS "  Include: ${CODEX_LLVM_ROOT}/include")
  message(STATUS "  Lib:    ${CODEX_LLVM_ROOT}/lib")

  add_executable(test_cfg_builder
    src/test_cfg_builder.cpp
    src/CfGBuilder.cpp
    src/ClangIndexerAdapter.cpp
    src/ClangAstVisitor.cpp
    src/ClangAstParser.cpp
    src/GraphSerialization.cpp
  )

  target_include_directories(test_cfg_builder PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CODEX_LLVM_ROOT}/include"
  )

  target_compile_definitions(test_cfg_builder PRIVATE
    CODEX_LAN_AGENT_HAS_CLANG_AST=1
    _CRT_SECURE_NO_WARNINGS
  )

  target_link_libraries(test_cfg_builder PRIVATE
    ${CODEX_LLVM_LIBS}
  )

  if(WIN32)
    target_compile_definitions(test_cfg_builder PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX
    )
    target_link_libraries(test_cfg_builder PRIVATE
      ws2_32
      winhttp
      version
    )
  endif()
else()
  target_compile_definitions(codex_lan_agent PRIVATE
    CODEX_LAN_AGENT_HAS_CLANG_AST=0
  )
  message(STATUS "Clang AST parsing disabled (CODEX_LAN_AGENT_ENABLE_CLANG_AST=OFF)")
endif()

if(CODEX_LAN_AGENT_BUILD_CLANG_INDEXER)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/clang_indexer"
    "${CMAKE_CURRENT_BINARY_DIR}/clang_indexer"
  )
endif()
