'use strict';
const $=s=>document.querySelector(s);let fileOffset=0,directory='',current='overview',busy=false,last=null,uploading=false,settingsLoaded=false,settingsDirty=false;
function el(tag,text,cls){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e}
function message(text=''){$('#message').textContent=text}
async function api(url,options={}){const r=await fetch(url,options);let value=await r.json();if(!r.ok){throw Error(value.error||'请求失败')}return value}
function post(url,value,password){const headers={'Content-Type':'application/json','X-UGripper-Request':'1'};if(password)headers['X-UGripper-Maintenance-Password']=password;return api(url,{method:'POST',headers,body:JSON.stringify(value)})}
function bytes(n){return (n/1024**3).toFixed(1)+' GiB'}
function metric(label,value,detail){const p=el('article',undefined,'panel metric');p.append(el('p',label,'muted'),el('div',value,'value'),el('p',detail,'muted'));return p}
function row(label,value,good){const d=el('div',undefined,'device');d.append(el('span',label),el('span',value,'state '+(good===null?'':good?'good':'bad')));return d}
function render(s){setDeviceLanguage(s.configuration?.UGRIPPER_LANG);renderProduction(s.production||{});renderTimeSync(s);last=s;busy=s.busy;$('#captureLocal').disabled=busy||captureRunning;$('#captureEgo').disabled=busy||captureRunning;$('#detectCover').disabled=busy||captureRunning;renderTactileStatus(s.tactile||[]);$('#sn').textContent=s.device_sn||'UGRIPPER';$('#mode').textContent=s.recording?'录制中':s.busy?'维护 / 传输中':'空闲';
 const d=s.disks.data,m=s.memory;$('#metrics').replaceChildren(metric('CPU 占用',s.cpu_percent===null?'—':s.cpu_percent+'%','设备系统'),metric('内存使用',Math.round(m.used/m.total*100)+'%',bytes(m.used)+' / '+bytes(m.total)),metric('数据盘剩余',d.mounted?bytes(d.free):'未挂载',d.mounted?'总容量 '+bytes(d.total):'请连接数据盘'),metric('系统盘剩余',bytes(s.disks.system.free),'总容量 '+bytes(s.disks.system.total)));
 const battery=s.battery||{};const bootBlocked=battery.connected!==true||!Number.isInteger(battery.percent)||battery.percent<20||battery.percent>100;const bootButton=$('form[data-kind=rk3588_boot] button');bootButton.disabled=busy||uploading||bootBlocked;$('#bootBatteryStatus').textContent=bootBlocked?(battery.connected===true&&Number.isInteger(battery.percent)&&battery.percent>=0&&battery.percent<20?'背包电量低于 20%，禁止升级 3588 固件':'电量数据不可用或已过期，禁止升级 3588 固件'):'背包电量满足升级条件（至少 20%）';const batteryMetric=metric('背包电量',battery.connected?battery.percent+'%':'电量不可用',battery.connected?(battery.charging?'充电中':battery.discharging?'放电中':'已连接'):'请检查蓝牙电量服务');
 batteryMetric.id='batteryMetric';if(battery.connected&&battery.percent<=10)batteryMetric.classList.add('battery-low');$('#metrics').prepend(batteryMetric);
 for(const side of ['left','right']){const identity=s.firmware?.gripper?.[side];$('#'+side+'DeviceSn').textContent='SN: '+(identity?.connected&&identity.serial?identity.serial:'—');$('#'+side+'Devices').replaceChildren(...s.devices.filter(d=>d.side===side).map(d=>row(d.label,!d.present?'未识别':d.stream===null?'已识别':d.stream?'正常出流':'暂未出流',d.present&&(d.stream===null||d.stream))));}
 const ego=s.ego||{},bound=ego.bound,connected=ego.connected;
 $('#egoStatus').replaceChildren(row('绑定状态',bound===true?'已绑定':bound===false?'未绑定':'未知',bound===true?true:null),row('连接状态',connected===true?'已连接':connected===false?'未连接':bound===false?'未检测（未绑定）':'未知（等待状态更新）',connected===true?true:connected===false?false:null));
 if(ego.bound)$('#egoStatus').append(row('电量',ego.battery?ego.battery.percent+'%'+(ego.battery.charging?' · 充电中':''):'电量不可用',ego.battery?true:null));
 if(ego.bound){const ts=ego.time_sync,ready=ts?.status==='ok';$('#egoStatus').append(row('时间同步',ready?'已与背包同步':ts?.status==='legacy'?'旧版录制前校时':ts?.status==='disconnected'?'授时连接断开':ts?.status==='error'?'授时异常':'正在与背包同步',ready?true:null));const measured=v=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(3)+' ms':'暂无测量';$('#egoStatus').append(row('与背包偏差',measured(ts?.offset_ms),null),row('往返延迟 RTT',measured(ts?.rtt_ms),null));}
 if(ego.serial)$('#egoStatus').append(row('序列号',ego.serial,null));
 if(ego.bound)for(const [key,label]of [['firmware_version','固件版本'],['software_version','软件版本']])$('#egoStatus').append(row(label,(ego[key]||'未读取')+(ego.connected!==true&&ego[key]?'（缓存）':''),null));
 $('#versions').replaceChildren(...Object.entries(s.versions).map(([k,v])=>row(k,v||'未安装',!!v)));
 $('#firmwareVersions').replaceChildren();
 for(const [side,label]of [['left','左'],['right','右']]){
  const camera=s.firmware?.main_camera?.[side],gripper=s.firmware?.gripper?.[side];
  $('#firmwareVersions').append(row(label+'主摄 iSerial',camera?.iserial||'未识别',!!camera?.iserial));
  const stamp=gripper?.checked_at?' · '+new Date(gripper.checked_at*1000).toLocaleString():'';
  $('#firmwareVersions').append(row(label+'夹爪固件',(gripper?.version|| (gripper?.status==='disconnected'?'未连接':gripper?.status==='unavailable'?'版本不可用':gripper?.status==='pending'?'等待自动读取':'状态待更新'))+stamp,!!gripper?.version));
 }
 if((!settingsLoaded||s.task?.kind==='configuration'&&s.task?.finished)&&!settingsDirty){for(const select of document.querySelectorAll('#configurationForm select')){const value=s.configuration?.[select.name];select.value=value??'';select.required=true;}settingsLoaded=true;}
 $('#connection').textContent=(s.service_active?'数采服务在线':'数采服务已停止')+' · '+new Date().toLocaleTimeString();
 if(s.task.id&&!uploading){$('#taskPanel').hidden=false;$('#taskText').textContent=s.task.stage+(s.task.message?' · '+s.task.message:'');if(s.task.status==='running')$('#progress').removeAttribute('value');else $('#progress').value=100;$('#results').replaceChildren();for(const [k,v]of Object.entries(s.task.result||s.task.results||{})){$('#results').append(el('p',k+': '+(typeof v==='object'?(v.installed_version||v.status||'完成'):v)))} }
}
async function refresh(){if($('#workspace').hidden)return;try{render(await api('/api/v1/status'))}catch(e){if(last)last.battery={connected:false};$('form[data-kind=rk3588_boot] button').disabled=true;$('#bootBatteryStatus').textContent='电量数据不可用或已过期，禁止升级 3588 固件';$('#connection').textContent='连接中断';$('#timeSyncFreshness').textContent='连接中断，进度未更新；重新连接后自动恢复';$('#productionFreshness').textContent='连接中断，进度未更新；重新连接后自动恢复';updateTactileImageWarnings([]);message(e.message)}}
for(const b of document.querySelectorAll('nav [data-page]'))b.onclick=()=>{current=b.dataset.page;for(const x of document.querySelectorAll('.page'))x.hidden=x.id!==current;for(const x of document.querySelectorAll('nav [data-page]'))x.classList.toggle('selected',x===b);$('#title').textContent=({overview:'设备概览',data:'录制数据',upgrade:'升级维护',settings:'配置'})[current];if(current==='data')loadFiles()};
async function loadFiles(){try{const v=await api('/api/v1/data?path='+encodeURIComponent(directory)+'&offset='+fileOffset);$('#previousFiles').disabled=fileOffset===0;$('#nextFiles').disabled=!v.has_more;$('#directory').textContent='/'+directory;$('#files').replaceChildren();for(const f of v.entries){const item=el('div',undefined,'file'),actions=el('div',undefined,'actions'),path=directory?directory+'/'+f.name:f.name;item.append(el('span',f.name,'name'));if(f.directory){const open=el('button','打开','secondary');open.onclick=()=>{directory=path;fileOffset=0;loadFiles()};actions.append(open)}const download=el('button',f.directory?'打包下载':'下载','secondary');download.onclick=()=>startDownload('/api/v1/download?path='+encodeURIComponent(path));actions.append(download);item.append(actions);$('#files').append(item)}}catch(e){message(e.message)}}
$('#previousFiles').onclick=()=>{fileOffset=Math.max(0,fileOffset-200);loadFiles()};$('#nextFiles').onclick=()=>{fileOffset+=200;loadFiles()};
$('#parent').onclick=()=>{directory=directory.split('/').slice(0,-1).join('/');fileOffset=0;loadFiles()};
function startDownload(url){if(busy){message('录制或维护进行中，请稍后下载');return}window.location.assign(url);message('下载开始后请等待完成，期间无法录制。')}
$('#logs').onclick=()=>startDownload('/api/v1/logs');
function confirmAction(text,fixture=false,protectedOperation=false){
 $('#confirmText').textContent=text;$('#fixtureLabel').hidden=!fixture;$('#fixture').checked=false;
 $('#maintenancePasswordLabel').hidden=!protectedOperation;$('#maintenancePassword').value='';$('#confirmError').textContent='';
 $('#confirmDialog').showModal();if(protectedOperation)$('#maintenancePassword').focus();
 return new Promise(resolve=>{
  const finish=value=>{$('#maintenancePassword').value='';$('#confirmDialog').close();resolve(value)};
  $('#cancelConfirm').onclick=()=>finish(false);
  $('#acceptConfirm').onclick=()=>{
   if(fixture&&!$('#fixture').checked){$('#confirmError').textContent='请先确认已安装指定标零治具';return}
   const password=$('#maintenancePassword').value;
   if(protectedOperation&&!password){$('#confirmError').textContent='请输入维护密码';$('#maintenancePassword').focus();return}
   finish(protectedOperation?password:true);
  };
  $('#maintenancePassword').onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();$('#acceptConfirm').click()}};
  $('#confirmDialog').oncancel=event=>{event.preventDefault();finish(false)};
 });
}
function upload(file,password){return new Promise((resolve,reject)=>{const x=new XMLHttpRequest();x.open('POST','/api/v1/upload');x.setRequestHeader('X-UGripper-Request','1');x.setRequestHeader('X-UGripper-Maintenance-Password',password);x.upload.onprogress=e=>{$('#progress').value=e.loaded/e.total*100;$('#taskText').textContent='正在上传 '+Math.round(e.loaded/e.total*100)+'%'};x.onload=()=>{try{const v=JSON.parse(x.responseText);x.status===201?resolve(v):reject(Error(v.error||'上传失败'))}catch(e){reject(e)}};x.onerror=()=>reject(Error('上传连接中断'));x.send(file)})}
$('#firmwareKind').onchange=()=>{$('#firmwareFile').accept=$('#firmwareKind').value==='main_camera'?'.img':'.hex';$('#firmwareFile').value=''};
for(const form of document.querySelectorAll('.upgradeForm'))form.onsubmit=async e=>{e.preventDefault();const kind=form.dataset.kind||$('#firmwareKind').value,file=form.querySelector('input[type=file]').files[0];if(!file||file.size>=200*1024**2||!file.size){message('请选择小于 200 MiB 的文件');return}if(busy){message('录制或维护进行中');return}if(kind==='rk3588_boot'&&(last?.battery?.connected!==true||!Number.isInteger(last.battery.percent)||last.battery.percent<20||last.battery.percent>100)){message('背包电量不足或不可用，禁止升级 3588 固件');return}const password=await confirmAction('确认使用 '+file.name+' 执行'+(kind==='software'?'软件覆盖安装':kind==='rk3588_boot'?'3588 启动固件升级，写入后自动重启设备，网页将暂时断开':'左右两侧固件升级')+'？过程中请勿断电或拔插设备。',false,true);if(!password)return;form.querySelector('button').disabled=true;uploading=true;$('#taskPanel').hidden=false;try{const u=await upload(file,password);await post('/api/v1/execute',{kind,upload:u.upload,confirm:true,force:kind==='main_camera'&&$('#forceFlash').checked},password);message();form.reset()}catch(e){message(e.message)}finally{uploading=false;form.querySelector('button').disabled=false;refresh()}};
for(const button of document.querySelectorAll('[data-zero-side]'))button.onclick=async()=>{
 const side=button.dataset.zeroSide,label=side==='left'?'左':'右';
 if(busy){message('录制或维护进行中');return}
 const password=await confirmAction('此操作会覆盖'+label+'编码器零位，错误操作会导致测量偏差。必须使用指定标零治具。确认仅对'+label+'侧执行？',true,true);if(!password)return;
 try{await post('/api/v1/execute',{kind:'zeroing',side,confirm:true,fixture:'FIXTURE_READY'},password);message();refresh()}catch(e){message(e.message)}
};
$('#configurationForm').onchange=()=>{settingsDirty=true};
$('#configurationForm').onsubmit=async e=>{
 e.preventDefault();const form=e.currentTarget;if(busy){message('录制或维护进行中');return}
 const settings=Object.fromEntries(new FormData(form));
 const password=await confirmAction('警告：修改设备参数将影响录制，并重启数采服务。请确认以下设置：\n'+Object.entries(settings).map(([k,v])=>k+' = '+v).join('\n'),false,true);if(!password)return;
 const button=form.querySelector('button');button.disabled=true;
 try{await post('/api/v1/execute',{kind:'configuration',settings,confirm:true},password);message('配置任务已提交，应用结果见任务进度。');settingsDirty=false;}catch(e){message(e.message)}finally{button.disabled=false;refresh()}
};
refresh();setInterval(refresh,3000);

