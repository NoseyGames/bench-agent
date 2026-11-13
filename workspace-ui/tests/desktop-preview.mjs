// Explicit manual UI harness, never started by the normal app or test suite.
// No remote model or account. Approve the first capture, inspect the next click
// permission, then choose No. This process never acknowledges actions itself.
import http from 'node:http';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {createInterface} from 'node:readline';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHost} from '../desktop-host.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const fixtureTitle='Bench desktop control test - disposable';
let host,fixture,lines,model,stopping=false;
function stop(){
  if(stopping)return;stopping=true;
  host?.close();model?.closeAllConnections();model?.close();fixture?.kill();lines?.close();
}
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,stop);

try{
  fixture=spawn(path.resolve(here,'../../test-pc.exe'),['--fixture'],{windowsHide:true,stdio:['ignore','pipe','inherit']});
  lines=createInterface({input:fixture.stdout});
  const windowId=await Promise.race([
    once(lines,'line').then(([line])=>line),
    once(fixture,'exit').then(()=>{throw Error('Disposable fixture exited before becoming ready.');}),
    new Promise((_,reject)=>setTimeout(()=>reject(Error('Disposable fixture did not start.')),10000).unref())
  ]);
  if(!/^[0-9]+:[0-9]+$/.test(windowId))throw Error('Invalid fixture window identifier.');
  fixture.once('exit',stop);
  model=http.createServer(async(req,res)=>{
    try{
      if(req.method==='GET'&&req.url==='/v1/models'){
        res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({data:[{id:'bench-ui-vision-fixture'}]}));return;
      }
      if(req.method!=='POST'||req.url!=='/v1/chat/completions'){res.writeHead(404);res.end();return;}
      let raw='';for await(const chunk of req){raw+=chunk;if(raw.length>40*1024*1024)throw Error('Fixture request too large.');}
      const body=JSON.parse(raw),content=body.messages?.at(-1)?.content;let reply;
      if(Array.isArray(content)){
        const observation=content.find(part=>part.type==='text')?.text||'';
        const frame=Number(observation.match(/Observation frame: (\d+)/)?.[1]);
        if(!Number.isInteger(frame)||frame<1)throw Error('Observation frame missing.');
        if(!content.some(part=>part.type==='image_url'&&part.image_url?.url?.startsWith('data:image/png;base64,')))throw Error('Screenshot missing.');
        // Only one click is ever proposed. If it is approved, stop with an
        // explicit blocked result; the harness cannot assess model accuracy.
        reply={action:frame===1?'click':'done',reason:frame===1?'Focus the empty edit field in the disposable fixture. Choose No to verify that denial closes this chat.':'Manual preview stopped after its single fixture action.',
          text:'',x:frame===1?80:0,y:frame===1?100:0,delta:0,frame,
          observation:'A disposable test app with an edit field and Verify click button is selected.',
          expected:frame===1?'The edit field receives focus.':'',verification:frame===1?'first':'uncertain',
          evidence:'This is a deterministic UI preview, not a real model assessment.',outcome:frame===1?'ongoing':'blocked'};
      }else reply={questions:['Use the app named Bench desktop control test - disposable?','Inspect the click permission and choose No to close this chat?']};
      res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply)}}]}));
    }catch(error){res.writeHead(500,{'Content-Type':'application/json'});res.end(JSON.stringify({error:error.message}));}
  });
  model.listen(0,'127.0.0.1');await once(model,'listening');
  const computerRoot=await fs.mkdtemp(path.join(os.tmpdir(),'bench-desktop-preview-'));
  host=createHost({engine:path.resolve(here,'../../bench-engine.exe'),computerRoot});
  host.server.listen(0,'127.0.0.1');await once(host.server,'listening');
  const origin=`http://127.0.0.1:${host.server.address().port}`;
  const configured=await fetch(origin+'/api/config',{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify({provider:'local',baseUrl:`http://127.0.0.1:${model.address().port}/v1`,model:'bench-ui-vision-fixture',jsonMode:true})});
  if(!configured.ok)throw Error('Could not configure the local fixture model.');
  console.log(JSON.stringify({url:origin+'/#'+host.token,fixtureTitle,windowId,
    instructions:'Choose Control an app, select the disposable fixture, send a task and answer the questions. Approve the initial screenshot, inspect the click permission, then choose No. No action is auto-approved. Fixture expires after five minutes.'}));
}catch(error){stop();console.error(error.message);process.exitCode=1;}
