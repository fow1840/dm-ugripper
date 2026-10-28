const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8').split('let inspectionJob =')[1];
function setup(options={}){
 const nodes={},requests=[],timers=new Map(),timeouts=new Map();let next=0;
 const create=(tag,text='',className='')=>({tag,textContent:text,className,children:[],value:'both',
  append(...items){this.children.push(...items)},replaceChildren(...items){this.children=items},setAttribute(key,value){this[key]=value}});
 const element=id=>nodes[id]||(nodes[id]=create('div'));
 const context={last:null,el:create,$:element,AbortController,api:options.api,setTimeout:(fn,ms)=>{timeouts.set(++next,{fn,ms});return next},clearTimeout:id=>timeouts.delete(id),document:{querySelectorAll:()=>[]},window:{addEventListener(){}},
  setInterval:f=>{timers.set(++next,f);return next},clearInterval:id=>timers.delete(id),
  post:async(url,body)=>{requests.push({url,body});return options.post?.(url,body)},refresh:async()=>{},message:()=>{}};
 vm.runInNewContext('let inspectionJob ='+source,context);
 return {nodes,requests,context,timeouts,poll:async()=>{const [id,t]=[...timeouts].find(([,t])=>t.ms===200);timeouts.delete(id);await t.fn()},render:state=>{context.last=state;context.renderInspection(state)},tick:()=>[...timers.values()].forEach(f=>f()),timers};
}
test('trajectory submits once only after five UI ticks',async()=>{
 const s=setup();s.nodes['#inspectionTrajectoryStart'].onclick();
 for(let i=0;i<4;i++)s.tick();
 assert.equal(s.requests.length,0);
 s.tick();await Promise.resolve();
 assert.equal(s.requests.length,1);
 assert.equal(s.requests[0].url,'/api/v1/inspection/trajectory/start');
 s.tick();assert.equal(s.requests.length,1);
});
test('cancel countdown never sends START',()=>{
 const s=setup();s.nodes['#inspectionTrajectoryStart'].onclick();s.tick();
 s.nodes['#inspectionCountdownCancel'].onclick();
 for(let i=0;i<6;i++)s.tick();
 assert.equal(s.requests.length,0);assert.equal(s.timers.size,0);
});

test('encoder submits both-hand check without side selector',async()=>{
 const s=setup();s.nodes['#inspectionEncoderStart'].onclick();await Promise.resolve();
 assert.equal(s.requests.length,1);
 assert.equal(s.requests[0].url,'/api/v1/inspection/encoder/start');
 assert.equal(JSON.stringify(s.requests[0].body),'{}');
});

function state(job,busy=false){return {inspection:job,busy,recording:{active:false}}}
function running(extra={}){return {id:'a'.repeat(32),kind:'encoder',status:'running',updated_at_ms:1,started_at_ms:1,sides:{},...extra}}
test('cancel is enabled while inspection holds device busy and sends current ID',async()=>{
 const s=setup();s.render(state(running(),true));
 assert.equal(s.nodes['#inspectionEncoderCancel'].disabled,false);
 await s.nodes['#inspectionEncoderCancel'].onclick();
 assert.equal(s.requests[0].url,'/api/v1/inspection/encoder/cancel');
 assert.equal(s.requests[0].body.id,'a'.repeat(32));
 assert.equal(s.nodes['#inspectionEncoderCancel'].textContent,'正在取消…');
 assert.equal(s.nodes['#inspectionEncoderCancel'].disabled,true);
 s.render(state(running({status:'cancelled',updated_at_ms:2})));
 assert.equal(s.nodes['#inspectionEncoderCancel'].textContent,'取消检查');
});
test('accepted start enables cancel before status refresh and ignores old snapshot',async()=>{
 const id='b'.repeat(32),s=setup({post:()=>({id})});
 s.render(state(running({status:'completed'})));
 await s.nodes['#inspectionEncoderStart'].onclick();
 assert.equal(s.nodes['#inspectionEncoderCancel'].disabled,false);
 await s.nodes['#inspectionEncoderCancel'].onclick();
 assert.equal(s.requests[1].body.id,id);
});
test('failed cancel shows inline error and can be retried',async()=>{
 const s=setup({post:()=>{throw Error('连接失败')}});s.render(state(running()));
 await s.nodes['#inspectionEncoderCancel'].onclick();
 assert.equal(s.nodes['#inspectionEncoderCancel'].disabled,false);
 assert.equal(s.nodes['#inspectionActionStatus'].textContent,'连接失败');
});
test('fixed five-round grid retains nodes and expanded details on refresh',()=>{
 const s=setup();const job=running({sides:{left:{value:-41,completed_cycles:1,checks:[{cycle:1,endpoint:'closed',value:-41,status:'pass'},{cycle:1,endpoint:'open',value:-4300,status:'fail',code:'open_above_range'}]}}});
 s.render(state(job));
 const box=s.nodes['#inspectionEncoderResult'],grid=box.children[1],hand=grid.children[0];
 const table=hand.children[4],rows=table.children[1].children;
 assert.equal(rows.length,5);
 assert.equal(rows[0].children[1].children[1].className,'inspection-badge pass');
 assert.equal(rows[0].children[2].children[1].className,'inspection-badge fail');
 const detail=hand.children[5].children[0].children[3];detail.open=true;
 s.render(state({...job,updated_at_ms:2}));
 assert.equal(box.children[1],grid);assert.equal(detail.open,true);
});
test('interrupted job restores start and shows interruption after restart',()=>{
 const s=setup();s.render(state(running()));
 s.render(state(running({status:'interrupted',result:'incomplete',code:'service_interrupted',updated_at_ms:2})));
 assert.equal(s.nodes['#inspectionEncoderStart'].disabled,false);
 assert.equal(s.nodes['#inspectionEncoderCancel'].disabled,true);
 assert.equal(s.nodes['#inspectionEncoderResult'].children[0].children[0].textContent,'已中断 · 请重新开始');
});

test('inspection readings update from task API independently of full-page refresh',async()=>{
 let calls=0;
 const s=setup({api:async path=>{calls++;assert.equal(path,'/api/v1/tasks/inspection/'+'a'.repeat(32));return running({updated_at_ms:2,sides:{left:{value:-123,checks:[]}}})}});
 s.render(state(running({sides:{left:{value:-4000,checks:[]}}})));
 const grid=s.nodes['#inspectionEncoderResult'].children[1];
 assert.equal([...s.timeouts.values()][0].ms,200);
 await s.poll();
 assert.equal(calls,1);
 assert.equal(grid.children[0].children[1].children[1].textContent,'-123');
 assert.equal(s.nodes['#inspectionEncoderResult'].children[1],grid);
 s.render(state(running({status:'cancelled',updated_at_ms:3})));
 assert.equal(s.timeouts.size,0);
});
test('failed inspection polling releases busy flag and schedules retry',async()=>{
 const s=setup({api:async()=>{throw Error('离线')}});s.render(state(running()));
 await s.poll();assert.match(s.nodes['#inspectionLiveStatus'].textContent,/离线/);
 assert.equal([...s.timeouts.values()].filter(t=>t.ms===200).length,1);
});
