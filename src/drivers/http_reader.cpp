#include "http_reader.h"
#include "../theme/m3_theme.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// ============================================================================
// String / URL Helpers
// ============================================================================

static void copyStr(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static bool startsWithIgnoreCase(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        ++s;
        ++prefix;
    }
    return true;
}

static bool containsIgnoreCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return false;
    size_t nLen = strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nLen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == nLen) return true;
    }
    return false;
}

static bool looksLikeLocalHost(const char* host) {
    if (!host || !host[0]) return false;
    if (startsWithIgnoreCase(host, "localhost")) return true;
    if (strncmp(host, "127.", 4) == 0) return true;
    if (strncmp(host, "10.", 3) == 0) return true;
    if (strncmp(host, "192.168.", 8) == 0) return true;
    if (strncmp(host, "172.", 4) == 0) {
        int second = atoi(host + 4);
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

bool httpReaderNormalizeUrl(const char* raw, char* out, size_t outSize) {
    if (!raw || !out || outSize < 12) return false;

    while (*raw && isspace((unsigned char)*raw)) ++raw;
    size_t len = strlen(raw);
    while (len > 0 && isspace((unsigned char)raw[len - 1])) --len;
    if (len == 0) {
        out[0] = '\0';
        return false;
    }

    char trimmed[HTTP_URL_MAX_LEN];
    if (len >= sizeof(trimmed)) len = sizeof(trimmed) - 1;
    memcpy(trimmed, raw, len);
    trimmed[len] = '\0';

    if (startsWithIgnoreCase(trimmed, "http://")) {
        snprintf(out, outSize, "http://%s", trimmed + 7);
    } else if (startsWithIgnoreCase(trimmed, "https://")) {
        snprintf(out, outSize, "https://%s", trimmed + 8);
    } else {
        const char* scheme = looksLikeLocalHost(trimmed) ? "http://" : "https://";
        snprintf(out, outSize, "%s%s", scheme, trimmed);
    }

    // Verify there is a non-empty host after "://"
    const char* sep = strstr(out, "://");
    if (!sep || !sep[3] || sep[3] == '/' || isspace((unsigned char)sep[3])) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void httpReaderExtractHost(const char* url, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!url || !url[0]) return;

    const char* p = strstr(url, "://");
    p = p ? (p + 3) : url;
    size_t n = 0;
    while (p[n] && p[n] != '/' && p[n] != '?' && p[n] != '#' && p[n] != ':' && n + 1 < outSize) {
        out[n] = p[n];
        ++n;
    }
    out[n] = '\0';
}

bool httpReaderResolveRedirect(const char* baseUrl, const char* location, char* out, size_t outSize) {
    if (!location || !location[0] || !out || outSize < 12) return false;
    while (*location && isspace((unsigned char)*location)) ++location;
    if (!*location) return false;

    if (startsWithIgnoreCase(location, "http://") || startsWithIgnoreCase(location, "https://")) {
        return httpReaderNormalizeUrl(location, out, outSize);
    }

    if (!baseUrl || !baseUrl[0]) return false;
    const char* sep = strstr(baseUrl, "://");
    if (!sep) return false;
    size_t schemeLen = (size_t)(sep - baseUrl) + 3; // includes "://"

    if (location[0] == '/' && location[1] == '/') {
        // Protocol-relative URL: "//host/path"
        size_t pLen = (size_t)(sep - baseUrl);
        snprintf(out, outSize, "%.*s:%s", (int)pLen, baseUrl, location);
        return true;
    }

    const char* pathStart = strchr(baseUrl + schemeLen, '/');
    size_t originLen = pathStart ? (size_t)(pathStart - baseUrl) : strlen(baseUrl);

    if (location[0] == '/') {
        snprintf(out, outSize, "%.*s%s", (int)originLen, baseUrl, location);
        return true;
    }

    // Relative to current directory
    const char* lastSlash = pathStart ? strrchr(pathStart, '/') : nullptr;
    if (lastSlash) {
        size_t dirLen = (size_t)(lastSlash - baseUrl) + 1;
        snprintf(out, outSize, "%.*s%s", (int)dirLen, baseUrl, location);
    } else {
        snprintf(out, outSize, "%.*s/%s", (int)originLen, baseUrl, location);
    }
    return true;
}

// ============================================================================
// Streaming Lightweight HTML-to-Text Extractor
// ============================================================================

enum ExtractorMode : uint8_t {
    EX_TEXT = 0,
    EX_TAG,
    EX_COMMENT,
    EX_SKIP_RAW,
    EX_ENTITY
};

static void emitBodyChar(HtmlTextExtractor* ex, char c) {
    if (!ex->bodyBuf || ex->bodyCap <= 1) return;
    if (ex->bodyLen + 1 >= ex->bodyCap) {
        ex->truncated = true;
        return;
    }

    if (c == '\n') {
        // Trim trailing space before newline
        if (ex->bodyLen > 0 && ex->bodyBuf[ex->bodyLen - 1] == ' ') {
            ex->bodyLen--;
        }
        // Avoid leading newlines or >2 consecutive newlines
        if (ex->bodyLen == 0 || ex->consecutiveNl >= 2) {
            ex->lastWasSpace = true;
            return;
        }
        ex->bodyBuf[ex->bodyLen++] = '\n';
        ex->consecutiveNl++;
        ex->lastWasSpace = true;
        return;
    }

    if (c == ' ' || c == '\t' || c == '\r') {
        if (ex->bodyLen == 0 || ex->lastWasSpace || ex->consecutiveNl > 0) {
            return;
        }
        ex->bodyBuf[ex->bodyLen++] = ' ';
        ex->lastWasSpace = true;
        return;
    }

    if ((unsigned char)c < 0x20) return;

    ex->bodyBuf[ex->bodyLen++] = c;
    ex->lastWasSpace = false;
    ex->consecutiveNl = 0;
}

static void emitBodyStr(HtmlTextExtractor* ex, const char* s) {
    if (!s) return;
    while (*s) {
        emitBodyChar(ex, *s++);
    }
}

static void emitTitleChar(HtmlTextExtractor* ex, char c) {
    if (!ex->titleBuf || ex->titleCap <= 1) return;
    if (ex->titleLen + 1 >= ex->titleCap) return;

    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if ((unsigned char)c < 0x20) return;
    if (c == ' ') {
        if (ex->titleLen == 0 || ex->titleBuf[ex->titleLen - 1] == ' ') return;
    }
    ex->titleBuf[ex->titleLen++] = c;
}

static void emitTargetChar(HtmlTextExtractor* ex, char c) {
    if (ex->inTitle) {
        emitTitleChar(ex, c);
    } else if (!ex->inHead) {
        emitBodyChar(ex, c);
    }
}

static void emitTargetStr(HtmlTextExtractor* ex, const char* s) {
    if (!s) return;
    while (*s) {
        emitTargetChar(ex, *s++);
    }
}

static bool isSkipRawTag(const char* tag) {
    return strcmp(tag, "script") == 0 ||
           strcmp(tag, "style") == 0 ||
           strcmp(tag, "noscript") == 0 ||
           strcmp(tag, "svg") == 0 ||
           strcmp(tag, "template") == 0;
}

static bool isBlockTag(const char* tag) {
    return strcmp(tag, "p") == 0 ||
           strcmp(tag, "div") == 0 ||
           strcmp(tag, "h1") == 0 ||
           strcmp(tag, "h2") == 0 ||
           strcmp(tag, "h3") == 0 ||
           strcmp(tag, "h4") == 0 ||
           strcmp(tag, "h5") == 0 ||
           strcmp(tag, "h6") == 0 ||
           strcmp(tag, "tr") == 0 ||
           strcmp(tag, "section") == 0 ||
           strcmp(tag, "article") == 0 ||
           strcmp(tag, "header") == 0 ||
           strcmp(tag, "footer") == 0 ||
           strcmp(tag, "blockquote") == 0 ||
           strcmp(tag, "pre") == 0 ||
           strcmp(tag, "ul") == 0 ||
           strcmp(tag, "ol") == 0 ||
           strcmp(tag, "dt") == 0 ||
           strcmp(tag, "dd") == 0;
}

static void handleCompletedTag(HtmlTextExtractor* ex) {
    ex->tagName[ex->tagNameLen] = '\0';
    const char* t = ex->tagName;
    if (!t[0]) return;

    if (strcmp(t, "head") == 0) {
        ex->inHead = !ex->tagClosing;
        return;
    }
    if (strcmp(t, "body") == 0) {
        if (!ex->tagClosing) ex->inHead = false;
        return;
    }
    if (strcmp(t, "title") == 0) {
        if (!ex->tagClosing) {
            if (ex->titleLen == 0) ex->inTitle = true;
        } else {
            ex->inTitle = false;
        }
        return;
    }

    if (!ex->tagClosing && isSkipRawTag(t)) {
        copyStr(ex->skipTag, sizeof(ex->skipTag), t);
        ex->skipCloseLen = 0;
        ex->mode = EX_SKIP_RAW;
        return;
    }

    if (ex->inHead || ex->inTitle) return;

    if (strcmp(t, "br") == 0 || strcmp(t, "hr") == 0) {
        emitBodyChar(ex, '\n');
    } else if (strcmp(t, "li") == 0) {
        if (!ex->tagClosing) {
            emitBodyChar(ex, '\n');
            emitBodyStr(ex, "- ");
        } else {
            emitBodyChar(ex, '\n');
        }
    } else if (isBlockTag(t)) {
        emitBodyChar(ex, '\n');
    }
}

static void decodeEntity(HtmlTextExtractor* ex) {
    ex->entityBuf[ex->entityLen] = '\0';
    const char* e = ex->entityBuf;

    if (strcmp(e, "amp") == 0)        { emitTargetChar(ex, '&'); return; }
    if (strcmp(e, "lt") == 0)         { emitTargetChar(ex, '<'); return; }
    if (strcmp(e, "gt") == 0)         { emitTargetChar(ex, '>'); return; }
    if (strcmp(e, "quot") == 0)       { emitTargetChar(ex, '"'); return; }
    if (strcmp(e, "apos") == 0)       { emitTargetChar(ex, '\''); return; }
    if (strcmp(e, "nbsp") == 0)       { emitTargetChar(ex, ' '); return; }
    if (strcmp(e, "mdash") == 0)      { emitTargetStr(ex, "--"); return; }
    if (strcmp(e, "ndash") == 0)      { emitTargetChar(ex, '-'); return; }
    if (strcmp(e, "hellip") == 0)     { emitTargetStr(ex, "..."); return; }
    if (strcmp(e, "copy") == 0)       { emitTargetStr(ex, "(c)"); return; }
    if (strcmp(e, "bull") == 0)       { emitTargetChar(ex, '-'); return; }
    if (strcmp(e, "lsquo") == 0 || strcmp(e, "rsquo") == 0) { emitTargetChar(ex, '\''); return; }
    if (strcmp(e, "ldquo") == 0 || strcmp(e, "rdquo") == 0) { emitTargetChar(ex, '"'); return; }

    if (e[0] == '#' && e[1]) {
        unsigned long code = 0;
        if (e[1] == 'x' || e[1] == 'X') {
            code = strtoul(e + 2, nullptr, 16);
        } else {
            code = strtoul(e + 1, nullptr, 10);
        }
        if (code >= 32 && code <= 126) {
            emitTargetChar(ex, (char)code);
            return;
        }
        if (code == 160) { emitTargetChar(ex, ' '); return; }
        if (code == 8211 || code == 8212) { emitTargetChar(ex, '-'); return; }
        if (code == 8216 || code == 8217) { emitTargetChar(ex, '\''); return; }
        if (code == 8220 || code == 8221) { emitTargetChar(ex, '"'); return; }
        if (code == 8230) { emitTargetStr(ex, "..."); return; }
        return;
    }

    // Unknown entity -> emit literal
    emitTargetChar(ex, '&');
    emitTargetStr(ex, e);
    emitTargetChar(ex, ';');
}

void htmlExtractorInit(HtmlTextExtractor* ex, bool isHtml,
                       char* titleBuf, size_t titleCap,
                       char* bodyBuf, size_t bodyCap) {
    if (!ex) return;
    memset(ex, 0, sizeof(*ex));
    ex->isHtml = isHtml;
    ex->titleBuf = titleBuf;
    ex->titleCap = titleCap;
    ex->bodyBuf = bodyBuf;
    ex->bodyCap = bodyCap;
    ex->mode = EX_TEXT;
    ex->lastWasSpace = true;
    if (titleBuf && titleCap > 0) titleBuf[0] = '\0';
    if (bodyBuf && bodyCap > 0) bodyBuf[0] = '\0';
}

void htmlExtractorFeed(HtmlTextExtractor* ex, const char* data, size_t len) {
    if (!ex || !data || len == 0) return;

    if (!ex->isHtml) {
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];
            if (c == '\r') continue;
            if (c == '\t') c = ' ';
            if (c == '\n') {
                emitBodyChar(ex, '\n');
            } else if ((unsigned char)c >= 0x20) {
                if (ex->bodyBuf && ex->bodyLen + 1 < ex->bodyCap) {
                    ex->bodyBuf[ex->bodyLen++] = c;
                    ex->lastWasSpace = (c == ' ');
                    ex->consecutiveNl = 0;
                } else {
                    ex->truncated = true;
                }
            }
        }
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = data[i];

        switch (ex->mode) {
            case EX_TEXT:
                if (c == '<') {
                    ex->mode = EX_TAG;
                    ex->tagClosing = false;
                    ex->tagNameDone = false;
                    ex->tagNameLen = 0;
                } else if (c == '&') {
                    ex->prevMode = EX_TEXT;
                    ex->mode = EX_ENTITY;
                    ex->entityLen = 0;
                } else {
                    emitTargetChar(ex, c);
                }
                break;

            case EX_TAG:
                if (c == '>') {
                    ex->mode = EX_TEXT;
                    handleCompletedTag(ex);
                } else if (ex->tagNameLen == 0 && !ex->tagNameDone && c == '/') {
                    ex->tagClosing = true;
                } else if (!ex->tagNameDone) {
                    if (isspace((unsigned char)c) || c == '/') {
                        ex->tagNameDone = true;
                    } else if (ex->tagNameLen + 1 < sizeof(ex->tagName)) {
                        ex->tagName[ex->tagNameLen++] = (char)tolower((unsigned char)c);
                        if (ex->tagNameLen == 3 &&
                            ex->tagName[0] == '!' &&
                            ex->tagName[1] == '-' &&
                            ex->tagName[2] == '-') {
                            ex->mode = EX_COMMENT;
                            ex->commentMatch = 0;
                        }
                    }
                }
                break;

            case EX_COMMENT:
                if (c == '-') {
                    if (ex->commentMatch < 2) ex->commentMatch++;
                } else if (c == '>' && ex->commentMatch == 2) {
                    ex->mode = EX_TEXT;
                    ex->commentMatch = 0;
                } else {
                    ex->commentMatch = 0;
                }
                break;

            case EX_SKIP_RAW:
                if (c == '<') {
                    ex->skipCloseBuf[0] = '<';
                    ex->skipCloseLen = 1;
                } else if (ex->skipCloseLen > 0) {
                    if (c == '>') {
                        ex->skipCloseBuf[ex->skipCloseLen] = '\0';
                        // Check if skipCloseBuf starts with "</<skipTag>"
                        size_t sLen = strlen(ex->skipTag);
                        if (ex->skipCloseLen >= 2 + sLen &&
                            ex->skipCloseBuf[0] == '<' &&
                            ex->skipCloseBuf[1] == '/' &&
                            strncmp(ex->skipCloseBuf + 2, ex->skipTag, sLen) == 0) {
                            char after = ex->skipCloseBuf[2 + sLen];
                            if (after == '\0' || isspace((unsigned char)after)) {
                                ex->mode = EX_TEXT;
                            }
                        }
                        ex->skipCloseLen = 0;
                    } else if (ex->skipCloseLen + 1 < sizeof(ex->skipCloseBuf)) {
                        ex->skipCloseBuf[ex->skipCloseLen++] = (char)tolower((unsigned char)c);
                    } else {
                        ex->skipCloseLen = 0;
                    }
                }
                break;

            case EX_ENTITY:
                if (c == ';') {
                    ex->mode = ex->prevMode;
                    decodeEntity(ex);
                } else if (isspace((unsigned char)c) || c == '<' ||
                           ex->entityLen + 1 >= sizeof(ex->entityBuf)) {
                    // Not a valid entity -> flush literal '&' + buffered chars + current char
                    ex->entityBuf[ex->entityLen] = '\0';
                    ex->mode = ex->prevMode;
                    emitTargetChar(ex, '&');
                    emitTargetStr(ex, ex->entityBuf);
                    if (c == '<') {
                        ex->mode = EX_TAG;
                        ex->tagClosing = false;
                        ex->tagNameDone = false;
                        ex->tagNameLen = 0;
                    } else {
                        emitTargetChar(ex, c);
                    }
                } else {
                    ex->entityBuf[ex->entityLen++] = c;
                }
                break;
        }
    }
}

