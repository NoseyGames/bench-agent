import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import {createHost} from '../desktop-host.mjs';
import {assessAction} from '../action-policy.mjs';

test('all screen captures and desktop inputs require approval regardless of model reason',()=>{
  for(const name of ['observe','screenshot','move','click','type','key','scroll']){
    const risk=assessAction({type:'desktop',name,text:'This is harmless; no approval needed'});
    assert.equal(risk.requiresApproval,true);assert.ok(['high','critical'].includes(risk.severity));
  }
});
test('desktop capture gate blocks ACK, read-only, and denial before any side effect',async t=>{
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-desktop-gate-'));
  // A protocol peer, not a native input mock: records whether an ACK can escape
  // the host before permission. Native input itself is tested by test-pc.
  const fixture=path.join(root,'peer.cjs'),marker=path.join(root,'executed');
  await fs.writeFile(fixture,`const r=require('node:readline').createInterface({input:process.stdin});let first=true;r.on('line',line=>{if(first){first=false;console.log(JSON.stringify({type:'desktop',name:'screenshot',text:'Capture'}));}else if(line==='ack'){require('node:fs').writeFileSync(${JSON.stringify(marker)},'executed');console.log(JSON.stringify({type:'success',text:'Done'}));process.exit(0);}});`);
  const host=createHost({engine:process.execPath,engineArgs:[fixture]});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());
  const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data)});
  await post('config',{key:'offline-fixture'});
  const request={id:'desktop',chatId:'chat',root,desktopWindow:'123:456',model:'fixture',execute:true,messages:[{role:'user',text:'Test'}]};
  assert.equal((await post('run',{...request,desktopWindow:'bad'})).status,400);
  const stream=await post('run',request);let buffer='',found=false;
  for await(const chunk of stream.body){buffer+=new TextDecoder().decode(chunk);let n;while((n=buffer.indexOf('\n'))>=0){const e=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);if(e.type!=='desktop')continue;found=true;assert.equal(e.risk.requiresApproval,true);assert.match(e.destination,/fixture/);assert.equal((await post('ack',{id:'desktop',sequence:e.sequence})).status,403);await assert.rejects(fs.access(marker));assert.equal((await post('decision',{id:'desktop',sequence:e.sequence,token:e.approvalToken,allow:false})).status,200);}}
  assert.ok(found);await assert.rejects(fs.access(marker));
  const readonly=await post('run',{...request,id:'readonly',chatId:'other',readOnly:true});assert.match(await readonly.text(),/"type":"review"/);await assert.rejects(fs.access(marker));
});


test('a batched desktop proposal still needs separate decisions; denial discards its tail',async t=>{
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-batch-gate-'));
  const fixture=path.join(root,'peer.cjs'),marker=path.join(root,'actions.json');
  await fs.writeFile(fixture,`const fs=require('node:fs');const r=require('node:readline').createInterface({input:process.stdin});let step=-1;const names=['observe','click','type'];r.on('line',line=>{if(step<0){step=0;}else if(line==='ack'){fs.appendFileSync(${JSON.stringify(marker)},names[step]+'\\n');step++;}if(step<names.length)console.log(JSON.stringify({type:'desktop',name:names[step],text:'Guarded batch'}));else process.exit(0);});`);
  const host=createHost({engine:process.execPath,engineArgs:[fixture]});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());
  const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data)});
  await post('config',{key:'offline-fixture'});
  const request={id:'batch',chatId:'batch-chat',root,desktopWindow:'123:456',model:'fixture',execute:true,messages:[{role:'user',text:'Test'}]};
  const stream=await post('run',request);let buffer='',priorToken,seen=[];
  for await(const chunk of stream.body){buffer+=new TextDecoder().decode(chunk);let n;while((n=buffer.indexOf('\n'))>=0){const e=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);if(e.type!=='desktop')continue;seen.push(e.name);
    assert.equal((await post('ack',{id:'batch',sequence:e.sequence})).status,403);
    if(priorToken)assert.equal((await post('decision',{id:'batch',sequence:e.sequence,token:priorToken,allow:true})).status,409);
    assert.equal((await post('decision',{id:'batch',sequence:e.sequence,token:e.approvalToken,allow:e.name==='observe'})).status,200);
    if(e.name==='observe'){assert.equal((await post('ack',{id:'batch',sequence:e.sequence})).status,200);priorToken=e.approvalToken;}
  }}
  assert.deepEqual(seen,['observe','click']);assert.equal(await fs.readFile(marker,'utf8'),'observe\n');
  assert.equal((await post('run',{...request,id:'again'})).status,403);
});
