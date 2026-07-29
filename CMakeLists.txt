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
  src/ClangAstVisitor.cpp
  src/ClangAstParser.cpp
  src/CfGBuilder.cpp
  src/ProcessRunner.cpp
  src/HttpClient.cpp
  src/main.cpp
  ${CODEX_LAN_AGENT_CLIPS_CORE_SOURCES}
)

target_include_directories(codex_lan_agent PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(Threads REQUIRED)

target_link_libraries(codex_lan_agent PRIVATE
  Threads::Threads
)

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
