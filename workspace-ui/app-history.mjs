/* Local-only, read-only history adapters. Paths come from known app locations,
 * never from an HTTP caller. No credential stores or whole-drive search. */
import fs from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createInterface} from 'node:readline';
import path from 'node:path';
import os from 'node:os';

const FILE_LIMIT=64*1024*1024,RESPONSE_LIMIT=24*1024*1024;
const visible=content=>typeof content==='string'?content:Array.isArray(content)?content.filter(c=>typeof c==='string'||['text','input_text','output_text'].includes(c?.type)).map(c=>typeof c==='string'?c:c.text||'').join('\n'):'';
const message=(role,content)=>['user','assistant'].includes(role)&&visible(content).trim()?{role,content:visible(content)}:null;
const titleOf=(messages,fallback)=>messages.find(m=>m.role==='user')?.content.replace(/\s+/g,' ').slice(0,90)||fallback;
export async function parseTranscript(file,kind){
  const stat=await fs.stat(file);if(stat.size>FILE_LIMIT)throw Error('Transcript exceeds 64 MB');
  const stream=createReadStream(file,{encoding:'utf8',end:Math.max(0,stat.size-1)}),reader=createInterface({input:stream,crlfDelay:Infinity});
  const messages=[],fallback=[];let title='',malformed=0;
  try{for await(const line of reader){if(!line.trim())continue;if(line.length>4*1024*1024){malformed++;continue;}let x;try{x=JSON.parse(line);}catch{malformed++;continue;}
    let m;
    if(kind==='codex'){
      if(x.type==='session_meta')title=x.payload?.title||title;
      const p=x.payload;
      if(x.type==='response_item'&&p?.type==='message'&&(!p.channel||['final','commentary'].includes(p.channel)))m=message(p.role,p.content);
      // Older stores may persist visible event messages without response items.
      if(x.type==='event_msg'&&p?.type==='user_message'){const f=message('user',p.message);if(f)fallback.push(f);}
      if(x.type==='event_msg'&&p?.type==='agent_message'){const f=message('assistant',p.message);if(f)fallback.push(f);}
    }else if(kind==='claude'){
      if(x.isSidechain)continue;
      if(x.type==='summary'&&typeof x.summary==='string')title=x.summary;
      if(['user','assistant'].includes(x.type))m=message(x.message?.role||x.type,x.message?.content);
    }
    if(m)messages.push(m);
    if(messages.length>20000)throw Error('Transcript exceeds 20,000 messages');
  }}finally{reader.close();stream.destroy();}
  const result=messages.length?messages:fallback;
  return {chat:{title:title||titleOf(result,path.basename(file)),messages:result},malformed};
}
async function boundedJson(file){const stat=await fs.stat(file);if(stat.size>FILE_LIMIT)throw Error('History file exceeds 64 MB');return JSON.parse(await fs.readFile(file,'utf8'));}
async function* walk(root,extension,depth=0,state={visited:0,limited:false}){
  if(depth>6||state.visited>=12000){state.limited=true;return;}
  let entries;try{const stat=await fs.lstat(root);if(stat.isSymbolicLink()||!stat.isDirectory())return;entries=await fs.readdir(root,{withFileTypes:true});}catch{return;}
  for(const e of entries){if(++state.visited>12000){state.limited=true;break;}if(e.isSymbolicLink())continue;const file=path.join(root,e.name);
    if(e.isDirectory())yield*walk(file,extension,depth+1,state);
    else if(e.isFile()&&extension(e.name)){try{const s=await fs.stat(file);yield{file,mtime:s.mtimeMs};}catch{}}
  }
}
async function exists(p){try{return(await fs.lstat(p)).isDirectory();}catch{return false;}}

async function cursorDatabase(file){
  const {DatabaseSync}=await import('node:sqlite');const db=new DatabaseSync(file,{readOnly:true});const chats=[];
  try{
    db.exec('PRAGMA query_only=ON; PRAGMA busy_timeout=1000;');
    const tables=new Set(db.prepare("SELECT name FROM sqlite_master WHERE type='table'").all().map(x=>x.name));
    if(tables.has('cursorDiskKV')){
      const rows=db.prepare("SELECT key,value FROM cursorDiskKV WHERE key LIKE 'composerData:%' AND length(value)<4194304 ORDER BY rowid DESC LIMIT 2000").all();
      const bubble=db.prepare('SELECT value FROM cursorDiskKV WHERE key=? AND length(value)<4194304');
      for(const row of rows){try{const c=JSON.parse(String(row.value)),id=c.composerId||row.key.slice('composerData:'.length);let conversation=c.conversation;
        if(!Array.isArray(conversation)&&Array.isArray(c.fullConversationHeadersOnly))conversation=c.fullConversationHeadersOnly.slice(0,20000).map(h=>{const v=bubble.get(`bubbleId:${id}:${h.bubbleId}`);try{return v?JSON.parse(String(v.value)):null;}catch{return null;}}).filter(Boolean);
        const messages=(conversation||[]).map(b=>message(b.type===1?'user':b.type===2?'assistant':b.role,b.text??b.content)).filter(Boolean);
        if(messages.length)chats.push({title:c.name||titleOf(messages,'Cursor chat'),messages,updated:c.lastUpdatedAt||c.createdAt});
      }catch{}}
    }
    if(tables.has('ItemTable')){
      const row=db.prepare('SELECT value FROM ItemTable WHERE key=? AND length(value)<33554432').get('workbench.panel.aichat.view.aichat.chatdata');
      if(row){const data=JSON.parse(String(row.value));for(const tab of data.tabs||[]){const messages=(tab.bubbles||[]).map(b=>message(b.type==='user'?'user':b.type==='ai'?'assistant':b.role,b.text??b.content)).filter(Boolean);if(messages.length)chats.push({title:tab.title||titleOf(messages,'Cursor chat'),messages});}}
    }
    return chats;
  }finally{db.close();}
}