let encoderReading=false,encoderTimer=null,encoderRequest=null;
function stopEncoderReading(text='已停止读取，显示最后一次读数'){
 encoderReading=false;clearTimeout(encoderTimer);encoderRequest?.abort();encoderRequest=null;
 $('#encoderToggle').textContent='开始读取';$('#encoderReadStatus').textContent=text;
}
function renderEncoder(side,value){
 const target=$('#encoder'+(side==='left'?'Left':'Right'));
 if(value.status!=='ok'){target.replaceChildren(el('p',value.message||'未收到有效读数','muted'));return}
 target.replaceChildren(row('原始计数',String(value.position),null),row('角度',value.angle_deg.toFixed(2)+'°',null),row('弧度',value.angle_rad.toFixed(4)+' rad',null),el('p','读取时间 '+new Date(value.read_at*1000).toLocaleTimeString(),'muted'));
}
async function pollEncoder(){
 const request=new AbortController();encoderRequest=request;
 try{
  const response=await fetch('/api/v1/encoder/stream',{method:'POST',headers:{'Content-Type':'application/json','X-UGripper-Request':'1'},body:'{}',signal:request.signal});
  if(!response.ok)throw Error((await response.json()).error);
  const reader=response.body.getReader(),decoder=new TextDecoder();let buffer='';
  while(encoderReading){
   const {value,done}=await reader.read();if(done)break;
   buffer+=decoder.decode(value,{stream:true});const lines=buffer.split('\n');buffer=lines.pop();
   for(const line of lines){if(!line)continue;const sample=JSON.parse(line);if(sample.error)throw Error(sample.error);encoderLatest=sample;}
   if(!encoderFrame)encoderFrame=requestAnimationFrame(drawEncoder);
  }
  if(encoderReading)stopEncoderReading('读取已结束，串口已释放（每次最多 60 秒）');
 }catch(e){if(encoderReading)stopEncoderReading(e.message)}
 finally{if(encoderRequest===request)encoderRequest=null;}
}
let encoderLatest=null,encoderFrame=0;
function drawEncoder(){
 encoderFrame=0;if(!encoderLatest)return;
 for(const side of ['left','right'])renderEncoder(side,encoderLatest[side]);
 if(encoderReading)$('#encoderReadStatus').textContent='100 Hz 目标 / 实测 '+encoderLatest.rate_hz+' Hz · 剩余 '+encoderLatest.remaining_s+' s · 丢失 '+(encoderLatest.dropped?.left||0)+' / '+(encoderLatest.dropped?.right||0);
}
$('#encoderToggle').onclick=()=>{
 if(encoderReading){stopEncoderReading();return}
 if(busy){message('录制或维护进行中，请稍后读取');return}
 encoderReading=true;$('#encoderToggle').textContent='停止读取';$('#encoderReadStatus').textContent='正在读取…';pollEncoder();
};
for(const button of document.querySelectorAll('nav [data-page]'))button.addEventListener('click',()=>{if(button.dataset.page!=='settings'&&encoderReading)stopEncoderReading()});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&encoderReading)stopEncoderReading()});
window.addEventListener('pagehide',()=>{if(encoderReading)stopEncoderReading()});

