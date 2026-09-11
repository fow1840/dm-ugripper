'use strict';
const $ = id => document.getElementById(id);
const levels = ['INFO','WARNING','ERROR','DEBUG','TRACE','UNKNOWN'];
let rows = [], filtered = [], page = 0, busy = false;
const size = 200;
function el(tag, text, cls) { const node = document.createElement(tag); if (text !== undefined) node.textContent = text; if (cls) node.className = cls; return node; }
function time(r) { return Number.isFinite(r.time) ? new Date(r.time).toLocaleString('sv-SE') + '.' + String(Math.floor(r.time % 1000)).padStart(3,'0') : '时间未知'; }
for (const level of levels) { const label = el('label'); const input = el('input'); input.type='checkbox'; input.value=level; input.checked=true; input.addEventListener('change', apply); label.append(input, document.createTextNode(level)); $('levels').append(label); }
function context(r) {
  const index = rows.indexOf(r);
  const nearby = rows.slice(Math.max(0,index-5), index+6).filter(x=>x.file===r.file);
  $('contextText').textContent = nearby.map(x=>`${x===r?'▶':' '} ${x.file}:${x.line}\n${x.raw}`).join('\n');
  $('context').showModal();
}
function aggregate(id, entries) {
  $(id).replaceChildren();
  for (const [key,n] of entries) { const item=el('div',undefined,'aggregate'); item.append(el('span',key),el('b',String(n))); $(id).append(item); }
  if (!entries.length) $(id).append(el('div','暂无记录','empty'));
}
function overview() {
  const s=LogParser.summarize(rows);
  $('stats').replaceChildren();
  for (const [label,n] of [['日志总行数',rows.length],['INFO',s.levels.INFO||0],['WARNING',s.levels.WARNING||0],['ERROR / FATAL',s.levels.ERROR||0]]) { const card=el('div',undefined,'card'); card.append(el('span',label),el('b',n.toLocaleString())); $('stats').append(card); }
  $('restoreStats').replaceChildren();
  for(const name of ['请求','执行','命令完成','恢复成功','命令失败','重试耗尽','等待','等待取消','超时']) { const badge=el('div',name,'badge'); badge.append(el('b',String(s.events[name]||0))); $('restoreStats').append(badge); }
  $('timeline').replaceChildren();
  const events=rows.filter(r=>r.event);
  for(const r of events.slice(-1000)) {const b=el('button',`${time(r)} · ${r.event} · ${r.file}:${r.line} · ${r.message}`); b.onclick=()=>context(r); $('timeline').append(b);}
  if(events.length>1000) $('timeline').prepend(el('p','显示最后 1000 条事件；使用“仅 restore”可浏览和导出完整记录。'));
  const ranked=o=>Object.entries(o).sort((a,b)=>b[1]-a[1]);
  aggregate('codes',ranked(s.codes)); aggregate('repeats',ranked(s.repeats).filter(x=>x[1]>1).slice(0,10));
  const oldCode=$('code').value;
  $('code').replaceChildren(new Option('全部错误码',''));
  for(const [code,n] of Object.entries(s.codes).sort((a,b)=>a[0].localeCompare(b[0]))) $('code').append(new Option(code+' ('+n+')',code));
  $('code').value=Object.hasOwn(s.codes,oldCode)?oldCode:'';
}
function apply() {
  const selected=new Set([...$('levels').querySelectorAll('input:checked')].map(x=>x.value));
  const q=$('search').value.toLowerCase(), mode=$('restore').value, code=$('code').value;
  const from=$('from').value ? new Date($('from').value).getTime() : -Infinity;
  const to=$('to').value ? new Date($('to').value).getTime() : Infinity;
  filtered=rows.filter(r=>selected.has(r.level)&&(!q||r.raw.toLowerCase().includes(q))&&(!code||r.code===code)&&(mode==='all'||(mode==='only'?r.restore:!r.restore))&&((from===-Infinity&&to===Infinity)||(Number.isFinite(r.time)&&r.time>=from&&r.time<=to)));
  page=0; render();
}
function codeAnnotation(r) {
  if(!r.code)return null;
  const reason=ErrorCodeReference[r.code] || '检测到异常，暂时没有对应的中文说明';
  return el('div',reason,'code-reason');
}
function render() {
  $('rows').replaceChildren();
  for(const r of filtered.slice(page*size,(page+1)*size)) {
    const tr=el('tr'); const stamp=el('td',time(r)); stamp.append(el('div',`${r.file}:${r.line}`));
    const level=el('td');level.append(el('span',r.level,`level ${r.level}`));
    const msg=el('td',r.message);const annotation=codeAnnotation(r);if(annotation)msg.append(annotation);tr.append(stamp,level,msg);tr.onclick=()=>context(r);tr.tabIndex=0;tr.onkeydown=e=>{if(e.key==='Enter')context(r);};$('rows').append(tr);
  }
  if(!filtered.length) {const tr=el('tr');const td=el('td','没有匹配日志','empty');td.colSpan=3;tr.append(td);$('rows').append(tr);}
  $('count').textContent=`${filtered.length.toLocaleString()} / ${rows.length.toLocaleString()} 行`;
  $('page').textContent=`${page+1} / ${Math.max(1,Math.ceil(filtered.length/size))} 页`;
  $('prev').disabled=page===0; $('next').disabled=(page+1)*size>=filtered.length;
  $('export').disabled=busy||!filtered.length;
}
async function importFiles(files) {
  if(busy || !files.length)return;
  if(files.length!==1) {
    $('status').textContent='一次只能打开一份日志，请重新选择。当前日志保持不变。';
    $('files').value='';
    return;
  }
  busy=true; $('files').disabled=true; $('clear').disabled=true; $('export').disabled=true;
  const file=files[0], nextRows=[];
  try {
    const reader=file.stream().pipeThrough(new TextDecoderStream()).getReader();
    let pending='',line=0;
    try {
      while(true) {
        const chunk=await reader.read();if(chunk.done)break;pending+=chunk.value;
        const parts=pending.split('\n');pending=parts.pop();
        for(const raw of parts) nextRows.push(LogParser.parse(raw.replace(/\r$/,''),file.name,++line));
        $('status').textContent='正在读取 '+file.name+' · '+line.toLocaleString()+' 行';
        await new Promise(resolve=>setTimeout(resolve,0));
      }
      if(pending)nextRows.push(LogParser.parse(pending.replace(/\r$/,''),file.name,++line));
    } finally { reader.releaseLock(); }
    rows=nextRows;
    $('context').close(); $('contextText').textContent='';
    overview(); $('reset').click();
    $('status').textContent='当前文件：'+file.name+' · '+rows.length.toLocaleString()+' 行。点击日志查看上下文。';
  } catch(error) {
    $('status').textContent='读取 '+file.name+' 失败：'+error.message+'。当前日志保持不变。';
  } finally {busy=false;$('files').disabled=false;$('clear').disabled=false;$('files').value='';render();}
}
$('files').onchange=e=>importFiles([...e.target.files]);
$('drop').onclick=()=>$('files').click();$('drop').onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();$('files').click();}};
for(const name of ['dragenter','dragover']) $('drop').addEventListener(name,e=>{e.preventDefault();$('drop').classList.add('drag');});
$('drop').ondragleave=()=>$('drop').classList.remove('drag');$('drop').ondrop=e=>{e.preventDefault();$('drop').classList.remove('drag');importFiles([...e.dataTransfer.files]);};
for(const id of ['restore','code','from','to']) $(id).onchange=apply;
let debounce;$('search').oninput=()=>{clearTimeout(debounce);debounce=setTimeout(apply,180);};
$('reset').onclick=()=>{for(const id of ['search','from','to','code'])$(id).value='';$('restore').value='all';for(const input of $('levels').querySelectorAll('input'))input.checked=true;apply();};
$('clear').onclick=()=>{rows=[];overview();apply();$('status').textContent='尚未导入日志';};
$('prev').onclick=()=>{page--;render();};$('next').onclick=()=>{page++;render();};$('close').onclick=()=>$('context').close();
$('export').onclick=()=>{const blob=new Blob([filtered.map(r=>r.raw).join('\n')+'\n'],{type:'text/plain;charset=utf-8'});const url=URL.createObjectURL(blob);const a=el('a');a.href=url;a.download='ugripper-filtered.log';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
overview();apply();
