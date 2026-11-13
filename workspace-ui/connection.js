import {presets} from './providers.mjs';

// Non-secret preferences survive restarts. Keys stay in the host's memory.
export function connectModels(post,onChange){
  let current=null;
  const dialog=document.createElement('dialog');dialog.id='bench-connection';
  dialog.innerHTML=`<form><div class="connection-heading"><span class="bench-mark">b.</span><div><h2>Choose your model</h2><p>Local on your computer, or connected to OpenAI.</p></div></div>
    <label>Run with <select id="model-provider"><option value="ollama">Ollama · local</option><option value="lmstudio">LM Studio · local</option><option value="local">Other local server</option><option value="openai">OpenAI · cloud</option></select></label>
    <p id="provider-help"></p><label>Server address <input id="model-address" required spellcheck="false"></label>
    <label id="model-key-label">API key <input id="model-key" type="password" autocomplete="off" placeholder="Optional for local servers"></label>
    <div class="model-find"><button type="button" id="model-discover">Find models</button><span>Lists models available on this server</span></div>
    <label>Model <input id="model-choice" list="model-list" required placeholder="Find models above, or enter its exact name" spellcheck="false"><datalist id="model-list"></datalist></label>
    <details><summary>Advanced</summary><label class="check-label"><input id="model-json" type="checkbox"> Request JSON output mode</label><p>Turn this on only if your model server supports JSON mode. Bench always validates the plan before executing it.</p><button type="button" id="model-forget">Forget this server’s saved key</button></details>
    <p id="model-result" role="status"></p><div class="connection-actions"><button type="button" id="model-cancel">Cancel</button><button class="primary" type="submit">Use this model</button></div></form>`;
  document.body.append(dialog);const $=id=>dialog.querySelector('#'+id);
  const result=text=>$('model-result').textContent=text;
  function help(){const provider=$('model-provider').value,local=provider!=='openai';
    $('provider-help').textContent=provider==='ollama'?'Start Ollama and download a model, then choose Find models. No OpenAI key needed.':provider==='lmstudio'?'Load a model in LM Studio and start its local server from the Developer tab.':provider==='local'?'Connect to an OpenAI-compatible server running on this computer. Include /v1 in the address.':'Tasks and selected project context are sent to OpenAI. Your API key stays in this app’s host memory.';
    $('model-address').readOnly=!local;$('model-key').placeholder=local?'Optional · only if your local server requires a key':'Paste a key, or use OPENAI_API_KEY';
  }
  function fill(c){$('model-provider').value=c.provider;$('model-address').value=c.baseUrl;$('model-choice').value=c.model;$('model-json').checked=c.jsonMode;$('model-key').value='';$('model-list').replaceChildren();help();}
  function values(){return {provider:$('model-provider').value,baseUrl:$('model-address').value.trim(),model:$('model-choice').value.trim(),jsonMode:$('model-json').checked,key:$('model-key').value.trim()};}
  let discovery=0;
  $('model-provider').onchange=()=>{discovery++;const provider=$('model-provider').value;fill({...presets[provider],provider});result('');};
  $('model-discover').onclick=async()=>{const button=$('model-discover');button.disabled=true;const ticket=++discovery;result('Looking for models…');try{const response=await(await post('models',values())).json();if(ticket!==discovery)return;$('model-list').replaceChildren(...response.models.map(id=>{const option=document.createElement('option');option.value=id;return option;}));if(!$('model-choice').value&&response.models.length)$('model-choice').value=response.models[0];result(response.models.length?`Found ${response.models.length} model${response.models.length===1?'':'s'}. Choose one below.`:'Server reached, but no models are available. Download or load a model in your local server first.');}catch(e){if(ticket===discovery)result(e.message);}finally{button.disabled=false;}};
  $('model-forget').onclick=async()=>{try{current=await(await post('config',{...values(),key:'',clearKey:true})).json();onChange(current);$('model-key').value='';result('The key for this server was removed from host memory.');}catch(e){result(e.message);}};
  $('model-cancel').onclick=()=>dialog.close();
  dialog.querySelector('form').onsubmit=async e=>{e.preventDefault();try{const value=values();if(!value.model)throw Error('Choose a model first.');current=await(await post('config',value)).json();const {key,...safe}=value;try{localStorage.setItem('bench-model-profile',JSON.stringify(safe));}catch{}$('model-key').value='';onChange(current);if(!current.configured){result('Add an API key, or choose a local provider.');return;}dialog.close();}catch(e){result(e.message);}};
  dialog.addEventListener('close',()=>{discovery++;$('model-key').value='';});
  const ready=(async()=>{let saved={};try{saved=JSON.parse(localStorage.getItem('bench-model-profile')||'{}');}catch{}current=await(await post('config',saved)).json();onChange(current);return current;})();
  ready.catch(()=>{});
  return {ready,getProfile:()=>current,open(){fill(current||{...presets.ollama,provider:'ollama'});result(current?.hasKey?'A key is configured for this server. Leave the key field empty to keep it.':'');dialog.showModal();}};
}
