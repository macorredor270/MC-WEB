// Interoperabilidad 1: clientes de Minecraft 1.8 de verdad (mineflayer) contra nuestro servidor dedicado.
//
//   node bot-vs-server.mjs
//
// Hace falta `npm install` en esta carpeta y el servidor compilado (MCWEB_SERVER=/ruta/a/mcweb-server, o
// build/linux/apps/mcweb-server/mcweb-server). Con INTEROP_VERBOSE=1 se ve también lo que escribe el servidor.
import { createRequire } from 'node:module';
import { connectBot, makeChecker, once, sleep, startServer, until, Vec3, VERSION } from './lib.mjs';

const require = createRequire(import.meta.url);
const { check, step, summary } = makeChecker('bot-vs-server');
const bots = [];
let server;
const finish = async (code) => {
  for (const b of bots) {
    try { b.quit(); } catch { /* ya cerrado */ }
  }
  await sleep(300);
  if (server) await server.stop();
  process.exit(code);
};
setTimeout(() => { console.log('TIEMPO AGOTADO'); finish(2); }, 240000);

async function flatCreative() {
  // El mundo plano es estable: el suelo está siempre en el mismo sitio y se puede construir encima
  server = await startServer({ 'level-type': 'FLAT', gamemode: '1', 'spawn-monsters': 'false' });
  console.log(`servidor en el puerto ${server.port}`);

  const ana = await connectBot(server.port, 'Ana');
  bots.push(ana);
  const Item = require('prismarine-item')(ana.registry);
  const mcData = ana.registry;

  // --- 1) Entrar ---
  check(ana.protocolErrors.length === 0, `entrar sin errores de protocolo (${ana.protocolErrors.join('; ') || 'ninguno'})`);
  check(ana.game.gameMode === 'creative', `modo de juego: ${ana.game.gameMode}`);
  check(ana.game.dimension === 'overworld', `dimensión: ${ana.game.dimension}`);
  check(ana.game.levelType === 'flat', `tipo de mundo: ${ana.game.levelType}`);
  check(ana.health === 20 && ana.food === 20, `vida ${ana.health} y comida ${ana.food}`);
  check(ana.entity && Number.isFinite(ana.entity.position.x), `posición inicial ${ana.entity.position}`);
  const spawn = ana.entity.position.clone();

  // --- 2) Chunks ---
  await step('llegan los chunks de alrededor (distancia 4)', async () => {
    await until(() => ana.world.getColumns().length >= 49, 15000, 'chunks');
    return ana.world.getColumns().length >= 49;
  });
  const ground = ana.blockAt(spawn.offset(0, -1, 0));
  check(ground && ground.name !== 'air', `el suelo bajo los pies es ${ground?.name}`);
  const sky = ana.blockAt(spawn.offset(0, 20, 0));
  check(sky && sky.name === 'air', 'el aire de arriba es aire');
  check(ana.blockAt(new Vec3(Math.floor(spawn.x), 0, Math.floor(spawn.z)))?.name === 'bedrock', 'la capa 0 es de roca base');

  // --- 3) Chat ---
  await step('el chat de un jugador llega a todos', async () => {
    const p = once(ana, 'chat', 8000, 'chat');
    ana.chat('hola mundo');
    const [who, msg] = await p;
    return who === 'Ana' && msg === 'hola mundo';
  });
  await step('un comando sin permiso recibe una respuesta, no un fallo', async () => {
    const p = once(ana, 'message', 8000, 'respuesta al comando');
    ana.chat('/time set day');
    await p;
    return true;
  });

  // --- 4) Moverse: el servidor acepta el movimiento y no nos devuelve atrás ---
  await step('andar hacia delante', async () => {
    let forced = 0;
    const onForced = () => forced++;
    ana.on('forcedMove', onForced);
    ana.look(0, 0, true);
    const start = ana.entity.position.clone();
    ana.setControlState('forward', true);
    await sleep(1500);
    ana.setControlState('forward', false);
    ana.off('forcedMove', onForced);
    const d = ana.entity.position.distanceTo(start);
    console.log(`     recorrido ${d.toFixed(2)} bloques, ${forced} correcciones del servidor`);
    return d > 3 && forced === 0;
  });
  await step('saltar y volver a caer', async () => {
    await until(() => ana.entity.onGround, 5000, 'en el suelo');
    const y0 = ana.entity.position.y;
    ana.setControlState('jump', true);
    let maxY = y0;
    for (let i = 0; i < 12; i++) { await sleep(50); maxY = Math.max(maxY, ana.entity.position.y); }
    ana.setControlState('jump', false);
    await until(() => ana.entity.onGround, 5000, 'aterrizar');
    console.log(`     salto de ${(maxY - y0).toFixed(2)} bloques`);
    return maxY - y0 > 1.0 && maxY - y0 < 1.5;
  });

  // --- 5) Segundo jugador ---
  const beto = await connectBot(server.port, 'Beto');
  bots.push(beto);
  await step('el primer jugador ve entrar al segundo', async () => {
    await until(() => ana.players.Beto && ana.players.Beto.entity, 8000, 'Beto visible');
    return true;
  });
  await step('el segundo ve al primero', async () => {
    await until(() => beto.players.Ana && beto.players.Ana.entity, 8000, 'Ana visible');
    return true;
  });
  await step('el chat llega al otro', async () => {
    const p = once(beto, 'chat', 8000, 'chat de Ana');
    ana.chat('¿me oyes, Beto?');
    const [who, msg] = await p;
    return who === 'Ana' && msg === '¿me oyes, Beto?';
  });
  await step('se ve moverse al otro', async () => {
    const seen = beto.players.Ana.entity.position.clone();
    ana.look(Math.PI / 2, 0, true);
    ana.setControlState('forward', true);
    await sleep(1200);
    ana.setControlState('forward', false);
    await sleep(500);
    const now = beto.players.Ana.entity.position;
    const real = ana.entity.position;
    console.log(`     Ana en ${real}, Beto la ve en ${now} (antes ${seen})`);
    return now.distanceTo(real) < 1.0 && now.distanceTo(seen) > 2;
  });

  // --- 6) Romper y colocar: los dos ven lo mismo ---
  const base = ana.entity.position.floored().offset(0, -1, 0);
  const target = base.offset(2, 0, 0);
  await step('romper un bloque (creativo, al instante)', async () => {
    const block = ana.blockAt(target);
    if (!block || block.name === 'air') return false;
    const gone = once(beto, `blockUpdate:(${target.x}, ${target.y}, ${target.z})`, 8000, 'Beto ve el hueco');
    await ana.dig(block);
    await gone;
    return ana.blockAt(target).name === 'air' && beto.blockAt(target).name === 'air';
  });
  await step('colocar un bloque', async () => {
    const cobble = mcData.itemsByName.cobblestone;
    await ana.creative.setInventorySlot(36, new Item(cobble.id, 64));
    await until(() => ana.inventory.slots[36], 5000, 'objeto en la barra');
    ana.setQuickBarSlot(0);
    const below = ana.blockAt(target.offset(0, -1, 0));
    const placed = once(beto, `blockUpdate:(${target.x}, ${target.y}, ${target.z})`, 8000, 'Beto ve el bloque');
    await ana.placeBlock(below, new Vec3(0, 1, 0));
    await placed;
    return ana.blockAt(target).name === 'cobblestone' && beto.blockAt(target).name === 'cobblestone';
  });

  // --- 7) Ventanas: cofre, mesa de trabajo y horno (clics con confirmación de transacción) ---
  const toolbar = (n) => 36 + n;
  const give = async (slot, name, count = 1) => {
    await ana.creative.setInventorySlot(slot, new Item(mcData.itemsByName[name].id, count));
  };
  const place = async (name, offset) => {
    await give(toolbar(2), name);
    ana.setQuickBarSlot(2);
    const pos = base.plus(offset);
    const below = ana.blockAt(pos.offset(0, -1, 0));
    await ana.placeBlock(below, new Vec3(0, 1, 0));
    await until(() => ana.blockAt(pos)?.name === name, 8000, `${name} colocado`);
    return ana.blockAt(pos);
  };
  await step('cofre: guardar y recuperar objetos', async () => {
    const chestBlock = await place('chest', new Vec3(0, 1, 3));
    await give(toolbar(3), 'cobblestone', 20);
    const chest = await ana.openContainer(chestBlock);
    check(chest.slots.length === 27 + 36, `  ventana de cofre con ${chest.slots.length} casillas`);
    await chest.deposit(mcData.itemsByName.cobblestone.id, null, 12);
    check(chest.containerItems().reduce((n, i) => n + i.count, 0) === 12, '  en el cofre hay 12 adoquines');
    chest.close();
    await sleep(300);
    const again = await ana.openContainer(chestBlock);
    const stored = again.containerItems().reduce((n, i) => n + i.count, 0);
    check(stored === 12, `  al reabrirlo siguen ahí (${stored})`);
    await again.withdraw(mcData.itemsByName.cobblestone.id, null, 5);
    const left = again.containerItems().reduce((n, i) => n + i.count, 0);
    again.close();
    check(left === 7, `  quedan 7 en el cofre (${left})`);
    return true;
  });
  await step('mesa de trabajo: fabricar palos con tablones', async () => {
    const table = await place('crafting_table', new Vec3(1, 1, 3));
    await give(toolbar(4), 'planks', 8);
    const stick = mcData.itemsByName.stick.id;
    const recipe = ana.recipesFor(stick, null, 1, table)[0];
    if (!recipe) throw new Error('mineflayer no conoce la receta');
    await ana.craft(recipe, 2, table);
    const sticks = ana.inventory.count(stick);
    console.log(`     ${sticks} palos tras fabricar dos veces`);
    return sticks === 8;
  });
  await step('horno: fundir y recoger (tarda unos 10 segundos)', async () => {
    const furnaceBlock = await place('furnace', new Vec3(2, 1, 3));
    await give(toolbar(5), 'iron_ore', 2);
    await give(toolbar(6), 'coal', 3);
    const furnace = await ana.openFurnace(furnaceBlock);
    await furnace.putFuel(mcData.itemsByName.coal.id, null, 1);
    await furnace.putInput(mcData.itemsByName.iron_ore.id, null, 1);
    await until(() => furnace.outputItem(), 20000, 'lingote en la salida', 250);
    const out = await furnace.takeOutput();
    furnace.close();
    return out && out.name === 'iron_ingot';
  });

  // --- 8) Se va el segundo jugador ---
  await step('el primero ve salir al segundo', async () => {
    const left = once(ana, 'playerLeft', 8000, 'Beto se va');
    beto.quit();
    const [p] = await left;
    return p.username === 'Beto';
  });

  // --- 9) El servidor sigue vivo y no ha habido errores ---
  await sleep(1000);
  check(ana.protocolErrors.length === 0, `sin errores de protocolo en toda la sesión (${ana.protocolErrors.join('; ') || 'ninguno'})`);
  check(!server.logs.some((l) => /ERROR|excepci|abort/i.test(l)), 'el servidor no ha registrado errores');
}