void htmlExtractorFinish(HtmlTextExtractor* ex) {
    if (!ex) return;
    if (ex->mode == EX_ENTITY && ex->entityLen > 0) {
        ex->entityBuf[ex->entityLen] = '\0';
        emitTargetChar(ex, '&');
        emitTargetStr(ex, ex->entityBuf);
    }
    if (ex->titleBuf && ex->titleCap > 0) {
        while (ex->titleLen > 0 && isspace((unsigned char)ex->titleBuf[ex->titleLen - 1])) {
            ex->titleLen--;
        }
        ex->titleBuf[ex->titleLen] = '\0';
    }
    if (ex->bodyBuf && ex->bodyCap > 0) {
        while (ex->bodyLen > 0 && isspace((unsigned char)ex->bodyBuf[ex->bodyLen - 1])) {
            ex->bodyLen--;
        }
        ex->bodyBuf[ex->bodyLen] = '\0';
    }
}

void httpReaderHtmlToText(const char* input, size_t len, bool isHtml,
                          char* outTitle, size_t titleCap,
                          char* outBody, size_t bodyCap,
                          bool* outTruncated) {
    HtmlTextExtractor ex;
    htmlExtractorInit(&ex, isHtml, outTitle, titleCap, outBody, bodyCap);
    htmlExtractorFeed(&ex, input, len);
    htmlExtractorFinish(&ex);
    if (outTruncated) *outTruncated = ex.truncated;
}

