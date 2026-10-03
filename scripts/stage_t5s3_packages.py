#!/usr/bin/env python3
"""Stage T5 external storage packages; local files only, no provisioning."""
from stage_x4pro_packages import ROOT, stage
if __name__ == '__main__':
    stage(sources=['platform_clock_v1', 'spi_esp32s3', 't5s3_sd', 't5s3_frontlight'],
          board='t5s3-pro', output=ROOT/'dist/t5s3-independent-packages', omitted=())
