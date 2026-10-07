#pragma once
template <class... Args>
inline void fakeStorageLog(Args&&...) {}
#define LOG_ERR(...) fakeStorageLog(__VA_ARGS__)
#define LOG_INF(...) fakeStorageLog(__VA_ARGS__)
#define LOG_DBG(...) fakeStorageLog(__VA_ARGS__)
