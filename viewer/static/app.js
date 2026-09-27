// The page owns the chrome and the text; the wasm module owns the canvas.
// Everything the panel says about a FASM line comes from the server, which
// resolved it with the assembler's own database -- nothing here re-derives
// a bit position.

import init, { create_viewer, legend } from "./bundle.js";
import { PIPELINE, SOURCES, TERMS } from "./reference.js";

const EXAMPLES = [
  "CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[63:32]=32'b00000000000000000000000000000100",
  "INT_L_X32Y38.BYP_ALT0.FAN_BOUNCE2",
  "CLBLL_L_X2Y100.SLICEL_X0.AFF.ZINI",
  "CLBLM_R_X33Y38.SLICEM_X0.WA7USED",
];

const dom = {
  family: document.getElementById("family"),
  part: document.getElementById("part"),
  load: document.getElementById("load"),
  fasm: document.getElementById("fasm"),
  resolve: document.getElementById("resolve"),
  clear: document.getElementById("clear"),
  parseStatus: document.getElementById("parse-status"),
  examples: document.getElementById("examples"),
  legend: document.getElementById("legend"),
  tileTypes: document.getElementById("tile-types"),
  typeSearch: document.getElementById("type-search"),
  labels: document.getElementById("labels"),
  tabChanges: document.getElementById("tab-changes"),
  tabReference: document.getElementById("tab-reference"),
  paneChanges: document.getElementById("pane-changes"),
  paneReference: document.getElementById("pane-reference"),
  reference: document.getElementById("reference"),
  canvas: document.getElementById("canvas"),
  tooltip: document.getElementById("tooltip"),
  stageStatus: document.getElementById("stage-status"),
  decode: document.getElementById("decode"),
};

const state = {
  viewer: null,
  families: [],
  family: null,
  part: null,
  // Every tile type of the loaded part, as the renderer colours them.
  tileTypes: [],
  typeFilter: null,
  charWidth: 0,
  needsRender: true,
};

function setStageStatus(text) {
  dom.stageStatus.textContent = text;
}

function setParseStatus(text, isError = false) {
  dom.parseStatus.textContent = text;
  dom.parseStatus.classList.toggle("error", isError);
}

/** Fetches and returns the response body as text, throwing the server's own
 *  error message when it reports one. */
async function fetchText(url, options) {
  const response = await fetch(url, options);
  const body = await response.text();
  if (!response.ok) {
    let message = `${response.status} ${response.statusText}`;
    try {
      const parsed = JSON.parse(body);
      if (parsed.error) message = parsed.error;
    } catch {
      // Not JSON; the status line is the best there is.
    }
    throw new Error(message);
  }
  return body;
}

function requestRender() {
  state.needsRender = true;
}

function resizeCanvas() {
  const ratio = window.devicePixelRatio || 1;
  const rect = dom.canvas.getBoundingClientRect();
  const width = Math.max(1, Math.round(rect.width * ratio));
  const height = Math.max(1, Math.round(rect.height * ratio));
  if (dom.canvas.width !== width || dom.canvas.height !== height) {
    dom.canvas.width = width;
    dom.canvas.height = height;
    if (state.viewer) state.viewer.resize(width, height);
    requestRender();
    return true;
  }
}

/** Canvas pixel coordinates, which are physical pixels, from a pointer
 *  event, whose coordinates are CSS pixels. */
function canvasPoint(event) {
  const ratio = window.devicePixelRatio || 1;
  const rect = dom.canvas.getBoundingClientRect();
  return {
    x: (event.clientX - rect.left) * ratio,
    y: (event.clientY - rect.top) * ratio,
  };
}

function renderLegend() {
  const entries = JSON.parse(legend());
  dom.legend.replaceChildren(
    ...entries.map(({ label, color }) => {
      const item = document.createElement("li");
      const swatch = document.createElement("span");
      swatch.className = "swatch";
      swatch.style.background = color;
      item.append(swatch, document.createTextNode(label));
      return item;
    }),
  );
}

