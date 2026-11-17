import hashlib,itertools,pathlib,subprocess,tempfile
h=lambda b:hashlib.md5(b).hexdigest()
src=pathlib.Path('work/s12-device-audit/handoff-candidate/update-recover.sh').read_text()
old={'capture':b'oldcap','manager.sh':b'oldmgr','native_first_client.sh':b'oldclient'}
new={'capture':b'newcap','manager.sh':b'newmgr','native_first_client.sh':b'newclient'}
expected={'capture':('a0f79ab36e87b9eb329de5df8bca7d90','869705e7bfcddb90e338bc71d883ffcb'),'manager.sh':('f7b30f6d79f4bbac8aca9e7400880afa','5e845de6ea3f3a15335239f983a23993'),'native_first_client.sh':('e2fc1c272f5f8310b93cebfc0ec4735c','9ed5fb54a8a627c16a3ad1a709e954a6')}
for bits in itertools.product([False,True],repeat=3):
 with tempfile.TemporaryDirectory() as tmp:
  data=pathlib.Path(tmp)/'data';p=data/'s12-followup';p.mkdir(parents=True);(p/'install.state').write_text('UPDATING\n')
  for key,backup,use_new in zip(old,('update-old-capture','update-old-manager','update-old-client'),bits):
   (p/backup).write_bytes(old[key]);(p/key).write_bytes(new[key] if use_new else old[key])
  script=src.replace('/data/',str(data)+'/').replace('/tmp/s12-update-recovery.lock',tmp+'/lock').replace('/tmp/native_first_client.pid',tmp+'/none.pid').replace('/tmp/s12-pcm-probe',tmp+'/none.probe')
  for key,(oh,nh) in expected.items():script=script.replace(oh,h(old[key])).replace(nh,h(new[key]))
  f=p/'recover.sh';f.write_text(script);subprocess.run(['sh',str(f)],check=True,stdout=subprocess.DEVNULL)
  assert all((p/key).read_bytes()==value for key,value in old.items());assert (p/'install.state').read_text().strip()=='COMMITTED'
print('Eight interrupted-update recovery cases passed')
