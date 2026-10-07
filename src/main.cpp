/*
 * DHT11 温湿度 — 经典 ESP32-D0WD-V3 (Espressif ESP32 Dev Module, 双核 240MHz)
 *
 * 目标引脚: DATA -> GPIO 4
 *
 * 功能:
 *   1. 每 READ_INTERVAL_MS(默认 2s，且不低于传感器物理下限) 读一次温湿度，
 *      串口打印 温度 / 湿度 / 体感温度(heat index) / 露点(dew point)
 *   2. 非阻塞：loop() 里只用 millis() 排期，不用 delay() 阻塞主循环
 *   3. 状态 LED(GPIO 2 板载 LED)：
 *        每成功读数一次短闪 50ms
 *        连续失败 >= 3 次后 100ms 快闪（没接好 / 缺上拉 / 线太长）
 *   4. 统计：累计成功/失败次数、温度与湿度极值，每 60s 打一行汇总
 *
 * ⚠️ DHT11 物理上最快约 1 秒一次采样。DHTesp 在间隔不足时会「静默跳过」这次读取
 *    （不报错，但温湿度仍是上一次的旧值），所以采样间隔要比 1s 宽裕些；
 *    本工程默认 2s，稳且够用。
 *
 * ⚠️ DHT11 精度有限：温度 ±2 °C、湿度 ±5 %RH，量程约 0~50 °C / 20~90 %RH，
 *    多数模块只输出整数（个别模块带一位小数）。想要 0.1 分辨率请换 DHT22/AM2302，
 *    同时把 setup() 里的 DHTesp::DHT11 改成 DHTesp::DHT22。
 *
 * ⚠️ DHT11 的 DATA 是「单片机拉低 -> 释放 -> 传感器回数据」的准双向口，
 *    所以必须用「能输出」的脚。经典 ESP32 的 GPIO 34~39 是输入专用脚，
 *    驱动不了起始信号，别拿它们接 DATA。
 *    可用: GPIO 2/4/5/12~19/21~23/25~27/32/33。
 *
 * 接线:
 *    DHT11 VCC  -> 3V3        (3.3V 与 5V 都能跑，ESP32 直接用 3V3 最省事)
 *    DHT11 GND  -> GND
 *    DHT11 DATA -> GPIO 4
 *    DATA 与 VCC 之间再并一个 4.7kΩ~10kΩ 上拉电阻（裸传感器必须加；
 *    三针模块通常已自带上拉，可以省掉，但加了更稳）。
 */

#include <Arduino.h>
#include <DHTesp.h>  // beegee-tokyo/DHT sensor library for ESPx (DHTesp)

// ======== 可配置项 ========
static const uint8_t DHT_PIN = 4;        // DHT11 DATA 脚 -> GPIO 4
// 传感器型号在 setup() 里写死成 DHTesp::DHT11；换成 DHT22/AM2302 时同步改掉，
// 否则解析规则不同会一直读到 ERROR_CHECKSUM。

static const uint8_t LED_PIN = 2;         // 板载 LED: GPIO 2（多数 ESP32 开发板如此）
static const bool LED_ACTIVE_HIGH = true; // 高电平点亮; 板载 LED 亮灭相反就改为 false

static const uint32_t READ_INTERVAL_MS = 2000;   // 采样间隔下限(实际取 max(它, 传感器物理下限))
static const uint32_t STARTUP_DELAY_MS = 1200;   // 上电后 DHT11 需要约 1s 稳定，首次采样往后推
static const uint32_t LED_OK_FLASH_MS = 50;      // 成功读数: 亮 50ms
static const uint32_t LED_FAIL_HALF_MS = 100;    // 失败告警: 亮100/灭100
static const uint8_t FAIL_BLINK_AFTER = 3;       // 连续失败几次开始快闪
static const uint32_t STATS_INTERVAL_MS = 60000; // 汇总行间隔: 60s

// ======== 运行状态 ========
static DHTesp s_dht;

