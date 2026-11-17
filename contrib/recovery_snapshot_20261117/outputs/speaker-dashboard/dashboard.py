#!/usr/bin/env python3
import ipaddress,json,re,secrets,subprocess,threading,webbrowser
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
ROOT=Path(__file__).resolve().parents[2]; SSH=ROOT/'work/speaker-ssh/bin/ssh'; TOKEN=secrets.token_urlsafe(32)
def network_details():
 try:
  addr=subprocess.check_output(['/usr/sbin/ipconfig','getifaddr','en0'],text=True).strip()
  info=subprocess.check_output(['/sbin/ifconfig','en0'],text=True)
  mask=re.search(r'inet\s+'+re.escape(addr)+r'\s+netmask\s+(0x[0-9a-fA-F]+)',info)
  if not mask: raise ValueError('无法识别网络掩码')
  ip=ipaddress.IPv4Address(addr); net=ipaddress.IPv4Network((addr,str(ipaddress.IPv4Address(int(mask.group(1),16)))),strict=False)
  if not ip.is_private or net.prefixlen < 16: raise ValueError('当前 Wi-Fi 网络不适合开放看板')
  return addr,net
 except (OSError,subprocess.CalledProcessError,ValueError):return None,None
LAN_IP,LAN_NET=network_details()
HOSTS={'127.0.0.1:18788','localhost:18788'}
if LAN_IP:HOSTS.add(LAN_IP+':18788')
CMD=r'''echo __SYS__; uci -c /usr/share/mico -q get version.version.ROM; uname -m; grep -c '^processor' /proc/cpuinfo; cat /proc/uptime; grep -E '^(MemTotal|MemAvailable):' /proc/meminfo; df -k /data /tmp; cat /proc/mtd; wpa_cli -i wlan0 status | grep -E '^(wpa_state|ip_address|ssid|freq)='; echo __CONFIG__; grep -E '^(BACKEND|LLM_MODEL|LLM_API_BASE|LLM_WEB_SEARCH|DEVICE_TTS_VOICE|TTS_ENGINE|SYSTEM1_FOLLOWUP_ENABLED|NATIVE_ENDPOINT_ENABLED|FOLLOWUP_TIMEOUT)=' /data/native_first.env; echo __KEYS__; for k in DEEPSEEK_API_KEY GLM_API_KEY KIMI_API_KEY MINIMAX_API_KEY LLM_API_KEY; do v=$(sed -n "s/^$k=//p" /data/native_first.env | tail -1); case "$v" in ''|"''"|'""') echo "$k=0";; *) echo "$k=1";; esac; done; echo __LOG__; tail -n 140 /tmp/native_first_client.log | grep -E '\[LLM-NATIVE\] direct|\[FOLLOWUP\] native live ASR text=|\[TTS\] 端侧流式合成|\[NATIVE\] result source=.*query=' | tail -30; echo __NATIVE__; tail -n 100 /tmp/mico_aivs_lab/instruction.log 2>/dev/null; echo __SPEAKING__; for pid in $(pidof ettsc 2>/dev/null); do c=$(tr '\000' '\n' < "/proc/$pid/cmdline" 2>/dev/null); case "$c" in /data/ettsc*) printf '%s\n' "$c";; esac; done'''
def run(cmd,data=None,timeout=15):
 p=subprocess.run([str(SSH),'-q','-o','BatchMode=yes','-o','ConnectTimeout=5','root@xiaomi-codex',cmd],input=data,text=True,capture_output=True,timeout=timeout)
 if p.returncode: raise RuntimeError(p.stderr.strip()[-400:] or p.stdout.strip()[-400:] or '音箱连接失败')
 return p.stdout