function renderExamples() {
  dom.examples.replaceChildren(
    ...EXAMPLES.map((line) => {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = line;
      button.addEventListener("click", () => {
        dom.fasm.value = line;
        resolve();
      });
      item.append(button);
      return item;
    }),
  );
}

async function loadFamilies() {
  const body = await fetchText("/api/families");
  state.families = JSON.parse(body).families;
  dom.family.replaceChildren(
    ...state.families.map((family) => new Option(family.name, family.name)),
  );
  dom.family.addEventListener("change", populateParts);
  populateParts();
}

function populateParts() {
  const family = state.families.find((entry) => entry.name === dom.family.value);
  const parts = family ? family.parts : [];
  dom.part.replaceChildren(
    ...parts.map((part) => new Option(part.name, part.name)),
  );
}

/** The legend of tile types actually present in the loaded part. Clicking
 *  one isolates it on the fabric, which is the quickest way to see where a
 *  type sits. */
function renderTileTypes() {
  const needle = dom.typeSearch.value.trim().toUpperCase();
  const shown = state.tileTypes.filter(
    (entry) => !needle || entry.name.includes(needle),
  );
  dom.tileTypes.replaceChildren(
    ...shown.map((entry) => {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.setAttribute(
        "aria-pressed",
        String(state.typeFilter === entry.index),
      );
      button.title = `${entry.name} \u2014 ${entry.kind}, ${entry.count} tiles`;

      const swatch = document.createElement("span");
      swatch.className = "swatch";
      swatch.style.background = entry.color;

      const name = element("span", "name", entry.name);
      const count = element("span", "count", String(entry.count));

      button.append(swatch, name, count);
      button.addEventListener("click", () => {
        // Clicking the isolated type again shows the whole fabric.
        state.typeFilter =
          state.typeFilter === entry.index ? null : entry.index;
        state.viewer.set_type_filter(
          state.typeFilter === null ? undefined : state.typeFilter,
        );
        renderTileTypes();
        requestRender();
      });
      item.append(button);
      return item;
    }),
  );
}

async function loadPart() {
  const family = dom.family.value;
  const part = dom.part.value;
  if (!family || !part) return;
  setStageStatus(`loading ${family}/${part}…`);
  dom.load.disabled = true;
  try {
    const body = await fetchText(`/api/parts/${family}/${part}/grid`);
    state.viewer.set_grid(body);
    state.family = family;
    state.part = part;
    // The grid is framed against the canvas as it is now, which is only
    // right once the element has been laid out.
    resizeCanvas();
    state.viewer.fit();
    const grid = JSON.parse(body);
    state.typeFilter = null;
    state.tileTypes = JSON.parse(state.viewer.tile_types()).sort((left, right) =>
      left.name.localeCompare(right.name),
    );
    renderTileTypes();
    setStageStatus(
      `${part} · ${grid.names.length} tiles · ` +
        `${state.tileTypes.length} tile types · ` +
        `grid ${grid.width}×${grid.height}`,
    );
    requestRender();
  } catch (error) {
    setStageStatus(`could not load: ${error.message}`);
  } finally {
    dom.load.disabled = false;
  }
}

async function resolve() {
  if (!state.part) return;
  const text = dom.fasm.value.trim();
  if (!text) {
    state.viewer.clear_evaluation();
    setParseStatus("");
    renderDecode(null);
    requestRender();
    return;
  }
  setParseStatus("resolving…");
  try {
    const body = await fetchText(
      `/api/parts/${state.family}/${state.part}/eval`,
      {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ text }),
      },
    );
    state.viewer.set_evaluation(body);
    state.viewer.frame_touched();
    const evaluation = JSON.parse(body);
    renderDecode(evaluation);
    const diagnostics = evaluation.diagnostics.trim();
    setParseStatus(
      diagnostics || `parsed: ${evaluation.severity}`,
      evaluation.severity === "error",
    );
    // Open the first tile that actually changed, so the bit block is
    // showing the thing the line did without a second click.
    const first = evaluation.features.find(
      (feature) => feature.outcome === "bits" && feature.tile,
    );
    if (first) await selectTile(first.tile, { recenter: false });
    requestRender();
  } catch (error) {
    setParseStatus(error.message, true);
  }
}

