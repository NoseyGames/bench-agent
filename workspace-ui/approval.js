export function askAction(event,signal){
  return new Promise(resolve=>{
    const d=document.createElement('dialog');d.className='bench-approval';
    const title=document.createElement('h2');title.textContent='Permission required · '+event.risk.severity;
    const reason=document.createElement('p');reason.textContent=event.risk.reason;
    const context=document.createElement('p');context.textContent='Working folder: '+event.workingDirectory+' · Runs with your account permissions.';
    const label=document.createElement('strong');label.textContent=event.type==='desktop'?'Desktop action: '+event.name:event.type==='command'?'Exact command':'Write file: '+event.name;
    const code=document.createElement('pre');code.textContent=event.text;
    const actionSummary=document.createElement('dl');actionSummary.className='bench-action-summary';
    let action=null;let exact=code;let desktopHelp=null;
    const note=document.createElement('p');note.textContent='The agent is paused. Yes allows only this action. No stops the agent and closes this chat. Prior completed changes remain.';
    const buttons=document.createElement('div');const no=document.createElement('button'),yes=document.createElement('button');no.textContent='No — close chat';yes.textContent='Yes — allow this action';
    if(event.type==='desktop'){
      context.textContent='Observation destination: '+event.destination;
      desktopHelp=document.createElement('details');const helpTitle=document.createElement('summary'),helpText=document.createElement('p');helpTitle.textContent='Observation and stop controls';helpText.textContent='After Yes, focus the selected app within 5 seconds if Windows cannot bring it forward. Resume releases a paused action. Ctrl+Alt+Shift+F10 is emergency stop. Each input rechecks the app and returns compact control text. Screenshots need a separate Yes. If app state changes unexpectedly, the remaining actions are discarded and the agent asks again.';desktopHelp.append(helpTitle,helpText);
      try{const parsed=JSON.parse(event.text);if(parsed&&typeof parsed==='object'&&!Array.isArray(parsed))action=parsed;}catch{}
      if(action){
        const row=(name,value)=>{if(typeof value!=='string'||!value)return;const term=document.createElement('dt'),description=document.createElement('dd');term.textContent=name;description.textContent=value;actionSummary.append(term,description);};
        row('Action',action.action||event.name);row('Why',action.reason);row('Observed',action.observation);row('Expected result',action.expected);
        if(typeof action.evidence==='string')row('Model check'+(typeof action.verification==='string'?' · '+action.verification:''),action.evidence);
        if(['click','move','scroll'].includes(action.action)&&Number.isInteger(action.x)&&Number.isInteger(action.y))row('Window position',action.x+', '+action.y);
        if(action.action==='scroll'&&Number.isInteger(action.delta))row('Scroll',String(action.delta));
        row(action.action==='key'?'Key or shortcut':'Exact text to type',action.text);
        const details=document.createElement('details'),summary=document.createElement('summary');summary.textContent='Exact action JSON';details.append(summary,code);exact=details;
      }
    }
    let settled=false;const finish=value=>{if(settled)return;settled=true;signal.removeEventListener('abort',abort);d.close();d.remove();resolve(value);};const abort=()=>finish(null);
    no.onclick=()=>finish(false);yes.onclick=()=>finish(true);d.oncancel=e=>{e.preventDefault();finish(false);};buttons.append(no,yes);d.append(title,reason,context);
    if(event.type==='desktop'&&event.before?.startsWith('data:image/png;base64,')){const frame=document.createElement('details'),summary=document.createElement('summary'),img=document.createElement('img');summary.textContent='View screenshot used for this action';img.src=event.before;img.alt='Latest selected-app screenshot used to plan this action';img.className='bench-approval-image';frame.append(summary,img);d.append(frame);}
    d.append(label);if(action)d.append(actionSummary);d.append(exact);if(desktopHelp)d.append(desktopHelp);d.append(note,buttons);document.body.append(d);
    if(signal.aborted){finish(null);return;}signal.addEventListener('abort',abort,{once:true});d.showModal();no.focus();
  });
}
