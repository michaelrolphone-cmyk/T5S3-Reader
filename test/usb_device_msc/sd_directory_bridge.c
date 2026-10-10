/* Real X4 SD 0.2.13 and FatFs. Only physical SDMMC/GPIO boundaries are fake. */
#define main unused_sd_fixture_main
#define t5_driver_get x4_sd_driver_get
#include "sd_test.c"
#undef main
#undef t5_driver_get
static risc_gpio_sdmmc_api_v1 native_extension;
const risc_storage_volume_api_v1_export_prepare *directory_sd_start(void) {
 format(true);
 native_extension.base=fixture_gpio;native_extension.base.struct_size=sizeof(native_extension);
 native_extension.sdmmc_tag=RISC_GPIO_SDMMC_TAG_V1;native_extension.sdmmc_version=1;
 native_extension.sdmmc=(risc_sdmmc_host_api_v1){1,sizeof(native_extension.sdmmc),NULL,native_open,native_read,native_write,native_sync,native_close};
 deps[1].api=&native_extension.base;assert(START());assert(ready(NULL));
 for(unsigned i=0;i<320;++i) {
  char path[80];snprintf(path,sizeof(path),"/Directory workload file %03u.txt",i);
  uint32_t h=file_open_write(NULL,path);assert(h);
  uint8_t bytes[1024];for(unsigned j=0;j<sizeof(bytes);++j)bytes[j]=(uint8_t)(i*11u+j);
  assert(file_write(NULL,h,bytes,sizeof(bytes))==sizeof(bytes));assert(file_close(NULL,h,true));
 }
 assert(mkdir_path(NULL,"/Games"));assert(mkdir_path(NULL,"/Books"));
 /* Populate the real published extension exactly as production discovery. */
 const risc_driver_v2 *d=x4_sd_driver_get(2);assert(d);
 return risc_storage_volume_export_prepare(d->capability);
}
void directory_sd_frozen(void) {assert(export_state==EXPORT_HOST && !mounted && native_live);assert_export_frozen();}
unsigned directory_sd_reads(void) {return native_reads;}
unsigned directory_sd_writes(void) {return native_writes;}
uint32_t directory_sd_hash(void) {uint32_t h=2166136261u;for(size_t i=0;i<(size_t)card_sectors*512;++i)h=(h^card_image[i])*16777619u;return h;}
void directory_sd_finish(void) {
 assert(export_state==EXPORT_LOCAL && mounted && native_live);uint64_t size=0;
 uint32_t h=file_open_read(NULL,"/Directory workload file 319.txt",&size);assert(h && size==1024);
 uint8_t bytes[1024];assert(file_read(NULL,h,bytes,sizeof(bytes))==sizeof(bytes));assert(file_close(NULL,h,true));
 for(unsigned j=0;j<sizeof(bytes);++j)assert(bytes[j]==(uint8_t)(319u*11u+j));
 verify_cleanup();free(card_image);
}
