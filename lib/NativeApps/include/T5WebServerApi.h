#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_WEB_SERVER_API_VERSION 1u
#define T5_WEB_SERVER_SSID_MAX 33u
#define T5_WEB_SERVER_PASSWORD_MAX 65u
#define T5_WEB_SERVER_HOSTNAME_MAX 64u
#define T5_WEB_SERVER_ROOT_MAX 96u
#define T5_WEB_SERVER_IP_MAX 16u

typedef enum {
    T5_WEB_SERVER_STATUS_UNSUPPORTED = 0,
    T5_WEB_SERVER_STATUS_OFF = 1,
    T5_WEB_SERVER_STATUS_RUNNING = 2,
    T5_WEB_SERVER_STATUS_ERROR = 3,
} t5_web_server_status_t;

typedef enum {
    T5_WEB_SERVER_ERROR_NONE = 0,
    T5_WEB_SERVER_ERROR_INVALID_CONFIG = -1,
    T5_WEB_SERVER_ERROR_STORAGE = -2,
    T5_WEB_SERVER_ERROR_WIFI = -3,
    T5_WEB_SERVER_ERROR_DNS = -4,
    T5_WEB_SERVER_ERROR_HTTP = -5,
} t5_web_server_error_t;

typedef struct {
    char ssid[T5_WEB_SERVER_SSID_MAX];
    char password[T5_WEB_SERVER_PASSWORD_MAX];
    // mDNS label only; firmware appends .local for the canonical portal URL.
    char hostname[T5_WEB_SERVER_HOSTNAME_MAX];
    // SD-rooted firmware path, e.g. /html. Native apps never access the files directly.
    char document_root[T5_WEB_SERVER_ROOT_MAX];
    uint8_t channel;
    uint8_t max_connections;
    uint8_t reserved[2];
} t5_web_server_config_t;

typedef struct {
    uint8_t status;
    uint8_t channel;
    uint8_t clients;
    uint8_t dns_active;
    uint8_t mdns_active;
    uint8_t reserved[3];
    int32_t last_error;
    uint32_t requests;
    uint32_t files_served;
    uint32_t bytes_served;
    char ip[T5_WEB_SERVER_IP_MAX];
    char ssid[T5_WEB_SERVER_SSID_MAX];
    char hostname[T5_WEB_SERVER_HOSTNAME_MAX];
    char document_root[T5_WEB_SERVER_ROOT_MAX];
} t5_web_server_state_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    bool (*supported)(void);
    void (*default_config)(t5_web_server_config_t *config);

    // Firmware owns Wi-Fi AP mode, wildcard DNS, mDNS, HTTP, SD file access,
    // captive-portal redirects and all cleanup. The app only supplies policy.
    bool (*start)(const t5_web_server_config_t *config);
    void (*stop)(void);

    // Service DNS and HTTP work from the owning native-app task. Call frequently
    // (roughly every 20-50 ms); no network or storage handles cross the ABI.
    bool (*service)(void);
    bool (*read_state)(t5_web_server_state_t *state);
} t5_web_server_api_v1;

const t5_web_server_api_v1 *t5_web_server_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
