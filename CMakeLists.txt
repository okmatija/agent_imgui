# agent_imgui — an LLM "agent in your Dear ImGui app" library.
# See README.md for how to consume it.

cmake_minimum_required(VERSION 3.16)

project(agent_imgui CXX)

# Standalone build (this repo is the top-level project) vs. embedded in a host
# project that already provides the ImGui targets.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_agent_imgui_standalone ON)
else()
    set(_agent_imgui_standalone OFF)
endif()

option(AGENT_IMGUI_BUILD_EXAMPLE
       "Build the portable SDL3 example application"
       ${_agent_imgui_standalone})

# agent_imgui plugs into the host application's Dear ImGui build: the panel
# renders with ImGui and the UI-driving tool uses the ImGui Test Engine. If the
# host hasn't already defined `dear_imgui` and `imgui_test_engine`, fetch and
# define them ourselves (standalone build). Either way a single copy of ImGui
# ends up in the final binary.
if(NOT TARGET dear_imgui OR NOT TARGET imgui_test_engine)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)
endif()

add_library(agent_imgui STATIC
    agent_imgui/http_transport.cc
    agent_imgui/http_transport.h
    agent_imgui/llm_provider.h
    agent_imgui/llm_claude.cc
    agent_imgui/llm_claude.h
    agent_imgui/llm_gemini.cc
    agent_imgui/llm_gemini.h
    agent_imgui/llm_mock.h
    agent_imgui/llm_panel.cc
    agent_imgui/llm_panel.h
    agent_imgui/source_search.cc
    agent_imgui/source_search.h
    agent_imgui/test_runner.cc
    agent_imgui/test_runner.h
    agent_imgui/ui_agent.cc
    agent_imgui/ui_agent.h
)

# Public so consumers get the `#include "agent_imgui/..."` prefix.
target_include_directories(agent_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(agent_imgui PUBLIC cxx_std_17)

# The system prompt is an editable on-disk asset loaded (and hot-reloaded) at
# runtime; point the library at its own source dir so it can find
# system_prompt.md. AGENT_IMGUI_HOST_SOURCE_DIR (the source tree the grep_source
# tool searches) is intentionally left to the consumer to set on this target.
target_compile_definitions(agent_imgui
    PRIVATE
        AGENT_IMGUI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/agent_imgui"
)

target_link_libraries(agent_imgui
    PUBLIC
        dear_imgui
        imgui_test_engine
)

# HTTPS transport for the Claude/Gemini providers: WinHTTP on Windows, libcurl
# everywhere else. (HTTPS needs a TLS-capable system library; there is no viable
# header-only option, so libcurl — universally available — is the minimal pick.)
if(WIN32)
    target_link_libraries(agent_imgui PRIVATE winhttp)
    target_compile_definitions(agent_imgui PRIVATE -D_USE_MATH_DEFINES)
else()
    find_package(CURL)
    if(CURL_FOUND)
        target_link_libraries(agent_imgui PRIVATE CURL::libcurl)
        target_compile_definitions(agent_imgui PRIVATE AGENT_IMGUI_HAVE_CURL)
    else()
        message(WARNING
            "libcurl not found: the agent_imgui LLM providers will return an "
            "error at call time on this platform. Install libcurl development "
            "headers (e.g. libcurl4-openssl-dev) to enable networking.")
    endif()
endif()

if(AGENT_IMGUI_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
