'use strict';
const byId = id => document.getElementById(id);
const audio = byId('audio'), play = byId('play'), volume = byId('volume');
let wanted = false;
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
async function refresh() {
  try {
    const response = await fetch('/api/status', { cache: 'no-store', signal: AbortSignal.timeout(4000) });
    if (!response.ok) throw new Error('offline');
    const state = await response.json();
    byId('connection').textContent = state.receiving ? '正在接收' : (state.active ? '正在调台' : '接收已停止');
    if (state.frequency_hz) byId('frequency').textContent = (state.frequency_hz / 1e6).toFixed(2);
    byId('mode').textContent = state.mode || 'FM';
    byId('stereo').textContent = state.receiving ? (state.stereo ? 'STEREO' : 'MONO') : '—';
    byId('station').textContent = state.rds?.ps || (state.receiving ? '当前电台' : '等待 N1 开始接收');
    byId('programme').textContent = state.rds?.text || (state.receiving ? '电台暂未提供节目文字' : '请在 N1 打开收音机并开始接收。');
    play.disabled = !state.receiving && !wanted;
    if (!state.active && wanted) { stop(); byId('notice').textContent = 'N1 已停止接收，恢复后可再次点击收听。'; }
  } catch (_) {
    byId('connection').textContent = '连接中断';
    if (!wanted) play.disabled = true;
  } finally { setTimeout(refresh, 2500); }
}
refresh();
