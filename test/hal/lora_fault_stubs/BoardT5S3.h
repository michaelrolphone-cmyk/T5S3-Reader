#pragma once
#define T5S3_LORA_CS 46
#define T5S3_LORA_IRQ 10
#define T5S3_LORA_RST 1
#define T5S3_LORA_BUSY 47
extern unsigned hardwareCalls;
namespace BoardT5S3 { inline void prepareSdBus() { ++hardwareCalls; } }
