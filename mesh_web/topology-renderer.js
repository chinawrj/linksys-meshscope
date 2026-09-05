(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.MeshTopologyRenderer = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const number = (value) => typeof value === "number" && Number.isFinite(value);
  const point = (value) => value && number(value.x) && number(value.y);
  const coordinate = (value) => Math.round(value * 100) / 100;
  const bands = new Map([["5GH", "high"], ["5GL", "low"], ["Ethernet", "wired"]]);

  function curve(from, to) {
    if (!point(from) || !point(to)) return null;
    const control = Math.max(42, (to.x - from.x) * .48);
    return `M ${coordinate(from.x)} ${coordinate(from.y)} C ${coordinate(from.x + control)} ${coordinate(from.y)}, ${coordinate(to.x - control)} ${coordinate(to.y)}, ${coordinate(to.x)} ${coordinate(to.y)}`;
  }

  // Vector links need no full-diagram DPR-scaled bitmap, no getContext(), and
  // no JS animation loop. Browser pinch zoom keeps paths and text sharp.
  function svg(edges, width, height) {
    if (!number(width) || !number(height) || width <= 0 || height <= 0) return "";
    const groups = edges.map((edge) => {
      const d = curve(edge.sourcePoint, edge.targetPoint);
      if (!d) return "";
      const band = bands.get(edge.band) || "wan";
      const kind = ["desired", "current"].includes(edge.kind) ? edge.kind : "live";
      const ports = [edge.sourcePoint, edge.targetPoint].map((p) =>
        `<circle class="topology-port" cx="${coordinate(p.x)}" cy="${coordinate(p.y)}" r="4"/>`).join("");
      return `<g class="topology-link link-${band} link-${kind}"><path class="link-glow" d="${d}"/><path class="link-line" d="${d}"/>${ports}</g>`;
    }).join("");
    // Presentation attributes work with the embedded server's strict CSP.
    // No untrusted names, IDs, bands, or HTML are interpolated into SVG.
    return `<svg class="topology-links" xmlns="http://www.w3.org/2000/svg" width="${coordinate(width)}" height="${coordinate(height)}" viewBox="0 0 ${coordinate(width)} ${coordinate(height)}" aria-hidden="true" focusable="false">${groups}</svg>`;
  }

  return { curve, svg };
});
