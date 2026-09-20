# RoboMaster 裁判系统装甲受击小闭环维护说明

> 本文对应 `D:\refer\referee-system` 当前实现，重点说明装甲受击算法、板间通信协议、计数同步和系统复位。早期任务笔记中的“事件包 + 3 次重发 + 300 ms 主控去重”已被本文的累计计数协议取代。

## 1. 工程结构与目标

仓库内包含两个并列且可独立构建的固件工程，不能合并：

```text
D:\refer\referee-system
├─ armor_subboard\    STM32G030F6P6 装甲子板
└─ referee_main\      STM32F105RCT6 + ThreadX 裁判主控
```

数据闭环为：

```text
DX 确认受击、ADC 区分弹丸大小
  -> 子板累计小弹/大弹次数
  -> UART 发送累计计数状态帧
  -> 主控以物理 UART 端口确定 armor_id
  -> 对新旧计数做 16 位差值
  -> 小弹扣 2 HP，大弹扣 20 HP
  -> 更新 OLED/LED，并向车端发送 RoboMaster 裁判帧
```

四路物理映射：

| 主控串口 | 权威 armor_id |
|---|---:|
| USART2 | 0 |
| USART3 | 1 |
| UART4 | 2 |
| UART5 | 3 |

子板上报帧中的 `armor_id` 只用于观察；主控必须以接收该帧的物理端口号为准，防止配置错误导致装甲位置串位。

## 2. 装甲子板算法

关键文件：

- `armor_subboard/Core/Src/armor_detector.c`：基线、DX 边沿、冷却、弹丸分类。
- `armor_subboard/Core/Src/armor_app.c`：计数、复位、状态上报、LED 保持。
- `armor_subboard/Core/Src/armor_link.c`：解析主控 `0xC1` 配置/保活帧。
- `armor_subboard/Core/Src/armor_protocol.c`：状态帧组包和异步串口发送。
- `armor_subboard/Core/Inc/armor_config.h`：现场需要调节的宏。

### 2.1 基线和受击确认

上电后的前 `ARMOR_BASELINE_TIME_MS = 500 ms` 不判定受击，使用最多 64 个循环样本的滑动平均作为 ADC 基线。

受击必须由 DX 从非有效电平变为有效电平确认。ADC 超阈值本身不会独立产生受击，因此模拟噪声不会直接增加计数。当前有效电平由下列宏控制：

```c
#define ARMOR_DX_ACTIVE_HIGH 1U
```

一次有效受击后进入 50 ms 冷却：

```c
#define ARMOR_HIT_COOLDOWN_MS 50U
```

冷却期间新的 DX 边沿被忽略。这是子板唯一的时间去重；主控不再使用受击时间窗口去重。

### 2.2 大小弹丸分类

在 DX 有效边沿到来时读取当次 ADC 值：

```text
adc_raw > adc_baseline + ARMOR_BIG_HIT_ADC_THRESHOLD  => 大弹
否则                                                    => 小弹
```

现场标定只需修改：

```c
#define ARMOR_BIG_HIT_ADC_THRESHOLD 1000U
```

该值目前是占位初值，必须结合真实传感器、装甲结构和场地弹丸实测。建议记录空闲基线、小弹峰值和大弹峰值后，把阈值放在两类峰值分布之间，并留出噪声裕量。

### 2.3 累计计数和发送策略

子板维护两个 `uint16_t` 计数：

- `small_hit_count`：小弹累计次数；
- `big_hit_count`：大弹累计次数。

每次受击只增加其中一个计数，并立即发送一次最新状态；平时每 50 ms 发送一次相同状态作为心跳。旧的“同一受击重发 3 包、间隔 5 ms”已经删除。即使某一状态包丢失，下一包的累计计数仍能让主控恢复期间发生的受击数。

计数自然允许 16 位回绕。例如上次为 `65534`、本次为 `1`，无符号差值为 `3`。

## 3. 板间通信协议 V0.2

两种帧均固定 8 字节、`115200 8N1`，校验为前 7 字节逐字节相加取低 8 位。

### 3.1 子板到主控：累计状态帧

```text
字节 0   0xA5
字节 1   普通状态：armor_id；复位确认：bit7 为 RESET_ACK、低 7 位回显 `0xC1` config sequence（主控不以它确定位置）
字节 2   small_hit_count low
字节 3   small_hit_count high
字节 4   big_hit_count low
字节 5   big_hit_count high
字节 6   reset_epoch
字节 7   sum8(bytes 0..6)
```

`RESET_ACK` 只在子板真正执行了本轮计数清零后置位，并回显触发该次复位的 `config sequence`。主控只有同时收到匹配的 `reset_epoch`、ACK 位和序号，才会撤销对应端口的 RESET 请求；因此复位前滞留的旧状态包不会仅凭同 epoch 被误当成复位确认。RESET 配置撤销后，子板清除 ACK 位。

