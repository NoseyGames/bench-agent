// Provider profiles contain no credentials. A local profile never borrows a cloud key.
export const presets={
  openai:{label:'OpenAI',baseUrl:'https://api.openai.com/v1',model:'gpt-4o-mini',jsonMode:true},
  ollama:{label:'Ollama',baseUrl:'http://127.0.0.1:11434/v1',model:'',jsonMode:false},
  lmstudio:{label:'LM Studio',baseUrl:'http://127.0.0.1:1234/v1',model:'',jsonMode:false},
  local:{label:'Other local server',baseUrl:'http://127.0.0.1:8080/v1',model:'',jsonMode:false}
};
export function profile(value={}){
  const provider=value.provider||'openai',defaults=presets[provider];
  if(!defaults)throw Error('Choose Ollama, LM Studio, OpenAI, or another local server.');
  const url=new URL(value.baseUrl||defaults.baseUrl);
  if(url.username||url.password||url.search||url.hash)throw Error('Server address cannot contain credentials, a query, or a fragment.');
  if(provider==='openai'&&url.href.replace(/\/$/,'')!==defaults.baseUrl)throw Error('The OpenAI address is fixed. Choose a local provider for a local server.');
  if(provider!=='openai'&&(url.protocol!=='http:'||!['127.0.0.1','localhost','[::1]'].includes(url.hostname)))throw Error('Local servers must use HTTP on 127.0.0.1, localhost, or [::1].');
  const model=value.model??defaults.model;
  if(typeof model!=='string'||model.length>200||/[\r\n]/.test(model))throw Error('Enter a valid model identifier.');
  return {provider,baseUrl:url.href.replace(/\/+$/,''),model:model.trim(),jsonMode:typeof value.jsonMode==='boolean'?value.jsonMode:defaults.jsonMode};
}
export async function discover(config,key){
  const headers=key?{Authorization:'Bearer '+key}:{};
  let response;
  try{response=await fetch(config.baseUrl+'/models',{headers,redirect:'error',signal:AbortSignal.timeout(10000)});}
  catch{throw Error(`Cannot reach ${presets[config.provider].label}. Start its server and check the address, then try again.`);}
  if(!response.ok){await response.body?.cancel();throw Error(`Model server returned HTTP ${response.status}. Check its address and API key.`);}
  let size=0;const chunks=[];for await(const chunk of response.body){size+=chunk.length;if(size>1024*1024)throw Error('Model list exceeds 1 MiB.');chunks.push(chunk);}const raw=Buffer.concat(chunks).toString('utf8');
  let data;try{data=JSON.parse(raw);}catch{throw Error('The server did not return a valid model list. Use its OpenAI-compatible /v1 address.');}
  if(!Array.isArray(data.data))throw Error('The server is missing the OpenAI-compatible models endpoint.');
  return data.data.map(m=>m.id).filter(id=>typeof id==='string'&&id.length<=200).slice(0,500);
}
