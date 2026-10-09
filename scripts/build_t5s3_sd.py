#!/usr/bin/env python3
"""Build T5 SD using the same storage/FatFs ELF recipe as the native SD port."""
from build_x4pro_drivers import build_one
if __name__ == '__main__':
    build_one('t5s3_sd')
