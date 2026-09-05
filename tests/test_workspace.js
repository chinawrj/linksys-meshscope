const test = require('node:test');
const assert = require('node:assert/strict');
const { orderedNodes, visibleNodes, needsAttention, clientMatchesNode, graphScale } = require('../mesh_web/workspace.js');

const nodes = [
  { id: 'child', parentId: 'parent', name: 'Attic', online: true },
  { id: 'root', name: 'Main', isAuthority: true, online: true },
  { id: 'offline', name: 'Garage', online: false },
  { id: 'parent', parentId: 'root', name: 'Office', model: 'MX5300', online: true },
  { id: 'sibling', parentId: 'root', name: 'Study', online: true },
];

test('Fit scales the full graph to phone bounds without an arbitrary 10% floor', () => {
  assert.equal(graphScale(1850,1800,352,525,true), 352/1850);
  assert.equal(graphScale(20000,1800,352,525,true), 352/20000);
  assert.equal(graphScale(1000,5000,352,525,true), 525/5000);
  assert.equal(graphScale(100,100,352,525,true), 1);
  assert.equal(graphScale(1850,1800,352,525,false), 1);
});
test('hidden/empty/invalid graph sizes never produce an invalid CSS scale', () => {
  for (const bad of [0,-1,NaN,Infinity,undefined]) {
    assert.equal(graphScale(bad,100,352,525,true),1);
    assert.equal(graphScale(100,100,bad,525,true),1);
  }
});

test('node list keeps each parent subtree together and retains offline records', () => {
  assert.deepEqual(orderedNodes(nodes).map(n => n.id), ['root', 'parent', 'child', 'sibling', 'offline']);
  assert.equal(nodes[0].id, 'child', 'ordering must not mutate live topology');
});
test('bad parent data cannot hide orphan nodes or hang on a cycle', () => {
  const broken = [{id:'a', parentId:'b'}, {id:'b', parentId:'a'}, {id:'c', parentId:'missing'}];
  assert.deepEqual(new Set(orderedNodes(broken).map(n => n.id)), new Set(['a','b','c']));
});
test('search composes with attention filtering, without inventing a new parent', () => {
  const lock = { enabled:true, nodes:[{nodeId:'parent', status:'cooldown'}] };
  const result = visibleNodes(nodes, {query:'mx5300', attention:true, lock});
  assert.equal(result.length, 1);
  assert.equal(result[0].parentId, 'root');
  assert.equal(visibleNodes(nodes, {query:'not-a-node'}).length, 0);
});
test('attention includes current recovery problems, not historical success', () => {
  const node = { id:'child', online:true, quality:{tone:'good'} };
  assert.equal(needsAttention(node, {}, {nodeHealth:[{childId:'child', state:'recovered', consecutiveFailures:0, totalFailures:12}]}), false);
  assert.equal(needsAttention(node, {}, {operation:{childId:'child', state:'verified'}}), false);
  assert.equal(needsAttention(node, {}, {operation:{childId:'child', state:'failed'}}), true);
  for (const status of ['cooldown','confirming','parent-offline','data-unavailable']) {
    assert.equal(needsAttention(node, {enabled:true,nodes:[{nodeId:'child',status}]}), true);
  }
  assert.equal(needsAttention(node, {enabled:false,nodes:[{nodeId:'child',status:'cooldown'}]}), false);
});
test('wired nodes do not resurrect stale wireless recovery warnings', () => {
  const wired = { id:'child', online:true, connectionType:'Ethernet' };
  const health = {nodeHealth:[{childId:'child',state:'blocked',consecutiveFailures:3}]};
  assert.equal(needsAttention(wired, {enabled:true,nodes:[{nodeId:'child',status:'wired-manual'}]}, health), false);
  assert.equal(needsAttention({...wired,online:false}, {}, health), true);
});
test('client node filter supports both backend schemas and case-insensitive UUIDs', () => {
  assert.equal(clientMatchesNode({parentId:'ABC'}, 'abc'), true);
  assert.equal(clientMatchesNode({nodeId:'abc'}, 'ABC'), true);
  assert.equal(clientMatchesNode({nodeId:'abc'}, ''), true);
  assert.equal(clientMatchesNode({nodeId:'other'}, 'abc'), false);
  assert.equal(clientMatchesNode({}, 'abc'), false);
});
