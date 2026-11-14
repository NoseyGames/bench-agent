import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {createInterface} from 'node:readline';

const engine=path.resolve('..',process.platform==='win32'?'bench-engine.exe':'bench-engine');
const desktopPlan={actions:[{name:'observe',args:{}}]};
const codingPlan={summary:'Write a test marker',steps:['Write marker'],files:[{name:'marker.txt',content:'changed'}],commands:[],read_files:[]};

// Exercise the production C mode switch without a model server or desktop input.
// A loopback discard endpoint prevents an unexpected branch from reaching cloud.
async function runPlan(t,plan,request={},onEvent=()=>{}) {
  const base=await fs.mkdtemp(path.join(os.tmpdir(),'bench-session-mode-'));
  const root=path.join(base,'project');await fs.mkdir(root);
  const planFile=path.join(base,'plan.json');
  await fs.writeFile(planFile,typeof plan==='string'?plan:JSON.stringify(plan));
  const child=spawn(engine,['--plan',planFile],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  const closed=once(child,'close');child.stdin.on('error',()=>{});
  let stderr='';child.stderr.on('data',chunk=>stderr+=chunk);
  const timer=setTimeout(()=>child.kill(),15000);
  t.after(()=>{clearTimeout(timer);child.kill();});
  child.stdin.write(JSON.stringify({sessionMode:'project',root,endpoint:'http://127.0.0.1:9/v1/chat/completions',model:'offline-fixture',task:'Verify plan routing',phase:'execute',answers:'Use the fixture only',...request})+'\n');
  const events=[];
  for await(const line of createInterface({input:child.stdout})) {
    const event=JSON.parse(line);events.push(event);await onEvent(event,{child,root});
  }
  const [code,signal]=await closed;clearTimeout(timer);
  assert.equal(signal,null,`Engine did not finish: ${stderr}`);
  return {events,code,root,stderr};
}

function noActions(events) {
  assert.equal(events.some(e=>['preview','saved','command','desktop','screenshot'].includes(e.type)),false,JSON.stringify(events));
}

test('C project mode rejects desktop-only and mixed plans before any action',async t=>{
  for(const plan of [desktopPlan,{...codingPlan,...desktopPlan}]) {
    const result=await runPlan(t,plan);
    assert.equal(result.code,1);noActions(result.events);
    assert.match(result.events.find(e=>e.type==='error')?.text,/Invalid coding plan/);
    await assert.rejects(fs.access(path.join(result.root,'marker.txt')));
  }
});

test('C rejects malformed and truncated coding plans atomically',async t=>{
  const partialFiles={...codingPlan,files:[...codingPlan.files,{name:'second.txt',content:null}]};
  const partialCommands={...codingPlan,commands:[{kind:'inspect',command:'echo leaked>command-marker.txt',timeout_seconds:10},{kind:'click',command:'echo invalid',timeout_seconds:10}]};
  const truncated=JSON.stringify(codingPlan).slice(0,-1);
  const duplicate=truncated+',"summary":"duplicate"}';
  const commented=truncated+'/* comment */}';
  const secondRoot=JSON.stringify(codingPlan)+'{}';
  for(const plan of [partialFiles,partialCommands,truncated,duplicate,commented,secondRoot]) {
    const result=await runPlan(t,plan);
    assert.equal(result.code,1);noActions(result.events);
    assert.ok(result.events.some(e=>e.type==='error'));
    await assert.rejects(fs.access(path.join(result.root,'marker.txt')));
    await assert.rejects(fs.access(path.join(result.root,'command-marker.txt')));
  }
});

test('C rejects ambiguous legacy computer mode before project inspection',async t=>{
  const result=await runPlan(t,desktopPlan,{sessionMode:undefined,computerMode:true,root:''});
  assert.equal(result.code,1);noActions(result.events);
  assert.equal(result.events.some(e=>e.type==='status'&&e.name==='project'),false);
  const error=result.events.find(e=>e.type==='error')?.text;
  assert.match(error,/explicit sessionMode/);assert.doesNotMatch(error,/coding plan/);
});

test('C desktop mode without a selected app never enters project validation',async t=>{
  const result=await runPlan(t,desktopPlan,{sessionMode:'desktop',root:''});
  assert.equal(result.code,1);noActions(result.events);
  assert.equal(result.events.some(e=>e.type==='status'&&e.name==='project'),false);
  const error=result.events.find(e=>e.type==='error')?.text;
  assert.match(error,/Desktop mode requires a selected app/);assert.doesNotMatch(error,/coding plan/);
});

test('C rejects mode conflicts and unknown modes before inspecting files',async t=>{
  const requests=[
    {sessionMode:'project',desktopWindow:'123:456'},
    {sessionMode:'commands',desktopWindow:'123:456'},
    {sessionMode:'project',computerMode:true},
    {sessionMode:'invented'},
    {sessionMode:{name:'desktop'}}
  ];
  for(const request of requests) {
    const result=await runPlan(t,codingPlan,{root:'',...request});
    assert.equal(result.code,1);noActions(result.events);
    assert.equal(result.events.some(e=>e.type==='status'&&e.name==='project'),false);
    assert.match(result.events.find(e=>e.type==='error')?.text,/Invalid or conflicting sessionMode/);
  }
});

test('explicit C commands mode preserves no-project execution and separate ACKs',async t=>{
  const plan={summary:'Run approved fixture commands',steps:['First command','Second command'],files:[],read_files:[],commands:[
    {kind:'inspect',command:'echo first>first.txt',timeout_seconds:10},
    {kind:'inspect',command:'echo second>second.txt',timeout_seconds:10}
  ]};
  let commands=0;
  const result=await runPlan(t,plan,{sessionMode:'commands',computerMode:true},async(event,{child,root})=>{
    if(event.type!=='command')return;
    const marker=path.join(root,commands++===0?'first.txt':'second.txt');
    await assert.rejects(fs.access(marker));
    await new Promise(resolve=>setTimeout(resolve,75));
    await assert.rejects(fs.access(marker),'C must wait for this action’s own ACK');
    child.stdin.write('ack\n');
  });
  assert.equal(result.code,0,result.stderr);assert.equal(commands,2);
  assert.ok(result.events.some(e=>e.type==='success'),JSON.stringify(result.events));
  assert.match(await fs.readFile(path.join(result.root,'first.txt'),'utf8'),/first/);
  assert.match(await fs.readFile(path.join(result.root,'second.txt'),'utf8'),/second/);
});
