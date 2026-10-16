#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hackerphone {
enum class Sim : uint8_t { Unknown, Absent, PinRequired, PukRequired, Ready, Fault };
enum class Call : uint8_t { Idle, Dialing, Alerting, Ringing, Active, Held, Ending };
enum class Outcome : uint8_t { None, Pending, Accepted, Failed, TimedOut, Uncertain };
enum class Request : uint8_t { Accepted, Busy, NoSim, NoNetwork, Invalid, TooLong, UnsupportedText };

struct PhoneState {
  bool online=false;
  Sim sim=Sim::Unknown;
  Call call=Call::Idle;
  uint8_t registration=0;
  uint8_t circuitRegistration=0,packetRegistration=0;
  int16_t signalDbm=-127;
  bool signalKnown=false;
  char caller[24]{};
  char error[96]{};
  Outcome callOutcome=Outcome::None, smsOutcome=Outcome::None;
  uint32_t malformedLines=0, timeouts=0, inboxNotifications=0;
  int16_t newestSmsIndex=-1;
  char newestSmsStorage[4]{};
  bool registered() const { return registration==1 || registration==5; }
  bool phonePriority() const { return call!=Call::Idle; }
};

// Only the board adapter touches UART or GPIO. Host tests use a recording transport.
struct AtTransport {
  virtual ~AtTransport()=default;
  virtual bool write(const char* bytes,size_t length)=0;
};

struct SmsPdu { char hex[333]{}; uint16_t tpduBytes=0; uint8_t characters=0; };
struct PhoneDiagnostic { Outcome outcome=Outcome::None; bool truncated=false; char command[80]{},reply[1536]{}; };
bool validNumber(const char* number);
Request encodeSms(const char* number,const char* utf8,SmsPdu& out);

// Single owner/task. Nonblocking, fixed storage, no modem power cycling.
class PhoneCore {
 public:
  explicit PhoneCore(AtTransport& transport):transport_(transport){}
  void begin(uint32_t now);
  void tick(uint32_t now);
  void feed(const uint8_t* data,size_t length,uint32_t now);
  Request dial(const char* number,uint32_t now);
  Request answer(uint32_t now);
  Request hangup(uint32_t now);
  Request sendSms(const char* number,const char* utf8,uint32_t now);
  Request inspect(const char* command,uint32_t now);
  Request configure(const char* setting,uint32_t now);
  const PhoneDiagnostic& diagnostic() const { return diagnostic_; }
  const PhoneState& state() const { return state_; }
  bool busy() const;
  void suspend(bool value){suspended_=value;}
 private:
  enum class Cmd : uint8_t { None, Probe, Echo, Errors, Clip, SmsNotice, SimQuery, RegQuery, Signal, Calls, Dial, Answer, Hangup, SmsMode, SmsPrompt, SmsBody, Diagnostic, Resync };
  AtTransport& transport_;
  PhoneState state_{};
  PhoneDiagnostic diagnostic_{};
  Cmd active_=Cmd::None;
  uint32_t sentAt_=0, timeoutMs_=0, pollAt_=0, settleAt_=0;
  uint8_t initStep_=0, pollStep_=0;
  bool started_=false, settling_=false, discard_=false, smsAck_=false, callSeen_=false;
  bool suspended_=false;
  bool resyncSeen_=false,recoverAllowed_=true;
  char line_[384]{}; size_t lineSize_=0;
  SmsPdu pdu_{};
  bool command(Cmd kind,const char* text,uint32_t now,uint32_t timeout=5000);
  void line(const char* text,uint32_t now);
  void finish(bool success,uint32_t now,const char* error=nullptr);
  void abort(uint32_t now,const char* error,bool uncertain=false);
  void error(const char* text);
  Request available() const;
  Request diagnosticCommand(const char* text,uint32_t now,uint32_t timeout=10000);
};
}
