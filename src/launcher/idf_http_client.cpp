#include "idf_http_client.h"

#include "launcher_platform.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t kTimeoutMs = 15000;
constexpr uint32_t kStreamIdleTimeoutMs = 15000;
constexpr size_t kStreamChunkSize = 1024;
constexpr int kMaxRedirects = 5;

bool isHttpsUrl(const String &url) {
    return url.startsWith("https://") || url.startsWith("HTTPS://");
}

String resolveRedirectUrl(const String &currentUrl, const String &location) {
    if (location.startsWith("http://") || location.startsWith("https://")) {
        return location;
    }
    int schemeEnd = currentUrl.indexOf("://");
    if (schemeEnd < 0) return location;
    int hostStart = schemeEnd + 3;
    int pathStart = currentUrl.indexOf('/', hostStart);
    String origin = (pathStart >= 0) ? currentUrl.substring(0, pathStart) : currentUrl;
    if (location.startsWith("/")) {
        return origin + location;
    }
    int lastSlash = currentUrl.lastIndexOf('/');
    if (lastSlash > hostStart) {
        return currentUrl.substring(0, lastSlash + 1) + location;
    }
    return origin + "/" + location;
}

bool streamBody(HTTPClient &http, int64_t contentLength, LauncherHttpChunkCb cb, void *ctx) {
    WiFiClient *stream = http.getStreamPtr();
    if (!stream) return contentLength == 0;

    uint8_t buf[kStreamChunkSize];
    int64_t remaining = contentLength;
    uint32_t lastDataMs = millis();

    while (remaining != 0) {
        int avail = stream->available();
        if (avail > 0) {
            size_t toRead = static_cast<size_t>(avail);
            if (toRead > sizeof(buf)) toRead = sizeof(buf);
            if (remaining > 0 && static_cast<int64_t>(toRead) > remaining) {
                toRead = static_cast<size_t>(remaining);
            }
            int n = stream->readBytes(buf, toRead);
            if (n > 0) {
                lastDataMs = millis();
                if (cb && !cb(buf, static_cast<size_t>(n), ctx)) {
                    return false;
                }
                if (remaining > 0) remaining -= n;
                continue;
            }
        }

        if (!stream->connected()) {
            // Drain any remaining buffered bytes after peer close
            if (stream->available() > 0) continue;
            break;
        }

        if (static_cast<uint32_t>(millis() - lastDataMs) > kStreamIdleTimeoutMs) {
            launcherConsolePrintf("[HTTP] Stream idle timeout\n");
            return false;
        }
        delay(2);
    }

    return (remaining <= 0);
}

