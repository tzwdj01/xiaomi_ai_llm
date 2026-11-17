#!/usr/bin/env python3
"""Read-only dashboard for the S12 at the separately configured SSH alias."""
import ipaddress,json,re,secrets,subprocess,threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

PORT=18788
PAGE=Path(__file__).resolve().parent / 'index.html'
TOKEN=secrets.token_urlsafe(32)
def local_network():
 try:
  addr=subprocess.check_output(['/usr/sbin/ipconfig','getifaddr','en0'],text=True).strip()
  info=subprocess.check_output(['/sbin/ifconfig','en0'],text=True)
  m=re.search(r'inet\s+'+re.escape(addr)+r'\s+netmask\s+(0x[0-9a-fA-F]+)',info)
  if not m: return None,None
  ip=ipaddress.IPv4Address(addr);mask=ipaddress.IPv4Address(int(m.group(1),16))
  net=ipaddress.IPv4Network((addr,str(mask)),strict=False)
  return (addr,net) if ip.is_private and net.prefixlen>=16 else (None,None)
 except (OSError,ValueError,subprocess.CalledProcessError): return None,None
IP,NET=local_network()
HOSTS={'127.0.0.1:%d'%PORT,'localhost:%d'%PORT}
if IP:HOSTS.add('%s:%d'%(IP,PORT))
CMD=r'''export PATH=/usr/sbin:/usr/bin:/sbin:/bin
echo __IDENTITY__
printf 'rom='; uci -c /usr/share/mico -q get version.version.ROM
printf 'board='; uci -c /usr/share/mico -q get version.version.HARDWARE
printf 'arch='; uname -m
printf 'kernel='; uname -r
printf 'cores='; grep -c '^processor' /proc/cpuinfo
printf 'platform='; tr '\000' ' ' < /proc/device-tree/compatible; echo
printf 'cpu_part='; sed -n 's/^CPU part[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -1
printf 'wifi_driver='; awk '$1 == "dhd" {print $1}' /proc/modules
printf 'wifi_vendor='; cat /sys/bus/sdio/devices/sdio:0001:1/vendor 2>/dev/null
printf 'wifi_device='; cat /sys/bus/sdio/devices/sdio:0001:1/device 2>/dev/null
printf 'wifi_firmware='; cat /sys/module/dhd/parameters/firmware_path 2>/dev/null; echo
echo __RELEASE__
cat /etc/openwrt_release
echo __MEMORY__
grep -E '^(MemTotal|MemAvailable):' /proc/meminfo
echo __DISK__
df -k /data /tmp
echo __FLASH__
cat /proc/mtd
echo __WIFI__
wpa_cli -i wlan0 status | grep -E '^(wpa_state|ip_address|ssid|freq)='
echo __CONFIG__
grep -E '^(BACKEND|LLM_MODEL|LLM_API_BASE|LLM_WEB_SEARCH|DEVICE_TTS_VOICE|TTS_ENGINE|FOLLOWUP_ENABLED|SYSTEM1_FOLLOWUP_ENABLED|NATIVE_RESULT_SOURCE|FOLLOWUP_TIMEOUT|PCM_CAPTURE_SECONDS)=' /data/native_first.env
echo __KEYS__
for k in DEEPSEEK_API_KEY GLM_API_KEY KIMI_API_KEY MINIMAX_API_KEY LLM_API_KEY; do v=$(sed -n "s/^$k=//p" /data/native_first.env | tail -1); case "$v" in ''|"''"|'""') echo "$k=0";; *) echo "$k=1";; esac; done
echo __MODE__
[ ! -f /data/s12-followup/install.state ] || cat /data/s12-followup/install.state
echo __TRIAL__
[ ! -f /tmp/s12-followup-trial/status ] || cat /tmp/s12-followup-trial/status
echo __LOG__
tail -n 140 /tmp/native_first_client.log 2>/dev/null | grep -E '\[LLM-NATIVE\] direct|\[FOLLOWUP\] native live ASR text=|\[TTS\] 端侧流式合成|\[NATIVE\] result source=.*query=' | tail -30
echo __NATIVE__
tail -n 120 /tmp/mico_aivs_lab/instruction.log 2>/dev/null
echo __SPEAKING__
for pid in $(pidof ettsc 2>/dev/null); do c=$(tr '\000' '\n' < "/proc/$pid/cmdline" 2>/dev/null); case "$c" in /data/ettsc*) printf '%s\n' "$c";; esac; done
'''
def read_device():
 proc=subprocess.run(['/usr/bin/ssh','-q','-o','BatchMode=yes','-o','ConnectTimeout=5','xiaomi-s12',CMD],text=True,capture_output=True,timeout=15)
 if proc.returncode: raise RuntimeError('无法通过 SSH 连接音箱')
 sections={};key=None
 for line in proc.stdout.splitlines():
  if line.startswith('__') and line.endswith('__'): key=line[2:-2].lower();sections[key]=[]
  elif key: sections[key].append(line)
 def pairs(name):
  return {k:v.strip('"\' ') for l in sections.get(name,[]) if '=' in l for k,v in [l.split('=',1)]}
 ident=pairs('identity'); wifi=pairs('wifi'); config=pairs('config'); keys=pairs('keys'); release=pairs('release')
 if not ident.get('board') or not wifi.get('ip_address'): raise RuntimeError('音箱信息尚未读取完整')
 mem={}
 for line in sections.get('memory',[]):
  m=re.match(r'(MemTotal|MemAvailable):\s+(\d+)',line)
  if m:mem[m[1]]=int(m[2])
 disks=[]
 for line in sections.get('disk',[]):
  m=re.match(r'(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)%\s+(\S+)',line)
  if m:disks.append({'mount':m[6],'total':int(m[2]),'used':int(m[3]),'percent':int(m[5])})
 partitions=[]
 for line in sections.get('flash',[]):
  m=re.match(r'mtd\d+:\s+([0-9a-f]+)\s+[0-9a-f]+\s+"([^"]+)"',line)
  if m:partitions.append({'name':m[2],'size':int(m[1],16)})
 events=[]
 for line in sections.get('native',[]):
  try:
   j=json.loads(line);h=j.get('header',{});p=j.get('payload',{});ns=h.get('namespace');name=h.get('name');t=''
   if ns=='SpeechRecognizer' and name=='RecognizeResult' and p.get('is_final'): t=(p.get('results') or [{}])[0].get('text','');role='我'
   elif ns=='SpeechSynthesizer' and name=='Speak':t=p.get('text','');role='小爱'
   else:continue
   if t:events.append({'role':role,'text':t,'source':'原生记录'})
  except (ValueError,TypeError,KeyError,IndexError):pass
 for line in sections.get('log',[]):
  m=re.search(r'\[FOLLOWUP\] native live ASR text=(.*)',line) or re.search(r'\[LLM-NATIVE\] direct .*? text=(.*)',line)
  if m:events.append({'role':'我','text':m.group(1),'source':'大模型对话'});continue
  m=re.search(r'\[TTS\] 端侧流式合成 .*?: (.*)',line)
  if m:events.append({'role':'AI','text':m.group(1),'source':'语音内容'})
 speaking=[]; lines=sections.get('speaking',[])
 for index,line in enumerate(lines):
  if line=='/data/ettsc' and index+1<len(lines) and not lines[index+1].startswith('/') and lines[index+1] not in ('probe','prewarm'):speaking.append(lines[index+1])
 return {'online':True,'identity':ident,'release':release,'memory':mem,'wifi':wifi,'disk':disks,'partitions':partitions,'config':config,'keys':keys,'events':events[-30:],'speaking':speaking,'trial_active':sections.get('trial',[])==['ACTIVE'],'followup_managed':sections.get('mode',[])==['COMMITTED']}
