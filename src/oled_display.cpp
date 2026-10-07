/*
 * OLED 显示模块实现（U8g2 + SSD1306 128x64 硬件 I2C）。
 */
#include "oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"

namespace {

// 字体（均已确认存在于 U8g2 2.36.18）：
//   wqy12_t_gb2312 : 点阵中文，显示「温度 / 湿度 / 体感 / 露点 / 失败」等标签
//   logisoso16_tn  : 只含数字/小数点/负号/冒号，显示温度/湿度数值（数字实际高 16px）
//   helvB08_tf     : 含 Latin-1，有 ° 字形，用作单位。
//                    想画 ° 必须**两个条件同时满足**：字体含 Latin-1 字形（_tf，不是 _tr）
//                    + 用 drawUTF8() 画（用 drawStr 会按字节把 UTF-8 的 C2 B0 拆成 Â 和 °）
// 数值字号想再调就换这一行：logisoso18_tn(18px) / logisoso20_tn(21px) 更大。
const uint8_t* const FONT_CN = u8g2_font_wqy12_t_gb2312;
const uint8_t* const FONT_BIG = u8g2_font_logisoso16_tn;
const uint8_t* const FONT_UNIT = u8g2_font_helvB08_tf;

// 构造参数顺序是 (rotation, reset, clock, data)：clock 传 SCL、data 传 SDA。
// U8g2 收到这两个脚后会在 begin() 里自己调 Wire.begin(data, clock)，
// 所以引脚只需要在这里给一次，别处不用再 Wire.begin。
U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_oled(U8G2_R0, U8X8_PIN_NONE,
                                           OLED_SCL_PIN, OLED_SDA_PIN);

bool s_ready = false;         // 是否初始化成功
bool s_dirty = false;         // 是否需要重绘
bool s_haveReading = false;   // 是否已经有过有效读数
Reading s_reading = {0.0f, 0.0f, 0.0f, 0.0f};  // 最近一次有效读数
uint8_t s_failStreak = 0;     // 连续失败次数（决定底行显示什么）
const char* s_lastError = "无";
uint32_t s_retryAt = 0;       // 下次重试初始化的时刻

// 扫描 I2C 总线：打印所有应答的地址，返回第一个设备的 7 位地址(0 = 一个都没扫到)
uint8_t i2cScan() {
  Serial.printf("[i2c] 扫描总线 (SDA=GPIO%u, SCL=GPIO%u)...\n",
                (unsigned)OLED_SDA_PIN, (unsigned)OLED_SCL_PIN);
  uint8_t first = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c]   发现设备: 0x%02X\n", addr);
      if (first == 0) {
        first = addr;
      }
    }
  }
  if (first == 0) {
    Serial.println("[i2c]   没扫到任何设备 —— 查 3V3/GND、SDA(21)/SCL(22) 是否接反或虚接");
  }
  return first;
}

// 画「大号数字 + 小号单位」，返回右边缘的 x
uint8_t drawValue(uint8_t x, uint8_t baseline, const char* num, const char* unit) {
  s_oled.setFont(FONT_BIG);
  s_oled.drawStr(x, baseline, num);  // 数字是纯 ASCII，drawStr 即可
  const uint8_t w = (uint8_t)s_oled.getStrWidth(num);

  // ⚠️ 单位必须用 drawUTF8 + getUTF8Width，不能用 drawStr/getStrWidth：
  //   源码里的 "°" 是 UTF-8 两字节 C2 B0，而 drawStr 是按字节查表的，
  //   会先命中 Latin-1 的 0xC2 = 'Â'（8px 下看着就是个 A），再命中 0xB0 = '°'，
  //   屏幕上就变成「Â°C」——多出一个 A。drawUTF8 才会把 C2 B0 正确解码成 U+00B0。
  s_oled.setFont(FONT_UNIT);
  s_oled.drawUTF8((uint8_t)(x + w + 1), baseline, unit);
  return (uint8_t)(x + w + 1 + s_oled.getUTF8Width(unit));
}

// 右下角右对齐的小字
void drawRight(uint8_t baseline, const char* text) {
  s_oled.setFont(FONT_CN);
  const uint8_t w = (uint8_t)s_oled.getUTF8Width(text);
  s_oled.drawUTF8((uint8_t)(OLED_WIDTH - w - 1), baseline, text);
}

// 小号「中文标签 + 数值 + 单位」对，共用一条基线（底行放体感 / 露点用）
// 底行要塞两组内容、宽度很紧（实测右格到屏边只剩 1~2px），所以有两条约束：
//   1) 单位必须走 drawUTF8 —— 原因见 drawValue：源码里的 "°" 是 UTF-8 两字节 C2 B0，
//      用 drawStr 按字节查表会画成「Â°」，屏幕上多一个像 A 的字符；
//   2) 数值最多占 4 个字符（"10.0"）。极冷极干时露点会出现 "-16.1" 这种 5 字符，
//      会把 °C 顶出屏幕，所以超过 4 字符就退回整数格式（"-16"），最多省 9px。
void drawSmallPair(uint8_t x, uint8_t baseline, const char* label, float value, const char* unit) {
  char buf[24];

  s_oled.setFont(FONT_CN);
  s_oled.drawUTF8(x, baseline, label);
  const uint8_t w = (uint8_t)s_oled.getUTF8Width(label);

  snprintf(buf, sizeof(buf), "%.1f", value);
  if (strlen(buf) > 4) {
    snprintf(buf, sizeof(buf), "%.0f", value);
  }

  s_oled.setFont(FONT_UNIT);
  s_oled.drawStr((uint8_t)(x + w + 1), baseline, buf);  // 数字是纯 ASCII
  const uint8_t nw = (uint8_t)s_oled.getStrWidth(buf);
  s_oled.drawUTF8((uint8_t)(x + w + 1 + nw), baseline, unit);
}

