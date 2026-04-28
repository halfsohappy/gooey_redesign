/* ── V1 Grouped Tracker — scenes as cards, messages nested inside ──
 * Implements the "Grouped Cards" design direction:
 * scene = container card, messages = nested sub-table rows.
 * Pulse dots reflect enabled+running state, not streamed values.
 */

import { getActiveDev } from './device-manager.js';
import { sendCmd, addr } from './command.js';
import { toast } from './toast.js';
import { esc } from './feed.js';

/* Expand state persists across re-renders */
let _openScene = null;
let _openMsg   = null;

export function renderGroupedTracker() {
  const container = document.getElementById('sceneTrackerV1');
  if (!container) return;

  const dev = getActiveDev();
  container.innerHTML = '';

  if (!dev || Object.keys(dev.scenes).length === 0) {
    container.innerHTML =
      '<div class="empty-state empty-state-inline">No scenes yet — Query device or create one below.</div>';
    return;
  }

  const sceneNames = Object.keys(dev.scenes);
  const msgCount   = Object.keys(dev.messages).length;

  const caption = document.createElement('div');
  caption.className = 'v1-tracker-caption';
  caption.textContent =
    sceneNames.length + ' scene' + (sceneNames.length !== 1 ? 's' : '') +
    ' · ' + msgCount + ' message' + (msgCount !== 1 ? 's' : '');
  container.appendChild(caption);

  sceneNames.forEach(function (name, idx) {
    container.appendChild(_buildSceneCard(dev, name, dev.scenes[name], idx));
  });
}

/* ── Scene card ── */

function _buildSceneCard(dev, name, s, idx) {
  const isRunning = !!s.running;

  const msgNames = (s.msgs || '').replace(/\+/g, ',').split(',')
    .map(function (m) { return m.trim(); }).filter(Boolean);
  const msgs = msgNames
    .map(function (mn) { return { name: mn, data: dev.messages[mn] || null }; })
    .filter(function (x) { return x.data !== null; });
  const enabledCount = msgs.filter(function (x) { return x.data.enabled !== 'false'; }).length;

  const card = document.createElement('div');
  card.className = 'scene-v1-card' + (isRunning ? ' running' : '');
  card.dataset.sceneName = name;

  /* ── Header row ── */
  const hdr = document.createElement('div');
  hdr.className = 'scene-v1-hdr' + (_openScene === name ? ' is-open' : '');

  /* Pulse dot */
  const dot = document.createElement('span');
  dot.className = 'v1-pulse' + (isRunning ? ' live' : '');
  dot.style.setProperty('--pulse-speed', (1.6 + idx * 0.07) + 's');
  dot.style.setProperty('--pulse-delay', -(idx * 0.25) + 's');
  hdr.appendChild(dot);

  /* Name + sub-label */
  const nameWrap = document.createElement('div');
  const nameEl   = document.createElement('div');
  nameEl.className = 'scene-v1-name';
  nameEl.textContent = name;
  const subEl = document.createElement('div');
  subEl.className = 'scene-v1-sub';
  subEl.textContent =
    (isRunning ? 'Active' : 'Stopped') +
    ' · ' + enabledCount + '/' + msgs.length + ' msg' +
    ' · ' + (s.period || '50') + 'ms';
  nameWrap.appendChild(nameEl);
  nameWrap.appendChild(subEl);
  hdr.appendChild(nameWrap);

  /* Message chips */
  const chips = document.createElement('div');
  chips.className = 'scene-v1-chips';
  msgs.slice(0, 7).forEach(function (x) {
    const isEnabled = x.data.enabled !== 'false';
    const chip = document.createElement('span');
    chip.className = 'scene-v1-chip' + (isEnabled ? ' enabled' : '');
    chip.textContent = x.name;
    chips.appendChild(chip);
  });
  if (msgs.length > 7) {
    const more = document.createElement('span');
    more.className = 'scene-v1-chip';
    more.style.cssText = 'color:var(--text-light);border-color:transparent';
    more.textContent = '+' + (msgs.length - 7);
    chips.appendChild(more);
  }
  hdr.appendChild(chips);

  /* Scene address */
  const adrEl = document.createElement('div');
  adrEl.className = 'scene-v1-adr';
  adrEl.textContent = (s.adr && s.adr !== '—') ? s.adr : (s.ip || '—');
  hdr.appendChild(adrEl);

  /* Start / Stop buttons */
  const btns = document.createElement('div');
  btns.className = 'scene-v1-btns';
  btns.addEventListener('click', function (e) { e.stopPropagation(); });

  const btnGo  = _mkBtn('scene-v1-btn scene-v1-btn-go',  '▶', 'Start scene', isRunning);
  const btnStp = _mkBtn('scene-v1-btn scene-v1-btn-stp', '■', 'Stop scene',  !isRunning);
  btnGo.addEventListener('click',  function () { _sceneAct('start', name, dev); });
  btnStp.addEventListener('click', function () { _sceneAct('stop',  name, dev); });
  btns.appendChild(btnGo);
  btns.appendChild(btnStp);
  hdr.appendChild(btns);

  /* ── Expandable body ── */
  const body = document.createElement('div');
  body.className = 'scene-v1-body' + (_openScene === name ? ' open' : '');

  /* Meta strip */
  const strip = document.createElement('div');
  strip.className = 'scene-v1-strip';
  const overrides = (s.override || '—').replace(/\+/g, ', ');
  [
    ['ip',        s.ip    || '—'],
    ['port',      s.port  || '—'],
    ['period',    (s.period || '50') + 'ms'],
    ['overrides', overrides],
  ].forEach(function (kv) { strip.appendChild(_metaItem(kv[0], kv[1])); });
  if (s.gate_src && s.gate_mode) {
    strip.appendChild(_metaItem('gate', s.gate_src + ' ' + s.gate_mode.toUpperCase()));
  }

  const spacer = document.createElement('div');
  spacer.className = 'scene-v1-strip-spacer';
  strip.appendChild(spacer);

  _makeActBtns([
    ['edit', function () {
      document.dispatchEvent(new CustomEvent('v1:editScene', { detail: { name: name, data: s } }));
    }],
    ['resync', function () {
      sendCmd(addr('/annieData/{device}/scene/{name}/info', name), null)
        .then(function (r) { if (r.status === 'ok') toast('resync: ' + name, 'success'); });
    }],
    ['save', function () {
      sendCmd(addr('/annieData/{device}/save/scene'), name);
      toast('save sent: ' + name, 'success');
    }],
    ['delete', function () {
      sendCmd(addr('/annieData/{device}/scene/{name}/delete', name), null)
        .then(function (r) {
          if (r.status === 'ok') {
            toast('deleted: ' + name, 'success');
            const d = getActiveDev();
            if (d) { delete d.scenes[name]; renderGroupedTracker(); }
          }
        });
    }, true],
  ], strip, 'scene-v1-act-btn');

  body.appendChild(strip);

  /* Message sub-table */
  if (msgs.length > 0) {
    const tbl = document.createElement('div');
    tbl.className = 'scene-v1-msgtable';

    const thead = document.createElement('div');
    thead.className = 'msg-v1-thead';
    thead.innerHTML =
      '<div></div><div>Name</div><div>Sensor</div>' +
      '<div>Range</div><div>Address</div><div style="text-align:right">Solo</div>';
    tbl.appendChild(thead);

    msgs.forEach(function (x, mi) {
      const built = _buildMsgRows(x.name, x.data, isRunning, mi, msgs.length);
      tbl.appendChild(built.row);
      tbl.appendChild(built.exp);
    });

    body.appendChild(tbl);
  } else {
    const noMsg = document.createElement('div');
    noMsg.className = 'scene-v1-nomsg';
    noMsg.textContent = 'No messages assigned to this scene.';
    body.appendChild(noMsg);
  }

  /* Toggle expand on header click */
  hdr.addEventListener('click', function () {
    _openScene = (_openScene === name) ? null : name;
    renderGroupedTracker();
  });

  card.appendChild(hdr);
  card.appendChild(body);
  return card;
}

