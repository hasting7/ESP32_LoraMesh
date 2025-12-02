// === CONFIG ===
const POLL_MS_MESSAGES = 1000;
const POLL_MS_NODES = 5000;

const CHAT_TYPES = new Set([1, 2, 4]);       // BROADCAST, NORMAL, CRITICAL
const SYSTEM_TYPES = new Set([3, 5, 6, 7]);  // ACK, MAINT, PING, COMMAND

let newestID = 0;
const allMessages = [];
const seenMessageIds = new Set();
let currentNodeAddr = null;

let selectedPeer = 'all';
let ackedIds = new Set();

// === HELPERS ===
function esc(str) {
  return String(str ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function getTimestampSeconds(msg) {
  if (typeof msg.timestamp === 'number') return msg.timestamp;
  const ts = Date.parse(msg.timestamp);
  return isFinite(ts) ? ts / 1000 : 0;
}

function formatTimeShort(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function formatAgo(secondsAgo) {
  const s = Math.trunc(secondsAgo);
  const abs = Math.abs(s);
  const mins = Math.floor(abs / 60);
  const hrs = Math.floor(abs / 3600);
  const days = Math.floor(abs / 86400);
  if (days > 0) return `${days} day${days !== 1 ? 's' : ''} ago`;
  if (hrs > 0) return `${hrs} hr${hrs !== 1 ? 's' : ''} ago`;
  if (mins > 0) return `${mins} min${mins !== 1 ? 's' : ''} ago`;
  return `${abs} sec ago`;
}

function isNum(x) {
  return typeof x === 'number' && isFinite(x);
}

const MSG_TYPE_LABEL = {
  1: "Broadcast",
  2: "Normal",
  3: "Ack",
  4: "Critical",
  5: "Maintenance",
  6: "Ping",
  7: "Command"
};

// === RENDER CHAT ===
function renderChat() {
  const list = document.getElementById('chat-list');
  const empty = document.getElementById('chat-empty');
  if (!list) return;

  let chats = allMessages.filter(m => CHAT_TYPES.has(Number(m.message_type)));
  if (selectedPeer !== 'all' && currentNodeAddr) {
    chats = chats.filter(m =>
      (m.source === currentNodeAddr && m.destination === selectedPeer) ||
      (m.source === selectedPeer && m.destination === currentNodeAddr)
    );
  }

  list.innerHTML = '';
  if (!chats.length) {
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';

  for (const msg of chats.reverse()) {
    const isFromMe = msg.origin === currentNodeAddr;
    const typeLabel = MSG_TYPE_LABEL[msg.message_type] || 'Unknown';
    const div = document.createElement('div');
    div.className = `chat-msg ${isFromMe ? 'from-me' : 'other'}`;
    div.innerHTML = `
      <div class="chat-meta">
        <span class="pill pill-type-${msg.message_type}">${typeLabel}</span>
        <span class="chat-route">${msg.source} → ${msg.destination}</span>
      </div>
      <div class="chat-content">${esc(msg.content)}</div>
      <div class="chat-footer">${formatTimeShort(getTimestampSeconds(msg))}</div>
    `;
    list.appendChild(div);
  }
}

// === RENDER SYSTEM TABLE ===
function renderSystem() {
  const tbody = document.getElementById('system-tbody');
  const empty = document.getElementById('system-empty');
  if (!tbody) return;

  tbody.innerHTML = '';
  let rows = allMessages.filter(m => SYSTEM_TYPES.has(Number(m.message_type)));
  if (!rows.length) {
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';

  for (const m of rows.reverse()) {
    const tr = document.createElement('tr');
    const origin = String(m.origin);
    const dest = String(m.destination);
    const me = String(currentNodeAddr);

    if (origin === me) {
      tr.classList.add('sys-from-me');
    } else if (dest === me) {
      tr.classList.add('sys-to-me');
    } else {
      tr.classList.add('sys-other');
    }

    tr.innerHTML = `
      <td>${formatTimeShort(getTimestampSeconds(m))}</td>
      <td>${esc(m.id)}</td>
      <td>${esc(MSG_TYPE_LABEL[m.message_type] || m.message_type)}</td>
      <td>${esc(m.origin)}</td>
      <td>${esc(m.source)}</td>
      <td>${esc(m.destination)}</td>
      <td>${esc(m.steps)}</td>
      <td>${esc(m.length)}</td>
      <td>${esc(m.rssi)}</td>
      <td>${esc(m.snr)}</td>
      <td>${esc(m.stage)}</td>
      <td>${esc(m.transfer_status)}</td>
      <td>${esc(m.ack_status)}</td>
      <td>${esc(m.ack_for)}</td>
      <td>${esc(m.content)}</td>
    `;
    tbody.appendChild(tr);
  }
}

// === RENDER NODES ===
function renderNodes(nodes) {
  const tbody = document.getElementById('node-tbody');
  const empty = document.getElementById('node-empty');
  const targetSel = document.getElementById('target');
  const peerSel = document.getElementById('filter-peer');

  tbody.innerHTML = '';
  targetSel.innerHTML = '';
  peerSel.innerHTML = '';

  if (!nodes.length) {
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';

  const current = nodes.find(n => n.current_node === 1);
  if (current) currentNodeAddr = String(current.address);

  // target node select (default to 0 = broadcast)
  const optDefault = document.createElement('option');
  optDefault.value = "0";
  optDefault.textContent = "Broadcast";
  targetSel.appendChild(optDefault);

  for (const n of nodes) {
    const tr = document.createElement('tr');
    if (n.current_node === 1) tr.classList.add('this-node');

    tr.innerHTML = `
      <td>${esc(n.address)}</td>
      <td>${esc(n.name)}</td>
      <td>${esc(n.messages ?? 0)}</td>
      <td>${isNum(n.avg_rssi) ? n.avg_rssi.toFixed(1) : '-'}</td>
      <td>${isNum(n.avg_snr) ? n.avg_snr.toFixed(1) : '-'}</td>
      <td>${n.status === 0 ? 'Alive' : (n.status === 1 ? 'Dead' : 'Unknown')}</td>
      <td>${formatAgo((Date.now() / 1000) - (n.last_connection ?? 0))}</td>
    `;
    tbody.appendChild(tr);

    // Add to selects
    const tOpt = document.createElement('option');
    tOpt.value = String(n.address);
    tOpt.textContent = `${n.name} — ${n.address}`;
    targetSel.appendChild(tOpt);

    const pOpt = tOpt.cloneNode(true);
    peerSel.appendChild(pOpt);
  }

  renderChat();
  renderSystem();
}

// === POLLING ===
async function pollMessages() {
  const url = newestID
    ? `/api/messages?since_id=${encodeURIComponent(newestID)}`
    : `/api/messages`;

  try {
    const res = await fetch(url);
    if (!res.ok) return;
    const msgs = await res.json();
    for (const m of msgs) {
      if (seenMessageIds.has(m.id)) continue;
      seenMessageIds.add(m.id);
      allMessages.push(m);
      newestID = Math.max(newestID, m.id);
    }
    renderChat();
    renderSystem();
  } catch (e) {
    console.warn("Failed to poll messages:", e);
  }
}

async function pollNodes() {
  try {
    const res = await fetch("/api/nodes");
    if (!res.ok) return;
    const data = await res.json();
    if (Array.isArray(data)) renderNodes(data);
  } catch (e) {
    console.warn("Failed to poll nodes:", e);
  }
}

// === INIT ===
function init() {
  document.getElementById('filter-peer')?.addEventListener('change', e => {
    selectedPeer = e.target.value || 'all';
    renderChat();
    renderSystem();
  });

  document.getElementById('send-form')?.addEventListener('submit', e => {
    const target = document.getElementById('target');
    if (target && !target.value) {
      target.value = "0"; // force broadcast
    }
  });

  pollMessages();
  pollNodes();
  setInterval(pollMessages, POLL_MS_MESSAGES);
  setInterval(pollNodes, POLL_MS_NODES);
}

document.addEventListener('DOMContentLoaded', init);
