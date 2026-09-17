#pragma once
// Host fixture compiles the production GPS driver runtime without Arduino logging.
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
