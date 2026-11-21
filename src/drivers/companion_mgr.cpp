#include "companion_mgr.h"
#include "../theme/m3_theme.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#endif

// ============================================================================
// Constants & Synchronization
// ============================================================================

static constexpr const char* NVS_NS              = "companion";
static constexpr size_t      WS_MAX_FRAME_BYTES  = 4096;
static constexpr uint32_t    CONNECT_TIMEOUT_MS  = 4000;
static constexpr uint32_t    HANDSHAKE_TIMEOUT_MS = 4000;
static constexpr uint32_t    HEARTBEAT_MS        = 20000;
static constexpr uint32_t    IDLE_TIMEOUT_MS     = 50000;
static constexpr uint32_t    BACKOFF_MIN_MS      = 2000;
static constexpr uint32_t    BACKOFF_MAX_MS      = 15000;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define COMP_LOCK()   portENTER_CRITICAL(&s_mux)
#define COMP_UNLOCK() portEXIT_CRITICAL(&s_mux)

// ============================================================================
// Shared State (protected by s_mux)
// ============================================================================

static char     s_host[COMPANION_HOST_MAX]   = "";
static uint16_t s_port                       = COMPANION_DEFAULT_PORT;
static char     s_token[COMPANION_TOKEN_MAX] = "";
static bool     s_userWantedConn             = false;

static CompanionConnState s_connState        = CompanionConnState::UNCONFIGURED;
static bool               s_forwardingOn     = true;
static char               s_lastError[COMPANION_DETAIL_MAX] = "";

static CompanionMessage   s_msgs[COMPANION_MAX_MESSAGES];
static size_t             s_msgCount         = 0;
static uint32_t           s_dataGen          = 0;

static CompanionReplyState s_replyState      = {0, CompanionReplyStatus::IDLE, "", ""};
static uint32_t            s_nextReqId       = 1;

static bool s_hasNewAlert                    = false;
static char s_alertSender[COMPANION_SENDER_MAX] = "";
static char s_alertPreview[64]               = "";

// Outbound command queue (small fixed ring to avoid heap churn)
static constexpr size_t TX_QUEUE_CAP = 6;
static constexpr size_t TX_MSG_MAX   = 512;
static char   s_txQueue[TX_QUEUE_CAP][TX_MSG_MAX];
static size_t s_txHead  = 0;
static size_t s_txTail  = 0;
static size_t s_txCount = 0;

static volatile bool s_forceReconnect = false;
static volatile bool s_forceDisconnect = false;
static bool s_workerStarted = false;

// UI-side change tracking
static CompanionConnState s_prevUiState  = CompanionConnState::UNCONFIGURED;
static uint8_t            s_prevUiUnread = 0;
static uint32_t           s_prevUiGen    = 0;

// ============================================================================
// Small String Helpers
// ============================================================================

static void copyStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static bool enqueueTxLocked(const char* json) {
    if (!json || !json[0] || s_txCount >= TX_QUEUE_CAP) return false;
    copyStr(s_txQueue[s_txTail], TX_MSG_MAX, json);
    s_txTail = (s_txTail + 1) % TX_QUEUE_CAP;
    s_txCount++;
    return true;
}

static bool dequeueTxLocked(char* out, size_t cap) {
    if (s_txCount == 0) return false;
    copyStr(out, cap, s_txQueue[s_txHead]);
    s_txHead = (s_txHead + 1) % TX_QUEUE_CAP;
    s_txCount--;
    return true;
}

static void clearTxQueueLocked() {
    s_txHead = 0;
    s_txTail = 0;
    s_txCount = 0;
}

static uint8_t countUnreadLocked() {
    uint8_t n = 0;
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (s_msgs[i].unread && !s_msgs[i].dismissed) ++n;
    }
    return n;
}

// ============================================================================
// Message Store Helpers (called under COMP_LOCK)
// ============================================================================

