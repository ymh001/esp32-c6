#!/usr/bin/env python3
"""Export a fresh versioned flash package. Run using the ESP-IDF Python env."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,tempfile
p=argparse.ArgumentParser();p.add_argument('version');args=p.parse_args()
if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?',args.version):p.error('Use a semantic version without v')
project=Path(__file__).resolve().parents[1];build=project/'build'
version='v'+args.version;dest=project.parents[1]/'releases'/'pocket-home'/version
if dest.exists():p.error('Version already exists; choose a new version')
config=json.loads((build/'flasher_args.json').read_text())
if config['extra_esptool_args']['chip']!='esp32c6':p.error('Unexpected target')
name=f'pocket-home-esp32c6-{version}-merged.bin'
dest.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=dest.parent) as tmp:
    tmp=Path(tmp);image=tmp/name
    cmd=[sys.executable,'-m','esptool','--chip','esp32c6','merge_bin','--output',str(image)]+config['write_flash_args']
    for offset,file in sorted(config['flash_files'].items(),key=lambda x:int(x[0],0)):cmd.extend([offset,str(build/file)])
    subprocess.run(cmd,check=True)
    digest=hashlib.sha256(image.read_bytes()).hexdigest()
    (tmp/'SHA256SUMS').write_text(f'{digest}  {name}\n')
    private=(project/'config.local.h').exists()
    text=f'''# Pocket Home {version}

目标板：Waveshare ESP32-C6-Touch-AMOLED-2.16，16 MB Flash，480×480。
ESP-IDF 5.5.2，LVGL 9.6.0~1。镜像起始地址：`0x0`。

功能：应用桌面、翻页时钟、Grafana 家庭耗电、自动旋转与旋转锁定、控制中心、触屏 Wi-Fi 配网、SD 卡 FAT 文件读写。
{'本包含本地网络配置，仅供本人设备使用，不提交或上传。' if private else '无预置私有网络凭据，可在设备端配网。'}

## 烧录

先备份设备 Flash。此合并镜像会初始化分区间隙中的设置区；
以后保留配置升级应在源码项目使用 `idf.py -p PORT flash`，不要重复写合并镜像。

```bash
python -m esptool --chip esp32c6 --port /dev/cu.usbmodem1101 --baud 921600 write_flash 0x0 {name}
```

设备端操作：点击图标进入应用；底部上滑或点底部横条返回桌面；
下滑打开控制中心，上滑收起；Wi-Fi 卡片开关，右侧“选网络”进入配网。
亮屏时长默认常亮；设置自动熄屏后，第一次触摸只唤醒。

## 验证

构建和原生 LVGL 交互测试通过；实机验证状态见同目录 `VALIDATION.md`。
Energy refreshes every 60 seconds; source timestamps and stale/error states are shown. Wi-Fi 配网支持普通个人网络，暂不支持企业认证。

SHA-256：`{digest}`
'''
    (tmp/'README.md').write_text(text)
    dest.mkdir()
    for f in tmp.iterdir():f.rename(dest/f.name)
print(dest)