// ============================================================================
// HTTP / HTTPS Fetcher State & Worker
// ============================================================================

#ifdef ESP32
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define READER_LOCK()   portENTER_CRITICAL(&s_mux)
#define READER_UNLOCK() portEXIT_CRITICAL(&s_mux)
#else
#define READER_LOCK()   do {} while (0)
#define READER_UNLOCK() do {} while (0)
#endif

static HttpReaderResult s_result = {};
static char s_pendingUrl[HTTP_URL_MAX_LEN] = "";
static uint32_t s_pendingGen = 0;
static bool s_hasPending = false;
static volatile bool s_cancelRequested = false;
static bool s_workerRunning = false;

static void formatHttpError(int code, char* out, size_t outSize) {
    if (code <= 0) {
        switch (code) {
            case HTTPC_ERROR_CONNECTION_REFUSED: copyStr(out, outSize, "Connection refused"); break;
            case HTTPC_ERROR_SEND_HEADER_FAILED: copyStr(out, outSize, "Send header failed"); break;
            case HTTPC_ERROR_SEND_PAYLOAD_FAILED: copyStr(out, outSize, "Send payload failed"); break;
            case HTTPC_ERROR_NOT_CONNECTED:      copyStr(out, outSize, "Not connected"); break;
            case HTTPC_ERROR_CONNECTION_LOST:    copyStr(out, outSize, "Connection lost"); break;
            case HTTPC_ERROR_NO_STREAM:          copyStr(out, outSize, "No response stream"); break;
            case HTTPC_ERROR_NO_HTTP_SERVER:     copyStr(out, outSize, "Host unreachable"); break;
            case HTTPC_ERROR_TOO_LESS_RAM:       copyStr(out, outSize, "Low memory for request"); break;
            case HTTPC_ERROR_READ_TIMEOUT:       copyStr(out, outSize, "Request timed out"); break;
            default:
                snprintf(out, outSize, "Network error (%d)", code);
                break;
        }
        return;
    }
    switch (code) {
        case 400: copyStr(out, outSize, "HTTP 400 Bad Request"); break;
        case 401: copyStr(out, outSize, "HTTP 401 Unauthorized"); break;
        case 403: copyStr(out, outSize, "HTTP 403 Forbidden"); break;
        case 404: copyStr(out, outSize, "HTTP 404 Not Found"); break;
        case 429: copyStr(out, outSize, "HTTP 429 Too Many Requests"); break;
        case 500: copyStr(out, outSize, "HTTP 500 Server Error"); break;
        case 502: copyStr(out, outSize, "HTTP 502 Bad Gateway"); break;
        case 503: copyStr(out, outSize, "HTTP 503 Unavailable"); break;
        default:
            snprintf(out, outSize, "HTTP %d", code);
            break;
    }
}

