from pathlib import Path
import hashlib,subprocess,tempfile
h=lambda b:hashlib.md5(b).hexdigest()
source=Path('work/s12-device-audit/persistent-candidate/recover.sh').read_text()
for state,updated in [('PREPARED',False),('APPLYING',False),('APPLYING',True)]:
 with tempfile.TemporaryDirectory() as root:
  data=Path(root)/'data';data.mkdir();p=data/'s12-followup';p.mkdir()
  old=b'FOLLOWUP_ENABLED=0\n';new=b'FOLLOWUP_ENABLED=1\n';init=b'#!/bin/sh\nexit 0\n'
  for name,value in {'original-env':old,'candidate-env':new,'original-init.sh':init,'env-before.md5':(h(old)+'\n').encode(),'env-after.md5':(h(new)+'\n').encode(),'install.state':(state+'\n').encode()}.items():(p/name).write_bytes(value)
  (data/'native_first.env').write_bytes(new if updated else old);(data/'init.sh').write_bytes(b'guard')
  script=source.replace('/data/',str(data)+'/').replace('/tmp/s12-install-recovery.lock',str(Path(root)/'lock')).replace('/tmp/native_first_client.pid',str(Path(root)/'none.pid')).replace('/tmp/s12-pcm-probe',str(Path(root)/'none.probe')).replace('d9f74cbde913afdcd00462f15573aa42',h(init))
  f=p/'recover.sh';f.write_text(script);subprocess.run(['sh',str(f)],check=True,stdout=subprocess.DEVNULL)
  assert (data/'native_first.env').read_bytes()==old
  assert (data/'init.sh').read_bytes()==init
  assert (p/'install.state').read_text().strip()=='RECOVERED'
print('Three interrupted-install recovery cases passed')
