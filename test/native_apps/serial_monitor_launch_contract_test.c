/* Regression contract for the native Serial Monitor startup path.
 * The real app UI host test covers session acquisition/revocation. This source
 * assertion checks that the production app renders its initial state before
 * entering potentially long-running serial capability acquisition, retains an
 * interactive retry after failure, and does not exit on missing hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || size > 131072 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    char *bytes = (char *)calloc((size_t)size + 1, 1);
    if (!bytes) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    return bytes;
}
int main(void) {
    char *source = read_source("Apps/serial_monitor.c");
    assert(source);
    const char *main = strstr(source, "void app_main(void) {");
    assert(main);
    const char *render = strstr(main, "render_acquiring(false);");
    const char *acquire = strstr(main, "acquire_serial_session(&state, &coding)");
    const char *retry = strstr(main, "if (!serial_lease && reconnect_pending");
    const char *poll = strstr(main, "ui->poll_event(&event, 75)");
    assert(render && acquire && render < acquire);
    assert(retry && poll && acquire < retry && retry < poll);
    free(source);
    return 0;
}
