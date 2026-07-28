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

find_package(Qt6 COMPONENTS Core QUIET)
if(Qt6_FOUND)
  set(CODEX_LAN_AGENT_QT_CORE Qt6::Core)
else()
  find_package(Qt5 COMPONENTS Core QUIET)
  if(Qt5_FOUND)
    set(CODEX_LAN_AGENT_QT_CORE Qt5::Core)
  endif()
endif()

if(CODEX_LAN_AGENT_QT_CORE)
  add_executable(directory_access
    tools/directory_access/main.cpp
  )
  target_link_libraries(directory_access PRIVATE
    ${CODEX_LAN_AGENT_QT_CORE}
  )

  add_executable(git_snapshot
    tools/git_snapshot/main.cpp
  )
  target_link_libraries(git_snapshot PRIVATE
    ${CODEX_LAN_AGENT_QT_CORE}
  )

  if(CODEX_LAN_AGENT_COPY_TO_ROOT)
    add_custom_command(TARGET directory_access POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:directory_access>"
        "${CMAKE_CURRENT_SOURCE_DIR}/directory_access.exe"
    )
    add_custom_command(TARGET git_snapshot POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:git_snapshot>"
        "${CMAKE_CURRENT_SOURCE_DIR}/git_snapshot.exe"
    )
  endif()
else()
  message(WARNING "Qt Core was not found; git_snapshot helper will not be built")
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

  set(CODEX_CLANG_AST_SOURCES
    src/ClangAstVisitor.cpp
    src/ClangAstParser.cpp
  )

  target_include_directories(codex_lan_agent PRIVATE
    "${CODEX_LLVM_ROOT}/include"
  )

  target_link_libraries(codex_lan_agent PRIVATE
    # Clang Tooling (核心框架)
    "${CODEX_LLVM_ROOT}/lib/clangTooling.lib"
    "${CODEX_LLVM_ROOT}/lib/clangToolingCore.lib"
    "${CODEX_LLVM_ROOT}/lib/clangToolingInclusions.lib"
    "${CODEX_LLVM_ROOT}/lib/clangToolingRefactoring.lib"
    "${CODEX_LLVM_ROOT}/lib/clangToolingSyntax.lib"
    
    # Clang AST & Frontend
    "${CODEX_LLVM_ROOT}/lib/clangAST.lib"
    "${CODEX_LLVM_ROOT}/lib/clangFrontend.lib"
    "${CODEX_LLVM_ROOT}/lib/clangFrontendTool.lib"
    "${CODEX_LLVM_ROOT}/lib/clangLex.lib"
    "${CODEX_LLVM_ROOT}/lib/clangSema.lib"
    "${CODEX_LLVM_ROOT}/lib/clangParse.lib"
    "${CODEX_LLVM_ROOT}/lib/clangBasic.lib"
    "${CODEX_LLVM_ROOT}/lib/clangEdit.lib"
    "${CODEX_LLVM_ROOT}/lib/clangAnalysis.lib"
    "${CODEX_LLVM_ROOT}/lib/clangDriver.lib"
    "${CODEX_LLVM_ROOT}/lib/clangSerialization.lib"
    "${CODEX_LLVM_ROOT}/lib/clangRewrite.lib"
    "${CODEX_LLVM_ROOT}/lib/clangRewriteFrontend.lib"
    "${CODEX_LLVM_ROOT}/lib/clangIndex.lib"
    "${CODEX_LLVM_ROOT}/lib/clangSupport.lib"
    "${CODEX_LLVM_ROOT}/lib/clangFormat.lib"
    "${CODEX_LLVM_ROOT}/lib/clangASTMatchers.lib"
    
    # Clang 扩展模块
    "${CODEX_LLVM_ROOT}/lib/clangARCMigrate.lib"
    "${CODEX_LLVM_ROOT}/lib/clangCodeGen.lib"
    "${CODEX_LLVM_ROOT}/lib/clangCrossTU.lib"
    "${CODEX_LLVM_ROOT}/lib/clangDependencyScanning.lib"
    "${CODEX_LLVM_ROOT}/lib/clangStaticAnalyzerCore.lib"
    "${CODEX_LLVM_ROOT}/lib/clangStaticAnalyzerFrontend.lib"
    
    # Clang-Tidy (来自 clang-tools-extra)
    "${CODEX_LLVM_ROOT}/lib/clangTidy.lib"
    "${CODEX_LLVM_ROOT}/lib/clangTidyMiscModule.lib"
    
    # LLVM 核心库 (LLVM 18 更新)
    "${CODEX_LLVM_ROOT}/lib/LLVMSupport.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMCore.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMTarget.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMMC.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMMCParser.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMObject.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMIRReader.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMAsmParser.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMAsmPrinter.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMCodeGen.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMCodeGenTypes.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMBitReader.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMBitWriter.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMBitstreamReader.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMObjectYAML.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMOption.lib"
    
    # LLVM X86 特定库 (替代 LLVMTargetTransformInfo 和 LLVMX86Utils)
    "${CODEX_LLVM_ROOT}/lib/LLVMX86CodeGen.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMX86Desc.lib"
    "${CODEX_LLVM_ROOT}/lib/LLVMX86Info.lib"
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
