# Pocket Home

这是本仓库的**新版独立固件**，当前版本 **v0.2.3**。旧版多页面时钟固件见 [Desk Clock](../desk-clock/README.md)，其源码单独保留。两者并非功能完全相同的版本升级。

Waveshare ESP32-C6-Touch-AMOLED-2.16 的独立固件。480×480 AMOLED，ESP-IDF 5.5.2、LVGL 9.6.0~1。

## 使用

- 开机进入应用桌面，点击“时钟”或“耗电”。应用内不左右滑切换。
- 下滑打开控制中心，上滑收起；应用底部上滑或点击横条回桌面。KEY 按键也可回桌面/唤醒。
- 控制中心：亮度 5–100%、Wi-Fi 真正开关、亮屏时长 30/60/120/300 秒或常亮（默认）。
- 点击“选网络”→搜索→选择→输入密码→连接。仅保存连接成功的网络，重启自动重连。
- 普通个人 Wi-Fi 支持；企业认证、网页认证和隐藏网络手动输入暂不支持。设备只使用 2.4 GHz。
- 自动熄屏后首次触摸只唤醒，第二次触摸才操作。熄屏不进入深度睡眠，Wi-Fi/RTC 继续工作。
- 时区为中国标准时间，Wi-Fi 连通后自动 NTP 校时并写入 RTC。
- 家庭耗电通过 Grafana 每 60 秒刷新今日用电、今日电费、剩余电费，电价遵循面板的 1.10 元/度。底部显示源数据同步时间；断网、更新失败或超过 10 分钟未同步会标注状态。无值显示 `--`，失败保留上次结果，跨日不显示昨天缓存的今日值。
- 复用旧固件 QMI8658 方向检测与防抖，支持四向自动旋转。控制中心可锁定当前方向，锁定设置和方向重启保留。

## 构建与烧录包

```bash
source /Users/yemenghao/projects/esp/activate-esp-idf-v5.5.2.sh
idf.py build
python tools/export_release.py 0.2.3
```

`build/` 保存临时产物。烧录包导出到仓库的 `releases/pocket-home/v<版本>/`，带合并镜像、说明和 SHA-256；已有版本不覆盖。
合并镜像从 `0x0` 写入并重置设置区；日常保留设置升级用 `idf.py -p PORT flash`。
首次刷写前备份完整 16 MB Flash；本机备份保存在忽略的 `firmware_backup/`。

首次默认网络可通过 `config.example.h` 复制为 `config.local.h` 设置；不配置也能编译，通过设备配网。
本地配置和含网络密码的镜像不提交 Git。忘记网络后不会再次回填编译时的默认凭据。
设置与网络分别保存于 NVS 的 `pocket_ui` 和 `pocket_net` 命名空间。

## 结构与扩展

- `components/board/`：来自原项目的电源、屏幕、触控、RTC、IMU 驱动；未包含音频服务。
- `main/ui/`：显示移植、应用桌面、手势、控制中心及 Wi-Fi 配网界面。
- `main/apps/`：独立应用；新增应用在 `apps.cpp` 注册名称、图标和创建/更新/销毁回调。
- `main/core/`：后台 Wi-Fi 队列与状态快照、NVS 设置、校时、串口诊断。
- `main/fonts/`：字体与翻页图块，Source Han Sans 遵循所附 SIL OFL。
- `host_tests/`：同一套真实 LVGL 界面的原生集成测试与截图，不是另写的网页仿稿。

UI 只在主任务调用 LVGL。Wi-Fi 在独立任务扫描/连接，通过命令队列和快照交互。
显示使用两块 32 行 RGB565 DMA 绘制缓冲与一块等大的旋转缓冲，总计 90 KiB，绘制和传输可重叠执行。LVGL 内存池为 64 KiB；翻页数字使用 Flash 中的上下半片，避免大型透明变换图层。
离开应用销毁对象、取消动画；控制中心作为系统覆盖层保留应用状态。

## 验证

在 ESP-IDF 已解析依赖后：

```bash
cmake -S host_tests -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j8
(cd build-host && ./data_test && ./sd_files_test && ./ui_test)
```

测试使用 64 KiB LVGL 内存池，覆盖触摸导航、手势、虚拟键盘、连接提交、Wi-Fi 开关、熄屏唤醒和多次页面生命周期。
截图写入 `build-host/screens/*.ppm`。桌面测试替代网络服务，用于验证 UI；真实无线功能通过设备日志检查。

USB 串口诊断（115200，换行结尾）：`status`、`home`、`clock`、`energy`、`control`、`wifi`、`wifi-off`、`wifi-on`、`test`。
`test` 做 30 次页面切换、亮度写入与内存检查，完成后恢复亮度/时长并返回桌面。

字体重新生成需要 Node.js/npm、Pillow 和 Source Han Sans；执行 `bash tools/generate_fonts.sh`。
16px 完整中文字库保留用于显示任意常见中文 SSID；24px 仅收录界面文字。

## v0.1.1

补齐小字号中文字库中的全角冒号、逗号、省略号等标点；构建辅助脚本生成界面字符清单，原生测试检查每个界面字符在 16px/24px 字库中都有真实字形。

