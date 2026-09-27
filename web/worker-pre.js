// Web Worker de MC-WEB (build web sin hilos). Recibe trabajos de la página, los hace con el
// módulo mcweb-worker.wasm y devuelve los bytes (transfiriendo el ArrayBuffer, sin copiarlo).
var Module = typeof Module !== 'undefined' ? Module : {};
(function () {
  let ready = false;
  const queue = [];

  function copyIn(buffer) {
    const u8 = new Uint8Array(buffer);
    const p = Module._malloc(Math.max(1, u8.length));
    HEAPU8.set(u8, p);
    return [p, u8.length];
  }
  function takeOut(ptr, lenPtr) {
    const len = HEAP32[lenPtr >> 2];
    const out = HEAPU8.slice(ptr, ptr + len);
    Module._free(ptr);
    return out;
  }

  function handle(m) {
    if (m.type === 'init') {
      let p = 0, n = 0;
      if (m.bundle) [p, n] = copyIn(m.bundle);
      const layers = Module._mcw_worker_init(m.seedLo >>> 0, m.seedHi >>> 0, p, n);
      if (p) Module._free(p);
      postMessage({ type: 'ready', layers });
      return;
    }
    const lenPtr = Module._malloc(4);
    let out;
    if (m.type === 'gen') {
      out = takeOut(Module._mcw_worker_generate(m.cx, m.cz, lenPtr), lenPtr);
    } else if (m.type === 'mesh') {
      const [p, n] = copyIn(m.input);
      out = takeOut(Module._mcw_worker_mesh(p, n, lenPtr), lenPtr);
      Module._free(p);
    }
    Module._free(lenPtr);
    postMessage({ type: m.type, id: m.id, buf: out.buffer }, [out.buffer]);
  }

  self.onmessage = (e) => {
    if (!ready) { queue.push(e.data); return; }
    try { handle(e.data); } catch (err) { postMessage({ type: 'error', id: e.data.id, message: String(err) }); }
  };
  Module.onRuntimeInitialized = () => {
    ready = true;
    for (const m of queue.splice(0)) {
      try { handle(m); } catch (err) { postMessage({ type: 'error', id: m.id, message: String(err) }); }
    }
  };
})();
