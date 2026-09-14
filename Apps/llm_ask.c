#include "T5AppApi.h"
#include "T5NetworkApi.h"
#include "T5StorageApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_TURNS 6
#define TURN_TEXT_CAP 2048
#define QUESTION_CAP 281
#define STATUS_CAP 128
#define TRANSCRIPT_CAP 16384
#define REQUEST_CAP 16384
#define RESPONSE_CAP 8192
#define SESSION_PATH "/sd/.crosspoint/llm_ask.session"
#define SESSION_MAGIC 0x4B53414Du
#define SESSION_VERSION 1u
#define COOKIE_KEYBOARD 0x41534B01u
#define COOKIE_WIFI 0x41534B02u

typedef struct {
    uint8_t from_user;
    char text[TURN_TEXT_CAP];
} turn_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t turn_count;
    uint8_t reserved;
    int32_t scroll_from_bottom;
    turn_t turns[MAX_TURNS];
} session_t;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} writer_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;
static const t5_network_api_v1 *network;
static session_t session;
static char status_text[STATUS_CAP];
static char transcript[TRANSCRIPT_CAP];
static char request_json[REQUEST_CAP];
static char response_json[RESPONSE_CAP];
static int32_t max_scroll_lines;

static const char kChatUrl[] = "https://api.llm7.io/v1/chat/completions";
static const char kSystemPrompt[] =
    "You are Manifold on a small e-ink reader. Reply in short plain text. No markdown.";

