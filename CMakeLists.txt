cmake_minimum_required(VERSION 3.21)

project(codex_lan_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
  CODEX_LAN_AGENT_COPY_TO_ROOT
  "Copy the built codex_lan_agent executable into the source root after build"
  OFF
)

add_executable(codex_lan_agent
  src/AgentConfig.cpp
  src/ProcessRunner.cpp
  src/HttpClient.cpp
  src/main.cpp
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
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )

  target_link_libraries(codex_lan_agent PRIVATE
    ws2_32
    winhttp
  )
endif()

if(CODEX_LAN_AGENT_COPY_TO_ROOT)
  add_custom_command(TARGET codex_lan_agent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:codex_lan_agent>"
      "${CMAKE_CURRENT_SOURCE_DIR}/codex_lan_agent.exe"
  )
endif()
