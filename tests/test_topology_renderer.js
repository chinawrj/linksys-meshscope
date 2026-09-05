const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { curve, svg } = require('../mesh_web/topology-renderer.js');
const edge = { sourcePoint: { x: 100, y: 200 }, targetPoint: { x: 500, y: 320 }, band: '5GH' };

test('vector renderer preserves curved links, endpoints and the complete graph extent', () => {
  assert.equal(curve(edge.sourcePoint, edge.targetPoint), 'M 100 200 C 292 200, 308 320, 500 320');
  const output = svg([edge], 1850, 1800);
  assert.match(output, /viewBox="0 0 1850 1800"/);
  assert.match(output, /link-high link-live/);
  assert.equal((output.match(/<circle /g) || []).length, 2);
  assert.match(output, /cx="500" cy="320"/);
});
test('all backhaul bands and current/desired preview paths remain distinguishable', () => {
  const output = svg(['5GH','5GL','Ethernet','WAN'].map(band => ({...edge,band})).concat([
    {...edge,kind:'current'}, {...edge,kind:'desired'},
  ]), 2000, 1200);
  for (const className of ['link-high','link-low','link-wired','link-wan','link-current','link-desired']) {
    assert.ok(output.includes(className));
  }
  assert.equal((output.match(/<g /g) || []).length, 6);
});
test('invalid samples cannot inject markup, non-finite coordinates, or script into SVG', () => {
  for (const bad of [NaN, Infinity, '10" onload="alert(1)']) {
    assert.equal(curve({x:bad,y:2},edge.targetPoint), null);
    assert.equal(svg([edge],bad,100), '');
  }
  const output = svg([{...edge,band:'<script>',kind:'" onclick="x'},{...edge,targetPoint:{x:NaN,y:4}}], 500, 400);
  assert.equal((output.match(/<g /g) || []).length, 1);
  assert.match(output, /link-wan link-live/);
  assert.doesNotMatch(output, /<script|onclick|NaN|style=/);
});
test('large/deep diagrams and repeated redraws allocate no canvas or animation callbacks', () => {
  const edges = Array.from({length:100}, (_,i) => ({...edge,sourcePoint:{x:i*456,y:i*330},targetPoint:{x:(i+1)*456,y:(i+1)*330}}));
  const expected = svg(edges, 47000, 34000);
  for(let i=0;i<250;i++) assert.equal(svg(edges,47000,34000), expected);
  assert.equal((expected.match(/<g /g) || []).length, 100);
  assert.doesNotMatch(expected, /canvas|animate|filter=/i);
  const renderer = fs.readFileSync(require.resolve('../mesh_web/topology-renderer.js'),'utf8');
  const app = fs.readFileSync(require.resolve('../mesh_web/app.js'),'utf8');
  assert.doesNotMatch(renderer, /requestAnimationFrame\(|\.getContext\(/);
  assert.doesNotMatch(app, /startTopologyCanvas|topologyAnimationFrame|getContext\("2d"\)/);
});
