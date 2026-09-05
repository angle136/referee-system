# armor_subboard_g030

装甲子板固件，目标 MCU 为 `STM32G030F6P6`。

## 当前外设

- `USART2 PA2/PA3`：与裁判系统主控通信，`115200 8N1`
- `ADC1 IN0 / PA0`：采集 `AX`
- `PA1`：读取 `DX`
- `PA4 / PA5`：装甲灯测试输出
- `PA13 / PA14`：SWD 调试

## 构建

```powershell
cmake --preset Debug
cmake --build build/Debug
```

## J-Link / Ozone

Ozone 可直接加载：

```text
build/Debug/siglebroad_1.elf
```

命令行烧录脚本：

```powershell
& "C:\Code for environment\JLink_V914a\JLink.exe" -CommanderScript flash.jlink
```
