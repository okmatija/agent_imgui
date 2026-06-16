# Provides the `dear_imgui` and `imgui_test_engine` targets the agent_imgui
# library links against, for standalone builds. A host project that already
# defines these targets (e.g. MuJoCo Studio) supplies its own and this file is
# never included. Versions match MuJoCo Studio's so the library code (extracted
# from it) stays compatible; this ImGui (1.92.x) ships the SDL3 + SDL_GPU
# backends the example uses.

include(FetchContent)

# --- Dear ImGui (core) -------------------------------------------------------
FetchContent_Declare(dear_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        3109131a882daec56a530aff540416983c240443
)
FetchContent_GetProperties(dear_imgui)
if(NOT dear_imgui_POPULATED)
    FetchContent_Populate(dear_imgui)
endif()

add_library(dear_imgui STATIC
    ${dear_imgui_SOURCE_DIR}/imgui.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_draw.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_tables.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(dear_imgui PUBLIC ${dear_imgui_SOURCE_DIR})
# PUBLIC so the macro reaches every consumer of imgui.h (item ids stay stable
# for the Test Engine).
target_compile_definitions(dear_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# Expose the ImGui source dir so the example can compile its SDL3 / SDL_GPU
# backends (which live under backends/).
set(AGENT_IMGUI_IMGUI_DIR ${dear_imgui_SOURCE_DIR} CACHE INTERNAL "Dear ImGui source dir")

# --- Dear ImGui Test Engine --------------------------------------------------
FetchContent_Declare(imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        6a90496de3aece72b0de65c46bb5aa1c9fb7210e
)
FetchContent_GetProperties(imgui_test_engine)
if(NOT imgui_test_engine_POPULATED)
    FetchContent_Populate(imgui_test_engine)
endif()

set(_te_dir ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine)
add_library(imgui_test_engine STATIC
    ${_te_dir}/imgui_te_context.cpp
    ${_te_dir}/imgui_te_coroutine.cpp
    ${_te_dir}/imgui_te_engine.cpp
    ${_te_dir}/imgui_te_exporters.cpp
    ${_te_dir}/imgui_te_perftool.cpp
    ${_te_dir}/imgui_te_ui.cpp
    ${_te_dir}/imgui_te_utils.cpp
    ${_te_dir}/imgui_capture_tool.cpp
)
target_include_directories(imgui_test_engine PUBLIC ${imgui_test_engine_SOURCE_DIR})
# Built-in std::thread coroutine backend (otherwise ImGuiTestEngine_Start
# asserts on a missing coroutine impl).
target_compile_definitions(imgui_test_engine
    PUBLIC IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
target_link_libraries(imgui_test_engine PUBLIC dear_imgui)
