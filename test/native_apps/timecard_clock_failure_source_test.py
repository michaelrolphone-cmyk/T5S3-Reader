#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/timecard.c").read_text(encoding="utf-8")

start = source.index("static void punch_today(")
end = source.index("\nstatic uint64_t make_cookie(", start)
func = source[start:end]

assert "now_minutes" not in source
assert func.count("system_api->local_datetime(&now)") == 1

clock_read = func.index("system_api->local_datetime(&now)")
failure_status = func.index('set_status("Clock unavailable")', clock_read)
failure_return = func.index("return;", failure_status)
date = func.index("make_ymd(now.year, now.month, now.day)", failure_return)
minutes = func.index("now.hour * 60 + now.minute", date)
save = func.index("set_punch(date, punch, minutes)", minutes)

assert clock_read < failure_status < failure_return < date < minutes < save

print("Time Card clock-failure punch contract PASS")
