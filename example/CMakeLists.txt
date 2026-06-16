# Portable SDL3 + SDL_GPU example host for the agent_imgui library.

include(FetchContent)

# --- SDL3 --------------------------------------------------------------------
# Use a system SDL3 if one is installed; otherwise fetch and build it statically.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.30
    )
    FetchContent_MakeAvailable(SDL3)
endif()

if(TARGET SDL3::SDL3-static)
    set(_sdl3_target SDL3::SDL3-static)
else()
    set(_sdl3_target SDL3::SDL3)
endif()

# --- stb_image_write (header-only PNG encoder, for screenshots) --------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# The ImGui SDL3 + SDL_GPU backends are compiled straight into the example.
add_executable(agent_imgui_example
    main.cc
    ${AGENT_IMGUI_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${AGENT_IMGUI_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(agent_imgui_example
    PRIVATE ${AGENT_IMGUI_IMGUI_DIR}/backends ${stb_SOURCE_DIR})
target_compile_features(agent_imgui_example PRIVATE cxx_std_17)
target_link_libraries(agent_imgui_example
    PRIVATE
        agent_imgui
        dear_imgui
        ${_sdl3_target}
)
