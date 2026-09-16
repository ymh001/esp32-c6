# HA voice services on DH4300 Plus

NAS project: `/volume2/docker/ha-voice/docker-compose.yaml`.

- Whisper: Chinese recognition, multilingual base-int8 CPU model, port 10300.
- Piper: Chinese zh_CN-huayan-medium voice, port 10200.
- Persistent models: `./whisper/data` and `./piper/data`.
- ARM64 images, restart unless stopped, bounded logs, CPU/memory limits.
- Ports bind to NAS LAN address 192.168.3.2.

Run from the project directory:

```sh
sudo docker compose up -d
sudo docker compose ps
sudo docker compose logs --tail=50
```

In Home Assistant, add two Wyoming Protocol integrations:
192.168.3.2:10300 (recognition), 192.168.3.2:10200 (synthesis).
Then choose them in a Chinese Assist pipeline. These are Wyoming TCP services,
not browser pages. Initial startup downloads models; wait for services to be ready.

## Verified deployment, 2026-09-15

Both services healthy, ARM64 images verified. Wyoming Describe responds on both
LAN endpoints. Piper synthesized `打开客厅灯` in 3.58 seconds; Whisper transcribed
the generated audio exactly as `打开客厅灯` in 2.09 seconds. This synthetic loopback
checks functionality, not real microphone/noise accuracy. No HA device action ran.

Image IDs used:
- Whisper: sha256:efeecfed9316c203350e3efba3bf2cbc45b0172b4646ee02a2a0534382a25024
- Piper: sha256:e9083a600a739b4b9c633684efe5952be4105ca1c10b5a62f8dfb28afb3beac3

## 外网屏幕

外网屏幕只连接 HA 的 HTTPS/WSS 入口 `ha.hz.leaflab.cn:8888`。
本 Compose 的两个端口用于 HA 在 NAS 内网访问，保持现有绑定。
Whisper/Piper 的 HTTPS 子域名目前返回 502，不是可用的 Wyoming 连接入口。
