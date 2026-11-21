#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr size_t   HTTP_URL_MAX_LEN     = 192;
static constexpr size_t   HTTP_TITLE_MAX_LEN   = 64;
static constexpr size_t   HTTP_BODY_MAX_LEN    = 4096;
static constexpr size_t   HTTP_RAW_READ_LIMIT  = 32768;
static constexpr uint32_t HTTP_CONNECT_TIMEOUT = 8000;
static constexpr uint32_t HTTP_READ_TIMEOUT    = 8000;
static constexpr uint32_t HTTP_TOTAL_BUDGET_MS = 12000;
static constexpr uint8_t  HTTP_MAX_REDIRECTS   = 4;

enum class HttpReaderState : uint8_t {
    IDLE = 0,
    LOADING,
    DONE,
    ERROR
};

struct HttpReaderResult {
    HttpReaderState state;
    uint32_t gen;
    int httpCode;
    uint32_t elapsedMs;
    size_t rawBytes;
    bool truncated;
    char url[HTTP_URL_MAX_LEN];
    char title[HTTP_TITLE_MAX_LEN];
    char body[HTTP_BODY_MAX_LEN];
    char error[96];
};

// Streaming lightweight HTML/text extractor (zero heap allocation).
struct HtmlTextExtractor {
    bool isHtml;
    char* titleBuf;
    size_t titleCap;
    size_t titleLen;
    char* bodyBuf;
    size_t bodyCap;
    size_t bodyLen;
    bool truncated;

    // Internal parser state
    uint8_t mode;          // 0=TEXT, 1=TAG, 2=COMMENT, 3=SKIP_RAW, 4=ENTITY
    uint8_t prevMode;      // mode to return to after ENTITY
    bool inHead;
    bool inTitle;
    bool tagClosing;
    bool tagNameDone;
    bool lastWasSpace;
    uint8_t consecutiveNl;
    uint8_t commentMatch;  // tracks "-->"
    char tagName[16];
    size_t tagNameLen;
    char skipTag[12];
    char skipCloseBuf[16];
    size_t skipCloseLen;
    char entityBuf[12];
    size_t entityLen;
};

void htmlExtractorInit(HtmlTextExtractor* ex, bool isHtml,
                       char* titleBuf, size_t titleCap,
                       char* bodyBuf, size_t bodyCap);
void htmlExtractorFeed(HtmlTextExtractor* ex, const char* data, size_t len);
void htmlExtractorFinish(HtmlTextExtractor* ex);

// One-shot helper around HtmlTextExtractor.
void httpReaderHtmlToText(const char* input, size_t len, bool isHtml,
                          char* outTitle, size_t titleCap,
                          char* outBody, size_t bodyCap,
                          bool* outTruncated = nullptr);

// URL helpers.
bool httpReaderNormalizeUrl(const char* raw, char* out, size_t outSize);
void httpReaderExtractHost(const char* url, char* out, size_t outSize);
bool httpReaderResolveRedirect(const char* baseUrl, const char* location, char* out, size_t outSize);

// Async HTTP/HTTPS GET reader API.
uint32_t httpReaderFetch(const char* rawUrl);
void httpReaderCancel();
bool httpReaderIsLoading();
uint32_t httpReaderGetGen();
HttpReaderState httpReaderGetState();
bool httpReaderGetResult(HttpReaderResult* out);
