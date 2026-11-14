// Explicit interactive integration check. Input targets only our disposable
// native fixture. These deterministic replies test the harness, not model vision.
import http from 'node:http';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {createInterface} from 'node:readline';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHost} from '../desktop-host.mjs';

const png=/^data:image\/png;base64,iVBOR/;
const sentinel='UNRELATED_PROJECT_SENTINEL';
const counts={local:0,responses:0};
const serverErrors=[];
let fixture,fixtureLines,host,engine,engineLines,model;
let thinMode=false,thinCalls=0,rejectedReply=null;
let priorResponsesPrefix=null,priorCallId=null,priorLocalMessages=null;

function action(frame,mode){
  assert.ok(frame>=1&&frame<=4,'Unexpected additional model turn');
  if(frame===1)return {actions:[{name:'click',args:{x:80,y:100}},{name:'type',args:{text:mode==='local'?'Local loop verified':' Responses loop verified'}}]};
  if(frame===2)return {actions:[{name:'observe',args:{}}]};
  if(frame===3)return {actions:[{name:'screenshot',args:{}}]};
  return {actions:[]};
}

function assertObservation(content,type,frame){
  const image=content.find(part=>part.type===type);
  assert.equal(!!image,frame===4,'PNG must occur only after the explicit screenshot request');
  if(image)assert.match(type==='image_url'?image.image_url.url:image.image_url,png);
  const text=content.find(part=>part.type===(type==='image_url'?'text':'input_text')).text;
  const observation=JSON.parse(text);assert.equal(observation.frame,frame);assert.equal(typeof observation.driver_result,'string');
  assert.ok(observation.desktop||observation.desktop_delta,'Structured desktop state or delta required');
}

function responsesReply(body){
  assert.equal(body.store,false);assert.equal(body.parallel_tool_calls,false);
  assert.deepEqual(body.include,['reasoning.encrypted_content']);
  assert.equal(body.reasoning.effort,'medium');
  assert.deepEqual(body.tool_choice,{type:'function',name:'desktop_action'});
  assert.equal(body.tools.length,1);const tool=body.tools[0];
  assert.equal(tool.name,'desktop_action');assert.equal(tool.type,'function');assert.equal(tool.strict,true);
  assert.equal(tool.parameters.additionalProperties,false);
  assert.deepEqual(tool.parameters.required,['actions']);
  const latest=body.input.at(-1);
  if(priorResponsesPrefix){
    // Opaque reasoning, assistant phase and arguments must survive unchanged as
    // JSON values. Dropping earlier screenshots also breaks this check.
    assert.equal(body.input.length,priorResponsesPrefix.length+1);
    assert.deepEqual(body.input.slice(0,-1),priorResponsesPrefix);
    assert.equal(latest.type,'function_call_output');assert.equal(latest.call_id,priorCallId);
    assertObservation(latest.output,'input_image',counts.responses+1);
    if(counts.responses===3)assert.equal(latest.output.find(p=>p.type==='input_image').detail,'original');
    assert.equal(typeof JSON.parse(latest.output.find(p=>p.type==='input_text').text).driver_result,'string');
  }else{
    assert.equal(body.input.length,1);assert.equal(latest.role,'user');assertObservation(latest.content,'input_image',1);
  }
  if(counts.responses===1)assert.match(latest.output.find(p=>p.type==='input_text').text,/Responses loop verified/,'Post-batch UIA must show text actually typed');
  const frame=++counts.responses;
  const output=[
    {type:'reasoning',id:'rs_fixture_'+frame,summary:[],encrypted_content:'opaque_reasoning_fixture_'+frame},
    {type:'message',id:'msg_fixture_'+frame,status:'completed',role:'assistant',phase:'commentary',content:[{type:'output_text',text:'Inspecting the fixture.',annotations:[]}]},
    {type:'function_call',id:'fc_fixture_'+frame,call_id:'call_fixture_'+frame,name:'desktop_action',arguments:JSON.stringify(action(frame,'responses')),status:'completed'}
  ];
  priorResponsesPrefix=structuredClone([...body.input,...output]);priorCallId=output.at(-1).call_id;
  return {id:'resp_fixture_'+frame,status:'completed',output};
}

