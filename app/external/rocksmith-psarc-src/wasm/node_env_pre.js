// Node test harness: forward the host process environment into the wasm runtime so
// getenv() sees RS_WEM_SMOKE_KEEP / TMPDIR etc. (Emscripten does not do this by default.)
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  if (typeof process !== "undefined" && process.env) {
    for (var k in process.env) {
      ENV[k] = process.env[k];
    }
  }
});
