// App history stays on this computer. Only text is rendered; never imported HTML.
export function connectImports(post,ui){
  const dialog=document.createElement('dialog');dialog.id='bench-import-dialog';
  dialog.innerHTML='<div class="import-heading"><h2>Bring your chats along</h2><button aria-label="Close import">Close</button></div><p>Bench finds local AI chat history on this computer. Your original chats stay untouched.</p><label>Import <select aria-label="Import batch size"><option value="10">10 recent chats</option><option value="50">50 recent chats</option><option value="2000">All available (up to 2,000)</option></select></label><div class="import-apps"></div><p class="import-status" role="status" aria-live="polite"></p><div class="import-footer"><button data-rescan>Scan again</button><button data-file>Import JSON file</button></div>';
  document.body.append(dialog);
  const list=dialog.querySelector('.import-apps'),status=dialog.querySelector('.import-status'),select=dialog.querySelector('select');let busy=false;
  const lock=value=>{busy=value;dialog.querySelectorAll('button,select').forEach(b=>b.disabled=value);};
  dialog.querySelector('.import-heading button').onclick=()=>dialog.close();
  dialog.addEventListener('cancel',event=>{if(busy)event.preventDefault();});
  dialog.querySelector('[data-file]').onclick=()=>{dialog.close();ui.importChatFile();};
  async function scan(){
    lock(true);list.replaceChildren();status.textContent='Looking for AI apps…';
    try{const data=await(await post('import-apps',{})).json();
      for(const app of data.apps){const row=document.createElement('div');row.className='import-app';const description=document.createElement('div'),name=document.createElement('strong'),note=document.createElement('small');name.textContent=app.name;note.textContent=app.available?`${app.count} ${app.unit} found${app.limited?' · scan capped':''}`:app.note;description.append(name,note);row.append(description);
        if(app.available){const button=document.createElement('button');button.textContent='Import';button.setAttribute('aria-label','Import from '+app.name);button.onclick=async()=>{lock(true);status.textContent='Reading '+app.name+' chats…';try{const data=await(await post('import-app',{app:app.id,limit:Number(select.value)})).json();if(!data.chats.length){status.textContent='No readable chats found. '+data.warnings.join(' ');return;}const result=ui.applyChatImport(data.chats);status.textContent=`Imported ${result.added} chats from ${app.name}. ${result.duplicates} duplicates skipped.`+(data.skipped?` ${data.skipped} entries could not be imported.`:'')+(data.remaining||data.limited?' More history may be available; choose a larger batch.':'')+' '+data.warnings.join(' ');}catch(e){status.textContent=e.message;}finally{lock(false);}};row.append(button);}list.append(row);}
      status.textContent=data.apps.length?'Choose an app to import. Existing chats and settings are kept.':'No supported local history found. Supported: '+data.supported.join(', ')+'. You can also import a JSON export.';
    }catch(e){status.textContent=e.message;}finally{lock(false);}
  }
  dialog.querySelector('[data-rescan]').onclick=scan;
  window.addEventListener('bench-import-apps',()=>{if(!dialog.open){dialog.showModal();scan();}});
}
