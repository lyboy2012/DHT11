/*
 * DHT11 传感器模块（硬件层）。
 *
 * 职责：初始化 DHTesp、读一次数据、换算体感温度与露点、打印传感器信息与读数行。
 * 不负责时间排期 —— 什么时候读由 main.cpp 决定，本模块只被调用。
 */
#pragma once

#include <Arduino.h>

#include "reading.h"

namespace sensor {

enum class Status : uint8_t {
  Ok,      // 读到了有效数据
  Failed,  // 超时 / 校验和错 / NaN
};

// 初始化传感器（内部是 DHTesp::setup）
void init();

// 传感器物理采样下限（DHT11 = 1000ms）
uint32_t minSamplingPeriod();

// 运行时真正生效的采样间隔 = max(READ_INTERVAL_MS, minSamplingPeriod())
uint32_t effectiveInterval();

// 读一次。成功时填充 out 并返回 Status::Ok；失败返回 Status::Failed，
// 原因用 lastError() 取（"TIMEOUT" / "CHECKSUM" / "读到 NaN"）。
Status read(Reading& out);

// 最近一次失败原因；成功时为 "无"
const char* lastError();

// 打印型号 / 量程 / 采样间隔
void logInfo();

// 打印一行读数（sampleSeq 是累计成功次数，用于对齐日志编号）
void logReading(const Reading& r, uint32_t sampleSeq);

}  // namespace sensor
