# Referee System

本仓库包含裁判系统的两个独立嵌入式工程。它们在仓库中并列保存，但不是一个统一的 CMake 工程，必须分别配置、编译、下载和调试。

## 工程结构

```text
referee-system/
├── armor_subboard/   # 装甲子板，STM32G030F6P6
└── referee_main/     # 裁判系统主控工程；当前目标位于 board/105_rc/
```

## 装甲子板

工程目录：`armor_subboard/`

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

生成的程序文件位于 `armor_subboard/build/Debug/`。J-Link/Ozone 配置保存在该工程自己的 `.vscode/` 和 `flash.jlink` 中。

## 裁判系统主控

当前主控目标目录：`referee_main/board/105_rc/`

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

主控工程继续使用自身的 ThreadX、BSP、模块和应用层结构，不依赖装甲子板工程的 CMake 配置。

## 开发约定

- 两个工程分别维护 CubeMX 配置、HAL、源代码和调试配置。
- 修改某一个工程时，只在对应目录内提交，避免把两个工程误合并成一个构建目标。
- 构建目录、编译产物和本地调试缓存不提交到仓库。
