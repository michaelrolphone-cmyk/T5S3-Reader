/* Real MSC provider + patched TinyUSB + real SD provider/FatFs. Transport fake
 * completes only USB packets; no filesystem or export behavior is replaced. */
#define main unused_msc_fixture_main
#include "owner_test.c"
#undef main
extern const risc_storage_volume_api_v1_export_prepare *directory_sd_start(void);
extern void directory_sd_frozen(void),directory_sd_finish(void);
extern unsigned directory_sd_reads(void),directory_sd_writes(void);
extern uint32_t directory_sd_hash(void);
static const risc_storage_volume_api_v1_export_prepare *real_volume;
static int32_t real_begin(void*c,uint64_t*t) {int32_t r=real_volume->begin_prepare(c,t);assert(r==RISC_STORAGE_EXPORT_PREPARING);sd_owned=true;mark('M');return r;}
static int32_t real_end(void*c,uint64_t t) {assert(!usb_live);int32_t r=real_volume->base.export_end(c,t);if(r==0){sd_owned=false;mark('E');}return r;}
static uint32_t le32(const uint8_t*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t le16(const uint8_t*p){return p[0]|((uint16_t)p[1]<<8);}
static unsigned commands,read_sectors;
static void host_read(uint32_t lba,uint16_t count,uint8_t*out) {
 command(0x28,(uint32_t)count*512,true,lba,count,0);++commands;
 for(unsigned i=0;i<count;++i){
  assert(ep[1][1].pending && !ep[1][1].stalled && ep[1][1].length==512);
  memcpy(out+i*512,ep[1][1].buffer,512);complete(0x81,NULL,512);now_ms+=2;assert(poll()==0);++read_sectors;
 }
 assert(csw(true)==0);directory_sd_frozen();
}
int main(void) {
 real_volume=directory_sd_start();assert(real_volume);volume=*real_volume;
 volume.begin_prepare=real_begin;volume.base.export_end=real_end;
 connect_provider();configure();directory_sd_frozen();
 const uint32_t hash=directory_sd_hash();const unsigned writes=directory_sd_writes(),reads=directory_sd_reads();
 command(0x25,8,true,0,0,0);assert(ep[1][1].pending && ep[1][1].length==8);
 const uint8_t*c=ep[1][1].buffer;assert(c[0]==0 && c[1]==1 && c[2]==255 && c[3]==255 && c[6]==2 && c[7]==0);
 complete(0x81,NULL,8);assert(poll()==0);assert(csw(true)==0);
 uint8_t sector[512],batch[128*512];host_read(0,1,sector);assert(sector[510]==0x55 && sector[511]==0xaa);
 uint32_t base=le32(sector+454);assert(base==2048);host_read(base,1,sector);
 assert(le16(sector+11)==512);uint32_t spc=sector[13],fat=base+le16(sector+14),data=fat+sector[16]*le32(sector+36),cluster=le32(sector+44);
 unsigned entries=0,long_entries=0,chain=0;bool done=false;
 while(!done && cluster<0xffffff8u) {
  assert(cluster>=2 && ++chain<1024);host_read(data+(cluster-2)*spc,(uint16_t)spc,batch);
  for(unsigned i=0;i<spc*512;i+=32){if(!batch[i]){done=true;break;}if(batch[i]==0xe5)continue;if(batch[i+11]==15){++long_entries;continue;}if(!(batch[i+11]&8))++entries;}
  if(!done){host_read(fat+cluster/128,1,sector);cluster=le32(sector+(cluster%128)*4)&0xfffffff;}
 }
 assert(done && entries==322 && long_entries>=640 && chain>32);
 /* Typical read-ahead: 64 KiB data command, plus metadata beyond 16-bit LBA. */
 host_read(data,128,batch);host_read(100000,1,sector);host_read(131071,1,sector);
 assert(directory_sd_reads()-reads==read_sectors && directory_sd_writes()==writes && directory_sd_hash()==hash);
 assert(api->end(NULL,token,RISC_USB_MSC_END_CABLE_REMOVED)==0);assert(t5_driver_get(2)->quiesce());directory_sd_finish();
 printf("PASS real FAT32 directory entries=%u LFN=%u clusters=%u READ10=%u sectors=%u; exclusive export; full-card hash unchanged; checked remount\n",entries,long_entries,chain,commands,read_sectors);
}
