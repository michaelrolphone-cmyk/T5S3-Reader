#pragma once
inline void clock_sync_test_log(const char*, const char*, ...) {}
#define LOG_ERR(...) clock_sync_test_log(__VA_ARGS__)
#define LOG_DBG(...) clock_sync_test_log(__VA_ARGS__)