static void upsertMessageLocked(const CompanionMessage& incoming, bool isLive) {
    if (!incoming.id[0]) return;

    // Update existing entry if same notification ID
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (strcmp(s_msgs[i].id, incoming.id) == 0) {
            bool textChanged = (strcmp(s_msgs[i].text, incoming.text) != 0);
            CompanionMessage updated = incoming;
            if (!isLive) {
                updated.unread = s_msgs[i].unread;
            } else if (!textChanged) {
                updated.unread = s_msgs[i].unread;
            }
            // Move to front (index 0 = newest)
            for (size_t j = i; j > 0; --j) {
                s_msgs[j] = s_msgs[j - 1];
            }
            s_msgs[0] = updated;
            s_dataGen++;
            if (isLive && textChanged) {
                s_hasNewAlert = true;
                copyStr(s_alertSender, sizeof(s_alertSender), updated.sender);
                copyStr(s_alertPreview, sizeof(s_alertPreview), updated.text);
            }
            return;
        }
    }

    // Insert at front, shifting older entries down
    size_t limit = (s_msgCount < COMPANION_MAX_MESSAGES) ? s_msgCount : (COMPANION_MAX_MESSAGES - 1);
    for (size_t j = limit; j > 0; --j) {
        s_msgs[j] = s_msgs[j - 1];
    }
    s_msgs[0] = incoming;
    if (s_msgCount < COMPANION_MAX_MESSAGES) {
        s_msgCount++;
    }
    s_dataGen++;

    if (isLive) {
        s_hasNewAlert = true;
        copyStr(s_alertSender, sizeof(s_alertSender), incoming.sender);
        copyStr(s_alertPreview, sizeof(s_alertPreview), incoming.text);
    }
}

static void removeMessageByIdLocked(const char* id) {
    if (!id || !id[0]) return;
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (strcmp(s_msgs[i].id, id) == 0) {
            for (size_t j = i; j + 1 < s_msgCount; ++j) {
                s_msgs[j] = s_msgs[j + 1];
            }
            s_msgCount--;
            s_dataGen++;
            return;
        }
    }
}

// ============================================================================
// Minimal RFC 6455 WebSocket Client over WiFiClient
// ============================================================================

