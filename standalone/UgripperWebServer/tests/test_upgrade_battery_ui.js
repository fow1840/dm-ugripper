const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
const buttons=[{},{},{}],notice={};
const context={busy:false,uploading:false,$:()=>notice,document:{querySelectorAll:selector=>{assert.equal(selector,'.upgradeForm button');return buttons;}}};
vm.createContext(context);
vm.runInContext(source.slice(source.indexOf('function upgradeBatteryError('),source.indexOf('function bytes(')),context);
for(const percent of [0,20,29,30,31,100]){
 const error=context.upgradeBatteryError({connected:true,percent});
 assert.equal(Boolean(error),percent<30);
 context.updateUpgradeBatteryButtons(error);
 assert.ok(buttons.every(b=>b.disabled===(percent<30)));
 if(percent===30)assert.match(notice.textContent,/30%/);
}
for(const value of [undefined,{}, {connected:false,percent:90},{connected:true,percent:-1},{connected:true,percent:101},{connected:true,percent:'50'}]){
 assert.match(context.upgradeBatteryError(value),/不可用/);
}
context.busy=true;context.updateUpgradeBatteryButtons('');assert.ok(buttons.every(b=>b.disabled));
console.log('upgrade battery UI PASS');
