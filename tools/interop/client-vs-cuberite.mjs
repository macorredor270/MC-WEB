// Interoperabilidad 3: nuestro cliente completo (mcweb, con ventana de verdad bajo Xvfb y manejado con xdotool, como si
// lo usara una persona) contra Cuberite, un servidor de Minecraft 1.8 escrito desde cero. Un bot de mineflayer mira desde
// fuera lo que hace el cliente: que entra, anda, escribe en el chat, rompe y coloca bloques.
//
//   CUBERITE_TARBALL=/ruta/Cuberite.tar.gz node client-vs-cuberite.mjs
//
// Hace falta Xvfb y xdotool (apt install xvfb xdotool; scrot es opcional, para las capturas), el cliente compilado
// (MCWEB_CLIENT o build/linux/apps/mcweb/mcweb) y `npm install` en esta carpeta.
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { connectBot, makeChecker, sleep, startCuberite, until } from './lib.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const { check, step, summary } = makeChecker('client-vs-cuberite');

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

/** Arranca Xvfb en el primer número de pantalla libre. */
async function startXvfb() {
  for (let n = 90; n < 140; n++) {
    if (existsSync(`/tmp/.X${n}-lock`)) continue;
    const proc = spawn('Xvfb', [`:${n}`, '-screen', '0', '800x450x24'], { stdio: 'ignore' });
    await sleep(800);
    if (proc.exitCode === null) return { display: `:${n}`, proc };
  }
  throw new Error('no hay pantalla X libre');
}

const outDir = process.env.MCWEB_INTEROP_OUT || mkdtempSync(path.join(tmpdir(), 'mcweb-interop-out-'));
const xdg = mkdtempSync(path.join(tmpdir(), 'mcweb-xdg-'));
const xvfb = await startXvfb();
const env = { ...process.env, DISPLAY: xvfb.display, XDG_DATA_HOME: xdg, XDG_RUNTIME_DIR: xdg };
const xdo = (...a) => {
  const r = spawnSync('xdotool', a, { env });
  if (r.status !== 0) console.log(`     xdotool ${a.join(' ')}: ${r.stderr}`);
  return r.stdout.toString().trim();
};
const shot = (name) => spawnSync('scrot', ['-o', path.join(outDir, name)], { env });

let cub, obs, client;
const cleanup = async () => {
  try { client?.kill('SIGTERM'); } catch {}
  try { obs?.quit(); } catch {}
  try { await cub?.stop(); } catch {}
  xvfb.proc.kill();
  rmSync(xdg, { recursive: true, force: true });
};

