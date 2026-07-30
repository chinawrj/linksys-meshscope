const assert = require("node:assert/strict");
const test = require("node:test");
const { compute } = require("../mesh_web/topology-layout.js");

const nodes = [
  { id: "main", name: "Main", online: true, isAuthority: true },
  { id: "big", name: "Atrium", online: true, parentId: "main", band: "5GH" },
  { id: "door", name: "Office", online: true, parentId: "main", band: "5GL" },
  { id: "yard", name: "Patio", online: true, parentId: "main", band: "5GH" },
  { id: "parent", name: "Bedroom", online: true, parentId: "door", band: "5GH" },
  { id: "road", name: "Garage", online: true, parentId: "big", band: "5GL" },
];

test("orders descendants by their parent subtree so second-layer edges do not cross", () => {
  const layout = compute(nodes);
  const byId = new Map(layout.positions.map((node) => [node.id, node]));

  assert.ok(byId.get("big").y < byId.get("door").y);
  assert.ok(byId.get("road").y < byId.get("parent").y);
  assert.equal(byId.get("big").y, byId.get("road").y);
  assert.equal(byId.get("door").y, byId.get("parent").y);
});

test("keeps every online node and preserves parent relationships", () => {
  const layout = compute(nodes);
  assert.equal(layout.positions.length, nodes.length);
  assert.equal(layout.edges.length, nodes.length - 1);
  assert.ok(layout.width >= 900);
  assert.ok(layout.height > 300);
  assert.equal(
    layout.edges.find((edge) => edge.target.id === "parent").source.id,
    "door",
  );
  assert.equal(
    layout.edges.find((edge) => edge.target.id === "road").source.id,
    "big",
  );
});
