/*
 * 一次有效的温湿度读数（领域数据）。
 *
 * 单独放一个头文件，是为了让「显示模块」不必依赖「DHT 驱动模块」：
 * 传感器负责填它，显示/统计只负责读它，两边只共享这个结构。
 */
#pragma once

struct Reading {
  float temperature;  // 温度 °C
  float humidity;     // 相对湿度 %RH
  float heatIndex;    // 体感温度 °C
  float dewPoint;     // 露点 °C
};
