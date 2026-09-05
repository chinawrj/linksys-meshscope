// Offline reproduction of two v0.8.0 canvas failure paths, without a router or
// browser process. Run: node tests/reproduce_v080_canvas.js
const { execFileSync } = require('node:child_process');
const vm = require('node:vm');
const source = execFileSync('git', ['show', 'v0.8.0:mesh_web/app.js'], { encoding: 'utf8' });
const renderer = source.slice(source.indexOf('function startTopologyCanvas('), source.indexOf('\nfunction filteredClients('));

function fixture(contextAvailable = true) {
  let next = 0;
  const pending = new Map();
  const context = new Proxy({}, { get: () => () => {} });
  const canvas = { style: {}, getContext: () => contextAvailable ? context : null };
  const scope = {
    $: () => canvas, state: { topologyAnimationFrame: null },
    workspace: { graphVisible: () => true },
    window: { devicePixelRatio: 3, matchMedia: () => ({ matches: false }) },
    requestAnimationFrame: (callback) => { pending.set(++next, callback); return next; },
    cancelAnimationFrame: (id) => pending.delete(id),
  };
  vm.createContext(scope);
  vm.runInContext(renderer, scope);
  return { scope, canvas, pending };
}

const race = fixture();
// renderTopology queues its start callback without retaining the returned ID.
for (let i = 0; i < 2; i++) race.scope.requestAnimationFrame(() => race.scope.startTopologyCanvas([], 1850, 1800));
for (const [id, callback] of [...race.pending]) { race.pending.delete(id); callback(); }
const started = race.pending.size;
race.scope.cancelAnimationFrame(race.scope.state.topologyAnimationFrame);
console.log(JSON.stringify({
  overlappingRenders: 2, animationLoopsStarted: started,
  loopsLeftAfterTeardown: race.pending.size,
  bytesPerCanvas: race.canvas.width * race.canvas.height * 4,
}));
try {
  fixture(false).scope.startTopologyCanvas([], 1850, 1800);
} catch (error) {
  console.log(JSON.stringify({ contextAllocationFailure: error.name, message: error.message }));
}
