import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import {DatabaseSync} from 'node:sqlite';
import {createHistoryScanner} from '../app-history.mjs';
import {createHost} from '../desktop-host.mjs';

async function fixture(t){
 const home=await fs.mkdtemp(path.join(os.tmpdir(),'bench-history-'));t.after(()=>fs.rm(home,{recursive:true,force:true}));
 const appData=path.join(home,'apps'),codexHome=path.join(home,'.codex'),claudeHome=path.join(home,'.claude');
 const write=async(file,data)=>{const full=path.join(home,file);await fs.mkdir(path.dirname(full),{recursive:true});await fs.writeFile(full,data);return full;};
 return {home,write,scanner:createHistoryScanner({home,appData,codexHome,claudeHome})};
}
test('Codex imports visible messages, ignores instructions and analysis, tolerates partial tail',async t=>{
 const f=await fixture(t);const records=['developer','user','assistant','assistant'].map((role,i)=>({type:'response_item',payload:{type:'message',role,channel:i===2?'analysis':'final',content:[{type:'input_text',text:'message '+i}]}}));
 const file=await f.write('.codex/sessions/test.jsonl',records.map(JSON.stringify).join('\n')+'\n{"partial":');const before=await fs.readFile(file);
 assert.equal((await f.scanner.scan()).apps[0].id,'codex');const result=await f.scanner.importApp('codex');assert.deepEqual(result.chats[0].messages.map(m=>m.content),['message 1','message 3']);assert.equal(result.warnings.length,1);assert.deepEqual(await fs.readFile(file),before);
 await assert.rejects(f.scanner.importApp('../../private'));await assert.rejects(f.scanner.importApp('codex',100000));
});
test('Claude and Continue readers exclude sidechains and tool/thinking blocks',async t=>{
 const f=await fixture(t);await f.write('.claude/projects/a/session.jsonl',[
 {type:'user',message:{role:'user',content:'Build app'}},{type:'assistant',message:{content:[{type:'thinking',thinking:'private'},{type:'text',text:'Done'}]}},{type:'user',isSidechain:true,message:{content:'subtask'}}].map(JSON.stringify).join('\n'));
 await f.write('.continue/sessions/chat.json',JSON.stringify({title:'Example',history:[{message:{role:'user',content:'Hello'}},{message:{role:'tool',content:'hidden'}}]}));
 assert.equal((await f.scanner.importApp('claude-code')).chats[0].messages.length,2);assert.equal((await f.scanner.importApp('continue')).chats[0].messages.length,1);
});
test('Cursor reads recognized composer database without modifying it',async t=>{
 const f=await fixture(t);const file=await f.write('apps/Cursor/User/globalStorage/state.vscdb','');const db=new DatabaseSync(file);db.exec('CREATE TABLE cursorDiskKV (key TEXT PRIMARY KEY,value TEXT)');const put=db.prepare('INSERT INTO cursorDiskKV VALUES (?,?)');put.run('composerData:one',JSON.stringify({name:'Example',composerId:'one',fullConversationHeadersOnly:[{bubbleId:'a'},{bubbleId:'b'}]}));put.run('bubbleId:one:a',JSON.stringify({type:1,text:'Question'}));put.run('bubbleId:one:b',JSON.stringify({type:2,text:'Answer'}));db.close();const before=await fs.readFile(file);const result=await f.scanner.importApp('cursor');assert.equal(result.chats[0].messages.length,2);assert.deepEqual(await fs.readFile(file),before);
});
test('authenticated routes discover and import by app ID, never caller paths',async t=>{
 const f=await fixture(t);await f.write('.continue/sessions/chat.json',JSON.stringify({history:[{message:{role:'user',content:'Fixture'}}]}));const host=createHost({historyScanner:f.scanner});host.server.listen(0,'127.0.0.1');await once(host.server,'listening');t.after(()=>host.close());const base='http://127.0.0.1:'+host.server.address().port;
 const post=(route,data,auth=true)=>fetch(base+'/api/'+route,{method:'POST',headers:{'Content-Type':'application/json',...(auth?{Authorization:'Bearer '+host.token}:{})},body:JSON.stringify(data)});
 assert.equal((await post('import-apps',{},false)).status,401);assert.equal((await(await post('import-apps',{})).json()).apps[0].id,'continue');assert.equal((await(await post('import-app',{app:'continue'})).json()).chats.length,1);assert.notEqual((await post('import-app',{app:f.home})).status,200);assert.equal((await fetch(base+'/app-history.mjs')).status,404);
});
