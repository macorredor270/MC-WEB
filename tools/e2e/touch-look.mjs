// Prueba en el navegador (Chromium con emulación móvil): contexto WebGL de alto rendimiento y giro táctil.
//
//   node tools/e2e/touch-look.mjs [url-base] [carpeta-de-capturas]
//
// Hace falta el build web servido en `url-base` (por defecto http://localhost:8124) y Playwright
// (`PLAYWRIGHT=/ruta/a/playwright/index.mjs` si no se resuelve como módulo). Con GL por software
// (SwiftShader) el juego va a pocos fps: la prueba espera a que la cámara se asiente.
const { chromium } = await import(process.env.PLAYWRIGHT || 'playwright');
const base = process.argv[2] || 'http://localhost:8124';
const out = process.argv[3] || '.';
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'OK  ' : 'FALLO'} ${msg}`); if (!ok) failed++; };
setTimeout(() => { console.log('TIEMPO AGOTADO'); process.exit(2); }, 300000);

const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const ctx = await browser.newContext({ viewport: { width: 480, height: 270 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
const page = await ctx.newPage();
await page.goto(`${base}/index.html?autostart=cc0&seed=42&time=4000&freeze-time=true&mode=creative&rd=3`);
await page.waitForFunction(() => typeof Module !== 'undefined' && Module._mcw_debug_yaw, null, { timeout: 120000 });
await page.waitForTimeout(12000);  // mundo cargado

// 1) El contexto WebGL2 que usa el juego pide la GPU potente
const attrs = await page.evaluate(() => document.getElementById('canvas').getContext('webgl2')?.getContextAttributes() ?? null);
check(attrs?.powerPreference === 'high-performance', `powerPreference = ${attrs?.powerPreference}`);
check(attrs?.alpha === false && attrs?.antialias === false, `sin alpha ni antialias (${JSON.stringify(attrs)})`);

// 2) Un arrastre de 198 px (CSS) gira la cámara 198 * 0,0045 = 0,891 rad (sensibilidad por defecto)
const yaw = () => page.evaluate(() => Module._mcw_debug_yaw());
const settled = async () => {
  let prev = await yaw();
  for (let i = 0; i < 120; i++) {
    await page.waitForTimeout(500);
    const cur = await yaw();
    if (Math.abs(cur - prev) < 1e-7) return cur;
    prev = cur;
  }
  return prev;
};
const yaw0 = await settled();
const cdp = await ctx.newCDPSession(page);
const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
await touch('touchStart', [{ x: 250, y: 100, id: 3 }]);
// El primer paso ya pasa el umbral de arrastre (si no, con pocos fps cuenta como "mantener")
await touch('touchMove', [{ x: 274, y: 100, id: 3 }]);
for (let i = 1; i <= 29; i++) await touch('touchMove', [{ x: 274 + i * 6, y: 100, id: 3 }]);
await touch('touchEnd', []);
const yaw1 = await settled();
const delta = yaw1 - yaw0;
console.log(`yaw ${yaw0.toFixed(4)} -> ${yaw1.toFixed(4)} (delta ${delta.toFixed(4)}, esperado -0.8910)`);
check(Math.abs(delta + 0.891) < 0.891 * 0.12, 'el arrastre gira la cámara lo esperado (sin pérdidas al empezar)');
await page.screenshot({ path: `${out}/touch-look.png` });

await browser.close();
process.exit(failed ? 1 : 0);
