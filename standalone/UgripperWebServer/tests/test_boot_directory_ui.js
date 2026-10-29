const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const {File}=require('node:buffer'),{execFileSync}=require('node:child_process');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
const context={File,Uint8Array,TextEncoder};vm.createContext(context);
vm.runInContext(source.slice(source.indexOf('function packageFirmwareDirectory('),source.indexOf("$('#bootInputMode').onchange=")),context);
function file(name,data){const f=new File([data],name.split('/').at(-1));f.webkitRelativePath='release/'+name;return f;}
const files=[file('update.sh','#!/bin/sh\nexit 0\n'),file('SHA256SUMS','checksums\n'),file('payload/version.info','V1.00\n'),file('payload/binary',new Uint8Array([0,255,10,128]))];
async function main(){
 const packed=context.packageFirmwareDirectory(files);
 assert.equal(packed.name,'firmware-release.tar');
 execFileSync('/usr/bin/python3',['-c',`import io,sys,tarfile
with tarfile.open(fileobj=io.BytesIO(sys.stdin.buffer.read())) as t:
 assert t.getnames()==['release/'+n for n in ['update.sh','SHA256SUMS','payload/version.info','payload/binary']]
 assert t.extractfile('release/update.sh').read()==b'#!/bin/sh\\nexit 0\\n'
 assert t.extractfile('release/payload/version.info').read()==b'V1.00\\n'
 assert t.extractfile('release/payload/binary').read()==bytes([0,255,10,128])
 assert all(m.isfile() for m in t.getmembers())
`],{input:Buffer.from(await packed.arrayBuffer())});
 for(const bad of ['../bad','/absolute','bad name','payload/../../bad'])assert.throws(()=>context.packageFirmwareDirectory([...files,file(bad,'')]),/非法/);
 assert.throws(()=>context.packageFirmwareDirectory([...files,files[0]]),/重复/);
 assert.throws(()=>context.packageFirmwareDirectory(files.slice(1)),/发布目录/);
 const oversized=file('large','');Object.defineProperty(oversized,'size',{value:200*1024**2});
 assert.throws(()=>context.packageFirmwareDirectory([...files,oversized]),/200 MiB/);
 // Optional round trip of a real extracted release; no script execution.
 if(process.argv[2]){
  const root=process.argv[2],all=[];
  function walk(folder){for(const name of fs.readdirSync(folder)){const p=path.join(folder,name);if(fs.statSync(p).isDirectory())walk(p);else all.push(file(path.relative(root,p),fs.readFileSync(p)));}}
  walk(root);fs.writeFileSync(process.argv[3],Buffer.from(await context.packageFirmwareDirectory(all).arrayBuffer()));
 }
 console.log('firmware directory upload tar round trip PASS');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
