#pragma once

#include <stdint.h>
#include <stddef.h>
#include <lvgl.h>

static constexpr uint8_t  COMPANION_PROTOCOL_VER = 1;
static constexpr uint16_t COMPANION_DEFAULT_PORT = 8765;
static constexpr size_t   COMPANION_MAX_MESSAGES = 8;
static constexpr size_t   COMPANION_HOST_MAX     = 48;
static constexpr size_t   COMPANION_TOKEN_MAX    = 16;
static constexpr size_t   COMPANION_ID_MAX       = 48;
static constexpr size_t   COMPANION_SENDER_MAX   = 48;
static constexpr size_t   COMPANION_TEXT_MAX     = 224;
static constexpr size_t   COMPANION_TIME_MAX     = 12;
static constexpr size_t   COMPANION_DETAIL_MAX   = 64;
static constexpr size_t   COMPANION_CHAT_BUBBLES_MAX  = 5;
static constexpr size_t   COMPANION_BUBBLE_SENDER_MAX = 24;
static constexpr size_t   COMPANION_BUBBLE_TEXT_MAX   = 128;
static constexpr uint16_t COMPANION_AVATAR_SIZE       = 24;
static constexpr size_t   COMPANION_MAX_AVATARS       = 4;

enum class CompanionConnState : uint8_t {
    UNCONFIGURED = 0,
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTH_FAILED
};

enum class CompanionReplyStatus : uint8_t {
    IDLE = 0,
    SENDING,
    SENT,
    FAILED,
    UNSUPPORTED
};

struct CompanionMessage {
    char     id[COMPANION_ID_MAX];
    char     sender[COMPANION_SENDER_MAX];
    char     text[COMPANION_TEXT_MAX];
    char     timeStr[COMPANION_TIME_MAX];
    uint64_t timestampMs;
    bool     canReply;
    bool     unread;
    uint8_t  unreadCount;
    bool     lastOutgoing;
    bool     dismissed;
};

struct CompanionChatBubble {
    char sender[COMPANION_BUBBLE_SENDER_MAX];
    char text[COMPANION_BUBBLE_TEXT_MAX];
    char timeStr[COMPANION_TIME_MAX];
    bool outgoing;
};

struct CompanionReplyState {
    uint32_t             reqId;
    CompanionReplyStatus status;
    char                 notifId[COMPANION_ID_MAX];
    char                 detail[COMPANION_DETAIL_MAX];
};

// Lifecycle (called from main.cpp)
void companionMgrInit();
bool companionMgrTick(); // returns true if UI-visible state changed

// Configuration (persisted in NVS)
void     companionSetConfig(const char* host, uint16_t port, const char* token);
void     companionGetHost(char* out, size_t cap);
uint16_t companionGetPort();
void     companionGetToken(char* out, size_t cap);
bool     companionIsConfigured();

// Connection controls
void               companionConnectNow();
void               companionDisconnect();
CompanionConnState companionGetState();
bool               companionIsConnected();
bool               companionIsForwardingEnabled();
const char*        companionStatusText();
void               companionGetLastError(char* out, size_t cap);

// Message cache (thread-safe copies)
uint32_t companionGetDataGen();
size_t   companionGetMessageCount();
bool     companionGetMessage(size_t index, CompanionMessage* out);
bool     companionFindMessageById(const char* id, CompanionMessage* out);
uint8_t  companionUnreadCount();
void     companionMarkRead(const char* id);
void     companionMarkAllRead();
void     companionClearLocalCache();

// Per-chat conversation bubbles & cached profile avatars (28x28 RGB565)
size_t              companionGetChatBubbleCount(const char* chatId);
bool                companionGetChatBubble(const char* chatId, size_t index, CompanionChatBubble* out);
const lv_img_dsc_t* companionGetAvatarImg(const char* chatId);

// Incoming notification toast hook (consumed once by main.cpp)
bool companionConsumeNewAlert(char* outSender, size_t senderCap, char* outPreview, size_t previewCap);

// Actions to Android companion
bool     companionRequestHistory();
uint32_t companionSendReply(const char* notifId, const char* replyText);
bool     companionDismissNotification(const char* notifId);

// Reply tracking
void companionGetReplyState(CompanionReplyState* out);
void companionResetReplyState();
