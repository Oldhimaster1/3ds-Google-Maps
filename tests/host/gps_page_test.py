#!/usr/bin/env python3
"""Execute the actual embedded page's JS with mocked browser APIs, not Safari."""
import ast
import json
from pathlib import Path
import subprocess

source = (Path(__file__).resolve().parents[2] / 'source/gps_server.c').read_text()
literal = source.split('static const char GPS_PAGE[] =', 1)[1].split('\n/* ===', 1)[0]
page = ''.join(ast.literal_eval(line.strip().rstrip(';'))
               for line in literal.splitlines() if line.strip().startswith('"'))
script = page.split('<script>\n', 1)[1].split('</script>', 1)[0]
subprocess.run(['node', '-e', r'''
const vm = require('vm'), assert = require('assert');
let text=''; process.stdin.on('data', d=>text+=d);
process.stdin.on('end', async()=>{
 const script=JSON.parse(text), token='0123456789abcdef0123456789abcdef';
 const store=new Map();
 function boot(hash, secure=true, storage=store, code=204){
  let watch, requests=[], replaced, s={textContent:'',className:'box'};
  let c={location:{hash}, history:{replaceState:(a,b,path)=>replaced=path},
   sessionStorage:{getItem:k=>storage.get(k),setItem:(k,v)=>storage.set(k,v)},
   window:{isSecureContext:secure}, document:{getElementById:()=>s},
   navigator:{geolocation:{watchPosition:(send,err)=>watch={send,err}}},
   Date, AbortController, setTimeout:()=>1, clearTimeout:()=>{},
   fetch:(path,args)=>{requests.push({path,args});return Promise.resolve({ok:code===204,status:code});}};
  vm.runInNewContext(script,c);
  return {watch,requests,s,c,replaced};
 }
 let fresh=boot('#'+token); assert(fresh.watch); assert.equal(fresh.replaced,'/');
 let reload=boot(''); assert(reload.watch); assert.equal(reload.c.token,token);
 let missing=boot('',true,new Map()); assert(!missing.watch); assert.match(missing.s.textContent,/Scan the QR/);
 let insecure=boot('#'+token,false); assert(!insecure.watch); assert.match(insecure.s.textContent,/does not trust/);
 let denied=boot('#'+token,true,store,403);
 let pos={coords:{latitude:35,longitude:-82,accuracy:4,speed:null,heading:null}};
 denied.watch.send(pos); await new Promise(setImmediate);
 assert.match(denied.s.textContent,/HTTP 403/); assert(!denied.s.textContent.includes('delivered'));
 fresh.watch.send(pos); await new Promise(setImmediate);
 assert.match(fresh.s.textContent,/Fix #1 delivered/);
 assert.equal(fresh.requests[0].args.headers.Authorization,'Bearer '+token);
 assert.equal(JSON.parse(fresh.requests[0].args.body).lat,35);
 fresh.watch.send(pos); assert.equal(fresh.requests.length,1);
 fresh.watch.err({message:'<img src=x onerror=alert(1)>'});
 assert(fresh.s.textContent.includes('<img')); assert.equal(fresh.s.innerHTML,undefined);
 console.log('PASS embedded JS: private fragment, refresh, missing pairing, secure-context check, acknowledged delivery, errors, throttling');
 console.log('NOT VERIFIED: actual browser permission, geolocation or Safari certificate trust');
});
'''], input=json.dumps(script), text=True, check=True)
