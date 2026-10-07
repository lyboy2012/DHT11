/*
 * 板载 LED 状态指示模块（硬件层，非阻塞）。
 *
 * 表现：
 *   正常      -> 每次成功读数短闪一下（LED_OK_FLASH_MS）
 *   连续失败  -> 快闪告警（LED_FAIL_HALF_MS 半周期）
 *
 * 本模块只认「连续失败次数」这个数字，不关心失败原因，也不碰传感器。
 */
#pragma once

#include <Arduino.h>

namespace led {

// 配置引脚并熄灭（上电先灭，避免闪一下）
void init();

// 每次 loop 调一次。failStreak 为当前连续失败次数，
// 由调用方从 stats 模块取，阈值比较在本模块内部完成。
void task(uint32_t now, uint8_t failStreak);

// 成功读到一次数据时调用，触发一次短闪
void flashOk(uint32_t now);

}  // namespace led
