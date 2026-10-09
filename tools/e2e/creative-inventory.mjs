// Prueba en el navegador del inventario del modo creativo: pestañas, desplazamiento (rueda, barra y arrastre con
// el dedo), coger y dejar objetos, y la búsqueda con el teclado (la tecla de inventario escribe y no cierra).
//
//   node tools/e2e/creative-inventory.mjs [url-base] [carpeta-de-capturas]
//
// Hace falta el build web servido en `url-base` (por defecto http://localhost:8124) y Playwright
// (`PLAYWRIGHT=/ruta/a/playwright/index.mjs` si no se resuelve como módulo).
const { chromium } = await import(process.env.PLAYWRIGHT || 'playwright');
const base = process.argv[2] || 'http://localhost:8124';
const out = process.argv[3] || '.';
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'OK  ' : 'FALLO'} ${msg}`); if (!ok) failed++; };
setTimeout(() => { console.log('TIEMPO AGOTADO'); process.exit(2); }, 420000);

const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const url = `${base}/index.html?autostart=cc0&seed=42&time=4000&freeze-time=true&mode=creative&rd=3`;

const TAB = { Blocks: 0, Decoration: 1, Redstone: 2, Transport: 3, Misc: 4, Search: 5, Food: 6, Tools: 7, Combat: 8, Brewing: 9, Materials: 10, Inventory: 11 };
const SCREEN_NONE = 0, SCREEN_MENU = 1;

async function open(ctx) {
  const page = await ctx.newPage();
  page.on('pageerror', (e) => console.log('pageerror', e.message));
  await page.goto(url);
  await page.waitForFunction(() => typeof Module !== 'undefined' && Module._mcw_debug, null, { timeout: 120000 });
  await page.waitForTimeout(12000);  // mundo cargado
  const dbg = (what) => page.evaluate((w) => Module._mcw_debug(w), what);
  const until = async (fn, ms = 60000, step = 200) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { if (await fn()) return true; await page.waitForTimeout(step); }
    return false;
  };
  const tabCenter = async (t) => ({ x: await dbg(150 + 2 * t), y: await dbg(151 + 2 * t) });
  const slotCenter = async (s) => ({ x: await dbg(200 + 2 * s), y: await dbg(201 + 2 * s) });
  return { page, dbg, until, tabCenter, slotCenter };
}

// ---------------------------------------------------------------------------------------------------------------
// 1) Con el dedo (móvil): pestañas, arrastrar para desplazar con inercia, barra, coger y dejar
// ---------------------------------------------------------------------------------------------------------------
{
  const ctx = await browser.newContext({ viewport: { width: 480, height: 270 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
  const { page, dbg, until, tabCenter, slotCenter } = await open(ctx);
  const cdp = await ctx.newCDPSession(page);
  const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
  const tap = async (p, id = 1) => { await touch('touchStart', [{ x: p.x, y: p.y, id }]); await touch('touchEnd', []); };

  const inv = { x: await dbg(108), y: await dbg(109) };  // botón del inventario
  await tap(inv);
  check(await until(async () => (await dbg(10)) === SCREEN_MENU, 30000), 'táctil: el botón abre el inventario');
  check((await dbg(140)) === TAB.Blocks, 'táctil: se abre en la pestaña de bloques');
  check((await dbg(142)) > 100, `táctil: la lista de bloques tiene ${await dbg(142)} casillas`);
  await page.screenshot({ path: `${out}/creative-touch-blocks.png` });

  // Pestañas
  await tap(await tabCenter(TAB.Tools));
  check(await until(async () => (await dbg(140)) === TAB.Tools, 20000), 'táctil: tocar la pestaña de herramientas la abre');
  await tap(await tabCenter(TAB.Inventory));
  check(await until(async () => (await dbg(140)) === TAB.Inventory, 20000), 'táctil: la pestaña de inventario de supervivencia');
  await page.screenshot({ path: `${out}/creative-touch-survival.png` });
  await tap(await tabCenter(TAB.Blocks));
  check(await until(async () => (await dbg(140)) === TAB.Blocks && (await dbg(141)) === 0, 20000), 'táctil: vuelve a bloques arriba del todo');

  // Arrastrar la rejilla hacia arriba desplaza la lista
  const s0 = await slotCenter(22);
  const row0 = await dbg(141);
  await touch('touchStart', [{ x: s0.x, y: s0.y, id: 2 }]);
  for (let i = 1; i <= 8; i++) await touch('touchMove', [{ x: s0.x, y: s0.y - i * 6, id: 2 }]);
  await touch('touchEnd', []);
  check(await until(async () => (await dbg(141)) > row0, 20000), `táctil: arrastrar hacia arriba baja la lista (fila ${row0} -> ${await dbg(141)})`);
  check((await dbg(145)) === 0, 'táctil: arrastrar no coge nada');

  // Un gesto rápido sigue deslizándose al soltar (inercia): llega más lejos de lo que se arrastró
  await tap(await tabCenter(TAB.Combat));
  await tap(await tabCenter(TAB.Blocks));
  check(await until(async () => (await dbg(141)) === 0, 20000), 'táctil: la lista vuelve arriba al cambiar de pestaña');
  await touch('touchStart', [{ x: s0.x, y: s0.y + 30, id: 3 }]);
  // (un gesto rápido: todos los movimientos seguidos y soltar enseguida; con GL por software los frames son lentos)
  for (let i = 1; i <= 6; i++) await touch('touchMove', [{ x: s0.x, y: s0.y + 30 - i * 14, id: 3 }]);
  await touch('touchEnd', []);
  const dragged = Math.floor(6 * 14 / 18);
  const flingSpeed = await until(async () => Math.abs(await dbg(148)) > 0, 5000, 20) ? await dbg(148) : 0;
  const rowEnd = await dbg(141);
  const farther = await until(async () => (await dbg(141)) > Math.max(rowEnd, dragged + 1), 15000);
  console.log(`inercia: velocidad ${flingSpeed.toFixed(0)}, arrastre ${dragged} filas, tras soltar fila ${rowEnd}, ahora ${await dbg(141)}`);
  check(farther, 'táctil: al soltar con el dedo en movimiento la lista sigue deslizándose');
  const stopAt = await dbg(141);
  await touch('touchStart', [{ x: s0.x, y: s0.y, id: 5 }]);  // tocar la frena
  await touch('touchEnd', []);
  await page.waitForTimeout(500);
  check((await dbg(148)) === 0, 'táctil: tocar frena el deslizamiento');
  check((await dbg(145)) === 0, 'táctil: y ese toque no coge nada');
  void stopAt;
  await tap(await tabCenter(TAB.Combat));
  await tap(await tabCenter(TAB.Blocks));
  await until(async () => (await dbg(141)) === 0, 20000);

  // Barra de desplazamiento: tocarla abajo lleva la lista al final; arriba, al principio
  const barTop = { x: await dbg(180), y: await dbg(181) + 4 }, barBottom = { x: await dbg(180), y: await dbg(182) - 4 };
  await touch('touchStart', [{ x: barBottom.x, y: barBottom.y, id: 4 }]);
  check(await until(async () => (await dbg(141)) === (await dbg(143)), 20000), 'táctil: la barra abajo lleva al final de la lista');
  await touch('touchMove', [{ x: barTop.x, y: barTop.y, id: 4 }]);
  check(await until(async () => (await dbg(141)) === 0, 20000), 'táctil: arrastrar la barra arriba lleva al principio');
  await touch('touchEnd', []);

  // Coger de la lista y dejar en la barra rápida (tocar, tocar)
  await tap(await slotCenter(0));
  check(await until(async () => (await dbg(145)) > 0, 20000), `táctil: tocar un objeto lo coge (id ${await dbg(145)})`);
  await tap(await slotCenter(45));  // barra rápida, casilla 0
  check(await until(async () => (await dbg(146)) > 0 && (await dbg(145)) === 0, 20000), `táctil: tocar la barra lo deja (${await dbg(147)} unidades)`);
  await page.screenshot({ path: `${out}/creative-touch-picked.png` });

  // Cerrar con la X
  const pause = { x: await dbg(112), y: await dbg(113) };
  await tap(pause);
  check(await until(async () => (await dbg(10)) === SCREEN_NONE, 30000), 'táctil: la X cierra el inventario');
  await ctx.close();
}

// ---------------------------------------------------------------------------------------------------------------
// 2) Con ratón y teclado: pestañas, rueda, barra arrastrada, búsqueda escribiendo
// ---------------------------------------------------------------------------------------------------------------
{
  const ctx = await browser.newContext({ viewport: { width: 640, height: 360 }, deviceScaleFactor: 1 });
  const { page, dbg, until, tabCenter, slotCenter } = await open(ctx);
  await page.keyboard.press('e');
  check(await until(async () => (await dbg(10)) === SCREEN_MENU, 30000), 'teclado: E abre el inventario');
  check((await dbg(140)) === TAB.Blocks, 'teclado: se abre en bloques');

  const food = await tabCenter(TAB.Food);
  await page.mouse.click(food.x, food.y);
  check(await until(async () => (await dbg(140)) === TAB.Food, 20000), 'ratón: clic en la pestaña de alimentos');

  await page.mouse.click((await tabCenter(TAB.Blocks)).x, (await tabCenter(TAB.Blocks)).y);
  await until(async () => (await dbg(140)) === TAB.Blocks);
  const hover = await slotCenter(4);
  await page.mouse.move(hover.x, hover.y);
  await page.mouse.wheel(0, 120);
  check(await until(async () => (await dbg(141)) === 1, 20000), 'ratón: la rueda baja una fila');
  await page.mouse.wheel(0, -120);
  check(await until(async () => (await dbg(141)) === 0, 20000), 'ratón: y la sube');

  // Arrastrar la barra con el ratón
  const bx = await dbg(180), by0 = await dbg(181) + 6, by1 = await dbg(182) - 6;
  await page.mouse.move(bx, by0);
  await page.mouse.down();
  await page.mouse.move(bx, (by0 + by1) / 2, { steps: 4 });
  const mid = await until(async () => (await dbg(141)) > 0 && (await dbg(141)) < (await dbg(143)), 20000);
  await page.mouse.move(bx, by1, { steps: 4 });
  const end = await until(async () => (await dbg(141)) === (await dbg(143)), 20000);
  await page.mouse.up();
  check(mid && end, `ratón: arrastrar la barra recorre la lista (hasta la fila ${await dbg(141)} de ${await dbg(143)})`);

  // Búsqueda
  const search = await tabCenter(TAB.Search);
  await page.mouse.click(search.x, search.y);
  check(await until(async () => (await dbg(140)) === TAB.Search, 20000), 'ratón: pestaña de búsqueda');
  const all = await dbg(142);
  check(all > 500, `búsqueda: sin texto salen todos (${all})`);
  await page.keyboard.type('pico', { delay: 80 });
  check(await until(async () => (await dbg(144)) === 4 && (await dbg(142)) === 5, 20000), `búsqueda: escribir "pico" deja ${await dbg(142)} objetos`);
  await page.screenshot({ path: `${out}/creative-search.png` });
  // La tecla de inventario escribe (no cierra) mientras se busca
  await page.keyboard.press('e');
  check(await until(async () => (await dbg(144)) === 5, 20000), 'búsqueda: la tecla E se escribe en el campo');
  check((await dbg(10)) === SCREEN_MENU, 'búsqueda: y el inventario sigue abierto');
  await page.keyboard.press('Backspace');
  check(await until(async () => (await dbg(144)) === 4, 20000), 'búsqueda: Retroceso borra una letra');
  await page.keyboard.press('Escape');
  check(await until(async () => (await dbg(10)) === SCREEN_NONE, 30000), 'búsqueda: Esc cierra el inventario');
  // Al reabrirlo, vuelve a la última pestaña que no era la búsqueda (con E se puede cerrar de nuevo)
  await page.keyboard.press('e');
  check(await until(async () => (await dbg(10)) === SCREEN_MENU, 30000), 'teclado: E vuelve a abrirlo');
  check((await dbg(140)) === TAB.Blocks, 'teclado: en la última pestaña de objetos usada');
  await page.keyboard.press('e');
  check(await until(async () => (await dbg(10)) === SCREEN_NONE, 30000), 'teclado: E lo cierra');
  await ctx.close();
}

await browser.close();
process.exit(failed ? 1 : 0);
