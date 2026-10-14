const MAX_LINES = 40
const CMD_GET_STATUS = 0x10
const CMD_PING = 0x11

const state = document.getElementById('state')
const lines = document.getElementById('lines')
const socket = new WebSocket(`ws://${location.host}/ws`)

const show = (text) => {
  const kept = lines.textContent.split('\n').slice(0, MAX_LINES - 1)
  lines.textContent = [text, ...kept].join('\n')
}

const send = (cmd) => {
  const line = JSON.stringify({ cmd })
  socket.send(line)
  show(`> ${line}`)
}

socket.addEventListener('open', () => {
  state.textContent = 'open'
})
socket.addEventListener('close', () => {
  state.textContent = 'closed'
})
socket.addEventListener('message', (event) => {
  show(event.data)
})

document.getElementById('ping').addEventListener('click', () => send(CMD_PING))
document.getElementById('status').addEventListener('click', () => send(CMD_GET_STATUS))