def validate_settings(x, data):
 if not isinstance(x,dict):raise ValueError('请求格式不正确')
 providers={'deepseek':'DEEPSEEK_API_KEY','glm':'GLM_API_KEY','kimi':'KIMI_API_KEY','minimax':'MINIMAX_API_KEY','custom':'LLM_API_KEY'}
 def val(k):
  v=x.get(k,'')
  if not isinstance(v,str):raise ValueError('设置格式不正确')
  return v.strip()
 backend=val('backend');search=val('search');voice=val('voice');model=val('model');url=val('url');api=val('key')
 if backend not in providers or search not in ('auto','off') or voice not in ('zh-CN-XiaoxiaoNeural','zh-CN-XiaoyiNeural','zh-CN-YunjianNeural'):raise ValueError('设置不合法')
 if model and not re.fullmatch(r'[A-Za-z0-9._:/-]{1,100}',model):raise ValueError('模型名称格式不正确')
 if url:
  u=urlsplit(url)
  if u.scheme!='https' or not u.hostname or u.username or u.password or u.query or u.fragment or not re.fullmatch(r'https://[A-Za-z0-9.:_/-]+',url):raise ValueError('接口地址必须是不含密码的 HTTPS 地址')
 if backend=='custom' and (not url or not model):raise ValueError('自定义接口需要地址和模型名称')
 if api and not re.fullmatch(r'[A-Za-z0-9._~+/-]{8,512}',api):raise ValueError('API Key 格式不正确')
 if not api and data.get('keys',{}).get(providers[backend])!='1':raise ValueError('此模型尚未保存 API Key')
 updates={'BACKEND':backend,'LLM_WEB_SEARCH':search,'DEVICE_TTS_VOICE':voice,'LLM_MODEL':model,'LLM_API_BASE':url}
 if data.get('followup_managed'):
  wait=x.get('wait')
  if type(wait) is not int or not 5<=wait<=10:raise ValueError('追问等待时间须为 5 到 10 秒')
  updates['FOLLOWUP_TIMEOUT']=str(wait);updates['PCM_CAPTURE_SECONDS']=str(wait)
 if backend!='custom':updates['LLM_API_KEY']=''
 if api:updates[providers[backend]]=api
 return updates