16 位字段均为小端。

### 3.2 主控到子板：`0xC1` 配置/保活/复位帧

```text
字节 0   0xA5
字节 1   0xC1
字节 2   team：0 红，1 蓝
字节 3   port_id / 下发给该物理端口的 armor_id
字节 4   mode flags
字节 5   目标 reset_epoch
字节 6   config sequence
字节 7   sum8(bytes 0..6)
```

`mode flags`：

```c
0x01  ENABLE：合法配置/保活，必须置位
0x02  RESET_COUNTERS：要求子板把大小弹计数清零
```

主控每 100 ms 对四路端口发送一次 `0xC1`。子板超过 500 ms 未收到合法帧会执行掉线灯效并进入离线模式；在线时阵营和 ID 由主控控制，离线时 PB6/PB7 可选择红蓝方。

## 4. 主控计数同步与扣血

关键文件：

- `referee_main/apps/referee_main/single_board/armor_link.c`：8 字节流解析和校验。
- `robot_control.c`：线程、`0xC1` 调度、状态更新和复位入口。
- `armor_counter.c/.h`：每端口基线、差值、安全重同步、16 位回绕和复位 ACK 状态机。
- `referee_state.c`：受互斥锁保护的 HP、阵营、在线状态、总受击次数。
- `referee_config.h`：伤害和异常差值上限。

### 4.1 正常差值

每个物理端口独立保存上次大小弹计数：

```c
small_delta = (uint16_t)(new_small - old_small);
big_delta   = (uint16_t)(new_big - old_big);
damage      = small_delta * 2U + big_delta * 20U;
```

首次收到某端口状态时只建立基线，不扣血。这能避免主控重启后把仍在运行的子板历史计数全部重新扣除。

收到完全相同的状态帧时差值为 0，所以重复包不会重复扣血；丢失中间包时，下一包差值包含这段时间的全部受击。

一次发现非零差值时，主控更新 HP 和总受击次数，并发送一次 `0x0206 hurt_status`；周期性的 `0x0201 robot_status` 和 `0x0003 robot_hp` 会携带最新 HP。

### 4.2 异常差值保护

默认单个状态间隔允许的最大增量：

```c
#define REFEREE_MAIN_MAX_SMALL_HIT_DELTA 50U
#define REFEREE_MAIN_MAX_BIG_HIT_DELTA   10U
```

超过上限通常表示子板重启、线路错帧后碰巧通过简单校验、计数状态不一致或维护时协议版本不匹配。主控会接受新值作为下一次基线并增加 `resync` 诊断计数，但本次不扣血，防止一次异常将 HP 清零。

这两个上限不是射速判定参数；如果现场确实可能在 50 ms 状态周期内超过它们，需要同步调整并重新验证异常保护范围。

## 5. 整体复位和 reset epoch

复位不能只清主控 HP，否则子板下一包仍带旧累计计数，会再次扣血。当前复位流程为：

1. UI 设置菜单确认 RESET，或 UI 未激活时按 KEY2；
2. `referee_control_reset_system()` 恢复主控 HP/阵营/统计；
3. 主控把全局 `reset_epoch` 加 1，并把四个端口标为 reset pending；
4. pending 端口的 `0xC1` 持续置位 `RESET_COUNTERS`，并携带目标 epoch；主控记录该帧的 config sequence；
5. 子板在 RESET 标志从未激活变为激活时执行一次清零；即使目标 epoch 与已应用 epoch 相同，也会执行这次边沿触发的复位。若目标 epoch 改变，同样执行复位，并立即回报新 epoch；
6. 主控在收到匹配 epoch、RESET_ACK 和 config sequence 前忽略该端口的伤害；确认后建立新基线并清除该端口 pending；
7. 后续 `0xC1` 对该端口恢复为仅 `ENABLE`。

因此，同一复位帧重复到达不会让子板反复清零；某路子板暂时离线也不会阻塞其他端口。该路恢复通信后仍会收到复位请求，完成 epoch 对齐后才重新参与扣血。

若主控在非 pending 状态看到 epoch 突然改变，说明子板可能独立重启或状态被改变；主控只重建该端口基线，不扣本包伤害，并增加 `resync`。

## 6. 主控 ThreadX、裁判输出和 UI

主要线程：

| 线程 | 职责 |
|---|---|
| `armor_rx_thread` | 轮询四路 UART、解析累计状态并直接完成差值同步 |
| `referee_tx_thread` | 调度 `0xC1`、`0x0201`、`0x0202`、`0x0208`、`0x0003`、`0x0001` |
| `referee_watchdog_thread` | 周期喂 IWDG |
| UI 线程 | SSD1306/KK_UI 页面、按键和设置确认框 |

旧的 `hit_event_thread` 和受击事件队列已经删除，因为状态帧本身就是可恢复的累计日志。

