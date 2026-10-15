const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
async function check(password,fail=false){
 const requests=[],errors=[];let dialogs=0;
 const context={$:()=>({open:false}),confirmAction:async()=>{dialogs++;return password},
  post:async(...args)=>{requests.push(args);if(fail)throw Error('network')},message:s=>errors.push(s)};
 vm.createContext(context);
 vm.runInContext(source.slice(source.indexOf('let versionConfirmationOpen='),source.indexOf('function bytes(')),context);
 const task={id:'job',confirmation:{token:'one',message:'XU failed'}};
 await Promise.all([context.confirmCameraVersion(task),context.confirmCameraVersion(task)]);
 assert.equal(dialogs,1);assert.equal(requests.length,1);
 assert.equal(requests[0][0],password?'/api/v1/camera/version-confirm':'/api/v1/camera/version-cancel');
 assert.equal(requests[0][1].token,'one');
 await context.confirmCameraVersion(task);
 assert.equal(dialogs,fail?2:1); // A failed request remains retryable; successful decisions are not repeated.
 if(fail)assert.equal(errors[0],'network');
}
(async()=>{await check('password');await check(false);await check('password',true);console.log('camera version confirmation UI PASS')})().catch(e=>{console.error(e);process.exitCode=1});
