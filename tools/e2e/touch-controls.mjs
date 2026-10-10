// Prueba en el navegador (Chromium con emulación móvil y toques reales por CDP): joystick, correr con candado,
// saltar, barra rápida, pausa, inventario y giro de cámara con el dedo.
//
//   node tools/e2e/touch-controls.mjs [url-base] [carpeta-de-capturas]
//
// Hace falta el build web servido en `url-base` (por defecto http://localhost:8124) y Playwright
// (`PLAYWRIGHT=/ruta/a/playwright/index.mjs` si no se resuelve como módulo). Con GL por software
// (SwiftShader) el juego va a pocos fps: la prueba espera a que pasen las cosas en vez de contar tiempo.
const { chromium } = await import(process.env.PLAYWRIGHT || 'playwright');
const base = process.argv[2] || 'http://localhost:8124';
const out = process.argv[3] || '.';
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'OK  ' : 'FALLO'} ${msg}`); if (!ok) failed++; };
setTimeout(() => { console.log('TIEMPO AGOTADO'); process.exit(2); }, 420000);

const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const ctx = await browser.newContext({ viewport: { width: 480, height: 270 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
const page = await ctx.newPage();
page.on('pageerror', (e) => console.log('pageerror', e.message));
await page.goto(`${base}/index.html?autostart=cc0&seed=42&time=4000&freeze-time=true&mode=creative&rd=3`);
await page.waitForFunction(() => typeof Module !== 'undefined' && Module._mcw_debug, null, { timeout: 120000 });
await page.waitForTimeout(12000);  // mundo cargado

const dbg = (what) => page.evaluate((w) => Module._mcw_debug(w), what);
const BUTTON = { Jump: 0, Sneak: 1, Attack: 2, Use: 3, Inventory: 4, Drop: 5, Pause: 6, Chat: 7, Perspective: 8 };
const buttonAt = async (name) => ({ x: await dbg(100 + 2 * BUTTON[name]), y: await dbg(101 + 2 * BUTTON[name]) });
const until = async (fn, ms = 60000, step = 250) => {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { if (await fn()) return true; await page.waitForTimeout(step); }
  return false;
};
const cdp = await ctx.newCDPSession(page);
const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
const settledYaw = async () => {
  let prev = await dbg(0);
  for (let i = 0; i < 120; i++) {
    await page.waitForTimeout(500);
    const cur = await dbg(0);
    if (Math.abs(cur - prev) < 1e-7) return cur;
    prev = cur;
  }
  return prev;
};

// Los botones están donde dice el juego y dentro de la pantalla
for (const n of Object.keys(BUTTON)) {
  const p = await buttonAt(n);
  check(p.x > 0 && p.x < 480 && p.y > 0 && p.y < 270, `botón ${n} en (${p.x.toFixed(0)}, ${p.y.toFixed(0)})`);
}
const stick = { x: await dbg(130), y: await dbg(131), r: await dbg(132) };
check(stick.r > 20, `joystick con radio ${stick.r.toFixed(0)}`);
await page.screenshot({ path: `${out}/touch-ui.png` });

// 1) Joystick fijo (el de serie, y no se mueve): un dedo sobre su centro, empujado hacia delante, mueve al jugador hacia donde mira
const x0 = await dbg(2), z0 = await dbg(4);
const yaw = await settledYaw();
await touch('touchStart', [{ x: stick.x, y: stick.y, id: 1 }]);
await touch('touchMove', [{ x: stick.x, y: stick.y - 0.6 * stick.r, id: 1 }]);
const moved = await until(async () => Math.hypot((await dbg(2)) - x0, (await dbg(4)) - z0) > 0.8);
const dx = (await dbg(2)) - x0, dz = (await dbg(4)) - z0, len = Math.hypot(dx, dz) || 1;
const fx = -Math.sin(yaw), fz = -Math.cos(yaw);  // (la cámara mira a (-sen, -cos): giro 0 = norte, -Z)
console.log(`avance (${dx.toFixed(2)}, ${dz.toFixed(2)}), mira (${fx.toFixed(2)}, ${fz.toFixed(2)})`);
check(moved, 'el joystick mueve al jugador');
check((dx * fx + dz * fz) / len > 0.85, 'y lo mueve hacia donde mira la cámara');
await page.screenshot({ path: `${out}/touch-moving.png` });

// 2) Empujado a tope hacia delante: candado de correr (sigue corriendo al aflojar un poco)
await touch('touchMove', [{ x: stick.x, y: stick.y - 2.2 * stick.r, id: 1 }]);
check(await until(async () => (await dbg(6)) === 1), 'a tope hacia delante: corre');
await touch('touchMove', [{ x: stick.x, y: stick.y - 0.7 * stick.r, id: 1 }]);  // la base no se mueve: se afloja a 0,7 del radio del centro de siempre
await page.waitForTimeout(1500);
check((await dbg(6)) === 1, 'el candado mantiene la carrera al aflojar');
await page.screenshot({ path: `${out}/touch-sprint.png` });
await touch('touchEnd', []);
check(await until(async () => (await dbg(6)) === 0, 30000), 'al soltar el joystick deja de correr');

// 3) Saltar: el botón levanta al jugador del suelo
await until(async () => (await dbg(9)) === 1);
const y0 = await dbg(3);
const jump = await buttonAt('Jump');
await touch('touchStart', [{ x: jump.x, y: jump.y, id: 2 }]);
let maxY = y0;
const jumped = await until(async () => { maxY = Math.max(maxY, await dbg(3)); return maxY > y0 + 0.3; }, 30000, 100);
await touch('touchEnd', []);
check(jumped, `el botón de saltar levanta al jugador (${y0.toFixed(2)} -> ${maxY.toFixed(2)})`);

// 4) Barra rápida: tocar y deslizar elige casilla
const hotbarLeft = 240 - 91 + 1, hotbarY = 270 - 10;
await touch('touchStart', [{ x: hotbarLeft + 2 * 20 + 10, y: hotbarY, id: 3 }]);
check(await until(async () => (await dbg(8)) === 2, 20000), 'tocar la barra elige la casilla 3');
await touch('touchMove', [{ x: hotbarLeft + 5 * 20 + 10, y: hotbarY, id: 3 }]);
check(await until(async () => (await dbg(8)) === 5, 20000), 'deslizar elige la casilla 6');
await touch('touchEnd', []);

// 5) Giro de cámara con el dedo, en el centro-derecha (sin botones)
const yaw0 = await settledYaw();
await touch('touchStart', [{ x: 250, y: 100, id: 4 }]);
await touch('touchMove', [{ x: 274, y: 100, id: 4 }]);
for (let i = 1; i <= 29; i++) await touch('touchMove', [{ x: 274 + i * 6, y: 100, id: 4 }]);
await touch('touchEnd', []);
const yaw1 = await settledYaw();
console.log(`yaw ${yaw0.toFixed(4)} -> ${yaw1.toFixed(4)} (delta ${(yaw1 - yaw0).toFixed(4)}, esperado -0.8910)`);
check(Math.abs(yaw1 - yaw0 + 0.891) < 0.891 * 0.12, 'el arrastre gira la cámara lo esperado');

// 6) Inventario y pausa: se abren al soltar el dedo sobre el botón
const inv = await buttonAt('Inventory');
await touch('touchStart', [{ x: inv.x, y: inv.y, id: 5 }]);
await touch('touchEnd', []);
check(await until(async () => (await dbg(10)) === 1, 30000), 'el botón del inventario abre el inventario');
await page.screenshot({ path: `${out}/touch-inventory.png` });
// Cerrar con la X (el mismo sitio que la pausa)
const pause = await buttonAt('Pause');
await touch('touchStart', [{ x: pause.x, y: pause.y, id: 6 }]);
await touch('touchEnd', []);
check(await until(async () => (await dbg(10)) === 0, 30000), 'la X cierra el inventario');
await touch('touchStart', [{ x: pause.x, y: pause.y, id: 7 }]);
await touch('touchEnd', []);
check(await until(async () => (await dbg(10)) === 2, 30000), 'el botón de pausa abre el menú de pausa');
await page.screenshot({ path: `${out}/touch-pause.png` });

await browser.close();
process.exit(failed ? 1 : 0);