function localReply(body){
  const latest=body.messages.at(-1);let reply;
  if(Array.isArray(latest.content)){
    assertObservation(latest.content,'image_url',counts.local+1);
    if(counts.local===1)assert.match(latest.content.find(p=>p.type==='text').text,/Local loop verified/,'Post-batch UIA must show text actually typed');
    assert.ok(body.messages.length<=4,'Local history must remain bounded');
    if(priorLocalMessages)assert.equal(body.messages.at(-2).role,'assistant');
    priorLocalMessages=structuredClone(body.messages);
    reply=action(++counts.local,'local');
  }else reply={questions:['Use the disposable fixture?','Type the test message?']};
  return {choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply)}}]};
}

async function runLocal(window,root,baseUrl){
  host=createHost({computerRoot:root});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');
  const signal=AbortSignal.timeout(60000);
  const post=(route,data)=>fetch(`http://127.0.0.1:${host.server.address().port}/api/${route}`,{method:'POST',headers:{Authorization:'Bearer '+host.token,'Content-Type':'application/json'},body:JSON.stringify(data),signal});
  assert.equal((await post('config',{provider:'local',baseUrl,model:'vision-fixture'})).status,200);
  const request={id:'first',chatId:'fixture-chat',root:path.join(root,'missing-project'),desktopWindow:window,model:'vision-fixture',execute:true,messages:[{role:'user',text:'Type the message in the disposable fixture'}]};
  const questions=await post('run',request);assert.equal(questions.status,200);assert.match(await questions.text(),/"type":"questions"/);
  await post('cancel',{id:'first'});
  const response=await post('run',{...request,id:'execute',messages:[...request.messages,{role:'user',text:'Yes, use the disposable fixture and type the test message.'}]});assert.equal(response.status,200);
  let pending='',approvals=0,screenshots=0,observations=0,done=false;const decoder=new TextDecoder();
  for await(const chunk of response.body){
    pending+=decoder.decode(chunk,{stream:true});let end;
    while((end=pending.indexOf('\n'))>=0){
      const event=JSON.parse(pending.slice(0,end));pending=pending.slice(end+1);
      if(event.type==='error')throw Error(event.text);
      if(event.type==='desktop'){
        assert.equal((await post('ack',{id:'execute',sequence:event.sequence})).status,403);
        assert.equal((await post('decision',{id:'execute',sequence:event.sequence,token:event.approvalToken,allow:true})).status,200);
        assert.equal((await post('ack',{id:'execute',sequence:event.sequence})).status,200);approvals++;
      }
      if(event.type==='screenshot'){assert.match(event.text,png);screenshots++;}
      if(event.type==='observation')observations++;
      if(event.type==='review'){assert.equal(event.name,'desktop-completed');assert.match(event.text,/not independently verified/);done=true;}
    }
  }
  assert.equal(approvals,5);assert.equal(screenshots,1);assert.equal(counts.local,4);assert.equal(observations,4);assert.ok(done);
  await post('cancel',{id:'execute'});host.close();host=null;
  console.log('Local desktop integration PASS: approval-before-ACK, bounded structured history, 1 requested screenshot, per-action batch gates, real fixture click/type.');
}

