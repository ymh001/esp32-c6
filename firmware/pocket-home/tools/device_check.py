#!/usr/bin/env python3
"""Collect bounded USB logs and optionally run on-device diagnostics."""
import argparse, time
from pathlib import Path
import serial
p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/cu.usbmodem1101');p.add_argument('--seconds',type=int,default=25);p.add_argument('--commands',default='status,test');p.add_argument('--log',default='device-check.log');p.add_argument('--schedule',default='');a=p.parse_args()
s=serial.Serial();s.port=a.port;s.baudrate=115200;s.timeout=0.1;s.dtr=False;s.rts=False;s.open()
start=time.monotonic();sent=False;status_sent=False
scheduled=[(float(x.split(':',1)[0]),x.split(':',1)[1]) for x in a.schedule.split(',') if x]
with Path(a.log).open('wb') as f:
 while time.monotonic()-start<a.seconds:
  elapsed=time.monotonic()-start
  while scheduled and elapsed>=scheduled[0][0]:
   _,command=scheduled.pop(0);s.write(('\n'+command+'\n').encode())
  if elapsed>1 and not sent:s.write(('\n'+'\n'.join(a.commands.split(','))+'\n').encode());sent=True
  if elapsed>a.seconds-3 and not status_sent:s.write(b'\nstatus\n');status_sent=True
  data=s.read(max(1,s.in_waiting))
  if data:f.write(data);f.flush()
s.close()
text=Path(a.log).read_text(errors='replace')
for line in text.splitlines():
 if any(x in line for x in ['diagnostics','pocket-home','health','network:','Time synchronized','Error','ERROR','assert','abort','Guru','rst:','Touch','touch','Panel','I2C']):print(line)