// ---------------------------------------------------------------------------------------------------------------
// Escenario B: mundo normal en supervivencia (criaturas, golpes, objetos que caen, romper con su tiempo)
// ---------------------------------------------------------------------------------------------------------------
async function survival() {
  console.log('\n== Supervivencia en un mundo normal ==');
  await server.stop();
  server = await startServer({ gamemode: '0', difficulty: '1', 'level-seed': '42' });
  const ana = await connectBot(server.port, 'Ana');
  bots.push(ana);
  const mcData = ana.registry;
  check(ana.game.gameMode === 'survival', `modo de juego: ${ana.game.gameMode}`);
  check(ana.protocolErrors.length === 0, `entrar sin errores de protocolo (${ana.protocolErrors.join('; ') || 'ninguno'})`);

  await step('llegan chunks con relieve variado', async () => {
    await until(() => ana.world.getColumns().length >= 49, 15000, 'chunks');
    const names = new Set();
    const p = ana.entity.position.floored();
    for (let dx = -40; dx <= 40; dx += 4)
      for (let dz = -40; dz <= 40; dz += 4)
        for (let y = 100; y >= 50; y--) {
          const b = ana.blockAt(new Vec3(p.x + dx, y, p.z + dz));
          if (b && b.name !== 'air') { names.add(b.name); break; }
        }
    console.log(`     bloques de la superficie: ${[...names].join(', ')}`);
    return names.size >= 3;
  });

  const animalNames = new Set(['pig', 'cow', 'sheep', 'chicken']);
  const isItem = (e) => e.type === 'object' && /item/i.test(String(e.name || e.displayName || ''));
  const animals = () => Object.values(ana.entities).filter((e) => e.type === 'mob' && animalNames.has(String(e.name).toLowerCase()));
  await step('se ven animales con su tipo, y dos de ellos', async () => {
    await until(() => animals().length >= 2, 10000, 'animales');
    console.log(`     animales: ${animals().map((e) => e.name).join(', ')}`);
    return animals().length >= 2;
  });

  // Ir hasta el animal más cercano caminando (sin buscador de caminos: mirar, avanzar y saltar)
  const goNear = async (entity, dist, ms = 20000) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
      const d = ana.entity.position.distanceTo(entity.position);
      if (d <= dist) { ana.clearControlStates(); return true; }
      await ana.lookAt(entity.position.offset(0, entity.height * 0.5, 0), true);
      ana.setControlState('forward', true);
      ana.setControlState('jump', true);
      ana.setControlState('sprint', d > 6);
      await sleep(100);
    }
    ana.clearControlStates();
    return false;
  };
  await step('golpear a un animal hasta que muere y suelta algo', async () => {
    const target = animals().sort((a, b) => a.position.distanceTo(ana.entity.position) - b.position.distanceTo(ana.entity.position))[0];
    console.log(`     objetivo: ${target.name} a ${target.position.distanceTo(ana.entity.position).toFixed(1)} bloques`);
    if (!await goNear(target, 2.4)) throw new Error('no llego hasta el animal');
    let dead = false;
    const onGone = (e) => { if (e.id === target.id) dead = true; };
    ana.on('entityGone', onGone);
    ana.on('entityDead', onGone);
    let drops = 0;
    ana.on('entitySpawn', (e) => { if (isItem(e)) drops++; });
    for (let i = 0; i < 60 && !dead; i++) {
      if (target.position.distanceTo(ana.entity.position) > 3.2) await goNear(target, 2.2, 5000);
      await ana.lookAt(target.position.offset(0, target.height * 0.5, 0), true);
      ana.attack(target);
      await sleep(550);
    }
    ana.clearControlStates();
    console.log(`     muerto: ${dead}, objetos nuevos vistos: ${drops}`);
    return dead;
  });
  await step('recoger lo que ha soltado', async () => {
    const items = () => Object.values(ana.entities).filter(isItem);
    const have = () => ana.inventory.items().reduce((n, i) => n + i.count, 0);
    const t0 = Date.now();
    while (Date.now() - t0 < 15000 && have() === 0) {
      const it = items()[0];
      if (it) await goNear(it, 0.5, 4000);
      else await sleep(200);
    }
    console.log(`     inventario: ${ana.inventory.items().map((i) => `${i.count} ${i.name}`).join(', ') || '(vacío)'}`);
    return have() > 0;
  });

  await step('romper un bloque con su tiempo y recoger lo que suelta', async () => {
    const p = ana.entity.position.floored();
    const block = ana.blockAt(p.offset(0, -1, 0));
    if (!block || block.name === 'air') throw new Error('sin suelo');
    const t0 = Date.now();
    await ana.dig(block);
    const ms = Date.now() - t0;
    console.log(`     ${block.name} roto en ${ms} ms`);
    return ana.blockAt(p.offset(0, -1, 0)).name !== block.name && ms > 200;
  });
}


try {
  await flatCreative();
  await survival();
} catch (e) {
  check(false, `error inesperado: ${e.stack || e.message}`);
}

const failed = summary();
await finish(failed ? 1 : 0);