// LLM7 currently sits behind Cloudflare using Google Trust Services. These
// service-specific roots deliberately live in the app rather than firmware.
static const char kLlm7TlsRoots[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n"
    "A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
    "b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n"
    "MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
    "YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n"
    "aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n"
    "jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n"
    "xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n"
    "1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n"
    "snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n"
    "U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n"
    "9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n"
    "BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n"
    "AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n"
    "yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n"
    "38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n"
    "AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n"
    "DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n"
    "HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIICnzCCAiWgAwIBAgIQf/MZd5csIkp2FV0TttaF4zAKBggqhkjOPQQDAzBHMQsw\n"
    "CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n"
    "MBIGA1UEAxMLR1RTIFJvb3QgUjQwHhcNMjMxMjEzMDkwMDAwWhcNMjkwMjIwMTQw\n"
    "MDAwWjA7MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVR29vZ2xlIFRydXN0IFNlcnZp\n"
    "Y2VzMQwwCgYDVQQDEwNXRTEwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAARvzTr+\n"
    "Z1dHTCEDhUDCR127WEcPQMFcF4XGGTfn1XzthkubgdnXGhOlCgP4mMTG6J7/EFmP\n"
    "LCaY9eYmJbsPAvpWo4H+MIH7MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggr\n"
    "BgEFBQcDAQYIKwYBBQUHAwIwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHQ4EFgQU\n"
    "kHeSNWfE/6jMqeZ72YB5e8yT+TgwHwYDVR0jBBgwFoAUgEzW63T/STaj1dj8tT7F\n"
    "avCUHYwwNAYIKwYBBQUHAQEEKDAmMCQGCCsGAQUFBzAChhhodHRwOi8vaS5wa2ku\n"
    "Z29vZy9yNC5jcnQwKwYDVR0fBCQwIjAgoB6gHIYaaHR0cDovL2MucGtpLmdvb2cv\n"
    "ci9yNC5jcmwwEwYDVR0gBAwwCjAIBgZngQwBAgEwCgYIKoZIzj0EAwMDaAAwZQIx\n"
    "AOcCq1HW90OVznX+0RGU1cxAQXomvtgM8zItPZCuFQ8jSBJSjz5keROv9aYsAm5V\n"
    "sQIwJonMaAFi54mrfhfoFNZEfuNMSQ6/bIBiNLiyoX46FohQvKeIoJ99cx7sUkFN\n"
    "7uJW\n"
    "-----END CERTIFICATE-----\n";

static size_t slen(const char *s) {
    size_t n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

static void scopy(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || capacity == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void set_status(const char *value) { scopy(status_text, sizeof(status_text), value); }

static void writer_init(writer_t *w, char *data, size_t capacity) {
    if (!w) return;
    w->data = data;
    w->capacity = capacity;
    w->length = 0;
    w->ok = data != NULL && capacity > 0;
    if (w->ok) data[0] = 0;
}

static void writer_char(writer_t *w, char c) {
    if (!w || !w->ok || w->length + 1 >= w->capacity) {
        if (w) w->ok = false;
        return;
    }
    w->data[w->length++] = c;
    w->data[w->length] = 0;
}

static void writer_text(writer_t *w, const char *s) {
    size_t i = 0;
    if (!s) return;
    while (s[i]) writer_char(w, s[i++]);
}

static void writer_uint(writer_t *w, uint32_t value) {
    char reverse[12];
    size_t count = 0;
    if (value == 0) {
        writer_char(w, '0');
        return;
    }
    while (value && count < sizeof(reverse)) {
        uint32_t quotient = value / 10u;
        reverse[count++] = (char)('0' + (value - quotient * 10u));
        value = quotient;
    }
    while (count) writer_char(w, reverse[--count]);
}

static void writer_hex4(writer_t *w, uint16_t value) {
    static const char hex[] = "0123456789ABCDEF";
    writer_char(w, hex[(value >> 12) & 0xF]);
    writer_char(w, hex[(value >> 8) & 0xF]);
    writer_char(w, hex[(value >> 4) & 0xF]);
    writer_char(w, hex[value & 0xF]);
}

static void writer_json_string(writer_t *w, const char *s) {
    size_t i = 0;
    writer_char(w, '"');
    if (!s) s = "";
    while (s[i]) {
        const uint8_t c = (uint8_t)s[i++];
        if (c == '"' || c == '\\') {
            writer_char(w, '\\');
            writer_char(w, (char)c);
        } else if (c == '\n') {
            writer_text(w, "\\n");
        } else if (c == '\r') {
            writer_text(w, "\\r");
        } else if (c == '\t') {
            writer_text(w, "\\t");
        } else if (c < 0x20) {
            writer_text(w, "\\u");
            writer_hex4(w, c);
        } else {
            writer_char(w, (char)c);
        }
    }
    writer_char(w, '"');
}

static void reset_session(void) {
    int i, j;
    session.magic = SESSION_MAGIC;
    session.version = SESSION_VERSION;
    session.turn_count = 0;
    session.reserved = 0;
    session.scroll_from_bottom = 0;
    for (i = 0; i < MAX_TURNS; ++i) {
        session.turns[i].from_user = 0;
        for (j = 0; j < TURN_TEXT_CAP; ++j) session.turns[i].text[j] = 0;
    }
}

static bool load_session(void) {
    size_t size = 0;
    int i;
    if (!storage->read_file(SESSION_PATH, &session, sizeof(session), &size) || size != sizeof(session) ||
        session.magic != SESSION_MAGIC || session.version != SESSION_VERSION || session.turn_count > MAX_TURNS) {
        reset_session();
        return false;
    }
    for (i = 0; i < MAX_TURNS; ++i) session.turns[i].text[TURN_TEXT_CAP - 1] = 0;
    if (session.scroll_from_bottom < 0) session.scroll_from_bottom = 0;
    return true;
}

static bool save_session(void) {
    return storage->write_file_atomic(SESSION_PATH, &session, sizeof(session));
}

static void clear_session_file(void) { storage->remove_file(SESSION_PATH); }

static void copy_turn(turn_t *dst, const turn_t *src) {
    int i;
    dst->from_user = src->from_user;
    for (i = 0; i < TURN_TEXT_CAP; ++i) dst->text[i] = src->text[i];
}

static void add_turn(bool from_user, const char *text) {
    int i;
    if (session.turn_count >= MAX_TURNS) {
        for (i = 1; i < MAX_TURNS; ++i) copy_turn(&session.turns[i - 1], &session.turns[i]);
        session.turn_count = MAX_TURNS - 1;
    }
    session.turns[session.turn_count].from_user = from_user ? 1u : 0u;
    scopy(session.turns[session.turn_count].text, TURN_TEXT_CAP, text);
    ++session.turn_count;
    session.scroll_from_bottom = 0;
}

static void build_transcript(void) {
    writer_t w;
    uint8_t i;
    writer_init(&w, transcript, sizeof(transcript));
    if (session.turn_count == 0) {
        writer_text(&w, "Press Ask to start a conversation.");
        return;
    }
    for (i = 0; i < session.turn_count; ++i) {
        if (i) writer_text(&w, "\n\n");
        writer_text(&w, session.turns[i].from_user ? "You: " : "AI: ");
        writer_text(&w, session.turns[i].text);
    }
}

static bool build_request(void) {
    writer_t w;
    int start;
    int i;
    writer_init(&w, request_json, sizeof(request_json));
    writer_text(&w, "{\"model\":\"fast\",\"max_tokens\":256,\"temperature\":0.3,\"messages\":[");
    writer_text(&w, "{\"role\":\"system\",\"content\":");
    writer_json_string(&w, kSystemPrompt);
    writer_char(&w, '}');

    start = session.turn_count > 4 ? session.turn_count - 4 : 0;
    for (i = start; i < session.turn_count; ++i) {
        writer_text(&w, ",{\"role\":");
        writer_json_string(&w, session.turns[i].from_user ? "user" : "assistant");
        writer_text(&w, ",\"content\":");
        writer_json_string(&w, session.turns[i].text);
        writer_char(&w, '}');
    }
    writer_text(&w, "]}");
    return w.ok;
}

static const char *find_text(const char *haystack, const char *needle) {
    size_t i, j;
    if (!haystack || !needle || !needle[0]) return NULL;
    for (i = 0; haystack[i]; ++i) {
        for (j = 0; needle[j] && haystack[i + j] == needle[j]; ++j) {}
        if (!needle[j]) return haystack + i;
    }
    return NULL;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex4(const char *s, uint16_t *value) {
    int i;
    uint16_t v = 0;
    if (!s || !value) return false;
    for (i = 0; i < 4; ++i) {
        int h = hex_value(s[i]);
        if (h < 0) return false;
        v = (uint16_t)((v << 4) | (uint16_t)h);
    }
    *value = v;
    return true;
}

static bool append_utf8(char *out, size_t capacity, size_t *length, uint32_t cp) {
    if (!out || !length) return false;
    if (cp <= 0x7F) {
        if (*length + 1 >= capacity) return false;
        out[(*length)++] = (char)cp;
    } else if (cp <= 0x7FF) {
        if (*length + 2 >= capacity) return false;
        out[(*length)++] = (char)(0xC0 | (cp >> 6));
        out[(*length)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        if (*length + 3 >= capacity) return false;
        out[(*length)++] = (char)(0xE0 | (cp >> 12));
        out[(*length)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*length)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        if (*length + 4 >= capacity) return false;
        out[(*length)++] = (char)(0xF0 | (cp >> 18));
        out[(*length)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*length)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*length)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        return false;
    }
    out[*length] = 0;
    return true;
}

static bool decode_json_string(const char *quoted, char *out, size_t capacity) {
    size_t pos = 0;
    size_t i = 0;
    if (!quoted || quoted[0] != '"' || !out || capacity == 0) return false;
    ++i;
    out[0] = 0;
    while (quoted[i] && quoted[i] != '"') {
        uint8_t c = (uint8_t)quoted[i++];
        if (c != '\\') {
            if (pos + 1 >= capacity) return false;
            out[pos++] = (char)c;
            out[pos] = 0;
            continue;
        }
        c = (uint8_t)quoted[i++];
        if (!c) return false;
        if (c == '"' || c == '\\' || c == '/') {
            if (pos + 1 >= capacity) return false;
            out[pos++] = (char)c;
            out[pos] = 0;
        } else if (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f') {
            char decoded = c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c == 'b' ? '\b' : '\f';
            if (pos + 1 >= capacity) return false;
            out[pos++] = decoded;
            out[pos] = 0;
        } else if (c == 'u') {
            uint16_t first;
            uint32_t cp;
            if (!parse_hex4(quoted + i, &first)) return false;
            i += 4;
            cp = first;
            if (first >= 0xD800 && first <= 0xDBFF && quoted[i] == '\\' && quoted[i + 1] == 'u') {
                uint16_t second;
                if (parse_hex4(quoted + i + 2, &second) && second >= 0xDC00 && second <= 0xDFFF) {
                    cp = 0x10000u + (((uint32_t)first - 0xD800u) << 10) + ((uint32_t)second - 0xDC00u);
                    i += 6;
                }
            }
            if (!append_utf8(out, capacity, &pos, cp)) return false;
        } else {
            return false;
        }
    }
    if (quoted[i] != '"') return false;
    while (pos && (out[pos - 1] == '\n' || out[pos - 1] == '\r')) out[--pos] = 0;
    return true;
}

static bool extract_string_after_key(const char *start, const char *key, char *out, size_t capacity) {
    const char *p = find_text(start, key);
    if (!p) return false;
    p += slen(key);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return decode_json_string(p, out, capacity);
}

static void set_http_status(int32_t code) {
    writer_t w;
    writer_init(&w, status_text, sizeof(status_text));
    writer_text(&w, "HTTP ");
    if (code < 0) writer_text(&w, "error");
    else writer_uint(&w, (uint32_t)code);
}

static void render_chat(bool waiting) {
    t5_ui_chrome_t chrome;
    t5_ui_text_view_result_t result;
    build_transcript();
    chrome.title = "Ask";
    chrome.subtitle = NULL;
    chrome.status = status_text;
    chrome.back_label = "Home";
    chrome.confirm_label = waiting ? "" : (network->wifi_connected() ? "Ask" : "Wi-Fi");
    chrome.previous_label = "Up";
    chrome.next_label = "Down";
    result.max_scroll_lines = 0;
    result.total_lines = 0;
    result.visible_lines = 0;
    ui->render_text_view(&chrome, transcript, session.scroll_from_bottom, &result);
    max_scroll_lines = result.max_scroll_lines;
    if (session.scroll_from_bottom > max_scroll_lines) session.scroll_from_bottom = max_scroll_lines;
}

static bool complete_question(void) {
    static const t5_http_header_t headers[] = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer unused"},
        {"User-Agent", "Manifold-ESP32"},
        {"Accept", "application/json"},
        {"Host", "api.llm7.io"},
    };
    t5_http_result_t result;
    const char *choices;
    const char *error_object;
    char reply[TURN_TEXT_CAP];
    bool ok;

    if (!build_request()) {
        set_status("Could not build request");
        return false;
    }

    set_status("Thinking...");
    render_chat(true);
    result = (t5_http_result_t){0};
    ok = network->http_request(kChatUrl, T5_HTTP_METHOD_POST, headers,
                               (uint32_t)(sizeof(headers) / sizeof(headers[0])), request_json, slen(request_json),
                               kLlm7TlsRoots, 30000, response_json, sizeof(response_json), &result);
    if (!ok) {
        result = (t5_http_result_t){0};
        ok = network->http_request(kChatUrl, T5_HTTP_METHOD_POST, headers,
                                   (uint32_t)(sizeof(headers) / sizeof(headers[0])), request_json, slen(request_json),
                                   kLlm7TlsRoots, 30000, response_json, sizeof(response_json), &result);
    }
    if (!ok) {
        set_status("Network request failed");
        return false;
    }
    if (result.status_code < 200 || result.status_code >= 300) {
        set_http_status(result.status_code);
        return false;
    }
    if (result.flags & T5_HTTP_RESPONSE_TRUNCATED) {
        set_status("Response too large");
        return false;
    }

    choices = find_text(response_json, "\"choices\"");
    if (choices && extract_string_after_key(choices, "\"content\"", reply, sizeof(reply)) && reply[0]) {
        add_turn(false, reply);
        set_status("Press Ask for another question");
        save_session();
        return true;
    }

    error_object = find_text(response_json, "\"error\"");
    if (error_object && extract_string_after_key(error_object, "\"message\"", reply, sizeof(reply)) && reply[0]) {
        set_status(reply);
    } else {
        set_status("Bad JSON from LLM7");
    }
    return false;
}

static bool request_keyboard(void) {
    if (!save_session()) {
        set_status("Could not save session");
        return false;
    }
    if (!system_ui->keyboard_request("Ask Manifold", "", QUESTION_CAP - 1, T5_SYSTEM_KEYBOARD_TEXT, COOKIE_KEYBOARD)) {
        set_status("Keyboard unavailable");
        return false;
    }
    app->set_back_exits_app(true);
    return true;
}

static bool request_wifi(void) {
    if (!save_session()) {
        set_status("Could not save session");
        return false;
    }
    if (!system_ui->wifi_request(COOKIE_WIFI)) {
        set_status("Wi-Fi picker unavailable");
        return false;
    }
    app->set_back_exits_app(true);
    return true;
}

static bool apis_ok(void) {
    size_t app_required = offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app);
    size_t storage_required = offsetof(t5_storage_api_v1, remove_file) + sizeof(storage->remove_file);
    size_t system_ui_required = offsetof(t5_system_ui_api_v1, wifi_take_result) + sizeof(system_ui->wifi_take_result);
    size_t ui_required = offsetof(t5_ui_api_v1, render_text_view) + sizeof(ui->render_text_view);
    size_t network_required = offsetof(t5_network_api_v1, http_request) + sizeof(network->http_request);
    return app && app->abi_version == T5_APP_ABI_VERSION && app->struct_size >= app_required &&
           app->screen_height && app->set_back_exits_app &&
           storage && storage->api_version == T5_STORAGE_API_VERSION && storage->struct_size >= storage_required &&
           storage->read_file && storage->write_file_atomic && storage->remove_file &&
           system_ui && system_ui->api_version == T5_SYSTEM_UI_API_VERSION && system_ui->struct_size >= system_ui_required &&
           system_ui->keyboard_request && system_ui->keyboard_take_result && system_ui->navigate_home &&
           system_ui->wifi_request && system_ui->wifi_take_result &&
           ui && ui->api_version == T5_UI_API_VERSION && ui->struct_size >= ui_required && ui->poll_event &&
           ui->render_text_view &&
           network && network->api_version == T5_NETWORK_API_VERSION && network->struct_size >= network_required &&
           network->wifi_connected && network->http_request;
}

__attribute__((visibility("default"))) void app_main(void) {
    bool wifi_result = false;
    bool wifi_connected = false;
    bool wifi_cancelled = false;
    uint64_t wifi_cookie = 0;
    bool keyboard_cancelled = false;
    uint64_t keyboard_cookie = 0;
    char question[QUESTION_CAP];
    t5_ui_event_t event;

    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    network = t5_network_get_api(T5_NETWORK_API_VERSION);
    if (!apis_ok()) return;

    app->set_back_exits_app(false);
    load_session();
    question[0] = 0;

    wifi_result = system_ui->wifi_take_result(&wifi_connected, &wifi_cancelled, &wifi_cookie);
    if (wifi_result) {
        if (wifi_cookie != COOKIE_WIFI) {
            set_status("Wi-Fi result ignored");
        } else if (!wifi_connected) {
            set_status(wifi_cancelled ? "Wi-Fi selection cancelled" : "Wi-Fi connection failed");
        } else {
            set_status("Press Ask to enter a question");
        }
    }

    if (system_ui->keyboard_take_result(question, sizeof(question), &keyboard_cancelled, &keyboard_cookie)) {
        if (keyboard_cookie != COOKIE_KEYBOARD) {
            set_status("Keyboard result ignored");
        } else if (!keyboard_cancelled && question[0]) {
            add_turn(true, question);
            save_session();
            complete_question();
        } else {
            set_status("Press Ask to enter a question");
        }
    } else if (!wifi_result) {
        set_status("Press Ask to enter a question");
    }

    if (!network->wifi_connected() && !wifi_result) {
        set_status("Connect to Wi-Fi to use Ask");
        render_chat(false);
        if (request_wifi()) return;
    }

    render_chat(false);
    while (ui->poll_event(&event, 20)) {
        if (event.type == T5_UI_EVENT_EXIT) {
            clear_session_file();
            app->set_back_exits_app(true);
            return;
        }
        if (event.type == T5_UI_EVENT_BACK) {
            clear_session_file();
            app->set_back_exits_app(true);
            system_ui->navigate_home();
            return;
        }
        if (event.type == T5_UI_EVENT_CONFIRM) {
            if (!network->wifi_connected()) {
                set_status("Connect to Wi-Fi to use Ask");
                render_chat(false);
                if (request_wifi()) return;
            } else if (request_keyboard()) {
                return;
            }
            render_chat(false);
            continue;
        }
        if (event.type == T5_UI_EVENT_PREVIOUS) {
            if (session.scroll_from_bottom < max_scroll_lines) ++session.scroll_from_bottom;
            render_chat(false);
            continue;
        }
        if (event.type == T5_UI_EVENT_NEXT) {
            if (session.scroll_from_bottom > 0) --session.scroll_from_bottom;
            render_chat(false);
            continue;
        }
        if (event.type == T5_UI_EVENT_TAP) {
            if (event.touch_y < app->screen_height() / 2) {
                if (session.scroll_from_bottom < max_scroll_lines) ++session.scroll_from_bottom;
            } else if (session.scroll_from_bottom > 0) {
                --session.scroll_from_bottom;
            }
            render_chat(false);
        }
    }

    clear_session_file();
    app->set_back_exits_app(true);
}