def data():
 raw=run(CMD); sections={}; key=None
 for line in raw.splitlines():
  if line.startswith('__') and line.endswith('__'): key=line[2:-2].lower();sections[key]=[]
  elif key: sections[key].append(line)
 sys=sections.get('sys',[]); cfg={}; keys={}
 for group,target in [('config',cfg),('keys',keys)]:
  for line in sections.get(group,[]):
   if '=' in line:
    k,v=line.split('=',1);target[k]=v.strip('"\'')
 mem={}; wifi={}; disk=[]; flash=0
 for line in sys:
  m=re.match(r'(MemTotal|MemAvailable):\s+(\d+)',line)
  if m: mem[m[1]]=int(m[2])
  m=re.match(r'(/dev/\S+|tmpfs)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)%\s+(\S+)',line)
  if m: disk.append({'mount':m[6],'total':int(m[2]),'used':int(m[3]),'percent':int(m[5])})
  m=re.match(r'mtd\d+:\s+([0-9a-f]+)',line)
  if m: flash+=int(m[1],16)
  if '=' in line:
   k,v=line.split('=',1)
   if k in ('ssid','ip_address','freq','wpa_state'):wifi[k]=v
 events=[]
 for line in sections.get('native',[]):
  try:
   j=json.loads(line); h=j.get('header',{});p=j.get('payload',{}); ns=h.get('namespace');n=h.get('name');t=''
   if ns=='SpeechRecognizer' and n=='RecognizeResult' and p.get('is_final'): t=(p.get('results') or [{}])[0].get('text','');role='我'
   elif ns=='SpeechSynthesizer' and n=='Speak': t=p.get('text','');role='小爱'
   else:continue
   if t:events.append({'role':role,'text':t,'source':'原生记录'})
  except (ValueError,TypeError,KeyError):pass
 for line in sections.get('log',[]):
  m=re.search(r'\[FOLLOWUP\] native live ASR text=(.*)',line) or re.search(r'\[LLM-NATIVE\] direct .*? text=(.*)',line)
  if m:events.append({'role':'我','text':m.group(1),'source':'大模型对话'});continue
  m=re.search(r'\[TTS\] 端侧流式合成 .*?: (.*)',line)
  if m:events.append({'role':'AI','text':m.group(1),'source':'合成完成'})
 current=[]; lines=sections.get('speaking',[])
 for i,line in enumerate(lines):
  if line=='/data/ettsc' and i+1<len(lines) and not lines[i+1].startswith('/') and lines[i+1] not in ('probe','prewarm'):current.append(lines[i+1])
 return {'online':True,'version':sys[0] if sys else '', 'arch':sys[1] if len(sys)>1 else '', 'cores':sys[2] if len(sys)>2 else '', 'uptime':sys[3] if len(sys)>3 else '', 'memory':mem,'wifi':wifi,'disk':disk,'flash':flash,'config':cfg,'keys':keys,'events':events[-30:],'speaking':current}
def save(x):
 allowed={'deepseek':'DEEPSEEK_API_KEY','glm':'GLM_API_KEY','kimi':'KIMI_API_KEY','minimax':'MINIMAX_API_KEY','custom':'LLM_API_KEY'}
 backend=x.get('backend');search=x.get('search');voice=x.get('voice');model=x.get('model','').strip();url=x.get('url','').strip(); api=x.get('key','').strip()
 wait=x.get('wait')
 if type(wait) is not int or wait < 5 or wait > 10:raise ValueError('等待追问时间须为 5 到 10 秒')
 if backend not in allowed or search not in ('auto','off') or voice not in ('zh-CN-XiaoxiaoNeural','zh-CN-XiaoyiNeural','zh-CN-YunjianNeural'):raise ValueError('设置不合法')
 if model and not re.fullmatch(r'[A-Za-z0-9._:/-]{1,100}',model):raise ValueError('模型名称格式不正确')
 if url:
  u=urlsplit(url)
  if u.scheme!='https' or not u.hostname or u.username or u.password or u.query or u.fragment or not re.fullmatch(r'https://[A-Za-z0-9.:_/-]+',url):raise ValueError('接口地址须为不含密码的 HTTPS 地址')
 if backend=='custom' and (not url or not model):raise ValueError('自定义接口必须填写地址和模型名称')
 if api and not re.fullmatch(r'[A-Za-z0-9._~+/-]{8,512}',api):raise ValueError('密钥格式不正确，请检查有无空格或换行')
 if not api and data()['keys'].get(allowed[backend])!='1':raise ValueError('这个模型还没有保存 API Key，请先填写密钥')
 updates={'BACKEND':backend,'LLM_WEB_SEARCH':search,'DEVICE_TTS_VOICE':voice,'LLM_MODEL':model,'LLM_API_BASE':url,'LLM_API_KEY':'','FOLLOWUP_TIMEOUT':str(wait)}
 if api:updates[allowed[backend]]=api
 if backend=='custom' and not api:updates.pop('LLM_API_KEY',None)
 # All values have restricted characters. Secrets go through stdin, never SSH command arguments.
 pairs=''.join(k+'='+v+'\n' for k,v in updates.items())
 script='''set -eu
[ ! -e /tmp/native_first_busy ] || { echo '音箱正在说话，请稍后再保存' >&2; exit 1; }
lock=/tmp/codex-dashboard-config.lock
mkdir "$lock" 2>/dev/null || { echo '另一次保存正在进行' >&2; exit 1; }
trap 'rm -rf "$lock"' EXIT
umask 077
cat > "$lock/updates" <<'CODEX_SETTINGS_EOF'
'''+pairs+'''CODEX_SETTINGS_EOF
f=/data/native_first.env
cp -p "$f" "$lock/original"
backup=/data/native_first.env.dashboard-$(date +%Y%m%d-%H%M%S)-$$
cp -p "$f" "$backup"
awk -F= 'NR==FNR {keys[$1]=1;next} !($1 in keys) {print}' "$lock/updates" "$f" > "$lock/new"
cat "$lock/updates" >> "$lock/new"
chmod 600 "$lock/new"; sh -n "$lock/new"
[ ! -e /tmp/native_first_busy ] || { echo '音箱刚开始对话，请稍后再保存' >&2; exit 1; }
cp "$lock/new" /data/native_first.env.dashboard-new; chmod 600 /data/native_first.env.dashboard-new; mv /data/native_first.env.dashboard-new "$f"
ENDPOINT_INSTALL_BACKUP=/data/endpoint-backup-20261115-010839; export ENDPOINT_INSTALL_BACKUP
. "$ENDPOINT_INSTALL_BACKUP/client-functions.sh"
if stop_route_client && launch_client /data/native_first_client.sh /tmp/native_first_client.log; then echo SAVED; else
 cp "$lock/original" /data/native_first.env.dashboard-restore; chmod 600 /data/native_first.env.dashboard-restore; mv /data/native_first.env.dashboard-restore "$f"
 stop_route_client || :; launch_client /data/native_first_client.sh /tmp/native_first_client.log || :
 echo '启动失败，已尝试恢复之前的配置' >&2; exit 1
fi
'''
 return run('sh -s',script,timeout=80)