let captureRunning=false;
async function captureImages(kind){
 if(captureRunning||busy){message('设备忙，请稍后抓图');return}
 captureRunning=true;$('#captureLocal').disabled=true;$('#captureEgo').disabled=true;$('#detectCover').disabled=true;
 $('#tactileLeftImages').replaceChildren();$('#tactileRightImages').replaceChildren();
 if(kind==='tactile')$('#tactileStatus').textContent='正在采集四路触觉并检测盖板…';
 $('#captureImages').replaceChildren();$('#captureStatus').textContent=kind==='ego'?'Ego 正在短录、拉取和截帧…':'正在获取相机帧…';
 try{
  const job=await post('/api/v1/snapshot',{kind});
  const base='/api/v1/snapshot?id='+encodeURIComponent(job.id);
  let result;
  const deadline=Date.now()+330000;
  do{
   if(Date.now()>deadline)throw Error('抓图等待超时，请检查设备');
   await new Promise(resolve=>setTimeout(resolve,1000));
   result=await api(base);
  }while(result.status==='running');
  $('#captureStatus').textContent=result.error||({ok:'获取完成',partial:'部分相机获取失败',failed:'获取失败'}[result.status]||result.status);
  if(kind==='tactile')$('#tactileStatus').textContent=result.error||'本次检测完成；请查看各传感器结论';
  for(const camera of result.cameras||[]){
   if(camera.camera.includes('_tcam_')){renderTactileImage(camera,base,job.id);continue;}
   const card=el('article',undefined,'panel');card.append(el('h3',camera.camera));
   if(camera.image){
    const link=el('a');link.href=base+'&file='+encodeURIComponent(camera.image);link.target='_blank';link.rel='noopener';
    const img=el('img');img.src=link.href;img.alt=camera.camera;img.className='capture-image';link.append(img);card.append(link);
   }
   if(camera.error)card.append(el('p',camera.error,'bad'));
   $('#captureImages').append(card);
  }
 }catch(e){$('#captureStatus').textContent=e.message;if(kind==='tactile')$('#tactileStatus').textContent=e.message}
 finally{captureRunning=false;$('#captureLocal').disabled=busy;$('#captureEgo').disabled=busy;$('#detectCover').disabled=busy;}
}
$('#captureLocal').onclick=()=>captureImages('local');
$('#captureEgo').onclick=()=>captureImages('ego');

