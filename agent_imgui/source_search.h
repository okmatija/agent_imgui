#ifndef AGENT_IMGUI_SOURCE_SEARCH_H_
#define AGENT_IMGUI_SOURCE_SEARCH_H_

#include <string>

namespace agent_imgui {

// Case-insensitive substring grep over the host application's C++ source tree (configured
// at build time via AGENT_IMGUI_HOST_SOURCE_DIR) AND, if `extra_dir` is non-empty,
// the loaded model's directory (its input files: .xml/.urdf/.mjcf/.txt). This
// is the single generic "search disk" capability the agent uses to find/verify
// names before referencing them: widget ids/labels live in the source, model
// entity names (joints, bodies) live in the input files. Returns up to
// `max_results` matches as "relative/path:line: trimmed line".
std::string GrepSource(const std::string& pattern, const std::string& extra_dir,
                       int max_results);

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_SOURCE_SEARCH_H_
