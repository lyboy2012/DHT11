# DHT11

ESP32 上读 DHT11 温湿度传感器的 PlatformIO 工程（Arduino 框架），
与同级 `led` / `wifi` 项目同款板子、同款 framework 版本、同款代码组织方式。

## 硬件

| 项目 | 值 |
|---|---|
| 板子 | 经典 ESP32-D0WD-V3（Espressif ESP32 Dev Module），双核 240MHz |
| Flash | 4MB |
| 传感器 | DHT11（温湿度，单总线） |
| DATA 引脚 | **GPIO 4** |
| 状态 LED | **GPIO 2 板载 LED**（多数 ESP32 开发板如此，无需外接元件） |
| 下载口 | 板载 USB-UART 桥（macOS 上枚举为 `/dev/cu.usbserial-*`） |
| 采样间隔 | 2s（DHT11 物理上限约 1 Hz） |

> ⚠️ DHT11 的 DATA 是「主机拉低 → 释放 → 传感器回数据」的准双向口，必须接在**能输出**的脚上。
> 经典 ESP32 的 **GPIO 34~39 是输入专用脚**，驱动不了起始信号，别拿来接 DATA。
> 可用：GPIO 2 / 4 / 5 / 12~19 / 21~23 / 25~27 / 32 / 33。

## 接线

```
        ┌───────────┐
 3V3 ───┤ VCC       │
 GPIO4 ─┤ DATA      │ DHT11
 GND ───┤ GND       │
        └───────────┘
          │
          └──[4.7kΩ~10kΩ]── 3V3      ← DATA 与 VCC 之间的上拉

        状态 LED（无需接线）: GPIO2 — 多数开发板板载
```

- **裸传感器（4 脚）必须加上拉电阻**（4.7kΩ~10kΩ，接在 DATA 与 VCC 之间），否则读出来全是 `TIMEOUT`。
- **三针模块**（VCC / DATA / GND，带小 PCB）通常已经板载上拉，可以省掉；再加一个也不影响。
- 供电用 **3V3** 最省事（DHT11 支持 3.3V~5.5V，DATA 电平跟着 VCC 走，用 5V 供电时 DATA 会输出 5V 电平，
  虽然多数情况能读，但不建议；要 5V 供电就在 DATA 与 ESP32 之间加电平转换或分压）。
- 杜邦线别拉太长（建议 < 1m），线长 + 无上拉是 `TIMEOUT` 的两大主因。

## 行为

1. **周期采样**：每 `READ_INTERVAL_MS`（默认 2s，且自动放大到不低于传感器物理下限）读一次，
   串口打印 **温度 / 湿度 / 体感温度（heat index）/ 露点（dew point）**。
2. **非阻塞**：`loop()` 里只用 `millis()` 排期，没有阻塞式 `delay()`，方便以后往上加 WiFi 上报、
   按键、OLED 之类的任务。
3. **失败可观测**：读取失败会打印具体原因（`TIMEOUT` / `CHECKSUM`）和连续失败次数。
4. **统计**：累计成功/失败次数、温度与湿度的运行极值，每 60s 打一行汇总。
5. **状态 LED（GPIO 2 板载）**：

| LED 表现 | 含义 |
|---|---|
| 每读到一次数据闪一下（50ms） | 正常，读数已刷新 |
| **100ms 快闪**（连续失败 ≥ 3 次） | 传感器没接好 / 缺上拉 / 线太长 / 供电不对 |

### 串口输出示例

```
=========================================
ESP32-D0WD-V3 (ESP32 Dev Module) — DHT11
芯片: ESP32-D0WD-V3  双核 240 MHz  Flash 4096 KB
[dht11] 型号 DHT11  DATA=GPIO4  物理最快采样 1000 ms  本工程间隔 2000 ms
[dht11] 量程: 温度 0~50 °C(0 位小数)  湿度 20~90 %RH
LED(GPIO2): 成功短闪 50ms / 连续失败3次后 100ms 快闪
=========================================
[dht11] 1200 ms 后开始首次采样
[dht11] #1  温度  24.0 °C   湿度  46.0 %RH   体感  24.1 °C   露点  11.7 °C
[dht11] #2  温度  24.0 °C   湿度  46.0 %RH   体感  24.1 °C   露点  11.7 °C
[dht11] [!!] 第 1 次读取失败: TIMEOUT  (连续 1 次)
[dht11] 汇总: 运行 60s  成功 29 次  失败 1 次(连续 0)
[dht11]       温度 23.0~25.0 °C   湿度 45.0~47.0 %RH
```

### 可调常量（`src/main.cpp` 顶部）

```cpp
static const uint8_t DHT_PIN = 4;                 // DHT11 DATA 脚
static const uint32_t READ_INTERVAL_MS = 2000;    // 采样间隔(会自动放大到不低于物理下限)
static const uint32_t STARTUP_DELAY_MS = 1200;    // 上电后首次采样的等待(DHT11 需约 1s 稳定)
static const uint32_t LED_OK_FLASH_MS = 50;       // 成功读数: 亮 50ms
static const uint32_t LED_FAIL_HALF_MS = 100;     // 失败告警: 亮100/灭100
static const uint8_t  FAIL_BLINK_AFTER = 3;       // 连续失败几次开始快闪
static const uint32_t STATS_INTERVAL_MS = 60000;  // 汇总行间隔
static const bool LED_ACTIVE_HIGH = true;         // 板载 LED 亮灭相反就改为 false
```