export function createHistoryScanner({home=os.homedir(),appData=process.env.APPDATA||path.join(home,process.platform==='darwin'?'Library/Application Support':'.config'),codexHome=process.env.CODEX_HOME||path.join(home,'.codex'),claudeHome=process.env.CLAUDE_CONFIG_DIR||path.join(home,'.claude')}={}){
  const definitions=[
    {id:'codex',name:'Codex',roots:[path.join(codexHome,'sessions'),path.join(codexHome,'archived_sessions')],match:n=>n.endsWith('.jsonl'),kind:'codex'},
    {id:'claude-code',name:'Claude Code',roots:[path.join(claudeHome,'projects')],match:n=>n.endsWith('.jsonl'),kind:'claude'},
    {id:'cursor',name:'Cursor',roots:[path.join(appData,'Cursor','User','globalStorage'),path.join(appData,'Cursor','User','workspaceStorage')],match:n=>n==='state.vscdb',kind:'cursor'},
    {id:'continue',name:'Continue',roots:[path.join(home,'.continue','sessions')],match:n=>n.endsWith('.json')&&!['sessions.json','index.json'].includes(n),kind:'continue'},
    {id:'lmstudio',name:'LM Studio',roots:[path.join(home,'.lmstudio','conversations'),path.join(appData,'LM Studio')],unsupported:'App data found. This conversation format needs a JSON export; direct import is not supported yet.'},
    {id:'claude-desktop',name:'Claude Desktop',roots:[path.join(appData,'Claude')],unsupported:'App data found. No supported local transcript format. Use an exported conversation JSON.'},
    {id:'chatgpt',name:'ChatGPT',roots:[path.join(appData,'ChatGPT'),path.join(appData,'OpenAI','ChatGPT')],unsupported:'App data found. Use ChatGPT’s conversations.json export; this importer does not read login/session caches.'}
  ];
  async function filesFor(app){const files=[],seen=new Set(),state={visited:0,limited:false};for(const root of app.roots)for await(const f of walk(root,app.match,0,state)){const real=await fs.realpath(f.file).catch(()=>null);if(real&&!seen.has(real)){seen.add(real);files.push({...f,file:real,root});}}return{files:files.sort((a,b)=>b.mtime-a.mtime),limited:state.limited};}
  async function scan(){const apps=[];for(const a of definitions){if(a.unsupported){if((await Promise.all(a.roots.map(exists))).some(Boolean))apps.push({id:a.id,name:a.name,available:false,count:0,note:a.unsupported});continue;}const {files,limited}=await filesFor(a);if(files.length)apps.push({id:a.id,name:a.name,available:true,count:files.length,unit:a.kind==='cursor'?'chat databases':'session files',limited,note:limited?'Scan limit reached; newest discovered files are listed.':'Local history found'});}return{apps,supported:definitions.filter(a=>!a.unsupported).map(a=>a.name)};}
  async function importApp(id,limit=10){const app=definitions.find(a=>a.id===id);if(!app||app.unsupported)throw Error('This app does not support direct local import.');if(![10,50,2000].includes(limit))throw Error('Choose 10, 50, or all recent chats.');const {files,limited}=await filesFor(app);const chats=[],warnings=[];let bytes=0,skipped=0,remaining=false;
    for(const f of files){if(chats.length>=limit){remaining=true;break;}try{
      // Recheck containment and reject links if the source changed after discovery.
      const realRoot=await fs.realpath(f.root),real=await fs.realpath(f.file),relative=path.relative(realRoot,real);
      if(relative.startsWith('..')||path.isAbsolute(relative)||(await fs.lstat(f.file)).isSymbolicLink())throw Error('Source moved outside its app directory');
      let candidates;
      if(app.kind==='cursor')candidates=await cursorDatabase(real);
      else if(app.kind==='continue'){const data=await boundedJson(real);const messages=(data.history||[]).map(h=>message(h.message?.role,h.message?.content)).filter(Boolean);candidates=[{title:data.title||titleOf(messages,'Continue chat'),messages}];}
      else {const parsed=await parseTranscript(real,app.kind);candidates=[parsed.chat];if(parsed.malformed)warnings.push('An incomplete or invalid transcript record was skipped.');}
      for(const c of candidates){if(!c.messages.length){skipped++;continue;}if(chats.length>=limit){remaining=true;break;}const size=Buffer.byteLength(JSON.stringify(c));if(bytes+size>RESPONSE_LIMIT){skipped++;remaining=true;warnings.push('The 24 MB import limit was reached. Choose a smaller batch.');continue;}bytes+=size;chats.push(c);}
    }catch(e){skipped++;warnings.push(e.code==='EBUSY'||e.code==='SQLITE_BUSY'?'An app database is busy. Close the source app and try again.':'A history file could not be read or uses an unsupported format.');}}
    return{app:app.name,chats,skipped,remaining,limited,warnings:[...new Set(warnings)],foundFiles:files.length};
  }
  return{scan,importApp};
}