static uint32_t s_readIntervalMs = READ_INTERVAL_MS;  // 运行时生效的采样间隔
static uint32_t s_nextReadAt = 0;                     // 下一次采样时刻
static uint32_t s_samples = 0;                        // 累计成功次数
static uint32_t s_failures = 0;                       // 累计失败次数
static uint8_t s_failStreak = 0;                      // 连续失败次数(成功即清零)
static const char* s_lastError = "无";                 // 最近一次失败原因

static bool s_haveExtremes = false;  // 是否已有极值样本
static float s_minT = 0.0f, s_maxT = 0.0f;
static float s_minH = 0.0f, s_maxH = 0.0f;
static uint32_t s_lastStatsAt = 0;   // 上次汇总时刻
static uint32_t s_startedAt = 0;     // 启动时刻(算平均采样间隔用)

static bool s_ledOn = false;         // LED 当前是否亮
static uint32_t s_ledChangedAt = 0;  // LED 上次翻转/变更时刻
static uint32_t s_okFlashUntil = 0;  // 成功短闪的截止时刻

// ======== LED ========
// 根据有效电平把 亮/灭 换算成实际输出电平
static inline uint8_t ledLevel(bool on) {
  if (LED_ACTIVE_HIGH) {
    return on ? HIGH : LOW;
  }
  return on ? LOW : HIGH;
}

static inline void ledSet(bool on) {
  digitalWrite(LED_PIN, ledLevel(on));
}

// 非阻塞 LED：正常时给成功读数一次短闪，连续失败时快闪告警
static void ledTask(uint32_t now) {
  // ---- 连续失败: 快闪 ----
  if (s_failStreak >= FAIL_BLINK_AFTER) {
    if (now - s_ledChangedAt >= LED_FAIL_HALF_MS) {
      s_ledChangedAt = now;
      s_ledOn = !s_ledOn;
      ledSet(s_ledOn);
    }
    return;  // 从快闪切回正常时, s_ledChangedAt 由下面的分支接管, 不会误触发
  }

  // ---- 正常: 只有成功短闪的时间窗内才亮 ----
  const bool wantOn = (int32_t)(now - s_okFlashUntil) < 0;
  if (wantOn != s_ledOn) {
    s_ledOn = wantOn;
    ledSet(wantOn);
  }
  s_ledChangedAt = now;  // 让「刚转入快闪」的第一次翻转从此刻起算 100ms
}

// ======== 读一次传感器 ========
// 成功返回 true 并填好 t/h；失败返回 false，原因留在 s_lastError
static bool readDht(float& t, float& h) {
  const TempAndHumidity v = s_dht.getTempAndHumidity();

  // getStatus() 区分 超时(TIMEOUT) / 校验和错(CHECKSUM) 两种失败原因
  if (s_dht.getStatus() != DHTesp::ERROR_NONE) {
    s_lastError = s_dht.getStatusString();
    return false;
  }
  // 保险: 状态正常也可能拿到 NaN
  if (isnan(v.temperature) || isnan(v.humidity)) {
    s_lastError = "读到 NaN";
    return false;
  }

  t = v.temperature;
  h = v.humidity;
  s_lastError = "无";
  return true;
}

