/*
 * 板载 LED 状态指示模块实现。
 *
 * 全程非阻塞：只用 millis() 比较，不用 delay()，不阻塞采样与刷屏。
 */
#include "led_indicator.h"

#include "config.h"

namespace {

bool s_on = false;              // LED 当前是否亮
uint32_t s_changedAt = 0;       // 上次翻转/变更时刻
uint32_t s_okFlashUntil = 0;    // 成功短闪的截止时刻

// 根据有效电平把 亮/灭 换算成实际输出电平
inline uint8_t level(bool on) {
  if (LED_ACTIVE_HIGH) {
    return on ? HIGH : LOW;
  }
  return on ? LOW : HIGH;
}

inline void set(bool on) {
  digitalWrite(LED_PIN, level(on));
}

}  // namespace

namespace led {

void init() {
  pinMode(LED_PIN, OUTPUT);
  set(false);
}

void task(uint32_t now, uint8_t failStreak) {
  // ---- 连续失败：快闪告警 ----
  if (failStreak >= FAIL_BLINK_AFTER) {
    if (now - s_changedAt >= LED_FAIL_HALF_MS) {
      s_changedAt = now;
      s_on = !s_on;
      set(s_on);
    }
    return;  // 从快闪切回正常时，s_changedAt 由下面的分支接管，不会误触发
  }

  // ---- 正常：只有成功短闪的时间窗内才亮 ----
  const bool wantOn = (int32_t)(now - s_okFlashUntil) < 0;
  if (wantOn != s_on) {
    s_on = wantOn;
    set(wantOn);
  }
  s_changedAt = now;  // 让「刚转入快闪」的第一次翻转从此刻起算一个半周期
}

void flashOk(uint32_t now) {
  s_okFlashUntil = now + LED_OK_FLASH_MS;
}

}  // namespace led
