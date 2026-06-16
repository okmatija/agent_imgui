#include "agent_imgui/http.h"

#include <string>

// ---------------------------------------------------------------------------
// Platform transport. Isolated here so <windows.h> / <curl/curl.h> never bleed
// into the rest of the sources. The provider tool-use loops are platform-
// independent and call HttpsPost().
// ---------------------------------------------------------------------------

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

namespace agent_imgui {
namespace {

std::wstring Widen(const std::string& s) {
  std::wstring w;
  w.reserve(s.size());
  for (unsigned char c : s) w += static_cast<wchar_t>(c);  // ASCII headers only.
  return w;
}

}  // namespace

bool HttpsPost(const std::string& host, const std::string& path,
               const std::string& headers, const std::string& body, int& status,
               std::string& response, std::string& err) {
  status = 0;
  response.clear();
  err.clear();

  HINTERNET session = WinHttpOpen(L"agent_imgui/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  0);
  if (!session) { err = "WinHttpOpen failed"; return false; }
  // Generous timeouts (ms): resolve, connect, send, receive. A model can take a
  // while on complex prompts (thinking + a long tool-use program).
  WinHttpSetTimeouts(session, 60000, 60000, 300000, 300000);
  HINTERNET connect = WinHttpConnect(session, Widen(host).c_str(),
                                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect) {
    err = "WinHttpConnect failed";
    WinHttpCloseHandle(session);
    return false;
  }
  HINTERNET request = WinHttpOpenRequest(
      connect, L"POST", Widen(path).c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    err = "WinHttpOpenRequest failed";
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  bool ok = false;
  std::wstring wheaders = Widen(headers);
  if (WinHttpAddRequestHeaders(request, wheaders.c_str(),
                               static_cast<DWORD>(-1),
                               WINHTTP_ADDREQ_FLAG_ADD) &&
      WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         const_cast<char*>(body.data()),
                         static_cast<DWORD>(body.size()),
                         static_cast<DWORD>(body.size()), 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD code = 0;
    DWORD code_size = sizeof(code);
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size, WINHTTP_NO_HEADER_INDEX);
    status = static_cast<int>(code);
    for (;;) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
      chunk.resize(read);
      response += chunk;
    }
    ok = true;
  } else {
    err = "WinHttp request failed (code " + std::to_string(GetLastError()) + ")";
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return ok;
}

}  // namespace agent_imgui

#elif defined(AGENT_IMGUI_HAVE_CURL)

#include <curl/curl.h>

#include <mutex>

namespace agent_imgui {
namespace {

size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  static_cast<std::string*>(userdata)->append(ptr, n);
  return n;
}

void EnsureGlobalInit() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}  // namespace

bool HttpsPost(const std::string& host, const std::string& path,
               const std::string& headers, const std::string& body, int& status,
               std::string& response, std::string& err) {
  status = 0;
  response.clear();
  err.clear();

  EnsureGlobalInit();
  CURL* curl = curl_easy_init();
  if (!curl) { err = "curl_easy_init failed"; return false; }

  const std::string url = "https://" + host + path;

  // Split the CRLF-separated header block into a curl_slist.
  curl_slist* header_list = nullptr;
  for (size_t start = 0; start <= headers.size();) {
    const size_t nl = headers.find("\r\n", start);
    const std::string line =
        headers.substr(start, nl == std::string::npos ? std::string::npos
                                                      : nl - start);
    if (!line.empty()) header_list = curl_slist_append(header_list, line.c_str());
    if (nl == std::string::npos) break;
    start = nl + 2;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "agent_imgui/1.0");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // thread-safe (no SIGALRM)

  const CURLcode rc = curl_easy_perform(curl);
  bool ok = (rc == CURLE_OK);
  if (ok) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    status = static_cast<int>(code);
  } else {
    err = std::string("curl: ") + curl_easy_strerror(rc);
  }

  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return ok;
}

}  // namespace agent_imgui

#else  // no transport backend

namespace agent_imgui {

bool HttpsPost(const std::string& /*host*/, const std::string& /*path*/,
               const std::string& /*headers*/, const std::string& /*body*/,
               int& status, std::string& response, std::string& err) {
  status = 0;
  response.clear();
  err =
      "No HTTPS transport in this build (need WinHTTP on Windows or libcurl "
      "elsewhere).";
  return false;
}

}  // namespace agent_imgui

#endif