static bool wsReadExact(WiFiClient& cli, uint8_t* buf, size_t len, uint32_t timeoutMs) {
    size_t got = 0;
    uint32_t start = millis();
    while (got < len) {
        int avail = cli.available();
        if (avail > 0) {
            int n = cli.read(buf + got, len - got);
            if (n > 0) {
                got += (size_t)n;
                start = millis();
                continue;
            }
        }
        if (!cli.connected()) return false;
        if ((uint32_t)(millis() - start) >= timeoutMs) return false;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

static bool wsSendFrame(WiFiClient& cli, uint8_t opcode, const uint8_t* payload, size_t len) {
    if (!cli.connected()) return false;

    uint8_t header[14];
    size_t hLen = 0;
    header[hLen++] = (uint8_t)(0x80 | (opcode & 0x0F)); // FIN + opcode

    if (len < 126) {
        header[hLen++] = (uint8_t)(0x80 | len); // MASK bit set (client->server required)
    } else if (len <= 0xFFFF) {
        header[hLen++] = (uint8_t)(0x80 | 126);
        header[hLen++] = (uint8_t)((len >> 8) & 0xFF);
        header[hLen++] = (uint8_t)(len & 0xFF);
    } else {
        return false; // Reject > 64KB
    }

    uint8_t mask[4];
    uint32_t r = esp_random();
    mask[0] = (uint8_t)(r & 0xFF);
    mask[1] = (uint8_t)((r >> 8) & 0xFF);
    mask[2] = (uint8_t)((r >> 16) & 0xFF);
    mask[3] = (uint8_t)((r >> 24) & 0xFF);
    memcpy(header + hLen, mask, 4);
    hLen += 4;

    if (cli.write(header, hLen) != hLen) return false;

    if (len > 0 && payload) {
        uint8_t chunk[128];
        size_t sent = 0;
        while (sent < len) {
            size_t n = len - sent;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            for (size_t i = 0; i < n; ++i) {
                chunk[i] = payload[sent + i] ^ mask[(sent + i) & 3];
            }
            if (cli.write(chunk, n) != n) return false;
            sent += n;
        }
    }
    return true;
}

static bool wsSendText(WiFiClient& cli, const char* text) {
    if (!text) return false;
    return wsSendFrame(cli, 0x01, (const uint8_t*)text, strlen(text));
}

static bool wsHandshake(WiFiClient& cli, const char* host, uint16_t port) {
    uint8_t nonce[16];
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = esp_random();
        memcpy(nonce + i, &r, 4);
    }
    unsigned char keyB64[32] = {0};
    size_t keyLen = 0;
    mbedtls_base64_encode(keyB64, sizeof(keyB64), &keyLen, nonce, sizeof(nonce));

    char req[320];
    int n = snprintf(req, sizeof(req),
                     "GET /ws HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     host, (unsigned)port, (const char*)keyB64);
    if (n <= 0 || n >= (int)sizeof(req)) return false;

    if (cli.write((const uint8_t*)req, (size_t)n) != (size_t)n) return false;

    // Read HTTP response headers until "\r\n\r\n"
    char resp[512];
    size_t rLen = 0;
    uint32_t start = millis();
    bool headerDone = false;
    while ((uint32_t)(millis() - start) < HANDSHAKE_TIMEOUT_MS && rLen + 1 < sizeof(resp)) {
        if (cli.available() > 0) {
            int c = cli.read();
            if (c >= 0) {
                resp[rLen++] = (char)c;
                resp[rLen] = '\0';
                if (rLen >= 4 &&
                    resp[rLen - 4] == '\r' && resp[rLen - 3] == '\n' &&
                    resp[rLen - 2] == '\r' && resp[rLen - 1] == '\n') {
                    headerDone = true;
                    break;
                }
            }
        } else if (!cli.connected()) {
            return false;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (!headerDone) return false;
    return (strstr(resp, " 101 ") != nullptr);
}

// Non-blocking poll for one frame if data is available.
// Returns: 1 = text frame received in outBuf, 0 = no frame ready / control handled, -1 = error/closed
static int wsPollFrame(WiFiClient& cli, char* outBuf, size_t outCap) {
    if (cli.available() < 2) return 0;

    uint8_t hdr[2];
    if (!wsReadExact(cli, hdr, 2, 1000)) return -1;

    uint8_t opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t payloadLen = hdr[1] & 0x7F;

    if (payloadLen == 126) {
        uint8_t ext[2];
        if (!wsReadExact(cli, ext, 2, 1000)) return -1;
        payloadLen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
        uint8_t ext[8];
        if (!wsReadExact(cli, ext, 8, 1000)) return -1;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | ext[i];
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (!wsReadExact(cli, mask, 4, 1000)) return -1;
    }

    if (payloadLen >= outCap || payloadLen > WS_MAX_FRAME_BYTES) {
        // Drain and discard oversized payload safely
        uint8_t drain[64];
        uint64_t rem = payloadLen;
        while (rem > 0) {
            size_t chunk = (rem > sizeof(drain)) ? sizeof(drain) : (size_t)rem;
            if (!wsReadExact(cli, drain, chunk, 1000)) return -1;
            rem -= chunk;
        }
        return 0;
    }

    if (payloadLen > 0) {
        if (!wsReadExact(cli, (uint8_t*)outBuf, (size_t)payloadLen, 2000)) return -1;
        if (masked) {
            for (size_t i = 0; i < (size_t)payloadLen; ++i) {
                outBuf[i] ^= mask[i & 3];
            }
        }
    }
    outBuf[payloadLen] = '\0';

    if (opcode == 0x08) { // Close
        wsSendFrame(cli, 0x08, nullptr, 0);
        return -1;
    }
    if (opcode == 0x09) { // Ping -> reply Pong
        wsSendFrame(cli, 0x0A, (const uint8_t*)outBuf, (size_t)payloadLen);
        return 0;
    }
    if (opcode == 0x0A) { // Pong
        return 0;
    }
    if (opcode == 0x01) { // Text
        return 1;
    }
    return 0;
}

// ============================================================================
// Protocol Message Parsing
// ============================================================================

static void parseNotificationObj(JsonObjectConst obj, CompanionMessage* out) {
    memset(out, 0, sizeof(*out));
    copyStr(out->id, sizeof(out->id), obj["id"] | "");
    const char* conv = obj["conversation"] | "";
    const char* sender = obj["sender"] | "";
    if (conv[0] && sender[0] && strcmp(conv, sender) != 0) {
        snprintf(out->sender, sizeof(out->sender), "%s: %s", conv, sender);
    } else if (sender[0]) {
        copyStr(out->sender, sizeof(out->sender), sender);
    } else if (conv[0]) {
        copyStr(out->sender, sizeof(out->sender), conv);
    } else {
        copyStr(out->sender, sizeof(out->sender), "WhatsApp");
    }
    copyStr(out->text, sizeof(out->text), obj["text"] | "");
    copyStr(out->timeStr, sizeof(out->timeStr), obj["time"] | "");
    out->timestampMs = obj["timestamp"] | (uint64_t)0;
    out->canReply = obj["can_reply"] | false;
    out->unread = true;
    out->dismissed = false;
}

static void handleIncomingJson(const char* payload, WiFiClient& cli) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    const char* type = doc["type"] | "";
    if (!type[0]) return;

    if (strcmp(type, "hello_ack") == 0) {
        bool ok = doc["ok"] | false;
        const char* errMsg = doc["error"] | "";
        bool fwd = doc["forwarding_enabled"] | true;

        COMP_LOCK();
        if (ok) {
            s_connState = CompanionConnState::CONNECTED;
            s_forwardingOn = fwd;
            s_lastError[0] = '\0';
        } else {
            s_connState = CompanionConnState::AUTH_FAILED;
            copyStr(s_lastError, sizeof(s_lastError), errMsg[0] ? errMsg : "Pairing token rejected");
        }
        COMP_UNLOCK();

        if (ok) {
            // Immediately request recent message history
            char histReq[96];
            snprintf(histReq, sizeof(histReq),
                     "{\"v\":%u,\"type\":\"history_request\",\"limit\":%u}",
                     (unsigned)COMPANION_PROTOCOL_VER, (unsigned)COMPANION_MAX_MESSAGES);
            wsSendText(cli, histReq);
        }
        return;
    }

    if (strcmp(type, "status") == 0) {
        bool fwd = doc["forwarding_enabled"] | true;
        COMP_LOCK();
        s_forwardingOn = fwd;
        s_dataGen++;
        COMP_UNLOCK();
        return;
    }

    if (strcmp(type, "notification") == 0) {
        CompanionMessage msg;
        parseNotificationObj(doc.as<JsonObjectConst>(), &msg);
        if (!msg.id[0]) return;

        COMP_LOCK();
        upsertMessageLocked(msg, true);
        COMP_UNLOCK();
        return;
    }

    if (strcmp(type, "history_response") == 0) {
        JsonArrayConst items = doc["items"].as<JsonArrayConst>();
        if (items.isNull()) return;

        COMP_LOCK();
        // Insert oldest-to-newest so index 0 ends up newest
        size_t n = items.size();
        for (size_t idx = n; idx > 0; --idx) {
            JsonObjectConst obj = items[idx - 1].as<JsonObjectConst>();
            if (obj.isNull()) continue;
            CompanionMessage msg;
            parseNotificationObj(obj, &msg);
            msg.unread = false;
            upsertMessageLocked(msg, false);
        }
        COMP_UNLOCK();
        return;
    }

    if (strcmp(type, "reply_result") == 0) {
        uint32_t reqId = doc["req_id"] | (uint32_t)0;
        const char* statusStr = doc["status"] | "failed";
        const char* detail = doc["detail"] | "";

        CompanionReplyStatus st = CompanionReplyStatus::FAILED;
        if (strcmp(statusStr, "sent") == 0) {
            st = CompanionReplyStatus::SENT;
        } else if (strcmp(statusStr, "unsupported") == 0) {
            st = CompanionReplyStatus::UNSUPPORTED;
        }

        COMP_LOCK();
        if (reqId == 0 || reqId == s_replyState.reqId) {
            s_replyState.status = st;
            copyStr(s_replyState.detail, sizeof(s_replyState.detail), detail);
            s_dataGen++;
        }
        COMP_UNLOCK();
        return;
    }

    if (strcmp(type, "dismiss_result") == 0) {
        bool ok = doc["ok"] | false;
        const char* notifId = doc["id"] | "";
        if (ok && notifId[0]) {
            COMP_LOCK();
            removeMessageByIdLocked(notifId);
            COMP_UNLOCK();
        }
        return;
    }

    if (strcmp(type, "ping") == 0) {
        char pong[64];
        snprintf(pong, sizeof(pong), "{\"v\":%u,\"type\":\"pong\"}", (unsigned)COMPANION_PROTOCOL_VER);
        wsSendText(cli, pong);
    }
}

// ============================================================================
// Background FreeRTOS Worker Task
// ============================================================================

static void companionWorkerTask(void* param) {
    (void)param;
    static char rxBuf[WS_MAX_FRAME_BYTES];
    static char txBuf[TX_MSG_MAX];

    WiFiClient cli;
    uint32_t backoffMs = BACKOFF_MIN_MS;
    uint32_t nextRetryMs = 0;
    uint32_t lastActivityMs = 0;
    uint32_t lastPingMs = 0;

    for (;;) {
        char host[COMPANION_HOST_MAX];
        uint16_t port = COMPANION_DEFAULT_PORT;
        char token[COMPANION_TOKEN_MAX];
        bool wanted = false;

        COMP_LOCK();
        copyStr(host, sizeof(host), s_host);
        port = s_port;
        copyStr(token, sizeof(token), s_token);
        wanted = s_userWantedConn;
        COMP_UNLOCK();

        if (s_forceDisconnect || !wanted || !host[0] || !g_sys.wifiConnected || g_sys.airplaneMode) {
            if (cli.connected()) {
                wsSendFrame(cli, 0x08, nullptr, 0);
                cli.stop();
            }
            s_forceDisconnect = false;
            COMP_LOCK();
            if (!host[0]) {
                s_connState = CompanionConnState::UNCONFIGURED;
            } else if (s_connState != CompanionConnState::AUTH_FAILED) {
                s_connState = CompanionConnState::DISCONNECTED;
            }
            COMP_UNLOCK();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (s_forceReconnect) {
            if (cli.connected()) cli.stop();
            s_forceReconnect = false;
            backoffMs = BACKOFF_MIN_MS;
            nextRetryMs = 0;
        }

        if (!cli.connected()) {
            uint32_t now = millis();
            if (nextRetryMs != 0 && (int32_t)(now - nextRetryMs) < 0) {
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }

            COMP_LOCK();
            s_connState = CompanionConnState::CONNECTING;
            s_lastError[0] = '\0';
            COMP_UNLOCK();

            if (!cli.connect(host, port, CONNECT_TIMEOUT_MS)) {
                COMP_LOCK();
                s_connState = CompanionConnState::DISCONNECTED;
                copyStr(s_lastError, sizeof(s_lastError), "Phone unreachable");
                COMP_UNLOCK();
                nextRetryMs = millis() + backoffMs;
                backoffMs = (backoffMs * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : (backoffMs * 2);
                continue;
            }

            cli.setNoDelay(true);

            if (!wsHandshake(cli, host, port)) {
                cli.stop();
                COMP_LOCK();
                s_connState = CompanionConnState::DISCONNECTED;
                copyStr(s_lastError, sizeof(s_lastError), "WebSocket handshake failed");
                COMP_UNLOCK();
                nextRetryMs = millis() + backoffMs;
                backoffMs = (backoffMs * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : (backoffMs * 2);
                continue;
            }

            // Send versioned hello with pairing token
            JsonDocument helloDoc;
            helloDoc["v"] = COMPANION_PROTOCOL_VER;
            helloDoc["type"] = "hello";
            helloDoc["device"] = "Pixel-CYD";
            helloDoc["token"] = token;
            size_t len = serializeJson(helloDoc, txBuf, sizeof(txBuf));
            if (len == 0 || !wsSendText(cli, txBuf)) {
                cli.stop();
                nextRetryMs = millis() + backoffMs;
                continue;
            }

            backoffMs = BACKOFF_MIN_MS;
            lastActivityMs = millis();
            lastPingMs = lastActivityMs;
        }

        // Connected loop step: poll inbound frames
        int res = wsPollFrame(cli, rxBuf, sizeof(rxBuf));
        if (res < 0) {
            cli.stop();
            COMP_LOCK();
            if (s_connState != CompanionConnState::AUTH_FAILED) {
                s_connState = CompanionConnState::DISCONNECTED;
                copyStr(s_lastError, sizeof(s_lastError), "Connection closed");
            }
            COMP_UNLOCK();
            nextRetryMs = millis() + backoffMs;
            backoffMs = (backoffMs * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : (backoffMs * 2);
            continue;
        }
        if (res == 1) {
            lastActivityMs = millis();
            handleIncomingJson(rxBuf, cli);
            // If auth failed, stop retrying until user updates token
            if (companionGetState() == CompanionConnState::AUTH_FAILED) {
                cli.stop();
                COMP_LOCK();
                s_userWantedConn = false;
                COMP_UNLOCK();
                continue;
            }
        }

        // Flush outbound command queue
        bool hasTx = false;
        COMP_LOCK();
        if (s_connState == CompanionConnState::CONNECTED) {
            hasTx = dequeueTxLocked(txBuf, sizeof(txBuf));
        }
        COMP_UNLOCK();

        if (hasTx) {
            if (!wsSendText(cli, txBuf)) {
                cli.stop();
                continue;
            }
            lastActivityMs = millis();
        }

        // Periodic heartbeat ping
        uint32_t now = millis();
        if ((uint32_t)(now - lastPingMs) >= HEARTBEAT_MS) {
            lastPingMs = now;
            if (!wsSendFrame(cli, 0x09, nullptr, 0)) {
                cli.stop();
                continue;
            }
        }
        if ((uint32_t)(now - lastActivityMs) >= IDLE_TIMEOUT_MS) {
            cli.stop();
            COMP_LOCK();
            s_connState = CompanionConnState::DISCONNECTED;
            copyStr(s_lastError, sizeof(s_lastError), "Heartbeat timeout");
            COMP_UNLOCK();
            nextRetryMs = millis() + backoffMs;
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================================
// Public API
// ============================================================================

void companionMgrInit() {
    Preferences prefs;
    if (prefs.begin(NVS_NS, true)) {
        String h = prefs.getString("host", "");
        uint16_t p = prefs.getUShort("port", COMPANION_DEFAULT_PORT);
        String t = prefs.getString("token", "");
        bool autoConn = prefs.getBool("auto", true);
        prefs.end();

        COMP_LOCK();
        copyStr(s_host, sizeof(s_host), h.c_str());
        s_port = (p > 0) ? p : COMPANION_DEFAULT_PORT;
        copyStr(s_token, sizeof(s_token), t.c_str());
        s_userWantedConn = autoConn && (s_host[0] != '\0');
        s_connState = s_host[0] ? CompanionConnState::DISCONNECTED : CompanionConnState::UNCONFIGURED;
        COMP_UNLOCK();
    }

    if (!s_workerStarted) {
        s_workerStarted = true;
        xTaskCreatePinnedToCore(companionWorkerTask, "companion_ws", 4096, nullptr, 1, nullptr, 0);
    }
}

bool companionMgrTick() {
    CompanionConnState curState;
    uint8_t curUnread;
    uint32_t curGen;

    COMP_LOCK();
    curState = s_connState;
    curUnread = countUnreadLocked();
    curGen = s_dataGen;
    COMP_UNLOCK();

    bool changed = (curState != s_prevUiState) || (curUnread != s_prevUiUnread) || (curGen != s_prevUiGen);
    s_prevUiState = curState;
    s_prevUiUnread = curUnread;
    s_prevUiGen = curGen;
    return changed;
}

void companionSetConfig(const char* host, uint16_t port, const char* token) {
    uint16_t cleanPort = (port > 0) ? port : COMPANION_DEFAULT_PORT;

    COMP_LOCK();
    copyStr(s_host, sizeof(s_host), host ? host : "");
    s_port = cleanPort;
    copyStr(s_token, sizeof(s_token), token ? token : "");
    s_userWantedConn = (s_host[0] != '\0');
    s_connState = s_host[0] ? CompanionConnState::DISCONNECTED : CompanionConnState::UNCONFIGURED;
    s_lastError[0] = '\0';
    clearTxQueueLocked();
    COMP_UNLOCK();

    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
        prefs.putString("host", s_host);
        prefs.putUShort("port", s_port);
        prefs.putString("token", s_token);
        prefs.putBool("auto", s_userWantedConn);
        prefs.end();
    }

    s_forceReconnect = true;
}

void companionGetHost(char* out, size_t cap) {
    COMP_LOCK();
    copyStr(out, cap, s_host);
    COMP_UNLOCK();
}

uint16_t companionGetPort() {
    COMP_LOCK();
    uint16_t p = s_port;
    COMP_UNLOCK();
    return p;
}

void companionGetToken(char* out, size_t cap) {
    COMP_LOCK();
    copyStr(out, cap, s_token);
    COMP_UNLOCK();
}

bool companionIsConfigured() {
    COMP_LOCK();
    bool cfg = (s_host[0] != '\0');
    COMP_UNLOCK();
    return cfg;
}

void companionConnectNow() {
    COMP_LOCK();
    if (s_host[0]) {
        s_userWantedConn = true;
        if (s_connState == CompanionConnState::AUTH_FAILED) {
            s_connState = CompanionConnState::DISCONNECTED;
        }
        s_lastError[0] = '\0';
    }
    COMP_UNLOCK();
    s_forceReconnect = true;
}

void companionDisconnect() {
    COMP_LOCK();
    s_userWantedConn = false;
    clearTxQueueLocked();
    COMP_UNLOCK();
    s_forceDisconnect = true;
}

CompanionConnState companionGetState() {
    COMP_LOCK();
    CompanionConnState st = s_connState;
    COMP_UNLOCK();
    return st;
}

bool companionIsConnected() {
    return companionGetState() == CompanionConnState::CONNECTED;
}

bool companionIsForwardingEnabled() {
    COMP_LOCK();
    bool fwd = s_forwardingOn;
    COMP_UNLOCK();
    return fwd;
}

const char* companionStatusText() {
    switch (companionGetState()) {
        case CompanionConnState::UNCONFIGURED: return "Not linked";
        case CompanionConnState::CONNECTING:   return "Connecting...";
        case CompanionConnState::CONNECTED:    return companionIsForwardingEnabled() ? "Connected" : "Paused";
        case CompanionConnState::AUTH_FAILED:  return "Bad token";
        default:                               return "Offline";
    }
}

void companionGetLastError(char* out, size_t cap) {
    COMP_LOCK();
    copyStr(out, cap, s_lastError);
    COMP_UNLOCK();
}

uint32_t companionGetDataGen() {
    COMP_LOCK();
    uint32_t g = s_dataGen;
    COMP_UNLOCK();
    return g;
}

size_t companionGetMessageCount() {
    COMP_LOCK();
    size_t c = s_msgCount;
    COMP_UNLOCK();
    return c;
}

bool companionGetMessage(size_t index, CompanionMessage* out) {
    if (!out) return false;
    COMP_LOCK();
    if (index >= s_msgCount) {
        COMP_UNLOCK();
        return false;
    }
    *out = s_msgs[index];
    COMP_UNLOCK();
    return true;
}

bool companionFindMessageById(const char* id, CompanionMessage* out) {
    if (!id || !id[0] || !out) return false;
    COMP_LOCK();
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (strcmp(s_msgs[i].id, id) == 0) {
            *out = s_msgs[i];
            COMP_UNLOCK();
            return true;
        }
    }
    COMP_UNLOCK();
    return false;
}

uint8_t companionUnreadCount() {
    COMP_LOCK();
    uint8_t n = countUnreadLocked();
    COMP_UNLOCK();
    return n;
}

void companionMarkRead(const char* id) {
    if (!id || !id[0]) return;
    COMP_LOCK();
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (strcmp(s_msgs[i].id, id) == 0 && s_msgs[i].unread) {
            s_msgs[i].unread = false;
            s_dataGen++;
            break;
        }
    }
    COMP_UNLOCK();
}

void companionMarkAllRead() {
    COMP_LOCK();
    bool changed = false;
    for (size_t i = 0; i < s_msgCount; ++i) {
        if (s_msgs[i].unread) {
            s_msgs[i].unread = false;
            changed = true;
        }
    }
    if (changed) s_dataGen++;
    COMP_UNLOCK();
}

void companionClearLocalCache() {
    COMP_LOCK();
    s_msgCount = 0;
    s_dataGen++;
    COMP_UNLOCK();
}

bool companionConsumeNewAlert(char* outSender, size_t senderCap, char* outPreview, size_t previewCap) {
    COMP_LOCK();
    if (!s_hasNewAlert) {
        COMP_UNLOCK();
        return false;
    }
    s_hasNewAlert = false;
    copyStr(outSender, senderCap, s_alertSender);
    copyStr(outPreview, previewCap, s_alertPreview);
    COMP_UNLOCK();
    return true;
}

bool companionRequestHistory() {
    if (!companionIsConnected()) return false;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"v\":%u,\"type\":\"history_request\",\"limit\":%u}",
             (unsigned)COMPANION_PROTOCOL_VER, (unsigned)COMPANION_MAX_MESSAGES);
    COMP_LOCK();
    bool ok = enqueueTxLocked(buf);
    COMP_UNLOCK();
    return ok;
}

uint32_t companionSendReply(const char* notifId, const char* replyText) {
    if (!notifId || !notifId[0] || !replyText || !replyText[0]) return 0;
    if (!companionIsConnected()) return 0;

    COMP_LOCK();
    if (s_replyState.status == CompanionReplyStatus::SENDING) {
        COMP_UNLOCK();
        return 0; // Prevent duplicate concurrent sends
    }
    uint32_t reqId = s_nextReqId++;
    s_replyState.reqId = reqId;
    s_replyState.status = CompanionReplyStatus::SENDING;
    copyStr(s_replyState.notifId, sizeof(s_replyState.notifId), notifId);
    s_replyState.detail[0] = '\0';
    s_dataGen++;
    COMP_UNLOCK();

    JsonDocument doc;
    doc["v"] = COMPANION_PROTOCOL_VER;
    doc["type"] = "reply_request";
    doc["req_id"] = reqId;
    doc["id"] = notifId;
    doc["text"] = replyText;

    char buf[TX_MSG_MAX];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        COMP_LOCK();
        s_replyState.status = CompanionReplyStatus::FAILED;
        copyStr(s_replyState.detail, sizeof(s_replyState.detail), "Reply too long");
        s_dataGen++;
        COMP_UNLOCK();
        return 0;
    }

    COMP_LOCK();
    bool queued = enqueueTxLocked(buf);
    if (!queued) {
        s_replyState.status = CompanionReplyStatus::FAILED;
        copyStr(s_replyState.detail, sizeof(s_replyState.detail), "Send queue full");
        s_dataGen++;
    }
    COMP_UNLOCK();
    return queued ? reqId : 0;
}

bool companionDismissNotification(const char* notifId) {
    if (!notifId || !notifId[0] || !companionIsConnected()) return false;

    JsonDocument doc;
    doc["v"] = COMPANION_PROTOCOL_VER;
    doc["type"] = "dismiss_request";
    doc["id"] = notifId;

    char buf[192];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return false;

    COMP_LOCK();
    bool queued = enqueueTxLocked(buf);
    COMP_UNLOCK();
    return queued;
}

void companionGetReplyState(CompanionReplyState* out) {
    if (!out) return;
    COMP_LOCK();
    *out = s_replyState;
    COMP_UNLOCK();
}

void companionResetReplyState() {
    COMP_LOCK();
    s_replyState.status = CompanionReplyStatus::IDLE;
    s_replyState.detail[0] = '\0';
    COMP_UNLOCK();
}