static bool performSingleFetch(const char* initialUrl, uint32_t gen, HttpReaderResult* out) {
    memset(out, 0, sizeof(*out));
    out->gen = gen;
    copyStr(out->url, sizeof(out->url), initialUrl);

    uint32_t startMs = millis();
    char currentUrl[HTTP_URL_MAX_LEN];
    copyStr(currentUrl, sizeof(currentUrl), initialUrl);

    for (uint8_t redir = 0; redir <= HTTP_MAX_REDIRECTS; ++redir) {
        if (s_cancelRequested) {
            out->state = HttpReaderState::IDLE;
            return false;
        }

        bool isHttps = startsWithIgnoreCase(currentUrl, "https://");
        HTTPClient http;
        http.setConnectTimeout(HTTP_CONNECT_TIMEOUT);
        http.setTimeout(HTTP_READ_TIMEOUT);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setUserAgent("PixelCYD/1.0 (ESP32)");

        const char* hdrKeys[] = { "Content-Type", "Location" };
        http.collectHeaders(hdrKeys, 2);

        WiFiClient plainClient;
        WiFiClientSecure secureClient;
        bool began = false;
        if (isHttps) {
            secureClient.setInsecure();
            began = http.begin(secureClient, currentUrl);
        } else {
            began = http.begin(plainClient, currentUrl);
        }

        if (!began) {
            out->state = HttpReaderState::ERROR;
            out->httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
            out->elapsedMs = millis() - startMs;
            copyStr(out->error, sizeof(out->error), "Invalid URL or client init failed");
            return true;
        }

        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("Accept", "text/html,text/plain;q=0.9,*/*;q=0.1");

        int code = http.GET();
        out->httpCode = code;

        if (s_cancelRequested) {
            http.end();
            out->state = HttpReaderState::IDLE;
            return false;
        }

        // Handle 3xx redirects manually so http:// -> https:// upgrades work cleanly.
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.header("Location");
            http.end();
            if (loc.length() == 0) {
                out->state = HttpReaderState::ERROR;
                out->elapsedMs = millis() - startMs;
                snprintf(out->error, sizeof(out->error), "HTTP %d (no Location)", code);
                return true;
            }
            if (redir == HTTP_MAX_REDIRECTS) {
                out->state = HttpReaderState::ERROR;
                out->elapsedMs = millis() - startMs;
                copyStr(out->error, sizeof(out->error), "Too many redirects");
                return true;
            }
            char nextUrl[HTTP_URL_MAX_LEN];
            if (!httpReaderResolveRedirect(currentUrl, loc.c_str(), nextUrl, sizeof(nextUrl))) {
                out->state = HttpReaderState::ERROR;
                out->elapsedMs = millis() - startMs;
                copyStr(out->error, sizeof(out->error), "Invalid redirect URL");
                return true;
            }
            copyStr(currentUrl, sizeof(currentUrl), nextUrl);
            copyStr(out->url, sizeof(out->url), currentUrl);
            continue;
        }

        if (code < 0) {
            http.end();
            out->state = HttpReaderState::ERROR;
            out->elapsedMs = millis() - startMs;
            formatHttpError(code, out->error, sizeof(out->error));
            return true;
        }

        String ctype = http.header("Content-Type");
        bool isHtml = true;
        if (ctype.length() > 0) {
            if (containsIgnoreCase(ctype.c_str(), "text/plain") ||
                containsIgnoreCase(ctype.c_str(), "application/json")) {
                isHtml = false;
            }
        }

        HtmlTextExtractor ex;
        htmlExtractorInit(&ex, isHtml, out->title, sizeof(out->title), out->body, sizeof(out->body));

        WiFiClient* stream = http.getStreamPtr();
        int remaining = http.getSize(); // -1 for chunked / unknown length
        uint8_t chunk[512];
        size_t totalRaw = 0;
        uint32_t lastDataMs = millis();

        while (stream &&
               totalRaw < HTTP_RAW_READ_LIMIT &&
               !ex.truncated &&
               !s_cancelRequested &&
               (millis() - startMs) < HTTP_TOTAL_BUDGET_MS) {
            if (remaining == 0) break;

            size_t avail = (size_t)stream->available();
            if (avail > 0) {
                size_t toRead = avail;
                if (toRead > sizeof(chunk)) toRead = sizeof(chunk);
                if (remaining > 0 && toRead > (size_t)remaining) toRead = (size_t)remaining;
                if (totalRaw + toRead > HTTP_RAW_READ_LIMIT) {
                    toRead = HTTP_RAW_READ_LIMIT - totalRaw;
                }
                int got = stream->readBytes(chunk, toRead);
                if (got > 0) {
                    totalRaw += (size_t)got;
                    if (remaining > 0) remaining -= got;
                    htmlExtractorFeed(&ex, (const char*)chunk, (size_t)got);
                    lastDataMs = millis();
                }
            } else {
                if (!http.connected()) break;
                if ((millis() - lastDataMs) >= HTTP_READ_TIMEOUT) break;
                delay(5);
            }
        }

        http.end();

        if (s_cancelRequested) {
            out->state = HttpReaderState::IDLE;
            return false;
        }

        htmlExtractorFinish(&ex);
        out->rawBytes = totalRaw;
        out->truncated = ex.truncated || (totalRaw >= HTTP_RAW_READ_LIMIT && remaining != 0);
        out->elapsedMs = millis() - startMs;

        if (!out->title[0]) {
            httpReaderExtractHost(out->url, out->title, sizeof(out->title));
        }

        if (code >= 200 && code < 300) {
            out->state = HttpReaderState::DONE;
            if (!out->body[0]) {
                copyStr(out->body, sizeof(out->body), "(Empty text response)");
            }
        } else {
            out->state = HttpReaderState::ERROR;
            formatHttpError(code, out->error, sizeof(out->error));
        }
        return true;
    }

    return true;
}

