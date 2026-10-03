"""Pinned esptool entry with explicit baud before macOS CH340 port open."""
import sys
import esptool
import serial

def main():
    args=sys.argv[1:]
    port=args[args.index('--port')+1]
    with serial.Serial(port,115200,timeout=1,write_timeout=10,exclusive=True) as handle:
        esp=esptool.detect_chip(handle,115200,connect_attempts=1)
        if esp.CHIP_NAME!='ESP32-S3':
            raise RuntimeError('Unexpected chip; command refused')
        esptool.main(args,esp=esp)

if __name__=='__main__': main()
