const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
const snippet=source.slice(source.indexOf('function renderEncoder('),source.indexOf('async function pollEncoder('));
test('encoder raw count is rendered signed, including negative register values',()=>{
 let rows;
 const context={$:()=>({replaceChildren:(...values)=>{rows=values}}),row:(label,value)=>({label,value}),el:()=>({})};
 vm.createContext(context);vm.runInContext(snippet,context);
 for(const [position,want] of [[0,0],[32767,32767],[32768,-32768],[61364,-4172],[65535,-1],[-4000,-4000]]) {
  context.renderEncoder('left',{status:'ok',position,angle_deg:0,angle_rad:0,read_at:0});
  assert.equal(rows[0].value,String(want));
 }
});
