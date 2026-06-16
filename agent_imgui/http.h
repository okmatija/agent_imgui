#ifndef AGENT_IMGUI_HTTP_H_
#define AGENT_IMGUI_HTTP_H_

#include <string>

namespace agent_imgui {

// Minimal HTTPS POST used by the LLM providers.
//
// Sends `body` to https://<host><path>. `headers` is a CRLF-separated block of
// "Name: value" lines with no trailing CRLF (ASCII only). On success returns
// true, sets `status` to the HTTP status code and `response` to the response
// body. On transport failure returns false and sets `err`.
//
// Backend: WinHTTP on Windows; libcurl elsewhere when built with
// AGENT_IMGUI_HAVE_CURL. Without either, it returns false with an explanatory
// error so the rest of the library still builds.
bool HttpsPost(const std::string& host, const std::string& path,
               const std::string& headers, const std::string& body, int& status,
               std::string& response, std::string& err);

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_HTTP_H_
