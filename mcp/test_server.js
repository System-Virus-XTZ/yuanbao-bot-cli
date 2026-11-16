// 极简 MCP server（stdio / JSON-RPC 2.0），用于验证客户端协议实现
// 协议：initialize → tools/list → tools/call
const readline = require('readline');

const rl = readline.createInterface({ input: process.stdin });

function reply(obj) {
  process.stdout.write(JSON.stringify(obj) + '\n');
}

const TOOLS = [
  {
    name: 'echo',
    description: '回显一段文本（测试用）',
    inputSchema: {
      type: 'object',
      properties: { text: { type: 'string', description: '要回显的文本' } },
      required: ['text'],
    },
  },
  {
    name: 'add',
    description: '两个数字相加',
    inputSchema: {
      type: 'object',
      properties: { a: { type: 'number' }, b: { type: 'number' } },
      required: ['a', 'b'],
    },
  },
];

rl.on('line', (line) => {
  const s = line.trim();
  if (!s) return;
  let req;
  try { req = JSON.parse(s); } catch { return; }
  if (!req.method) return;                 // 通知（notifications/initialized）不回复
  if (req.method === 'initialize') {
    reply({ jsonrpc: '2.0', id: req.id, result: {
      protocolVersion: '2024-11-05',
      capabilities: { tools: {} },
      serverInfo: { name: 'test-server', version: '1.0.0' },
    }});
  } else if (req.method === 'tools/list') {
    reply({ jsonrpc: '2.0', id: req.id, result: { tools: TOOLS } });
  } else if (req.method === 'tools/call') {
    const name = req.params && req.params.name;
    const args = (req.params && req.params.arguments) || {};
    let text = '';
    if (name === 'echo') text = 'echo: ' + (args.text || '');
    else if (name === 'add') text = 'sum: ' + (Number(args.a) + Number(args.b));
    else text = 'unknown tool: ' + name;
    reply({ jsonrpc: '2.0', id: req.id, result: { content: [{ type: 'text', text }] } });
  } else {
    reply({ jsonrpc: '2.0', id: req.id, error: { code: -32601, message: 'method not found' } });
  }
});
