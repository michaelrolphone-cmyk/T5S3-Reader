#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_STREAM_API_VERSION 1u
#define T5_STREAM_CHUNK 512u
#define T5_STREAM_MAX_BUFFER 4096u
typedef uint32_t t5_stream_t;
typedef uint32_t t5_pipe_t;
typedef int32_t t5_stream_result_t;
enum {
  T5_STREAM_OK = 0, T5_STREAM_AGAIN = 1, T5_STREAM_EOF = 2,
  T5_STREAM_INVALID = -1, T5_STREAM_DENIED = -2, T5_STREAM_LIMIT = -3,
  T5_STREAM_UNSUPPORTED = -4, T5_STREAM_IO = -5, T5_STREAM_DISCONNECTED = -6,
  T5_STREAM_CANCELLED = -7, T5_STREAM_BUSY = -8, T5_STREAM_CLOSED = -9,
  T5_STREAM_TIMEOUT = -10
};
enum { T5_STREAM_READ = 1, T5_STREAM_WRITE = 2, T5_STREAM_SEEK = 4 };
enum { T5_STREAM_BYTES = 1, T5_STREAM_RECORDS = 2 };
enum { T5_PIPE_BLOCK_PRODUCER = 0 };
enum { T5_PIPE_RUNNING = 1, T5_PIPE_PAUSED, T5_PIPE_DONE, T5_PIPE_FAILED, T5_PIPE_CANCELLED };
enum { T5_STREAM_FILE_READ = 1, T5_STREAM_FILE_CREATE_NEW = 2 };
typedef struct {
  uint32_t struct_size, kind, flags, owner, capacity, buffered, high_water;
  int32_t terminal;
  uint64_t bytes_read, bytes_written;
} t5_stream_info_t;
typedef struct {
  uint32_t struct_size, owner, state, buffered;
  t5_stream_t source, destination;
  int32_t last_error;
  uint64_t bytes_transferred, stalls;
} t5_pipe_info_t;
/* All operations are session-bound. No app pointers/callbacks are retained.
 * read/write transfer at most T5_STREAM_CHUNK bytes and always set *count.
 * AGAIN means retry later; EOF is distinct from temporary lack of data.
 * A pipe exclusively leases source reads and destination writes until terminal.
 * BLOCK_PRODUCER is the only MVP policy; other values are rejected.
 */
typedef struct {
  uint32_t api_version, struct_size;
  t5_stream_result_t (*open_buffer)(uint32_t capacity, t5_stream_t *out);
  t5_stream_result_t (*open_file)(const char *path, uint32_t mode, t5_stream_t *out);
  /* Borrows the already-started USB service; close never stops VBUS. */
  t5_stream_result_t (*open_usb)(t5_stream_t *out);
  /* Starts one asynchronous HTTP body request. Uses existing network/TLS policy. */
  t5_stream_result_t (*open_http)(const char *url, t5_stream_t *out);
  t5_stream_result_t (*read)(t5_stream_t, void *, uint32_t capacity, uint32_t *count);
  t5_stream_result_t (*write)(t5_stream_t, const void *, uint32_t size, uint32_t *count);
  t5_stream_result_t (*finish)(t5_stream_t); /* buffer EOF; file flush/close */
  t5_stream_result_t (*seek)(t5_stream_t, uint64_t offset);
  t5_stream_result_t (*close)(t5_stream_t);
  t5_stream_result_t (*info)(t5_stream_t, t5_stream_info_t *);
  t5_stream_result_t (*pipe_connect)(t5_stream_t, t5_stream_t, uint32_t policy, t5_pipe_t *);
  t5_stream_result_t (*pipe_pause)(t5_pipe_t, uint32_t paused);
  t5_stream_result_t (*pipe_cancel)(t5_pipe_t);
  t5_stream_result_t (*pipe_close)(t5_pipe_t);
  t5_stream_result_t (*pipe_info)(t5_pipe_t, t5_pipe_info_t *);
} t5_stream_api_v1;
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
