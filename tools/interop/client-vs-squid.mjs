// Interoperabilidad 2: nuestro cliente (mcweb) contra un servidor de Minecraft 1.8 de terceros (flying-squid).
//
//   node client-vs-squid.mjs
//
// Hace falta `npm install` en esta carpeta y el cliente compilado (MCWEB_CLIENT=/ruta/a/mcweb, o
// build/linux/apps/mcweb/mcweb). Sin pantalla (DISPLAY) se usa xvfb-run. Deja la captura del cliente en
// MCWEB_INTEROP_OUT (por defecto, la carpeta temporal) para mirarla a ojo.
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import zlib from 'node:zlib';
import { freePort, makeChecker, sleep } from './lib.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const squid = require('flying-squid');
const { Vec3 } = require('vec3');
const { check, step, summary } = makeChecker('client-vs-squid');

function clientBinary() {
  const candidates = [
    process.env.MCWEB_CLIENT,
    path.join(here, '../../build/linux/apps/mcweb/mcweb'),
    path.join(here, '../../build/linux-release/apps/mcweb/mcweb'),
    path.join(here, '../../build/clang/apps/mcweb/mcweb'),
  ].filter(Boolean);
  const found = candidates.find((c) => existsSync(c));
  if (!found) throw new Error(`no encuentro mcweb (MCWEB_CLIENT=...); probé: ${candidates.join(', ')}`);
  return found;
}

/** Lee un PNG RGB/RGBA de 8 bits (lo que escribe el cliente) y devuelve {width, height, rgb: Uint8Array}. */
function readPng(file) {
  const data = readFileSync(file);
  let pos = 8, width = 0, height = 0, type = 0;
  const idat = [];
  while (pos < data.length) {
    const len = data.readUInt32BE(pos);
    const kind = data.toString('latin1', pos + 4, pos + 8);
    const chunk = data.subarray(pos + 8, pos + 8 + len);
    pos += 12 + len;
    if (kind === 'IHDR') { width = chunk.readUInt32BE(0); height = chunk.readUInt32BE(4); type = chunk[9]; }
    else if (kind === 'IDAT') idat.push(chunk);
  }
  const bpp = type === 6 ? 4 : 3;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * bpp;
  const out = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let i = 0; i < stride; i++) {
      const x = raw[y * (stride + 1) + 1 + i];
      const a = i >= bpp ? out[y * stride + i - bpp] : 0;
      const b = y > 0 ? out[(y - 1) * stride + i] : 0;
      const c = y > 0 && i >= bpp ? out[(y - 1) * stride + i - bpp] : 0;
      let v;
      if (f === 0) v = x;
      else if (f === 1) v = x + a;
      else if (f === 2) v = x + b;
      else if (f === 3) v = x + ((a + b) >> 1);
      else {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
      }
      out[y * stride + i] = v & 255;
    }
  }
  return { width, height, bpp, px: out };
}

const stateId = (id, meta = 0) => (id << 4) | meta;
const outDir = process.env.MCWEB_INTEROP_OUT || mkdtempSync(path.join(tmpdir(), 'mcweb-interop-out-'));
const shot = path.join(outDir, 'client-vs-squid.png');

const port = await freePort();
const squidErrors = [];
const server = squid.createMCServer({
  motd: 'squid', port, 'max-players': 5, 'online-mode': false, logging: false, gameMode: 1, difficulty: 1,
  generation: { name: 'superflat', options: {} }, kickTimeout: 10000, plugins: {}, modpe: false, 'view-distance': 4,
  'everybody-op': true, version: '1.8.8', 'player-list-text': { header: { text: 'squid' }, footer: { text: 'prueba' } },
});
server.on('error', (e) => squidErrors.push(String(e)));
server.on('clientError', (c, e) => squidErrors.push(`clientError ${e}`));
await new Promise((r) => server.on('listening', r));
console.log(`flying-squid 1.8.8 en el puerto ${port}`);

