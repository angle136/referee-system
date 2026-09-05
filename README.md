# referee-system

RoboMaster 裁判系统替代方案仓库，按固件目标拆分：装甲子板和裁判系统主控分开维护，共用协议放在 `shared/`。

## 固件目录

| 目录 | MCU | 作用 |
|---|---|---|
| `firmware/armor_subboard_g030/` | `STM32G030F6P6` | 单块装甲子板，负责采集 `AX`、读取 `DX`，通过 `USART2` 上报主控 |
| `firmware/referee_main_f105/` | `STM32F105RCT6` | 裁判系统主控，后续负责汇总四块装甲板并向车端发送裁判串口帧 |

## 共用目录

| 目录 | 作用 |
|---|---|
| `shared/armor_link_protocol/` | 装甲子板到主控的 UART 小协议 |
| `shared/referee_protocol/` | 主控到车端的 RoboMaster 裁判串口协议 |
| `hardware/` | epro、引脚表、接线关系和硬件复核记录 |
| `tools/` | 烧录、调试和辅助脚本 |

## 当前阶段

当前已完成 `firmware/armor_subboard_g030/` 的 CubeMX + CMake 工程、J-Link/Ozone 调试准备，以及 `USART2 + ADC1 + DX + LED` 冒烟测试。
