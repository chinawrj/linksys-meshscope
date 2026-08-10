const assert = require("node:assert/strict");
const test = require("node:test");
const { compute } = require("../mesh_web/topology-layout.js");

const nodes = [
  { id: "main", name: "Main", online: true, isAuthority: true },
  { id: "big", name: "Atrium", online: true, parentId: "main", band: "5GH" },
  { id: "door", name: "Office", online: true, parentId: "main", band: "5GL" },
  { id: "yard", name: "Patio", online: true, parentId: "main", band: "5GH" },
  { id: "parent", name: "Bedroom", online: true, parentId: "door", band: "5GH", phyRateMbps: 1729.3, phyRateStale: false },
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
  assert.equal(
    layout.edges.find((edge) => edge.target.id === "parent").phyRateMbps,
    1729.3,
  );
});

test("uses the browser width, compacts safe gaps, and scrolls only when cards cannot fit", () => {
  const wide = compute(nodes, { availableWidth: 1600 });
  assert.equal(wide.width, 1600);
  assert.ok(wide.contentWidth <= wide.width);

  const compact = compute(nodes, { availableWidth: 1080 });
  assert.equal(compact.width, 1080);
  assert.ok(compact.columnGap >= 72);
  assert.ok(compact.contentWidth <= 1080);

  const narrow = compute(nodes, { availableWidth: 360 });
  assert.ok(narrow.contentWidth > 360);
  assert.equal(narrow.width, narrow.contentWidth);
});