const joined = { name: null, spawnedAt: null, left: false, lastPos: null };
server.on('newPlayer', (p) => {
  joined.name = p.username;
  p.on('spawned', () => {
    joined.spawnedAt = p.position.clone();
    server.broadcast('hola desde squid');
    const base = p.position.floored();
    for (let i = 0; i < 4; i++) server.setBlock(server.overworld, base.offset(3, i, 2), stateId(41));  // una torre de bloques de oro
    server.setBlock(server.overworld, base.offset(3, 4, 2), stateId(95, 14));                          // y cristal rojo encima
    server.setBlock(server.overworld, base.offset(-3, 0, 2), stateId(54, 2));                           // un cofre
    server.spawnMob(90, server.overworld, base.offset(0, 0, 3).offset(0.5, 0, 0.5), {});               // un cerdo
  });
  p.on('disconnected', () => { joined.left = true; joined.lastPos = p.position.clone(); });
});

// El cliente: con pantalla virtual si no hay una
const needsXvfb = !process.env.DISPLAY;
const args = ['--cc0', '--size', '800x450', '--demo', `unirse:127.0.0.1:${port}`, '--screenshot', shot, '--screenshot-delay', '8', '--exit', '--no-vsync'];
const cmd = needsXvfb ? 'xvfb-run' : clientBinary();
const cmdArgs = needsXvfb ? ['-a', '-s', '-screen 0 800x450x24', clientBinary(), ...args] : args;
const lines = [];
const proc = spawn(cmd, cmdArgs, { stdio: ['ignore', 'pipe', 'pipe'] });
const onData = (d) => {
  for (const l of d.toString().split('\n')) if (l.trim()) { lines.push(l); if (process.env.INTEROP_VERBOSE) console.log('[cli]', l); }
};
proc.stdout.on('data', onData);
proc.stderr.on('data', onData);
const code = await Promise.race([new Promise((r) => proc.on('exit', r)), sleep(90000).then(() => 'timeout')]);
if (code === 'timeout') proc.kill('SIGKILL');
await sleep(500);

await step('el cliente sale limpiamente', () => code === 0 || (console.log(`     código de salida: ${code}`), false));
await step('el servidor ve entrar a un jugador y aparecer en el mundo', () => joined.name && joined.spawnedAt && (console.log(`     ${joined.name} en ${joined.spawnedAt}`), true));
await step('el cliente se da por conectado y carga el mundo', () => lines.some((l) => l.includes('Conectado a')) && lines.some((l) => l.includes('mundo cargado')));
await step('llega el chat del servidor', () => lines.some((l) => l.includes('hola desde squid')));
await step('el servidor ve salir al jugador', () => joined.left);
await step('sin avisos ni errores del cliente (salvo el audio, que no hay en la prueba)', () => {
  const bad = lines.filter((l) => /\b(WARN|ERROR)\b/.test(l) && !/audio|ALSA/i.test(l));
  if (bad.length) console.log(bad.map((l) => `     ${l}`).join('\n'));
  return bad.length === 0;
});
await step('flying-squid no ha registrado errores', () => squidErrors.length === 0 || (console.log(squidErrors.join('\n')), false));

// La captura: ni vacía ni de un solo color (se ve el mundo, el cerdo y los bloques que puso el servidor)
await step('la captura enseña un mundo con detalle', () => {
  if (!existsSync(shot) || statSync(shot).size < 5000) return false;
  const img = readPng(shot);
  let sum = 0, sum2 = 0, n = 0, green = 0, blue = 0;
  for (let i = 0; i < img.width * img.height; i++) {
    const r = img.px[i * img.bpp], g = img.px[i * img.bpp + 1], b = img.px[i * img.bpp + 2];
    const l = 0.299 * r + 0.587 * g + 0.114 * b;
    sum += l; sum2 += l * l; n++;
    if (g > b + 25 && g >= r - 5) green++;
    if (b > r + 30 && b > g) blue++;
  }
  const mean = sum / n, sd = Math.sqrt(sum2 / n - mean * mean);
  console.log(`     ${img.width}x${img.height}, luminancia ${mean.toFixed(0)} ± ${sd.toFixed(0)}, ${(100 * green / n).toFixed(0)} % verde (hierba), ${(100 * blue / n).toFixed(0)} % azul (cielo)  -> ${shot}`);
  return sd > 20 && green / n > 0.08 && blue / n > 0.03;
});

server.close?.();
const failed = summary();
process.exit(failed ? 1 : 0);
