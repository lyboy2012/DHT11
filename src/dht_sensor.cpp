/*
 * DHT11 传感器模块实现。
 *
 * 用的是 DHTesp（beegee-tokyo/DHT sensor library for ESPx）：
 *   - 内部状态机读单总线时序，失败会给出 TIMEOUT / CHECKSUM 具体原因；
 *   - 自带体感温度、露点换算；
 *   - 读取时会进临界区(portENTER_CRITICAL)屏蔽任务切换，时序更稳。
 */
#include "dht_sensor.h"

#include <DHTesp.h>

#include "config.h"

namespace {

DHTesp s_dht;
const char* s_lastError = "无";

}  // namespace

namespace sensor {

void init() {
  // DHT11 的解析规则与 DHT22 不同，型号写错会一直 CHECKSUM
  s_dht.setup(DHT_PIN, DHTesp::DHT11);
}

uint32_t minSamplingPeriod() {
  return (uint32_t)s_dht.getMinimumSamplingPeriod();
}

uint32_t effectiveInterval() {
  const uint32_t minPeriod = minSamplingPeriod();
  // 间隔不足时 DHTesp 会「静默跳过」这次读取（不报错，返回的仍是上次的旧值），
  // 所以这里必须兜底到传感器物理下限之上。
  return (READ_INTERVAL_MS > minPeriod) ? READ_INTERVAL_MS : minPeriod;
}

Status read(Reading& out) {
  const TempAndHumidity v = s_dht.getTempAndHumidity();

  // getStatus() 区分 超时(TIMEOUT) / 校验和错(CHECKSUM) 两种失败原因
  if (s_dht.getStatus() != DHTesp::ERROR_NONE) {
    s_lastError = s_dht.getStatusString();
    return Status::Failed;
  }
  // 保险：状态正常也可能拿到 NaN
  if (isnan(v.temperature) || isnan(v.humidity)) {
    s_lastError = "读到 NaN";
    return Status::Failed;
  }

  out.temperature = v.temperature;
  out.humidity = v.humidity;
  out.heatIndex = s_dht.computeHeatIndex(out.temperature, out.humidity, false);
  out.dewPoint = s_dht.computeDewPoint(out.temperature, out.humidity, false);

  s_lastError = "无";
  return Status::Ok;
}

const char* lastError() {
  return s_lastError;
}

void logInfo() {
  Serial.printf("[dht11] 型号 DHT11  DATA=GPIO%u  物理最快采样 %lu ms  本工程间隔 %lu ms\n",
                (unsigned)DHT_PIN,
                (unsigned long)minSamplingPeriod(),
                (unsigned long)effectiveInterval());
  Serial.printf("[dht11] 量程: 温度 %d~%d °C(%d 位小数)  湿度 %d~%d %%RH\n",
                (int)s_dht.getLowerBoundTemperature(),
                (int)s_dht.getUpperBoundTemperature(),
                (int)s_dht.getNumberOfDecimalsTemperature(),
                (int)s_dht.getLowerBoundHumidity(),
                (int)s_dht.getUpperBoundHumidity());
}

void logReading(const Reading& r, uint32_t sampleSeq) {
  Serial.printf("[dht11] #%lu  温度 %5.1f °C   湿度 %5.1f %%RH   体感 %5.1f °C   露点 %5.1f °C\n",
                (unsigned long)sampleSeq,
                r.temperature, r.humidity, r.heatIndex, r.dewPoint);
}

}  // namespace sensor