void render() {
  if (!s_ready) {
    return;
  }

  char buf[48];
  s_oled.clearBuffer();

  // ---- 还没读到过数据（上电后头 1~2 秒，或一直失败）----
  if (!s_haveReading) {
    s_oled.setFont(FONT_CN);
    s_oled.drawUTF8(2, 20, "DHT11 温湿度");
    s_oled.drawUTF8(2, 42, "等待数据...");
    s_oled.setFont(FONT_UNIT);
    s_oled.drawStr(2, 61, "OLED OK");
    s_oled.sendBuffer();
    return;
  }

  // ---- 第一行：标签 + 右上角状态 ----
  // 布局基线取自字体的真实度量（u8g2 字体头部）：
  //   logisoso16_tn 数字高 16px（基线 38 -> 占 y=22~38）
  //   wqy12 中文高 13px（基线 12 -> 占 y=0~13；基线 62 -> 占 y=50~63）
  // 换成 16px 后四行也放得下了（2×16 + 2×13 = 58px < 64px），
  // 但仍保留主次：温度/湿度用大字，体感/露点并到小字一行，一屏四个值齐全又不挤。
  s_oled.setFont(FONT_CN);
  s_oled.drawUTF8(2, 12, "温度");
  s_oled.drawUTF8(66, 12, "湿度");
  if (s_failStreak >= FAIL_BLINK_AFTER) {
    drawRight(12, "异常");  // 失败详情在底行，这里只给个提示
  } else if (s_failStreak > 0) {
    snprintf(buf, sizeof(buf), "失败%u", (unsigned)s_failStreak);
    drawRight(12, buf);
  } else {
    drawRight(12, "正常");
  }

  // ---- 第二行：温度 / 湿度 大字 ----
  snprintf(buf, sizeof(buf), "%.1f", s_reading.temperature);
  drawValue(2, 38, buf, "°C");
  snprintf(buf, sizeof(buf), "%.1f", s_reading.humidity);
  drawValue(66, 38, buf, "%");

  // ---- 第三行：体感 / 露点小字；连续失败时改为失败详情 ----
  if (s_failStreak >= FAIL_BLINK_AFTER) {
    // 上面的数值已经是旧值，底行直接把原因和次数写清楚
    snprintf(buf, sizeof(buf), "失败%u次 %s", (unsigned)s_failStreak, s_lastError);
    s_oled.setFont(FONT_CN);
    s_oled.drawUTF8(2, 62, buf);
  } else {
    drawSmallPair(2, 62, "体感", s_reading.heatIndex, "°C");
    drawSmallPair(66, 62, "露点", s_reading.dewPoint, "°C");
  }

  s_oled.sendBuffer();
}

}  // namespace

namespace oled {

bool init() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_HZ);

  const uint8_t found = i2cScan();
  if (found == 0) {
    s_ready = false;
    return false;  // 总线上没设备，别去初始化，省得白刷 I2C
  }

  // ⚠️ U8g2 要的是 8 位地址(7 位左移一位)，写 0x3C 反而会通信失败
  s_oled.setI2CAddress((uint8_t)(found << 1));
  s_oled.begin();
  s_oled.setBusClock(OLED_I2C_HZ);  // 让后续传输跑 400kHz

  Serial.printf("[oled] SSD1306 128x64 就绪, I2C 地址 0x%02X%s\n",
                (unsigned)found,
                (found == 0x3C || found == 0x3D) ? "" : "  ← 非典型地址, 可能不是 SSD1306 屏");

  s_ready = true;
  s_dirty = true;
  return true;
}

bool ready() {
  return s_ready;
}

void update(const Reading& r, bool haveReading, uint8_t failStreak, const char* lastError) {
  s_reading = r;
  s_haveReading = haveReading;
  s_failStreak = failStreak;
  s_lastError = lastError;
  s_dirty = true;
}

void task(uint32_t now) {
  // ---- 掉线/没插好时定期重试初始化（方便先烧程序、后接线）----
  if (!s_ready) {
    if (now - s_retryAt >= OLED_RETRY_MS) {
      s_retryAt = now;
      Serial.println("[oled] 重试初始化...");
      init();
    }
    return;
  }

  // ---- 只在数据/状态变化时刷屏，省 I2C 带宽也避免闪屏 ----
  if (s_dirty) {
    s_dirty = false;
    render();
  }
}

}  // namespace oled
