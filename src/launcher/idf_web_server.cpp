#include "idf_web_server.h"

// Vendored from bmorcelli/Launcher (src/idf/idf_web_server.cpp).
httpd_handle_t launcherWebServerStart(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    // Extra sockets keep browser keep-alive + concurrent upload alive within LWIP's budget.
    config.max_open_sockets = 5;
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;
    // Generous timeouts so slow SD/flash writes never trigger premature resets.
    config.recv_wait_timeout = 45;
    config.send_wait_timeout = 45;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.backlog_conn = 5;
    // Larger stack so ArduinoJson + SD + esp_flash helpers run safely inside handlers.
    config.stack_size = 8192;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) return nullptr;
    return server;
}

void launcherWebServerStop(httpd_handle_t server) {
    if (server) httpd_stop(server);
}
