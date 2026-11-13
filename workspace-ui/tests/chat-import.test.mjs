import test from 'node:test';
import assert from 'node:assert/strict';
import {mergeChatImport} from '../chat-import.js';
import {freshState,newChat,submitMessage} from '../state.js';
import {needsSetup,completeSetup,FIRST_RUN_KEY} from '../first-run.js';

test('chat imports merge without overwriting drafts, preferences, or queues',()=>{
  const original=freshState(),chat=newChat(original);chat.draft='Unsent work';original.preferences.model='my-local-model';original.queue=[{chatId:chat.id,text:'Later',files:[]}];
  const backup=freshState(),incoming=newChat(backup);incoming.draft='hello';submitMessage(incoming);backup.queue=[{chatId:incoming.id,text:'Do not run imported jobs',files:[]}];
  const before=structuredClone(original),result=mergeChatImport(original,backup);
  assert.deepEqual(original,before);assert.equal(result.added,1);assert.equal(result.state.chats.length,2);assert.equal(result.state.chats[0].draft,'Unsent work');assert.equal(result.state.preferences.model,'my-local-model');assert.deepEqual(result.state.queue,before.queue);assert.notEqual(result.state.chats[1].id,incoming.id);
  const again=mergeChatImport(result.state,backup);assert.equal(again.added,0);assert.equal(again.duplicates,1);
});
test('ChatGPT tree follows selected branch, skips metadata, and preserves text safely',()=>{
  const exported=[{title:'A conversation',current_node:'b',mapping:{s:{parent:null,message:{author:{role:'system'},content:{parts:['Internal']}}},a:{parent:'s',message:{author:{role:'user'},content:{parts:['<script>alert(1)</script>']}}},b:{parent:'a',message:{author:{role:'assistant'},content:{parts:['Selected answer']}}},other:{parent:'a',message:{author:{role:'assistant'},content:{parts:['Other branch']}}}}}];
  const result=mergeChatImport(freshState(),exported);assert.equal(result.added,1);assert.equal(result.skipped,1);assert.deepEqual(result.state.chats[0].messages.map(m=>m.text),['<script>alert(1)</script>','Selected answer']);
});
test('imports reject malformed trees and unsupported exports without modifying state',()=>{
  const state=freshState();assert.throws(()=>mergeChatImport(state,{unrelated:'x'}));assert.throws(()=>mergeChatImport(state,[{current_node:'a',mapping:{a:{parent:'a'}}}]));assert.deepEqual(state.chats,[]);
});
test('simple message JSON imports and remaps project IDs for native backups',()=>{
  const base=freshState();const simple=mergeChatImport(base,{title:'Simple',messages:[{role:'user',content:'Hello'},{role:'assistant',content:[{type:'text',text:'Hi'}]}]});assert.equal(simple.state.chats[0].messages[1].text,'Hi');
  const source=freshState();source.projects.push({id:'project',name:'Example'});newChat(source,'project');const result=mergeChatImport(base,source);assert.notEqual(result.state.projects[0].id,'project');assert.equal(result.state.chats[0].project,result.state.projects[0].id);
});
test('first-run completion persists and storage failures do not claim success',()=>{
  const data=new Map(),storage={getItem:k=>data.get(k),setItem:(k,v)=>data.set(k,v)};
  assert.equal(needsSetup(storage),true);assert.equal(completeSetup(storage),true);assert.equal(data.get(FIRST_RUN_KEY),'done');assert.equal(needsSetup(storage),false);
  const broken={getItem(){throw Error('denied');},setItem(){throw Error('full');}};assert.equal(needsSetup(broken),true);assert.equal(completeSetup(broken),false);
});
