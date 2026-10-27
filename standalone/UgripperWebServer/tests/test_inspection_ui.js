const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8').split('let inspectionJob =')[1];
function setup(){
 const nodes={},requests=[],timers=new Map();let next=0;
 const element=id=>nodes[id]||(nodes[id]={value:'both'});
 const context={last:null,$:element,document:{querySelectorAll:()=>[]},window:{addEventListener(){}},
  setInterval:f=>{timers.set(++next,f);return next},clearInterval:id=>timers.delete(id),
  post:async(url,body)=>{requests.push({url,body})},refresh:async()=>{},message:()=>{}};
 vm.runInNewContext('let inspectionJob ='+source,context);
 return {nodes,requests,tick:()=>[...timers.values()].forEach(f=>f()),timers};
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
