import './app.js';
import {askAction} from './approval.js';
import {connectImports} from './app-import.js';
import {createFirstRun} from './first-run.js';
import {connectModels} from './connection.js';
import {desktopInstructions} from './preferences.js';
const $=s=>document.querySelector(s),ui=window.workspaceUI;
let token=location.hash.slice(1)||sessionStorage.getItem('bench-token')||'';
if(location.hash){sessionStorage.setItem('bench-token',token);history.replaceState(null,'',location.pathname);}
let paused=false,active=null,activeDesktop=false,cursor={x:30,y:30},savedDiff='',pendingDiff='',mapping={},skipPreview=false;
try{mapping=JSON.parse(localStorage.getItem('bench-project-paths')||'{}');}catch{}
const toolbar=document.createElement('div');toolbar.id='bench-toolbar';
toolbar.innerHTML='<label class="root-label">Project folder <input id="bench-root" placeholder="C:\\path\\to\\existing-project" aria-label="Existing project folder"></label><button id="bench-connect">Connection</button><button id="bench-toggle">Workspace</button>';
$('#chat-main header').after(toolbar);
const browse=document.createElement('button');browse.textContent='Browse';browse.id='bench-browse';$('#bench-connect').before(browse);
const quit=document.createElement('button');quit.textContent='Quit';quit.title='Stop the local host and all active commands';$('#window-toolbar').append(quit);
const work=document.createElement('aside');work.id='bench-work';work.setAttribute('aria-label','Live agent workspace');
work.innerHTML='<div class="work-head"><strong>Agent workspace</strong><button id="bench-close">Hide</button></div><div id="bench-state" role="status">Ready · choose a model and send a task</div><div id="bench-file">No file open</div><div id="bench-editor"><pre id="bench-code">Your agent’s file edits will appear here, before they are saved.</pre><img id="bench-cursor" src="./ai.cur" alt="Agent cursor" hidden></div><pre id="bench-terminal" aria-label="Command output">Build and test output will stream here.</pre><div id="bench-report" hidden></div><div class="bench-controls"><label>Speed <select id="bench-speed"><option value="0.25">0.25×</option><option value="0.5" selected>0.5× · readable</option><option value="1">1×</option><option value="2">2×</option><option value="4">4×</option></select></label><button id="bench-pause">Pause</button><button id="bench-stop" disabled>Stop</button></div><div class="bench-controls"><label><input type="checkbox" id="bench-execute"> Allow edits and real commands</label></div>';
$('#workspace-shell').append(work);
$('#bench-speed').parentElement.firstChild.textContent='Preview speed ';
$('#bench-speed').parentElement.title='Controls file previews; native desktop input uses its own readable pace';
async function post(route,data,signal){const r=await fetch('/api/'+route,{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(data),signal});if(!r.ok)throw Error((await r.json()).error||r.statusText);return r;}
connectImports(post,ui);
const state=text=>$('#bench-state').textContent=text;
// App selection is deliberately session-only: stale HWNDs are never restored.
let desktopTarget='';
const desktopImage=document.createElement('img');desktopImage.id='bench-desktop-image';desktopImage.hidden=true;desktopImage.alt='Latest screenshot of the selected desktop app';$('#bench-editor').before(desktopImage);
// Keep the current observation and the model's evidence separate from input delivery.
const desktopProgress=document.createElement('section');desktopProgress.id='bench-desktop-progress';desktopProgress.hidden=true;desktopProgress.setAttribute('aria-label','Desktop progress');
const desktopHeading=document.createElement('strong');desktopHeading.textContent='Inspecting the selected app';
const desktopObservation=document.createElement('p'),desktopExpected=document.createElement('p'),desktopVerification=document.createElement('p');desktopVerification.className='desktop-verification';
desktopProgress.append(desktopHeading,desktopObservation,desktopExpected,desktopVerification);desktopImage.before(desktopProgress);
function actionDetails(event){try{const value=JSON.parse(event.text);return value&&typeof value==='object'&&!Array.isArray(value)?value:null;}catch{return null;}}
function showDesktopAction(event){
  const action=actionDetails(event);desktopProgress.hidden=false;work.hidden=false;
  desktopHeading.textContent='Next action · '+event.name;
  desktopObservation.textContent=typeof action?.observation==='string'?'Observed: '+action.observation:'';
  desktopExpected.textContent=typeof action?.expected==='string'?'Expected: '+action.expected:'';
  if(typeof action?.evidence==='string')desktopVerification.textContent='Model check'+(typeof action.verification==='string'?' · '+action.verification:'')+': '+action.evidence;
}
const desktopButton=document.createElement('button');desktopButton.textContent='Control an app';desktopButton.title='Choose a Windows app for observed mouse and keyboard control';$('#chat-main header .header-actions').prepend(desktopButton);
desktopButton.onclick=async()=>{
  desktopButton.disabled=true;
  try{
    const {windows}=await(await post('desktop-windows',{})).json();const d=document.createElement('dialog');d.className='bench-approval';
    const h=document.createElement('h2');h.textContent='Control a desktop app';const p=document.createElement('p');p.textContent='Bench reads the app’s accessible controls, asks before each action, then checks the result. Screenshots are optional and need a separate Yes; choose a vision-capable model if the app needs them. OpenAI or your selected local model receives the observations. Stop anytime with Ctrl+Alt+Shift+F10.';
    const select=document.createElement('select');select.setAttribute('aria-label','Desktop app');select.style.maxWidth='100%';
    for(const w of windows){const o=document.createElement('option');o.value=w.id;o.textContent=w.title;select.append(o);}
    const use=document.createElement('button');use.textContent='Use selected app';use.disabled=!windows.length;use.onclick=()=>{desktopTarget=select.value;desktopButton.textContent='App: '+select.selectedOptions[0].textContent.slice(0,28);$('#bench-execute').checked=true;d.close();};
    const off=document.createElement('button');off.textContent='Use commands instead';off.onclick=()=>{desktopTarget='';desktopButton.textContent='Control an app';d.close();};
    const close=document.createElement('button');close.textContent='Cancel';close.onclick=()=>d.close();d.onclose=()=>d.remove();d.append(h,p,select,use,off,close);document.body.append(d);d.showModal();
  }catch(e){state(e.message);ui.notify('question',e.message);}finally{desktopButton.disabled=false;}
};
const projectDialog=document.createElement('dialog');projectDialog.id='bench-project-dialog';projectDialog.innerHTML='<h2>Open a project</h2><p>Choose the folder containing the code you want to work on.</p><div class="project-actions"></div>';document.body.append(projectDialog);projectDialog.querySelector('.project-actions').before(toolbar.querySelector('.root-label'));projectDialog.querySelector('.project-actions').append(browse);const projectDone=document.createElement('button');projectDone.textContent='Done';projectDone.className='primary';projectDone.onclick=()=>{updateSetup();projectDialog.close();};projectDialog.querySelector('.project-actions').append(projectDone);const computerButton=document.createElement('button');computerButton.textContent='Use computer mode';computerButton.onclick=()=>{$('#bench-root').value='';$('#bench-root').onchange();projectDialog.close();};projectDialog.querySelector('.project-actions').append(computerButton);
const projectButton=document.createElement('button');projectButton.id='bench-project-button';projectButton.textContent='Open project';projectButton.onclick=()=>{projectDialog.showModal();$('#bench-root').focus();};$('#chat-main header .header-actions').before(projectButton);$('#chat-main header .header-actions').prepend($('#bench-toggle'));
const models=connectModels(post,c=>{ui.setModel(c.model||'Choose model');ui.setConnectionLabel(c.configured&&c.model?(c.provider==='openai'?'Cloud model':'Local model'):'Choose a model');$('#bench-connect').textContent=c.provider==='openai'?'OpenAI · Models':'Local · Models';updateSetup();});
window.addEventListener('bench-open-models',()=>models.open());
function updateSetup(){const root=$('#bench-root').value.trim();projectButton.textContent=root?root.split(/[\\/]/).filter(Boolean).at(-1):'Computer';projectButton.title=root||'Computer mode - no project selected';}
work.hidden=true;
const binding=()=>{const s=ui.exportState(),c=s.chats.find(c=>c.id===s.active);return c?.project||c?.id||'default';};
let bound=binding();$('#bench-root').value=mapping[bound]||'';
$('#bench-root').onchange=()=>{mapping[bound]=$('#bench-root').value.trim();localStorage.setItem('bench-project-paths',JSON.stringify(mapping));updateSetup();};
$('#bench-root').oninput=$('#bench-root').onchange;
browse.onclick=async()=>{try{const r=await(await post('folder',{})).json();if(r.path){$('#bench-root').value=r.path;$('#bench-root').onchange();projectDialog.close();}}catch(e){state(e.message);}};
quit.onclick=async()=>{active?.abort();try{await post('shutdown',{});state('Host stopped. You can close this window.');}catch(e){state(e.message);}};
new MutationObserver(()=>{const next=binding();if(next!==bound){bound=next;$('#bench-root').value=mapping[bound]||'';updateSetup();}}).observe($('#chat-title'),{childList:true,characterData:true,subtree:true});
$('#project-select').addEventListener('change',()=>{bound=binding();$('#bench-root').value=mapping[bound]||'';updateSetup();});
$('#bench-connect').onclick=()=>models.open();
$('#bench-toggle').onclick=()=>work.hidden=!work.hidden;$('#bench-close').onclick=()=>work.hidden=true;
$('#bench-pause').onclick=()=>{paused=!paused;$('#bench-pause').textContent=paused?'Resume':'Pause';state(paused?(activeDesktop?'Paused before the next input · current input or model request may finish. Stop cancels the task.':'Paused · active commands/API requests continue'):'Resuming visual execution');};
$('#bench-stop').onclick=()=>active?.abort();
const skip=document.createElement('button');skip.textContent='Skip animation';skip.title='Finish the current preview without waiting for every character';$('#bench-pause').before(skip);skip.onclick=()=>{skipPreview=true;};
function check(signal){if(signal.aborted)throw new DOMException('Cancelled','AbortError');}
async function waitForResume(signal){
  // Permission and execution are separate: Yes never bypasses a user pause.
  while(paused){check(signal);await new Promise(resolve=>setTimeout(resolve,50));}
  check(signal);
}
// Advance on visible frames, cap each timestep, and stop time while paused.
async function animate(duration,signal,paint=()=>{}) {
  let elapsed=0,last=performance.now();
  while(elapsed<duration){await new Promise(requestAnimationFrame);check(signal);const now=performance.now();if(!paused)elapsed+=skipPreview?duration:Math.min(50,now-last)*Number($('#bench-speed').value);last=now;paint(Math.min(1,elapsed/duration));}
}
async function move(x,y,signal){const start={...cursor},distance=Math.hypot(x-start.x,y-start.y);await animate(Math.max(500,distance/180*1000),signal,t=>{const ease=t*t*(3-2*t);cursor={x:start.x+(x-start.x)*ease,y:start.y+(y-start.y)*ease};$('#bench-cursor').style.left=cursor.x+'px';$('#bench-cursor').style.top=cursor.y+'px';});}
function diff(event){if(event.before===event.text)return "";const old=event.before?event.before.split('\n'):[],next=event.text?event.text.split('\n'):[];return `diff --git a/${event.name} b/${event.name}\n--- a/${event.name}\n+++ b/${event.name}\n@@ -${old.length?1:0},${old.length} +${next.length?1:0},${next.length} @@\n`+old.map(l=>'-'+l+'\n').join('')+next.map(l=>'+'+l+'\n').join('');}
async function preview(event,signal){
  skipPreview=false;
  work.hidden=false;$('#bench-cursor').hidden=false;$('#bench-file').textContent=event.name+' · preview (not saved)';
  $('#bench-code').textContent=event.before;$('#bench-editor').scrollTop=0;
  await move(40,24,signal);$('#bench-cursor').classList.add('click');await animate(500,signal);$('#bench-cursor').classList.remove('click');
  const node=document.createTextNode(''),caret=document.createElement('span');caret.id='bench-caret';$('#bench-code').replaceChildren(node,caret);
  // Code points keep surrogate pairs intact. 32 chars/sec at 1×, 16 at default.
  const chars=Array.from(event.text);let shown=0;
  await animate(Math.max(800,chars.length/32*1000),signal,t=>{const count=Math.floor(chars.length*t);if(count!==shown){node.appendData(chars.slice(shown,count).join(''));shown=count;caret.scrollIntoView({block:'nearest'});const a=caret.getBoundingClientRect(),b=$('#bench-editor').getBoundingClientRect();cursor={x:a.left-b.left+$('#bench-editor').scrollLeft+8,y:a.top-b.top+$('#bench-editor').scrollTop+6};$('#bench-cursor').style.left=cursor.x+'px';$('#bench-cursor').style.top=cursor.y+'px';}});
  caret.remove();skipPreview=false;await animate(1800,signal);
}
ui.setAdapter(async function*({chatId,messages,preferences,signal}){
  const controller=new AbortController();active=controller;const abort=()=>controller.abort();signal.addEventListener('abort',abort,{once:true});if(signal.aborted)abort();
   const id=crypto.randomUUID();let cancellation;
   const cancel=()=>cancellation||=post('cancel',{id}).catch(()=>{});controller.signal.addEventListener('abort',cancel,{once:true});
  paused=false;activeDesktop=!!desktopTarget;work.dataset.mode=activeDesktop?'desktop':'project';$('#bench-pause').textContent='Pause';$('#bench-stop').disabled=false;$('#bench-root').disabled=true;$('#bench-execute').disabled=true;
  $('#bench-speed').disabled=activeDesktop;$('#bench-speed').title='File preview speed; desktop input uses its own readable pace';skip.hidden=activeDesktop;
  $('#bench-report').hidden=true;savedDiff='';pendingDiff='';browse.disabled=true;desktopButton.disabled=true;desktopImage.hidden=true;desktopImage.removeAttribute('src');desktopProgress.hidden=true;desktopObservation.textContent='';desktopExpected.textContent='';desktopVerification.textContent='';let terminal=false;
  try{
    if(!models.getProfile())await models.ready;
    if(!models.getProfile()?.configured||!models.getProfile()?.model){models.open();throw Error('Choose a model to start your task.');}
    // Queue draining can change chats before the title observer updates the toolbar.
    const chat=ui.exportState().chats.find(c=>c.id===chatId),root=mapping[chat?.project||chatId]||'';

    const r=await post('run',{id,chatId,root,desktopWindow:desktopTarget,model:models.getProfile().model,messages,instructions:desktopInstructions(preferences),execute:$('#bench-execute').checked,readOnly:preferences.permission==='read'},controller.signal);
    const reader=r.body.getReader(),decoder=new TextDecoder();let buffer='';
    while(true){const {value,done}=await reader.read();buffer+=decoder.decode(value||new Uint8Array(),{stream:!done});let end;
      while((end=buffer.indexOf('\n'))>=0){const event=JSON.parse(buffer.slice(0,end));buffer=buffer.slice(end+1);check(controller.signal);
        if(event.risk?.requiresApproval){
          if(event.type==='desktop')showDesktopAction(event);
          state('Paused - permission required');ui.notify('permission','Review the next action before it runs');
          const allow=await askAction(event,controller.signal);check(controller.signal);
          await post('decision',{id,sequence:event.sequence,token:event.approvalToken,allow},controller.signal);
          if(!allow){terminal=true;state('Stopped - permission denied; chat closed');ui.closeDeniedChat(chatId);return;}
        }
        if(event.type==='desktop'){if(paused)state('Approved · paused before input. Resume when ready.');await waitForResume(controller.signal);state('Approved desktop action · switch to the selected app');await post('ack',{id,sequence:event.sequence},controller.signal);yield '\nApproved desktop action: '+event.name+'\n';continue;}
        if(event.type==='screenshot'){work.hidden=false;desktopImage.hidden=false;desktopImage.src=event.text;state('New screenshot received · checking the result');continue;}
        if(event.type==='observation'){work.hidden=false;desktopProgress.hidden=false;desktopHeading.textContent='Observation '+event.name;desktopObservation.textContent=event.text;continue;}
        if(event.type==='verification'){desktopProgress.hidden=false;desktopVerification.dataset.result=event.name;desktopVerification.textContent='Model check · '+event.name+': '+event.text;yield '\nModel check ('+event.name+'): '+event.text+'\n';continue;}
        if(event.type==='replan'){desktopProgress.hidden=false;desktopHeading.textContent='Taking another look';desktopExpected.textContent=event.text;state(event.text);yield '\n'+event.text+'\n';continue;}
        if(event.type==='status'){if(!paused)state(event.text);continue;}
        if(event.type==='questions'){terminal=true;state('Waiting for your answers');ui.notify('question','Answer the clarification questions in chat');yield event.text+'\nReply with your answers to start.\n';}
        if(event.type==='plan'){yield '\n'+event.text+'\n';state('Working through the plan');}
        if(event.type==='step')yield '• '+event.text+'\n';
        if(event.type==='preview'){state('Previewing '+event.name);await preview(event,controller.signal);await post('ack',{id,sequence:event.sequence},controller.signal);pendingDiff=diff(event);}
        if(event.type==='saved'){savedDiff+=pendingDiff;pendingDiff='';if(savedDiff.length<512*1024)ui.setChatDiff(chatId,savedDiff);$('#bench-file').textContent=event.name+' · saved';yield '\nSaved '+event.name+'\n';}
        if(event.type==='command'){work.hidden=false;state('Preparing '+event.name);$('#bench-terminal').textContent='$ '+event.text+'\n';await animate(1200,controller.signal);await post('ack',{id,sequence:event.sequence},controller.signal);yield '\n'+event.name+': '+event.text+'\n';}
        if(event.type==='output'){$('#bench-terminal').textContent=event.text;$('#bench-terminal').scrollTop=$('#bench-terminal').scrollHeight;}
        if(event.type==='exit'){yield event.text+'\n';$('#bench-terminal').textContent+='\n'+event.text;}
        if(event.type==='review'){terminal=true;work.hidden=false;const desktopResult=event.name==='desktop-completed'||event.name==='desktop-blocked';const heading=desktopResult?(event.name==='desktop-blocked'?'Desktop task blocked':'Desktop result · model assessment'):'Review ready · nothing applied';state(heading);$('#bench-report').hidden=false;$('#bench-report').dataset.result=event.name==='desktop-blocked'?'error':'review';$('#bench-report').textContent=(desktopResult?heading+'\n':'')+event.text;if(event.proposal){$('#bench-file').textContent=event.name+' · proposed';$('#bench-code').textContent=event.proposal;ui.setChatDiff(chatId,diff({...event,text:event.proposal}));}yield '\n'+(desktopResult?heading+'\n':'')+event.text+'\n';}
        if(event.type==='success'||event.type==='error'){terminal=true;const text=(event.type==='success'?'Succeeded: ':'Failed: ')+event.text+(event.name?'\nRun log: '+event.name:'');$('#bench-report').hidden=false;$('#bench-report').dataset.result=event.type;$('#bench-report').textContent=text;state(text);yield '\n'+text+'\n';}
      }
      if(done)break;
    }
    if(!terminal)throw Error('Connection closed before a final result');
  }catch(error){const text=controller.signal.aborted?'Stopped. Previously saved files and command effects remain.':'Failed: '+error.message;state(text);$('#bench-report').hidden=false;$('#bench-report').dataset.result='error';$('#bench-report').textContent=text;yield '\n'+text+'\n';}
  finally{signal.removeEventListener('abort',abort);if(!terminal||controller.signal.aborted)await cancel();active=null;activeDesktop=false;skipPreview=false;browse.disabled=false;desktopButton.disabled=false;$('#bench-stop').disabled=true;$('#bench-root').disabled=false;$('#bench-execute').disabled=false;$('#bench-cursor').hidden=true;$('#bench-speed').disabled=false;skip.hidden=false;}
});
ui.setHostAdapter({capabilities:['diagnoseWorkspace'],async invoke(){try{await models.ready;models.open();}catch(e){state(e.message);}}});
models.ready.then(c=>{updateSetup();state(c.configured&&c.model?'Ready · send a computer or project task':'Choose a model to get started');}).catch(e=>state(e.message));
function brandWelcome(){const welcome=$('.welcome');if(!welcome||welcome.dataset.bench)return;welcome.dataset.bench='true';welcome.replaceChildren();const mark=document.createElement('span');mark.className='bench-mark';mark.textContent='b.';const tag=document.createElement('span');tag.className='bench-tag';tag.textContent='Your coding workbench';const heading=document.createElement('h1');heading.textContent='Build something that works.';const sub=document.createElement('p');sub.textContent='Pick a model and give it a task. Work on your computer or open a project. You approve each command before it runs.';const examples=document.createElement('div');examples.className='bench-examples';for(const [title,desc,prompt] of [['Fix a bug','Find the cause. Verify the fix.','Find and fix a bug in this project. Ask me which behavior is failing, then run the relevant tests.'],['Add a feature','Turn an idea into working code.','Help me add a feature to this project. Ask me about the desired behavior and acceptance criteria first.'],['Improve the code','Refactor with tests to back it up.','Refactor a small part of this project for clarity while preserving its behavior. Ask me which area to focus on.']]){const b=document.createElement('button'),h=document.createElement('strong'),d=document.createElement('small');h.textContent=title;d.textContent=desc;b.append(h,d);b.onclick=()=>{$('#prompt').value=prompt;$('#prompt').dispatchEvent(new Event('input',{bubbles:true}));$('#prompt').focus();};examples.append(b);}welcome.append(mark,tag,heading,sub,examples);}
new MutationObserver(brandWelcome).observe($('#messages'),{childList:true});brandWelcome();

const firstRun=createFirstRun({openProject:()=>projectButton.click(),openModels:()=>models.open(),importChats:()=>ui.importChats(),notify:text=>ui.notify('question',text)});firstRun.autoStart();
