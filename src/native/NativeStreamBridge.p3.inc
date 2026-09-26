
bool nativeStreamSerialIsBusy() {
  if (!authorized() || !initialize()) return true;
  Lock lock;
  return serialOpen;
}

t5_stream_result_t nativeStreamOpenSerialPair(t5_stream_t* rx, t5_stream_t* tx) {
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!rx || !tx) return T5_STREAM_INVALID;
  if (!initialize()) return T5_STREAM_LIMIT;
  if (!nativeSerialProviderActive()) return T5_STREAM_UNSUPPORTED;

  Lock lock;
  if (serialOpen) return T5_STREAM_BUSY;
  const uint32_t sessionEpoch = nativeSerialProviderEpoch();
  if (!sessionEpoch) return T5_STREAM_DISCONNECTED;
  auto* link = new (std::nothrow) SerialLink{sessionEpoch, owner};
  if (!link) return T5_STREAM_LIMIT;
  auto result = registry.publishEndpoint(owner, T5_STREAM_BYTES, T5_STREAM_READ,
                                         T5_STREAM_CHUNK * 4, nullptr, 0, 0,
                                         RuntimeStreams::kStreamPublic, rx);
  if (result != T5_STREAM_OK) {
    delete link;
    return result;
  }
  result = registry.publishEndpoint(owner, T5_STREAM_BYTES, T5_STREAM_WRITE,
                                    T5_STREAM_CHUNK * 4, nullptr, 0, 0,
                                    RuntimeStreams::kStreamPublic, tx);
  if (result != T5_STREAM_OK) {
    (void)registry.close(owner, *rx);
    *rx = 0;
    delete link;
    return result;
  }
  pairSerial = link;
  pairRx = *rx;
  pairTx = *tx;
  serialOpen = true;
  serialRefs = 2;
  pendingSerial = {};
  pendingSerial.rx = *rx;
  pendingSerial.tx = *tx;
  serialTxRetry.store(false, std::memory_order_release);
  wake();
  return T5_STREAM_OK;
}

#if !defined(ESP_PLATFORM) && !defined(ARDUINO_ARCH_ESP32)
bool nativeStreamUsbIsBusy() { return nativeStreamSerialIsBusy(); }
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx) {
  return nativeStreamOpenSerialPair(rx, tx);
}
#endif

t5_stream_result_t nativeStreamCloseOwned(t5_stream_t stream) {
  if (!active || invocation.id() != owner || !t5_app_get_api(T5_APP_ABI_VERSION))
    return T5_STREAM_DENIED;
  if (!stream || !initialize()) return T5_STREAM_INVALID;
  RetiredProvider retired;
  t5_stream_result_t result;
  {
    Lock lock;
    result = registry.close(owner, stream, &retired.value);
    if (result == T5_STREAM_OK) notePairClosed(stream);
    forgetClosedGnssStream(stream);
  }
  wake();
  return result;
}

void nativeStreamsBegin() {
  if (!invocation.begin()) return;
  owner = invocation.id();
  active = true;
  if (!invocation.track(RuntimeResources::ExecutionContext::Resource::Streams, releaseStreams)) {
    invocation.end();
    active = false;
    return;
  }
  nativeSerialPortsBegin();
  if (!invocation.track(RuntimeResources::ExecutionContext::Resource::SerialPort, releaseSerial)) {
    nativeSerialPortsEnd();
    invocation.end();
    active = false;
  }
}
void nativeStreamsEnd() {
  invocation.end();
  active = false;
}
extern "C" const t5_stream_api_v1* t5_stream_get_api(uint32_t version) {
  if (!authorized() || !initialize()) return nullptr;
  if (version == T5_STREAM_API_VERSION) return &api;
  if (version == RISCRTE_STREAM_API_VERSION_2) return &api2.v1;
  return nullptr;
}

t5_stream_result_t nativeGnssAttach(uint32_t providerOwner, uint32_t device, uint32_t borrowedSourceLease) {
  if (!authorized() || !initialize() || providerOwner != owner) return T5_STREAM_DENIED;
  Lock lock;
  return gnss.attach(providerOwner, device, borrowedSourceLease);
}
RuntimeStreams::LiveGnssSession::PollDecision nativeGnssBeforePoll(uint32_t providerOwner) {
  if (!authorized() || !initialize() || providerOwner != owner)
    return RuntimeStreams::LiveGnssSession::PollDecision::Denied;
  Lock lock;
  const auto result = gnss.beforePoll(providerOwner);
  wake();
  return result;
}
t5_stream_result_t nativeGnssPublishCopy(uint32_t providerOwner, const t5_gps_state_t& observation,
                                        uint32_t sampleMs) {
  if (!authorized() || !initialize() || providerOwner != owner) return T5_STREAM_DENIED;
  Lock lock;
  const auto result = gnss.publishCopied(providerOwner, observation, sampleMs);
  wake();
  return result;
}
void nativeGnssDisconnect(uint32_t providerOwner) {
  if (!mutex || !providerOwner || invocation.id() != providerOwner) return;
  Lock lock;
  if (gnss.owner() == providerOwner) gnss.disconnect();
  wake();
}
t5_stream_result_t nativeGnssSubscribe(uint32_t authenticatedOwner, uint32_t authorizedDevice,
                                      uint32_t issuedReadConsent,
                                      uint64_t* subscription, t5_stream_t* stream) {
  if (subscription) *subscription = 0;
  if (stream) *stream = 0;
  if (!authorized() || !initialize() || authenticatedOwner != owner ||
      !authorizedDevice || !issuedReadConsent || !subscription || !stream) return T5_STREAM_DENIED;
  Lock lock;
  if (gnss.device() != authorizedDevice) return T5_STREAM_DISCONNECTED;
  const auto result = gnss.subscribe(authenticatedOwner, subscription, stream);
  if (result != T5_STREAM_OK) return result;
  if (!gnssGrants.bind(authenticatedOwner, authorizedDevice, issuedReadConsent,
                       *subscription, *stream)) {
    (void)gnss.unsubscribe(authenticatedOwner, *subscription);
    *subscription = 0;
    *stream = 0;
    return T5_STREAM_DENIED;
  }
  return T5_STREAM_OK;
}
t5_stream_result_t nativeGnssUnsubscribe(uint32_t authenticatedOwner, uint64_t subscription) {
  if (!active || invocation.id() != authenticatedOwner || !mutex) return T5_STREAM_DENIED;
  Lock lock;
  const auto result = gnss.unsubscribe(authenticatedOwner, subscription);
  if (result == T5_STREAM_OK || result == T5_STREAM_INVALID)
    (void)gnssGrants.forgetSubscription(authenticatedOwner, subscription);
  return result;
}
