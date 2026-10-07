#pragma once
// Reuse the existing bounded FatFs parser, compiling only open/read/close/mount
// for boot. Private symbol names avoid IDF/ELF filesystem state interposition.
#define RISCRTE_SD_BOOTSTRAP 1
#define f_open risc_boot_f_open
#define f_read risc_boot_f_read
#define f_close risc_boot_f_close
#define f_mount risc_boot_f_mount
#define ff_wtoupper risc_boot_ff_wtoupper
#define ff_uni2oem risc_boot_ff_uni2oem
#define ff_oem2uni risc_boot_ff_oem2uni
#define disk_initialize risc_boot_disk_initialize
#define disk_status risc_boot_disk_status
#define disk_read risc_boot_disk_read
#define disk_ioctl risc_boot_disk_ioctl
#define risc_fatfs_checkpoint risc_boot_fatfs_checkpoint
#include "../../Drivers/storage_fatfs/fatfs/ff.h"
#include "../../Drivers/storage_fatfs/fatfs/diskio.h"
