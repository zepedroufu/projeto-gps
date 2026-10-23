const fs=require('fs'),vm=require('vm');
const dados=[['data_hora','lat','lon','vel_kmh','satelites','viagem']];
for(let i=0;i<10;i++) dados.push(['2026-10-21 10:00:0'+i,-18.9,-48.2,30,8,i<4?1:(i<8?2:3)]);
const sheet={getLastRow:()=>dados.length,
 getRange:(r,c,nr,nc)=>({getValues:()=>{const o=[];for(let i=0;i<nr;i++)o.push((dados[r-1+i]||[]).slice(0,nc));return o;},
  setValues:()=>{}}),
 deleteRows:(p,q)=>{dados.splice(p-1,q);}};
const ctx={SpreadsheetApp:{getActiveSpreadsheet:()=>({getSheets:()=>[sheet],getSpreadsheetTimeZone:()=>'America/Sao_Paulo'})},
 LockService:{getScriptLock:()=>({waitLock(){},releaseLock(){}})},Utilities:{formatDate:()=>'X'},Number,String,isNaN,Date};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(process.argv[2],'utf8'),ctx);
let p=ctx.getPontos();
const v2=p.filter(x=>x.v==='2').map(x=>({r:x.r,t:x.t}));
console.log('viagem 2 linhas',v2.map(x=>x.r));
console.log('apagadas',ctx.excluirViagem(v2));
console.log(dados.map(d=>d[5]).join(','));
// linha errada: t diferente nao apaga
console.log('apagadas (t errado)',ctx.excluirViagem([{r:2,t:'outra'}]));
// nao contiguas
const v13=ctx.getPontos().filter(x=>x.v==='1'||x.v==='3').map(x=>({r:x.r,t:x.t}));
console.log('apagadas 1e3',ctx.excluirViagem(v13), dados.length);
