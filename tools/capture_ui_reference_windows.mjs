// Exact approved HTML, rendered by installed Microsoft Edge on Windows only.
// CDP is bound to loopback in an isolated disposable browser profile.
import {spawn} from 'node:child_process';
import {readFile,writeFile,mkdir,mkdtemp,access,rm} from 'node:fs/promises';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
if(process.platform!=='win32')throw new Error('Real Windows reference capture required');
const reference=resolve(process.argv[2]), output=resolve(process.argv[3]);
const bytes=await readFile(reference), sha=createHash('sha256').update(bytes).digest('hex');
if(sha!=='0027e749e99758d1a56b5530b1fd6defac00af8006a436ac782b0ad9aa3d870f')throw new Error('Approved HTML identity mismatch');
const candidates=[join(process.env['ProgramFiles(x86)'],'Microsoft/Edge/Application/msedge.exe'),join(process.env.ProgramFiles,'Microsoft/Edge/Application/msedge.exe')];
let edge;for(const file of candidates){try{await access(file);edge=file;break}catch{}}
if(!edge)throw new Error('Installed Edge absent');
let malgun=false;try{await access(join(process.env.WINDIR,'Fonts/malgun.ttf'));malgun=true}catch{}
await mkdir(output,{recursive:true});const profile=await mkdtemp(join(tmpdir(),'luna-html-reference-'));
const child=spawn(edge,['--headless=new','--disable-gpu','--disable-background-networking','--no-first-run','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore'});
let socket;
try{
 let port;for(let i=0;i<300;i++){try{port=(await readFile(join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0];if(port)break}catch{}await new Promise(r=>setTimeout(r,100))}
 if(!port||!/^\d+$/.test(port))throw new Error('Owned Edge CDP did not start');
 const target=await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();
 socket=new WebSocket(target.webSocketDebuggerUrl);
 await new Promise((ok,bad)=>{socket.addEventListener('open',ok,{once:true});socket.addEventListener('error',bad,{once:true})});
 let sequence=0;const pending=new Map();
 socket.addEventListener('message',event=>{const message=JSON.parse(event.data);const waiter=pending.get(message.id);if(waiter){pending.delete(message.id);clearTimeout(waiter.timer);message.error?waiter.bad(new Error(JSON.stringify(message.error))):waiter.ok(message.result)}});
 const send=(method,params={})=>new Promise((ok,bad)=>{const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);bad(new Error('CDP timeout: '+method))},20000);pending.set(id,{ok,bad,timer});socket.send(JSON.stringify({id,method,params}))});
 const evaluate=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw new Error('Reference script failed');return r.result.value};
 await send('Page.enable');const browser=await send('Browser.getVersion');
 for(const [width,height] of [[1366,768],[1920,1080]]){
  await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
  await send('Page.navigate',{url:pathToFileURL(reference).href});
  let loaded=false;for(let i=0;i<100;i++){loaded=await evaluate('document.readyState==="complete" && document.querySelectorAll(".paper").length>0');if(loaded)break;await new Promise(r=>setTimeout(r,100))}
  if(!loaded)throw new Error('Original HTML did not render');
  await evaluate('document.fonts.ready.then(()=>true)');
  const metrics=await evaluate(`Object.fromEntries(['.titlebar','.toolbar','.main','.navpanel','.listpanel','.listhead','.modebar','.paper','.thumb','.papername','.paper-memo','.reader','.tabstrip','.tab','.readerbody','.sheet'].map(s=>{const e=document.querySelector(s),r=e.getBoundingClientRect(),c=getComputedStyle(e);return [s,{x:r.x,y:r.y,width:r.width,height:r.height,font:c.font,color:c.color,background:c.backgroundColor,padding:c.padding,radius:c.borderRadius}]}))`);
  const shot=await send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false,fromSurface:true});
  await writeFile(join(output,`html-windows-${width}.png`),Buffer.from(shot.data,'base64'));
  await writeFile(join(output,`html-windows-${width}-metrics.json`),JSON.stringify({referenceSha256:sha,viewport:{width,height},os:process.platform,malgunFontFilePresent:malgun,browser,metrics},null,2));
 }
 await send('Browser.close').catch(()=>{});socket.close();console.log('PASS: exact v0.9.1 captured in Windows Edge at both resolutions; Malgun font file present='+malgun);
}finally{if(socket&&socket.readyState===1)socket.close();if(child.exitCode===null)child.kill();await rm(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200}).catch(()=>{})}