async function selectTile(name, { recenter = true } = {}) {
  if (!state.part) return;
  try {
    const body = await fetchText(
      `/api/parts/${state.family}/${state.part}/tiles/${encodeURIComponent(name)}`,
    );
    state.viewer.set_focus_tile(body);
    const detail = JSON.parse(body);
    if (recenter) state.viewer.focus_on(detail.index);
    renderTileCard(detail);
    requestRender();
  } catch (error) {
    setParseStatus(error.message, true);
  }
}

/* ---------- the decode panel ---------- */

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

const OUTCOME_TEXT = {
  bits: "This line sets configuration bits.",
  pseudo_pip:
    "This is a pseudo PIP. Vivado shows it as a PIP, but the database has " +
    "no configuration bits for it. The bitstream does not change.",
  zero_value:
    "The value is zero. Only a bit that is 1 selects a feature bit, so the " +
    "tool reads nothing.",
  error: "The database does not have this feature.",
};

/** prjxray names an interconnect feature `<destination>.<source>`: the
 *  database stores a block of bits per destination signal, and one pattern
 *  within it per source that can drive it. It is the one piece of the
 *  notation a reader is likely to have backwards. */
function routingOf(feature) {
  const parts = feature.tile_feature.split(".");
  if (parts.length !== 2) return null;
  const kind = feature.tile_type || "";
  const isInterconnect =
    kind.startsWith("INT_") || kind === "INT" || kind.includes("INT_INTERFACE");
  if (!isInterconnect) return null;
  return { destination: parts[0], source: parts[1] };
}

/** `SLICEM_X0.ALUT.INIT` and friends. */
function lutOf(feature) {
  const match = feature.tile_feature.match(/^(.*)\.([A-D])LUT\.INIT$/);
  if (!match) return null;
  return { site: match[1], lut: match[2] };
}

function renderLut(feature, lut) {
  const section = document.createElement("div");
  section.append(
    element("p", "caption", `${lut.site} ${lut.lut}LUT · INIT[63:0]`),
  );
  const value = BigInt(feature.value_bits);
  const grid = element("div", "lut");
  const start = feature.start_bit;
  const end = feature.start_bit + feature.width;
  for (let bit = 63; bit >= 0; bit -= 1) {
    const cell = document.createElement("span");
    const addressed = bit >= start && bit < end;
    if (addressed) {
      cell.classList.add("touched");
      // The value bits are indexed from the assignment's start bit.
      if ((value >> BigInt(bit - start)) & 1n) cell.classList.add("one");
    }
    cell.title = `INIT[${bit}]`;
    grid.append(cell);
  }
  section.append(grid);
  section.append(
    element(
      "p",
      "explain",
      `This line covers INIT[${end - 1}:${start}]. Those are the squares ` +
        `with an outline. A bright square is a bit that the line sets to ` +
        `1. The tool reads each one from the segbits file, as ` +
        `${feature.tile_type}.${feature.tile_feature}[<bit>]. Bits outside ` +
        `the range keep their earlier value.`,
    ),
  );
  return section;
}

function renderBitsTable(feature) {
  const table = document.createElement("table");
  const head = document.createElement("tr");
  for (const label of ["frame", "word", "bit", "col", "row", ""]) {
    head.append(element("th", null, label));
  }
  table.append(head);
  for (const bit of feature.frame_bits) {
    const row = document.createElement("tr");
    row.append(
      element("td", null, `0x${bit.frame_address.toString(16).padStart(8, "0")}`),
      element("td", null, String(bit.word)),
      element("td", null, String(bit.index)),
      element("td", null, String(bit.word_column)),
      element("td", null, String(bit.word_bit)),
      element("td", bit.value ? "set" : "clear", bit.value ? "set" : "clear"),
    );
    table.append(row);
  }
  return table;
}

