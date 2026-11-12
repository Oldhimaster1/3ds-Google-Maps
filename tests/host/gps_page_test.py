#!/usr/bin/env python3
"""Execute the production embedded GPS page in Node's JS VM with mocked browser APIs.

Covers pairing refresh/rotation, transient fetch retries while stationary,
authorization, background/foreground transitions, and safe error rendering.
This is NOT a real Safari or 3DS acceptance test.
"""
import ast
import json
from pathlib import Path
import subprocess

source = (Path(__file__).resolve().parents[2] / 'source/gps_server.c').read_text()
literal = source.split('static const char GPS_PAGE[] =', 1)[1].split('/* ===', 1)[0]
page = ''.join(ast.literal_eval(line.strip().rstrip(';'))
               for line in literal.splitlines() if line.strip().startswith('"'))
script = page.split('<script>', 1)[1].split('</script>', 1)[0]
assert '3DS GPS Bridge' in page and 'Authorization' in script
subprocess.run(['node', '-e', r'''
const vm=require('vm'), assert=require('node:assert/strict');
let text='';
process.stdin.on('data',data=>text+=data);
process.stdin.on('end',async()=>{
 const code=JSON.parse(text);
 const goodToken='0123456789abcdef0123456789abcdef';
 const nextToken='11111111111111111111111111111111';
 const position={coords:{latitude:35,longitude:-82,accuracy:4,speed:null,heading:null}};
 const flush=()=>new Promise(resolve=>setImmediate(resolve));
 function boot({hash='#'+goodToken,search='',secure=true,storage=new Map(),responses=[204],hidden=false}={}){
   let now=100000,watch=null,cleared=0,interval=null;
   const status={textContent:'',className:'box'};
   const requests=[],timeouts=new Map(),listeners={};
   let timer=0,replaced=null;
   const api={
     location:{hash,pathname:'/',search},
     history:{replaceState:(a,b,path)=>{replaced=path;}},
     sessionStorage:{
       getItem:k=>storage.get(k)||null,
       setItem:(k,v)=>storage.set(k,v),
       removeItem:k=>storage.delete(k)
     },
     window:{isSecureContext:secure},
     document:{hidden,getElementById:()=>status,
               addEventListener:(name,fn)=>{listeners[name]=fn;}},
     navigator:{geolocation:{
       watchPosition:(send,err)=>{watch={send,err};return 3;},
       clearWatch:id=>{assert.equal(id,3);cleared++;}
     }},
     Date:{now:()=>now},AbortController,
     setTimeout:(cb,delay)=>{const id=++timer;timeouts.set(id,{cb,delay});return id;},
     clearTimeout:id=>timeouts.delete(id),
     setInterval:(fn)=>{interval=fn;return 12;},
     clearInterval:id=>{assert.equal(id,12);interval=null;},
     fetch:(path,opts)=>{
       requests.push({path,opts});
       const response=responses.shift()??204;
       if(response==='network')return Promise.reject(Error('socket closed'));
       return Promise.resolve({ok:response===204,status:response});
     }
   };
   vm.runInNewContext(code,api);
   return {status,requests,storage,api,get watch(){return watch;},
           get cleared(){return cleared;},get interval(){return interval;},
           get replaced(){return replaced;},
           advance(ms){now+=ms;},flush};
 }
 const freshStore=new Map();
 const fresh=boot({storage:freshStore});
 assert(fresh.watch&&fresh.interval);
 assert.equal(fresh.replaced,'/');
 assert.equal(freshStore.get('gpsToken'),goodToken);
 fresh.watch.send(position); await flush();
 assert.match(fresh.status.textContent,/fix #1 delivered/);
 assert.equal(fresh.requests[0].path,'/location');
 assert.equal(fresh.requests[0].opts.headers.Authorization,'Bearer '+goodToken);
 assert.equal(JSON.parse(fresh.requests[0].opts.body).lat,35);
 fresh.watch.send(position); await flush();
 assert.equal(fresh.requests.length,1,'rate limit prevents flooding');
 fresh.advance(2600);fresh.interval();await flush();
 assert.equal(fresh.requests.length,2,'stationary location is retried periodically');

 const reload=boot({hash:'',storage:freshStore});
 assert(reload.watch,'a same-tab reload retains pairing');
 const oldStore=new Map([['gpsToken',goodToken]]);
 const freshQr=boot({hash:'',search:'?pair='+nextToken,storage:oldStore});
 assert(freshQr.watch,'query QR must survive certificate-warning navigation');
 assert.equal(oldStore.get('gpsToken'),nextToken,'fresh QR must override stale storage');
 assert.equal(freshQr.replaced,'/','remove secret from visible query');
 const missing=boot({hash:'',storage:new Map()});
 assert(!missing.watch);
 assert.match(missing.status.textContent,/Scan the CURRENT QR/);
 const insecure=boot({secure:false});
 assert(!insecure.watch);
 assert.match(insecure.status.textContent,/does not trust/);

 const retry=boot({responses:['network',204]});
 retry.watch.send(position);await flush();
 assert.match(retry.status.textContent,/retrying automatically/);
 assert.equal(retry.requests.length,1);
 retry.advance(2100);retry.interval();await flush();
 assert.equal(retry.requests.length,2,'retry does not require movement');
 assert.match(retry.status.textContent,/fix #1 delivered/);

 const forbiddenStore=new Map();
 const forbidden=boot({hash:'#'+goodToken,storage:forbiddenStore,responses:[403]});
 forbidden.watch.send(position);await flush();
 assert.equal(forbidden.cleared,1);
 assert(!forbidden.interval);
 assert.equal(forbiddenStore.has('gpsToken'),false);
 assert.match(forbidden.status.textContent,/Scan the CURRENT QR/);
 forbidden.advance(5000);forbidden.watch.send(position);await flush();
 assert.equal(forbidden.requests.length,1,'stale token cannot flood server');
 const newPair=boot({hash:'#'+nextToken,storage:forbiddenStore,responses:[204]});
 newPair.watch.send(position);await flush();
 assert.match(newPair.status.textContent,/fix #1 delivered/);
 assert.equal(newPair.requests[0].opts.headers.Authorization,'Bearer '+nextToken);

 const background=boot({hidden:true});
 background.watch.send(position);await flush();
 assert.equal(background.requests.length,0);
 background.api.document.hidden=false;
 background.advance(3000);
 background.interval();await flush();
 assert.equal(background.requests.length,1);

 const errors=boot();
 errors.watch.err({code:2,message:'<img src=x onerror=alert(1)>'});
 assert(errors.status.textContent.includes('<img'));
 assert.equal(errors.status.innerHTML,undefined,'untrusted errors must never use innerHTML');

 console.log('PASS GPS page checks: query/fragment scan, refresh, missing/insecure pairing, HTTP auth,');
 console.log('retry while stationary, throttling, stale-token rotation, privacy, backgrounding');
 console.log('NOT VERIFIED: iOS Safari certificate, real geolocation, console networking');
});
'''],input=json.dumps(script),text=True,check=True)