/* ── Message row + expand row ── */

function _buildMsgRows(mn, m, sceneRunning, mi, total) {
  const isEnabled = m.enabled !== 'false';
  const isLive    = sceneRunning && isEnabled;
  const isSoloed  = !!m.soloed;
  const isOpen    = _openMsg === mn;
  const low  = m.low  || m.min || '';
  const high = m.high || m.max || '';
  const isLast = mi === total - 1;

  /* Main row */
  const row = document.createElement('div');
  row.className = 'msg-v1-row' + (isOpen ? ' open' : '') + (isLast && !isOpen ? ' last' : '');
  row.dataset.msgName = mn;

  /* Pulse dot */
  const dot = document.createElement('span');
  dot.className = 'v1-pulse' + (isLive ? ' live' : '');
  dot.style.setProperty('--pulse-speed', (1.45 + mi * 0.11) + 's');
  dot.style.setProperty('--pulse-delay', -(mi * 0.2) + 's');
  dot.title = isLive ? 'enabled · scene running' : !isEnabled ? 'disabled' : 'scene stopped';
  row.appendChild(dot);

  /* Name */
  const nameCell = document.createElement('div');
  nameCell.className = 'msg-v1-name' + (isLive ? '' : ' dim');
  nameCell.textContent = mn;
  if (isSoloed) {
    const badge = document.createElement('span');
    badge.className = 'msg-v1-solo-badge';
    badge.textContent = 'SOLO';
    nameCell.appendChild(badge);
  }
  row.appendChild(nameCell);

  /* Sensor */
  const sensorCell = document.createElement('div');
  sensorCell.className = 'msg-v1-sensor';
  sensorCell.textContent = m.value || m.val || '—';
  row.appendChild(sensorCell);

  /* Range */
  const rangeCell = document.createElement('div');
  rangeCell.className = 'msg-v1-range' + (isLive ? '' : ' dim');
  if (low !== '' && high !== '') {
    rangeCell.innerHTML =
      esc(String(low)) + '<span class="msg-v1-range-arrow">→</span>' + esc(String(high));
  } else {
    rangeCell.textContent = '—';
    rangeCell.style.color = 'var(--text-light)';
  }
  row.appendChild(rangeCell);

  /* Address */
  const adrCell = document.createElement('div');
  adrCell.className = 'msg-v1-adr';
  adrCell.textContent = m.adr || m.addr || m.address || '—';
  row.appendChild(adrCell);

  /* Solo button */
  const soloCell = document.createElement('div');
  soloCell.style.textAlign = 'right';
  const soloBtn = document.createElement('button');
  soloBtn.className = 'msg-v1-solo-btn' + (isSoloed ? ' soloed' : '');
  soloBtn.textContent = 'solo';
  soloBtn.addEventListener('click', function (e) {
    e.stopPropagation();
    const willSolo = !m.soloed;
    m.soloed = willSolo;
    const act = willSolo ? 'solo' : 'unsolo';
    sendCmd(addr('/annieData/{device}/msg/{name}/' + act, mn), null)
      .then(function (r) {
        if (r.status === 'ok') { toast(act + ': ' + mn, 'success'); renderGroupedTracker(); }
      });
  });
  soloCell.appendChild(soloBtn);
  row.appendChild(soloCell);

  /* Expand row */
  const exp = document.createElement('div');
  exp.className = 'msg-v1-exp' + (isOpen ? ' open' : '');

  [
    ['ip',   m.ip   || '—'],
    ['port', m.port || '—'],
  ].forEach(function (kv) { exp.appendChild(_metaItem(kv[0], kv[1])); });
  if (low !== '' && high !== '') exp.appendChild(_metaItem('range', low + ' → ' + high));
  exp.appendChild(_metaItem('address', m.adr || m.addr || '—'));

  const expSpacer = document.createElement('div');
  expSpacer.className = 'msg-v1-exp-spacer';
  exp.appendChild(expSpacer);

  _makeActBtns([
    [isEnabled ? 'disable' : 'enable', function () {
      const act = isEnabled ? 'disable' : 'enable';
      sendCmd(addr('/annieData/{device}/msg/{name}/' + act, mn), null)
        .then(function (r) {
          if (r.status === 'ok') {
            toast(act + ': ' + mn, 'success');
            m.enabled = isEnabled ? 'false' : 'true';
            renderGroupedTracker();
          }
        });
    }],
    ['resync', function () {
      sendCmd(addr('/annieData/{device}/msg/{name}/info', mn), null)
        .then(function (r) { if (r.status === 'ok') toast('resync: ' + mn, 'success'); });
    }],
    ['save', function () {
      sendCmd(addr('/annieData/{device}/save/msg'), mn);
      toast('save sent: ' + mn, 'success');
    }],
    ['edit', function () {
      document.dispatchEvent(new CustomEvent('v1:editMsg', { detail: { name: mn, data: m } }));
    }],
    ['delete', function () {
      sendCmd(addr('/annieData/{device}/msg/{name}/delete', mn), null)
        .then(function (r) {
          if (r.status === 'ok') {
            toast('deleted: ' + mn, 'success');
            const d = getActiveDev();
            if (d) { delete d.messages[mn]; renderGroupedTracker(); }
          }
        });
    }, true],
  ], exp, 'msg-v1-exp-act');

  row.addEventListener('click', function () {
    _openMsg = (_openMsg === mn) ? null : mn;
    renderGroupedTracker();
  });

  return { row: row, exp: exp };
}