function renderFeature(feature) {
  const card = element("div", "feature");

  const head = element("div", "feature-head");
  const name = element("div", "feature-name");
  if (feature.tile) {
    const link = element("a", "tile-link", feature.tile);
    link.addEventListener("click", () => selectTile(feature.tile));
    name.append(link, document.createTextNode(`.${feature.tile_feature}`));
  } else {
    name.textContent = feature.feature;
  }
  head.append(name);
  card.append(head);

  const body = element("div", "feature-body");
  const tags = element("div", "tags");
  if (feature.tile_type) tags.append(element("span", "tag", feature.tile_type));
  tags.append(
    element(
      "span",
      `tag ${feature.outcome === "bits" ? "bits" : feature.outcome === "error" ? "error" : ""}`,
      feature.outcome === "bits"
        ? `${feature.frame_bits.length} bits`
        : feature.outcome.replace("_", " "),
    ),
  );
  tags.append(element("span", "tag", `line ${feature.line}`));
  body.append(tags);

  const explain = element("p", "explain", OUTCOME_TEXT[feature.outcome] ?? "");
  if (feature.outcome === "pseudo_pip") {
    explain.append(document.createTextNode(" "));
    explain.append(termLink("pseudo-pip", "What is a pseudo pip?"));
  }
  body.append(explain);
  if (feature.error) {
    body.append(element("p", "explain", feature.error));
  }

  const routing = routingOf(feature);
  if (routing) {
    const explain = element("p", "explain");
    explain.append(
      document.createTextNode("Interconnect. The database writes a "),
      termLink("pip", "pip"),
      document.createTextNode(" as "),
      element("strong", null, "destination.source"),
      document.createTextNode(", so this routes "),
      element("strong", null, routing.source),
      document.createTextNode(" → "),
      element("strong", null, routing.destination),
      document.createTextNode(
        ". The bits below are the pattern that selects that source. All " +
          "of them are important. A bit with a ! in front must be 0.",
      ),
    );
    body.append(explain);
  }

  const lut = lutOf(feature);
  if (lut) body.append(renderLut(feature, lut));

  if (feature.frame_bits.length > 0) {
    const caption = element("p", "caption");
    caption.append(termLink("frame-address", "frame bits"));
    body.append(caption);
    body.append(renderBitsTable(feature));
  }
  card.append(body);
  return card;
}

function renderDecode(evaluation) {
  if (!evaluation || evaluation.features.length === 0) {
    dom.decode.replaceChildren(
      element(
        "p",
        "empty",
        "Type a FASM line and click Resolve. The viewer draws a ring " +
          "around each tile that changes. It also lights the bits that " +
          "change, in the square in the corner.",
      ),
    );
    return;
  }
  dom.decode.replaceChildren(...evaluation.features.map(renderFeature));
}

