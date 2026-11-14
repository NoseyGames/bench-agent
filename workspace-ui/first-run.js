export const FIRST_RUN_KEY='bench-setup-complete-v1';
export function needsSetup(storage){try{return storage.getItem(FIRST_RUN_KEY)!=='done';}catch{return true;}}
export function completeSetup(storage){try{storage.setItem(FIRST_RUN_KEY,'done');return true;}catch{return false;}}

// Setup never invents progress, changes permissions, or auto-runs a task.
export function createFirstRun({openProject,openModels,importChats,storage=localStorage,notify}){
  const dialog=document.createElement('dialog');dialog.id='bench-first-run';dialog.setAttribute('aria-labelledby','setup-heading');
  dialog.innerHTML='<div class="setup-top"><span class="bench-wordmark"><img src="./brand.svg" alt="">Bench</span><button id="setup-skip">Skip setup</button></div><div id="setup-scene" aria-hidden="true"><span class="setup-orbit"></span><span class="setup-dot"></span><img src="./brand.svg" alt=""></div><div id="setup-copy" aria-live="polite"></div><div class="setup-bottom"><div id="setup-progress" aria-label="Setup progress"></div><button id="setup-back">Back</button><button id="setup-next" class="primary">Continue</button></div>';
  document.body.append(dialog);const $=id=>dialog.querySelector('#'+id);let index=0,returnFocus=null;
  const steps=[
    {title:'Your work, in one place.',body:'Start a conversation, or bring your chats from another app. Bench keeps the task in view and opens the workspace when there is work to review.',action:'Import existing chats',run:importChats},
    {title:'Choose where to work.',body:'Open a project for file changes, builds, and tests. No project? Use computer commands, or choose Control an app to work in a specific desktop window.',action:'Choose project folder',run:openProject},
    {title:'Choose your model.',body:'Connect Ollama or LM Studio on your computer, or choose OpenAI. Local providers stay local: Bench never switches them to a cloud provider. Change the model from the composer.',action:'Choose a model',run:openModels},
    {title:'Every action stays yours.',body:'Review the exact action before it runs. Desktop observations, screenshots, and each input need their own Yes. No stops the agent and closes the chat; earlier completed changes remain.',action:null}
  ];
  function render(){const step=steps[index];dialog.dataset.step=String(index);const copy=$('setup-copy');copy.replaceChildren();const eyebrow=document.createElement('span');eyebrow.className='setup-eyebrow';eyebrow.textContent=`GET STARTED · ${index+1} OF ${steps.length}`;const h=document.createElement('h1');h.id='setup-heading';h.textContent=step.title;const p=document.createElement('p');p.textContent=step.body;copy.append(eyebrow,h,p);if(step.action){const action=document.createElement('button');action.className='setup-action';action.textContent=step.action;action.onclick=async()=>{try{await step.run();}catch(e){notify(e.message);}};copy.append(action);}const progress=$('setup-progress');progress.replaceChildren();steps.forEach((_,i)=>{const dot=document.createElement('span');dot.className=i===index?'active':'';progress.append(dot);});progress.setAttribute('aria-label',`Step ${index+1} of ${steps.length}`);$('setup-back').disabled=index===0;$('setup-next').textContent=index===steps.length-1?'Open Bench':'Continue';}
  function finish(){if(!completeSetup(storage))notify('Setup could not be remembered because browser storage is unavailable.');dialog.close();returnFocus?.focus();}
  $('setup-skip').onclick=finish;$('setup-next').onclick=()=>{if(index===steps.length-1)finish();else{index++;render();}};$('setup-back').onclick=()=>{index=Math.max(0,index-1);render();};dialog.addEventListener('cancel',e=>{e.preventDefault();finish();});
  const open=()=>{if(dialog.open)return;returnFocus=document.activeElement;index=0;render();dialog.showModal();$('setup-next').focus();};
  window.addEventListener('bench-replay-setup',open);
  return {open,autoStart(){if(needsSetup(storage)){if(!completeSetup(storage))notify('Setup cannot be remembered while browser storage is unavailable.');open();}}};
}
