# DHT11

ESP32 上读 DHT11 温湿度、并把结果显示到 **SSD1306 OLED** 的 PlatformIO 工程（Arduino 框架），
与同级 `led` / `wifi` 项目同款板子、同款 framework 版本。

代码按功能拆成模块（配置 / 传感器 / 显示 / LED / 统计），`main.cpp` 只留启动顺序和采样排期，
详见下文「[代码结构](#代码结构模块划分)」。

## 硬件

| 项目 | 值 |
|---|---|
| 板子 | 经典 ESP32-D0WD-V3（Espressif ESP32 Dev Module），双核 240MHz |
| Flash | 4MB |
| 传感器 | DHT11（温湿度，单总线） |
| DATA 引脚 | **GPIO 4** |
| 显示屏 | **SSD1306 0.96″ 128×64，I2C**（4 针模块） |
| OLED SDA / SCL | **GPIO 21 / GPIO 22**（ESP32 经典默认 I2C 脚） |
| OLED 地址 | 0x3C（少数模块 0x3D，程序**启动时自动扫描**，两种都支持） |
| 状态 LED | **GPIO 2 板载 LED**（多数 ESP32 开发板如此，无需外接元件） |
| 下载口 | 板载 USB-UART 桥（macOS 上枚举为 `/dev/cu.usbserial-*`） |
| 采样间隔 | 5s（DHT11 物理上限约 1 Hz） |
| 固件占用 | Flash 39.2%（约 501 KB / 1280 KB），其中中文点阵字库 198 KB |

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

        ┌───────────┐
 3V3 ───┤ VCC       │
 GPIO21─┤ SDA       │ SSD1306 128×64 (I2C)
 GPIO22─┤ SCL       │
 GND ───┤ GND       │
        └───────────┘

        状态 LED（无需接线）: GPIO2 — 多数开发板板载
```

- **裸传感器（4 脚）必须加上拉电阻**（4.7kΩ~10kΩ，接在 DATA 与 VCC 之间），否则读出来全是 `TIMEOUT`。
- **三针模块**（VCC / DATA / GND，带小 PCB）通常已经板载上拉，可以省掉；再加一个也不影响。
- 供电用 **3V3** 最省事（DHT11 支持 3.3V~5.5V，DATA 电平跟着 VCC 走，用 5V 供电时 DATA 会输出 5V 电平，
  虽然多数情况能读，但不建议；要 5V 供电就在 DATA 与 ESP32 之间加电平转换或分压）。
- **OLED 和 DHT11 共用 3V3 / GND 即可**，0.96″ 四针模块一般自带 4.7kΩ 上拉，不用外加。
- 杜邦线别拉太长（建议 < 1m），线长 + 无上拉是 `TIMEOUT` 和 I2C 扫不到设备的两大主因。

## 行为

1. **周期采样**：每 `READ_INTERVAL_MS`（当前 5s，且自动放大到不低于传感器物理下限）读一次，
   串口打印 **温度 / 湿度 / 体感温度（heat index）/ 露点（dew point）**。
2. **非阻塞**：`loop()` 里只用 `millis()` 排期，没有阻塞式 `delay()`，方便以后往上加 WiFi 上报、
   按键之类的任务。
3. **失败可观测**：读取失败会打印具体原因（`TIMEOUT` / `CHECKSUM`）和连续失败次数。
4. **统计**：累计成功/失败次数、温度与湿度的运行极值，每 60s 打一行汇总。
5. **OLED 显示**：数据变化时才刷屏（省 I2C 带宽、避免闪屏）；读失败时**保留最后一次有效数值**，
   底部把失败原因写出来，不会突然变空白。
6. **状态 LED（GPIO 2 板载）**：

| LED 表现 | 含义 |
|---|---|
| 每读到一次数据闪一下（50ms） | 正常，读数已刷新 |
| **100ms 快闪**（连续失败 ≥ 3 次） | 传感器没接好 / 缺上拉 / 线太长 / 供电不对 |

### OLED 显示布局（128×64）

```
┌──────────────────────────────┐
│ 温度              湿度        │   wqy12 点阵中文
│ 24.0°C          46.0%        │   logisoso20 大号数字 + helvB08 单位
│ 露点 11.7°C            正常    │   右下角状态：正常 / 失败N / 失败N次 TIMEOUT
└──────────────────────────────┘
```

上电后到第一次读数之间显示「DHT11 温湿度 / 等待数据... / OLED OK」。
连续失败 ≥ 3 次时，底行换成 `失败3次 TIMEOUT` 这样的提示，同时板上 LED 快闪。

### 串口输出示例

```
=========================================
ESP32-D0WD-V3 (ESP32 Dev Module) — DHT11 + OLED
芯片: ESP32-D0WD-V3  双核 240 MHz  Flash 4096 KB
[dht11] 型号 DHT11  DATA=GPIO4  物理最快采样 1000 ms  本工程间隔 5000 ms
[dht11] 量程: 温度 0~50 °C(0 位小数)  湿度 20~90 %RH
LED(GPIO2): 成功短闪 50ms / 连续失败3次后 100ms 快闪
[i2c] 扫描总线 (SDA=GPIO21, SCL=GPIO22)...
[i2c]   发现设备: 0x3C
[oled] SSD1306 128x64 就绪, I2C 地址 0x3C
=========================================
[dht11] 1200 ms 后开始首次采样
[dht11] #1  温度  24.0 °C   湿度  46.0 %RH   体感  24.1 °C   露点  11.7 °C
[dht11] #2  温度  24.0 °C   湿度  46.0 %RH   体感  24.1 °C   露点  11.7 °C
[dht11] [!!] 第 1 次读取失败: TIMEOUT  (连续 1 次)
[dht11] 汇总: 运行 60s  成功 11 次  失败 1 次(连续 0)
[dht11]       温度 23.0~25.0 °C   湿度 45.0~47.0 %RH
```

### 可调常量（`include/config.h`）

所有引脚、周期、阈值集中在**一个文件**里，其它模块只引用不硬编码，换硬件只改这一处：

```cpp
constexpr uint8_t  DHT_PIN = 4;               // DHT11 DATA 脚
constexpr uint32_t READ_INTERVAL_MS = 5000;   // 采样间隔(会自动放大到不低于物理下限)
constexpr uint32_t STARTUP_DELAY_MS = 1200;   // 上电后首次采样的等待(DHT11 需约 1s 稳定)

constexpr uint8_t  LED_PIN = 2;               // 板载 LED
constexpr bool     LED_ACTIVE_HIGH = true;    // 板载 LED 亮灭相反就改为 false
constexpr uint32_t LED_OK_FLASH_MS = 50;      // 成功读数短闪
constexpr uint32_t LED_FAIL_HALF_MS = 100;    // 失败告警半周期
constexpr uint8_t  FAIL_BLINK_AFTER = 3;      // 连续失败几次开始快闪

constexpr uint8_t  OLED_SDA_PIN = 21;         // OLED SDA
constexpr uint8_t  OLED_SCL_PIN = 22;         // OLED SCL
constexpr uint32_t OLED_I2C_HZ = 400000;      // SSD1306 支持 400kHz
constexpr uint32_t OLED_RETRY_MS = 30000;     // 没认到屏幕时每 30s 重试初始化

constexpr uint32_t STATS_INTERVAL_MS = 60000; // 汇总行间隔
```

「换屏幕型号 / 尺寸」改的是 `src/oled_display.cpp` 里那一行构造器：

```cpp
// 当前：SSD1306 128x64
U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
// 0.91" 128x32：换成 U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C
// 1.3" SH1106 ：换成 U8G2_SH1106_128X64_NONAME_F_HW_I2C
// 旋转 180° ：把第一个参数 U8G2_R0 改成 U8G2_R2
```

> 构造器参数顺序是 `(rotation, reset, clock, data)` —— **先 clock(SCL) 后 data(SDA)**，别写反。
> U8g2 收到这两个脚后会自己调 `Wire.begin(data, clock)`，所以引脚只在这里给一次。

「换传感器型号」改的是 `src/dht_sensor.cpp` 里的 `DHTesp::DHT11`，改成 `DHTesp::DHT22`
（AM2302 / RHT03 同 DHT22）。型号选错会一直 `CHECKSUM`，因为两者的数据格式不同。

## 代码结构（模块划分）

`main.cpp` 只负责**启动顺序**和**采样排期**，硬件操作都在各自模块里：

| 文件 | 职责 | 依赖 |
|---|---|---|
| `include/config.h` | 引脚、周期、阈值 —— 唯一配置入口 | — |
| `include/reading.h` | 一次读数的数据结构（温度/湿度/体感/露点） | — |
| `include/dht_sensor.h` + `src/dht_sensor.cpp` | DHT11 驱动：读一次、算体感与露点、打印读数行 | DHTesp、config |
| `include/oled_display.h` + `src/oled_display.cpp` | SSD1306：I2C 扫描、地址自适应、三行布局、脏标记刷屏、掉线重试 | U8g2、Wire、config、reading |
| `include/led_indicator.h` + `src/led_indicator.cpp` | 板载 LED：成功短闪 / 连续失败快闪，全程非阻塞 | config |
| `include/stats.h` + `src/stats.cpp` | 运行统计：成功/失败计数、连续失败、极值、周期汇总 | config |
| `src/main.cpp` | 启动顺序 + 采样排期（约 120 行） | 以上全部 |

**依赖方向恒为 `main → 各模块 → config/reading`，模块之间互不依赖**：
显示模块不认识 DHT 驱动，LED 不认识统计，全部通过 `main.cpp` 串联。
所以换屏不用碰传感器代码，换传感器不用碰显示代码。

几个刻意的接口设计：

- `sensor::read(Reading&)` 用出参 + 返回 `Status`，调用方拿不到「半个读数」；
- `oled::update(...)` 只打脏标记，真正绘制统一在 `oled::task()` 里做，
  调用方只管喂数据，不用关心什么时候刷屏；
- `led::task(now, failStreak)` 只接收「连续失败次数」这个数字，
  内部自己跟阈值比较，不关心失败原因；
- `stats::task(now, lastError)` 内部自己判断汇总间隔，main 里不用再管计时。

`include/` 目录 PlatformIO 默认就在头文件搜索路径里
（实测编译参数含 `-Iinclude`、`-Isrc`），所以直接 `#include "config.h"` 即可，
`platformio.ini` 不需要额外的 `build_flags`。

行为与拆分前**完全一致**（纯结构调整）：编译后 Flash 从 512849 变 513441 字节，只多了约 0.6KB。

## 构建 / 烧录 / 看串口

```bash
pio run -e esp32dev                 # 编译
pio run -e esp32dev -t upload       # 编译并烧录
pio device monitor -e esp32dev      # 串口监视器 (115200)
pio run -e esp32dev -t erase        # 擦除整片 flash（连不上/反复重启时可用）
```

> 若终端里提示 `pio: command not found`，用绝对路径：
> `~/.platformio/penv/bin/pio run -e esp32dev`。
>
> **烧录/擦除前必须先关掉串口监视器**，否则会报
> `could not open port ...: Operation not permitted`（macOS 对已被占用的串口设备返回 EPERM，不是权限问题）。

串口里 `monitor_filters = time, direct` 已配好，每行会带时间戳，方便看采样节奏。

## 常见问题

### DHT11 部分

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

### OLED 部分

**屏幕全黑，串口里 `[i2c] 没扫到任何设备`**
I2C 总线没通。按顺序排查：① VCC/GND 是否接好、模块是否真的通电；
② SDA/SCL 有没有接反（SDA→GPIO21、SCL→GPIO22）；③ 杜邦线过长或虚接；
④ 模块是否需要外部上拉（大多数 0.96″ 模块自带 4.7kΩ）。
程序**每 30s 会自动重试一次初始化**，所以你接好线之后不用重新上电，串口会自己出现
`[oled] 重试初始化...` 然后就能亮。

**串口扫到的是 `0x3D` 而不是 `0x3C`**
正常。有些模块出厂地址是 0x3D，程序用扫到的地址通信，不用改代码。

**扫到了设备，但屏幕花屏 / 图像整体偏移几列**
多半是 **SH1106** 而不是 SSD1306（1.3″ 屏常见）。把构造器换成
`U8G2_SH1106_128X64_NONAME_F_HW_I2C` 即可，SSD1306 的显存寻址和它不一样。

**只显示上面一半、另一半是噪点**
买的是 0.91″ 128×32 的屏，但用了 128×64 的构造器。换成 `U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C`，
并相应精简布局（128×32 只有 4 行 8px 字）。

**`°C` 的度数符号没显示出来**
说明当前单位字体缺 `°` 字形。本工程用的是 `u8g2_font_helvB08_tf`（191 个字形，覆盖 Latin-1，含 `°`）；
如果换成 `_tr` 结尾的字体（只有 ASCII 32~127），`°` 就会静默丢掉，显示成 `24.0C`，
把字体名换回 `_tf` 结尾即可。

**屏幕刷新一次要多久**
全屏 128×64 走 400kHz I2C 大约 20~30ms。本工程只在数据变化时刷（5s 一次），对 DHT11 的
位读取没有影响（两者串行执行，不并发）。

### 环境 / 工具链部分

**`pio run` 报 `PermissionError: .../.platformio/platforms.lock`**
当前 DSH 会话的文件沙箱只允许写工作区（`.../esp32/DHT11`），而 PlatformIO 默认要在 `~/.platformio`
下加锁/装包。两种办法：① 放开沙箱权限后重跑；② 把 core 目录指到可写位置并复用已装好的包：

```bash
CORE="$TMPDIR/pio-core-dht11"
mkdir -p "$CORE" && ln -sfn ~/.platformio/platforms "$CORE/platforms" \
                       && ln -sfn ~/.platformio/packages "$CORE/packages"
PLATFORMIO_CORE_DIR="$CORE" ~/.platformio/penv/bin/pio run -e esp32dev
```

**下载依赖特别慢 / 卡住不动（国内网络）**
PlatformIO 的包走 `dl.registry.platformio.org`，国内直连经常只有几十 KB/s。
**命令行工具不会自动使用 macOS 的「系统代理」**，必须显式传环境变量（端口按自己的代理改）：

```bash
export HTTPS_PROXY=http://127.0.0.1:7890 HTTP_PROXY=http://127.0.0.1:7890
~/.platformio/penv/bin/pio run -e esp32dev
```

实测同一台机器：直连约 34 KB/s，走代理约 670 KB/s（差 20 倍），
U8g2 这个 13.5MB 的包直连要 6 分钟以上，走代理 20 秒。

## 环境说明

**平台**：`espressif32@7.1.3` 在 `platformio.ini` 里 **钉住了 framework `4.20017.260907`（Arduino core 2.0.17）**，
这是该平台版本官方匹配的组合。原因见配置内注释：registry 上更新的 `4.30312.261001`（core 3.3.12）
在本平台下不可用——它的预编译库包要求 GCC 14/15，而本平台为经典 ESP32 指定的是 GCC 8.4。

该 framework 包约 246MB；**本机已随同级 `led` / `wifi` 项目装好**，几个工程共用同一份，无需重复下载。
换机器首次安装时慢链路上耗时较长，传输中断会报 `HTTPClientError`，**重试即可**（已下载部分会被复用）。

**驱动库 1：`DHTesp`**（registry 名 `DHT sensor library for ESPx`，作者 `beegee-tokyo`），钉住 `1.19`。
选它的原因：

1. 只依赖 Arduino 核心，不需要 `Adafruit Unified Sensor` 之类的传递依赖；
2. 内部是状态机式的时序读取，出错会给出 `TIMEOUT` / `CHECKSUM` 具体原因，比裸 `delayMicroseconds` 好排查；
3. 自带体感温度、露点换算，省得自己实现；
4. 读取时会进临界区（`portENTER_CRITICAL`）屏蔽任务切换，时序更稳。

**驱动库 2：`U8g2`**（作者 `olikraus`），钉住 `2.36.18`。选它的原因：

1. 一个库覆盖 SSD1306 / SH1106 / 各种尺寸，换屏只改一行构造器；
2. 自带点阵中文字体（`wqy*_gb2312`）、大号数字字体（`logisoso*_tn`），
   中文标签和数字大字不用自己做字模；
3. 内置 I2C 地址设置接口，配合启动扫描能同时兼容 0x3C / 0x3D 模块。

**Flash 占用**：约 501 KB / 1280 KB（39.2%）。其中：
中文点阵 `u8g2_font_wqy12_t_gb2312` **198 KB**、单位字体 `helvB08_tf` 2.1 KB、
数字字体 `logisoso20_tn` 0.4 KB、U8g2 代码本体约 30 KB。
如果哪天要省空间，把中文标签换成英文（用 `u8g2_font_6x12_tf` 之类）能省掉这 198 KB。

**代码里几个刻意的选择**（容易被当成"多此一举"，其实都是踩过坑的）：

| 写法 | 原因 |
|---|---|
| 构造器传 `clock` 在前、`data` 在后 | U8g2 的参数顺序是 `(rotation, reset, clock, data)`，写反了就是黑屏 |
| `setI2CAddress(addr << 1)` | U8g2 要 **8 位**地址，直接写 0x3C 会通信失败 |
| 单位字体用 `_tf` 不用 `_tr` | `_tr` 只有 ASCII，没有 `°` 字形 |
| 启动先做 I2C 扫描 | 0x3C/0x3D 都能自动适配，扫不到时串口有明确提示，不用盲猜 |
| 只在数据变化时刷屏 | 5s 一次足够，省 I2C 带宽也避免闪烁 |

## 目录结构

```
DHT11/
├── platformio.ini          # 板型/框架/库依赖(DHTesp + U8g2)/串口监视器配置
├── include/                # 模块接口 + 配置（PlatformIO 默认已在 -I 路径里）
│   ├── config.h            #   引脚、周期、阈值（唯一配置入口）
│   ├── reading.h           #   一次读数的数据结构（传感器与显示共享）
│   ├── dht_sensor.h        #   DHT11 模块接口
│   ├── oled_display.h      #   OLED 显示模块接口
│   ├── led_indicator.h     #   板载 LED 模块接口
│   ├── stats.h             #   运行统计模块接口
│   └── README              #   PlatformIO 自动生成的目录说明（保留）
├── src/                    # 模块实现 + 应用入口
│   ├── main.cpp            #   启动顺序 + 采样排期（只做编排，约 120 行）
│   ├── dht_sensor.cpp      #   DHT11 驱动（DHTesp 封装）
│   ├── oled_display.cpp    #   SSD1306 绘制（U8g2）
│   ├── led_indicator.cpp   #   板载 LED 状态指示
│   └── stats.cpp           #   计数 / 极值 / 周期汇总
├── lib/                    # 本项目私有库目录（暂无内容）
├── test/                   # PlatformIO 单元测试目录（暂无内容）
├── .vscode/                # PlatformIO IDE 的调试/推荐插件配置
└── README.md
```

各文件行数（重构后，共约 730 行）：

```
include/reading.h          14
include/led_indicator.h    26
include/oled_display.h     34
include/stats.h            34
include/config.h           36
include/dht_sensor.h       42
src/led_indicator.cpp      61
src/dht_sensor.cpp         86
src/stats.cpp              86
src/main.cpp              124   ← 重构前是 370 行
src/oled_display.cpp      185
```

