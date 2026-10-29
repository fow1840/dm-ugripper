const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
async function check(){
 let answer=false,dialogs=0;const requests=[],node={};
 const ctx={$:s=>s==='#confirmDialog'?{open:false}:node,uploading:false,confirmAction:async()=>{dialogs++;return answer},post:async(...v)=>requests.push(v),message:()=>{},refresh:()=>{}};
 vm.createContext(ctx);vm.runInContext(source.slice(source.indexOf('let bootRebootPromptOpen='),source.indexOf('function bytes(')),ctx);
 const task={kind:'rk3588_boot',id:'one',status:'awaiting_reboot',reboot_pending:true};
 await Promise.all([ctx.confirmBootReboot(task),ctx.confirmBootReboot(task)]);
 assert.equal(dialogs,1);assert.equal(requests.length,0);
 await ctx.confirmBootReboot(task);assert.equal(dialogs,1);
 answer='password';await ctx.confirmBootReboot(task,true);assert.equal(requests.length,1);assert.equal(requests[0][0],'/api/v1/upgrades/one/reboot');
 await ctx.confirmBootReboot({...task,id:'external',external:true});assert.equal(dialogs,2);
 await ctx.confirmBootReboot({...task,id:'two'});assert.equal(dialogs,3);
 ctx.renderBootUpgrade({...task,status:'failed',message:'校验失败',reboot_pending:false});assert.match(node.textContent,/升级失败.*校验失败/);
 assert.match(ctx.bootUpgradeText(task),/需要重启/);
 assert.match(ctx.bootUpgradeText({...task,status:'rebooting'}),/等待设备/);
 assert.match(ctx.bootUpgradeText({...task,status:'success',reboot_pending:false,result:{boot_verified:true}}),/重启后校验通过/);
 console.log('boot upgrade feedback UI PASS');
}
check().catch(e=>{console.error(e);process.exitCode=1});
