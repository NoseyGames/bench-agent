import test from 'node:test';
import assert from 'node:assert/strict';
import {freshState,newChat,validateState,queueDraft,consumeQueued} from '../state.js';
import {archiveInactive} from '../preferences.js';

test('follow-up text and attachments survive reload with no duplicate draft',()=>{
  const s=freshState(),c=newChat(s);c.draft='next task';c.files=[{name:'a.txt',content:'reference',size:9}];
  let stored;queueDraft(s,c,()=>{stored=JSON.stringify(s);return true;});
  const loaded=validateState(JSON.parse(stored));assert.equal(loaded.queue[0].text,'next task');
  assert.equal(loaded.queue[0].files[0].content,'reference');assert.equal(loaded.chats[0].draft,'');
  loaded.chats[0].draft='a newer draft';
  const next=consumeQueued(loaded,()=>true);assert.equal(next.message.text,'next task');
  assert.equal(next.chat.draft,'a newer draft');assert.equal(loaded.queue.length,0);
});
test('storage failure retains the draft and rolls back queued consumption',()=>{
  const s=freshState(),c=newChat(s);c.draft='keep me';
  assert.throws(()=>queueDraft(s,c,()=>false),/draft is intact/);assert.equal(c.draft,'keep me');assert.equal(s.queue.length,0);
  queueDraft(s,c,()=>true);const before=structuredClone(s);
  assert.throws(()=>consumeQueued(s,()=>false),/remains queued/);assert.deepEqual(s,before);
});
test('archived and missing chats do not block later runnable messages',()=>{
  const s=freshState(),a=newChat(s),b=newChat(s);a.draft='archived';queueDraft(s,a,()=>true);a.archived=true;
  s.queue.push({chatId:'missing',text:'unavailable',files:[]});b.draft='run me';queueDraft(s,b,()=>true);
  assert.equal(consumeQueued(s,()=>true).chat.id,b.id);assert.equal(s.queue.length,2);
  assert.equal(consumeQueued(s,()=>true),null);a.archived=false;
  assert.equal(consumeQueued(s,()=>true).message.text,'archived');
});
test('legacy workspaces migrate and malformed queues are rejected',()=>{
  const s=freshState();newChat(s);delete s.queue;assert.deepEqual(validateState(s).queue,[]);
  s.queue=[{chatId:s.active,text:42,files:[]}];assert.throws(()=>validateState(s),/queued/);
});
test('auto archive excludes queued chats',()=>{
  const s=freshState(),c=newChat(s);c.updated=0;c.messages=[{role:'user',text:'old',files:[]}];
  c.draft='pending';queueDraft(s,c,()=>true);newChat(s);s.preferences.autoArchive=7;
  assert.equal(archiveInactive(s),0);assert.equal(c.archived,false);
});
