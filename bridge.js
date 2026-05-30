const net = require('net');
const { WebSocketServer } = require('ws');

const WS_PORT = 8080;
const REDIS_PORT = 6379;
const REDIS_HOST = '127.0.0.1';

const wss = new WebSocketServer({ port: WS_PORT });

console.log(`WebSocket bridge running on ws://localhost:${WS_PORT}`);
console.log(`Forwarding to your C++ server at ${REDIS_HOST}:${REDIS_PORT}`);

wss.on('connection', (ws) => {
  console.log('Browser client connected');

  const tcp = net.createConnection(REDIS_PORT, REDIS_HOST);

  tcp.on('connect', () => {
    console.log('Connected to C++ Redis server');
  });

  tcp.on('data', (data) => {
    if (ws.readyState === ws.OPEN) {
      ws.send(data.toString());
    }
  });

  tcp.on('error', (err) => {
    console.error('TCP error:', err.message);
    if (ws.readyState === ws.OPEN) {
      ws.send('-ERR Cannot connect to Redis server. Is it running?\r\n');
    }
  });

  tcp.on('close', () => {
    if (ws.readyState === ws.OPEN) ws.close();
  });

  ws.on('message', (data) => {
    if (tcp.writable) {
      tcp.write(data.toString());
    }
  });

  ws.on('close', () => {
    console.log('Browser client disconnected');
    tcp.destroy();
  });

  ws.on('error', (err) => {
    console.error('WS error:', err.message);
    tcp.destroy();
  });
});