static void runWorkerLoop() {
    static HttpReaderResult workBuf;

    for (;;) {
        char urlToFetch[HTTP_URL_MAX_LEN];
        uint32_t gen = 0;

        READER_LOCK();
        if (!s_hasPending) {
            s_workerRunning = false;
            READER_UNLOCK();
            return;
        }
        copyStr(urlToFetch, sizeof(urlToFetch), s_pendingUrl);
        gen = s_pendingGen;
        s_hasPending = false;
        s_cancelRequested = false;
        READER_UNLOCK();

        bool committed = performSingleFetch(urlToFetch, gen, &workBuf);

        READER_LOCK();
        if (committed && !s_hasPending && gen == s_result.gen) {
            memcpy(&s_result, &workBuf, sizeof(s_result));
        }
        READER_UNLOCK();
    }
}

#ifdef ESP32
static void httpWorkerTask(void* param) {
    (void)param;
    runWorkerLoop();
    vTaskDelete(nullptr);
}
#endif

uint32_t httpReaderFetch(const char* rawUrl) {
    char normalized[HTTP_URL_MAX_LEN];
    bool valid = httpReaderNormalizeUrl(rawUrl, normalized, sizeof(normalized));

    READER_LOCK();
    uint32_t gen = ++s_result.gen;
    if (!valid) {
        s_hasPending = false;
        s_cancelRequested = true;
        s_result.state = HttpReaderState::ERROR;
        s_result.httpCode = 0;
        s_result.elapsedMs = 0;
        s_result.rawBytes = 0;
        s_result.truncated = false;
        copyStr(s_result.url, sizeof(s_result.url), rawUrl ? rawUrl : "");
        copyStr(s_result.title, sizeof(s_result.title), "Invalid URL");
        s_result.body[0] = '\0';
        copyStr(s_result.error, sizeof(s_result.error), "Enter a valid http:// or https:// URL");
        READER_UNLOCK();
        return gen;
    }

    if (!g_sys.wifiConnected) {
        s_hasPending = false;
        s_cancelRequested = true;
        s_result.state = HttpReaderState::ERROR;
        s_result.httpCode = 0;
        s_result.elapsedMs = 0;
        s_result.rawBytes = 0;
        s_result.truncated = false;
        copyStr(s_result.url, sizeof(s_result.url), normalized);
        httpReaderExtractHost(normalized, s_result.title, sizeof(s_result.title));
        s_result.body[0] = '\0';
        copyStr(s_result.error, sizeof(s_result.error), "Wi-Fi not connected");
        READER_UNLOCK();
        return gen;
    }

    s_result.state = HttpReaderState::LOADING;
    s_result.httpCode = 0;
    s_result.elapsedMs = 0;
    s_result.rawBytes = 0;
    s_result.truncated = false;
    copyStr(s_result.url, sizeof(s_result.url), normalized);
    httpReaderExtractHost(normalized, s_result.title, sizeof(s_result.title));
    s_result.body[0] = '\0';
    s_result.error[0] = '\0';

    copyStr(s_pendingUrl, sizeof(s_pendingUrl), normalized);
    s_pendingGen = gen;
    s_hasPending = true;
    s_cancelRequested = true; // interrupt any in-flight fetch so new URL runs next

    bool needSpawn = !s_workerRunning;
    if (needSpawn) {
        s_workerRunning = true;
    }
    READER_UNLOCK();

    if (needSpawn) {
#ifdef ESP32
        BaseType_t ok = xTaskCreatePinnedToCore(
            httpWorkerTask, "http_reader", 8192, nullptr, 1, nullptr, 0);
        if (ok != pdPASS) {
            READER_LOCK();
            s_workerRunning = false;
            s_hasPending = false;
            s_result.state = HttpReaderState::ERROR;
            copyStr(s_result.error, sizeof(s_result.error), "Failed to start HTTP task");
            READER_UNLOCK();
        }
#else
        runWorkerLoop();
#endif
    }
    return gen;
}

void httpReaderCancel() {
    READER_LOCK();
    s_hasPending = false;
    s_cancelRequested = true;
    if (s_result.state == HttpReaderState::LOADING) {
        s_result.gen++;
        s_result.state = HttpReaderState::IDLE;
    }
    READER_UNLOCK();
}

bool httpReaderIsLoading() {
    READER_LOCK();
    bool loading = (s_result.state == HttpReaderState::LOADING);
    READER_UNLOCK();
    return loading;
}

uint32_t httpReaderGetGen() {
    READER_LOCK();
    uint32_t gen = s_result.gen;
    READER_UNLOCK();
    return gen;
}

HttpReaderState httpReaderGetState() {
    READER_LOCK();
    HttpReaderState st = s_result.state;
    READER_UNLOCK();
    return st;
}

bool httpReaderGetResult(HttpReaderResult* out) {
    if (!out) return false;
    READER_LOCK();
    memcpy(out, &s_result, sizeof(*out));
    READER_UNLOCK();
    return true;
}
