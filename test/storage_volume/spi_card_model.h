#pragma once
/* SPI wire fixture for the actual T5 SD ELF. The same filesystem/HAL suite is
 * used for both transports; this only models byte-level card responses. */
#include <RiscSpiBusV1.h>
#include "../../Drivers/storage_fatfs/sd_protocol.h"
#include <deque>
namespace SpiCardFixture {
static bool claimed,active,selected,idle=true,writing,busy;
static uint64_t generation,session;
static uint32_t write_lba;
static std::vector<uint8_t> command,write_data;
static std::deque<uint8_t> responses;
static void respond(uint8_t value) { responses.push_back(value); }
static void execute() {
    assert(command.size()==6);
    assert(command[5]==static_cast<uint8_t>((risc_sd_crc7(command.data(),5)<<1)|1));
    uint32_t arg=0;for(unsigned i=1;i<5;++i)arg=(arg<<8)|command[i];
    switch(command[0]&63) {
      case 0: idle=true;busy=false;respond(1);break;
      case 8: respond(idle?1:0);respond(0);respond(0);respond(1);respond(0xaa);break;
      case 55: respond(idle?1:0);break;
      case 41: idle=false;respond(0);break;
      case 58: respond(0);respond(0xc0);respond(0xff);respond(0x80);respond(0);break;
      case 16: case 59: respond(0);break;
      case 17: {
        assert(arg<card_sectors);++card_reads;respond(0);respond(0xfe);
        const auto *data=card_image+(size_t)arg*512;
        for(unsigned i=0;i<512;++i)respond(data[i]);
        uint16_t crc=risc_sd_crc16(data,512);if(card_bad_crc)crc^=1;
        respond(crc>>8);respond(crc);break;
      }
      case 24: assert(arg<card_sectors);write_lba=arg;writing=true;write_data.clear();respond(0);break;
      case 13: respond(0);respond(0);break;
      default: assert(false && "Unexpected SPI SD command");
    }
    command.clear();
}
static uint8_t byte(uint8_t tx) {
    if(!selected)return 0xff;
    if(!responses.empty()) { uint8_t value=responses.front();responses.pop_front();return value; }
    if(busy && card_busy_forever)return 0;
    if(writing) {
        if(write_data.empty() && tx==0xff)return 0xff;
        write_data.push_back(tx);
        if(write_data.size()==515) {
            assert(write_data[0]==0xfe);
            assert(risc_sd_crc16(write_data.data()+1,512)==(uint16_t)((write_data[513]<<8)|write_data[514]));
            ++card_writes;
            if(!card_reject_write)std::memcpy(card_image+(size_t)write_lba*512,write_data.data()+1,512);
            respond(card_reject_write?0x0b:0x05);
            writing=false;busy=true;
        }
        return 0xff;
    }
    if(!command.empty() || (tx&0xc0)==0x40) {
        command.push_back(tx);if(command.size()==6)execute();
    }
    return 0xff;
}
static bool claim(void*,uint8_t cs,uint64_t*out) {
    if(claimed || cs!=12)return false;
    claimed=true;*out=1;return true;
}
static bool begin(void*,uint64_t token,uint32_t hz,bool select,uint64_t*out) {
    assert(hz==400000 || hz==25000000);
    if(!claimed || token!=1 || active)return false;
    active=true;selected=select;*out=session=++generation;return true;
}
static bool select(void*,uint64_t token,bool value) {
    if(!active || token!=session)return false;
    selected=value;return true;
}
static bool transfer(void*,uint64_t token,const uint8_t*tx,uint8_t*rx,size_t size) {
    if(!active || token!=session || !size || size>4096)return false;
    for(size_t i=0;i<size;++i) { const uint8_t value=byte(tx?tx[i]:0xff);if(rx)rx[i]=value; }
    return true;
}
static bool end(void*,uint64_t token) {
    if(!active || token!=session)return false;
    active=selected=writing=busy=false;command.clear();responses.clear();return true;
}
static bool release(void*,uint64_t token) {
    if(!claimed || token!=1 || active)return false;
    claimed=false;return true;
}
static const risc_spi_bus_api_v1 api={1,sizeof(api),nullptr,claim,begin,select,transfer,end,release};
}
