#include "file_service.h"
#include "mp24_board.h"
#include "device_controls.h"
#include "storage_service.h"
#include "mission_service.h"
#include "music_service.h"
#include <esp_partition.h>
namespace hackerphone {
char fileMessage[80]="Ready",fileOutput[160]{};
static File output,input;static String finalPath,tempPath;static uint32_t total=0,done=0,expectedCrc=0,crc=0,lastChunk=0;
static uint8_t operation=0;static const esp_partition_t* backupPartition=nullptr;
static uint8_t* buffer=nullptr;
static bool allowed(const char* path){return mp24::cardReady&&safeSdPath(path)&&!strncmp(path,"/hackerphone/",13);}
bool fileBusy(){return operation!=0;}
void fileCancel(){output.close();input.close();if(operation&&tempPath.length())SD.remove(tempPath);operation=0;backupPartition=nullptr;snprintf(fileMessage,sizeof(fileMessage),"Cancelled");}
static bool begin(const char* path,uint32_t length,uint8_t mode){
 if(fileBusy()||!allowed(path)||SD.exists(path)||length>64*1024*1024)return false;
 if(!buffer){buffer=static_cast<uint8_t*>(ps_malloc(4096));if(!buffer)return false;}
 finalPath=path;tempPath=finalPath+".part";if(SD.exists(tempPath))return false;
 if(!SD.exists("/hackerphone")&&!SD.mkdir("/hackerphone"))return false;
 // Create only the destination's missing parents within HackerPhone.
 for(unsigned i=13;i<finalPath.length();++i)if(finalPath[i]=='/'){String dir=finalPath.substring(0,i);if(!SD.exists(dir)&&!SD.mkdir(dir))return false;}
 if(!SD.exists("/hackerphone")&&!SD.mkdir("/hackerphone"))return false;
 output=SD.open(tempPath,FILE_WRITE);if(!output)return false;operation=mode;total=length;done=crc=0;lastChunk=millis();snprintf(fileOutput,sizeof(fileOutput),"%s",path);snprintf(fileMessage,sizeof(fileMessage),"Writing new file");return true;
}
static bool append(const uint8_t* bytes,size_t n){if(!output||n>total-done||output.write(bytes,n)!=n)return false;crc=fileCrc32(crc,bytes,n);done+=n;lastChunk=millis();return true;}
static bool finish(){
 output.flush();output.close();input.close();if(done!=total||(operation==1&&crc!=expectedCrc)){fileCancel();snprintf(fileMessage,sizeof(fileMessage),"Size / CRC mismatch");return false;}
 if(SD.exists(finalPath)||!SD.rename(tempPath,finalPath)){fileCancel();snprintf(fileMessage,sizeof(fileMessage),"Commit failed");return false;}
 bool backup=operation==3;operation=0;snprintf(fileMessage,sizeof(fileMessage),"Saved %lu bytes CRC32 %08lx",(unsigned long)done,(unsigned long)crc);
 if(backup){File manifest=SD.open(finalPath+".txt",FILE_WRITE);if(manifest){manifest.printf("Raw engineering SPIFFS backup; do not flash generically.\nSize %lu\nCRC32 %08lx\n",(unsigned long)done,(unsigned long)crc);manifest.close();}}return true;
}
static bool containsPath(const char* parent,const char* child){size_t n=strlen(parent);return !strncmp(parent,child,n)&&(child[n]==0||child[n]=='/');}
static bool inUse(const char* path){auto music=musicStatus();return (storageStatus.recording&&containsPath(path,storageStatus.path))||(mission.active&&(containsPath(path,mission.path)||containsPath(mission.path,path)))||(music.busy&&containsPath(path,music.path));}
bool fileRename(const char* from,const char* to){if(fileBusy()||!allowed(from)||!allowed(to)||inUse(from)||SD.exists(to))return false;return SD.rename(from,to);}
bool fileTrash(const char* path){
 if(fileBusy()||!allowed(path))return false;File f=SD.open(path);if(!f||f.isDirectory()){f.close();return false;}f.close();SD.mkdir("/hackerphone/trash");
 const char* basename=strrchr(path,'/');for(unsigned n=0;n<1000;++n){snprintf(fileOutput,sizeof(fileOutput),"/hackerphone/trash/%08lx-%03u-%s",(unsigned long)millis(),n,basename?basename+1:"file");if(strlen(fileOutput)>=150)return false;if(!SD.exists(fileOutput))return fileRename(path,fileOutput);}return false;
}
bool fileCopy(const char* from,const char* to){if(fileBusy()||!allowed(from)||!allowed(to)||inUse(from))return false;File f=SD.open(from);if(!f||f.isDirectory()){f.close();return false;}if(!begin(to,f.size(),2)){f.close();return false;}input=f;return true;}
bool fileBackupSpiffs(){if(fileBusy())return false;backupPartition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");if(!backupPartition)return false;char p[128];snprintf(p,sizeof(p),"/hackerphone/backups/spiffs-%08lx.bin",(unsigned long)millis());return begin(p,backupPartition->size,3);}
void fileTick(uint32_t now){
 if(!operation)return;if(operation==1){if(uint32_t(now-lastChunk)>60000)fileCancel();return;}
 if(done==total){finish();return;}unsigned n=std::min<uint32_t>(4096,total-done);bool ok=false;
 if(operation==2)ok=input.read(buffer,n)==int(n);else if(operation==3)ok=esp_partition_read(backupPartition,done,buffer,n)==ESP_OK;
 if(!ok||!append(buffer,n)){fileCancel();snprintf(fileMessage,sizeof(fileMessage),"File IO error");}
}
bool fileCommand(const char* line){
 bool ok=false;unsigned size=0,hash=0,offset=0;char path[151],extra;
 if(!strcmp(line,"FILE STATUS")){Serial.printf("HP_FILE busy=%u done=%lu total=%lu crc=%08lx path=%s message=%s\n",fileBusy(),(unsigned long)done,(unsigned long)total,(unsigned long)crc,fileOutput,fileMessage);return true;}
 if(!strcmp(line,"FILE CANCEL")){fileCancel();ok=true;}
 else if(!strncmp(line,"FILE MKDIR ",11))ok=allowed(line+11)&&!SD.exists(line+11)&&SD.mkdir(line+11);
 else if(!strncmp(line,"FILE TRASH ",11))ok=fileTrash(line+11);
 else if(!strncmp(line,"FILE RENAME ",12)||!strncmp(line,"FILE COPY ",10)){bool copy=!strncmp(line,"FILE COPY ",10);const char* args=line+(copy?10:12);const char* separator=strchr(args,'|');if(separator&&size_t(separator-args)<sizeof(path)){memcpy(path,args,separator-args);path[separator-args]=0;ok=copy?fileCopy(path,separator+1):fileRename(path,separator+1);}}
 else if(!strcmp(line,"BACKUP SPIFFS"))ok=fileBackupSpiffs();
 else if(sscanf(line,"UPLOAD BEGIN %u %x %150[^\n]",&size,&hash,path)==3){ok=begin(path,size,1);if(ok)expectedCrc=hash;}
 else if(!strncmp(line,"UPLOAD DATA ",12)){
   const char* arg=line+12;char* end;offset=strtoul(arg,&end,10);if(operation==1&&end!=arg&&*end==' '&&offset==done){const char* hex=end+1;unsigned n=strlen(hex);ok=n&&n<=192&&n%2==0;
     auto nib=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};for(unsigned i=0;ok&&i<n;i+=2){int a=nib(hex[i]),b=nib(hex[i+1]);if(a<0||b<0)ok=false;else buffer[i/2]=(a<<4)|b;}if(ok)ok=append(buffer,n/2);}
   if(!ok&&operation==1){fileCancel();snprintf(fileMessage,sizeof(fileMessage),"Upload aborted: offset / data");}
 }
 else if(!strcmp(line,"UPLOAD END"))ok=operation==1&&finish();
 else return false;
 Serial.printf("HP_FILE %s done=%lu path=%s\n",ok?"OK":"FAILED",(unsigned long)done,fileOutput);return true;
}
}