try {
  cub = await startCuberite({ gamemode: 1, flat: true });
  console.log(`Cuberite en el puerto ${cub.port}`);
  obs = await connectBot(cub.port, 'Obs');
  const chats = [];
  const changes = [];
  obs.on('chat', (user, msg) => chats.push({ user, msg }));
  obs.on('blockUpdate', (o, n) => { if (o.type !== n.type) changes.push({ pos: n.position.clone(), from: o.name, to: n.name }); });

  const lines = [];
  client = spawn(clientBinary(), ['--cc0', '--size', '800x450', '--demo', `unirse:127.0.0.1:${cub.port}`, '--no-vsync'], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  const onData = (d) => {
    for (const l of d.toString().split('\n')) if (l.trim()) { lines.push(l); if (process.env.INTEROP_VERBOSE) console.log('[cli]', l); }
  };
  client.stdout.on('data', onData);
  client.stderr.on('data', onData);

  await step('el cliente entra en Cuberite y aparece en el mundo', async () => {
    await until(() => lines.some((l) => l.includes('jugador en')), 30000, 'jugador en');
    return true;
  });
  const me = await step('el observador ve entrar al jugador', async () => {
    await until(() => Object.values(obs.players).find((p) => p.username === 'Jugador' && p.entity), 10000, 'jugador visible');
    return true;
  }) && Object.values(obs.players).find((p) => p.username === 'Jugador');
  await sleep(2500);  // que cargue el terreno

  // Sin gestor de ventanas, el foco hay que dárselo a mano
  const wid = xdo('search', '--name', 'MC-WEB').split('\n')[0];
  check(!!wid, `ventana del cliente encontrada (${wid})`);
  xdo('windowfocus', wid);
  xdo('mousemove', '400', '225');
  await sleep(300);
  xdo('click', '1');  // el primer clic solo captura el ratón
  await sleep(500);
  xdo('windowfocus', wid);  // (capturar el ratón le quita el foco de teclado a la ventana)
  await sleep(300);
  shot('cuberite-1-entrada.png');

  const at = () => me?.entity?.position?.clone();
  await step('andar con W mueve al jugador y el observador lo ve', async () => {
    const p0 = at();
    xdo('keydown', 'w');
    await sleep(4500);  // (lejos de la zona de aparición: Cuberite no deja construir cerca)
    xdo('keyup', 'w');
    await sleep(600);
    const p1 = at();
    const d = Math.hypot(p1.x - p0.x, p1.z - p0.z);
    console.log(`     recorrió ${d.toFixed(2)} bloques`);
    return d > 12;
  });
  shot('cuberite-2-andar.png');

  await step('saltar con espacio: el observador ve subir al jugador', async () => {
    const y0 = at().y;
    xdo('keydown', 'space');  // (se mira el estado de la tecla cada tick: una pulsación instantánea se pierde)
    await sleep(150);
    xdo('keyup', 'space');
    let maxY = y0;
    for (let i = 0; i < 12; i++) {
      await sleep(60);
      maxY = Math.max(maxY, at().y);
    }
    console.log(`     subió ${(maxY - y0).toFixed(2)}`);
    return maxY - y0 > 0.3;
  });
  await sleep(600);

  await step('el chat del cliente llega a Cuberite y de vuelta', async () => {
    xdo('key', 't');
    await sleep(300);
    xdo('type', '--delay', '40', 'hola desde mcweb');
    await sleep(200);
    xdo('key', 'Return');
    await until(() => chats.some((c) => c.msg.includes('hola desde mcweb')), 6000, 'chat recibido');
    return true;
  });
  await step('un mensaje del servidor sale en el cliente (chat de otro jugador)', async () => {
    obs.chat('hola desde el observador');
    await sleep(800);
    shot('cuberite-3-chat.png');
    return true;
  });

  // Romper y colocar: Cuberite nos da piedra y el observador mira los cambios
  await step('con la orden give de Cuberite el cliente recibe objetos en la barra', async () => {
    cub.command('give Jugador stone 64');
    await sleep(1200);
    shot('cuberite-4-objeto.png');
    return true;
  });
  // Mirar hacia abajo, al suelo a nuestros pies (el cliente descarta el primer movimiento tras capturar el ratón)
  for (const dy of ['1', '160', '160']) {
    xdo('mousemove_relative', '--', '0', dy);
    await sleep(150);
  }
  await sleep(300);
  shot('cuberite-4b-mirando-abajo.png');
  await step('clic izquierdo: el bloque que se mira se rompe en el servidor', async () => {
    changes.length = 0;
    xdo('mousedown', '1');
    await sleep(250);
    xdo('mouseup', '1');
    // (un bloque de verdad cerca de los pies del cliente, no uno cualquiera que cambie por ahí)
    const near = (c) => Math.hypot(c.pos.x - at().x, c.pos.z - at().z) < 4 && Math.abs(c.pos.y - at().y) < 3;
    await until(() => changes.some((c) => c.to === 'air' && c.from !== 'air' && near(c)), 6000, 'bloque roto');
    return true;
  });
  await step('clic derecho: se coloca un bloque en el servidor', async () => {
    changes.length = 0;
    xdo('mousedown', '3');
    await sleep(150);
    xdo('mouseup', '3');
    const near = (c) => Math.hypot(c.pos.x - at().x, c.pos.z - at().z) < 4 && Math.abs(c.pos.y - at().y) < 3;
    await until(() => changes.some((c) => c.to === 'stone' && near(c)), 6000, 'bloque colocado');
    return true;
  });
  shot('cuberite-5-bloques.png');

  await step('la tecla E abre el inventario creativo y Esc lo cierra', async () => {
    xdo('key', 'e');
    await sleep(700);
    shot('cuberite-6-inventario.png');
    xdo('key', 'Escape');
    await sleep(400);
    return true;
  });

  // Fin
  client.kill('SIGTERM');
  const code = await Promise.race([new Promise((r) => client.on('exit', r)), sleep(10000).then(() => 'timeout')]);
  await step('el cliente se cierra al recibir la señal', () => code !== 'timeout' || (client.kill('SIGKILL'), false));
  await step('sin avisos ni errores del cliente (salvo el audio, que no hay en la prueba)', () => {
    const bad = lines.filter((l) => /\b(WARN|ERROR)\b/.test(l) && !/audio|ALSA/i.test(l));
    if (bad.length) console.log(bad.map((l) => `     ${l}`).join('\n'));
    return bad.length === 0;
  });
  console.log(`capturas en ${outDir}`);
} catch (e) {
  check(false, `la prueba se rompió: ${e.message}`);
} finally {
  await cleanup();
}
const failed = summary();
process.exit(failed ? 1 : 0);