换传感器型号：把 `setup()` 里的 `DHTesp::DHT11` 改成 `DHTesp::DHT22`（AM2302 / RHT03 同 DHT22）。
型号选错会一直 `CHECKSUM`，因为两者的数据格式不同。

## 构建 / 烧录 / 看串口

```bash
pio run -e esp32dev                 # 编译
pio run -e esp32dev -t upload       # 编译并烧录
pio device monitor -e esp32dev      # 串口监视器 (115200)
pio run -e esp32dev -t erase        # 擦除整片 flash（连不上/反复重启时可用）
```

> 若终端里提示 `pio: command not found`，用绝对路径：
> `~/.platformio/penv/bin/pio run -e esp32dev`。

串口里 `monitor_filters = time, direct` 已配好，每行会带时间戳，方便看采样节奏。

## 常见问题

**LED 一直快闪，串口全是 `TIMEOUT`**
最常见。按顺序排查：① DATA 有没有接到 GPIO4；② 裸传感器是否漏了 4.7kΩ~10kΩ 上拉；
③ 供电是否共地、是否用的 3V3；④ 杜邦线是否过长/接触不良；⑤ 引脚是否写对（`DHT_PIN`）。

**偶尔一条 `CHECKSUM`，但绝大多数读数正常**
单总线上很常见，DHT11 时序容错差，偶发校验错属正常。本工程只在**连续**失败 ≥ 3 次时才快闪告警。

**串口读数一直不变、像卡住了**
采样间隔被压到了传感器物理下限以下。DHT11 最快约 1 Hz，DHTesp 在间隔不足时会**静默跳过**这次读取
（不报错，返回的还是上一次的旧值）。本工程已用 `max(READ_INTERVAL_MS, getMinimumSamplingPeriod())`
兜底，想改间隔请改 `READ_INTERVAL_MS` 而不是绕过这个兜底。

**湿度永远在 20~90、温度在 0~50 之间**
这是 DHT11 的量程，不是 bug。超出量程的读数没有意义，需要更宽量程请换 DHT22（0~100%RH / -40~125°C）。

**数值精度只有整数**
DHT11 出厂精度就是温度 ±2°C、湿度 ±5%RH，且多数模块只给整数（个别模块带一位小数）。
要 0.1 分辨率请上 DHT22/AM2302。

**上电第一次读数失败**
DHT11 上电后需要约 1s 稳定，本工程用 `STARTUP_DELAY_MS=1200` 已避开。若你的模块更慢，把这个值调大。

**`pio run` 报 `PermissionError: .../.platformio/platforms.lock`**
当前 DSH 会话的文件沙箱只允许写工作区（`.../esp32/DHT11`），而 PlatformIO 默认要在 `~/.platformio`
下加锁/装包。两种办法：① 放开沙箱权限后重跑；② 把 core 目录指到可写位置并复用已装好的包：

```bash
CORE="$TMPDIR/pio-core-dht11"
mkdir -p "$CORE" && ln -sfn ~/.platformio/platforms "$CORE/platforms" \
                       && ln -sfn ~/.platformio/packages "$CORE/packages"
PLATFORMIO_CORE_DIR="$CORE" ~/.platformio/penv/bin/pio run -e esp32dev
```

## 环境说明

**平台**：`espressif32@7.1.3` 在 `platformio.ini` 里 **钉住了 framework `4.20017.260907`（Arduino core 2.0.17）**，
这是该平台版本官方匹配的组合。原因见配置内注释：registry 上更新的 `4.30312.261001`（core 3.3.12）
在本平台下不可用——它的预编译库包要求 GCC 14/15，而本平台为经典 ESP32 指定的是 GCC 8.4。

该 framework 包约 246MB；**本机已随同级 `led` / `wifi` 项目装好**，几个工程共用同一份，无需重复下载。
换机器首次安装时慢链路上耗时较长，传输中断会报 `HTTPClientError`，**重试即可**（已下载部分会被复用）。

**驱动库**：`DHTesp`（registry 名 `DHT sensor library for ESPx`，作者 `beegee-tokyo`），钉住 `1.19`。
选它的原因：

1. 只依赖 Arduino 核心，不需要 `Adafruit Unified Sensor` 之类的传递依赖；
2. 内部是状态机式的时序读取，出错会给出 `TIMEOUT` / `CHECKSUM` 具体原因，比裸 `delayMicroseconds` 好排查；
3. 自带体感温度、露点换算，省得自己实现；
4. 读取时会进临界区（`portENTER_CRITICAL`）屏蔽任务切换，时序更稳。

## 目录结构

```
DHT11/
├── platformio.ini          # 板型/框架/库依赖/串口监视器配置
├── src/main.cpp            # 全部逻辑（采样排期 + 打印 + LED + 统计）
├── include/                # 本项目头文件目录（暂无内容）
├── lib/                    # 本项目私有库目录（暂无内容）
├── test/                   # PlatformIO 单元测试目录（暂无内容）
├── .vscode/                # PlatformIO IDE 的调试/推荐插件配置
└── README.md
```
