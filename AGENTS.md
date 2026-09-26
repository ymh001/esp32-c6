# 固件开发约定

- 新固件项目位于 `firmware/pocket-home/`；`firmware/desk-clock/` 保留为原固件参考。
- `firmware/<project>/` 放源码、配置、测试和构建说明；`build/` 放临时产物并忽略。
- 烧录包导出到 `releases/<project>/v<version>/`。合并镜像命名为 `<project>-<chip>-v<version>-merged.bin`。
- 每个烧录包附 README，说明目标硬件、烧录地址/命令、版本、验证状态及 SHA-256；不覆盖已发布版本。
- Wi-Fi 等私有凭据放入已忽略的本地配置；含凭据的镜像仅供本地使用，不提交 Git。
- 目标板为 Waveshare ESP32-C6-Touch-AMOLED-2.16，480×480，无 PSRAM；采用分块绘制。
- 当前产品需求：应用桌面、翻页时钟、Grafana 家庭耗电、下滑控制中心（亮度/Wi-Fi/亮屏时长/旋转锁定）、设备端 Wi-Fi 配网。应用内不做左右滑切换。
