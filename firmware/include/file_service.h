#pragma once
#include <stdint.h>
namespace hackerphone {
extern char fileMessage[80],fileOutput[160];
bool fileCommand(const char* line);
void fileTick(uint32_t now);
void fileCancel();
bool fileBusy();
bool fileTrash(const char* path);
bool fileRename(const char* from,const char* to);
bool fileCopy(const char* from,const char* to);
bool fileBackupSpiffs();
}
