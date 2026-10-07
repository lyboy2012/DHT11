/*
 * DHT11 温湿度 + SSD1306 OLED —— 应用入口
 *
 * 本文件只做两件事：**启动顺序** 和 **采样排期**（非阻塞 millis 轮询）。
 * 具体硬件操作都在各自模块里，依赖方向恒为 main -> 各模块 -> config/reading：
 *
 *   include/config.h         引脚、周期、阈值（唯一配置入口，换硬件只改这里）
 *   include/reading.h        一次读数的数据结构（传感器与显示共享）
 *   include/dht_sensor.h     DHT11 驱动：读一次、算体感/露点、打印读数行
 *   include/oled_display.h   SSD1306 显示：I2C 扫描、地址自适应、三行布局
 *   include/led_indicator.h  板载 LED：成功短闪 / 连续失败快闪
 *   include/stats.h          运行统计：计数、极值、周期汇总
 *
 * 模块之间互不依赖：显示不认识 DHT 驱动，LED 不认识统计，只通过 main 串联。
 *
 * 硬件：经典 ESP32-D0WD-V3（Espressif ESP32 Dev Module，双核 240MHz）
 *   DHT11  VCC -> 3V3    GND -> GND    DATA -> GPIO4
 *          （裸传感器需在 DATA 与 VCC 之间并 4.7kΩ~10kΩ 上拉）
 *   OLED   VCC -> 3V3    GND -> GND    SDA  -> GPIO21    SCL -> GPIO22
 *   LED    GPIO2 板载，无需接线
 *
 * ⚠️ DHT11 的 DATA 是「主机拉低 → 释放 → 传感器回数据」的准双向口，必须接在能输出的脚上。
 *    经典 ESP32 的 GPIO 34~39 是输入专用脚，驱动不了起始信号。
 * ⚠️ DHT11 物理上最快约 1 秒一次采样，间隔不足时 DHTesp 会静默跳过（返回旧值），
 *    所以实际间隔取 max(READ_INTERVAL_MS, 物理下限)，见 dht_sensor.cpp。
 * ⚠️ DHT11 精度 ±2°C / ±5%RH、量程约 0~50°C / 20~90%RH，多数模块只给整数；
 *    要 0.1 分辨率请换 DHT22，同时改 dht_sensor.cpp 里的型号。
 */

#include <Arduino.h>

#include "config.h"
#include "dht_sensor.h"
#include "led_indicator.h"
#include "oled_display.h"
#include "reading.h"
#include "stats.h"

namespace {

uint32_t s_readIntervalMs = READ_INTERVAL_MS;  // 运行时生效的采样间隔
uint32_t s_nextReadAt = 0;                     // 下一次采样时刻

bool s_haveReading = false;                    // 是否成功读到过；失败时显示屏保留旧值
Reading s_lastReading = {0.0f, 0.0f, 0.0f, 0.0f};

void logBanner() {
  Serial.println();
  Serial.println("=========================================");
  Serial.println("ESP32-D0WD-V3 (ESP32 Dev Module) — DHT11 + OLED");
  Serial.printf("芯片: %s  双核 %lu MHz  Flash %lu KB\n",
                ESP.getChipModel(),
                (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(ESP.getFlashChipSize() / 1024));
}

}  // namespace

void setup() {
  Serial.begin(115200);

  led::init();  // 上电先熄灭，避免闪一下

  delay(200);
  logBanner();

  sensor::init();
  s_readIntervalMs = sensor::effectiveInterval();
  sensor::logInfo();

  Serial.printf("LED(GPIO%u): 成功短闪 %lums / 连续失败%u次后 %lums 快闪\n",
                (unsigned)LED_PIN,
                (unsigned long)LED_OK_FLASH_MS,
                (unsigned)FAIL_BLINK_AFTER,
                (unsigned long)LED_FAIL_HALF_MS);

  // 屏幕认不到不算致命错误，测温照常，oled 内部会定期重试
  if (!oled::init()) {
    Serial.printf("[oled] 未检测到屏幕，程序继续运行（每 %lu s 重试一次初始化）\n",
                  (unsigned long)(OLED_RETRY_MS / 1000));
  }
  Serial.println("=========================================");

  const uint32_t now = millis();
  stats::begin(now);
  oled::update(s_lastReading, s_haveReading, 0, "无");  // haveReading=false -> 先画等待画面

  // 上电后 DHT11 需要约 1s 稳定；首次采样同时也要等过物理采样周期
  const uint32_t minPeriod = sensor::minSamplingPeriod();
  const uint32_t firstDelay = (STARTUP_DELAY_MS > minPeriod) ? STARTUP_DELAY_MS : minPeriod;
  s_nextReadAt = now + firstDelay;
  Serial.printf("[dht11] %lu ms 后开始首次采样\n", (unsigned long)firstDelay);
}

void loop() {
  const uint32_t now = millis();

  // ---- 到点采样（非阻塞：只在时间到的那一圈读一次）----
  if ((int32_t)(now - s_nextReadAt) >= 0) {
    // 用有符号差值比较，避免 millis() 回绕(~49天)时判断出错
    s_nextReadAt = now + s_readIntervalMs;

    Reading r = {0.0f, 0.0f, 0.0f, 0.0f};
    if (sensor::read(r) == sensor::Status::Ok) {
      s_lastReading = r;
      s_haveReading = true;
      stats::noteSuccess(r.temperature, r.humidity);
      led::flashOk(now);
      sensor::logReading(r, stats::samples());
    } else {
      stats::noteFailure();
      Serial.printf("[dht11] [!!] 第 %lu 次读取失败: %s  (连续 %u 次)\n",
                    (unsigned long)stats::failures(), sensor::lastError(),
                    (unsigned)stats::failStreak());
    }

    // 无论成败都刷新画面：成功显示新值，失败保留最后一次有效值并提示原因
    oled::update(s_lastReading, s_haveReading, stats::failStreak(), sensor::lastError());
  }

  stats::task(now, sensor::lastError());
  led::task(now, stats::failStreak());
  oled::task(now);
}
