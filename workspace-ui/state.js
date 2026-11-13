import {normalizePreferences,defaultPreferences} from './preferences.js';
export const STORAGE_KEY = 'workspace-ui-v1';
export const freshState = () => ({version:1,projects:[],chats:[],queue:[],active:null,theme:'dark',preferences:defaultPreferences()});
export function validateState(value) {
  if (!value || value.version!==1 || !Array.isArray(value.projects)||!Array.isArray(value.chats)) throw new Error('Invalid workspace file');
  const ids=new Set();
  for(const p of value.projects){if(typeof p.id!=='string'||typeof p.name!=='string'||ids.has(p.id))throw new Error('Invalid project');ids.add(p.id);}
  const chats=new Set();
  for(const c of value.chats){
    if(typeof c.id!=='string'||chats.has(c.id)||typeof c.title!=='string'||typeof c.draft!=='string'||!Array.isArray(c.messages)||!Array.isArray(c.files)||!(c.project===null||ids.has(c.project)))throw new Error('Invalid chat');
    if(c.diff!==undefined&&typeof c.diff!=='string')throw new Error('Invalid diff');
    if(typeof c.pinned!=='boolean'||typeof c.archived!=='boolean')throw new Error('Invalid chat flags');
    chats.add(c.id);
    for(const m of c.messages){if(!['user','assistant'].includes(m.role)||typeof m.text!=='string'||!Array.isArray(m.files))throw new Error('Invalid message');for(const f of m.files)validateFile(f);}
    for(const f of c.files)validateFile(f);
  }
  if(value.active!==null&&!chats.has(value.active))throw new Error('Invalid active chat');
  if(!['dark','light'].includes(value.theme))throw new Error('Invalid theme');
  value.preferences=normalizePreferences(value.preferences||{mode:value.theme});
  if(value.queue===undefined)value.queue=[];
  if(!Array.isArray(value.queue))throw new Error('Invalid follow-up queue');
  for(const item of value.queue){
    if(!item||typeof item.chatId!=='string'||typeof item.text!=='string'||!Array.isArray(item.files))throw new Error('Invalid queued message');
    item.files.forEach(validateFile);
  }
  return value;
}

export function queueDraft(state,chat,persist) {
  const draft=chat.draft,files=chat.files;
  state.queue.push({chatId:chat.id,text:draft,files:structuredClone(files)});
  chat.draft='';chat.files=[];
  if(!persist()){state.queue.pop();chat.draft=draft;chat.files=files;throw new Error('Follow-up was not queued because storage could not be saved. Your draft is intact.');}
}

// Archived chats remain queued for restoration; they never block runnable chats.
export function consumeQueued(state,persist) {
  const index=state.queue.findIndex(item=>state.chats.some(c=>c.id===item.chatId&&!c.archived));
  if(index<0)return null;
  const item=state.queue[index],chat=state.chats.find(c=>c.id===item.chatId);
  const before={draft:chat.draft,files:chat.files,title:chat.title,updated:chat.updated,count:chat.messages.length};
  chat.draft=item.text;chat.files=item.files;
  const message=submitMessage(chat);
  chat.draft=before.draft;chat.files=before.files;state.queue.splice(index,1);
  if(!persist()){
    state.queue.splice(index,0,item);chat.messages.length=before.count;chat.title=before.title;chat.updated=before.updated;
    throw new Error('Queued message could not be saved. It remains queued.');
  }
  return {chat,message};
}
function validateFile(f){if(!f||typeof f.name!=='string'||typeof f.content!=='string'||typeof f.size!=='number')throw new Error('Invalid attachment');}
export function newChat(state,project=null) {
  const chat={id:crypto.randomUUID(),title:'New chat',project,pinned:false,archived:false,draft:'',files:[],messages:[],diff:'',updated:Date.now()};
  state.chats.unshift(chat);state.active=chat.id;return chat;
}
export function submitMessage(chat){
  const text=chat.draft.trim();if(!text&&!chat.files.length)return null;
  const message={id:crypto.randomUUID(),role:'user',text,files:chat.files.map(f=>({...f})),created:Date.now()};
  chat.messages.push(message);if(chat.title==='New chat')chat.title=(text||chat.files[0].name).slice(0,65);
  chat.draft='';chat.files=[];chat.updated=Date.now();return message;
}
export function searchChats(state,query,archived=false){const q=query.toLocaleLowerCase().trim();return state.chats.filter(c=>!!c.archived===archived&&(c.title+' '+c.messages.map(m=>m.text).join(' ')).toLocaleLowerCase().includes(q));}
export function classifyDiff(text){let additions=0,deletions=0;const lines=text.split('\n').map(text=>{let type='context';if(text.startsWith('+++')||text.startsWith('---')||text.startsWith('diff --git'))type='file';else if(text.startsWith('+')){type='add';additions++;}else if(text.startsWith('-')){type='remove';deletions++;}else if(text.startsWith('@@'))type='hunk';return {text,type};});return {lines,additions,deletions};}
