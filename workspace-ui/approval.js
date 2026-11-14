export function askAction(event,signal){
  return new Promise(resolve=>{
    const d=document.createElement('dialog');d.className='bench-approval';d.setAttribute('aria-labelledby','bench-approval-title');d.setAttribute('aria-describedby','bench-approval-risk');
    const kind=event.type==='desktop'?(['observe','screenshot'].includes(event.name)?event.name:'input'):event.type==='command'?'command':'file';
    d.dataset.kind=kind;d.dataset.severity=event.risk.severity;
    const kicker=document.createElement('div');kicker.className='approval-kicker';
    const category=document.createElement('span'),severity=document.createElement('strong');category.textContent={observe:'Read app text',screenshot:'Share screen image',input:'Real desktop input',command:'Run shell command',file:'Change a file'}[kind];severity.className='approval-severity';severity.textContent=event.risk.severity+' risk';kicker.append(category,severity);
    const title=document.createElement('h2');title.id='bench-approval-title';title.textContent=kind==='observe'?'Allow this app to be observed?':kind==='screenshot'?'Allow this screenshot to be shared?':kind==='input'?({move:'Move the mouse?',click:'Click in the selected app?',type:'Type into the focused control?',key:'Send this key or shortcut?',scroll:'Scroll in the selected app?'}[event.name]||'Allow desktop input?'):kind==='command'?'Run this command?':(event.before?'Replace ':'Create ')+event.name+'?';
    const reason=document.createElement('p');reason.id='bench-approval-risk';reason.className='approval-risk';reason.textContent=event.risk.reason;
    const context=document.createElement('dl');context.className='approval-context';
    const contextRow=(key,value)=>{const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=key;dd.textContent=value||'Not available';context.append(dt,dd);};
    if(event.type==='desktop'){contextRow('Selected app',event.appTitle||'Selected desktop window');contextRow('Model destination',event.destination);}else{contextRow('Working folder',event.workingDirectory);contextRow('Access','Your account permissions');}
    const label=document.createElement('strong');label.textContent=event.type==='desktop'?'Desktop action: '+event.name:event.type==='command'?'Exact command':'Write file: '+event.name;
    const code=document.createElement('pre');code.textContent=event.text;
    const actionSummary=document.createElement('dl');actionSummary.className='bench-action-summary';
    let action=null;let exact=code;let desktopHelp=null;
    const note=document.createElement('p');note.className='approval-note';note.textContent='Paused for your decision. Yes allows only this action. No stops the agent and closes this chat. Prior completed changes remain.';
    const buttons=document.createElement('div');buttons.className='approval-buttons';const no=document.createElement('button'),yes=document.createElement('button');no.textContent='No — close chat';yes.textContent='Yes — allow this action';yes.className='approval-allow';
    if(event.type==='desktop'){
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
    no.onclick=()=>finish(false);yes.onclick=()=>finish(true);d.oncancel=e=>{e.preventDefault();finish(false);};buttons.append(no,yes);d.append(kicker,title,reason,context);
    if(event.type==='desktop'&&event.before?.startsWith('data:image/png;base64,')){const frame=document.createElement('details'),summary=document.createElement('summary'),img=document.createElement('img');summary.textContent='View screenshot used for this action';img.src=event.before;img.alt='Latest selected-app screenshot used to plan this action';img.className='bench-approval-image';frame.append(summary,img);d.append(frame);}
    d.append(label);if(action)d.append(actionSummary);d.append(exact);if(desktopHelp)d.append(desktopHelp);d.append(note,buttons);document.body.append(d);
    if(signal.aborted){finish(null);return;}signal.addEventListener('abort',abort,{once:true});d.showModal();no.focus();
  });
}
