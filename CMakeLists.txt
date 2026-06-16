# agent_imgui — an LLM "agent in your Dear ImGui app" library.
# Originally extracted from MuJoCo Studio (src/experimental/studio/llm). See
# README.md for how to consume it.

cmake_minimum_required(VERSION 3.16)

project(agent_imgui CXX)

# agent_imgui plugs into the *host* application's Dear ImGui build: the panel
# renders with ImGui and the UI-driving tool uses the ImGui Test Engine. The
# host project must therefore define the `dear_imgui` and `imgui_test_engine`
# targets before pulling this in. We deliberately do not fetch our own ImGui so
# that a single copy is linked into the final application.
if(NOT TARGET dear_imgui OR NOT TARGET imgui_test_engine)
    message(FATAL_ERROR
        "agent_imgui requires the parent project to define the 'dear_imgui' and "
        "'imgui_test_engine' targets before add_subdirectory(agent_imgui). "
        "See README.md.")
endif()

add_library(agent_imgui STATIC
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

if(WIN32)
    # WinHTTP transport for the Claude/Gemini providers (Windows-only today).
    target_link_libraries(agent_imgui PRIVATE winhttp)
    target_compile_definitions(agent_imgui PRIVATE -D_USE_MATH_DEFINES)
endif()
