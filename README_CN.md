# RiscRTE

[![RiscRTE PlatformIO Build](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/workflows/platformio-build.yml/badge.svg)](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/workflows/platformio-build.yml)

[English](README.md) | 中文

**RiscRTE — RISC Runtime Environment** 是面向资源受限 RISC 系统的模块化嵌入式运行环境。当前硬件目标为 **LilyGo T5 ePaper S3 / T5S3 Pro** 与 **LilyGo EPD47 ESP32-S3**。

RiscRTE 不再定义为单一电子书阅读器固件。电子书阅读是平台上的一个应用领域；平台还在发展可动态加载的 ELF 应用、硬件驱动、后台服务、基于 capability 的硬件访问、资源所有权和框架级 UI/runtime。

本项目的电子书阅读能力源自并继续维护 **CrossPoint Reader** 的大量代码和设计。**CrossPoint** 在本仓库中用于电子书阅读子系统、其相关功能，以及尚未迁移的兼容实现标识；整个固件、运行时、构建、版本与发布产物统一使用 **RiscRTE** 名称。

## 致谢

感谢 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 项目。RiscRTE 中的阅读器子系统继承了 CrossPoint 的活动页面架构、阅读器逻辑、设置系统、SD 卡缓存、Web 文件传输等大量基础工作。

本仓库不是 CrossPoint 官方项目，也不隶属于 LilyGo。

## 使用的设备

当前目标设备：