class Handler(BaseHTTPRequestHandler):
 def send(self,obj,code=200):
  body=json.dumps(obj,ensure_ascii=False).encode();self.send_response(code);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
 def valid(self):
  try: client=ipaddress.IPv4Address(self.client_address[0])
  except ValueError:return False
  return self.headers.get('Host') in HOSTS and (client.is_loopback or (LAN_NET is not None and client in LAN_NET))
 def do_GET(self):
  if not self.valid():self.send_error(403);return
  if self.path=='/':
   b=(Path(__file__).parent/'index.html').read_text().replace('__TOKEN__',TOKEN).encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(b)
  elif self.path=='/data':
   try:self.send(data())
   except Exception as e:self.send({'online':False,'error':str(e)})
  else:self.send_error(404)
 def do_POST(self):
  if not self.valid() or self.path!='/save' or self.headers.get('Origin') != 'http://'+self.headers.get('Host','') or self.headers.get('X-Token')!=TOKEN:self.send_error(403);return
  try:
   length=int(self.headers.get('Content-Length','0'))
   if length<1 or length>4096:raise ValueError('请求大小不正确')
   x=json.loads(self.rfile.read(length))
   if not ipaddress.IPv4Address(self.client_address[0]).is_loopback and x.get('key','').strip():raise ValueError('局域网连接未加密，请在 Mac 本机打开看板后再填写 API Key')
   save(x);self.send({'ok':True})
  except Exception as e:self.send({'ok':False,'error':str(e)},400)
 def log_message(self,*a):pass
if __name__=='__main__':
 servers=[]
 try:
  servers.append(ThreadingHTTPServer(('127.0.0.1',18788),Handler))
  if LAN_IP: servers.append(ThreadingHTTPServer((LAN_IP,18788),Handler))
 except OSError:
  for server in servers:server.server_close()
  print('端口已占用，请先关闭旧看板。');raise SystemExit(1)
 for server in servers[1:]:threading.Thread(target=server.serve_forever,daemon=True).start()
 threading.Timer(.5,lambda:webbrowser.open('http://127.0.0.1:18788/')).start()
 print('本机地址：http://127.0.0.1:18788/',flush=True)
 print('局域网地址：'+('http://'+LAN_IP+':18788/' if LAN_IP else '未找到可用的 Wi-Fi'),flush=True)
 print('当前家庭局域网无需登录。按 Ctrl+C 退出。',flush=True)
 servers[0].serve_forever()
