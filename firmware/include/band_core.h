#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable watch/companion state. Hardware and authenticated BLE transport are
// deliberately outside this module. No GPIO, modem, filesystem or radio access.
namespace hackerband {
constexpr uint8_t ProtocolVersion = 1;
constexpr size_t HeaderSize = 14, MaxPayload = 144, MaxFrame = HeaderSize + MaxPayload;
enum class Kind : uint8_t { Time=1, Ring, CallEnded, Message, Observation, Ignore, Decline, Result };
struct Packet {
  Kind kind=Kind::Time;
  uint32_t session=0, sequence=0;
  uint16_t size=0;
  uint8_t payload[MaxPayload]{};
};
size_t encode(const Packet& packet, uint8_t* output, size_t capacity);
bool decode(const uint8_t* bytes, size_t length, Packet& output);
void put32(uint8_t* bytes, uint32_t value);
uint32_t get32(const uint8_t* bytes);

// A transport opens a new session only AFTER encrypted, authenticated pairing.
// Counters never wrap: reconnect with a fresh random nonzero session before wrap.
class Session {
 public:
  bool open(uint32_t id, bool encrypted, bool authenticated);
  void close();
  bool accept(const Packet& packet);
  bool make(Kind kind, const uint8_t* payload, size_t size, Packet& packet);
  bool connected() const { return id_ != 0; }
 private:
  uint32_t id_=0, received_=0, sent_=0;
};

class Stopwatch {
 public:
  void tick(uint32_t now);
  void toggle(uint32_t now);
  void reset(uint32_t now);
  uint64_t elapsed(uint32_t now) const;
  bool running() const { return running_; }
 private:
  uint64_t elapsed_=0;
  uint32_t at_=0;
  bool running_=false;
};

// The adapter seeds this from a validated RTC or authenticated phone time.
// An unset clock stays unset; boot/build time is never displayed as real time.
class Clock {
 public:
  bool set(uint32_t utc, int16_t offsetMinutes, uint32_t now);
  void tick(uint32_t now);
  bool valid() const { return valid_; }
  uint64_t utc(uint32_t now) const;
  int64_t local(uint32_t now) const;
  int16_t offset() const { return offset_; }
 private:
  uint64_t utcMs_=0;
  uint32_t at_=0;
  int16_t offset_=0;
  bool valid_=false;
};

enum class CallAction : uint8_t { None, Ignore, Decline };
class IncomingCall {
 public:
  bool ring(uint32_t id, const char* caller, uint32_t now);
  void end(uint32_t id);
  void disconnect();
  void tick(uint32_t now);
  CallAction ignore();
  CallAction decline();
  bool active() const { return id_!=0; }
  bool audible() const { return active() && !ignored_ && !declinePending_; }
  bool declinePending() const { return declinePending_; }
  void declineResult(bool accepted);
  uint32_t id() const { return id_; }
  const char* caller() const { return caller_; }
 private:
  uint32_t id_=0, refreshed_=0;
  bool ignored_=false, declinePending_=false;
  char caller_[25]{};
};

// Call identifiers must be issued per ringing episode. This guard is checked
// again immediately before the phone queues AT+CHUP, never from a BLE callback.
bool canDecline(uint32_t requestedId, uint32_t currentId, bool ringing, bool authenticated);

struct Notice { uint32_t id=0; char title[33]{}, body[97]{}; };
class Inbox {
 public:
  static constexpr size_t Capacity=4;
  bool push(uint32_t id, const char* title, const char* body);
  const Notice* latest() const;
  size_t size() const { return count_; }
  void clear();
 private:
  Notice items_[Capacity]{};
  size_t next_=0, count_=0;
};

// No address persistence: callers pass a session-local observation identifier.
// Sustained sightings are required and alerts are bounded by a global cooldown.
class AlertGate {
 public:
  bool observe(uint32_t id, uint8_t confidence, uint32_t now);
  void reset();
 private:
  struct Entry { uint32_t id=0, first=0, last=0; uint8_t hits=0; } entries_[8]{};
  uint32_t alertAt_=0;
  bool alerted_=false;
};

struct RainCell { char glyph=' '; uint8_t brightness=0; };
// A display adapter chooses the grid after its real dimensions are known.
// Fixed storage and bounded catch-up keep animation out of notification timing.
class MatrixRain {
 public:
  bool begin(uint8_t columns, uint8_t rows, uint32_t seed, uint32_t now);
  void tick(uint32_t now);
  void interact(uint32_t now);
  RainCell cell(uint8_t column, uint8_t row) const;
 private:
  static constexpr unsigned MaxColumns=40;
  uint8_t heads_[MaxColumns]{}, lengths_[MaxColumns]{};
  uint8_t columns_=0, rows_=0;
  uint32_t random_=1, frame_=0, at_=0, interactionAt_=0;
  bool interacting_=false;
  uint32_t next();
};
}
