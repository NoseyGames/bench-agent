export function adapterPreferences(preferences) {
  const result = structuredClone(preferences);
  if (!result.customInstructions) result.instructions = '';
  if (!result.memoryEnabled) result.memoryNotes = '';
  return result;
}

// Prompt context, not a promise that the model will obey or implement Git actions.
export function desktopInstructions(preferences) {
  const p=adapterPreferences(preferences), sections=[];
  const add=(label,value)=>{if(typeof value==='string'&&value.trim())sections.push(label+':\n'+value.trim());};
  add('Name',p.name);add('Occupation',p.occupation);add('About the user',p.about);
  add('Response style',p.responseStyle);add('User instructions',p.instructions);
  add('Local memory notes',p.memoryNotes);
  add('Preferred Git branch prefix (when creating a branch)',p.gitBranchPrefix);
  add('Git commit instructions (when committing)',p.gitCommitInstructions);
  add('Pull request instructions (when preparing a pull request)',p.gitPrInstructions);
  return sections.join('\n\n');
}

export const defaultPreferences = () => ({
  mode:'dark', theme:'Warm', accent:'#cc7d5e', background:'#2d2d2b', foreground:'#f9f9f7',
  lightAccent:'#a75f43', lightBackground:'#faf9f6', lightForeground:'#252522',
  uiFont:'system', codeFont:'mono', uiSize:14, codeSize:13, contentSize:14, contrast:1,
  translucent:true, pointer:true, reduceMotion:'system', diffMarkers:'color', smoothing:true,
  fullWidth:false, sendShortcut:'enter', followUp:'queue', plainText:true, autoArchive:0,
  confirmClose:'drafts', showTimestamps:false, notifications:'unfocused', notifyQuestions:true,
  notifyPermissions:true, sound:'none', instructions:'', name:'', occupation:'', about:'',
  responseStyle:'Balanced', customInstructions:true, model:'gpt-4o-mini', effort:'Medium', memoryEnabled:true, toolMemory:false, memoryNotes:'',
  approvalPolicy:'on-request', sandbox:'workspace-write', webSearch:'disabled', outputDetail:'default', reasoningSummary:'auto', networkAccess:false, workspaceDependencies:false,
  visibleEfforts:['Minimal','Low','Medium','High','Maximum'], permission:'workspace',
  showFullAccess:false, defaultProject:'inherit', sidebarWidth:240,
  gitBranchPrefix:'work/', gitCommitInstructions:'', gitPrInstructions:'',
  shortcuts:{new:'Mod+N',search:'Mod+K',sidebar:'Mod+B',settings:'Mod+,',review:'Mod+Shift+D'}
});
const enums={mode:['dark','light','system'],theme:['Warm','Neutral','Slate','Custom'],uiFont:['system','sans','serif','mono'],codeFont:['mono','system'],reduceMotion:['system','on','off'],diffMarkers:['color','symbols'],sendShortcut:['enter','mod-enter'],followUp:['queue','steer'],confirmClose:['never','drafts','always'],notifications:['never','unfocused','always'],sound:['none','soft','bell','pop'],responseStyle:['Concise','Balanced','Detailed'],permission:['read','workspace','full'],defaultProject:['inherit','none']};
export function normalizePreferences(input={}) {
  if(!input||typeof input!=='object'||Array.isArray(input))throw new Error('Invalid preferences');
  const result=defaultPreferences();
  for(const key of Object.keys(result)){
    if(!(key in input))continue;const value=input[key];
    if(key==='shortcuts'){
      if(!value||typeof value!=='object'||Array.isArray(value))throw new Error('Invalid shortcuts');
      for(const name of Object.keys(result.shortcuts)){if(value[name]!==undefined){if(typeof value[name]!=='string'||!/^Mod\+(Shift\+)?[A-Z,]$/.test(value[name]))throw new Error('Invalid shortcut');result.shortcuts[name]=value[name];}}
      if(new Set(Object.values(result.shortcuts)).size!==Object.keys(result.shortcuts).length)throw new Error('Duplicate shortcuts');
      continue;
    }
    if(key==='visibleEfforts'){if(!Array.isArray(value)||!value.length||value.some(v=>!['Minimal','Low','Medium','High','Maximum'].includes(v)))throw new Error('Invalid effort levels');result[key]=[...new Set(value)];continue;}
    if(enums[key]&&!enums[key].includes(value))throw new Error('Invalid '+key);
    if(/^(accent|background|foreground|lightAccent|lightBackground|lightForeground)$/.test(key)&&!/^#[\da-f]{6}$/i.test(value))throw new Error('Invalid color');
    if(typeof result[key]==='boolean'&&typeof value!=='boolean')throw new Error('Invalid '+key);
    if(typeof result[key]==='string'&&(typeof value!=='string'||value.length>12000))throw new Error('Invalid '+key);
    if(typeof result[key]==='number'&&(!Number.isFinite(value)||value<0||value>1000))throw new Error('Invalid '+key);
    result[key]=value;
  }
  for(const [key,min,max] of [['uiSize',12,20],['codeSize',10,22],['contentSize',12,24],['contrast',.7,1.5],['sidebarWidth',200,360]])result[key]=Math.min(max,Math.max(min,result[key]));
  for(const [key,options] of Object.entries({approvalPolicy:['untrusted','on-failure','on-request','never'],sandbox:['read-only','workspace-write','full'],webSearch:['disabled','cached','live'],outputDetail:['default','low','medium','high'],reasoningSummary:['auto','concise','detailed','none']}))if(!options.includes(result[key]))throw new Error('Invalid '+key);
  if(!result.showFullAccess&&result.permission==='full')result.permission='workspace';
  if(!result.visibleEfforts.includes(result.effort))result.effort=result.visibleEfforts[0];
  if(![0,7,14,30,90].includes(result.autoArchive))throw new Error('Invalid archive interval');
  return result;
}
export function shouldSend(event,preference){return event.key==='Enter'&&!event.isComposing&&!event.shiftKey&&!!(preference==='enter'||event.ctrlKey||event.metaKey);}
export function shortcutMatches(event,shortcut){const parts=shortcut.split('+');return !!(event.ctrlKey||event.metaKey)&&!event.altKey&&event.shiftKey===parts.includes('Shift')&&event.key.toUpperCase()===parts.at(-1);}
export function archiveInactive(state,now=Date.now()){const days=state.preferences?.autoArchive;if(!days)return 0;let count=0;for(const chat of state.chats){if(!chat.archived&&!chat.pinned&&chat.id!==state.active&&!chat.draft&&!state.queue?.some(item=>item.chatId===chat.id)&&chat.messages.length&&chat.updated<now-days*86400000){chat.archived=true;count++;}}return count;}
