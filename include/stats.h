/*
 * 运行统计模块（纯逻辑，不碰硬件）。
 *
 * 职责：成功/失败计数、连续失败次数、温湿度极值、按间隔打印汇总行。
 */
#pragma once

#include <Arduino.h>

namespace stats {

// 记录起始时刻（setup 里调一次）
void begin(uint32_t now);

// 一次成功读数
void noteSuccess(float temperature, float humidity);

// 一次失败读数
void noteFailure();

// 累计成功次数（也用作日志里的 #编号）
uint32_t samples();

// 累计失败次数
uint32_t failures();

// 当前连续失败次数（成功即清零）
uint8_t failStreak();

// 每次 loop 调一次；到 STATS_INTERVAL_MS 就打印汇总，
// lastError 只在「还没有任何成功样本」时会被用到。
void task(uint32_t now, const char* lastError);

}  // namespace stats
