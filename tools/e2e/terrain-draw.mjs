// Prueba en el navegador (Chromium): el terreno se dibuja con pocas llamadas (páginas con multi-draw), sin
// errores de GL, y la imagen sale.
//
//   node tools/e2e/terrain-draw.mjs [url-base] [carpeta-de-capturas]
//
// Hace falta el build web servido en `url-base` (por defecto http://localhost:8124) y Playwright
// (`PLAYWRIGHT=/ruta/a/playwright/index.mjs` si no se resuelve como módulo).
const { chromium } = await import(process.env.PLAYWRIGHT || 'playwright');
const base = process.argv[2] || 'http://localhost:8124';
const out = process.argv[3] || '.';
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'OK  ' : 'FALLO'} ${msg}`); if (!ok) failed++; };
setTimeout(() => { console.log('TIEMPO AGOTADO'); process.exit(2); }, 300000);

const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const ctx = await browser.newContext({ viewport: { width: 480, height: 270 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
const page = await ctx.newPage();
const logs = [];
page.on('console', (m) => logs.push(m.text()));
page.on('pageerror', (e) => logs.push('pageerror ' + e.message));
await page.goto(`${base}/index.html?autostart=cc0&seed=42&time=4000&freeze-time=true&mode=creative&rd=4`);
await page.waitForFunction(() => typeof Module !== 'undefined' && Module._mcw_debug, null, { timeout: 120000 });
const dbg = (what) => page.evaluate((w) => Module._mcw_debug(w), what);
const until = async (fn, ms = 120000) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (await fn()) return true; await page.waitForTimeout(500); } return false; };

check(await until(async () => (await dbg(13)) > 20), 'el terreno se dibuja (secciones dibujadas > 20)');
await page.waitForTimeout(5000);
const calls = await dbg(11), quads = await dbg(12), drawn = await dbg(13), total = await dbg(14);
console.log(`llamadas ${calls}, quads ${quads}, secciones ${drawn}/${total}`);
check(calls > 0 && calls < 20, `menos de 20 llamadas de dibujo del terreno por frame (${calls})`);
check(logs.some((l) => /multi-draw/.test(l)), 'el navegador dibuja por tramos con multi-draw: ' + (logs.find((l) => /terreno: páginas/.test(l)) || '?'));
check(!logs.some((l) => /error de GL|no compila|no enlaza|pageerror/i.test(l)), 'sin errores de GL ni de shaders');
logs.filter((l) => /error|warn/i.test(l) && !/ALSA|audio/i.test(l)).slice(0, 5).forEach((l) => console.log('  log:', l));
await page.screenshot({ path: `${out}/terrain-draw.png` });

// Romper un bloque cambia una sección: se vuelve a subir solo esa malla (sin errores) y la imagen cambia
const cdp = await ctx.newCDPSession(page);
const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
// (se mira hacia el suelo arrastrando el dedo)
await touch('touchStart', [{ x: 250, y: 40, id: 2 }]);
await touch('touchMove', [{ x: 250, y: 70, id: 2 }]);
for (let i = 1; i <= 8; i++) await touch('touchMove', [{ x: 250, y: 70 + i * 12, id: 2 }]);
await touch('touchEnd', []);
await page.waitForTimeout(3000);
const atk = { x: await dbg(104), y: await dbg(105) };
const before = await dbg(12);
await touch('touchStart', [{ x: atk.x, y: atk.y, id: 1 }]);
await page.waitForTimeout(500);
await touch('touchEnd', []);
check(await until(async () => (await dbg(12)) !== before, 60000), `romper un bloque cambia lo que se dibuja (${before} -> ${await dbg(12)} quads)`);
check(!logs.some((l) => /error de GL|pageerror/i.test(l)), 'sin errores de GL tras editar el terreno');
await page.screenshot({ path: `${out}/terrain-edit.png` });
await browser.close();
process.exit(failed ? 1 : 0);