function tactileLabel(name){const parts=name.split('_');return (parts[0]==='left'?'左夹爪':'右夹爪')+' · '+(parts[2]==='l'?'左':'右')+'触觉传感器（'+name+'）'}
function updateTactileImageWarnings(states){
 for(const badge of document.querySelectorAll('[data-tactile-warning]')){
  const state=states.find(c=>c.camera===badge.dataset.tactileWarning);
  const changed=state&&badge.dataset.sensorSerial&&state.serial!==badge.dataset.sensorSerial;
  const known=state&&!state.error&&!changed&&typeof state.warning==='boolean';
  badge.className='tactile-warning '+(known?(state.warning?'alarm':'clear'):'unknown');
  badge.textContent=changed?'传感器已更换，请重新取图':!known?'状态未知 / 未连接':state.warning?'盖板损坏告警':'无损坏告警';
 }
}
function renderTactileStatus(states){
 updateTactileImageWarnings(states);
 for(const side of ['left','right']){
  const target=$('#tactile'+(side==='left'?'Left':'Right')+'Status');
  target.replaceChildren(...states.filter(c=>c.side===side).map(c=>row(tactileLabel(c.camera),c.error?'未连接 / 序列号未知':c.warning?'黄灯：触觉损坏告警':'无损坏告警',c.error?null:!c.warning)));
 }
 for(const button of document.querySelectorAll('[data-baseline-reset]'))button.disabled=busy||captureRunning;
}
function appendCaptureImage(card,base,file,label){
 const link=el('a');link.href=base+'&file='+encodeURIComponent(file);link.target='_blank';link.rel='noopener';
 const img=el('img');img.src=link.href;img.alt=label;img.className='capture-image';link.append(img);card.append(link);
}
function renderTactileImage(camera,base,job){
 const card=el('article',undefined,'panel');
 const heading=el('div',undefined,'tactile-image-heading');
 const badge=el('span','状态未知 / 未连接','tactile-warning unknown');
 badge.dataset.tactileWarning=camera.camera;badge.dataset.sensorSerial=camera.serial||'';badge.setAttribute('role','status');
 heading.append(el('h4',tactileLabel(camera.camera)),badge);card.append(heading);
 card.append(el('p','当前告警状态（连续检测历史）','muted'));
 if(camera.serial)card.append(el('p','传感器 SN：'+camera.serial,'muted'));
 if(camera.image)appendCaptureImage(card,base,camera.image,'当前图');
 if(camera.error)card.append(el('p',camera.error,'bad'));
 if(camera.comparisons)card.append(el('h4','本次盖板损坏检测结果'));
 for(const comparison of camera.comparisons||[]){
  const label=comparison.scope==='reference'?'短期基线':'长期基线';
  const status={clean:'正常',damaged:'疑似盖板损坏',baseline_mismatch:'基线不匹配，请人工检查',missing:'尚未建立基线'}[comparison.status]||comparison.status;
  card.append(el('p',label+'：'+status,comparison.status==='damaged'?'bad':'muted'));
  if(comparison.image)appendCaptureImage(card,base,comparison.image,label+'检测状态图');
 }
 if(camera.status==='ok'&&camera.serial&&camera.comparisons){
  for(const [scope,label]of [['reference','重置短期基线'],['persistent','重置长期基线'],['both','重置长短期基线']]){
   const button=el('button',label,'secondary');button.dataset.baselineReset='1';button.onclick=async()=>{
    if(busy||captureRunning){message('设备忙，请稍后操作');return}
    if(!await confirmAction(tactileLabel(camera.camera)+'：'+label+'。请确认上方当前图中的盖板完好、无接触且无遮挡。此操作将使用该图替换基线，并清除对应检测历史与告警。'))return;
    captureRunning=true;button.disabled=true;
    try{await post('/api/v1/tactile/reset',{id:job,camera:camera.camera,serial:camera.serial,scope,confirm:true});$('#tactileStatus').textContent=label+'已完成，请重新运行盖板损坏检测查看更新结果';}
    catch(e){$('#tactileStatus').textContent=e.message}
    finally{captureRunning=false;refresh()}
   };card.append(button);
  }
 }
 $('#tactile'+(camera.camera.startsWith('left_')?'Left':'Right')+'Images').append(card);
 updateTactileImageWarnings(last?.tactile||[]);
}
$('#detectCover').onclick=()=>captureImages('tactile');

