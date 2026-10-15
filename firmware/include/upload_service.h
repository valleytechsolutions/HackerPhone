#pragma once
#include <stdint.h>
namespace hackerphone {
struct UploadStatus {bool busy=false,accepted=false;uint32_t sent=0,total=0;int http=0;char message[80]="Select a geolocated WiGLE CSV";};
UploadStatus uploadStatus();
bool uploadStart(unsigned service,const char* path); // 0 WiGLE, 1 WDG Wars
void uploadCancel();
}
