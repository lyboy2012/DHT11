/*
 * OLED 显示模块（硬件层，SSD1306 128x64 / I2C）。
 *
 * 职责：
 *   - 启动扫描 I2C 总线，用扫到的地址初始化（0x3C / 0x3D 都自适应）；
 *   - 扫不到屏幕不算致命错误，之后每 OLED_RETRY_MS 自动重试一次；
 *   - 把一次读数画成三行：温度/湿度大字上行，体感/露点小字下行，右上角状态；
 *
 * 画法上只在数据变化时打「脏标记」，真正的绘制统一在 task() 里做，
 * 所以调用方只管喂数据，不用关心什么时候刷屏。
 */
#pragma once

#include <Arduino.h>

#include "reading.h"

namespace oled {

// 扫描 + 初始化。返回 false 表示没扫到屏幕（不影响测温，之后会自动重试）
bool init();

// 屏幕是否可用
bool ready();

// 更新画面数据（只打脏标记，不立刻绘制）
//   haveReading = false 时画「等待数据」画面
//   正常：温度/湿度大字 + 体感/露点小字 + 右上角「正常」
//   偶发失败(1~2 次)：右上角显示「失败N」
//   连续失败(>= FAIL_BLINK_AFTER)：右上角「异常」，底行改为失败原因与次数，
//                                  温湿度保留最后一次有效值
void update(const Reading& r, bool haveReading, uint8_t failStreak, const char* lastError);

// 每次 loop 调一次：处理「没认到屏幕时的定期重试」与脏标记重绘
void task(uint32_t now);

}  // namespace oled
