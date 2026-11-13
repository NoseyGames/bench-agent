import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import {createHost} from '../desktop-host.mjs';

const engine=path.resolve('..',process.platform==='win32'?'bench-engine.exe':'bench-engine');
async function fixture(t,execute=true){
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-desktop-'));
  const original='int main(void) { return 1; }\n',updated='int main(void) { return 0; }\n';
  await fs.writeFile(path.join(root,'main.c'),original);
  const plan=path.join(root,'plan.json');
  await fs.writeFile(plan,JSON.stringify({summary:'Repair the failing program',steps:['Fix return code','Compile','Test'],files:[{name:'main.c',content:updated}],read_files:[],commands:[{kind:'build',command:'gcc main.c -o check.exe',timeout_seconds:30},{kind:'test',command:process.platform==='win32'?'check.exe':'./check.exe',timeout_seconds:30}]}));
  const host=createHost({engine,engineArgs:['--plan',plan]});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');
  t.after(()=>host.close());
  const base=`http://127.0.0.1:${host.server.address().port}`;
  const post=(route,data,auth=true)=>fetch(base+'/api/'+route,{method:'POST',headers:{'Content-Type':'application/json',...(auth?{Authorization:'Bearer '+host.token}:{})},body:JSON.stringify(data)});
  await post('config',{key:'offline-test-only'});
  return {root,original,updated,host,post,base,execute};
}
test('loopback host rejects missing auth and foreign origins; serves real UI',async t=>{
  const {post,base}=await fixture(t);
  assert.equal((await post('config',{},false)).status,401);
  assert.equal((await fetch(base+'/api/config',{method:'POST',headers:{Origin:'https://example.com'}})).status,403);
  assert.equal((await fetch(base+'/../engine.c')).status,404);
  assert.match(await(await fetch(base)).text(),/desktop.js/);
});
test('actual C engine waits for preview ACK, backs up, builds and tests',async t=>{
  const f=await fixture(t),id='run-1';
  const r=await f.post('run',{id,chatId:'chat-1',root:f.root,model:'offline',messages:[{role:'user',text:'Repair',files:[]}],execute:true});
  assert.equal(r.status,200);const events=[];let pending='';
  for await(const chunk of r.body){pending+=new TextDecoder().decode(chunk);let n;while((n=pending.indexOf('\n'))>=0){const e=JSON.parse(pending.slice(0,n));pending=pending.slice(n+1);events.push(e);
    if(e.type==='preview'){
      assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.original);
      await new Promise(resolve=>setTimeout(resolve,150));
      assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.original);
      assert.equal((await f.post('ack',{id,sequence:999})).status,409);
    }
    if(['preview','command'].includes(e.type))assert.equal((await f.post('ack',{id,sequence:e.sequence})).status,200);
  }}
  const success=events.find(e=>e.type==='success');assert.ok(success,JSON.stringify(events));
  assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.updated);
  assert.equal(await fs.readFile(path.join(success.name,'original','main.c'),'utf8'),f.original);
  assert.equal(events.filter(e=>e.type==='exit'&&e.text==='Exit 0').length,2);
});
test('read-only rejects edits before ACK and leaves project unchanged',async t=>{
  const f=await fixture(t);const r=await f.post('run',{id:'read-only',chatId:'read',root:f.root,model:'offline',messages:[{role:'user',text:'Repair'}],execute:true,readOnly:true});
  const events=await r.text();assert.match(events,/No changes were applied/);assert.match(events, /"type":"review"/);
  assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.original);
});
test('cancelling a pending preview does not write files',async t=>{
  const f=await fixture(t);const r=await f.post('run',{id:'cancel',chatId:'cancel',root:f.root,model:'offline',messages:[{role:'user',text:'Repair'}],execute:true});
  let output='';for await(const chunk of r.body){output+=new TextDecoder().decode(chunk);if(output.includes('"preview"')){assert.equal((await f.post('cancel',{id:'cancel'})).status,200);break;}}
  assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.original);
});

test('simultaneous runs reserve one slot before asynchronous root validation',async t=>{
  const f=await fixture(t);
  const responses=await Promise.all(['one','two'].map(id=>f.post('run',{id,chatId:id,root:f.root,model:'offline',messages:[{role:'user',text:'Repair'}],execute:true})));
  assert.deepEqual(responses.map(r=>r.status).sort(),[200,409]);
  await Promise.all(['one','two'].map(id=>f.post('cancel',{id})));
  await Promise.all(responses.map(r=>r.text()));
  assert.equal(await fs.readFile(path.join(f.root,'main.c'),'utf8'),f.original);
});

test('invalid root releases the reserved run slot',async t=>{
  const f=await fixture(t),request={id:'retry',chatId:'retry',root:path.join(f.root,'missing'),model:'offline',messages:[{role:'user',text:'Repair'}],execute:true};
  assert.equal((await f.post('run',request)).status,400);
  request.root=f.root;const r=await f.post('run',request);assert.equal(r.status,200);
  await f.post('cancel',{id:request.id});await r.text();
});

test('cancel response waits for engine exit so a follow-up can reuse the slot',async t=>{
  const f=await fixture(t),request={id:'reuse',chatId:'reuse',root:f.root,model:'offline',messages:[{role:'user',text:'Repair'}],execute:true};
  const first=await f.post('run',request);assert.equal(first.status,200);
  assert.equal((await f.post('cancel',{id:request.id})).status,200);await first.text();
  const next=await f.post('run',request);assert.equal(next.status,200);
  await f.post('cancel',{id:request.id});await next.text();
});

test('current preferences reach the engine in both clarification and execution phases',async t=>{
  const script="require('node:readline').createInterface({input:process.stdin}).once('line',s=>{const r=JSON.parse(s);console.log(JSON.stringify({type:r.phase==='questions'?'questions':'success',text:r.task}));process.exit(0);});";
  const host=createHost({engine:process.execPath,engineArgs:['-e',script]});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());
  const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+host.token},body:JSON.stringify(data)});
  await post('config',{key:'offline-only'});
  const request={id:'context',chatId:'context',root:process.cwd(),model:'fixture',messages:[{role:'user',text:'Original task'}],instructions:'First preferences'};
  const initial=await(await post('run',request)).text();assert.match(initial,/First preferences/);
  request.messages.push({role:'user',text:'Answers'});request.instructions='Updated preferences';
  const result=await(await post('run',request)).text();assert.match(result,/success/);assert.match(result,/Original task/);assert.match(result,/Updated preferences/);assert.doesNotMatch(result,/First preferences/);
});
