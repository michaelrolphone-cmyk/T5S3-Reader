#pragma once

#include <T5StreamApi.h>
#include <stddef.h>
#include <stdint.h>

// Transport-independent transfer loops for callers with an active stream
// execution context. HTTP, file operations, scheduling and handle ownership
// remain in the firmware stream API. This header deliberately needs no Arduino,
// SD, HTTPClient or application ELF types, so the actual loops are host-testable.
namespace RuntimeHttpStreams {

enum class Result {
  Ok,
  Invalid,
  Http,
  File,
  Transfer,
  Timeout,
  Cancelled,
};

struct Hooks {
  void* context = nullptr;
  uint32_t (*now_ms)(void*) = nullptr;
  void (*cooperate)(void*) = nullptr;  // Feed the watchdog and yield to the scheduler.
  bool (*cancelled)(void*) = nullptr;
  uint32_t stall_timeout_ms = 60000;
};

using Consume = bool (*)(void*, const uint8_t*, uint32_t);
using Progress = void (*)(void*, uint64_t);

inline bool hasApi(const t5_stream_api_v1* api) {
  return api && api->api_version == T5_STREAM_API_VERSION &&
         api->struct_size >= offsetof(t5_stream_api_v1, pipe_info) + sizeof(api->pipe_info) &&
         api->open_http && api->open_file && api->read && api->finish && api->close &&
         api->pipe_connect && api->pipe_cancel && api->pipe_close && api->pipe_info;
}

struct Handles {
  const t5_stream_api_v1* api;
  t5_stream_t source = 0;
  t5_stream_t destination = 0;
  t5_pipe_t pipe = 0;
  bool pipeDone = false;
  explicit Handles(const t5_stream_api_v1* value) : api(value) {}
  ~Handles() {
    if (pipe) {
      if (!pipeDone) (void)api->pipe_cancel(pipe);
      (void)api->pipe_close(pipe);
    }
    if (destination) (void)api->close(destination);
    if (source) (void)api->close(source);
  }
  Handles(const Handles&) = delete;
  Handles& operator=(const Handles&) = delete;
};

inline bool timedOut(const Hooks& hooks, uint32_t lastProgress) {
  return hooks.now_ms(hooks.context) - lastProgress >= hooks.stall_timeout_ms;
}
inline bool interrupted(const Hooks& hooks) {
  return hooks.cancelled && hooks.cancelled(hooks.context);
}
inline void cooperate(const Hooks& hooks) { hooks.cooperate(hooks.context); }

// Reads an HTTP response through open_http, delivering at most 512 bytes per
// callback. maxBytes == 0 means that the consumer itself enforces its bound
// (e.g. the release-assets streaming parser). An EOF after a failed consumer
// operation or timeout must never be treated as a successful response.
inline Result fetch(const t5_stream_api_v1* api, const char* url, const Hooks& hooks,
                    Consume consume, void* consumer, uint64_t maxBytes = 0,
                    uint64_t* received = nullptr) {
  if (received) *received = 0;
  if (!hasApi(api) || !url || !consume || !hooks.now_ms || !hooks.cooperate ||
      !hooks.stall_timeout_ms) return Result::Invalid;
  Handles handles(api);
  if (api->open_http(url, &handles.source) != T5_STREAM_OK) return Result::Http;
  uint8_t bytes[T5_STREAM_CHUNK];
  uint64_t total = 0;
  uint32_t lastProgress = hooks.now_ms(hooks.context);
  for (;;) {
    if (interrupted(hooks)) return Result::Cancelled;
    uint32_t count = 0;
    const int32_t status = api->read(handles.source, bytes, sizeof(bytes), &count);
    if (count > sizeof(bytes)) return Result::Transfer;
    if (status == T5_STREAM_EOF) {
      if (!total) return Result::Http;
      if (received) *received = total;
      return Result::Ok;
    }
    if (status != T5_STREAM_OK && status != T5_STREAM_AGAIN) return Result::Http;
    if (count) {
      if (maxBytes && (total > maxBytes || count > maxBytes - total)) return Result::Transfer;
      if (!consume(consumer, bytes, count)) return Result::Transfer;
      total += count;
      lastProgress = hooks.now_ms(hooks.context);
    } else if (timedOut(hooks, lastProgress)) {
      return Result::Timeout;
    }
    cooperate(hooks);
  }
}

// The caller owns the staged path ONLY after CREATE_NEW succeeds. It must not
// delete a pre-existing path if the exclusive open fails after its initial
// exists check. destinationCreated reports that ownership even on subsequent
// HTTP, pipe, cancellation and finish failures, so cleanup is conditional.
// Only DONE followed by pipe_close and successful destination finish is success.
inline Result download(const t5_stream_api_v1* api, const char* url,
                       const char* newStagePath, const Hooks& hooks,
                       Progress progress = nullptr, void* progressContext = nullptr,
                       uint64_t* transferred = nullptr, bool* destinationCreated = nullptr) {
  if (transferred) *transferred = 0;
  if (destinationCreated) *destinationCreated = false;
  if (!hasApi(api) || !url || !newStagePath || !hooks.now_ms ||
      !hooks.cooperate || !hooks.stall_timeout_ms) return Result::Invalid;
  Handles handles(api);
  if (api->open_file(newStagePath, T5_STREAM_FILE_CREATE_NEW, &handles.destination) != T5_STREAM_OK)
    return Result::File;
  if (destinationCreated) *destinationCreated = true;
  if (api->open_http(url, &handles.source) != T5_STREAM_OK) return Result::Http;
  if (api->pipe_connect(handles.source, handles.destination, T5_PIPE_BLOCK_PRODUCER,
                        &handles.pipe) != T5_STREAM_OK) return Result::Transfer;

  uint64_t lastBytes = 0;
  uint32_t lastProgress = hooks.now_ms(hooks.context);
  for (;;) {
    if (interrupted(hooks)) return Result::Cancelled;
    t5_pipe_info_t status{};
    status.struct_size = sizeof(status);
    if (api->pipe_info(handles.pipe, &status) != T5_STREAM_OK) return Result::Transfer;
    if (status.state == T5_PIPE_FAILED || status.state == T5_PIPE_CANCELLED)
      return Result::Transfer;
    if (status.bytes_transferred != lastBytes) {
      lastBytes = status.bytes_transferred;
      lastProgress = hooks.now_ms(hooks.context);
      if (progress) progress(progressContext, lastBytes);
    }
    if (status.state == T5_PIPE_DONE) {
      if (!lastBytes || status.buffered || status.last_error) return Result::Transfer;
      handles.pipeDone = true;
      if (api->pipe_close(handles.pipe) != T5_STREAM_OK) return Result::Transfer;
      handles.pipe = 0;
      if (api->finish(handles.destination) != T5_STREAM_OK) return Result::File;
      if (transferred) *transferred = lastBytes;
      return Result::Ok;
    }
    if (status.state != T5_PIPE_RUNNING && status.state != T5_PIPE_PAUSED)
      return Result::Transfer;
    if (timedOut(hooks, lastProgress)) return Result::Timeout;
    cooperate(hooks);
  }
}

}  // namespace RuntimeHttpStreams
