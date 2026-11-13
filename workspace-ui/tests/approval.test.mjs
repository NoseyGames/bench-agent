import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import {createHost} from '../desktop-host.mjs';
import {assessAction} from '../action-policy.mjs';

test('opaque, disguised, and destructive commands all require permission',()=>{
 for(const text of ['echo hello','powershell -EncodedCommand abc','python -c "print(1)"','cmd /c del file','rm -rf folder'])assert.equal(assessAction({type:'command',text}).requiresApproval,true);
 assert.equal(assessAction({type:'command',text:'Remove-Item data'}).severity,'critical');
 assert.equal(assessAction({type:'preview',before:'old'}).requiresApproval,true);
});
async function setup(t){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-permission-')),computerRoot=path.join(root,'scratch');
 const plan=path.join(root,'plan.json');await fs.writeFile(plan,JSON.stringify({summary:'Write marker',steps:['Write marker'],files:[],read_files:[],commands:[{kind:'inspect',command:'echo approved>marker.txt',timeout_seconds:10},{kind:'inspect',command:'echo next>second.txt',timeout_seconds:10}]}));
 const host=createHost({engine:path.resolve('..','bench-engine'+(process.platform==='win32'?'.exe':'')),engineArgs:['--plan',plan],computerRoot});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());
 const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data)});
 await post('config',{key:'offline-fixture'});
 const request={id:'test',chatId:'chat',root:'',model:'fixture',messages:[{role:'user',text:'Write marker'}],execute:true};return {post,request,computerRoot};
}
async function consume(response,onEvent){let buffer='';const events=[];for await(const chunk of response.body){buffer+=new TextDecoder().decode(chunk);let n;while((n=buffer.indexOf('\n'))>=0){const event=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);events.push(event);await onEvent(event);}}return events;}
test('no-project commands cannot bypass approval; denial stops and closes chat',async t=>{
 const f=await setup(t);const r=await f.post('run',f.request);assert.equal(r.status,200);let commands=0;
 await consume(r,async e=>{if(e.type!=='command')return;commands++;
   assert.equal(e.workingDirectory,f.computerRoot);assert.equal((await f.post('ack',{id:'test',sequence:e.sequence})).status,403);
   assert.equal((await f.post('decision',{id:'test',sequence:e.sequence,token:'wrong',allow:true})).status,409);
   await assert.rejects(fs.access(path.join(f.computerRoot,'marker.txt')));
   assert.equal((await f.post('decision',{id:'test',sequence:e.sequence,token:e.approvalToken,allow:false})).status,200);
 });
 assert.equal(commands,1);await assert.rejects(fs.access(path.join(f.computerRoot,'marker.txt')));await assert.rejects(fs.access(path.join(f.computerRoot,'second.txt')));
 assert.equal((await f.post('run',{...f.request,id:'retry'})).status,403);
});
test('each action needs fresh permission; approved no-project commands really execute',async t=>{
 const f=await setup(t);let priorToken,commands=0;
 const events=await consume(await f.post('run',f.request),async e=>{if(e.type!=='command')return;commands++;
   if(priorToken)assert.equal((await f.post('decision',{id:'test',sequence:e.sequence,token:priorToken,allow:true})).status,409);
   assert.equal((await f.post('decision',{id:'test',sequence:e.sequence,token:e.approvalToken,allow:true})).status,200);
   assert.equal((await f.post('ack',{id:'test',sequence:e.sequence})).status,200);priorToken=e.approvalToken;
 });
 assert.equal(commands,2);assert.ok(events.some(e=>e.type==='success'));assert.match(await fs.readFile(path.join(f.computerRoot,'marker.txt'),'utf8'),/approved/);assert.match(await fs.readFile(path.join(f.computerRoot,'second.txt'),'utf8'),/next/);
});