let productionId='';
function productionLabel(value){return ({passed:'通过',failed:'不通过',cancelled:'已取消',interrupted:'异常中断',running:'执行中',starting:'启动中',recording:'录制中',finalizing:'收尾中',pause:'停止间隔',finished:'已结束',success:'成功'})[value]||value||'';}
function renderProduction(job){
 productionId=job.id||'';const running=job.status==='running';
 $('#productionStart').disabled=running;$('#productionCancel').disabled=!running;
 const rounds=job.rounds||[],plan=job.plan||[['A',120,20,51],['B',300,20,11]];
 const total=plan.reduce((sum,p)=>sum+p[3],0),completed=rounds.filter(r=>['success','failed'].includes(r.status)).length;
 $('#productionProgressPanel').hidden=!job.id;
 $('#productionProgress').max=total;$('#productionProgress').value=completed;
 $('#productionProgressLabel').textContent='已完成轮数：'+completed+' / '+total+' · '+Math.floor(completed/total*100)+'%';
 const count=plan.find(p=>p[0]===job.group)?.[3]||0;
 const seconds=Math.max(0,Math.ceil((job.phase_deadline||0)-Date.now()/1000));
 $('#productionPhase').textContent=job.group ? '当前组：'+job.group+' · 当前轮：'+job.round+' / '+count+' · '+productionLabel(job.phase)+(running?' · 阶段剩余约 '+seconds+' s':'') : productionLabel(job.phase);
 $('#productionFreshness').textContent='进度更新时间：'+new Date().toLocaleTimeString();

 $('#productionSummary').textContent=job.id ? '状态：'+productionLabel(job.status)+' · '+(job.group||'')+' '+(job.round||0)+' · '+productionLabel(job.phase)+' · 剩余约 '+Math.ceil((job.remaining_s||0)/60)+' min · A 失败 '+(job.failures?.A||0)+' / B 失败 '+(job.failures?.B||0)+' / 总失败 '+((job.failures?.A||0)+(job.failures?.B||0))+(job.reason?' · '+job.reason:'') : '尚未运行';
 $('#productionDetails').replaceChildren(...(job.rounds||[]).map(r=>{
  const item=el('div');item.append(el('p',r.group+' #'+r.round+' · '+productionLabel(r.status)+' · '+(r.actual_s??r.planned_s)+' s · '+(r.episode||'')+(r.reason?' · '+r.reason:'')));
  if(r.log){const log=el('pre',r.log);log.dataset.noI18n='1';item.append(log);}return item;
 }));
}
$('#productionStart').onclick=async()=>{
 if(!await confirmAction('启动生产压测？A 组 51 轮，B 组 11 轮，约 2 小时 57 分 40 秒，另加启动和收尾时间。关闭网页或拔网线仍继续运行。'))return;
 try{await post('/api/v1/production/start',{confirm:true});refresh()}catch(e){message(e.message)}
};
$('#productionCancel').onclick=async()=>{
 if(!await confirmAction('取消生产压测并停止当前录制？结果将标记为已取消。'))return;
 try{await post('/api/v1/production/cancel',{id:productionId});message('正在取消，等待录制收尾');refresh()}catch(e){message(e.message)}
};

