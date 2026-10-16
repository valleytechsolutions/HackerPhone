#pragma once
#include "phone_core.h"
#include <stddef.h>
#include <stdint.h>
namespace hackerphone {
struct CellularStatus {bool active=false,starting=false,connected=false,stopping=false;uint32_t address=0,dns=0,errors=0;char message[80]="Cellular data off";};
bool cellularStart(PhoneCore& phone);
void cellularStop();
bool cellularOwnsModem();
bool cellularWrite(const char* bytes,size_t size);
void cellularPoll(PhoneCore& phone,uint32_t now);
CellularStatus cellularStatus();
bool cellularTest();
}
