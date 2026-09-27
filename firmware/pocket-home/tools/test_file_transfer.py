#!/usr/bin/env python3
"""On-device transfer acceptance test. Requires local serial access; never prints access codes.
Creates only uniquely named test files and removes them afterwards.
"""
import argparse,hashlib,http.client,json,os,re,threading,time,urllib.parse,urllib.request,urllib.error,uuid
import serial
p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/cu.usbmodem101');p.add_argument('--log',default='/tmp/pocket-file-transfer.log');a=p.parse_args()
s=serial.Serial();s.port=a.port;s.baudrate=115200;s.timeout=.1;s.dtr=False;s.rts=False;s.open()
lines=bytearray();stop=False;lock=threading.Lock()
def capture():
 with open(a.log,'wb',buffering=0) as f:
  while not stop:
   d=s.read(8192)
   if d:
    f.write(d)
    with lock:lines.extend(d)
thread=threading.Thread(target=capture);thread.start()
def log():
 with lock:return lines.decode(errors='replace')
def command(c):s.write(('\n'+c+'\n').encode())
def until(fn,seconds=30):
 start=time.monotonic()
 while time.monotonic()-start<seconds:
  v=fn()
  if v:return v
  time.sleep(.2)
 raise AssertionError('Timed out waiting for device')
code='';base='';created=[]
def api(path,method='GET',data=None,key=None):
 req=urllib.request.Request(base+path,data=data,method=method,headers={'X-File-Key':code if key is None else key,'Content-Type':'application/octet-stream'})
 try:
  with urllib.request.urlopen(req,timeout=120) as r:return r.status,r.read()
 except urllib.error.HTTPError as e:return e.code,e.read()
def endpoint(name):return '/api/file?name='+urllib.parse.quote(name,safe='')
try:
 command('status');time.sleep(3);command('transfer-on')
 def connection():
  command('transfer-status');m=re.findall(r'enabled=1 running=1 busy=\d url=http://([\d.]+)/ code=(\d{8})',log());return m[-1] if m else None
 ip,code=until(connection,45);base='http://'+ip
 print('File server ready',base,flush=True)
 assert api('/api/list?dir=',key='invalid')[0]==401
 assert api('/api/file?name=..%2Foutside')[0]==400
 assert api('/api/file?name=bad%00name')[0]==400
 assert api('/api/list?dir=')[0]==200
 name='pocket-test-'+uuid.uuid4().hex[:10]+'-中文.bin';created.append(name)
 data=bytes((i*37+(i>>8))&255 for i in range(512*1024+17))
 command('clock');command('test')
 started=time.monotonic();status,_=api(endpoint(name),'POST',data);assert status==200,status
 print('Uploaded binary during UI self-test:',len(data),'bytes in',round(time.monotonic()-started,1),'seconds',flush=True)
 status,got=api(endpoint(name));assert status==200 and hashlib.sha256(got).digest()==hashlib.sha256(data).digest()
 assert api(endpoint(name),'POST',b'replacement')[0]==409
 assert api(endpoint(name))[1]==data
 command('sd-unmount');command('wifi-off');time.sleep(1)
 assert api('/api/list?dir=')[0]==200
 # Interrupt a partial HTTP body: it must never appear as a completed file.
 partial='pocket-abort-'+uuid.uuid4().hex[:10]+'.bin';created.append(partial)
 c=http.client.HTTPConnection(ip,timeout=10);c.putrequest('POST',endpoint(partial));c.putheader('X-File-Key',code);c.putheader('Content-Length','65536');c.endheaders();c.send(b'partial'*100);c.close();time.sleep(3)
 listing=json.loads(api('/api/list?dir=')[1]);assert partial not in [f['name'] for f in listing['files']]
 assert api(endpoint(name),'DELETE')[0]==200;created.remove(name)
 assert api(endpoint(name))[0]==404
 empty='pocket-empty-'+uuid.uuid4().hex[:10]+'.txt';created.append(empty)
 assert api(endpoint(empty),'POST',b'')[0]==200
 assert api(endpoint(empty))[1]==b''
 assert api(endpoint(empty),'DELETE')[0]==200;created.remove(empty)
 # Disable with a slow upload still open: UI task must not deadlock.
 c=http.client.HTTPConnection(ip,timeout=10);c.putrequest('POST',endpoint(partial));c.putheader('X-File-Key',code);c.putheader('Content-Length','65536');c.endheaders();c.send(b'partial');time.sleep(.5)
 command('transfer-off');time.sleep(4);c.close();command('status');command('transfer-status');time.sleep(1)
 assert 'File server stopped' in log()
 previous=code;command('transfer-on')
 ip,code=until(lambda: ((m[-1][0],m[-1][1]) if (m:=re.findall(r'enabled=1 running=1 busy=\d url=http://([\d.]+)/ code=(\d{8})',log())) and m[-1][1]!=previous else (command('transfer-status') or None)),30)
 assert code!=previous and api('/api/list?dir=',key=previous)[0]==401
 listing=json.loads(api('/api/list?dir=')[1]);assert partial not in [f['name'] for f in listing['files']]
 assert 'SELFTEST PASS: 30' in log()
 assert not any(x in log() for x in ('Guru Meditation','DMA timeout','assert failed','Stack canary'))
 print('PASS: auth, path rejection, streamed SHA256 round-trip, conflict protection, abort cleanup, empty file, delete, live UI, Wi-Fi/SD guards, stop-during-upload, restart/key rotation',flush=True)
finally:
 for n in created:
  try:api(endpoint(n),'DELETE')
  except Exception:pass
 command('transfer-off');time.sleep(3);command('status');command('control');time.sleep(1)
 stop=True;thread.join();s.close()
