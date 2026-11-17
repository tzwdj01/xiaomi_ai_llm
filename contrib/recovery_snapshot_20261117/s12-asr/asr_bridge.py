#!/usr/bin/env python3
import cgi,ipaddress,os,re,secrets,subprocess,tempfile,threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
BASE=Path(__file__).resolve().parent
MODEL=BASE.parent/'models/ggml-base.bin'
TOKEN_FILE=BASE/'bridge-token.private'
TOKEN=TOKEN_FILE.read_text().strip()
assert re.fullmatch(r'[0-9a-f]{48}',TOKEN)
SPEAKER=os.environ.get('S12_SPEAKER_IP', '')
if not SPEAKER: raise SystemExit('Set S12_SPEAKER_IP to the verified S12 IPv4 address')
MAC_IP=subprocess.check_output(['/usr/sbin/ipconfig','getifaddr','en0'],text=True).strip()
assert ipaddress.IPv4Address(MAC_IP).is_private
LOCK=threading.Lock()
class H(BaseHTTPRequestHandler):
 def respond(self,code,data):
  b=data.encode();self.send_response(code);self.send_header('Content-Type','text/plain; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(b)
 def allowed(self):return self.client_address[0]==SPEAKER and self.path.startswith('/'+TOKEN+'/')
 def do_GET(self):
  if not self.allowed() or self.path!='/'+TOKEN+'/':return self.respond(403,'forbidden')
  self.respond(200,'ready')
 def do_POST(self):
  if not self.allowed() or self.path!='/'+TOKEN+'/api/v1/route/asr':return self.respond(403,'forbidden')
  try:
   length=int(self.headers.get('Content-Length','0'))
   if length<=0 or length>1500000:raise ValueError('invalid size')
   ct,options=cgi.parse_header(self.headers.get('Content-Type',''))
   if ct!='multipart/form-data':raise ValueError('invalid content type')
   form=cgi.FieldStorage(fp=self.rfile,headers=self.headers,environ={'REQUEST_METHOD':'POST','CONTENT_TYPE':self.headers['Content-Type'],'CONTENT_LENGTH':str(length)})
   item=form['file'];raw=item.file.read(1400001)
   if len(raw)>1400000 or len(raw)<44 or raw[:4]!=b'RIFF' or raw[8:12]!=b'WAVE':raise ValueError('invalid audio')
   if not LOCK.acquire(blocking=False):return self.respond(503,'busy')
   try:
    with tempfile.NamedTemporaryFile(prefix='s12-voice-',suffix='.wav') as f:
     f.write(raw);f.flush()
     r=subprocess.run(['/opt/homebrew/bin/whisper-cli','-m',str(MODEL),'-f',f.name,'-l','zh','-nt','-np'],text=True,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=40)
    if r.returncode:raise RuntimeError('ASR failed')
    text=' '.join(r.stdout.split()).replace('\x00','')[:1000]
    # Whisper marks non-speech hallucinations in paired brackets.
    compact=''.join(text.split())
    if (compact[:1],compact[-1:]) in (('(',')'),('（','）'),('[',']'),('【','】')) or compact=='[BLANK_AUDIO]':text=''
    route='llm' if text else 'empty'
    reason='local_asr' if text else 'no_speech'
    self.respond(200,'TEXT='+text+'\nROUTE='+route+'\nREASON='+reason+'\n')
   finally:LOCK.release()
  except Exception:self.respond(400,'invalid request')
 def log_message(self,*args):pass
if __name__=='__main__':
 s=ThreadingHTTPServer((MAC_IP,18799),H);print('S12 ASR bridge ready',flush=True);s.serve_forever()
