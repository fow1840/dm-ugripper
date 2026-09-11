/* Shared by the offline browser and Node verification. */
(function (root) {
  const aliases = {I:'INFO', INFO:'INFO', W:'WARNING', WARN:'WARNING', WARNING:'WARNING', E:'ERROR', ERROR:'ERROR', F:'ERROR', FATAL:'ERROR', CRITICAL:'ERROR', D:'DEBUG', DEBUG:'DEBUG', T:'TRACE', TRACE:'TRACE'};
  function parse(raw, file, line) {
    const clean = raw.replace(/\x1b\[[0-9;]*m/g, '');
    const outer = clean.match(/^\[(\d{10}(?:\.\d+)?)\]\[tid ([^\]]+)\]\[([A-Z]+)\]\[([^\]]*)\]\s*/);
    const levelMatch = outer ? outer[3] : (clean.match(/\[(INFO|WARNING|WARN|ERROR|FATAL|CRITICAL|DEBUG|TRACE|I|W|E|F|D|T)\]/i) || clean.match(/(?:^|\s)(INFO|WARNING|WARN|ERROR|FATAL|CRITICAL|DEBUG|TRACE)(?=\s|:)/i) || [])[1];
    const date = clean.match(/\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:[.,]\d+)?(?:Z|[+-]\d{2}:\d{2})?/);
    const time = outer ? Number(outer[1])*1000 : date ? Date.parse(date[0].replace(',', '.')) : NaN;
    const message = outer ? clean.slice(outer[0].length) : clean;
    const restore = /\brestore(?:[ _-]+usb)?\b|ugripper_restore_usb/i.test(message);
    let event = '';
    for (const [pattern, label] of [
      [/^restore usb requested\b/i,'请求'], [/^restore usb start\b/i,'执行'],
      [/^restore usb recovered\b/i,'恢复成功'], [/^restore usb command completed\b/i,'命令完成'],
      [/^restore usb command failed\b/i,'命令失败'], [/^restore usb failed after max attempts\b/i,'重试耗尽'],
      [/^restore usb pending cleared\b/i,'等待取消'], [/^restore usb pending wait\b/i,'等待'],
      [/^restore usb (?:symlinks|ready) timeout\b/i,'超时']
    ]) { if (pattern.test(message)) { event = label; break; } }
    const fields = {};
    for (const m of message.matchAll(/\b(cause|evidence|side|attempt)=([\s\S]*?)(?=\s+[a-zA-Z_][\w]*=|$)/g)) fields[m[1]] = m[2].trim();
    return {raw, message, file, line, time, level:aliases[(levelMatch || '').toUpperCase()] || 'UNKNOWN', source:outer ? outer[4] : '', code:(message.match(/\bUGR-\d{2}-\d{2}\b/) || [''])[0], restore, event, fields};
  }
  function summarize(rows) {
    const levels = {}, events = {}, codes = {}, repeats = {};
    for (const r of rows) {
      levels[r.level] = (levels[r.level] || 0)+1;
      if (r.event) events[r.event] = (events[r.event] || 0)+1;
      if (r.code) codes[r.code] = (codes[r.code] || 0)+1;
      repeats[r.message] = (repeats[r.message] || 0)+1;
    }
    return {levels, events, codes, repeats};
  }
  const api = {parse, summarize};
  if (typeof module !== 'undefined') module.exports = api;
  root.LogParser = api;
})(globalThis);
