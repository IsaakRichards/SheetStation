/**
 * SheetStation signaling backend.
 *
 * Paste this into Extensions > Apps Script of the Google Sheet you want to use,
 * then Deploy > New deployment > Web app, "Execute as: Me",
 * "Who has access: Anyone". Copy the /exec URL into every SheetStation app.
 *
 * WARNING: there is no authentication. Anyone with the /exec URL can list your devices
 * and fetch any shared file. Never use SheetStation for sensitive data.
 *
 * The sheet only ever holds who is online, what they share, and short-lived
 * WebRTC connection offers/answers. Files never touch Google.
 *
 * Every request is a POST whose body is JSON: {"action": "...", ...}
 *   hello  {id, name, platform, catalog:[{n,s}]}  register / refresh this device
 *   peers  {id}                                  list devices seen recently
 *   send   {from, to, type, payload}             leave a message for a device
 *   poll   {id}                                  take (and delete) my messages
 */

var PEERS = 'Peers';
var MAIL = 'Mailbox';
var ONLINE_MS = 2 * 60 * 1000;   // a device is "online" if seen in the last 2 minutes
var MAIL_TTL_MS = 5 * 60 * 1000; // unread messages expire after 5 minutes
var CELL_LIMIT = 49000;          // Sheets cells hold at most 50,000 characters

function doGet() {
  return json_({ ok: true, app: 'SheetStation', time: Date.now() });
}

function doPost(e) {
  var out;
  try {
    var req = JSON.parse(e.postData.contents);
    out = handle_(req);
  } catch (err) {
    out = { ok: false, error: String(err) };
  }
  return json_(out);
}

function handle_(req) {
  switch (req.action) {
    case 'hello': return hello_(req);
    case 'peers': return peers_(req);
    case 'send':  return send_(req);
    case 'poll':  return poll_(req);
    default: return { ok: false, error: 'unknown action ' + req.action };
  }
}

function sheet_(name, headers) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sh = ss.getSheetByName(name);
  if (!sh) {
    sh = ss.insertSheet(name);
    sh.getRange(1, 1, 1, headers.length).setValues([headers]);
    sh.setFrozenRows(1);
  }
  return sh;
}

function peersSheet_() { return sheet_(PEERS, ['id', 'name', 'platform', 'lastSeen', 'catalog']); }
function mailSheet_() { return sheet_(MAIL, ['to', 'from', 'type', 'payload', 'created']); }

function withLock_(fn) {
  var lock = LockService.getScriptLock();
  lock.waitLock(20000);
  try { return fn(); } finally { lock.releaseLock(); }
}

function findRow_(sh, id) {
  var last = sh.getLastRow();
  if (last < 2) return -1;
  var ids = sh.getRange(2, 1, last - 1, 1).getValues();
  for (var i = 0; i < ids.length; i++) if (String(ids[i][0]) === String(id)) return i + 2;
  return -1;
}

function hello_(req) {
  if (!req.id) return { ok: false, error: 'missing id' };
  var catalog = JSON.stringify(req.catalog || []);
  if (catalog.length > CELL_LIMIT) {
    // keep as many entries as fit in one cell
    var list = req.catalog.slice();
    while (list.length && JSON.stringify(list).length > CELL_LIMIT) list.pop();
    catalog = JSON.stringify(list);
  }
  return withLock_(function () {
    var sh = peersSheet_();
    var row = findRow_(sh, req.id);
    var values = [[String(req.id), String(req.name || ''), String(req.platform || ''), Date.now(), catalog]];
    if (row < 0) sh.appendRow(values[0]);
    else sh.getRange(row, 1, 1, 5).setValues(values);
    return { ok: true };
  });
}

function peers_(req) {
  var sh = peersSheet_();
  var last = sh.getLastRow();
  var now = Date.now();
  var out = [];
  if (last >= 2) {
    var rows = sh.getRange(2, 1, last - 1, 5).getValues();
    rows.forEach(function (r) {
      if (String(r[0]) === String(req.id)) return;
      var seen = Number(r[3]) || 0;
      var cat = [];
      try { cat = JSON.parse(r[4] || '[]'); } catch (e) {}
      out.push({ id: String(r[0]), name: String(r[1]), platform: String(r[2]),
                 lastSeen: seen, online: now - seen < ONLINE_MS, catalog: cat });
    });
  }
  return { ok: true, now: now, peers: out };
}

function send_(req) {
  if (!req.to || !req.from) return { ok: false, error: 'missing to/from' };
  var payload = String(req.payload || '');
  if (payload.length > CELL_LIMIT) return { ok: false, error: 'payload too large' };
  return withLock_(function () {
    mailSheet_().appendRow([String(req.to), String(req.from), String(req.type || ''), payload, Date.now()]);
    return { ok: true };
  });
}

function poll_(req) {
  if (!req.id) return { ok: false, error: 'missing id' };
  return withLock_(function () {
    // polling doubles as a cheap heartbeat
    var ps = peersSheet_();
    var prow = findRow_(ps, req.id);
    if (prow > 0) ps.getRange(prow, 4).setValue(Date.now());

    var sh = mailSheet_();
    var last = sh.getLastRow();
    if (last < 2) return { ok: true, messages: [] };
    var rows = sh.getRange(2, 1, last - 1, 5).getValues();
    var now = Date.now();
    var mine = [], keep = [];
    rows.forEach(function (r) {
      if (now - (Number(r[4]) || 0) > MAIL_TTL_MS) return; // expired, drop
      if (String(r[0]) === String(req.id)) mine.push({ from: String(r[1]), type: String(r[2]), payload: String(r[3]) });
      else keep.push(r);
    });
    if (keep.length !== rows.length) {
      sh.getRange(2, 1, rows.length, 5).clearContent();
      if (keep.length) sh.getRange(2, 1, keep.length, 5).setValues(keep);
    }
    return { ok: true, messages: mine };
  });
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
