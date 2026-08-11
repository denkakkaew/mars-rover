#pragma once
#include <stdint.h>

/// The failsafe state machine.
///
/// docs/protocol.md section 6 is the contract; this is its implementation. A degraded
/// link must never leave the rover driving into the glass (risk R1), so the whole thing
/// is expressed as a pure function over explicit inputs, testable on a PC with no ESP32
/// and no clock (IMPLEMENTATION_PLAN.md S.3/S.4).
///
/// **No Arduino headers.** Time arrives as a parameter; nothing here calls `millis()`.
namespace safety {

enum class State : uint8_t {
  Safe,          ///< Drive current cut, servo commands ignored. Servos hold position.
  Armed,         ///< Acting on commands.
  Incompatible,  ///< Protocol version mismatch. Never arms, whatever arrives.
};

/// Everything the decision depends on. Nothing else may influence it.
struct Inputs {
  bool connected = false;

  /// Whether a failsafe-refreshing command has arrived **since the current connection
  /// opened**. Scoped to the connection on purpose: reconnecting must not re-arm, and
  /// a plain "have we ever been commanded" flag would let a reconnect inside the timeout
  /// window arm the rover with nobody touching the screen (protocol.md 6.3).
  bool commanded_since_connect = false;

  uint32_t last_command_ms = 0;
  uint32_t now_ms = 0;
  uint32_t timeout_ms = 500;

  /// Whether a matching `hello` has been exchanged on this connection. Nothing moves
  /// before it has: a console that has not identified itself is not one to take
  /// commands from, and failing closed here is what makes the handshake deadline a
  /// safety property rather than a log message (protocol.md 5).
  bool handshake_ok = false;

  /// False once a `hello` with the wrong version has been seen, or once the deadline
  /// for hearing one has passed (protocol.md 5).
  bool peer_compatible = true;
};

/// True once `timeout_ms` or more has elapsed. Unsigned arithmetic on the difference,
/// so this stays correct across the ~49-day `millis()` wraparound — do not rewrite it
/// as `now > last + timeout`, which does not.
bool timedOut(uint32_t last_command_ms, uint32_t now_ms, uint32_t timeout_ms);

/// The whole state machine. Pure: same inputs, same answer, no hidden state.
State evaluate(const Inputs &in);

/// Thin holder that tracks `Inputs` across events. Every decision is delegated to
/// `evaluate`; this class only records what happened and when.
class Failsafe {
 public:
  explicit Failsafe(uint32_t timeout_ms);

  /// A console connected. Clears the armed-since-connect flag, so the fresh connection
  /// starts safe and needs a real command before anything moves.
  void onConnect();

  /// The console went away, cleanly or otherwise.
  void onDisconnect();

  /// Call **only** for commands where `protocol::refreshesFailsafe` is true. Malformed
  /// frames, unknown verbs, and pings must not reach this (protocol.md 6.2).
  void onCommand(uint32_t now_ms);

  /// Records the outcome of a `hello` handshake. Once false, only a matching `hello`
  /// clears it — no command, reconnect, or timeout can.
  void setPeerCompatible(bool compatible);

  /// Marks the handshake as agreed. Cleared automatically on connect and disconnect,
  /// so every new connection has to identify itself again.
  void setHandshakeOk(bool ok);

  State state(uint32_t now_ms) const;

  const Inputs &inputs() const { return in_; }

 private:
  Inputs in_;
};

}  // namespace safety
