/*
 * 运行统计模块实现。
 */
#include "stats.h"

#include "config.h"

namespace {

uint32_t s_samples = 0;      // 累计成功次数
uint32_t s_failures = 0;     // 累计失败次数
uint8_t s_failStreak = 0;    // 连续失败次数(成功即清零)

bool s_haveExtremes = false;  // 是否已有极值样本
float s_minT = 0.0f, s_maxT = 0.0f;
float s_minH = 0.0f, s_maxH = 0.0f;

uint32_t s_startedAt = 0;        // 启动时刻
uint32_t s_lastSummaryAt = 0;    // 上次汇总时刻

void updateExtremes(float t, float h) {
  if (!s_haveExtremes) {
    s_minT = s_maxT = t;
    s_minH = s_maxH = h;
    s_haveExtremes = true;
    return;
  }
  if (t < s_minT) s_minT = t;
  if (t > s_maxT) s_maxT = t;
  if (h < s_minH) s_minH = h;
  if (h > s_maxH) s_maxH = h;
}

void printSummary(uint32_t now, const char* lastError) {
  const uint32_t upS = (now - s_startedAt) / 1000;

  if (!s_haveExtremes) {
    Serial.printf("[dht11] 汇总: 运行 %lus  成功 0 次  失败 %lu 次  最近原因: %s\n",
                  (unsigned long)upS, (unsigned long)s_failures, lastError);
    return;
  }

  Serial.printf("[dht11] 汇总: 运行 %lus  成功 %lu 次  失败 %lu 次(连续 %u)\n",
                (unsigned long)upS, (unsigned long)s_samples,
                (unsigned long)s_failures, (unsigned)s_failStreak);
  Serial.printf("[dht11]       温度 %.1f~%.1f °C   湿度 %.1f~%.1f %%RH\n",
                s_minT, s_maxT, s_minH, s_maxH);
}

}  // namespace

namespace stats {

void begin(uint32_t now) {
  s_startedAt = now;
  s_lastSummaryAt = now;
}

void noteSuccess(float temperature, float humidity) {
  s_samples++;
  s_failStreak = 0;
  updateExtremes(temperature, humidity);
}

void noteFailure() {
  s_failures++;
  if (s_failStreak < 255) {
    s_failStreak++;  // 防止长期失败时溢出
  }
}

uint32_t samples() { return s_samples; }

uint32_t failures() { return s_failures; }

uint8_t failStreak() { return s_failStreak; }

void task(uint32_t now, const char* lastError) {
  if (now - s_lastSummaryAt < STATS_INTERVAL_MS) {
    return;
  }
  s_lastSummaryAt = now;
  printSummary(now, lastError);
}

}  // namespace stats
