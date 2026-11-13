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
let priorResponsesPrefix=null,priorCallId=null,priorLocalMessages=null;

function action(frame,mode){
  assert.ok(frame>=1&&frame<=3,'Unexpected additional model turn');
  const common={reason:'Exercise only the disposable fixture',text:'',x:0,y:0,delta:0,frame,
    observation:'The disposable fixture is the selected app.',expected:'The edit field receives focus.',
    verification:frame===1?'first':'verified',evidence:'Deterministic fixture reply; this is not a model vision assessment.',outcome:'ongoing'};
  if(frame===1)return {...common,action:'click',x:80,y:100};
  if(frame===2)return {...common,action:'type',text:mode==='local'?'Local loop verified':' Responses loop verified',expected:'The fixture edit field displays the test message.'};
  return {...common,action:'done',expected:'',outcome:'completed',reason:'Fixture sequence complete; deterministic replies do not establish model vision accuracy.'};
}

function assertImage(content,type){
  const image=content.find(part=>part.type===type);assert.ok(image,'Missing screenshot');
  assert.match(type==='image_url'?image.image_url.url:image.image_url,png);
}

function responsesReply(body){
  assert.equal(body.store,false);assert.equal(body.parallel_tool_calls,false);
  assert.deepEqual(body.include,['reasoning.encrypted_content']);
  assert.equal(body.reasoning.effort,'medium');
  assert.deepEqual(body.tool_choice,{type:'function',name:'desktop_action'});
  assert.equal(body.tools.length,1);const tool=body.tools[0];
  assert.equal(tool.name,'desktop_action');assert.equal(tool.type,'function');assert.equal(tool.strict,true);
  assert.equal(tool.parameters.additionalProperties,false);
  assert.deepEqual([...tool.parameters.required].sort(),['action','reason','text','x','y','delta','frame','observation','expected','verification','evidence','outcome'].sort());
  const latest=body.input.at(-1);
  if(priorResponsesPrefix){
    // Opaque reasoning, assistant phase and arguments must survive unchanged as
    // JSON values. Dropping earlier screenshots also breaks this check.
    assert.equal(body.input.length,priorResponsesPrefix.length+1);
    assert.deepEqual(body.input.slice(0,-1),priorResponsesPrefix);
    assert.equal(latest.type,'function_call_output');assert.equal(latest.call_id,priorCallId);
    assertImage(latest.output,'input_image');
    assert.equal(latest.output.find(p=>p.type==='input_image').detail,'original');
    assert.match(latest.output.find(p=>p.type==='input_text').text,/Driver result:/);
  }else{
    assert.equal(body.input.length,1);assert.equal(latest.role,'user');assertImage(latest.content,'input_image');
    assert.equal(latest.content.find(p=>p.type==='input_image').detail,'original');
  }
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
    assertImage(latest.content,'image_url');
    if(priorLocalMessages){assert.equal(body.messages.length,priorLocalMessages.length+2);assert.deepEqual(body.messages.slice(0,-2),priorLocalMessages);assert.equal(body.messages.at(-2).role,'assistant');}
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
  let pending='',approvals=0,screenshots=0,verified=0,done=false;const decoder=new TextDecoder();
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
      if(event.type==='verification')verified++;
      if(event.type==='review'){assert.equal(event.name,'desktop-completed');done=true;}
    }
  }
  assert.equal(approvals,3);assert.equal(screenshots,3);assert.equal(counts.local,3);assert.ok(verified>=2);assert.ok(done);
  await post('cancel',{id:'execute'});host.close();host=null;
  console.log('Local desktop integration PASS: approval-before-ACK, retained history, 3 screenshots, real fixture click/type.');
}

async function runResponses(window,root,baseUrl){
  engine=spawn(path.resolve('../bench-engine.exe'),[],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let stderr='';engine.stderr.on('data',chunk=>stderr+=chunk);engine.stdin.on('error',()=>{});
  const closed=once(engine,'close');const deadline=setTimeout(()=>engine?.kill(),60000);
  engineLines=createInterface({input:engine.stdout});
  engine.stdin.write(JSON.stringify({key:'',endpoint:baseUrl+'/chat/completions',jsonMode:true,root:path.join(root,'missing-project'),desktopWindow:window,desktopProtocol:'responses',model:'gpt-6-astra',task:'Type the test message in the disposable fixture',answers:'Use only this test window.',phase:'execute'})+'\n');
  let approvals=0,screenshots=0,verified=0,done=false;
  try{
    for await(const line of engineLines){
      const event=JSON.parse(line);if(event.type==='error')throw Error(event.text);
      // Direct C harness exercises protocol/driver integration. The HTTP-host
      // test above proves that a raw ACK cannot bypass user approval.
      if(event.type==='desktop'){approvals++;engine.stdin.write('ack\n');}
      if(event.type==='screenshot'){assert.match(event.text,png);screenshots++;}
      if(event.type==='verification')verified++;
      if(event.type==='review'){assert.equal(event.name,'desktop-completed');done=true;}
    }
    const [code]=await closed;assert.equal(code,0,stderr);assert.equal(approvals,3);assert.equal(screenshots,3);assert.equal(counts.responses,3);assert.ok(verified>=2);assert.ok(done);
    console.log('Responses desktop integration PASS: native tool calls, preserved encrypted reasoning/phase/history, matched image tool results, real fixture click/type.');
  }finally{clearTimeout(deadline);engineLines.close();engineLines=null;engine.kill();engine=null;}
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
      const reply=req.url==='/v1/responses'?responsesReply(body):localReply(body);
      res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(reply));
    }catch(error){serverErrors.push(error);res.writeHead(500,{'Content-Type':'application/json'});res.end(JSON.stringify({error:error.message}));}
  });
  model.listen(0,'127.0.0.1');await once(model,'listening');
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'bench-vision-smoke-'));
  await fs.writeFile(path.join(root,'private.c'),sentinel);
  const baseUrl=`http://127.0.0.1:${model.address().port}/v1`;
  await runLocal(window,root,baseUrl);await runResponses(window,root,baseUrl);
  assert.deepEqual(serverErrors,[]);
}finally{
  host?.close();engine?.kill();engineLines?.close();model?.closeAllConnections();model?.close();fixture?.kill();fixtureLines?.close();
}
