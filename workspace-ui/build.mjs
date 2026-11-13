import fs from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.dirname(fileURLToPath(import.meta.url));
await fs.mkdir(path.join(root,'dist'),{recursive:true});
for(const file of ['index.html','style.css','workspace.css','app.js','state.js','settings.js','preferences.js','appearance.js','desktop.js','desktop.css','ai.cur','connection.js','providers.mjs','brand.css','brand.svg','chat-import.js','first-run.js','calm.css','app-import.js'])await fs.copyFile(path.join(root,file),path.join(root,'dist',file));
console.log('Built dist/ frontend assets. Run desktop-host.mjs for the C engine connection.');