function renderTileCard(detail) {
  const card = element("div", "feature");
  const head = element("div", "feature-head");
  head.append(element("div", "feature-name", detail.tile.name));
  card.append(head);

  const body = element("div", "feature-body");
  const tags = element("div", "tags");
  tags.append(element("span", "tag", detail.tile.tile_type));
  if (detail.tile.clock_region) {
    tags.append(element("span", "tag", `region ${detail.tile.clock_region}`));
  }
  body.append(tags);

  // The three coordinate spaces are the single most confusing thing about
  // reading a fabric, so the tile spells its own out.
  const coordCaption = element("p", "caption");
  coordCaption.append(termLink("coordinates", "coordinates"));
  body.append(coordCaption);
  const coords = document.createElement("table");
  const nameXY = detail.tile.name.match(/_X(\d+)Y(\d+)$/);
  const rows = [
    [
      "grid",
      `${detail.tile.grid_x}, ${detail.tile.grid_y}`,
      "the place on the chip. This viewer draws it.",
    ],
  ];
  if (nameXY) {
    rows.push([
      "tile name",
      `X${nameXY[1]}Y${nameXY[2]}`,
      "counted inside this tile type. Other types use the same numbers.",
    ]);
  }
  if (detail.sites.length > 0) {
    const siteXY = detail.sites[0].name.match(/_X(\d+)Y(\d+)$/);
    if (siteXY) {
      rows.push([
        "site",
        `X${siteXY[1]}Y${siteXY[2]}`,
        "counted inside the site type, across the whole chip.",
      ]);
    }
  }
  for (const [label, value, note] of rows) {
    const row = document.createElement("tr");
    row.append(
      element("td", null, label),
      element("td", "coord", value),
      element("td", "note", note),
    );
    coords.append(row);
  }
  body.append(coords);

  if (detail.sites.length > 0) {
    body.append(element("p", "caption", "sites"));
    const table = document.createElement("table");
    for (const site of detail.sites) {
      const row = document.createElement("tr");
      row.append(element("td", null, site.name), element("td", null, site.site_type));
      table.append(row);
    }
    body.append(table);
  }

  if (detail.tile.bits_blocks.length === 0) {
    body.append(
      element(
        "p",
        "explain",
        "This tile has no configuration bits. There is no bit block to " +
          "show.",
      ),
    );
  } else {
    const windowCaption = element("p", "caption");
    windowCaption.append(termLink("bit-block", "bit block"));
    body.append(windowCaption);
    const table = document.createElement("table");
    const head = document.createElement("tr");
    for (const label of ["bus", "base", "frames", "offset", "words"]) {
      head.append(element("th", null, label));
    }
    table.append(head);
    for (const block of detail.tile.bits_blocks) {
      const row = document.createElement("tr");
      row.append(
        element("td", null, block.bus),
        element("td", null, `0x${block.base_address.toString(16).padStart(8, "0")}`),
        element("td", null, String(block.frames)),
        element("td", null, String(block.offset)),
        element("td", null, String(block.words)),
      );
      table.append(row);
    }
    body.append(table);
    body.append(
      element(
        "p",
        "explain",
        "The square in the corner of the fabric draws this bit block. " +
          "It has one column for each frame and one row for each bit. " +
          "The first row is the offset above. A bright cell is a bit that " +
          "the line sets. A tile with two bit blocks shows the block that " +
          "holds the bits.",
      ),
    );
  }
  card.append(body);
  dom.decode.prepend(card);
}

/* ---------- the reference ---------- */

/** Renders one block of reference content. Everything is inserted as text,
 *  never as markup. */
function renderBlock(block) {
  if (block.p) return element("p", null, block.p);
  if (block.code) return element("pre", null, block.code);
  if (block.list) {
    const list = document.createElement("ul");
    list.append(...block.list.map((item) => element("li", null, item)));
    return list;
  }
  if (block.table) {
    const table = document.createElement("table");
    const head = document.createElement("tr");
    head.append(...block.table.head.map((cell) => element("th", null, cell)));
    table.append(head);
    for (const cells of block.table.rows) {
      const row = document.createElement("tr");
      row.append(...cells.map((cell) => element("td", null, cell)));
      table.append(row);
    }
    return table;
  }
  if (block.links) {
    const list = element("ul", "sources-list");
    for (const [label, href] of block.links) {
      const item = document.createElement("li");
      const link = element("a", null, label);
      link.href = href;
      link.target = "_blank";
      link.rel = "noreferrer noopener";
      item.append(link);
      list.append(item);
    }
    return list;
  }
  return document.createTextNode("");
}

/** A link into the reference, for use from the decode panel. */
function termLink(id, text) {
  const link = element("a", "term", text);
  link.addEventListener("click", () => showTerm(id));
  return link;
}

/** Opens the reference at one term. */
function showTerm(id) {
  selectTab("reference");
  const entry = document.getElementById(`term-${id}`);
  if (!entry) return;
  entry.scrollIntoView({ block: "start", behavior: "smooth" });
  // A brief highlight, so it is clear which entry was jumped to.
  entry.classList.add("flash");
  setTimeout(() => entry.classList.remove("flash"), 1600);
}