bool executeRequest(
    const char *url,
    bool isPost,
    const char *postBody,
    size_t postBodyLen,
    LauncherHttpChunkCb cb,
    void *ctx,
    LauncherHttpResponse *response,
    const char *headerKey = nullptr,
    const char *headerValue = nullptr,
    const char *header2Key = nullptr,
    const char *header2Value = nullptr
) {
    LauncherHttpResponse localResponse;
    LauncherHttpResponse *resp = response ? response : &localResponse;
    *resp = LauncherHttpResponse();

    if (!url || !url[0]) {
        resp->transport_error = -1;
        return false;
    }

    String currentUrl(url);
    static const char *kCollectHeaders[] = {"Content-Range", "Content-Length", "Location"};

    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        HTTPClient http;
        http.setReuse(false);
        http.setConnectTimeout(kTimeoutMs);
        http.setTimeout(kTimeoutMs);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.collectHeaders(kCollectHeaders, 3);

        WiFiClient plainClient;
        WiFiClientSecure secureClient;
        bool began = false;
        if (isHttpsUrl(currentUrl)) {
            // Skip CA bundle parsing to preserve internal DRAM for TLS handshake (~15 KB saved)
            // and avoid root/intermediate CA rotation failures.
            secureClient.setInsecure();
            secureClient.setTimeout(kTimeoutMs / 1000);
            began = http.begin(secureClient, currentUrl);
        } else {
            plainClient.setTimeout(kTimeoutMs / 1000);
            began = http.begin(plainClient, currentUrl);
        }

        if (!began) {
            resp->transport_error = -2;
            launcherConsolePrintf("[HTTP] begin() failed for %s (freeHeap=%lu)\n",
                                  currentUrl.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()));
            return false;
        }

        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("User-Agent", "Pixel-CYD-Store/1.1");
        if (isPost) {
            http.addHeader("Content-Type", "application/json");
        }
        if (headerKey && headerValue) http.addHeader(headerKey, headerValue);
        if (header2Key && header2Value) http.addHeader(header2Key, header2Value);

        int code = isPost
                       ? http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(postBody)), postBodyLen)
                       : http.GET();

        if (code <= 0) {
            resp->status = 0;
            resp->transport_error = code;
            launcherConsolePrintf("[HTTP] %s failed: err=%d (%s) freeHeap=%lu\n",
                                  currentUrl.c_str(), code, HTTPClient::errorToString(code).c_str(),
                                  static_cast<unsigned long>(ESP.getFreeHeap()));
            http.end();
            return false;
        }

        // Handle HTTP redirects manually to preserve Range/HWID headers across hops & schemes.
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.header("Location");
            http.end();
            if (loc.isEmpty() || hop >= kMaxRedirects) {
                resp->status = code;
                return false;
            }
            if (code == 303) {
                isPost = false;
                postBody = nullptr;
                postBodyLen = 0;
            }
            currentUrl = resolveRedirectUrl(currentUrl, loc);
            continue;
        }

        resp->status = code;
        resp->transport_error = 0;

        int len = http.getSize();
        if (len >= 0) {
            resp->content_length = len;
        } else {
            String clHdr = http.header("Content-Length");
            if (!clHdr.isEmpty()) resp->content_length = atoll(clHdr.c_str());
        }

        String crHdr = http.header("Content-Range");
        if (!crHdr.isEmpty()) {
            strncpy(resp->content_range, crHdr.c_str(), sizeof(resp->content_range) - 1);
            resp->content_range[sizeof(resp->content_range) - 1] = '\0';
        }

        if (code < 200 || code >= 300) {
            http.end();
            return false;
        }

        bool ok = streamBody(http, resp->content_length, cb, ctx);
        http.end();
        return ok;
    }

    return false;
}

struct StringSink {
    String *out;
    size_t maxSize;
};

bool stringChunkCb(const uint8_t *data, size_t len, void *ctx) {
    StringSink *sink = static_cast<StringSink *>(ctx);
    if (!sink || !sink->out) return false;
    if (sink->out->length() + len > sink->maxSize) return false;
    sink->out->concat(reinterpret_cast<const char *>(data), len);
    return true;
}
} // namespace

bool launcherHttpGetToString(const char *url, String &out, size_t maxSize, LauncherHttpResponse *response) {
    out = "";
    StringSink sink = {&out, maxSize};
    LauncherHttpResponse localResponse;
    LauncherHttpResponse *resp = response ? response : &localResponse;
    return launcherHttpGetStream(url, stringChunkCb, &sink, resp) && resp->status == 200;
}

bool launcherHttpGetStream(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, const char *headerKey,
    const char *headerValue
) {
    return executeRequest(url, false, nullptr, 0, cb, ctx, response, headerKey, headerValue);
}

bool launcherHttpGetRange(
    const char *url, uint32_t offset, uint32_t size, LauncherHttpChunkCb cb, void *ctx,
    LauncherHttpResponse *response, const char *hwid
) {
    if (size == 0) return false;
    String range = "bytes=" + String(offset) + "-" + String(offset + size - 1);
    const char *effectiveHwid = (hwid && hwid[0]) ? hwid : "aa:11:bb:22:cc:44";
    return executeRequest(url, false, nullptr, 0, cb, ctx, response, "Range", range.c_str(), "HWID", effectiveHwid);
}

bool launcherHttpPost(
    const char *url, const char *body, size_t bodyLen, String &out, size_t maxSize,
    LauncherHttpResponse *response
) {
    out = "";
    StringSink sink = {&out, maxSize};
    LauncherHttpResponse localResponse;
    LauncherHttpResponse *resp = response ? response : &localResponse;
    return executeRequest(url, true, body, bodyLen, stringChunkCb, &sink, resp);
}
