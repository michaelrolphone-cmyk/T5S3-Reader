/* Source-level guard for the trusted consent UI and GNSS diagnostic. The
 * production firmware and real native app are also compiled by PlatformIO CI.
 * Both visible touch buttons must match their behavior; denying access must
 * not immediately exit the diagnostic app. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    const long size = ftell(file);
    assert(size > 0 && size < 131072);
    assert(fseek(file, 0, SEEK_SET) == 0);
    char *content = (char *)calloc((size_t)size + 1u, 1u);
    assert(content);
    assert(fread(content, 1u, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    return content;
}

int main(void) {
    char *consent = read_source("src/native/NativeDeviceConsent.cpp");
    assert(strstr(consent, "buttonY + 17, \"DENY\"") != NULL);
    assert(strstr(consent, "buttonY + 17, \"ALLOW\"") != NULL);
    assert(strstr(consent, "outline(denyX, buttonY, buttonWidth, 55)"));
    assert(strstr(consent, "outline(allowX, buttonY, buttonWidth, 55)"));
    assert(strstr(consent, "mappedInputManager.wasTouchTapped(touch, renderer)"));
    assert(strstr(consent, "const bool touchDeny ="));
    assert(strstr(consent, "const bool touchAllow ="));
    assert(strstr(consent, "!gpio.hadTouchActivity(), touchDeny, touchAllow)"));
    assert(strstr(consent, "NativeConsentDecision::Allow"));
    assert(strstr(consent, "NativeConsentDecision::Deny"));
    assert(strstr(consent, "permission prompt approved by fresh on-screen Allow tap"));
    assert(strstr(consent, "permission prompt timed out without a decision"));
    assert(!strstr(consent, "physical Confirm"));
    free(consent);

    char *app = read_source("Apps/gnss_stream_diagnostic.c");
    const char *main = strstr(app, "void app_main(void)");
    assert(main);
    const char *request = strstr(main, "devices->request(");
    const char *deny = strstr(main, "if (result == T5_DEVICE_DENIED)");
    const char *retry = deny ? strstr(deny, "if (!await_retry_or_back(true)) return;") : NULL;
    const char *subscribe = strstr(main, "location->subscribe(authorization");
    assert(request && deny && retry && subscribe);
    assert(request < deny && deny < retry && retry < subscribe);
    assert(strstr(app, "permission_retry_ready ? \"Retry\""));
    assert(strstr(app, "if (allow_retry && event.type == T5_UI_EVENT_CONFIRM) return true;"));
    assert(strstr(main, "if (result == T5_DEVICE_OK && authorization) break;"));
    assert(strstr(main, "status(\"Starting authorized GNSS stream\");"));
    free(app);
    puts("GNSS on-screen Allow/Deny and interactive denial contract passed");
    return 0;
}
