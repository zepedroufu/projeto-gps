const fs = require('fs'), vm = require('vm');
const dados = [['data_hora','lat','lon','vel_kmh','satelites','viagem']];
const sheet = {
  getLastRow: () => dados.length,
  getRange: (r, c, nr, nc) => ({
    setValues: (v) => { v.forEach((linha, i) => { dados[r - 1 + i] = linha.slice(0, nc); }); },
    getValues: () => { const out = []; for (let i = 0; i < nr; i++) out.push((dados[r - 1 + i] || []).concat(Array(6).fill('')).slice(0, nc)); return out; }
  })
};
const ctx = {
  SpreadsheetApp: { getActiveSpreadsheet: () => ({ getSheets: () => [sheet], getSpreadsheetTimeZone: () => 'America/Sao_Paulo' }) },
  ContentService: { createTextOutput: (t) => t },
  Utilities: { formatDate: (d) => 'DATA' }, parseInt, parseFloat, Number, String, isNaN, Date
};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), ctx);
// arquivo novo (6 colunas) e arquivo antigo (5 colunas)
console.log(ctx.doPost({ postData: { contents: 'data_hora,lat,lon,vel_kmh,satelites,viagem\n2026-10-21 07:30:00,-18.9,-48.2,30.0,8,12\n2026-10-21 07:30:05,-18.91,-48.21,31.0,9,12\n' } }));
console.log(ctx.doPost({ postData: { contents: 'data_hora,lat,lon,vel_kmh,satelites\n2026-10-20 07:30:00,-18.9,-48.2,30.0,8\n\n' } }));
console.log(JSON.stringify(ctx.getPontos()));
