#pragma once

// Firmware logging is intentionally silent in host bridge tests.
#define LOG_ERR(tag, format, ...) do { (void)(tag); } while (0)
#define LOG_INF(tag, format, ...) do { (void)(tag); } while (0)
