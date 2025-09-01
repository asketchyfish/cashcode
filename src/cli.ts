#!/usr/bin/env bun
import { startDevServer } from './server/dev';

function help() {
  console.log(`cash - Cashcode CLI\n\nUsage:\n  cash dev [--port 3000]  Start dev server\n  cash start [--port 3000] Start server (alias of dev for now)\n  cash build               Build (stub)\n  cash help                Show help\n`);
}

function parsePort(argv: string[]): number | undefined {
  const pIdx = argv.findIndex(a => a === '--port' || a === '-p');
  if (pIdx !== -1 && argv[pIdx + 1]) {
    const v = Number(argv[pIdx + 1]);
    if (!Number.isNaN(v) && v > 0) return v;
  }
  const env = Number(process.env.CASH_PORT);
  if (!Number.isNaN(env) && env > 0) return env;
  return undefined;
}

async function main() {
  const argv = process.argv.slice(2);
  const cmd = argv[0] ?? 'help';
  switch (cmd) {
    case 'dev':
    case 'start': {
      const port = parsePort(argv);
      startDevServer(port);
      break;
    }
    case 'build': {
      console.log('Build not implemented yet.');
      process.exit(0);
      break;
    }
    case 'help':
    default:
      help();
      process.exit(0);
  }
}

main();