## v0.1.2

控制中心 Wi-Fi 卡片增加 IP 地址副标题；断开或连接中显示状态，避免残留旧 IP。

## v0.2.0

接入 Grafana 总表耗电，新增四向自动旋转与旋转锁定。HTTPS 请求在独立任务执行并校验服务器证书；设备使用专用 Viewer 服务账号 token，不保存管理员密码。

在忽略的 `config.local.h` 配置 `POCKET_GRAFANA_URL`（完整 `/api/ds/query` URL）及 `POCKET_GRAFANA_TOKEN`。查询使用 `home-history` 数据源中的 `energy_totals` 和 `meter_summary`；SQL 位于 `main/core/energy.cpp`。未配置接口时仍可编译。

`data_test` 覆盖数据解析、空值、错误响应、负余额和旋转像素排列；构建此测试需设置 CMake 的 `POCKET_IDF_PATH`，默认使用环境变量 `IDF_PATH`。原生交互测试增加四向触摸、锁定切换和旧数据状态；实机 `test` 同时覆盖四向绘制，结束后恢复设置。

## v0.2.1

优化整页刷新：32 行双缓冲替代 16 行单缓冲，每页分块从 30 次降为 15 次；状态文字不变时不再重复分配和重绘。旋转缓冲仅在前一笔 DMA 已完成后使用，避免覆盖正在传输的像素。

同一实机切页测试中，各页面整页刷新耗时中位数降低约 34–49%。`refresh` 串口日志记录刷新提交耗时、DMA 等待、分块数与像素数。仍采用分块刷新，不保证屏幕上完全没有扫描感；具体测试数据见 v0.2.1 烧录包的验证记录。

## v0.2.2

补上电池图标和电量百分比，覆盖桌面、应用和系统覆盖页。复用现有 AXP2101 电压及充电完成状态估算，每 5 秒采样；不高于 20% 时图标变红。没有电池或读取失败时显示 `--%`，不把失败当作零电量。百分比是现有电压曲线的估计值，并非精密电量计读数。


## v0.2.3：SD 卡读写

**验证状态：代码和主机文件测试通过；重新连接设备后，32 GB 实卡挂载、6 次 8 KB 写入/回读校验和卸载重挂通过。**
排查记录见 `releases/pocket-home/v0.2.3/VALIDATION.md`（仓库根目录下）。

卡槽使用 SPI2，与 AMOLED 共用 GPIO0/1/2，SD CS 为 GPIO6，LCD CS 为 GPIO15。
先将两个片选置高、初始化一次总线，挂载 SD 使其进入 SPI 模式，再初始化屏幕。
普通数据传输由 ESP-IDF 的 SPI 总线仲裁串行执行；挂载/卸载先排空 LCD DMA。
SD 时钟先用 1 MHz，LCD 仍为 40 MHz QSPI。保留设备现有 40–160 MHz CPU 动态调频，
并将其加入默认构建配置，避免干净构建退回固定频率。

- 挂载点 `/sdcard`，支持 FAT16/FAT32、UTF-8 长文件名。
- 缺卡、文件系统不支持或挂载失败时仍启动界面；**不会格式化**。
- ESP-IDF 5.5.2 自带 FatFs 未启用 exFAT；本版不支持 exFAT/NTFS。
- 当前提供固件文件接口及串口诊断，没有新增文件管理器界面，也不是 USB 读卡器。
- 建议关机插拔卡。没有卡检测引脚，不支持无条件热插拔；通电插入尚未进入 SPI 模式
  的卡可能干扰屏幕总线。卸载不关闭卡电源。

USB 串口（115200，命令以换行结束）：

| 命令 | 用途 |
| --- | --- |
| `sd-status` | 挂载状态、卡容量、文件系统总量/剩余空间 |
| `sd-ls` | 列出根目录，最多 64 项 |
| `sd-read 路径` | 读取文件前 512 字节，以十六进制和 ASCII 输出 |
| `sd-write 新路径 文本` | 新建文件并落盘；已有同名文件时拒绝覆盖，路径不含空格 |
| `sd-test` | 随机文件写入 8 KB，关闭重开逐字节校验，成功后删除测试文件 |
| `sd-unmount` | 关闭挂载，不释放屏幕共用的 SPI 总线 |
| `sd-mount` | 在主任务暂停 LCD 提交期间重新挂载 |

路径相对 `/sdcard`，可读子目录文件；拒绝绝对路径、上级路径和 FAT 路径别名。
串口整行最多 383 字节，超长整行丢弃，避免执行被截断的写入命令。
自检使用独占创建，遇到同名文件换名，失败时保留自检文件便于排查。

文件接口位于 `components/board/include/board_sd.h`，由 UI/main 任务调用；
不要从其他任务并发调用挂载、卸载或 LCD 操作。
纯文件 IO 与路径测试位于 `host_tests/sd_files_test.cpp`。
共享总线初始化顺序参考 ESP-IDF 本地文档 `docs/en/api-reference/peripherals/sdspi_share.rst`。
