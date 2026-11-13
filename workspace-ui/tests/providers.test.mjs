import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {once} from 'node:events';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {profile,discover} from '../providers.mjs';
import {createHost} from '../desktop-host.mjs';

test('provider profiles restrict destinations and do not require a local key',()=>{
  assert.equal(profile({provider:'ollama'}).baseUrl,'http://127.0.0.1:11434/v1');
  assert.equal(profile({provider:'lmstudio'}).jsonMode,false);
  for(const baseUrl of ['https://example.com/v1','http://127.0.0.1.evil/v1','http://secret@localhost/v1','http://localhost/v1?token=x'])assert.throws(()=>profile({provider:'local',baseUrl}));
  assert.throws(()=>profile({provider:'openai',baseUrl:'http://localhost/v1'}));
});

test('local HTTP models + real C clarification, planning, save, build and test without cloud credentials',async t=>{
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-local-'));
  await fs.writeFile(path.join(root,'main.c'),'int main(void) { return 1; }\n');
  const requests=[];
  const model=http.createServer(async(req,res)=>{
    let raw='';for await(const c of req)raw+=c;
    requests.push({url:req.url,authorization:req.headers.authorization,body:raw?JSON.parse(raw):null});
    res.setHeader('Content-Type','application/json');
    if(req.url==='/v1/models'){res.end(JSON.stringify({data:[{id:'local-test-model'}]}));return;}
    const question=requests.filter(r=>r.url==='/v1/chat/completions').length===1;
    const value=question?{questions:['Should the exit code be zero?','Use GCC to compile and test?']}:{summary:'Fix the test',steps:['Save','Build','Test'],files:[{name:'main.c',content:'int main(void) { return 0; }\n'}],read_files:[],commands:[{kind:'build',command:'gcc main.c -o check.exe',timeout_seconds:30},{kind:'test',command:process.platform==='win32'?'check.exe':'./check.exe',timeout_seconds:30}]};
    res.end(JSON.stringify({choices:[{finish_reason:'stop',message:{content:'```json\n'+JSON.stringify(value)+'\n```'}}]}));
  });
  model.listen(0,'127.0.0.1');await once(model,'listening');t.after(()=>model.close());
  const baseUrl=`http://127.0.0.1:${model.address().port}/v1`;
  assert.deepEqual(await discover(profile({provider:'local',baseUrl}),''),['local-test-model']);
  const host=createHost();host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());
  const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data)});
  await post('config',{key:'cloud-key-must-not-leak'});
  const c=await(await post('config',{provider:'local',baseUrl,model:'local-test-model',jsonMode:false})).json();
  assert.equal(c.configured,true);assert.equal(c.hasKey,false);
  const run=async(id,text)=>{
    const res=await post('run',{id,chatId:'local-chat',root,model:c.model,messages:[{role:'user',text}],execute:true});assert.equal(res.status,200);
    const events=[];let buffer='';for await(const chunk of res.body){buffer+=new TextDecoder().decode(chunk);let n;while((n=buffer.indexOf('\n'))>=0){const e=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);events.push(e);if(['preview','command'].includes(e.type))assert.equal((await post('ack',{id,sequence:e.sequence})).status,200);}}return events;
  };
  assert.ok((await run('questions','Fix the program')).some(e=>e.type==='questions'));
  const execution=await run('execute','Yes, use GCC and return zero.');assert.ok(execution.some(e=>e.type==='success'),JSON.stringify(execution));
  assert.equal(await fs.readFile(path.join(root,'main.c'),'utf8'),'int main(void) { return 0; }\n');
  for(const r of requests){assert.equal(r.authorization,undefined);if(r.body){assert.equal(r.body.model,'local-test-model');assert.equal(r.body.response_format,undefined);}}
});
