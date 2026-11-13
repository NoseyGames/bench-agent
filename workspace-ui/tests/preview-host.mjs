// Explicit offline UI smoke harness. Uses real C writes/build/tests, no LLM.
import {createHost} from '../desktop-host.mjs';
import fs from 'node:fs/promises';
import path from 'node:path';
const root=path.resolve('ui-smoke-project');await fs.mkdir(root,{recursive:true});
await fs.writeFile(path.join(root,'main.c'),'int main(void) { return 1; }\n');
const plan=path.join(root,'fixture-plan.json');
await fs.writeFile(plan,JSON.stringify({summary:'Offline UI verification: fix the return value, build and test.',steps:['Review the file edit','Compile the C project','Run the test executable'],files:[{name:'main.c',content:'/* Verified by the desktop integration test. */\nint main(void) {\n    return 0;\n}\n'}],read_files:[],commands:[{kind:'build',command:'gcc main.c -o check.exe',timeout_seconds:30},{kind:'test',command:process.platform==='win32'?'check.exe':'./check.exe',timeout_seconds:30}]}));
const host=createHost({engine:path.resolve('../bench-engine.exe'),engineArgs:['--plan',plan]});
host.server.listen(0,'127.0.0.1',async()=>{await fetch('http://127.0.0.1:'+host.server.address().port+'/api/config',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+host.token},body:JSON.stringify({key:'offline-fixture-not-a-real-key'})});console.log(JSON.stringify({url:'http://127.0.0.1:'+host.server.address().port+'/#'+host.token,root}));});
for(const signal of ['SIGTERM','SIGINT'])process.on(signal,()=>host.close());