function selectTab(which) {
  const reference = which === "reference";
  dom.tabChanges.setAttribute("aria-selected", String(!reference));
  dom.tabReference.setAttribute("aria-selected", String(reference));
  dom.paneChanges.hidden = reference;
  dom.paneReference.hidden = !reference;
}

function renderReference() {
  const byId = new Map(TERMS.map((term) => [term.id, term]));
  const nodes = [];

  const pipeline = element("div", "entry");
  pipeline.id = `term-${PIPELINE.id}`;
  pipeline.append(element("h3", null, PIPELINE.title));
  pipeline.append(...PIPELINE.body.map(renderBlock));
  nodes.push(pipeline);

  for (const term of TERMS) {
    const entry = element("div", "entry");
    entry.id = `term-${term.id}`;
    entry.append(element("h3", null, term.term));
    if (term.aka) entry.append(element("p", "aka", term.aka.join(" · ")));
    entry.append(...term.body.map(renderBlock));
    if (term.see) {
      const see = element("p", "see");
      see.append(document.createTextNode("See also: "));
      term.see.forEach((id, index) => {
        if (index > 0) see.append(document.createTextNode(", "));
        const target = byId.get(id);
        see.append(termLink(id, target ? target.term.toLowerCase() : id));
      });
      entry.append(see);
    }
    nodes.push(entry);
  }

  const sources = element("div", "entry");
  sources.id = `term-${SOURCES.id}`;
  sources.append(element("h3", null, SOURCES.title));
  sources.append(...SOURCES.body.map(renderBlock));
  nodes.push(sources);

  dom.reference.replaceChildren(...nodes);
}

/* ---------- interaction ---------- */

function installPointerHandlers() {
  let dragging = false;
  let moved = 0;
  let last = null;

  dom.canvas.addEventListener("pointerdown", (event) => {
    dragging = true;
    moved = 0;
    last = canvasPoint(event);
    try {
      // Keeps a drag alive when the pointer leaves the canvas. Not every
      // pointer can be captured, and a drag that cannot be captured still
      // works, so this must not abort the handler.
      dom.canvas.setPointerCapture(event.pointerId);
    } catch {
      // Ignored on purpose; see above.
    }
    dom.canvas.classList.add("dragging");
  });

  dom.canvas.addEventListener("pointermove", (event) => {
    const point = canvasPoint(event);
    if (dragging && last) {
      const dx = point.x - last.x;
      const dy = point.y - last.y;
      moved += Math.abs(dx) + Math.abs(dy);
      state.viewer.pan(dx, dy);
      requestRender();
    } else {
      showTooltip(event, point);
    }
    last = point;
  });

  const endDrag = (event) => {
    if (!dragging) return;
    dragging = false;
    dom.canvas.classList.remove("dragging");
    // A press that did not travel is a click, not a drag.
    if (moved < 6) {
      const point = canvasPoint(event);
      const index = state.viewer.pick(point.x, point.y);
      if (index !== undefined) {
        const name = state.viewer.tile_name(index);
        if (name) selectTile(name, { recenter: false });
      } else {
        state.viewer.clear_focus();
        requestRender();
      }
    }
  };
  dom.canvas.addEventListener("pointerup", endDrag);
  dom.canvas.addEventListener("pointercancel", () => {
    dragging = false;
    dom.canvas.classList.remove("dragging");
  });

  dom.canvas.addEventListener("pointerleave", () => {
    dom.tooltip.hidden = true;
  });

  dom.canvas.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      const point = canvasPoint(event);
      const factor = Math.exp(-event.deltaY * 0.0015);
      state.viewer.zoom_at(factor, point.x, point.y);
      requestRender();
    },
    { passive: false },
  );
}

function showTooltip(event, point) {
  const index = state.viewer.pick(point.x, point.y);
  if (index === undefined) {
    dom.tooltip.hidden = true;
    return;
  }
  const info = JSON.parse(state.viewer.tile_info(index) ?? "null");
  if (!info) {
    dom.tooltip.hidden = true;
    return;
  }
  dom.tooltip.replaceChildren(
    document.createTextNode(info.name),
    element("span", "dim", `  ${info.type}`),
  );
  dom.tooltip.hidden = false;
  const rect = dom.canvas.getBoundingClientRect();
  dom.tooltip.style.left = `${event.clientX - rect.left + 14}px`;
  dom.tooltip.style.top = `${event.clientY - rect.top + 14}px`;
}

