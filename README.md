# ESP32-C6 Touch AMOLED 2.16

Waveshare **ESP32-C6-Touch-AMOLED-2.16** 的两个独立固件项目。它们使用同一款硬件，各自拥有源码、配置和构建入口；选择其中一个烧录运行。

## 选择固件

| 项目 | 定位 | 主要功能与交互 | 源码 / 构建说明 |
| --- | --- | --- | --- |
| **Desk Clock（旧版）** | 原有多页面桌面时钟固件，单独保留 | 时钟、农历/月历、用电等页面，左右滑动切页；另有设备控制和语音相关实现 | [firmware/desk-clock](firmware/desk-clock/README.md) |
| **Pocket Home（新版）** | 手机应用桌面风格的重写固件，v0.2.3 增加 SD 文件接口，已通过 32 GB 实卡读写验证 | 桌面图标、翻页时钟、Grafana 家庭耗电、下拉控制中心、触屏 Wi-Fi 配网、自动旋转/锁定、电池电量；不使用左右滑动切换应用 | [firmware/pocket-home](firmware/pocket-home/README.md) |

Pocket Home 尚未迁移 Desk Clock 的所有功能；两者不是同一固件的新旧目录副本。新增功能和修复应在对应项目内进行。

## 目录结构

```text
firmware/
  desk-clock/                 # 旧版独立固件源码
  pocket-home/                # 新版独立固件源码
    build/                    # 本地临时编译产物，忽略
hardware/                     # 共用硬件资料、引脚和限制
deploy/                       # 现有服务端部署参考
releases/
  <project>/v<version>/        # 各项目独立的版本说明、校验和与本地烧录包
```

使用 ESP-IDF，在所选项目目录内执行 `idf.py build`；具体 SDK 版本、配置与烧录命令见各项目 README。不能在仓库根目录直接编译，也不共用项目间的 `build/`、`sdkconfig` 或依赖下载目录。

## 源码与烧录包

- 源码按独立目录保存在同一个仓库的 `main` 分支，旧版 Git 历史保留。
- 烧录包命名为 `<project>-<chip>-v<version>-merged.bin`，按项目、版本分别导出；不覆盖已有版本。
- 本仓库当前上传源码、发布说明、校验和及验证记录。**含个人凭据的 BIN 不上传**，版本目录里不一定有可下载镜像；请按项目说明自行配置和编译。
- 本地 Wi-Fi 密码、Grafana token、设备备份、构建产物和日志均排除在 Git 之外。配置模板可以提交，真实凭据不可提交。
- 切换固件前备份设备数据，遵循目标固件自己的分区和烧录说明，不假设两套固件的设置互通。

更多资料：[硬件说明](hardware/WAVESHARE_ESP32_C6_TOUCH_AMOLED_2_16_HARDWARE.md)、[候选项目调研](firmware/CANDIDATE_PROJECTS.md)、[烧录包约定](releases/README.md)。