let timeSyncId='';
const timeSyncRows=new Map();
function renderTimeSync(s){
 const job=s.time_sync||{}, running=job.status==='running', total=job.settings?.count||1;
 if((job.id||'')!==timeSyncId){timeSyncRows.clear();$('#timeSyncDetails').replaceChildren();}
 if(job.id&&job.id!==timeSyncId&&job.settings){for(const [key,value] of Object.entries(job.settings)){const input=$('#timeSyncForm').elements.namedItem(key);if(input)input.value=value;}}
 timeSyncId=job.id||'';
 $('#timeSyncStart').disabled=!!s.busy||running;
 $('#timeSyncCancel').disabled=!running;
 $('#productionStart').disabled=!!s.busy||s.production?.status==='running';
 for(const input of $('#timeSyncForm').querySelectorAll('input'))input.disabled=running;
 const config=running?job.configuration:s.configuration;
 $('#timeSyncConfiguration').textContent='视频编码：'+(config?.CAMERA_CODEC||'—')+' · 电源频率：'+(config?.UGRIPPER_POWER_LINE_FREQUENCY||'—')+' Hz';
 const completed=(job.rounds||[]).filter(r=>r.status!=='running').length;
 $('#timeSyncProgress').hidden=!job.id;$('#timeSyncProgress').max=total;$('#timeSyncProgress').value=completed;
 const labels={completed:'采集完成',resetting:'毫秒表清零中',delay:'等待毫秒表启动',cancelled:'已取消'};
 const label=v=>labels[v]||productionLabel(v);
 $('#timeSyncStatus').textContent=job.id?'状态：'+label(job.status)+' · '+(job.round||0)+' / '+total+' · '+label(job.phase)+(running?' · 阶段剩余约 '+Math.ceil(job.phase_remaining_s||0)+' s':'')+(job.reason?' · '+job.reason:''):'尚未运行';
 $('#timeSyncFreshness').textContent=job.id?'进度更新时间：'+new Date().toLocaleTimeString():'';
 const activeRounds=new Set();
 for(const r of job.rounds||[]){
  const key=String(r.round);activeRounds.add(key);
  let row=timeSyncRows.get(key);
  if(!row){
   const item=el('div'),summary=el('p'),details=el('details'),log=el('pre');
   log.dataset.noI18n='1';details.append(el('summary','日志'),log);item.append(summary,details);
   row={item,summary,details,log};timeSyncRows.set(key,row);$('#timeSyncDetails').append(item);
  }
  const summary='#'+r.round+' · '+label(r.status)+' · '+(r.episode||'')+(r.reason?' · '+r.reason:'');
  if(row.summaryText!==summary){row.summary.textContent=summary;row.summaryText=summary;}
  const log=r.log||'';
  if(row.log.textContent!==log){const top=row.log.scrollTop,left=row.log.scrollLeft;row.log.textContent=log;row.log.scrollTop=top;row.log.scrollLeft=left;}
  row.details.hidden=!log;
 }
 for(const [key,row] of timeSyncRows){if(!activeRounds.has(key)){row.item.remove();timeSyncRows.delete(key);}}
}
$('#timeSyncForm').onsubmit=async event=>{
 event.preventDefault();
 const settings=Object.fromEntries([...new FormData(event.target)].map(([k,v])=>[k,Number(v)]));
 if(settings.delay_s>=settings.duration_s){message('毫秒表延迟必须小于录制时长');return;}
 $('#timeSyncStart').disabled=true;
 try{await post('/api/v1/time-sync/start',settings);await refresh()}catch(e){message(e.message);$('#timeSyncStart').disabled=false;}
};
$('#timeSyncCancel').onclick=async()=>{
 $('#timeSyncCancel').disabled=true;
 try{await post('/api/v1/time-sync/cancel',{id:timeSyncId});message('正在取消，等待录制收尾');await refresh()}catch(e){message(e.message);$('#timeSyncCancel').disabled=false;}
};
