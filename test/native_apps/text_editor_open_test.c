#include <stdlib.h>
#ifndef TEXT_EDITOR_SOURCE
#define TEXT_EDITOR_SOURCE "../../Apps/text_editor.c"
#endif
#include TEXT_EDITOR_SOURCE
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(86); } } while (0)
static unsigned entries, skipped, cursor, opened, closed, reads, writes, yielded, tick, latency;
static bool directory_ok = true, read_ok = true, write_ok = true, short_read, invalid_text;
static bool exit_scan, back_scan, poll_ok = true;
static char saved_path[T5_FILE_OPEN_PATH_MAX], saved_text[128];
static bool open_dir(const char *p) {
    CHECK(!strcmp(p, DOCUMENTS)); ++opened; cursor = 0; return directory_ok;
}
static bool next_dir(t5_app_dirent_t *e) {
    tick += latency;
    if (cursor == entries) return false;
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), cursor < skipped ? "skip%04u.bin" : "note%04u.txt", cursor);
    ++cursor; return true;
}
static void close_dir(void) { ++closed; }
static uint32_t now(void) { return tick; }
static bool input(t5_app_input_t *out, uint32_t wait) {
    CHECK(wait > 0); ++yielded; tick += wait;
    out->exit_requested = exit_scan;
    out->buttons = back_scan ? T5_APP_BUTTON_BACK : 0;
    return poll_ok;
}
static bool exists_file(const char *p) { (void)p; return false; }
static bool read_file(const char *p, void *buffer, size_t cap, size_t *n) {
    ++reads;
    // Match the production public-storage VFS precondition, not a permissive mock.
    if (strncmp(p, "/sd/", 4) || !read_ok) return false;
    const char *contents = invalid_text ? "\x01" : "hello";
    *n = strlen(contents);
    if (buffer) {
        CHECK(cap >= *n); memcpy(buffer, contents, *n);
        if (short_read && *n) --*n;
    }
    return true;
}
static bool write_file(const char *p, const void *data, size_t n) {
    ++writes; CHECK(!strncmp(p, "/sd/", 4));
    if (!write_ok) return false;
    CHECK(n < sizeof(saved_text)); memcpy(saved_text, data, n); saved_text[n] = 0;
    snprintf(saved_path, sizeof(saved_path), "%s", p); return true;
}
static const t5_app_api_v1 test_app = {
    .millis=now, .dir_open=open_dir, .dir_next=next_dir, .dir_close=close_dir, .poll=input
};
static const t5_storage_api_v1 test_storage = {
    .exists=exists_file, .read_file=read_file, .write_file_atomic=write_file
};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { (void)v; return NULL; }
const t5_storage_api_v1 *t5_storage_get_api(uint32_t v) { (void)v; return NULL; }
const t5_file_open_api_v1 *t5_file_open_get_api(uint32_t v) { (void)v; return NULL; }
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { (void)v; return NULL; }
static void reset(void) {
    te_reset(&document); path[0] = filename[0] = 0; mode = EDITING;
    entries=skipped=cursor=opened=closed=reads=writes=yielded=tick=latency=0;
    read_ok=write_ok=directory_ok=poll_ok=true; short_read=invalid_text=exit_scan=back_scan=false;
}
static void handoff(void) {
    const char *source="/sd/Documents/notes.txt";
    reset(); CHECK(load_handoff_path(source)); CHECK(!strcmp(path, source));
    CHECK(!strcmp(document.text,"hello")); CHECK(te_character(&document,'!'));
    write_ok=false; CHECK(!save_current()); CHECK(document.dirty);
    write_ok=true; CHECK(save_current()); CHECK(!document.dirty);
    CHECK(!strcmp(saved_path,source)); CHECK(strchr(saved_text,'!'));
    CHECK(te_character(&document,'?'));
    te_document prior=document; char old_path[sizeof(path)]; strcpy(old_path,path);
    for(unsigned failure=0; failure<3; ++failure) {
        read_ok=failure!=0; short_read=failure==1; invalid_text=failure==2;
        CHECK(!load_handoff_path("/sd/Other/retry.md"));
        CHECK(!memcmp(&document,&prior,sizeof(prior))); CHECK(!strcmp(path,old_path));
    }
    read_ok=true; short_read=invalid_text=false;
    unsigned before=reads;
    CHECK(!load_handoff_path("/Documents/notes.txt"));
    CHECK(!load_handoff_path("/sd/../notes.txt")); CHECK(reads==before);
    CHECK(load_handoff_path("/sd/Other/retry.md")); CHECK(save_current());
    CHECK(!strcmp(saved_path,"/sd/Other/retry.md"));
}
static void picker(void) {
    reset(); entries=130; transition(DO_OPEN); CHECK(file_count==64);
    CHECK(!strcmp(files[0],"note0000.txt")); CHECK(opened==1);
    key_press(0x4e,0); CHECK(file_count==64); CHECK(!strcmp(files[0],"note0064.txt"));
    key_press(0x4e,0); CHECK(file_count==2); CHECK(!strcmp(files[1],"note0129.txt"));
    CHECK(opened==1 && closed==1); // No prefix rescans; exactly one close at EOF.
    unsigned end=cursor; key_press(0x4e,0); CHECK(cursor==end && file_count==2);
    key_press(0x4a,0); CHECK(opened==2 && file_count==64);
    read_ok=false; key_press(0x28,0); CHECK(mode==FILE_PICKER && closed==1);
    read_ok=true; key_press(0x28,0); CHECK(mode==EDITING && closed==2);
    CHECK(!strcmp(path,"/sd/Documents/note0000.txt"));
    reset(); entries=301; skipped=300; transition(DO_OPEN);
    CHECK(file_count==0 && cursor==128); key_press(0x4e,0);
    CHECK(file_count==0 && cursor==256); key_press(0x4e,0);
    CHECK(file_count==1 && !strcmp(files[0],"note0300.txt")); CHECK(opened==1 && closed==1);
    reset(); entries=1000; transition(DO_OPEN); key_press(0x29,0);
    CHECK(mode==EDITING && closed==1); // Esc leaves the document untouched.
    reset(); entries=1000; latency=30; transition(DO_OPEN);
    CHECK(cursor<=4 && cursor>0 && yielded>0); // elapsed checkpoint, not only item budget
    key_press(0x29,0); CHECK(closed==1);
    reset(); entries=1000; back_scan=true; transition(DO_OPEN);
    CHECK(mode==EDITING && closed==1 && cursor<=8);
    reset(); entries=1000; exit_scan=true; transition(DO_OPEN);
    CHECK(mode==DONE && closed==1 && cursor<=8);
    reset(); entries=1000; poll_ok=false; transition(DO_OPEN);
    CHECK(mode==DONE && closed==1 && cursor<=8);
    reset(); directory_ok=false; transition(DO_OPEN);
    CHECK(!file_count && strstr(status,"Cannot open")); directory_ok=true; entries=1;
    key_press(0x4a,0); CHECK(file_count==1 && opened==2 && closed==1);
    // A dirty buffer must complete Save/Discard or Cancel before any new scan.
    reset(); CHECK(te_character(&document,'x')); transition(DO_OPEN);
    CHECK(mode==UNSAVED && opened==0); key_press(0x29,0);
    CHECK(mode==EDITING && document.dirty && document.text[0]=='x');
    reset(); entries=1000; transition(DO_OPEN); key_press(0x11,0);
    CHECK(mode==NEW_NAME && closed==1);
}
int main(int argc, char **argv) {
    app=&test_app; storage=&test_storage;
    if(argc==1 || !strcmp(argv[1],"handoff")) handoff();
    if(argc==1 || !strcmp(argv[1],"picker")) picker();
    puts("Text Editor document opening PASS"); return 0;
}
