#include "mp24_display.h"
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <initializer_list>

namespace mp24 {
static spi_device_handle_t panel=nullptr;
static uint8_t* strip=nullptr;
static bool fault=false;
static spi_transaction_t transaction;
uint32_t displayFrames=0,displayErrors=0;
static bool transfer(bool data,const void* bytes,size_t size){
  if(fault||!panel||!strip||!bytes||!size||size>160*8*2)return false;
  digitalWrite(6,data?HIGH:LOW);
  transaction={};transaction.length=size*8;
  if(size<=4){transaction.flags=SPI_TRANS_USE_TXDATA;memcpy(transaction.tx_data,bytes,size);}
  else {if(bytes!=strip)memcpy(strip,bytes,size);transaction.tx_buffer=strip;}
  // The supported SPI driver owns register sequencing and completion handling.
  esp_err_t result=spi_device_queue_trans(panel,&transaction,pdMS_TO_TICKS(50));
  spi_transaction_t* completed=nullptr;
  if(result==ESP_OK)result=spi_device_get_trans_result(panel,&completed,pdMS_TO_TICKS(50));
  if(result!=ESP_OK){
    // Preserve the descriptor/buffer and stop further use if completion times
    // out: the driver may still own them. USB remains available for recovery.
    fault=true;++displayErrors;Serial.printf("HP_DISPLAY_FAIL %s\n",esp_err_to_name(result));return false;
  }return completed==&transaction;
}
static bool command(uint8_t value,std::initializer_list<uint8_t> data={}){
  if(!transfer(false,&value,1))return false;
  return data.size()==0||transfer(true,data.begin(),data.size());
}
bool displayBegin(){
  pinMode(6,OUTPUT);pinMode(7,OUTPUT);digitalWrite(7,HIGH);delay(5);
  digitalWrite(7,LOW);delay(20);digitalWrite(7,HIGH);delay(150);
  spi_bus_config_t bus{};bus.mosi_io_num=5;bus.miso_io_num=-1;bus.sclk_io_num=4;
  bus.quadwp_io_num=bus.quadhd_io_num=-1;bus.max_transfer_sz=160*8*2;
  if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
  spi_device_interface_config_t device{};device.clock_speed_hz=26000000;
  device.mode=0;device.spics_io_num=-1;device.queue_size=1;
  if(spi_bus_add_device(SPI2_HOST,&device,&panel)!=ESP_OK)return false;
  strip=static_cast<uint8_t*>(heap_caps_malloc(160*8*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
  if(!strip)return false;
  if(!command(0x01))return false;delay(150);
  if(!command(0x11))return false;delay(500);
  // ST7735R register values derived from TFT_eSPI's MIT-licensed Rcmd tables.
  // Panel pins, no-CS wiring and MADCTL=0x60 are specific to the MP24 sample.
  if(!command(0xb1,{1,0x2c,0x2d})||!command(0xb2,{1,0x2c,0x2d})||
     !command(0xb3,{1,0x2c,0x2d,1,0x2c,0x2d})||!command(0xb4,{7})||
     !command(0xc0,{0xa2,2,0x84})||!command(0xc1,{0xc5})||
     !command(0xc2,{0x0a,0})||!command(0xc3,{0x8a,0x2a})||
     !command(0xc4,{0x8a,0xee})||!command(0xc5,{0x0e})||
     !command(0x20)||!command(0x36,{0x60})||!command(0x3a,{5})||
     !command(0xe0,{2,0x1c,7,0x12,0x37,0x32,0x29,0x2d,0x29,0x25,0x2b,0x39,0,1,3,0x10})||
     !command(0xe1,{3,0x1d,7,6,0x2e,0x2c,0x29,0x2d,0x2e,0x2e,0x37,0x3f,0,0,2,0x10})||
     !command(0x13))return false;
  delay(10);if(!command(0x29))return false;delay(100);return true;
}
bool displayPresent(const void* pixels){
  if(!strip||!pixels)return false;
  const auto* source=static_cast<const uint8_t*>(pixels);
  if(!command(0x2a,{0,0,0,159})||!command(0x2b,{0,0,0,127})||!command(0x2c))return false;
  // A small internal DMA staging buffer keeps the 40 KB framebuffer in PSRAM.
  for(unsigned offset=0;offset<160*128*2;offset+=160*8*2){
    memcpy(strip,source+offset,160*8*2);if(!transfer(true,strip,160*8*2))return false;
  }
  ++displayFrames;return true;
}
}
