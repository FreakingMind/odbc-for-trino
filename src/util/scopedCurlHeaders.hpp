#pragma once

#include <initializer_list>
#include <string>

#include <curl/curl.h>

/*
Owns a header list installed as CURLOPT_HTTPHEADER for the lifetime of a
scope. curl does not copy the list, so it must stay alive for every
request made with the handle; on destruction the option is cleared
*before* the list is freed, so later requests on the same handle can't
read freed memory.
*/
class ScopedCurlHeaders {
  public:
    ScopedCurlHeaders(CURL* curl, std::initializer_list<std::string> lines)
        : curl_(curl) {
      for (const auto& line : lines) {
        list_ = curl_slist_append(list_, line.c_str());
      }
      curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, list_);
    }

    ~ScopedCurlHeaders() {
      curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, nullptr);
      curl_slist_free_all(list_);
    }

    ScopedCurlHeaders(const ScopedCurlHeaders&)            = delete;
    ScopedCurlHeaders& operator=(const ScopedCurlHeaders&) = delete;

  private:
    CURL* curl_;
    curl_slist* list_ = nullptr;
};
