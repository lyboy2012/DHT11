/*
 * 全工程的可调配置 —— 引脚、周期、阈值集中在这里。
 *
 * 其它模块只 #include 本文件，不各自硬编码，所以换引脚/调节奏只改这一处。
 * 用 constexpr 而不是 static const：它们在头文件里被多个 .cpp 包含时
 * 各自是编译期常量，不占 RAM、也不会产生重复定义。
 */
#pragma once

#include <Arduino.h>

// ======== DHT11 温湿度传感器 ========
constexpr uint8_t DHT_PIN = 4;  // DHT11 DATA 脚
// 传感器型号在 dht_sensor.cpp 里写死成 DHTesp::DHT11；换成 DHT22/AM2302 时同步改掉，
// 否则解析规则不同会一直读到 CHECKSUM。

constexpr uint32_t READ_INTERVAL_MS = 5000;  // 采样间隔下限(实际取 max(它, 传感器物理下限))
constexpr uint32_t STARTUP_DELAY_MS = 1200;  // 上电后 DHT11 需要约 1s 稳定，首次采样往后推

// ======== 板载状态 LED ========
constexpr uint8_t LED_PIN = 2;          // 多数 ESP32 开发板的板载 LED 就在 GPIO2
constexpr bool LED_ACTIVE_HIGH = true;  // 高电平点亮; 板载 LED 亮灭相反就改为 false

constexpr uint32_t LED_OK_FLASH_MS = 50;    // 成功读数: 亮 50ms
constexpr uint32_t LED_FAIL_HALF_MS = 100;  // 失败告警: 亮100/灭100
constexpr uint8_t FAIL_BLINK_AFTER = 3;     // 连续失败几次开始快闪

// ======== OLED (SSD1306 128x64, I2C) ========
constexpr uint8_t OLED_SDA_PIN = 21;       // ESP32 经典默认 I2C 数据脚
constexpr uint8_t OLED_SCL_PIN = 22;       // ESP32 经典默认 I2C 时钟脚
constexpr uint32_t OLED_I2C_HZ = 400000;   // SSD1306 支持 400kHz
constexpr uint32_t OLED_RETRY_MS = 30000;  // 没认到屏幕时每 30s 重试一次初始化
constexpr uint8_t OLED_WIDTH = 128;        // 屏宽，右对齐小字时要用

// ======== 运行统计 ========
constexpr uint32_t STATS_INTERVAL_MS = 60000;  // 汇总行间隔: 60s