def save_settings(x):
 data=read_device()
 if data.get('trial_active'):raise ValueError('连续对话测试正在进行，请稍后保存')
 updates=validate_settings(x,data)
 pairs=''.join(k+'='+v+'\n' for k,v in updates.items())
 script=r'''set -eu
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
[ "$(micocfg_model)" = S12 ] || { echo '音箱身份不匹配' >&2; exit 1; }
[ ! -e /tmp/s12-followup-trial/active ] || { echo '连续对话测试正在进行' >&2; exit 1; }
[ ! -e /tmp/native_first_busy ] || { echo '音箱正在说话，请稍后保存' >&2; exit 1; }
umask 077
lock=/tmp/s12-dashboard-config.lock
mkdir "$lock" 2>/dev/null || { echo '另一次保存正在进行' >&2; exit 1; }
trap 'rm -rf "$lock"' EXIT
cat > "$lock/updates" <<'S12_SETTINGS_EOF'
'''+pairs+r'''S12_SETTINGS_EOF
f=/data/native_first.env
[ -f "$f" ] && [ ! -L "$f" ]
cp -p "$f" "$lock/original"
awk -F= 'NR==FNR {keys[$1]=1;next} !($1 in keys) {print}' "$lock/updates" "$f" > "$lock/new"
cat "$lock/updates" >> "$lock/new"
chmod 600 "$lock/new"; sh -n "$lock/new"
[ "$(md5sum "$lock/original" | cut -d ' ' -f 1)" = "$(md5sum "$f" | cut -d ' ' -f 1)" ] || { echo '音箱配置刚刚发生变化，请重试' >&2; exit 1; }
[ ! -e /tmp/native_first_busy ] || { echo '音箱刚开始对话，请稍后保存' >&2; exit 1; }
mkdir -p /data/.s12-dashboard-backup
chmod 700 /data/.s12-dashboard-backup
backup=/data/.s12-dashboard-backup/previous.env
cp -p "$lock/original" "$backup"
cp "$lock/new" /data/.s12-dashboard-config-new; chmod 600 /data/.s12-dashboard-config-new
mv /data/.s12-dashboard-config-new "$f"
client=/data/native_first_client.sh
if [ "$(cat /data/s12-followup/install.state 2>/dev/null)" = COMMITTED ]; then client=/data/s12-followup/native_first_client.sh; fi
restore() {
 cp "$lock/original" /data/.s12-dashboard-config-restore
 chmod 600 /data/.s12-dashboard-config-restore
 mv /data/.s12-dashboard-config-restore "$f"
 sh "$client" stop >/dev/null 2>&1 || :
 CONFIG_FILE="$f" sh "$client" </dev/null >/dev/null 2>&1 &
}
if ! sh "$client" stop >/dev/null 2>&1; then restore; echo '未能停止助手，已恢复原设置' >&2; exit 1; fi
CONFIG_FILE="$f" sh "$client" </dev/null >/dev/null 2>&1 &
sleep 6
if [ ! -s /tmp/native_first_client.pid ] || ! kill -0 "$(cat /tmp/native_first_client.pid)" 2>/dev/null || ! grep -q '\[IDLE\] 等待原生唤醒词' /tmp/native_first_client.log; then
 restore; echo '助手未正常启动，已尝试恢复原设置' >&2; exit 1
fi
echo SAVED
'''
 proc=subprocess.run(['/usr/bin/ssh','-q','-o','BatchMode=yes','-o','ConnectTimeout=5','xiaomi-s12','/bin/sh -s'],input=script,text=True,capture_output=True,timeout=60)
 if proc.returncode or 'SAVED' not in proc.stdout:raise RuntimeError(proc.stderr.strip()[-250:] or '保存失败')

