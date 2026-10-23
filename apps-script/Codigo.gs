// ================= Apps Script do projeto GPS =================
// Cole TUDO isto no arquivo "Codigo.gs" do editor do Apps Script (Extensoes > Apps Script).
// O arquivo HTML da pagina (mapa.html desta pasta) vai em um arquivo HTML chamado "mapa".
// A planilha precisa ter o cabecalho na linha 1: data_hora, lat, lon, vel_kmh, satelites, viagem

var CHAVE = 'troque-esta-senha';  // escolha uma senha simples, sem espacos; o mapa abre em .../exec?k=SUA_SENHA

// Recebe os arquivos CSV enviados pela ESP32 e escreve na planilha
// (colunas: data_hora, lat, lon, vel_kmh, satelites, viagem)
function doPost(e) {
  var planilha = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];
  var linhas = e.postData.contents.split('\n');
  var dados = [];

  for (var i = 0; i < linhas.length; i++) {
    var l = linhas[i].trim();
    if (l === '' || l.indexOf('data_hora') === 0) continue;  // pula vazias e cabeçalho
    var c = l.split(',');
    if (c.length < 5) continue;
    var viagem = (c.length >= 6 && c[5] !== '') ? parseInt(c[5]) : '';  // arquivos antigos não têm viagem
    dados.push([c[0], parseFloat(c[1]), parseFloat(c[2]), parseFloat(c[3]), parseInt(c[4]), viagem]);
  }

  if (dados.length > 0) {
    planilha.getRange(planilha.getLastRow() + 1, 1, dados.length, 6).setValues(dados);
  }
  return ContentService.createTextOutput('OK ' + dados.length);
}

// Texto de data/hora de uma célula (a planilha pode converter o texto em data)
function textoHora(valor, tz) {
  return (valor instanceof Date)
    ? Utilities.formatDate(valor, tz, 'yyyy-MM-dd HH:mm:ss')
    : String(valor);
}

// Abre a pagina do mapa (use o link terminando em /exec?k=SUA_SENHA)
function doGet(e) {
  if (!e || e.parameter.k !== CHAVE) {
    return ContentService.createTextOutput('Acesso negado');
  }
  return HtmlService.createHtmlOutputFromFile('mapa')
    .setTitle('Monitoramento do veiculo')
    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

// Chamada pela página do mapa para buscar os pontos da planilha
function getPontos() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var planilha = ss.getSheets()[0];
  var ultima = planilha.getLastRow();
  if (ultima < 2) return [];

  var tz = ss.getSpreadsheetTimeZone();
  var valores = planilha.getRange(2, 1, ultima - 1, 6).getValues();
  var pontos = [];
  for (var i = 0; i < valores.length; i++) {
    var l = valores[i];
    var lat = Number(l[1]), lon = Number(l[2]);
    if (isNaN(lat) || isNaN(lon) || (lat === 0 && lon === 0)) continue;
    pontos.push({ r: i + 2, t: textoHora(l[0], tz), lat: lat, lon: lon, vel: Number(l[3]), sats: Number(l[4]),
                  v: (l[5] === '' || l[5] === null) ? '' : String(l[5]) });
  }
  return pontos;
}

// Chamada pelo botao "Excluir viagem" do mapa: apaga da planilha as linhas da viagem.
// itens = [{r: numero da linha, t: data_hora}]. Confere a data_hora de cada linha antes de apagar,
// para nunca apagar a linha errada se a planilha mudou.
function excluirViagem(itens) {
  var trava = LockService.getScriptLock();
  trava.waitLock(20000);
  try {
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var planilha = ss.getSheets()[0];
    var tz = ss.getSpreadsheetTimeZone();
    var ultima = planilha.getLastRow();
    var linhas = [];
    for (var i = 0; i < itens.length; i++) {
      var r = Number(itens[i].r);
      if (!(r >= 2 && r <= ultima)) continue;
      var atual = textoHora(planilha.getRange(r, 1, 1, 1).getValues()[0][0], tz);
      if (atual === String(itens[i].t)) linhas.push(r);
    }
    linhas.sort(function (a, b) { return b - a; });  // de baixo para cima
    var apagadas = 0, k = 0;
    while (k < linhas.length) {
      var fim = k;
      while (fim + 1 < linhas.length && linhas[fim + 1] === linhas[fim] - 1) fim++;
      var qtd = fim - k + 1;
      planilha.deleteRows(linhas[fim], qtd);  // bloco continuo: da menor linha para cima
      apagadas += qtd;
      k = fim + 1;
    }
    return apagadas;
  } finally {
    trava.releaseLock();
  }
}
