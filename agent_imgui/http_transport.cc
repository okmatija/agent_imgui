#include "agent_imgui/http_transport.h"

#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Built-in HttpTransport backends. Isolated here so <windows.h> / <curl/curl.h>
// never bleed into the rest of the sources. Everything above this file is
// platform-independent and talks only to the HttpTransport interface.
// ---------------------------------------------------------------------------

namespace agent_imgui {
namespace {

// Split "https://host/path?query" into host and "/path?query".
bool ParseHttpsUrl(const std::string& url, std::string& host, std::string& path) {
  const std::string prefix = "https://";
  if (url.rfind(prefix, 0) != 0) return false;
  const std::string rest = url.substr(prefix.size());
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) {
    host = rest;
    path = "/";
  } else {
    host = rest.substr(0, slash);
    path = rest.substr(slash);
  }
  return !host.empty();
}

}  // namespace
}  // namespace agent_imgui

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

class WinHttpTransport : public HttpTransport {
 public:
  HttpResponse Post(const std::string& url,
                    const std::vector<std::string>& headers,
                    const std::string& body) override {
    HttpResponse r;
    std::string host, path;
    if (!ParseHttpsUrl(url, host, path)) {
      r.error = "invalid https url: " + url;
      return r;
    }

    HINTERNET session = WinHttpOpen(
        L"agent_imgui/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { r.error = "WinHttpOpen failed"; return r; }
    // Generous timeouts (ms): resolve, connect, send, receive.
    WinHttpSetTimeouts(session, 60000, 60000, 300000, 300000);
    HINTERNET connect = WinHttpConnect(session, Widen(host).c_str(),
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
      r.error = "WinHttpConnect failed";
      WinHttpCloseHandle(session);
      return r;
    }
    HINTERNET request = WinHttpOpenRequest(
        connect, L"POST", Widen(path).c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
      r.error = "WinHttpOpenRequest failed";
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return r;
    }

    std::string header_block;
    for (const std::string& h : headers) {
      if (!header_block.empty()) header_block += "\r\n";
      header_block += h;
    }
    const std::wstring wheaders = Widen(header_block);

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
      WinHttpQueryHeaders(request,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size,
                          WINHTTP_NO_HEADER_INDEX);
      r.status = static_cast<int>(code);
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
        chunk.resize(read);
        r.body += chunk;
      }
      r.ok = true;
    } else {
      r.error =
          "WinHttp request failed (code " + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return r;
  }
};

}  // namespace

std::shared_ptr<HttpTransport> DefaultHttpTransport() {
  return std::make_shared<WinHttpTransport>();
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

class CurlTransport : public HttpTransport {
 public:
  CurlTransport() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  }

  HttpResponse Post(const std::string& url,
                    const std::vector<std::string>& headers,
                    const std::string& body) override {
    HttpResponse r;
    CURL* curl = curl_easy_init();
    if (!curl) { r.error = "curl_easy_init failed"; return r; }

    curl_slist* header_list = nullptr;
    for (const std::string& h : headers) {
      header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "agent_imgui/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // thread-safe (no SIGALRM)

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
      long code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
      r.status = static_cast<int>(code);
      r.ok = true;
    } else {
      r.body.clear();
      r.error = std::string("curl: ") + curl_easy_strerror(rc);
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return r;
  }
};

}  // namespace

std::shared_ptr<HttpTransport> DefaultHttpTransport() {
  return std::make_shared<CurlTransport>();
}

}  // namespace agent_imgui

#else  // no backend compiled in

namespace agent_imgui {
namespace {

class NullTransport : public HttpTransport {
 public:
  HttpResponse Post(const std::string&, const std::vector<std::string>&,
                    const std::string&) override {
    HttpResponse r;
    r.error =
        "No HTTP transport in this build (need WinHTTP on Windows or libcurl "
        "elsewhere, or inject a custom HttpTransport).";
    return r;
  }
};

}  // namespace

std::shared_ptr<HttpTransport> DefaultHttpTransport() {
  return std::make_shared<NullTransport>();
}

}  // namespace agent_imgui

#endif