| 编译目标 | 硬件 | 状态 |
| --- | --- | --- |
| `t5s3-pro` | [LilyGo T5 ePaper S3](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | 主要目标 |
| `lilygo-epd47-s3` | [LilyGo EPD47 ESP32-S3](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3) | 支持的编译目标，实机验收仍在继续 |

两种目标均使用 ESP32-S3、960 x 540 墨水屏、GT911 触摸与 microSD。旧版 ESP32-WROVER EPD47 不在支持范围内。EPD47 没有前光和 BQ 电量计，使用 GPIO21 唤醒，且不支持触摸唤醒。

| ![](./docs/README_img/t5s3.png) | ![](./docs/README_img/t5s31.png) |
| --- | --- |

## 主要功能

RiscRTE 当前包含或正在围绕以下能力构建：

- 从 SD 卡动态加载和卸载原生 `.elf` 应用；
- 应用 manifest、Apps 启动器与 App Store；
- 可独立打包的 ELF 硬件驱动与系统服务；
- capability 驱动的硬件访问与资源所有权；
- Wi-Fi、存储、电源、输入、UI 与导航等平台能力；
- CrossPoint Reader 子系统，包括 EPUB、TXT、Markdown、XTC 阅读、阅读进度、插图、字体、排版与书籍管理；
- BMP 图片查看、Wi-Fi 文件上传、设置和低功耗管理。

## 准备工作

你需要：

- 受支持的 LilyGo T5S3 或 EPD47 ESP32-S3 设备
- microSD 卡
- USB-C 数据线
- Python 3
- PlatformIO Core，或 VS Code + PlatformIO 插件

安装 PlatformIO Core：

```bash
python -m pip install platformio==6.1.19
```

获取源码后进入仓库根目录：

```bash
git clone <仓库地址>
cd T5S3-Reader
```

## 如何下载程序到设备

### 方式一：使用 LILYGO Spark

LILYGO Spark 中可能仍存在早期以 `corsspoint_lilygo_t5s3_e_paper` 发布的条目。这个名称仅作为尚未迁移的外部/历史实现引用保留，不是当前 RiscRTE 的产品或发布命名规范。

### 方式二：使用 PlatformIO 下载

1. 将设备通过 USB-C 连接电脑。
2. 在仓库根目录编译固件：

```bash
pio run -e t5s3-pro
# 或
pio run -e lilygo-epd47-s3
```

3. 下载到设备：

```bash
pio run -e t5s3-pro -t upload
# 或
pio run -e lilygo-epd47-s3 -t upload
```

4. 如果无法进入下载模式，可以按住设备的 BOOT 键，再按一下 RESET，或按住 BOOT 后重新插入 USB，然后重新执行上传命令。

5. 如需查看串口日志：

```bash
pio device monitor -b 115200
```

### 方式三：使用 flash_download_tools 手动刷入

1. 下载 [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html)
2. 选择 `esp32s3`。
3. 选择与板型严格对应的完整合并镜像，按镜像说明设置下载地址，然后选择串口并点击 `START`。PlatformIO 生成的 `.pio/build/<环境>/firmware.bin` 是应用镜像，不能作为地址 `0x0` 的完整恢复镜像使用。

## RiscRTE 固件与发布产物

当前 CI 的板型限定产物使用 RiscRTE 名称，例如：

- `riscrte-t5s3-pro.bin`
- `riscrte-t5s3-pro-merged.bin`
- `riscrte-lilygo-epd47-s3.bin`
- `riscrte-lilygo-epd47-s3-merged.bin`

正式 T5S3 版本化发布产物为：

```text
riscrte_lilygo_t5s3_<version>-app.bin
riscrte_lilygo_t5s3_<version>.bin
riscrte_lilygo_t5s3_<version>.elf
```

其中 `-app.bin` 是 OTA/SD 应用镜像；无 `-app` 的版本化 `.bin` 是从 `0x0` 写入的合并 USB 镜像；`.elf` 是用于调试符号的非刷写文件。

`firmware-t5s3-pro.bin` 暂时保留为 OTA/SD 消费方使用的兼容文件名。历史版本中以 `corsspoint_` 开头的文件名仅作为已发布旧产物的引用保留，新版本不得继续使用该命名。

通过 SD 卡升级时，固件还会检查内嵌的板型标记并拒绝另一种板子的固件。如果应用升级失败或中断，请按住开发板的 BOOT 键并按 RESET（或重新连接 USB），释放 BOOT 后通过 PlatformIO 上传正确的环境。

EPD47 目标链接了 GPL-3.0 的 LilyGo 显示驱动。分发 EPD47 二进制前请阅读 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## SD 卡和电子书

将电子书直接放到 SD 卡根目录，或按自己的习惯创建文件夹分类。

推荐结构：

```text
/
  Apps/
  Drivers/
  Services/
  Books/
    book.epub
    novel.txt
  .fonts/
  .sleep/
    sleep.bmp
```

当前阅读器兼容实现仍会在 SD 卡上使用 `.crosspoint/` 保存设置、阅读进度、缓存和封面缩略图。该路径是 CrossPoint Reader 的遗留兼容状态，不能只因品牌重命名而破坏已有用户数据；后续如迁移必须提供明确的数据迁移策略。

### 如何添加字体

在 `SD_fonts/` 文件夹中提供了一些字体。将字体文件复制到 SD 卡的 `.fonts/` 文件夹，然后在 `Settings -> Reader -> Reader Font Family` 中选择字体。`SourceHanSansSC` 包含中文字符。

更多英文字体生成参考文档：[sd-card-fonts](./docs/sd-card-fonts.md)

更多中文字体生成参考文档：[中文字体使用说明](./docs/中文字体使用说明.md)

## 设备如何操作

### 基础按键

| 按键 | 功能 |
| --- | --- |
| BOOT | 短按：上一项 / 阅读时上一页 |
| IO48 | 短按：下一项 / 阅读时下一页 |
| BOOT | 长按：确认 / 打开 |
| IO48 | 长按：关机 |
| PWR | 打开设备电源 |
| RTS | 复位 |
| HOME | 回到主页面 |

### 开机和关机

- 长按 `PWR` 键开机。
- 长按 `IO48` 键关机。
- 长时间无操作且未插 USB 时，设备会自动进入关机/低功耗状态。
- 如果设备无响应，可以按 RESET 后重新长按电源键启动。

### CrossPoint Reader 阅读功能

阅读器子系统支持文件浏览、最近阅读、EPUB/TXT/Markdown/XTC 阅读、书签、章节导航、字体和排版设置等。这里的 **CrossPoint Reader** 名称只指 RiscRTE 中的电子书阅读能力，不指整个平台。

## 说明

RiscRTE 仍在持续调整。架构与新平台功能以 `docs/RISCRTE_PLATFORM_SPEC.md` 和 `docs/PLATFORM_CAPABILITY_ROADMAP.md` 为准；阅读器特有行为可继续参考 CrossPoint Reader 相关文档。

再次感谢 CrossPoint Reader 项目和相关开源库作者。
