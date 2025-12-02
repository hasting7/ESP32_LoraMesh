let currentNodeAddr = null;
let allMessages = [];

function renderChat() {
  const listEl = document.getElementById("chat-list");
  const emptyEl = document.getElementById("chat-empty");
  listEl.innerHTML = "";

  let chats = allMessages.filter((m) =>
    [1, 2, 4].includes(Number(m.message_type))
  );

  if (!chats.length) {
    emptyEl.style.display = "";
    return;
  }
  emptyEl.style.display = "none";

  chats.sort((a, b) => {
    return getTime(a.timestamp) - getTime(b.timestamp);
  });

  for (const m of chats) {
    const isMe = m.origin === currentNodeAddr;
    const div = document.createElement("div");
    div.className = "chat-msg " + (isMe ? "right" : "left");

    const meta = document.createElement("div");
    meta.className = "chat-meta";
    meta.textContent = `${m.origin} → ${m.destination}`;
    div.appendChild(meta);

    const content = document.createElement("div");
    content.textContent = m.content || "";
    div.appendChild(content);

    const footer = document.createElement("div");
    footer.className = "chat-footer";
    footer.textContent = formatTime(getTime(m.timestamp));
    div.appendChild(footer);

    listEl.appendChild(div);
  }
}

function renderSystem() {
  const tbody = document.getElementById("system-tbody");
  const empty = document.getElementById("system-empty");
  tbody.innerHTML = "";

  let sys = allMessages.filter((m) =>
    [3, 5, 6, 7].includes(Number(m.message_type))
  );

  if (!sys.length) {
    empty.style.display = "";
    return;
  }
  empty.style.display = "none";

  sys.sort((a, b) => getTime(b.timestamp) - getTime(a.timestamp));

  for (const m of sys) {
    const tr = document.createElement("tr");

    if (m.origin === currentNodeAddr) {
      tr.className = "sys-from-me";
    } else if (m.destination === currentNodeAddr) {
      tr.className = "sys-to-me";
    } else {
      tr.className = "sys-neutral";
    }

    tr.innerHTML = `
      <td>${formatTime(getTime(m.timestamp))}</td>
      <td>${m.id}</td>
      <td>${m.message_type}</td>
      <td>${m.origin}</td>
      <td>${m.source}</td>
      <td>${m.destination}</td>
      <td>${m.steps}</td>
      <td>${m.length}</td>
      <td>${m.rssi}</td>
      <td>${m.snr}</td>
      <td>${m.stage}</td>
      <td>${m.transfer_status}</td>
      <td>${m.ack_status}</td>
      <td>${m.ack_for}</td>
      <td>${m.content || ""}</td>
    `;
    tbody.appendChild(tr);
  }
}

function renderNodes(nodes) {
  const tbody = document.getElementById("node-tbody");
  tbody.innerHTML = "";

  const current = nodes.find((n) => n.current_node === 1);
  currentNodeAddr = current ? current.address : null;

  for (const n of nodes) {
    const tr = document.createElement("tr");
    const secondsAgo = getNow() - n.last_connection;
    tr.innerHTML = `
      <td>${n.address}</td>
      <td>${n.name}</td>
      <td>${n.messages}</td>
      <td>${n.avg_rssi}</td>
      <td>${n.avg_snr}</td>
      <td>${n.status === 0 ? "Alive" : "Dead"}</td>
      <td>${Math.round(secondsAgo)} sec ago</td>
    `;
    tbody.appendChild(tr);
  }

  renderChat();
  renderSystem();
}

function formatTime(ts) {
  const d = new Date(ts * 1000);
  return `${String(d.getHours()).padStart(2, "0")}:${String(
    d.getMinutes()
  ).padStart(2, "0")}`;
}

function getTime(ts) {
  return typeof ts === "number" ? ts : Date.parse(ts) / 1000;
}

function getNow() {
  return Date.now() / 1000;
}

// Example polling logic
async function pollNodes() {
  try {
    const res = await fetch("/api/nodes");
    const data = await res.json();
    renderNodes(data);
  } catch (e) {
    console.error("Failed to poll nodes:", e);
  }
}

async function pollMessages() {
  try {
    const res = await fetch("/api/messages");
    const data = await res.json();
    allMessages = data;
    renderChat();
    renderSystem();
  } catch (e) {
    console.error("Failed to poll messages:", e);
  }
}

setInterval(pollMessages, 2000);
setInterval(pollNodes, 5000);
