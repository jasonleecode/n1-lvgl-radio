# N1 收音机 · LVGL Radio

为斐讯 N1 / Debian 12 arm64 编写的轻量 RTL-SDR 收音机。界面使用已移植的 LVGL，接收和解调复用 `rtl_fm`，音频通过 PulseAudio 输出。无需 GNU Radio 或 Gqrx。

![应用界面](docs/preview.png)

## 功能

- 大号频率显示，支持直接输入 MHz、100Hz～1MHz 步进调台。
- FM 广播单声道、AM、USB、LSB、NFM；常用 FM / 中波 / 短波 / VHF 入口。
- 音量、静音、音频电平显示，以及最多 16 个命名收藏电台。
- 常规 R820T 调谐、Q 路直采、上变频频移、射频增益与 ppm 校准设置。
- FM 使用 240kHz 原生解调、增强 I/Q 降采样和 193 抽头抗混叠滤波，再输出 48kHz 单声道，抑制导频与立体声分量折叠造成的杂音。显式设置宽带 FM，避免 rtl_fm 的 wbfm 别名附带的 16kHz 调谐偏移，保留足够频偏余量。默认手动射频增益 25.4dB，可在设置中调整或切回自动。
- 独立接收线程，不阻塞 LVGL；调台和停止会清理子进程并释放接收棒。
- 收藏与设置原子保存到 `$XDG_CONFIG_HOME/n1-radio/settings.ini`，默认 `~/.config/n1-radio/settings.ini`。
- 启动器提供 256MiB 应用内存上限、32MiB 应用 swap 上限。不会更改系统 swap。

音频电平是 **PCM 音频电平，非 RF 信号强度**。首版没有频谱瀑布图、自动搜台、立体声或 RDS。

## 低频短波的硬件限制

普通 RTL2838 + R820T 的常规调谐约从 24MHz 开始。界面提供 AM / USB / LSB 解调，不表示任意接收棒都能接收低频短波：

- 低于 24MHz，需要接收棒本身具备 Q 路直采硬件，或外接上变频器。
- 直采请在“接收设置”中选择，仅用于 28.8MHz 以下；普通板卡的 Q 引脚可能没有连接天线。
- 使用上变频器时，填写设备实际频移。应用调谐硬件频率 = 界面频率 + 频移。
- 默认频移栏为常见的 125MHz 示例值，不代表检测到了上变频器。
- 软件不会自动打开 Bias-T。

## 使用

从 XFCE 的“应用菜单 → 多媒体 → N1 收音机”打开，或运行：

```sh
n1-radio
```

先退出 Gqrx、rtl_test 等占用接收棒的程序。点击“开始接收”，调到当地电台频率。点击数字可直接输入频率；“收藏当前”保存频率和解调模式，右侧 x 移除收藏。硬件输入方式为全局设置。

未接收时不占用 RTL2838；关闭窗口或停止接收会释放它。设备被占用、USB 掉线和音频服务错误会显示在状态栏，不会把设备打开失败显示为正常接收。

## 安装已编译版本

从本仓库 Releases 下载 arm64 压缩包，在 N1 上解压后：

```sh
sudo apt-get install --no-install-recommends rtl-sdr libsdl2-2.0-0 libpulse0
sudo python3 install.py
```

在解压目录运行安装命令。程序安装到 `/opt/n1-radio`，启动器安装到 `/usr/local/bin/n1-radio`。应用菜单中显示“N1 收音机”。不要以 root 运行图形应用，应在 n1 的 XFCE 会话启动。

## 构建

目标依赖：SDL2、PulseAudio、rtl-sdr。LVGL SDK 来自 N1 已安装的 `/opt/lvgl/install`，配置参考 `docs/lvgl-n1.defconfig`，对应 LVGL `10.0.0-dev`，提交 `58ae125776675b7dbbd2773ae06f02bc30fdde5d`。这是开发快照，并非承诺兼容所有 LVGL 9/10 版本。

推荐在 Mac 的 ARM64 Docker 中构建，避免 N1 内存不足。将 N1 的 `/opt/lvgl/install` 复制到构建机后：

```sh
sh scripts/build-docker.sh /absolute/path/to/lvgl/install
```

脚本保留构建容器与镜像，生成 `build/n1-radio`。已有 Debian arm64 构建环境可执行：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/lvgl/install -DCMAKE_BUILD_TYPE=Release
cmake --build build -j3
ctest --test-dir build --output-on-failure
```

字体 C 源码已包含，普通构建不需要 Node / Python 字体工具。常用中文字符分成两份字体，适配现有 SDK 的小字体索引布局。字体源自 Noto Sans CJK SC，许可证见 `assets/FONT-LICENSE.txt`。修改字体可用 `scripts/make-fonts.py`，需要 fontTools、lv_font_conv 和 Noto CJK 原始字体集合；不使用系统专有字体。

## 测试

- `radio-core-test`：频率与硬件范围、收藏与配置原子读写、损坏配置处理。
- `radio-dsp-test`：测试 1kHz 通带、19kHz 导频与 38kHz 立体声分量抑制，以及跨分块流式处理一致性。
- `radio-engine-test`：模拟 RTL 接收器和音频服务，测试接收、音量不重启设备、连续 8 次调台、停止、设备占用后重试和退出。
- `radio-hardware-check`：需要真实接收棒与 PulseAudio；在静音状态依次验证 FM / AM / USB / LSB / NFM 产生 PCM 音频数据。不会证明收到具体电台或验证低频直采硬件。
- `scripts/preview.py`：在独立 Xvfb 上渲染实际 LVGL 界面，生成截图，不接收无线电。

```sh
build/radio-hardware-check
```

实机测试中修复了退出接收进程时过早关闭管道的问题：现在先发送 SIGINT，并在 USB 清理期间排空管道，再关闭文件描述符。USB 供电或硬件掉线仍可能需要重插。

首次用户测试发现无声：接收端 PCM 正常但输出缓冲几乎一直为空。已移除自定义的零预缓冲 / 小缓冲，改用 PulseAudio 标准缓冲，兼容蓝牙输出。

用户已在 N1 上验证：无声和 FM 破音问题修复，整体测试 OK。

用户验收结果与最终构建信息记录在 `docs/validation.json`。按用户要求，仓库仅在用户测试确认后上传。

## 项目布局

- `src/main.c`：LVGL 界面与交互。
- `src/engine.c`：接收进程、PCM 音频与后台线程。
- `src/config.c`：配置、收藏和调谐参数验证。
- `scripts/n1-radio`：受内存上限保护的桌面启动器。
- `assets/`：桌面入口、图标与字体许可。

项目自身采用 MIT 许可证，见仓库根目录 `LICENSE`；LVGL 及字体保持各自原许可证。