async function runResponses(window,root,baseUrl){
  engine=spawn(path.resolve('../bench-engine.exe'),[],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let stderr='';engine.stderr.on('data',chunk=>stderr+=chunk);engine.stdin.on('error',()=>{});
  const closed=once(engine,'close');const deadline=setTimeout(()=>engine?.kill(),60000);
  engineLines=createInterface({input:engine.stdout});
  engine.stdin.write(JSON.stringify({key:'',endpoint:baseUrl+'/chat/completions',jsonMode:true,root:path.join(root,'missing-project'),desktopWindow:window,desktopProtocol:'responses',model:'gpt-6-astra',task:'Type the test message in the disposable fixture',answers:'Use only this test window.',phase:'execute'})+'\n');
  let approvals=0,screenshots=0,observations=0,done=false;
  try{
    for await(const line of engineLines){
      const event=JSON.parse(line);if(event.type==='error')throw Error(event.text);
      // Direct C harness exercises protocol/driver integration. The HTTP-host
      // test above proves that a raw ACK cannot bypass user approval.
      if(event.type==='desktop'){approvals++;engine.stdin.write('ack\n');}
      if(event.type==='screenshot'){assert.match(event.text,png);screenshots++;}
      if(event.type==='observation')observations++;
      if(event.type==='review'){assert.equal(event.name,'desktop-completed');done=true;}
    }
    const [code]=await closed;assert.equal(code,0,stderr);assert.equal(approvals,5);assert.equal(screenshots,1);assert.equal(counts.responses,4);assert.equal(observations,4);assert.ok(done);
    console.log('Responses desktop integration PASS: native tool calls, preserved encrypted reasoning/phase/history, structured deltas and one requested image tool result, real fixture click/type.');
  }finally{clearTimeout(deadline);engineLines.close();engineLines=null;engine.kill();engine=null;}
}

async function runRejectedPlans(window,root,baseUrl){
  const coding={summary:'Must not run in desktop mode',steps:['Write file'],files:[{name:'accidental.txt',content:'must not save'}],commands:[],read_files:[]};
  const proposals=[JSON.stringify(coding),JSON.stringify({...coding,actions:[{name:'click',args:{x:80,y:100}}]}),'{"actions":[{"name":"click","args":{"x":80,"y":100}},{"name":"type","args":'];
  for(const proposed of proposals){
    rejectedReply=proposed;
    engine=spawn(path.resolve('../bench-engine.exe'),[],{windowsHide:true,stdio:['pipe','pipe','pipe']});
    engine.stdin.on('error',()=>{});const closed=once(engine,'close'),timer=setTimeout(()=>engine?.kill(),30000);
    engineLines=createInterface({input:engine.stdout});const events=[];
    engine.stdin.write(JSON.stringify({sessionMode:'desktop',endpoint:baseUrl+'/chat/completions',root:path.join(root,'missing-project'),desktopWindow:window,desktopProtocol:'chat',model:'invalid-fixture',task:'Reject this plan',answers:'Only inspect the fixture',phase:'execute'})+'\n');
    try{
      for await(const line of engineLines){const event=JSON.parse(line);events.push(event);if(event.type==='desktop'){assert.equal(event.name,'observe','No partial action gate is allowed');engine.stdin.write('ack\n');}}
      const [code]=await closed;assert.equal(code,1);assert.ok(events.some(e=>e.type==='error'));
      assert.equal(events.filter(e=>e.type==='desktop').length,1);
      assert.ok(!events.some(e=>['preview','command','saved','plan'].includes(e.type)||e.name==='project'));
      assert.ok(!events.some(e=>e.type==='error'&&/coding plan|require summary/.test(e.text)),'Desktop must not reach the coding validator');
      await assert.rejects(fs.access(path.join(root,'accidental.txt')));
    }finally{clearTimeout(timer);engineLines.close();engineLines=null;engine.kill();engine=null;}
  }
  rejectedReply=null;
  console.log('Desktop schema isolation PASS: coding-only, mixed and truncated plans rejected by the real desktop loop before any input or file/command gate.');
}

async function runThin(root,baseUrl){
  fixture.kill();fixtureLines.close();
  fixture=spawn(path.resolve('../test-pc.exe'),['--thin-fixture'],{windowsHide:true,stdio:['ignore','pipe','inherit']});
  fixtureLines=createInterface({input:fixture.stdout});
  const [window]=await once(fixtureLines,'line');thinMode=true;
  engine=spawn(path.resolve('../bench-engine.exe'),[],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  const closed=once(engine,'close');const timer=setTimeout(()=>engine?.kill(),60000);engine.stdin.on('error',()=>{});
  let stderr='';engine.stderr.on('data',chunk=>stderr+=chunk);engineLines=createInterface({input:engine.stdout});
  engine.stdin.write(JSON.stringify({endpoint:baseUrl+'/chat/completions',root,desktopWindow:window,desktopProtocol:'chat',model:'thin-fixture',task:'Inspect the empty test app',answers:'Only inspect',phase:'execute'})+'\n');
  const gates=[];let screenshots=0,done=false;
  try{
    for await(const line of engineLines){const e=JSON.parse(line);if(e.type==='error')throw Error(e.text);if(e.type==='desktop'){gates.push(e.name);assert.equal(thinCalls,0,'No model call before screenshot sharing approval');engine.stdin.write('ack\n');}if(e.type==='screenshot')screenshots++;if(e.type==='review'){assert.equal(e.name,'desktop-completed');done=true;}}
    const [code]=await closed;assert.equal(code,0,stderr);assert.deepEqual(gates,['observe','screenshot']);assert.equal(screenshots,1);assert.equal(thinCalls,1);assert.ok(done);
    console.log('Thin accessibility integration PASS: separate observation/screenshot approvals, one fallback PNG, no input.');
  }finally{clearTimeout(timer);engineLines.close();engineLines=null;engine.kill();engine=null;}
}

try{
  fixture=spawn(path.resolve('../test-pc.exe'),['--fixture'],{windowsHide:true,stdio:['ignore','pipe','inherit']});
  fixtureLines=createInterface({input:fixture.stdout});
  const window=await Promise.race([once(fixtureLines,'line').then(([line])=>line),new Promise((_,reject)=>setTimeout(()=>reject(Error('Fixture did not start')),10000).unref())]);
  assert.match(window,/^[0-9]+:[0-9]+$/);
  model=http.createServer(async(req,res)=>{
    try{
      let raw='';for await(const chunk of req)raw+=chunk;
      assert.ok(!raw.includes(sentinel),'Desktop request leaked project files');
      const body=JSON.parse(raw);assert.ok(req.url==='/v1/chat/completions'||req.url==='/v1/responses');
      let reply;
      if(rejectedReply!==null)reply={choices:[{finish_reason:'stop',message:{content:rejectedReply}}]};
      else if(thinMode){const content=body.messages.at(-1).content;assert.ok(content.some(p=>p.type==='image_url'),'Thin state requires approved screenshot');assert.match(content.find(p=>p.type==='text').text,/\"thin\":true/);thinCalls++;reply={choices:[{finish_reason:'stop',message:{content:JSON.stringify({actions:[]})}}]};}
      else reply=req.url==='/v1/responses'?responsesReply(body):localReply(body);
      res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(reply));
    }catch(error){serverErrors.push(error);res.writeHead(500,{'Content-Type':'application/json'});res.end(JSON.stringify({error:error.message}));}
  });
  model.listen(0,'127.0.0.1');await once(model,'listening');
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-vision-smoke-'));
  await fs.writeFile(path.join(root,'private.c'),sentinel);
  const baseUrl=`http://127.0.0.1:${model.address().port}/v1`;
  await runLocal(window,root,baseUrl);await runResponses(window,root,baseUrl);await runRejectedPlans(window,root,baseUrl);await runThin(root,baseUrl);
  assert.deepEqual(serverErrors,[]);
}finally{
  host?.close();engine?.kill();engineLines?.close();model?.closeAllConnections();model?.close();fixture?.kill();fixtureLines?.close();
}
