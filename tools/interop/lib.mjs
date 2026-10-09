// Utilidades comunes de las pruebas de interoperabilidad: arrancar nuestro servidor dedicado, conectar bots de
// mineflayer (un cliente 1.8 de terceros) y llevar la cuenta de comprobaciones.
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
export const mineflayer = require('mineflayer');
export const { Vec3 } = require('vec3');

export const VERSION = '1.8.8';

/** Sitio del servidor dedicado: MCWEB_SERVER, o el de build/linux. */
export function serverBinary() {
  const candidates = [
    process.env.MCWEB_SERVER,
    path.join(here, '../../build/linux/apps/mcweb-server/mcweb-server'),
    path.join(here, '../../build/linux-release/apps/mcweb-server/mcweb-server'),
    path.join(here, '../../build/clang/apps/mcweb-server/mcweb-server'),
  ].filter(Boolean);
  const found = candidates.find((c) => existsSync(c));
  if (!found) throw new Error(`no encuentro mcweb-server (MCWEB_SERVER=...); probé: ${candidates.join(', ')}`);
  return found;
}

export function freePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.listen(0, '127.0.0.1', () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
    s.on('error', reject);
  });
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Espera hasta que `fn()` devuelva algo verdadero (o falle con `what` pasado `ms`). */
export async function until(fn, ms, what, step = 50) {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > ms) throw new Error(`tiempo agotado: ${what}`);
    await sleep(step);
  }
}

export function once(emitter, event, ms, what = event) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => {
      emitter.off(event, on);
      reject(new Error(`tiempo agotado esperando "${what}"`));
    }, ms);
    const on = (...args) => {
      clearTimeout(t);
      resolve(args);
    };
    emitter.once(event, on);
  });
}

/** Arranca mcweb-server en una carpeta temporal. */
export async function startServer(props = {}, ops = []) {
  const dir = mkdtempSync(path.join(tmpdir(), 'mcweb-interop-'));
  if (ops.length) writeFileSync(path.join(dir, 'ops.txt'), ops.join('\n') + '\n');
  const port = await freePort();
  const all = { 'online-mode': 'false', 'level-seed': '42', 'view-distance': '4', gamemode: '1', difficulty: '1', ...props };
  writeFileSync(path.join(dir, 'server.properties'), Object.entries(all).map(([k, v]) => `${k}=${v}`).join('\n') + '\n');
  const proc = spawn(serverBinary(), ['--dir', dir, '--port', String(port)], { stdio: ['pipe', 'pipe', 'pipe'] });
  const logs = [];
  let ready;
  const readyP = new Promise((r) => (ready = r));
  const onData = (d) => {
    for (const line of d.toString().split('\n')) {
      if (!line.trim()) continue;
      logs.push(line);
      if (process.env.INTEROP_VERBOSE) console.log('[srv]', line);
      if (line.includes('servidor escuchando')) ready();
    }
  };
  proc.stdout.on('data', onData);
  proc.stderr.on('data', onData);
  const exited = new Promise((r) => proc.on('exit', (code) => r(code)));
  await Promise.race([readyP, sleep(20000).then(() => { throw new Error('el servidor no arrancó'); }), exited.then((c) => { throw new Error(`el servidor salió con ${c}`); })]);
  return {
    dir,
    port,
    logs,
    command: (c) => proc.stdin.write(c + '\n'),
    async stop() {
      proc.stdin.write('stop\n');
      await Promise.race([exited, sleep(8000)]);
      if (proc.exitCode === null) proc.kill('SIGKILL');
      rmSync(dir, { recursive: true, force: true });
    },
  };
}

/** Conecta un bot de mineflayer. Devuelve cuando ya ha aparecido en el mundo. */
export async function connectBot(port, username, opts = {}) {
  const bot = mineflayer.createBot({ host: '127.0.0.1', port, username, version: VERSION, auth: 'offline', hideErrors: false, ...opts });
  bot.protocolErrors = [];
  bot._client.on('error', (e) => bot.protocolErrors.push(e.message));
  bot.on('error', (e) => bot.protocolErrors.push(e.message));
  bot.kickedWith = null;
  bot.on('kicked', (reason) => (bot.kickedWith = reason));
  await Promise.race([once(bot, 'spawn', 20000, 'spawn'), once(bot, 'end', 20000, 'end').then(([r]) => { throw new Error(`desconectado al entrar: ${r} ${JSON.stringify(bot.kickedWith)}`); })]);
  return bot;
}

/** Cuenta de comprobaciones con salida legible. */
export function makeChecker(title) {
  let failed = 0, passed = 0;
  const check = (ok, msg) => {
    console.log(`${ok ? 'OK   ' : 'FALLO'} ${msg}`);
    if (ok) passed++;
    else failed++;
  };
  const step = async (msg, fn) => {
    try {
      const r = await fn();
      check(r !== false, msg);
      return r;
    } catch (e) {
      check(false, `${msg}: ${e.message}`);
      return undefined;
    }
  };
  return {
    check,
    step,
    summary() {
      console.log(`\n${title}: ${passed} bien, ${failed} mal`);
      return failed;
    },
  };
}
