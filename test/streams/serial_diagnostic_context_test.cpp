// Link the real app-facing production serial API with its existing simulated
// bridge fixture; no test reimplementation of diagnostics is permitted.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_serial_provider_fixture_main
#include "serial_provider_bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop

int main() {
  RuntimeResources::ExecutionContext first;
  assert(first.begin());
  nativeSerialPortsBegin();
  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(serial && serial->last_diagnostic);
  t5_serial_diagnostic_t detail{};
  assert(!serial->last_diagnostic(&detail));

  t5_serial_port_lease_t lease = 99;
  t5_stream_t rx = 99, tx = 99;
  assert(serial->acquire(nullptr, &lease, &rx, &tx) == T5_SERIAL_INVALID);
  assert(!lease && !rx && !tx);
  assert(serial->last_diagnostic(&detail) && detail.result == T5_SERIAL_INVALID);
  assert(detail.provider_error == 0 && std::strstr(detail.detail, "provider=none"));

  // A cached function-table pointer must not disclose an old app's failure.
  nativeSerialPortsEnd();
  first.end();
  assert(!serial->last_diagnostic(&detail));
  RuntimeResources::ExecutionContext second;
  assert(second.begin());
  nativeSerialPortsBegin();
  assert(!serial->last_diagnostic(&detail));
  assert(serial->acquire(nullptr, &lease, &rx, &tx) == T5_SERIAL_INVALID);
  assert(serial->last_diagnostic(&detail) && detail.result == T5_SERIAL_INVALID);
  nativeSerialPortsEnd();
  second.end();
  assert(!serial->last_diagnostic(&detail));
  std::puts("Serial diagnostics reject stale execution-context owners");
}
