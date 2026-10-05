#pragma once
#include <cstdint>

namespace wire {
enum class StationEvent { none, started, stopped, disconnected, associated, got_ip };

// Default netif handlers enqueue GOT_IP separately from STA_CONNECTED. If a
// disconnect was already queued, that IP notification can arrive after it.
// Call under the same lock as mailbox publication and the readiness update.
class StationEventGate {
 public:
  bool accept(StationEvent event, bool matching_netif = true) {
    if (event == StationEvent::got_ip) return associated_ && matching_netif;
    if (event == StationEvent::associated) associated_ = true;
    if (event == StationEvent::started || event == StationEvent::stopped
        || event == StationEvent::disconnected) associated_ = false;
    return event != StationEvent::none;
  }
 private:
  bool associated_ = false;
};

// Worker-owned policy: no driver calls or waiting in the Wi-Fi event callback.
// A missing completion event must not leave one connect attempt pending forever.
class StationReconnect {
 public:
  enum class Action { none, connect, timeout };
  static constexpr uint32_t attempt_timeout_ms = 15000;
  void event(StationEvent event, uint32_t now) {
    switch (event) {
      case StationEvent::started:
        retry_ms_ = 250; state_ = State::waiting; deadline_ = now; break;
      case StationEvent::stopped: state_ = State::off; break;
      case StationEvent::disconnected:
        // A disconnect requested by our timeout can report asynchronously.
        // Keep the retry already scheduled instead of doubling its backoff.
        if (state_ != State::waiting) retry(now);
        break;
      case StationEvent::associated:
        state_ = State::connecting; deadline_ = now + attempt_timeout_ms; break;
      case StationEvent::got_ip: online(); break;
      case StationEvent::none: break;
    }
  }
  Action poll(uint32_t now, bool ready) {
    if (ready) { online(); return Action::none; }
    if (state_ == State::online) retry(now);
    if (state_ == State::off || int32_t(now - deadline_) < 0) return Action::none;
    if (state_ == State::waiting) {
      state_ = State::connecting; deadline_ = now + attempt_timeout_ms;
      return Action::connect;
    }
    if (state_ == State::connecting) { retry(now); return Action::timeout; }
    return Action::none;
  }
  void connect_result(bool accepted, uint32_t now) {
    if (!accepted) retry(now);
  }
 private:
  enum class State { off, waiting, connecting, online };
  State state_ = State::off;
  uint32_t deadline_ = 0, retry_ms_ = 250;
  void online() { state_ = State::online; retry_ms_ = 250; }
  void retry(uint32_t now) {
    state_ = State::waiting; deadline_ = now + retry_ms_;
    retry_ms_ = retry_ms_ < 4000 ? retry_ms_ * 2 : 4000;
  }
};
}