/* ── Shared helpers ── */

function _sceneAct(act, name, dev) {
  sendCmd(addr('/annieData/{device}/scene/{name}/' + act, name), null)
    .then(function (r) {
      if (r.status === 'ok') {
        toast(act + ': ' + name, 'success');
        if (dev.scenes[name]) {
          dev.scenes[name].running = (act === 'start');
          renderGroupedTracker();
        }
      }
    });
}

function _mkBtn(cls, text, title, disabled) {
  const btn = document.createElement('button');
  btn.className = cls;
  btn.textContent = text;
  btn.title = title;
  btn.disabled = !!disabled;
  return btn;
}

function _metaItem(key, val) {
  const el = document.createElement('span');
  el.className = 'scene-v1-meta-item';
  el.innerHTML =
    '<span class="scene-v1-meta-key">' + esc(key) + '</span>' +
    '<span class="scene-v1-meta-val">' + esc(String(val)) + '</span>';
  return el;
}

function _makeActBtns(defs, parent, cls) {
  defs.forEach(function (def, i) {
    if (i > 0) {
      const sep = document.createElement('span');
      sep.className = 'v1-act-sep';
      sep.textContent = '·';
      parent.appendChild(sep);
    }
    const btn = document.createElement('button');
    btn.className = cls + (def[2] ? ' danger' : '');
    btn.textContent = def[0];
    btn.addEventListener('click', function (e) { e.stopPropagation(); def[1](); });
    parent.appendChild(btn);
  });
}
