# 小裁判主控业务线配置
# 第一阶段只验证 CubeMX + ThreadX + app 入口，不启用旧机器人控制模块。

include(${CMAKE_CURRENT_LIST_DIR}/../../modules/module_config.cmake)

# 内置模块第一阶段全关，避免 MOTOR/REMOTE/REFEREE 等拉入 CAN、遥控或车端裁判接收逻辑。
set(MODULES_SINGLE)
set(MODULES_GIMBAL)
set(MODULES_CHASSIS)

# 即使后续启用 OFFLINE，F105 调试阶段也先关闭看门狗和蜂鸣器。
set(OFFLINE_WATCHDOG_ENABLE 0)
set(OFFLINE_BEEP_ENABLE 0)
