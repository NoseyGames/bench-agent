import {validateState} from './state.js';

function messagesOf(chat){
  if(!chat.mapping)return chat.messages;
  // ChatGPT exports are trees. Import the selected branch, not every alternative.
  if(!chat.current_node)throw Error('This conversation has no selected branch. Export it as a messages array instead.');
  const nodes=[],seen=new Set();let id=chat.current_node;
  while(id){if(seen.has(id))throw Error('Conversation contains a cyclic message tree.');seen.add(id);const n=chat.mapping[id];if(!n)throw Error('Conversation references a missing message.');if(n.message)nodes.push(n.message);id=n.parent;}
  return nodes.reverse();
}
function textOf(content){
  if(typeof content==='string')return content;
  const parts=Array.isArray(content)?content:content?.parts;
  if(Array.isArray(parts))return parts.map(p=>typeof p==='string'?p:typeof p?.text==='string'?p.text:'').filter(Boolean).join('\n');
  return typeof content?.text==='string'?content.text:'';
}
const timestamp=n=>Number.isFinite(n)?(n<1e12?n*1000:n):Date.now();
const signature=c=>JSON.stringify({title:c.title,messages:c.messages.map(m=>({role:m.role,text:m.text,files:m.files||[]})),draft:c.draft||'',files:c.files||[]});

/** Returns a new state. Caller persists it before replacing live state.
 * Imported chats are history only: no queued instructions or preferences run. */
export function mergeChatImport(state,input){
  let incoming,projects=[],skipped=0;
  if(input?.version===1&&Array.isArray(input.chats)){
    const native=validateState(structuredClone(input));incoming=native.chats;projects=native.projects;
  }else{
    const list=Array.isArray(input)?input:Array.isArray(input?.conversations)?input.conversations:input?.messages?[input]:null;
    if(!list)throw Error('Choose a Bench backup, ChatGPT conversations.json, or JSON with a messages array.');
    incoming=list.map((chat,i)=>{
      if(!chat||typeof chat!=='object')throw Error('Invalid conversation in import.');
      const source=messagesOf(chat);if(!Array.isArray(source))throw Error('Each conversation needs messages or a ChatGPT message tree.');
      const messages=[];
      for(const m of source){const role=m?.role||m?.author?.role;if(!['user','assistant'].includes(role)){skipped++;continue;}const text=textOf(m.text??m.content);if(!text.trim()){skipped++;continue;}messages.push({id:crypto.randomUUID(),role,text,files:[],created:timestamp(m.created??m.create_time)});}
      return {id:crypto.randomUUID(),title:typeof chat.title==='string'?chat.title.slice(0,200):'Imported chat '+(i+1),project:null,pinned:false,archived:false,draft:'',files:[],messages,diff:'',updated:timestamp(chat.updated??chat.update_time)};
    }).filter(c=>c.messages.length);
  }
  if(!incoming.length)throw Error('No readable chats were found. Images, tool calls, and system messages are not imported from external exports.');
  const next=structuredClone(state),known=new Set(next.chats.map(signature)),projectIds=new Map();let added=0,duplicates=0,first=null;
  for(const chat of incoming){
    const key=signature(chat);if(known.has(key)){duplicates++;continue;}known.add(key);
    const c=structuredClone(chat);c.id=crypto.randomUUID();c.messages=c.messages.map(m=>({...m,id:crypto.randomUUID()}));
    if(c.project){const old=projects.find(p=>p.id===c.project);if(!old)throw Error('Imported project is missing.');if(!projectIds.has(old.id)){let p=next.projects.find(p=>p.name===old.name);if(!p){p={id:crypto.randomUUID(),name:old.name};next.projects.push(p);}projectIds.set(old.id,p.id);}c.project=projectIds.get(old.id);}
    c.archived=false;next.chats.push(c);first??=c.id;added++;
  }
  if(first)next.active=first;
  return {state:validateState(next),added,duplicates,skipped};
}
