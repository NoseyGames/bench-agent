export function askAction(event,signal){
  return new Promise(resolve=>{
    const d=document.createElement('dialog');d.className='bench-approval';
    const title=document.createElement('h2');title.textContent='Permission required · '+event.risk.severity;
    const reason=document.createElement('p');reason.textContent=event.risk.reason;
    const context=document.createElement('p');context.textContent='Working folder: '+event.workingDirectory+' · Runs with your account permissions.';
    const label=document.createElement('strong');label.textContent=event.type==='command'?'Exact command':'Write file: '+event.name;
    const code=document.createElement('pre');code.textContent=event.text;
    const note=document.createElement('p');note.textContent='The agent is paused. Yes allows only this action. No stops the agent and closes this chat. Prior completed changes remain.';
    const buttons=document.createElement('div');const no=document.createElement('button'),yes=document.createElement('button');no.textContent='No — close chat';yes.textContent='Yes — allow this action';
    let settled=false;const finish=value=>{if(settled)return;settled=true;signal.removeEventListener('abort',abort);d.close();d.remove();resolve(value);};const abort=()=>finish(null);
    no.onclick=()=>finish(false);yes.onclick=()=>finish(true);d.oncancel=e=>{e.preventDefault();finish(false);};buttons.append(no,yes);d.append(title,reason,context,label,code,note,buttons);document.body.append(d);
    if(signal.aborted){finish(null);return;}signal.addEventListener('abort',abort,{once:true});d.showModal();no.focus();
  });
}
