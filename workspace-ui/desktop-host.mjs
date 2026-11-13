// Loopback-only desktop transport. C owns API calls, file validation and commands.
// A random per-launch bearer token and strict Host/Origin checks protect writes.
import http from 'node:http';
import os from 'node:os';
import {assessAction} from './action-policy.mjs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes} from 'node:crypto';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createInterface} from 'node:readline';
import {createHistoryScanner} from './app-history.mjs';
import {profile,discover} from './providers.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const assets=new Set(['index.html','app.js','state.js','style.css','workspace.css','settings.js','preferences.js','appearance.js','desktop.js','desktop.css','ai.cur','connection.js','providers.mjs','brand.css','brand.svg','chat-import.js','first-run.js','calm.css','app-import.js','approval.js']);
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.cur':'image/x-icon','.mjs':'text/javascript','.svg':'image/svg+xml'};
export function createHost({engine=path.join(here,'..',process.platform==='win32'?'bench-engine.exe':'bench-engine'),engineArgs=[],historyScanner=createHistoryScanner(),computerRoot=path.join(process.env.LOCALAPPDATA||os.homedir(),'Bench','computer-workspace'),token=randomBytes(32).toString('hex')}={}) {
  const runs=new Map(),questions=new Map(),closedChats=new Set(); let config=profile(),importing=false;
  const credentials=new Map([['openai|https://api.openai.com/v1',process.env.OPENAI_API_KEY||'']]);
  const keyFor=c=>credentials.get(c.provider+'|'+c.baseUrl)||'';
  async function body(req) {
    let text=''; for await(const chunk of req) {text+=chunk;if(Buffer.byteLength(text)>2*1024*1024)throw Error('Request exceeds 2 MiB');}
    return JSON.parse(text);
  }
  function cancel(id) {
    const run=runs.get(id);if(!run)return;
    run.cancelled=true;clearTimeout(run.timer);
    if(run.child){run.child.stdin.end();run.child.kill();}else runs.delete(id);
    run.res.end();
    return run.closed;
  }
  const server=http.createServer(async(req,res)=>{
    const origin=`http://127.0.0.1:${server.address().port}`;
    res.setHeader('Cache-Control','no-store');res.setHeader('X-Content-Type-Options','nosniff');
    res.setHeader('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    const send=(code,value)=>{res.writeHead(code,{'Content-Type':'application/json'});res.end(JSON.stringify(value));};
    let reservation;
    try {
      if(req.headers.host!==new URL(origin).host || (req.headers.origin&&req.headers.origin!==origin))return send(403,{error:'Invalid origin'});
      const route=new URL(req.url,origin).pathname;
      if(!route.startsWith('/api/')) {
        if(req.method!=='GET')return send(405,{error:'GET required'});
        const name=route.slice(1)||'index.html';if(!assets.has(name))return send(404,{error:'Not found'});
        const data=await fs.readFile(path.join(here,name));res.writeHead(200,{'Content-Type':mime[path.extname(name)]||'application/octet-stream'});res.end(data);return;
      }
      if(req.headers.authorization!==`Bearer ${token}`)return send(401,{error:'Launch this window from Launch-workspace.cmd'});
      if(req.method!=='POST'||!req.headers['content-type']?.startsWith('application/json'))return send(405,{error:'JSON POST required'});
      const data=await body(req);
      if(route==='/api/desktop-windows'){
        if(runs.size)return send(409,{error:'Stop the current task before choosing an app.'});
        const env={...process.env};delete env.OPENAI_API_KEY;
        const {stdout}=await promisify(execFile)(engine,['--desktop-windows'],{env,windowsHide:true,timeout:10000,maxBuffer:256*1024});
        return send(200,{windows:JSON.parse(stdout)});
      }
      if(route==='/api/import-apps')return send(200,await historyScanner.scan());
      if(route==='/api/import-app'){
        if(runs.size||importing)return send(409,{error:'Wait for the active task or import to finish.'});
        importing=true;
        try{return send(200,await historyScanner.importApp(data.app,data.limit??10));}finally{importing=false;}
      }
      if(route==='/api/config') {
        if(Object.keys(data).length){
          if(runs.size)return send(409,{error:'Stop the active task before changing models.'});
          const next=profile({...config,...data});
          if(typeof data.key==='string'&&data.key.trim())credentials.set(next.provider+'|'+next.baseUrl,data.key.trim());
          if(data.clearKey)credentials.delete(next.provider+'|'+next.baseUrl);
          if(!data.clearKey)config=next;
        }
        return send(200,{...config,configured:config.provider!=='openai'||!!keyFor(config),hasKey:!!keyFor(config)});
      }
      if(route==='/api/models') {
        const next=profile({...config,...data});
        const key=typeof data.key==='string'&&data.key.trim()?data.key.trim():keyFor(next);
        return send(200,{models:await discover(next,key)});
      }
      if(route==='/api/shutdown') {send(200,{ok:true});setImmediate(()=>{for(const id of runs.keys())cancel(id);server.close();});return;}
      if(route==='/api/folder') {
        if(process.platform!=='win32')return send(400,{error:'Paste the absolute project folder path in the toolbar.'});
        const picker=spawn('powershell.exe',['-NoProfile','-STA','-Command',
          'Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.FolderBrowserDialog; $d.Description = "Choose an existing project"; $d.ShowNewFolderButton = $false; if ($d.ShowDialog() -eq "OK") { [Console]::OutputEncoding = [Text.Encoding]::UTF8; [Console]::Write($d.SelectedPath) }; $d.Dispose()'],{windowsHide:true,stdio:['ignore','pipe','ignore']});
        let selected='';const timer=setTimeout(()=>picker.kill(),120000);
        picker.stdout.on('data',chunk=>{selected+=chunk;if(selected.length>32768)picker.kill();});
        picker.on('error',()=>{clearTimeout(timer);if(!res.writableEnded)send(400,{error:'Folder picker unavailable; paste the folder path instead.'});});
        picker.on('close',()=>{clearTimeout(timer);if(!res.writableEnded)send(200,{path:selected.replace(/^\uFEFF/,'')});});
        res.on('close',()=>{clearTimeout(timer);picker.kill();});return;
      }
      if(route==='/api/cancel') {await cancel(data.id);return send(200,{ok:true});}
      if(route==='/api/decision') {
        const run=runs.get(data.id);
        if(!run?.approval||run.cancelled||run.waiting!==data.sequence||run.approval.token!==data.token||typeof data.allow!=='boolean')return send(409,{error:'This permission request is no longer pending.'});
        if(!data.allow){closedChats.add(run.chatId);questions.delete(run.chatId);await cancel(data.id);return send(200,{closed:true});}
        run.approval=null;return send(200,{approved:true});
      }
      if(route==='/api/ack') {
        const run=runs.get(data.id);
        if(!run?.child||run.cancelled||run.waiting===null||run.waiting!==data.sequence)return send(409,{error:'This action is no longer pending'});
        if(run.approval)return send(403,{error:'Explicit permission is required before this action.'});
        run.waiting=null;run.child.stdin.write('ack\n');return send(200,{ok:true});
      }
      if(route!=='/api/run')return send(404,{error:'Not found'});
      if(config.provider==='openai'&&!keyFor(config))return send(400,{error:'Open Models and connect OpenAI, or choose a local model.'});
      if(runs.size)return send(409,{error:'Another run is active. Stop it before starting another.'});
      if(typeof data.id!=='string'||typeof data.root!=='string'||typeof data.model!=='string'||!data.model.trim()||!Array.isArray(data.messages))throw Error('Invalid run request');
      if(typeof data.chatId!=='string'||!data.chatId)throw Error('A chat identifier is required');
      const desktopWindow=data.desktopWindow||'';
      if(typeof desktopWindow!=='string'||(desktopWindow&&!/^[1-9][0-9]{0,19}:[1-9][0-9]{0,9}$/.test(desktopWindow)))throw Error('Select a valid desktop app.');
      if(closedChats.has(data.chatId))return send(403,{error:'This chat was closed after permission was denied. Start a new chat.'});
      // Reserve synchronously, before filesystem validation yields to another request.
      reservation={id:data.id,chatId:data.chatId,approval:null,child:null,res,waiting:null,sequence:0,timer:null,cancelled:false};
      runs.set(data.id,reservation);
      res.on('close',()=>{if(runs.get(data.id)===reservation)cancel(data.id);});
      const computerMode=!!desktopWindow||!data.root.trim();
      if(computerMode)await fs.mkdir(computerRoot,{recursive:true});
      const root=await fs.realpath(computerMode?computerRoot:data.root);if(!(await fs.stat(root)).isDirectory())throw Error('Project must be a directory');
      if(reservation.cancelled||res.destroyed){cancel(data.id);return;}
      const last=data.messages.filter(m=>m.role==='user').at(-1);if(!last||typeof last.text!=='string')throw Error('A task is required');
      const pending=questions.get(data.chatId),answer=pending?.root===root&&pending.desktopWindow===desktopWindow;
      const attachmentText=(last.files||[]).map(f=>`\nAttached reference ${f.name}:\n${f.content}`).join('');
      const task=answer?pending.task:last.text+attachmentText;
      if(data.instructions!==undefined&&(typeof data.instructions!=='string'||data.instructions.length>128000))throw Error('Invalid instructions');
      const request={key:keyFor(config),endpoint:config.baseUrl+'/chat/completions',jsonMode:config.jsonMode,root,computerMode,desktopWindow,model:data.model.trim(),task:task+(data.instructions?`\nUser preferences and instructions:\n${data.instructions}`:''),phase:answer?'execute':'questions',answers:answer?last.text+attachmentText:'',questions:answer?pending.text:''};
      if(answer)questions.delete(data.chatId);
      const env={...process.env};delete env.OPENAI_API_KEY;
      const child=spawn(engine,engineArgs,{cwd:path.dirname(engine),env,windowsHide:true,stdio:['pipe','pipe','pipe']});
      res.writeHead(200,{'Content-Type':'application/x-ndjson'});
      const run=reservation;run.child=child;
      run.closed=new Promise(resolve=>child.once('close',resolve));
      const emit=event=>!run.cancelled&&!res.destroyed&&!res.writableEnded&&res.write(JSON.stringify(event)+'\n');
      let ended=false,terminal=false;
      const fail=message=>{if(!ended){emit({type:'error',text:message});terminal=true;}};
      child.on('error',error=>{fail(`Cannot start C engine: ${error.message}`);res.end();});
      child.stdin.on('error',()=>{});
      let stderr='';child.stderr.on('data',chunk=>{stderr=(stderr+chunk).slice(-4000);});
      const lines=createInterface({input:child.stdout});
      lines.on('line',line=>{
        if(run.cancelled)return;
        try {
          const event=JSON.parse(line);
          if(event.type==='questions'){questions.set(data.chatId,{root,task,text:event.text,desktopWindow});terminal=true;}
          if(['error','success','review'].includes(event.type))terminal=true;
          if(['preview','command','desktop'].includes(event.type)) {
            // Read-only is enforced before either writes or arbitrary shell commands.
            if(!data.execute||data.readOnly){terminal=true;emit({type:'review',text:'Plan ready. No changes were applied. Enable edits and commands, then submit your task again to execute it.',name:event.name,before:event.before,proposal:event.type==='preview'?event.text:''});cancel(data.id);return;}
            event.sequence=++run.sequence;run.waiting=event.sequence;
            event.risk=assessAction(event);event.workingDirectory=root;
            if(event.type==='desktop')event.destination=config.baseUrl+' · '+data.model;
            if(event.risk.requiresApproval){run.approval={token:randomBytes(24).toString('hex')};event.approvalToken=run.approval.token;}
          }
          if(!emit(event))child.stdout.pause();
        } catch {fail('Invalid event from C engine');cancel(data.id);}
      });
      res.on('drain',()=>child.stdout.resume());
      child.on('close',code=>{if(!ended){if(!terminal)fail(`C engine stopped (exit ${code}). ${stderr}`);res.end();}clearTimeout(run.timer);if(runs.get(data.id)===run)runs.delete(data.id);});
      res.on('close',()=>{ended=true;});
      run.timer=setTimeout(()=>{fail('Session time limit reached (30 minutes)');cancel(data.id);},30*60*1000);
      child.stdin.write(JSON.stringify(request)+'\n');
    } catch(error) {
      if(reservation&&runs.get(reservation.id)===reservation){
        if(reservation.child)cancel(reservation.id);else runs.delete(reservation.id);
      }
      if(!res.destroyed&&!res.writableEnded){if(!res.headersSent)send(400,{error:error.message});else res.end();}
    }
  });
  return {server,token,close(){for(const id of runs.keys())cancel(id);server.close();}};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const host=createHost();
  host.server.listen(Number(process.env.PORT||47291),'127.0.0.1',()=>{
    const url=`http://127.0.0.1:${host.server.address().port}/#${host.token}`;
    console.log('Bench desktop host is running on loopback. Close this process to stop it.');
    if(process.argv.includes('--open')) {
      if(process.platform==='win32') {
        const edge=path.join(process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)','Microsoft/Edge/Application/msedge.exe');
        const browser=spawn(edge,[`--app=${url}`,'--window-size=1440,960'],{stdio:'ignore',detached:true,windowsHide:false});
        browser.on('error',()=>{console.error('Cannot open Edge. Install Edge or run without --open.');process.exitCode=1;host.close();});browser.unref();
      } else {const browser=spawn(process.platform==='darwin'?'open':'xdg-open',[url],{stdio:'ignore'});browser.on('error',e=>console.error(e.message));}
    } else console.log(url);
  });
  host.server.on('error',error=>{console.error(error.message);process.exitCode=1;});
  for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>host.close());
}
