'use strict';
const $=s=>document.querySelector(s);let fileOffset=0,directory='',current='overview',busy=false,last=null,uploading=false,settingsLoaded=false,settingsDirty=false;
function el(tag,text,cls){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e}
function message(text=''){$('#message').textContent=text}
async function api(url,options={}){const r=await fetch(url,options);let value=await r.json();if(!r.ok){throw Error(value.error||'请求失败')}return value}
function post(url,value){return api(url,{method:'POST',headers:{'Content-Type':'application/json','X-UGripper-Request':'1'},body:JSON.stringify(value)})}
function bytes(n){return (n/1024**3).toFixed(1)+' GiB'}
function metric(label,value,detail){const p=el('article',undefined,'panel metric');p.append(el('p',label,'muted'),el('div',value,'value'),el('p',detail,'muted'));return p}
function row(label,value,good){const d=el('div',undefined,'device');d.append(el('span',label),el('span',value,'state '+(good?'good':'bad')));return d}
function render(s){last=s;busy=s.busy;$('#sn').textContent=s.device_sn||'UGRIPPER';$('#mode').textContent=s.recording?'录制中':s.busy?'维护 / 传输中':'空闲';
 const d=s.disks.data,m=s.memory;$('#metrics').replaceChildren(metric('CPU 占用',s.cpu_percent===null?'—':s.cpu_percent+'%','设备系统'),metric('内存使用',Math.round(m.used/m.total*100)+'%',bytes(m.used)+' / '+bytes(m.total)),metric('数据盘剩余',d.mounted?bytes(d.free):'未挂载',d.mounted?'总容量 '+bytes(d.total):'请连接数据盘'),metric('系统盘剩余',bytes(s.disks.system.free),'总容量 '+bytes(s.disks.system.total)));
 for(const side of ['left','right']){$('#'+side+'Devices').replaceChildren(...s.devices.filter(d=>d.side===side).map(d=>row(d.label,!d.present?'未识别':d.stream===null?'已识别':d.stream?'正常出流':'暂未出流',d.present&&(d.stream===null||d.stream))));}
 $('#versions').replaceChildren(...Object.entries(s.versions).map(([k,v])=>row(k,v||'未安装',!!v)));
 $('#firmwareVersions').replaceChildren();
 for(const [side,label]of [['left','左'],['right','右']]){
  const camera=s.firmware?.main_camera?.[side],gripper=s.firmware?.gripper?.[side];
  $('#firmwareVersions').append(row(label+'主摄 iSerial',camera?.iserial||'未识别',!!camera?.iserial));
  const stamp=gripper?.checked_at?' · '+new Date(gripper.checked_at*1000).toLocaleString():'';
  $('#firmwareVersions').append(row(label+'夹爪固件',(gripper?.version|| (gripper?.checked_at?'未读取到版本':'尚未读取'))+stamp,!!gripper?.version));
 }
 if((!settingsLoaded||s.task?.kind==='configuration'&&s.task?.finished)&&!settingsDirty){for(const select of document.querySelectorAll('#configurationForm select')){const value=s.configuration?.[select.name];select.value=value??'';select.required=true;}settingsLoaded=true;}
 $('#connection').textContent=(s.service_active?'数采服务在线':'数采服务已停止')+' · '+new Date().toLocaleTimeString();
 if(s.task.id&&!uploading){$('#taskPanel').hidden=false;$('#taskText').textContent=s.task.stage+(s.task.message?' · '+s.task.message:'');if(s.task.status==='running')$('#progress').removeAttribute('value');else $('#progress').value=100;$('#results').replaceChildren();for(const [k,v]of Object.entries(s.task.result||s.task.results||{})){$('#results').append(el('p',k+': '+(typeof v==='object'?(v.installed_version||v.status||'完成'):v)))} }
}
async function refresh(){if($('#workspace').hidden)return;try{render(await api('/api/v1/status'))}catch(e){$('#connection').textContent='连接中断';message(e.message)}}
for(const b of document.querySelectorAll('nav [data-page]'))b.onclick=()=>{current=b.dataset.page;for(const x of document.querySelectorAll('.page'))x.hidden=x.id!==current;for(const x of document.querySelectorAll('nav [data-page]'))x.classList.toggle('selected',x===b);$('#title').textContent=b.textContent;if(current==='data')loadFiles()};
async function loadFiles(){try{const v=await api('/api/v1/data?path='+encodeURIComponent(directory)+'&offset='+fileOffset);$('#previousFiles').disabled=fileOffset===0;$('#nextFiles').disabled=!v.has_more;$('#directory').textContent='/'+directory;$('#files').replaceChildren();for(const f of v.entries){const item=el('div',undefined,'file'),actions=el('div',undefined,'actions'),path=directory?directory+'/'+f.name:f.name;item.append(el('span',f.name,'name'));if(f.directory){const open=el('button','打开','secondary');open.onclick=()=>{directory=path;fileOffset=0;loadFiles()};actions.append(open)}const download=el('button',f.directory?'打包下载':'下载','secondary');download.onclick=()=>startDownload('/api/v1/download?path='+encodeURIComponent(path));actions.append(download);item.append(actions);$('#files').append(item)}}catch(e){message(e.message)}}
$('#previousFiles').onclick=()=>{fileOffset=Math.max(0,fileOffset-200);loadFiles()};$('#nextFiles').onclick=()=>{fileOffset+=200;loadFiles()};
$('#parent').onclick=()=>{directory=directory.split('/').slice(0,-1).join('/');fileOffset=0;loadFiles()};
function startDownload(url){if(busy){message('录制或维护进行中，请稍后下载');return}window.location.assign(url);message('下载开始后请等待完成，期间无法录制。')}
$('#logs').onclick=()=>startDownload('/api/v1/logs');
function confirmAction(text,fixture=false){$('#confirmText').textContent=text;$('#fixtureLabel').hidden=!fixture;$('#fixture').checked=false;$('#confirmDialog').showModal();return new Promise(resolve=>{$('#cancelConfirm').onclick=()=>{$('#confirmDialog').close();resolve(false)};$('#acceptConfirm').onclick=()=>{if(fixture&&!$('#fixture').checked)return;$('#confirmDialog').close();resolve(true)};$('#confirmDialog').oncancel=()=>resolve(false)})}
function upload(file){return new Promise((resolve,reject)=>{const x=new XMLHttpRequest();x.open('POST','/api/v1/upload');x.setRequestHeader('X-UGripper-Request','1');x.upload.onprogress=e=>{$('#progress').value=e.loaded/e.total*100;$('#taskText').textContent='正在上传 '+Math.round(e.loaded/e.total*100)+'%'};x.onload=()=>{try{const v=JSON.parse(x.responseText);x.status===201?resolve(v):reject(Error(v.error||'上传失败'))}catch(e){reject(e)}};x.onerror=()=>reject(Error('上传连接中断'));x.send(file)})}
$('#firmwareKind').onchange=()=>{$('#firmwareFile').accept=$('#firmwareKind').value==='main_camera'?'.img':'.hex';$('#firmwareFile').value=''};
for(const form of document.querySelectorAll('.upgradeForm'))form.onsubmit=async e=>{e.preventDefault();const kind=form.dataset.kind||$('#firmwareKind').value,file=form.querySelector('input[type=file]').files[0];if(!file||file.size>=200*1024**2||!file.size){message('请选择小于 200 MiB 的文件');return}if(busy){message('录制或维护进行中');return}if(!await confirmAction('确认使用 '+file.name+' 执行'+(kind==='software'?'软件覆盖安装':'左右两侧固件升级')+'？过程中请勿断电或拔插设备。'))return;form.querySelector('button').disabled=true;uploading=true;$('#taskPanel').hidden=false;try{const u=await upload(file);await post('/api/v1/execute',{kind,upload:u.upload,confirm:true,force:kind==='main_camera'&&$('#forceFlash').checked});message();form.reset()}catch(e){message(e.message)}finally{uploading=false;form.querySelector('button').disabled=false;refresh()}};
for(const button of document.querySelectorAll('[data-zero-side]'))button.onclick=async()=>{
 const side=button.dataset.zeroSide,label=side==='left'?'左':'右';
 if(busy){message('录制或维护进行中');return}
 if(!await confirmAction('此操作会覆盖'+label+'编码器零位，错误操作会导致测量偏差。必须使用指定标零治具。确认仅对'+label+'侧执行？',true))return;
 try{await post('/api/v1/execute',{kind:'zeroing',side,confirm:true,fixture:'FIXTURE_READY'});message();refresh()}catch(e){message(e.message)}
};
$('#readVersions').onclick=async()=>{
 if(busy){message('录制或维护进行中');return}
 if(!await confirmAction('读取左右夹爪固件版本将短暂停止数采，完成后自动恢复。确认读取？'))return;
 try{await post('/api/v1/execute',{kind:'read_versions',confirm:true});message();refresh()}catch(e){message(e.message)}
};
$('#configurationForm').onchange=()=>{settingsDirty=true};
$('#configurationForm').onsubmit=async e=>{
 e.preventDefault();if(busy){message('录制或维护进行中');return}
 const settings=Object.fromEntries(new FormData(e.currentTarget));
 if(!await confirmAction('警告：修改设备参数将影响录制，并重启数采服务。请确认以下设置：\n'+Object.entries(settings).map(([k,v])=>k+' = '+v).join('\n')))return;
 const button=e.currentTarget.querySelector('button');button.disabled=true;
 try{await post('/api/v1/execute',{kind:'configuration',settings,confirm:true});message('配置任务已提交，应用结果见任务进度。');settingsDirty=false;}catch(e){message(e.message)}finally{button.disabled=false;refresh()}
};
refresh();setInterval(refresh,3000);
