#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/wifi_settings.c").read_text(encoding="utf-8")
start = source.index("void app_main(void)")
body = source[start:]

take = body.index("system_ui->wifi_take_result(&connected, &cancelled, &cookie)")
cookie_check = body.index("cookie != WIFI_COOKIE", take)
request = body.index("system_ui->wifi_request(WIFI_COOKIE)", cookie_check)
ret = body.index("return;", request)
render = body.index("render_result(connected, cancelled, selected)")

assert take < cookie_check < request < ret < render
assert "if (!system_ui->wifi_take_result(&connected, &cancelled, &cookie) ||" in body
print("Wi-Fi result cookie correlation source contract PASS")
