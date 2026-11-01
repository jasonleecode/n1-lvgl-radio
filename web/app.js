'use strict';
const byId = id => document.getElementById(id);
const audio = byId('audio'), play = byId('play'), volume = byId('volume');
let wanted = false, state = null, editDirty = false, pendingPreset = null;
let presetSignature = '', presets = [], snapshot = 0, queue = Promise.resolve();
try { volume.value = localStorage.getItem('n1-radio-volume') || '80'; } catch (_) {}
function setVolume() {
  audio.volume = Number(volume.value) / 100;
  byId('volumeText').textContent = `${volume.value}%`;
  try { localStorage.setItem('n1-radio-volume', volume.value); } catch (_) {}
}
setVolume(); volume.addEventListener('input', setVolume);
function stop() { wanted = false; audio.pause(); audio.removeAttribute('src'); audio.load(); play.textContent = '▶ 开始收听'; }
play.addEventListener('click', async () => {
  if (wanted) { stop(); byId('notice').textContent = '已停止本机播放，N1 继续收听。'; return; }
  wanted = true; play.textContent = '■ 停止收听';
  byId('notice').textContent = '正在缓冲音频…';
  audio.src = `/stream.mp3?t=${Date.now()}`;
  try { await audio.play(); } catch (_) {
    if (wanted) { stop(); byId('notice').textContent = '暂时无法播放，请点击重试。'; }
  }
});
audio.addEventListener('playing', () => { byId('notice').textContent = '正在本机播放，音量仅影响这台设备。'; });
audio.addEventListener('waiting', () => { if (wanted) byId('notice').textContent = '正在缓冲音频…'; });
audio.addEventListener('error', () => { if (wanted) { stop(); byId('notice').textContent = '音频连接中断，请点击重新收听。'; } });
function controlNotice(message, error = false) {
  byId('controlNotice').textContent = message;
  byId('controlNotice').classList.toggle('error', error);
}
const mhz = hz => (hz / 1e6).toFixed(6).replace(/0+$/, '').replace(/\.$/, '');
async function jsonFetch(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', signal: AbortSignal.timeout(12000), ...options });
  const data = await response.json();
  if (!response.ok || !data.ok) throw new Error(data.message || '暂时无法连接 N1');
  return data;
}
function command(payload) {
  const task = queue.then(async () => {
    controlNotice('正在同步到 N1…');
    const result = await jsonFetch('/api/control', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(payload) });
    await loadState();
    controlNotice(payload.command.startsWith('preset_') ? '收藏已与 N1 同步。' : '调台已发送，所有设备将收听当前电台。');
    return result;
  });
  queue = task.catch(error => { controlNotice(error.message, true); loadState(); });
  return task;
}
for (const id of ['tuneFrequency', 'tuneMode']) byId(id).addEventListener('input', () => { editDirty = true; });
byId('tuneForm').addEventListener('submit', async event => {
  event.preventDefault();
  const frequency = Number(byId('tuneFrequency').value), hz = Math.round(frequency * 1e6);
  if (!Number.isFinite(frequency) || hz < 500000 || hz > 1766000000) { controlNotice('频率需在 0.5～1766 MHz 之间。', true); return; }
  try { await command({command:'tune', value:hz, mode:byId('tuneMode').value}); editDirty = false; await loadState(); } catch (_) {}
});
for (const [id, sign] of [['stepDown', -1], ['stepUp', 1]]) byId(id).addEventListener('click', () => {
  editDirty = false;
  command({command:'step', value:sign * Number(byId('stepSize').value)}).catch(() => {});
});
byId('showSave').addEventListener('click', () => {
  if (!state?.frequency_hz) { controlNotice('请先选择电台。', true); return; }
  pendingPreset = {value:state.frequency_hz, mode:state.mode};
  byId('saveTarget').textContent = `收藏 ${mhz(state.frequency_hz)} MHz · ${state.mode}`;
  byId('presetName').value = presets.find(p => p.frequency_hz === state.frequency_hz && p.mode === state.mode)?.name || state.rds?.ps || `电台 ${mhz(state.frequency_hz)}`;
  byId('saveForm').hidden = false; byId('presetName').focus();
});
byId('cancelSave').addEventListener('click', () => { byId('saveForm').hidden = true; pendingPreset = null; });
byId('saveForm').addEventListener('submit', async event => {
  event.preventDefault(); if (!pendingPreset) return;
  const payload = {command:'preset_save', ...pendingPreset, name:byId('presetName').value.trim()};
  try { await command(payload); byId('saveForm').hidden = true; pendingPreset = null; } catch (_) {}
});
function renderPresets() {
  const signature = JSON.stringify([presets, state?.frequency_hz, state?.mode]);
  if (signature === presetSignature) return;
  presetSignature = signature;
  const list = byId('presetList'); list.replaceChildren();
  byId('presetCount').textContent = `${presets.length} / 16`;
  if (!presets.length) { const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = '还没有收藏，试试“收藏当前”。'; list.append(empty); }
  for (const preset of presets) {
    const row = document.createElement('div'); row.className = 'preset-row';
    const pick = document.createElement('button'); pick.type = 'button'; pick.className = 'preset-pick';
    pick.classList.toggle('active', preset.frequency_hz === state?.frequency_hz && preset.mode === state?.mode);
    const name = document.createElement('strong'); name.textContent = preset.name;
    const frequency = document.createElement('span'); frequency.textContent = `${mhz(preset.frequency_hz)} MHz · ${preset.mode}`;
    pick.append(name, frequency);
    pick.addEventListener('click', () => { editDirty = false; command({command:'tune', value:preset.frequency_hz, mode:preset.mode}).catch(() => {}); });
    const remove = document.createElement('button'); remove.type = 'button'; remove.className = 'preset-remove'; remove.textContent = '移除'; remove.setAttribute('aria-label', `移除 ${preset.name}`);
    remove.addEventListener('click', () => { command({command:'preset_delete', value:preset.number, frequency_hz:preset.frequency_hz, mode:preset.mode}).catch(() => {}); });
    row.append(pick, remove); list.append(row);
  }
}
async function loadState() {
  const generation = ++snapshot;
  try {
    const [next, saved] = await Promise.all([jsonFetch('/api/status'), jsonFetch('/api/presets')]);
    if (generation !== snapshot) return;
    state = next; presets = saved.presets;
    byId('connection').textContent = state.receiving ? '正在接收' : (state.active ? '正在调台' : '接收已停止');
    byId('frequency').textContent = mhz(state.frequency_hz);
    byId('mode').textContent = state.mode;
    byId('stereo').textContent = state.receiving ? (state.stereo ? 'STEREO' : 'MONO') : '—';
    byId('station').textContent = state.rds?.ps || presets.find(p => p.frequency_hz === state.frequency_hz && p.mode === state.mode)?.name || (state.receiving ? '当前电台' : '等待开始接收');
    byId('programme').textContent = state.rds?.text || (state.receiving ? '电台暂未提供节目文字' : '可输入频率或点击收藏电台开始接收。');
    if (!editDirty && !['tuneFrequency','tuneMode'].includes(document.activeElement.id)) { byId('tuneFrequency').value = mhz(state.frequency_hz); byId('tuneMode').value = state.mode; }
    byId('showSave').disabled = false;
    play.disabled = !state.receiving && !wanted;
    if (!state.active && wanted) { stop(); byId('notice').textContent = 'N1 已停止接收，恢复后可再次点击收听。'; }
    renderPresets();
  } catch (_) {
    if (generation !== snapshot) return;
    byId('connection').textContent = '等待 N1';
    if (!wanted) play.disabled = true;
    byId('programme').textContent = '可输入频率调台，N1 将启动接收。';
  }
}
async function refresh() { await loadState(); setTimeout(refresh, 1000); }
refresh();
