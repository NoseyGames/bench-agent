// Explicit interactive check: only the disposable fixture, no real model/account.
import http from 'node:http';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {createInterface} from 'node:readline';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHost} from '../desktop-host.mjs';
const fixture=spawn(path.resolve('../test-pc.exe'),['--fixture'],{windowsHide:true,stdio:['ignore','pipe','inherit']});
const lines=createInterface({input:fixture.stdout});
const window=await Promise.race([once(lines,'line').then(([line])=>line),new Promise((_,reject)=>setTimeout(()=>reject(Error('Fixture did not start')),10000).unref())]);
assert.match(window,/^[0-9]+:[0-9]+$/);
let images=0;
const model=http.createServer(async(req,res)=>{
  let raw='';for await(const c of req)raw+=c;assert.ok(!raw.includes('UNRELATED_PROJECT_SENTINEL'));const body=JSON.parse(raw);
  const content=body.messages.at(-1).content;let reply;
  if(Array.isArray(content)){
    assert.match(content.find(p=>p.type==='image_url').image_url.url,/^data:image\/png;base64,iVBOR/);images++;
    const actions=[{action:'click',x:80,y:100,reason:'Focus the disposable fixture edit field'},{action:'type',text:'Vision loop verified',reason:'Type only into the disposable fixture'},{action:'done',reason:'Fixture sequence complete; deterministic replies do not establish model vision accuracy.'}];reply=actions[images-1];
  }else reply={questions:['Use the disposable fixture?','Type the test message?']};
  res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply)}}]}));
});model.listen(0,'127.0.0.1');await once(model,'listening');
const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-vision-smoke-'));await fs.writeFile(path.join(root,'private.c'),'UNRELATED_PROJECT_SENTINEL');const host=createHost({computerRoot:root});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');
const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data)});
try{
  await post('config',{provider:'local',baseUrl:`http://127.0.0.1:${model.address().port}/v1`,model:'vision-fixture'});
  const request={id:'first',chatId:'fixture-chat',root:path.join(root,'missing-project'),desktopWindow:window,model:'vision-fixture',execute:true,messages:[{role:'user',text:'Type the message in the disposable fixture'}]};
  const questions=await post('run',request);assert.match(await questions.text(),/"type":"questions"/);
  await post('cancel',{id:'first'});
  const response=await post('run',{...request,id:'execute',messages:[...request.messages,{role:'user',text:'Yes, use the disposable fixture and type the test message.'}]});assert.equal(response.status,200);
  let pending='',approvals=0,screenshots=0,done=false;
  for await(const chunk of response.body){pending+=new TextDecoder().decode(chunk);let n;while((n=pending.indexOf('\n'))>=0){const e=JSON.parse(pending.slice(0,n));pending=pending.slice(n+1);if(e.type==='error')throw Error(e.text);if(e.type==='desktop'){assert.equal((await post('ack',{id:'execute',sequence:e.sequence})).status,403);assert.equal((await post('decision',{id:'execute',sequence:e.sequence,token:e.approvalToken,allow:true})).status,200);assert.equal((await post('ack',{id:'execute',sequence:e.sequence})).status,200);approvals++;}if(e.type==='screenshot'){assert.match(e.text,/^data:image\/png;base64,iVBOR/);screenshots++;}if(e.type==='review')done=true;}}
  assert.equal(approvals,3);assert.equal(screenshots,3);assert.equal(images,3);assert.ok(done);console.log('Desktop vision integration PASS: 3 approved actions, 3 real screenshots, local multimodal requests, real click and typing.');
}finally{host.close();model.close();fixture.kill();lines.close();}
