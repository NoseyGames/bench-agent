export function applyAppearance(p,root=document.documentElement){
  const light=p.mode==='light'||(p.mode==='system'&&matchMedia('(prefers-color-scheme: light)').matches);
  root.dataset.theme=light?'light':'dark';
  const background=light?p.lightBackground:p.background,foreground=light?p.lightForeground:p.foreground,accent=light?p.lightAccent:p.accent;
  const fonts={system:'"Segoe UI", system-ui, sans-serif',sans:'Arial, Helvetica, sans-serif',serif:'Georgia, serif',mono:'Consolas, monospace'};
  const blend=(a,b,t)=>{const rgb=v=>v.match(/[\da-f]{2}/gi).map(x=>parseInt(x,16));const x=rgb(a),y=rgb(b);return '#'+x.map((v,i)=>Math.round(v+(y[i]-v)*t).toString(16).padStart(2,'0')).join('');};
  const vars={'surface':background,'text':foreground,'accent':accent,'sidebar':blend(background,light?'#eeeeee':'#101419',light?.35:.32),'raised':blend(background,foreground,light?.015:.12),'card':blend(background,foreground,light?.035:.055),'bubble':blend(background,foreground,light?.055:.045),'hover':blend(background,foreground,light?.09:.085),'border':blend(background,foreground,.09*p.contrast),'muted':blend(background,foreground,.5*p.contrast),'button':foreground,'ink':background,'ui-font':fonts[p.uiFont],'code-font':fonts[p.codeFont],'ui-size':p.uiSize+'px','content-size':p.contentSize+'px','code-size':p.codeSize+'px','sidebar-width':p.sidebarWidth+'px','conversation-width':p.fullWidth?'100%':'800px'};
  for(const [key,value] of Object.entries(vars))root.style.setProperty('--'+key,value);
  root.dataset.pointer=String(p.pointer);root.dataset.motion=p.reduceMotion;root.dataset.diff=p.diffMarkers;root.dataset.translucent=String(p.translucent);root.dataset.smoothing=String(p.smoothing);
}
