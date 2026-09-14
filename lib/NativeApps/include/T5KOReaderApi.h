#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_KOREADER_API_VERSION 1u
#define T5_KOREADER_USERNAME_MAX 65u
#define T5_KOREADER_PASSWORD_MAX 65u
#define T5_KOREADER_SERVER_URL_MAX 129u
#define T5_KOREADER_MESSAGE_MAX 128u

#define T5_KOREADER_MATCH_FILENAME 0u
#define T5_KOREADER_MATCH_BINARY 1u

typedef enum {
    T5_KOREADER_AUTH_OK = 0,
    T5_KOREADER_AUTH_NO_CREDENTIALS = 1,
    T5_KOREADER_AUTH_NETWORK_ERROR = 2,
    T5_KOREADER_AUTH_FAILED = 3,
    T5_KOREADER_AUTH_SERVER_ERROR = 4,
    T5_KOREADER_AUTH_JSON_ERROR = 5,
    T5_KOREADER_AUTH_NOT_FOUND = 6,
    T5_KOREADER_AUTH_UNKNOWN = 255,
} t5_koreader_auth_status_t;

typedef struct {
    char username[T5_KOREADER_USERNAME_MAX];
    char password[T5_KOREADER_PASSWORD_MAX];
    char server_url[T5_KOREADER_SERVER_URL_MAX];
    uint8_t match_method;
    uint8_t has_credentials;
    uint8_t reserved[2];
} t5_koreader_settings_t;

typedef struct {
    uint8_t status;
    uint8_t reserved[3];
    int32_t http_status;
    char message[T5_KOREADER_MESSAGE_MAX];
} t5_koreader_auth_result_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Read/write the firmware-owned KOReader credential store. Password text is
    // exposed only to the trusted first-party settings ELF so the firmware's
    // password keyboard can preserve the same prefill behavior as the legacy UI.
    bool (*read_settings)(t5_koreader_settings_t *settings);
    bool (*set_username)(const char *username);
    bool (*set_password)(const char *password);
    bool (*set_server_url)(const char *server_url);
    bool (*set_match_method)(uint8_t match_method);

    // Runs the existing firmware KOReaderSyncClient authentication request.
    // Wi-Fi selection remains a firmware System UI responsibility.
    bool (*authenticate)(t5_koreader_auth_result_t *result);

    // Mirrors the legacy authentication activity, which powers Wi-Fi down when
    // its authentication screen is dismissed.
    void (*end_auth_session)(void);
} t5_koreader_api_v1;

const t5_koreader_api_v1 *t5_koreader_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