/** The advance width of one character of the label font, in CSS pixels.
 *  The renderer needs it to decide whether a name fits in its cell, and
 *  only the page knows what the stylesheet ended up using. */
function labelCharWidth() {
  if (state.charWidth) return state.charWidth;
  const probe = document.createElement("span");
  probe.textContent = "M".repeat(40);
  probe.style.cssText =
    "position:absolute;visibility:hidden;white-space:pre;" +
    "font-family:var(--mono);font-size:10px";
  dom.labels.append(probe);
  const width = probe.getBoundingClientRect().width / 40;
  probe.remove();
  // Fall back to a sane monospace advance if the measurement is unusable,
  // which happens when the element is not laid out yet.
  state.charWidth = width > 0.5 ? width : 6;
  return state.charWidth;
}

/** Tile names, drawn as HTML over the canvas. The renderer returns only the
 *  labels whose text fits the cell it names, so they never overrun into the
 *  neighbouring tile; a cell too small for the full name may still get the
 *  tile type on its own. */
function renderLabels() {
  const entries = JSON.parse(
    // The type line is 9px against the name's 10px.
    state.viewer.labels(labelCharWidth(), 0.9, window.devicePixelRatio || 1),
  );
  if (entries.length === 0) {
    if (dom.labels.childElementCount > 0) dom.labels.replaceChildren();
    return;
  }
  const ratio = window.devicePixelRatio || 1;
  dom.labels.replaceChildren(
    ...entries.map((entry) => {
      const span = document.createElement("span");
      span.style.left = `${entry.x / ratio}px`;
      span.style.top = `${entry.y / ratio}px`;
      if (entry.name) span.append(document.createTextNode(entry.name));
      if (entry.type) span.append(element("em", null, entry.type));
      return span;
    }),
  );
}

function frame() {
  if (state.needsRender && state.viewer) {
    state.needsRender = false;
    try {
      // A resize can leave the surface without a frame to draw into; the
      // renderer says so instead of leaving a stale or blank canvas.
      if (!state.viewer.render()) state.needsRender = true;
      renderLabels();
    } catch (error) {
      setStageStatus(`render failed: ${error}`);
    }
  }
  requestAnimationFrame(frame);
}

async function main() {
  renderExamples();
  try {
    await init();
  } catch (error) {
    setStageStatus(`could not load the renderer: ${error}`);
    return;
  }
  renderLegend();
  renderReference();
  dom.tabChanges.addEventListener("click", () => selectTab("changes"));
  dom.tabReference.addEventListener("click", () => selectTab("reference"));
  resizeCanvas();
  try {
    state.viewer = await create_viewer(dom.canvas);
  } catch (error) {
    setStageStatus(
      `no GPU: ${error}. The viewer needs WebGPU or WebGL2.`,
    );
    return;
  }
  installPointerHandlers();
  new ResizeObserver(resizeCanvas).observe(dom.canvas);
  window.addEventListener("keydown", (event) => {
    if (event.key === "f" && document.activeElement !== dom.fasm) {
      state.viewer.fit();
      requestRender();
    }
  });
  dom.typeSearch.addEventListener("input", renderTileTypes);
  dom.load.addEventListener("click", loadPart);
  dom.resolve.addEventListener("click", resolve);
  dom.clear.addEventListener("click", () => {
    dom.fasm.value = "";
    state.viewer.clear_evaluation();
    state.viewer.clear_focus();
    setParseStatus("");
    renderDecode(null);
    requestRender();
  });
  dom.fasm.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && (event.metaKey || event.ctrlKey)) resolve();
  });
  requestAnimationFrame(frame);

  await loadFamilies();
  await loadPart();
}

main();
