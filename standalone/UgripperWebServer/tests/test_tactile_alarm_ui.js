const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
const nodes={};
function el(){return {children:[],append(...v){this.children.push(...v)},replaceChildren(...v){this.children=v}}}
const context={URLSearchParams,Date,el,tactileLabel:s=>s,$:key=>nodes[key]??(nodes[key]=el())};vm.createContext(context);
vm.runInContext(source.slice(source.indexOf('let tactileAlarmKey=')),context);
const saved={id:'same',updated_at_ms:1,images:[{camera:'left_tcam_l',serial:'sn',scope:'reference'}]};
const draw=states=>context.renderTactileAlarm(saved,states);
draw([{camera:'left_tcam_l',serial:'sn',warning:false}]);assert.equal(nodes['#tactileAlarmPanel'].hidden,true);assert.equal(nodes['#tactileAlarmImages'].children.length,0);
draw([{camera:'left_tcam_l',serial:'sn',warning:true}]);assert.equal(nodes['#tactileAlarmPanel'].hidden,false);assert.equal(nodes['#tactileAlarmImages'].children.length,1);
draw([{camera:'left_tcam_l',serial:'sn',warning:false}]);assert.equal(nodes['#tactileAlarmPanel'].hidden,true);assert.equal(nodes['#tactileAlarmImages'].children.length,0);
draw([{camera:'left_tcam_l',serial:'new',warning:true}]);assert.equal(nodes['#tactileAlarmImages'].children.length,0);
draw([{camera:'left_tcam_l',serial:'sn',warning:true,error:'disconnected'}]);assert.equal(nodes['#tactileAlarmPanel'].hidden,true);
console.log('alarm UI gates PASS: normal, alarm, cleared same generation, replacement, disconnected');
