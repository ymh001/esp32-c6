# Pocket Home v0.1.2

目标板：Waveshare ESP32-C6-Touch-AMOLED-2.16，16 MB Flash，480×480。
ESP-IDF 5.5.2，LVGL 9.6.0~1。镜像起始地址：`0x0`。

功能：应用桌面、翻页时钟、耗电占位、控制中心、触屏 Wi-Fi 配网。
本包含本地网络配置，仅供本人设备使用，不提交或上传。

## 烧录

先备份设备 Flash。此合并镜像会初始化分区间隙中的设置区；
以后保留配置升级应在源码项目使用 `idf.py -p PORT flash`，不要重复写合并镜像。

```bash
python -m esptool --chip esp32c6 --port /dev/cu.usbmodem1101 --baud 921600 write_flash 0x0 pocket-home-esp32c6-v0.1.2-merged.bin
```

设备端操作：点击图标进入应用；底部上滑或点底部横条返回桌面；
下滑打开控制中心，上滑收起；Wi-Fi 卡片开关，右侧“选网络”进入配网。
亮屏时长默认常亮；设置自动熄屏后，第一次触摸只唤醒。

## 验证

构建和原生 LVGL 交互测试通过；实机验证状态见同目录 `VALIDATION.md`。
耗电接口尚未接入，数值明确显示占位。Wi-Fi 配网支持普通个人网络，暂不支持企业认证。

SHA-256：`2d95b803dde52df731f009bc5433f596535e9476b9b4e596ccd7c03261aeb8f3`