// ======== 极值统计 ========
static void updateExtremes(float t, float h) {
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

// ======== 打印一次读数 ========
static void reportReading(float t, float h) {
  // 体感温度(heat index)与露点由 DHTesp 用刚读到的温湿度换算
  const float heatIndex = s_dht.computeHeatIndex(t, h, false);
  const float dewPoint = s_dht.computeDewPoint(t, h, false);

  Serial.printf("[dht11] #%lu  温度 %5.1f °C   湿度 %5.1f %%RH   体感 %5.1f °C   露点 %5.1f °C\n",
                (unsigned long)s_samples, t, h, heatIndex, dewPoint);
}

// ======== 汇总行 ========
static void printStats(uint32_t now) {
  const uint32_t upS = (now - s_startedAt) / 1000;
  if (!s_haveExtremes) {
    Serial.printf("[dht11] 汇总: 运行 %lus  成功 0 次  失败 %lu 次  最近原因: %s\n",
                  (unsigned long)upS, (unsigned long)s_failures, s_lastError);
    return;
  }
  Serial.printf("[dht11] 汇总: 运行 %lus  成功 %lu 次  失败 %lu 次(连续 %u)\n",
                (unsigned long)upS, (unsigned long)s_samples,
                (unsigned long)s_failures, (unsigned)s_failStreak);
  Serial.printf("[dht11]       温度 %.1f~%.1f °C   湿度 %.1f~%.1f %%RH\n",
                s_minT, s_maxT, s_minH, s_maxH);
}

// ======== 打印传感器能力 ========
static void printSensorInfo() {
  Serial.printf("[dht11] 型号 DHT11  DATA=GPIO%u  物理最快采样 %lu ms  本工程间隔 %lu ms\n",
                (unsigned)DHT_PIN,
                (unsigned long)s_dht.getMinimumSamplingPeriod(),
                (unsigned long)s_readIntervalMs);
  Serial.printf("[dht11] 量程: 温度 %d~%d °C(%d 位小数)  湿度 %d~%d %%RH\n",
                (int)s_dht.getLowerBoundTemperature(),
                (int)s_dht.getUpperBoundTemperature(),
                (int)s_dht.getNumberOfDecimalsTemperature(),
                (int)s_dht.getLowerBoundHumidity(),
                (int)s_dht.getUpperBoundHumidity());
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  ledSet(false);  // 上电先熄灭，避免闪一下

  delay(200);
  Serial.println();
  Serial.println("=========================================");
  Serial.println("ESP32-D0WD-V3 (ESP32 Dev Module) — DHT11");
  Serial.printf("芯片: %s  双核 %lu MHz  Flash %lu KB\n",
                ESP.getChipModel(),
                (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(ESP.getFlashChipSize() / 1024));

  s_dht.setup(DHT_PIN, DHTesp::DHT11);

  // 采样间隔取「配置值」与「传感器物理下限」中的较大者：
  // 间隔不足时 DHTesp 会静默跳过读取（返回的仍是上次的旧值），不会报错，最难查。
  const uint32_t minPeriod = (uint32_t)s_dht.getMinimumSamplingPeriod();
  s_readIntervalMs = (READ_INTERVAL_MS > minPeriod) ? READ_INTERVAL_MS : minPeriod;

  printSensorInfo();
  Serial.printf("LED(GPIO%u): 成功短闪 %lums / 连续失败%u次后 %lums 快闪\n",
                (unsigned)LED_PIN,
                (unsigned long)LED_OK_FLASH_MS,
                (unsigned)FAIL_BLINK_AFTER,
                (unsigned long)LED_FAIL_HALF_MS);
  Serial.println("=========================================");

  s_startedAt = millis();
  s_lastStatsAt = s_startedAt;
  // 上电后 DHT11 需要约 1s 稳定；首次采样同时也要等过物理采样周期
  const uint32_t firstDelay = (STARTUP_DELAY_MS > minPeriod) ? STARTUP_DELAY_MS : minPeriod;
  s_nextReadAt = s_startedAt + firstDelay;
  Serial.printf("[dht11] %lu ms 后开始首次采样\n", (unsigned long)firstDelay);
}

void loop() {
  const uint32_t now = millis();

  // ---- 到点采样(非阻塞: 只在时间到的那一圈做一次读) ----
  if ((int32_t)(now - s_nextReadAt) >= 0) {
    // 用有符号差值比较，避免 millis() 回绕(~49天)时判断出错
    s_nextReadAt = now + s_readIntervalMs;

    float t = 0.0f, h = 0.0f;
    if (readDht(t, h)) {
      s_samples++;
      s_failStreak = 0;
      s_okFlashUntil = now + LED_OK_FLASH_MS;
      updateExtremes(t, h);
      reportReading(t, h);
    } else {
      s_failures++;
      if (s_failStreak < 255) {
        s_failStreak++;  // 防止长期失败时溢出
      }
      Serial.printf("[dht11] [!!] 第 %lu 次读取失败: %s  (连续 %u 次)\n",
                    (unsigned long)s_failures, s_lastError, (unsigned)s_failStreak);
    }
  }

  // ---- 周期性汇总 ----
  if (now - s_lastStatsAt >= STATS_INTERVAL_MS) {
    s_lastStatsAt = now;
    printStats(now);
  }

  ledTask(now);
}
