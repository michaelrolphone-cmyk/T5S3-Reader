from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/timecard.c").read_text(encoding="utf-8")

load = source[source.index("static bool load_store("):source.index("\nstatic bool json_char(")]
assert "store_ready = false" in load
assert "loaded_days[loaded_count++] = day" in load
assert "if (!array_closed) return false" in load
assert "if (document_end != end) return false" in load
assert load.index("if (document_end != end) return false") < load.index("memcpy(days, loaded_days")
commit = load.index("memcpy(days, loaded_days")
assert commit < load.index("store_ready = true", commit)

save = source[source.index("static bool save_store("):source.index("\nstatic bool set_punch(")]
set_punch = source[source.index("static bool set_punch("):source.index("\nstatic void format_ampm(")]
assert "if (!store_ready) return false" in save
assert "!store_ready" in set_punch

edit = source[source.index("static bool request_edit("):source.index("\nstatic bool activate(")]
assert edit.index("if (!store_ready)") < edit.index("system_ui->keyboard_request")

consume = source[source.index("static bool consume_keyboard("):source.index("\nstatic bool apis_ok(")]
assert "if (!load_store())" in consume
assert 'set_status("History unavailable; read-only")' in consume

main = source[source.index("__attribute__((visibility(\"default\"))) void app_main(void)") :]
assert 'set_status(store_ready ? "Select a week" : "History unavailable; read-only")' in main

print("Time Card store-load failure contract PASS")
