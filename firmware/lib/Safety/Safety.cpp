#include "Safety.h"

namespace safety {

bool timedOut(uint32_t last_command_ms, uint32_t now_ms, uint32_t timeout_ms) {
  return static_cast<uint32_t>(now_ms - last_command_ms) >= timeout_ms;
}

State evaluate(const Inputs &in) {
  // A version mismatch outranks everything, including a valid command stream. It is not
  // cleared by driving, reconnecting, or waiting (protocol.md 5).
  if (!in.peer_compatible) return State::Incompatible;

  // No console, no authority to move.
  if (!in.connected) return State::Safe;

  // Boot, or a fresh connection: safe until commanded. This is the rule that stops the
  // rover lurching back into motion the instant Wi-Fi recovers.
  if (!in.commanded_since_connect) return State::Safe;

  // Silence is a degraded link, not a command to keep driving.
  if (timedOut(in.last_command_ms, in.now_ms, in.timeout_ms)) return State::Safe;

  return State::Armed;
}

Failsafe::Failsafe(uint32_t timeout_ms) { in_.timeout_ms = timeout_ms; }

void Failsafe::onConnect() {
  in_.connected = true;
  in_.commanded_since_connect = false;
}

void Failsafe::onDisconnect() {
  in_.connected = false;
  in_.commanded_since_connect = false;
}

void Failsafe::onCommand(uint32_t now_ms) {
  in_.last_command_ms = now_ms;
  in_.commanded_since_connect = true;
}

void Failsafe::setPeerCompatible(bool compatible) { in_.peer_compatible = compatible; }

State Failsafe::state(uint32_t now_ms) const {
  Inputs snapshot = in_;
  snapshot.now_ms = now_ms;
  return evaluate(snapshot);
}

}  // namespace safety