class Handler(BaseHTTPRequestHandler):
 def valid(self):
  try: client=ipaddress.IPv4Address(self.client_address[0])
  except ValueError:return False
  return self.headers.get('Host') in HOSTS and (client.is_loopback or (NET is not None and client in NET))
 def do_GET(self):
  if not self.valid():self.send_error(403);return
  if self.path=='/':
   body=PAGE.read_text().replace('__TOKEN__',TOKEN).encode();kind='text/html; charset=utf-8'
  elif self.path=='/data':
   try:obj=read_device()
   except Exception as e:obj={'online':False,'error':str(e)}
   body=json.dumps(obj,ensure_ascii=False).encode();kind='application/json; charset=utf-8'
  else:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
 def do_POST(self):
  if not self.valid() or self.path!='/save' or self.headers.get('Origin')!='http://'+self.headers.get('Host','') or self.headers.get('X-Token')!=TOKEN:self.send_error(403);return
  try:
   n=int(self.headers.get('Content-Length','0'))
   if n<1 or n>4096:raise ValueError('请求大小不正确')
   data=json.loads(self.rfile.read(n))
   if not ipaddress.IPv4Address(self.client_address[0]).is_loopback and data.get('key',''):raise ValueError('API Key 请在 Mac 本机打开看板后填写')
   save_settings(data);obj={'ok':True};code=200
  except Exception as e:obj={'ok':False,'error':str(e)};code=400
  body=json.dumps(obj,ensure_ascii=False).encode();self.send_response(code);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
 def log_message(self,*a):pass
if __name__=='__main__':
 servers=[ThreadingHTTPServer(('127.0.0.1',PORT),Handler)]
 if IP:servers.append(ThreadingHTTPServer((IP,PORT),Handler))
 for server in servers[1:]:threading.Thread(target=server.serve_forever,daemon=True).start()
 print('S12 dashboard listening on localhost'+(' and '+IP if IP else ''),flush=True)
 servers[0].serve_forever()