OLED 为 128x64 SSD1306，I2C1，当前驱动 7 位地址为 `0x3C`。KEY3/KEY4 在屏幕关闭时均可唤醒 UI；列表中 KEY1/KEY2 上下移动，确认键进入；KEY4 在子页面返回首页，在首页退出 UI。设置菜单中的切换阵营和系统复位都有确认框与 toast。

## 7. 构建与测试

装甲子板：

```powershell
cmake --build D:\refer\referee-system\armor_subboard\build\Debug --parallel 4
```

产物：

```text
D:\refer\referee-system\armor_subboard\build\Debug\siglebroad_1.elf
```

主控：

```powershell
cmake --build D:\refer\referee-system\referee_main\board\105_rc\build\Debug --parallel 4
```

产物：

```text
D:\refer\referee-system\referee_main\board\105_rc\build\Debug\base.elf
```

协议算法测试：

```powershell
python D:\refer\referee-system\referee_main\apps\referee_main\tests\test_protocol.py
```

C 流解析器主机测试可用 MinGW GCC 编译：

```powershell
gcc referee_main/apps/referee_main/tests/test_armor_link.c `
     referee_main/apps/referee_main/single_board/armor_link.c `
     -I referee_main/apps/referee_main/single_board `
     -I referee_main/modules -std=c11 -Wall -Wextra -Werror `
     -o $env:TEMP\referee_test_armor_link.exe
& $env:TEMP\referee_test_armor_link.exe
```

累计计数、回绕、异常重同步和复位 ACK 状态机测试：

```powershell
gcc referee_main/apps/referee_main/tests/test_armor_counter.c `
     referee_main/apps/referee_main/single_board/armor_counter.c `
     -I referee_main/apps/referee_main/single_board `
     -std=c11 -Wall -Wextra -Werror `
     -o $env:TEMP\referee_test_armor_counter.exe
& $env:TEMP\referee_test_armor_counter.exe
```

每次改协议必须同时完成：两端固件构建、Python 帧布局测试、C 流解析器测试和 C 计数状态机测试。只烧一端的新协议会导致闭环失效。

## 8. 调试与现场标定

建议顺序：

1. 只接一块子板，确认主控对应物理端口在线；
2. 在 Ozone 观察 `armor_adc_raw_debug` 和 `armor_adc_voltage_mv_debug`；
3. 记录空闲、小弹、大弹 ADC 数据，修改 `ARMOR_BIG_HIT_ADC_THRESHOLD`；
4. 连续敲击，核对子板大小弹累计计数每次只增加 1；
5. 人为丢掉若干串口包，确认主控下一包能按累计差值补回；
6. 执行系统复位，确认四路回报新 epoch，复位前计数不会重新扣血；
7. 分别验证小弹扣 2、大弹扣 20，以及 HP 下限为 0；
8. 再接入其余三路并核对 UART 物理位置。

常见问题：

- 一击多次计数：先观察子板计数；若子板已多加，检查 DX 波形和 50 ms 冷却，而不是在主控增加时间去重。
- 大小弹判断错误：调 `ARMOR_BIG_HIT_ADC_THRESHOLD`，并确认 DX 边沿发生时 ADC 已到峰值；如果硬件存在明显相位差，后续应增加短峰值窗口，而不是让 ADC 独立触发受击。
- 主控在线但不扣血：首包只建基线；复位后还要等对应端口回报目标 epoch。
- `resync` 增加：检查子板是否复位、协议版本是否一致、串口干扰和异常差值上限。
- DAP/SWD 初始化失败：优先排查供电、SWDIO、SWCLK、NRST、BOOT0 和焊接，不先怀疑业务协议。

## 9. 生成代码和维护边界

- `board/105_rc/Core/*` 与子板 CubeMX 生成文件可能在重新生成时被覆盖。
- 当前业务代码位于主控 `apps/referee_main/single_board` 和子板独立的 `armor_*.c/.h`，尽量不要把业务逻辑塞入 CubeMX 生成函数。
- 修改 `.ioc` 后先审查生成 diff，特别是时钟、UART、I2C、IWDG、GPIO 和中断回调。
- 不启用 MOTOR/CAN 旧模块；当前闭环只走 UART。
- 协议字段、伤害值、复位语义或计数宽度变化时，两端代码和本文必须同一 commit 更新。
- 相对独立的问题单独 commit，提交前至少运行 `git diff --check` 和本节全部构建/测试。

## 10. 当前仍需实测确认

- `ARMOR_BIG_HIT_ADC_THRESHOLD` 的最终值；
- DX 有效电平和真实脉宽，50 ms 冷却是否兼顾去抖与最高有效射速；
- DX 边沿与 ADC 峰值是否同步，是否需要短时峰值保持；
- 四路串口与车体前后左右装甲的最终物理映射；
- OLED 实物地址是否确为 `0x3C`；
- 简单 `sum8` 在现场电磁环境下是否足够，后续可升级 CRC8/CRC16 和协议版本字段。
