(function () {
  "use strict";

  const SVG_NS = "http://www.w3.org/2000/svg";
  const STANDARD_MODULES = ["base", "altrep", "shared", "runtime", "other"];
  const BACKEND_SUBCATEGORIES = [
    "core", "boundary", "character class", "collation", "encoding",
    "fixed", "io", "regex", "transform"
  ];
  const MAX_SEARCH_RESULTS = 60;
  const MAX_NEIGHBOR_NODES = 240;
  const MAX_DETAIL_ITEMS = 300;

  const els = {
    svg: document.getElementById("graph"),
    viewport: document.getElementById("viewport"),
    groups: document.getElementById("groups-layer"),
    edges: document.getElementById("edges-layer"),
    nodes: document.getElementById("nodes-layer"),
    status: document.getElementById("graph-status"),
    error: document.getElementById("data-error"),
    search: document.getElementById("search"),
    searchResults: document.getElementById("search-results"),
    moduleFilters: document.getElementById("module-filters"),
    edgeFilters: document.getElementById("edge-filters"),
    subcategoryLegend: document.getElementById("subcategory-legend"),
    viewLegendContent: document.getElementById("view-legend-content"),
    details: document.getElementById("detail-content"),
    unitsView: document.getElementById("units-view"),
    fitView: document.getElementById("fit-view"),
    zoomIn: document.getElementById("zoom-in"),
    zoomOut: document.getElementById("zoom-out")
  };

  const raw = window.CHARR_CODE_MAP;
  if (!raw || !Array.isArray(raw.entities) ||
      !Array.isArray(raw.relationships) || !Array.isArray(raw.unit_dependencies)) {
    showFatal("data.js did not define a valid window.CHARR_CODE_MAP object.");
    return;
  }

  const state = {
    metadata: raw.metadata || {},
    entities: raw.entities.map(normalizeEntity),
    relationships: raw.relationships.map(normalizeRelationship),
    unitDependencies: raw.unit_dependencies.map(normalizeUnitDependency),
    entityById: new Map(),
    entitiesByUnit: new Map(),
    incomingByEntity: new Map(),
    outgoingByEntity: new Map(),
    unitById: new Map(),
    incomingByUnit: new Map(),
    outgoingByUnit: new Map(),
    moduleEnabled: new Map(),
    edgeEnabled: new Map(),
    view: "units",
    selectedId: null,
    selectedKind: null,
    graph: { nodes: [], edges: [], groups: [], truncated: null },
    transform: { x: 0, y: 0, scale: 1 },
    dragging: null,
    unitLayout: null
  };

  buildIndexes();
  buildFilterControls();
  bindControls();
  showUnitGraph(true);

  function normalizeEntity(value) {
    const entity = Object.assign({}, value || {});
    entity.id = text(entity.id);
    entity.kind = text(entity.kind || "unknown");
    entity.name = text(entity.name || entity.qualified_name || entity.id);
    entity.qualified_name = text(entity.qualified_name || entity.name);
    entity.namespace = text(entity.namespace);
    entity.expected_namespace = text(entity.expected_namespace);
    entity.path = text(entity.path || entity.defining_path);
    entity.line = numberOrNull(entity.line === undefined ? entity.defining_line : entity.line);
    entity.compilation_unit = text(entity.compilation_unit || entity.home_compilation_unit);
    entity.module = moduleName(entity.module, entity.path);
    entity.subcategory = text(entity.subcategory || backendSubcategory(entity.module, entity.path));
    entity.lint_tag = text(entity.lint_tag);
    entity.signature = text(entity.signature);
    entity.noexcept = normalizeBoolean(entity.noexcept);
    entity.owner_type = text(entity.owner_type);
    entity.trivial_destructor = normalizeBoolean(entity.trivial_destructor);
    entity.linkage = text(entity.linkage);
    entity.namespace_path_match = normalizeBoolean(entity.namespace_path_match);
    return entity;
  }

  function normalizeRelationship(value) {
    const relation = Object.assign({}, value || {});
    relation.source_id = text(relation.source_id);
    relation.target_id = text(relation.target_id);
    relation.relationship = text(relation.relationship || "uses");
    relation.path = text(relation.path);
    relation.line = numberOrNull(relation.line);
    relation.count = Math.max(1, Number(relation.count) || 1);
    return relation;
  }

  function normalizeUnitDependency(value) {
    const dependency = Object.assign({}, value || {});
    dependency.source_unit = text(dependency.source_unit);
    dependency.target_unit = text(dependency.target_unit);
    dependency.source_path = text(dependency.source_path);
    dependency.target_path = text(dependency.target_path);
    dependency.relationship = text(dependency.relationship || "uses");
    dependency.edge_count = Math.max(1, Number(
      dependency.edge_count === undefined ? dependency.count : dependency.edge_count
    ) || 1);
    dependency.source_entity_count = Math.max(0, Number(dependency.source_entity_count) || 0);
    return dependency;
  }

  function buildIndexes() {
    for (const entity of state.entities) {
      if (!entity.id || state.entityById.has(entity.id)) continue;
      state.entityById.set(entity.id, entity);
    }

    for (const entity of state.entities) {
      if (isCompilationUnit(entity)) state.unitById.set(entity.id, entity);
    }

    for (const dependency of state.unitDependencies) {
      ensureUnit(dependency.source_unit, dependency.source_path);
      ensureUnit(dependency.target_unit, dependency.target_path);
      pushMapList(state.outgoingByUnit, dependency.source_unit, dependency);
      pushMapList(state.incomingByUnit, dependency.target_unit, dependency);
    }

    for (const entity of state.entities) {
      if (!entity.compilation_unit || isCompilationUnit(entity)) continue;
      const unit = resolveUnitReference(entity.compilation_unit);
      if (unit) pushMapList(state.entitiesByUnit, unit.id, entity);
    }

    for (const relation of state.relationships) {
      pushMapList(state.outgoingByEntity, relation.source_id, relation);
      pushMapList(state.incomingByEntity, relation.target_id, relation);
    }

    for (const unit of state.unitById.values()) {
      const members = state.entitiesByUnit.get(unit.id) || [];
      if (unit.module === "other" && members.length) {
        unit.module = mostCommon(members.map(function (entity) { return entity.module; })) || "other";
      }
    }
  }

  function ensureUnit(id, path) {
    if (!id || state.unitById.has(id)) return;
    const inferredPath = path || id;
    const placeholder = normalizeEntity({
      id: id,
      kind: "compilation_unit",
      name: basename(inferredPath),
      qualified_name: inferredPath,
      path: inferredPath,
      compilation_unit: id,
      module: moduleName("", inferredPath),
      lint_tag: ""
    });
    placeholder.placeholder = true;
    state.entityById.set(id, placeholder);
    state.unitById.set(id, placeholder);
  }

  function resolveUnitReference(reference) {
    if (state.unitById.has(reference)) return state.unitById.get(reference);
    for (const unit of state.unitById.values()) {
      if (unit.path === reference || unit.qualified_name === reference) return unit;
    }
    return null;
  }

  function buildFilterControls() {
    const modules = unique(
      STANDARD_MODULES.concat(Array.from(state.unitById.values()).map(function (unit) {
        return unit.module;
      })).concat(state.entities.map(function (entity) { return entity.module; })))
      .filter(Boolean)
      .sort(moduleSort);

    const edgeTypes = unique(
      state.unitDependencies.map(function (edge) { return edge.relationship; })
        .concat(state.relationships.map(function (edge) { return edge.relationship; })))
      .filter(Boolean)
      .sort(compareText);

    for (const module of modules) {
      state.moduleEnabled.set(module, module !== "external");
    }
    for (const edgeType of edgeTypes) state.edgeEnabled.set(edgeType, true);

    renderModuleFilters();
    renderEdgeFilters();
    renderSubcategoryLegend();
  }

  function renderSubcategoryLegend() {
    els.subcategoryLegend.replaceChildren();
    const present = new Set(state.entities.filter(function (entity) {
      return entity.module === "base" || entity.module === "altrep";
    }).map(function (entity) {
      return entity.subcategory;
    }).filter(Boolean));

    for (const category of BACKEND_SUBCATEGORIES) {
      if (!present.has(category)) continue;
      const item = document.createElement("li");
      const swatch = document.createElement("span");
      swatch.className = "swatch";
      swatch.style.background = subcategoryColor(category);
      const label = document.createElement("span");
      label.textContent = category;
      item.append(swatch, label);
      els.subcategoryLegend.append(item);
    }
  }

  function renderModuleFilters() {
    els.moduleFilters.replaceChildren();
    const counts = new Map();
    const source = state.graph.nodes.length ? state.graph.nodes.map(function (node) {
      return node.entity;
    }) : (state.view === "units" ? Array.from(state.unitById.values()) : state.entities);
    for (const entity of source) counts.set(entity.module, (counts.get(entity.module) || 0) + 1);

    for (const module of Array.from(state.moduleEnabled.keys()).sort(moduleSort)) {
      const row = document.createElement("label");
      row.className = "check-row";
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = state.moduleEnabled.get(module);
      checkbox.addEventListener("change", function () {
        state.moduleEnabled.set(module, checkbox.checked);
        renderGraph(false);
      });
      const swatch = document.createElement("span");
      swatch.className = "swatch";
      swatch.style.background = moduleColor(module);
      const name = document.createElement("span");
      name.textContent = module;
      const count = document.createElement("span");
      count.className = "filter-count";
      count.textContent = String(counts.get(module) || 0);
      row.append(checkbox, swatch, name, count);
      els.moduleFilters.append(row);
    }
  }

  function renderEdgeFilters() {
    els.edgeFilters.replaceChildren();
    const activeEdges = state.graph.edges.length ? state.graph.edges :
      (state.view === "units" ? state.unitDependencies : state.relationships);
    const counts = new Map();
    for (const edge of activeEdges) {
      counts.set(edge.relationship, (counts.get(edge.relationship) || 0) + 1);
    }

    for (const edgeType of Array.from(state.edgeEnabled.keys()).sort(compareText)) {
      if (!counts.has(edgeType)) continue;
      const row = document.createElement("label");
      row.className = "check-row";
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = state.edgeEnabled.get(edgeType);
      checkbox.addEventListener("change", function () {
        state.edgeEnabled.set(edgeType, checkbox.checked);
        renderGraph(false);
      });
      const spacer = document.createElement("span");
      const name = document.createElement("span");
      name.textContent = edgeType;
      const count = document.createElement("span");
      count.className = "filter-count";
      count.textContent = String(counts.get(edgeType) || 0);
      row.append(checkbox, spacer, name, count);
      els.edgeFilters.append(row);
    }
  }

  function bindControls() {
    els.unitsView.addEventListener("click", function () { showUnitGraph(true); });
    els.fitView.addEventListener("click", fitGraph);
    els.zoomIn.addEventListener("click", function () { zoomAt(1.25); });
    els.zoomOut.addEventListener("click", function () { zoomAt(0.8); });

    els.search.addEventListener("input", updateSearchResults);
    els.search.addEventListener("keydown", function (event) {
      if (event.key === "Escape") closeSearch();
      if (event.key === "Enter") {
        const first = els.searchResults.querySelector("button");
        if (first) first.click();
      }
    });
    document.addEventListener("pointerdown", function (event) {
      if (!event.target.closest(".search-wrap")) closeSearch();
    });

    els.svg.addEventListener("wheel", onWheel, { passive: false });
    els.svg.addEventListener("pointerdown", onPointerDown);
    els.svg.addEventListener("pointermove", onPointerMove);
    els.svg.addEventListener("pointerup", endPan);
    els.svg.addEventListener("pointercancel", endPan);
    window.addEventListener("resize", debounce(function () {
      if (state.graph.nodes.length) fitGraph();
    }, 120));
  }

  function showUnitGraph(fit) {
    state.view = "units";
    state.selectedId = null;
    state.selectedKind = null;
    els.unitsView.setAttribute("aria-pressed", "true");
    if (!state.unitLayout) state.unitLayout = buildUnitLayout();
    state.graph = state.unitLayout;
    renderModuleFilters();
    renderEdgeFilters();
    renderGraph(fit);
    showDefaultDetails();
  }

  function showEntityNeighborhood(entityId, fit) {
    const entity = state.entityById.get(entityId);
    if (!entity) return;
    if (state.moduleEnabled.get(entity.module) === false) {
      state.moduleEnabled.set(entity.module, true);
    }
    state.view = "entity";
    state.selectedId = entity.id;
    state.selectedKind = "entity";
    els.unitsView.setAttribute("aria-pressed", "false");
    state.graph = buildEntityNeighborhood(entity);
    renderModuleFilters();
    renderEdgeFilters();
    renderGraph(fit !== false);
    showEntityDetails(entity);
  }

  function buildUnitLayout() {
    const nodes = Array.from(state.unitById.values()).map(function (unit) {
      return graphNode(unit, "unit");
    });
    const nodeById = new Map(nodes.map(function (node) { return [node.id, node]; }));
    const edges = state.unitDependencies.filter(function (edge) {
      return nodeById.has(edge.source_unit) && nodeById.has(edge.target_unit);
    }).map(function (edge, index) {
      return {
        id: "unit-edge:" + index,
        source: edge.source_unit,
        target: edge.target_unit,
        relationship: edge.relationship,
        count: edge.edge_count,
        raw: edge
      };
    });

    const margin = 70;
    const gap = 40;
    const leftWidth = 360;
    const rightWidth = 1140;
    const rightX = margin + leftWidth + gap;
    const panelHeight = 430;
    const moduleBoxes = {
      runtime: { x: margin, y: margin, width: leftWidth, height: 170 },
      shared: { x: margin, y: margin + 170 + gap, width: leftWidth, height: 420 },
      base: { x: rightX, y: margin, width: rightWidth, height: panelHeight },
      altrep: {
        x: rightX,
        y: margin + panelHeight + gap,
        width: rightWidth,
        height: panelHeight
      }
    };
    const groups = [];
    const modules = unique(nodes.map(function (node) { return node.module; })).sort(moduleSort);
    let overflowY = margin + panelHeight * 2 + gap + 60;

    modules.forEach(function (module) {
      const box = moduleBoxes[module] || {
        x: margin,
        y: overflowY,
        width: leftWidth + gap + rightWidth,
        height: 240
      };
      const members = nodes.filter(function (node) { return node.module === module; })
        .sort(function (a, b) { return compareText(a.entity.path || a.name, b.entity.path || b.name); });
      groups.push({
        module: module,
        x: box.x,
        y: box.y,
        width: box.width,
        height: box.height
      });
      positionUnitGroup(members, box.x, box.y, box.width, box.height);
      if (!moduleBoxes[module]) overflowY += box.height + gap;
    });

    relaxUnitLayout(nodes, edges, groups);
    return { nodes: nodes, edges: edges, groups: groups, truncated: null };
  }

  function positionUnitGroup(nodes, x, y, width, height) {
    const columns = Math.max(1, Math.ceil(Math.sqrt(nodes.length * width / height)));
    const rows = Math.max(1, Math.ceil(nodes.length / columns));
    const usableWidth = width - 90;
    const usableHeight = height - 85;
    nodes.forEach(function (node, index) {
      const column = index % columns;
      const row = Math.floor(index / columns);
      const stagger = row % 2 ? 0.25 : -0.25;
      node.x = x + 45 + (column + 0.5 + stagger) * usableWidth / columns;
      node.y = y + 55 + (row + 0.5) * usableHeight / rows;
      node.homeX = node.x;
      node.homeY = node.y;
    });
  }

  function relaxUnitLayout(nodes, edges, groups) {
    const nodeById = new Map(nodes.map(function (node) { return [node.id, node]; }));
    const groupByModule = new Map(groups.map(function (group) { return [group.module, group]; }));
    const iterations = nodes.length > 170 ? 80 : 135;

    for (let iteration = 0; iteration < iterations; iteration += 1) {
      const cooling = 1 - iteration / iterations;
      const force = new Map(nodes.map(function (node) { return [node.id, { x: 0, y: 0 }]; }));

      for (let i = 0; i < nodes.length; i += 1) {
        for (let j = i + 1; j < nodes.length; j += 1) {
          const a = nodes[i];
          const b = nodes[j];
          if (a.module !== b.module) continue;
          let dx = a.x - b.x;
          let dy = a.y - b.y;
          let distanceSquared = dx * dx + dy * dy;
          if (distanceSquared < 1) {
            dx = deterministicJitter(a.id + b.id);
            dy = deterministicJitter(b.id + a.id);
            distanceSquared = dx * dx + dy * dy;
          }
          const strength = Math.min(3.5, 2200 / distanceSquared);
          const distance = Math.sqrt(distanceSquared);
          force.get(a.id).x += strength * dx / distance;
          force.get(a.id).y += strength * dy / distance;
          force.get(b.id).x -= strength * dx / distance;
          force.get(b.id).y -= strength * dy / distance;
        }
      }

      for (const edge of edges) {
        const source = nodeById.get(edge.source);
        const target = nodeById.get(edge.target);
        if (!source || !target || source.module !== target.module || source === target) continue;
        const dx = target.x - source.x;
        const dy = target.y - source.y;
        const distance = Math.max(1, Math.sqrt(dx * dx + dy * dy));
        const strength = (distance - 105) * 0.0025 * Math.min(3, Math.log2(edge.count + 1));
        force.get(source.id).x += strength * dx / distance;
        force.get(source.id).y += strength * dy / distance;
        force.get(target.id).x -= strength * dx / distance;
        force.get(target.id).y -= strength * dy / distance;
      }

      for (const node of nodes) {
        const f = force.get(node.id);
        f.x += (node.homeX - node.x) * 0.012;
        f.y += (node.homeY - node.y) * 0.012;
        const limit = 5 * cooling + 0.4;
        node.x += clamp(f.x, -limit, limit);
        node.y += clamp(f.y, -limit, limit);
        const group = groupByModule.get(node.module);
        node.x = clamp(node.x, group.x + 38, group.x + group.width - 38);
        node.y = clamp(node.y, group.y + 48, group.y + group.height - 35);
      }
    }
  }

  function buildEntityNeighborhood(entity) {
    const outgoing = state.outgoingByEntity.get(entity.id) || [];
    const incoming = state.incomingByEntity.get(entity.id) || [];
    const allRelations = outgoing.concat(incoming);
    const neighborScores = new Map();

    for (const relation of allRelations) {
      const otherId = relation.source_id === entity.id ? relation.target_id : relation.source_id;
      if (!otherId || otherId === entity.id) continue;
      neighborScores.set(otherId, (neighborScores.get(otherId) || 0) + relation.count);
    }

    const allNeighborIds = Array.from(neighborScores.keys()).sort(function (a, b) {
      return neighborScores.get(b) - neighborScores.get(a) || compareText(a, b);
    });
    const visibleNeighborIds = allNeighborIds.slice(0, MAX_NEIGHBOR_NODES - 1);
    const visible = new Set([entity.id].concat(visibleNeighborIds));
    const nodes = Array.from(visible).map(function (id) {
      return graphNode(resolveEntity(id), "entity");
    });
    const edges = state.relationships.filter(function (relation) {
      return visible.has(relation.source_id) && visible.has(relation.target_id) &&
        (relation.source_id === entity.id || relation.target_id === entity.id);
    }).map(function (relation, index) {
      return {
        id: "entity-edge:" + index,
        source: relation.source_id,
        target: relation.target_id,
        relationship: relation.relationship,
        count: relation.count,
        raw: relation
      };
    });

    positionEntityNeighborhood(nodes, edges, entity.id);
    return {
      nodes: nodes,
      edges: edges,
      groups: [],
      truncated: allNeighborIds.length > visibleNeighborIds.length ? {
        shown: visibleNeighborIds.length,
        total: allNeighborIds.length
      } : null
    };
  }

  function positionEntityNeighborhood(nodes, edges, centerId) {
    const center = nodes.find(function (node) { return node.id === centerId; });
    if (!center) return;
    center.x = 800;
    center.y = 510;
    const directionById = new Map();
    for (const edge of edges) {
      if (edge.source === centerId && edge.target !== centerId) {
        addDirection(directionById, edge.target, "out");
      }
      if (edge.target === centerId && edge.source !== centerId) {
        addDirection(directionById, edge.source, "in");
      }
    }

    const sections = { out: [], in: [], both: [], self: [] };
    for (const node of nodes) {
      if (node.id === centerId) continue;
      const directions = directionById.get(node.id) || new Set();
      const key = directions.size > 1 ? "both" : (directions.has("out") ? "out" : "in");
      sections[key].push(node);
    }
    Object.values(sections).forEach(function (items) {
      items.sort(function (a, b) { return compareText(a.name, b.name); });
    });

    placeArcRings(sections.out, center.x, center.y, -Math.PI * 0.46, Math.PI * 0.46, 155);
    placeArcRings(sections.in, center.x, center.y, Math.PI * 0.54, Math.PI * 1.46, 155);
    placeArcRings(sections.both, center.x, center.y, Math.PI * 1.52, Math.PI * 1.98, 285);
  }

  function placeArcRings(nodes, centerX, centerY, startAngle, endAngle, firstRadius) {
    let offset = 0;
    let ring = 0;
    while (offset < nodes.length) {
      const capacity = 14 + ring * 8;
      const slice = nodes.slice(offset, offset + capacity);
      const radius = firstRadius + ring * 112;
      slice.forEach(function (node, index) {
        const fraction = slice.length === 1 ? 0.5 : index / (slice.length - 1);
        const angle = startAngle + (endAngle - startAngle) * fraction;
        node.x = centerX + Math.cos(angle) * radius;
        node.y = centerY + Math.sin(angle) * radius;
      });
      offset += slice.length;
      ring += 1;
    }
  }

  function renderGraph(fit) {
    const enabledNodes = state.graph.nodes.filter(function (node) {
      return state.moduleEnabled.get(node.module) !== false;
    });
    const enabledNodeIds = new Set(enabledNodes.map(function (node) { return node.id; }));
    const enabledEdges = state.graph.edges.filter(function (edge) {
      return enabledNodeIds.has(edge.source) && enabledNodeIds.has(edge.target) &&
        state.edgeEnabled.get(edge.relationship) !== false;
    });

    els.groups.replaceChildren();
    els.edges.replaceChildren();
    els.nodes.replaceChildren();
    renderGroups(state.graph.groups.filter(function (group) {
      return state.moduleEnabled.get(group.module) !== false;
    }));
    renderEdges(enabledEdges, enabledNodes);
    renderNodes(enabledNodes, enabledEdges);
    renderViewLegend(enabledNodes);

    const mode = state.view === "units" ? "compilation units" : "entities in one-hop neighborhood";
    let status = enabledNodes.length + " " + mode + ", " + enabledEdges.length + " semantic dependencies";
    if (state.graph.truncated) {
      status += ". Showing " + state.graph.truncated.shown + " of " +
        state.graph.truncated.total + " immediate neighbors";
    }
    els.status.textContent = status;
    if (fit) requestAnimationFrame(fitGraph);
    else applyTransform();
  }

  function renderGroups(groups) {
    const fragment = document.createDocumentFragment();
    for (const group of groups) {
      const rect = svgElement("rect", {
        class: "module-box",
        x: group.x,
        y: group.y,
        width: group.width,
        height: group.height,
        rx: 14
      });
      rect.style.stroke = moduleColor(group.module);
      const title = svgElement("text", {
        class: "module-title",
        x: group.x + 20,
        y: group.y + 29
      });
      title.textContent = group.module;
      fragment.append(rect, title);
    }
    els.groups.append(fragment);
  }

  function renderEdges(edges, nodes) {
    const fragment = document.createDocumentFragment();
    const nodeById = new Map(nodes.map(function (node) { return [node.id, node]; }));
    const selected = state.selectedId;
    for (const edge of edges) {
      const source = nodeById.get(edge.source);
      const target = nodeById.get(edge.target);
      if (!source || !target) continue;
      const related = selected && (edge.source === selected || edge.target === selected);
      const dimmed = selected && !related;
      const path = svgElement("path", {
        class: "edge" + (related ? " related" : "") + (dimmed ? " dimmed" : ""),
        d: edgePath(source, target, edge)
      });
      path.dataset.source = edge.source;
      path.dataset.target = edge.target;
      path.dataset.relationship = edge.relationship;
      const title = svgElement("title");
      title.textContent = source.name + " → " + target.name + "\n" +
        edge.relationship + " (" + edge.count + ")";
      path.append(title);
      fragment.append(path);
    }
    els.edges.append(fragment);
  }

  function renderNodes(nodes, edges) {
    const fragment = document.createDocumentFragment();
    const neighborIds = new Set();
    if (state.selectedId) {
      for (const edge of edges) {
        if (edge.source === state.selectedId) neighborIds.add(edge.target);
        if (edge.target === state.selectedId) neighborIds.add(edge.source);
      }
    }

    for (const node of nodes) {
      const selected = node.id === state.selectedId;
      const neighbor = neighborIds.has(node.id);
      const dimmed = state.selectedId && !selected && !neighbor;
      const group = svgElement("g", {
        class: "node" + (selected ? " selected" : "") +
          (neighbor ? " neighbor" : "") + (dimmed ? " dimmed" : ""),
        transform: "translate(" + node.x + " " + node.y + ")",
        tabindex: "0",
        role: "button",
        "aria-label": node.name + ", " + node.kind
      });
      group.dataset.id = node.id;
      group.append(nodeShape(node));

      const label = svgElement("text", {
        class: "node-label",
        x: 0,
        y: node.kind === "compilation_unit" ? 31 : 27,
        "text-anchor": "middle"
      });
      label.textContent = trimMiddle(node.name, state.view === "units" ? 28 : 24);
      group.append(label);

      if (node.entity.lint_tag) group.append(lintBadge(node));
      if (node.entity.namespace_path_match === false) group.append(mismatchBadge(node));

      const title = svgElement("title");
      title.textContent = node.entity.qualified_name || node.name;
      group.append(title);
      group.addEventListener("click", function (event) {
        event.stopPropagation();
        selectGraphNode(node);
      });
      group.addEventListener("keydown", function (event) {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          selectGraphNode(node);
        }
      });
      fragment.append(group);
    }
    els.nodes.append(fragment);
  }

  function nodeShape(node) {
    const color = entityColor(node.entity);
    const shapeKind = entityShapeKind(node.entity);
    let shape;
    if (shapeKind === "unit") {
      shape = svgElement("rect", { class: "node-shape", x: -30, y: -17, width: 60, height: 34, rx: 5 });
    } else if (shapeKind === "function") {
      shape = svgElement("ellipse", { class: "node-shape", cx: 0, cy: 0, rx: 25, ry: 16 });
    } else if (shapeKind === "type") {
      shape = svgElement("polygon", {
        class: "node-shape",
        points: "-24,0 -13,-19 13,-19 24,0 13,19 -13,19"
      });
    } else {
      shape = svgElement("polygon", { class: "node-shape", points: "0,-20 20,0 0,20 -20,0" });
    }
    shape.style.fill = color;
    if (lintClass(node.entity.lint_tag) === "r") shape.style.strokeDasharray = "5 3";
    if (lintClass(node.entity.lint_tag) === "cxx") shape.style.strokeDasharray = "2 3";
    return shape;
  }

  function lintBadge(node) {
    const badge = svgElement("g", { class: "node-badge", transform: "translate(24 -18)" });
    badge.append(svgElement("circle", { cx: 0, cy: 0, r: 8 }));
    const label = svgElement("text", { x: 0, y: 0 });
    label.textContent = lintLetter(node.entity.lint_tag);
    badge.append(label);
    return badge;
  }

  function mismatchBadge() {
    const badge = svgElement("g", { class: "mismatch-marker", transform: "translate(-26 -20)" });
    badge.append(svgElement("polygon", { points: "0,-9 9,8 -9,8" }));
    const label = svgElement("text", { x: 0, y: 3 });
    label.textContent = "!";
    badge.append(label);
    return badge;
  }

  function renderViewLegend(nodes) {
    els.viewLegendContent.replaceChildren();

    const colors = new Map();
    const shapes = new Set();
    const badges = new Set();
    for (const node of nodes) {
      const entity = node.entity;
      const categoryColor = (entity.module === "base" || entity.module === "altrep") &&
        entity.subcategory;
      const colorKey = categoryColor ? "category:" + entity.subcategory :
        "module:" + entity.module;
      colors.set(colorKey, {
        label: categoryColor ? entity.subcategory : entity.module,
        color: entityColor(entity)
      });
      shapes.add(entityShapeKind(entity));
      if (entity.lint_tag) badges.add(lintClass(entity.lint_tag));
    }

    const colorItems = Array.from(colors.entries()).map(function (item) {
      return { key: item[0], label: item[1].label, color: item[1].color };
    }).sort(legendColorSort);
    appendViewLegendSection("Colors", colorItems, function (item) {
      const swatch = document.createElement("span");
      swatch.className = "view-legend-swatch";
      swatch.style.background = item.color;
      return swatch;
    });

    const shapeOrder = ["unit", "function", "type", "other"];
    const shapeItems = shapeOrder.filter(function (kind) {
      return shapes.has(kind);
    }).map(function (kind) {
      return { key: kind, label: shapeLabel(kind) };
    });
    appendViewLegendSection("Shapes", shapeItems, function (item) {
      const marker = document.createElement("span");
      marker.className = "legend-shape " + item.key;
      return marker;
    });

    const badgeOrder = ["entry", "abi", "cxx", "r", "neutral", "unwind", "unclassified", "other"];
    const badgeItems = badgeOrder.filter(function (kind) {
      return badges.has(kind);
    }).map(function (kind) {
      return { key: kind, label: lintLabel(kind) };
    });
    appendViewLegendSection("Lint badges", badgeItems, function (item) {
      const badge = document.createElement("span");
      badge.className = "view-legend-badge";
      badge.textContent = lintLetterForClass(item.key);
      return badge;
    });
  }

  function appendViewLegendSection(title, items, marker) {
    if (!items.length) return;
    const section = document.createElement("section");
    section.className = "view-legend-section " +
      title.toLocaleLowerCase().replace(/\s+/g, "-");
    const heading = document.createElement("h4");
    heading.textContent = title;
    const list = document.createElement("ul");
    list.className = "view-legend-list";
    for (const item of items) {
      const row = document.createElement("li");
      row.className = "view-legend-row";
      const label = document.createElement("span");
      label.textContent = item.label;
      row.append(marker(item), label);
      list.append(row);
    }
    section.append(heading, list);
    els.viewLegendContent.append(section);
  }

  function selectGraphNode(node) {
    state.selectedId = node.id;
    state.selectedKind = node.kind === "compilation_unit" ? "unit" : "entity";
    if (state.selectedKind === "unit") showUnitDetails(node.entity);
    else showEntityDetails(node.entity);
    renderGraph(false);
  }

  function showDefaultDetails() {
    els.details.className = "";
    els.details.replaceChildren();
    appendTitle(els.details, "Map summary");
    appendProperties(els.details, [
      ["Compilation units", state.metadata.translation_unit_count || state.unitById.size],
      ["Entities", state.entities.length],
      ["Relationships", state.relationships.length],
      ["Unit dependencies", state.unitDependencies.length],
      ["Semantic entrypoints", state.metadata.entrypoint_count],
      ["ABI shims", state.metadata.abi_shim_count],
      ["Indirect/unresolved calls", state.metadata.unresolved_call_count]
    ]);
    const prompt = document.createElement("p");
    prompt.className = "small-copy";
    prompt.textContent = "Select a compilation unit to inspect its entities and semantic dependencies.";
    els.details.append(prompt);
  }

  function showUnitDetails(unit) {
    els.details.className = "";
    els.details.replaceChildren();
    const members = (state.entitiesByUnit.get(unit.id) || []).slice().sort(entitySort);
    const outgoing = state.outgoingByUnit.get(unit.id) || [];
    const incoming = state.incomingByUnit.get(unit.id) || [];

    appendTitle(els.details, unit.name || basename(unit.path));
    appendPills(els.details, [
      unit.module,
      unit.subcategory,
      "compilation unit",
      members.length + " entities"
    ]);
    appendProperties(els.details, [
      ["ID", unit.id],
      ["Path", unit.path || unit.qualified_name],
      ["Namespace", unit.namespace],
      ["Line", unit.line]
    ]);

    const actions = document.createElement("div");
    actions.className = "detail-actions";
    const clear = actionButton("Clear selection", function () {
      state.selectedId = null;
      state.selectedKind = null;
      showDefaultDetails();
      renderGraph(false);
    });
    actions.append(clear);
    els.details.append(actions);

    appendSectionHeading(els.details, "Entities (" + members.length + ")");
    appendEntityList(els.details, members);
    appendSectionHeading(els.details, "Outgoing units (" + outgoing.length + ")");
    appendUnitRelations(els.details, outgoing, "out");
    appendSectionHeading(els.details, "Incoming units (" + incoming.length + ")");
    appendUnitRelations(els.details, incoming, "in");
  }

  function showEntityDetails(entity) {
    els.details.className = "";
    els.details.replaceChildren();
    const outgoing = state.outgoingByEntity.get(entity.id) || [];
    const incoming = state.incomingByEntity.get(entity.id) || [];

    appendTitle(els.details, entity.qualified_name || entity.name);
    const pills = [
      entity.module,
      entity.subcategory,
      entity.kind,
      entity.lint_tag
    ].filter(Boolean);
    appendPills(els.details, pills, entity.namespace_path_match === false);
    appendProperties(els.details, [
      ["ID", entity.id],
      ["Signature", entity.signature],
      ["Namespace", entity.namespace],
      ["Expected", entity.expected_namespace],
      ["Path", formatLocation(entity.path, entity.line)],
      ["Unit", entity.compilation_unit],
      ["Linkage", entity.linkage],
      ["noexcept", booleanLabel(entity.noexcept)],
      ["Owner type", entity.owner_type],
      ["Trivial dtor", booleanLabel(entity.trivial_destructor)],
      ["Namespace/path", entity.namespace_path_match === false ? "mismatch" :
        (entity.namespace_path_match === true ? "match" : "not reported")]
    ]);

    const actions = document.createElement("div");
    actions.className = "detail-actions";
    if (state.view !== "entity" || state.graph.nodes[0] && state.graph.nodes[0].id !== entity.id) {
      actions.append(actionButton("Show neighborhood", function () {
        showEntityNeighborhood(entity.id, true);
      }));
    }
    const unit = resolveUnitReference(entity.compilation_unit);
    if (unit) {
      actions.append(actionButton("Open unit", function () {
        showUnitGraph(false);
        state.selectedId = unit.id;
        state.selectedKind = "unit";
        showUnitDetails(unit);
        renderGraph(false);
        focusNode(unit.id);
      }));
    }
    actions.append(actionButton("Unit graph", function () { showUnitGraph(true); }));
    els.details.append(actions);

    appendSectionHeading(els.details, "Outgoing relationships (" + outgoing.length + ")");
    appendEntityRelations(els.details, outgoing, "out");
    appendSectionHeading(els.details, "Incoming relationships (" + incoming.length + ")");
    appendEntityRelations(els.details, incoming, "in");
  }

  function appendTitle(parent, value) {
    const title = document.createElement("h3");
    title.className = "detail-title";
    title.textContent = value;
    parent.append(title);
  }

  function appendPills(parent, values, mismatch) {
    const row = document.createElement("div");
    row.className = "pill-row";
    for (const value of values) {
      if (!value) continue;
      const pill = document.createElement("span");
      pill.className = "pill";
      pill.textContent = value;
      row.append(pill);
    }
    if (mismatch) {
      const warning = document.createElement("span");
      warning.className = "pill warning";
      warning.textContent = "namespace mismatch";
      row.append(warning);
    }
    parent.append(row);
  }

  function appendProperties(parent, entries) {
    const list = document.createElement("dl");
    list.className = "properties";
    for (const entry of entries) {
      if (entry[1] === "" || entry[1] === null || entry[1] === undefined) continue;
      const term = document.createElement("dt");
      term.textContent = entry[0];
      const value = document.createElement("dd");
      value.textContent = String(entry[1]);
      list.append(term, value);
    }
    parent.append(list);
  }

  function appendSectionHeading(parent, textValue) {
    const heading = document.createElement("h3");
    heading.textContent = textValue;
    parent.append(heading);
  }

  function appendEntityList(parent, entities) {
    const list = document.createElement("ul");
    list.className = "entity-list";
    const visible = entities.slice(0, MAX_DETAIL_ITEMS);
    for (const entity of visible) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.className = "entity-button";
      const kind = document.createElement("span");
      kind.className = "item-kind";
      kind.textContent = shortKind(entity.kind);
      const name = document.createElement("span");
      name.textContent = entity.qualified_name || entity.name;
      const detail = document.createElement("span");
      detail.className = "item-sub";
      detail.textContent = [entity.lint_tag, formatLocation(entity.path, entity.line)]
        .filter(Boolean).join(" · ");
      button.append(kind, name, detail);
      button.addEventListener("click", function () { showEntityNeighborhood(entity.id, true); });
      item.append(button);
      list.append(item);
    }
    parent.append(list);
    appendListLimit(parent, visible.length, entities.length);
  }

  function appendUnitRelations(parent, relations, direction) {
    const sorted = relations.slice().sort(function (a, b) {
      return b.edge_count - a.edge_count || compareText(a.relationship, b.relationship);
    });
    const list = document.createElement("ul");
    list.className = "relation-list";
    const visible = sorted.slice(0, MAX_DETAIL_ITEMS);
    for (const relation of visible) {
      const otherId = direction === "out" ? relation.target_unit : relation.source_unit;
      const other = state.unitById.get(otherId);
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.className = "relation-button";
      const arrow = document.createElement("span");
      arrow.className = "direction";
      arrow.textContent = direction === "out" ? "→" : "←";
      const name = document.createElement("span");
      name.textContent = other ? (other.name || basename(other.path)) : otherId;
      const detail = document.createElement("span");
      detail.className = "item-sub";
      detail.textContent = relation.relationship + " · " + relation.edge_count + " entity edges";
      button.append(arrow, name, detail);
      button.addEventListener("click", function () {
        if (!other) return;
        state.selectedId = other.id;
        state.selectedKind = "unit";
        showUnitDetails(other);
        renderGraph(false);
        focusNode(other.id);
      });
      item.append(button);
      list.append(item);
    }
    parent.append(list);
    appendListLimit(parent, visible.length, sorted.length);
  }

  function appendEntityRelations(parent, relations, direction) {
    const sorted = relations.slice().sort(function (a, b) {
      return b.count - a.count || compareText(a.relationship, b.relationship);
    });
    const list = document.createElement("ul");
    list.className = "relation-list";
    const visible = sorted.slice(0, MAX_DETAIL_ITEMS);
    for (const relation of visible) {
      const otherId = direction === "out" ? relation.target_id : relation.source_id;
      const other = resolveEntity(otherId);
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.className = "relation-button";
      const arrow = document.createElement("span");
      arrow.className = "direction";
      arrow.textContent = direction === "out" ? "→" : "←";
      const name = document.createElement("span");
      name.textContent = other.qualified_name || other.name;
      const detail = document.createElement("span");
      detail.className = "item-sub";
      detail.textContent = relation.relationship + " · " + relation.count;
      button.append(arrow, name, detail);
      button.addEventListener("click", function () { showEntityNeighborhood(other.id, true); });
      item.append(button);
      list.append(item);
    }
    parent.append(list);
    appendListLimit(parent, visible.length, sorted.length);
  }

  function appendListLimit(parent, visible, total) {
    if (visible >= total) return;
    const note = document.createElement("p");
    note.className = "list-note";
    note.textContent = "Showing " + visible + " of " + total + ". Use search for the rest.";
    parent.append(note);
  }

  function actionButton(label, handler) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    button.addEventListener("click", handler);
    return button;
  }

  function updateSearchResults() {
    const query = els.search.value.trim().toLocaleLowerCase();
    if (!query) {
      closeSearch();
      return;
    }
    const terms = query.split(/\s+/).filter(Boolean);
    const results = state.entities.map(function (entity) {
      const haystack = [entity.name, entity.qualified_name, entity.path, entity.namespace,
        entity.module, entity.subcategory, entity.kind, entity.lint_tag, entity.signature]
        .join(" ").toLocaleLowerCase();
      if (!terms.every(function (term) { return haystack.includes(term); })) return null;
      let score = 0;
      const name = entity.name.toLocaleLowerCase();
      const qualified = entity.qualified_name.toLocaleLowerCase();
      if (name === query || qualified === query) score += 100;
      if (name.startsWith(query)) score += 50;
      if (qualified.startsWith(query)) score += 30;
      if (isCompilationUnit(entity)) score += 3;
      score -= Math.min(20, name.length / 10);
      return { entity: entity, score: score };
    }).filter(Boolean).sort(function (a, b) {
      return b.score - a.score || compareText(a.entity.qualified_name, b.entity.qualified_name);
    }).slice(0, MAX_SEARCH_RESULTS);

    els.searchResults.replaceChildren();
    if (!results.length) {
      const empty = document.createElement("div");
      empty.className = "small-copy";
      empty.style.padding = "0.7rem";
      empty.textContent = "No matching entities.";
      els.searchResults.append(empty);
    } else {
      for (const result of results) {
        const entity = result.entity;
        const button = document.createElement("button");
        button.className = "search-result";
        button.setAttribute("role", "option");
        const kind = document.createElement("span");
        kind.className = "item-kind";
        kind.textContent = shortKind(entity.kind);
        const name = document.createElement("span");
        name.className = "result-name";
        name.textContent = entity.qualified_name || entity.name;
        const path = document.createElement("span");
        path.className = "result-path";
        path.textContent = formatLocation(entity.path, entity.line);
        button.append(kind, name, path);
        button.addEventListener("click", function () {
          closeSearch();
          els.search.value = "";
          if (isCompilationUnit(entity)) {
            showUnitGraph(false);
            state.selectedId = entity.id;
            state.selectedKind = "unit";
            showUnitDetails(entity);
            renderGraph(false);
            focusNode(entity.id);
          } else {
            showEntityNeighborhood(entity.id, true);
          }
        });
        els.searchResults.append(button);
      }
    }
    els.searchResults.hidden = false;
    els.search.setAttribute("aria-expanded", "true");
  }

  function closeSearch() {
    els.searchResults.hidden = true;
    els.search.setAttribute("aria-expanded", "false");
  }

  function fitGraph() {
    const visibleNodes = Array.from(els.nodes.querySelectorAll(".node"));
    const visibleGroups = Array.from(els.groups.children);
    if (!visibleNodes.length && !visibleGroups.length) return;
    let box;
    try {
      box = els.viewport.getBBox();
    } catch (error) {
      return;
    }
    if (!box.width || !box.height) return;
    const rect = els.svg.getBoundingClientRect();
    const padding = 55;
    const scale = clamp(Math.min(
      (rect.width - padding * 2) / box.width,
      (rect.height - padding * 2) / box.height
    ), 0.12, 2.4);
    state.transform.scale = scale;
    state.transform.x = rect.width / 2 - (box.x + box.width / 2) * scale;
    state.transform.y = rect.height / 2 - (box.y + box.height / 2) * scale;
    applyTransform();
  }

  function focusNode(id) {
    const node = state.graph.nodes.find(function (item) { return item.id === id; });
    if (!node) return;
    const rect = els.svg.getBoundingClientRect();
    state.transform.scale = Math.max(state.transform.scale, 1);
    state.transform.x = rect.width / 2 - node.x * state.transform.scale;
    state.transform.y = rect.height / 2 - node.y * state.transform.scale;
    applyTransform();
  }

  function zoomAt(factor, clientX, clientY) {
    const rect = els.svg.getBoundingClientRect();
    const x = clientX === undefined ? rect.width / 2 : clientX - rect.left;
    const y = clientY === undefined ? rect.height / 2 : clientY - rect.top;
    const oldScale = state.transform.scale;
    const newScale = clamp(oldScale * factor, 0.08, 7);
    const worldX = (x - state.transform.x) / oldScale;
    const worldY = (y - state.transform.y) / oldScale;
    state.transform.scale = newScale;
    state.transform.x = x - worldX * newScale;
    state.transform.y = y - worldY * newScale;
    applyTransform();
  }

  function onWheel(event) {
    event.preventDefault();
    zoomAt(Math.exp(-event.deltaY * 0.0012), event.clientX, event.clientY);
  }

  function onPointerDown(event) {
    if (event.button !== 0 || event.target.closest(".node")) return;
    els.svg.setPointerCapture(event.pointerId);
    state.dragging = {
      pointerId: event.pointerId,
      x: event.clientX,
      y: event.clientY,
      originX: state.transform.x,
      originY: state.transform.y
    };
    els.svg.classList.add("grabbing");
  }

  function onPointerMove(event) {
    if (!state.dragging || state.dragging.pointerId !== event.pointerId) return;
    state.transform.x = state.dragging.originX + event.clientX - state.dragging.x;
    state.transform.y = state.dragging.originY + event.clientY - state.dragging.y;
    applyTransform();
  }

  function endPan(event) {
    if (!state.dragging || state.dragging.pointerId !== event.pointerId) return;
    state.dragging = null;
    els.svg.classList.remove("grabbing");
  }

  function applyTransform() {
    els.viewport.setAttribute("transform", "translate(" + state.transform.x + " " +
      state.transform.y + ") scale(" + state.transform.scale + ")");
  }

  function edgePath(source, target, edge) {
    if (source.id === target.id) {
      return "M " + (source.x + 18) + " " + source.y +
        " C " + (source.x + 68) + " " + (source.y - 58) + ", " +
        (source.x + 78) + " " + (source.y + 58) + ", " +
        (source.x + 17) + " " + (source.y + 8);
    }
    const dx = target.x - source.x;
    const dy = target.y - source.y;
    const distance = Math.max(1, Math.sqrt(dx * dx + dy * dy));
    const curveSign = hash(edge.relationship + source.id + target.id) % 2 ? 1 : -1;
    const curve = Math.min(32, distance * 0.07) * curveSign;
    const midX = (source.x + target.x) / 2 - dy / distance * curve;
    const midY = (source.y + target.y) / 2 + dx / distance * curve;
    return "M " + source.x + " " + source.y + " Q " + midX + " " + midY +
      " " + target.x + " " + target.y;
  }

  function graphNode(entity, fallbackKind) {
    return {
      id: entity.id,
      name: entity.name || entity.qualified_name || entity.id,
      kind: entity.kind || fallbackKind,
      module: entity.module || "other",
      entity: entity,
      x: 0,
      y: 0
    };
  }

  function resolveEntity(id) {
    if (state.entityById.has(id)) return state.entityById.get(id);
    const external = normalizeEntity({
      id: id,
      kind: "external",
      name: id,
      qualified_name: id,
      module: "other"
    });
    external.placeholder = true;
    state.entityById.set(id, external);
    return external;
  }

  function svgElement(name, attributes) {
    const element = document.createElementNS(SVG_NS, name);
    if (attributes) {
      for (const key of Object.keys(attributes)) {
        element.setAttribute(key, String(attributes[key]));
      }
    }
    return element;
  }

  function isCompilationUnit(entity) {
    return entity.kind === "compilation_unit" || entity.kind === "translation_unit";
  }

  function isFunctionKind(kind) {
    return /function|method|constructor|destructor|entrypoint|shim/.test(text(kind).toLocaleLowerCase());
  }

  function isTypeKind(kind) {
    return /class|struct|type|enum/.test(text(kind).toLocaleLowerCase());
  }

  function entityShapeKind(entity) {
    if (isCompilationUnit(entity)) return "unit";
    if (isFunctionKind(entity.kind)) return "function";
    if (isTypeKind(entity.kind)) return "type";
    return "other";
  }

  function shapeLabel(kind) {
    const labels = {
      unit: "Compilation unit",
      function: "Function or method",
      type: "Class or struct",
      other: "Other entity"
    };
    return labels[kind] || kind;
  }

  function shortKind(kind) {
    const aliases = {
      compilation_unit: "unit",
      translation_unit: "unit",
      constructor: "ctor",
      destructor: "dtor",
      entrypoint: "entry",
      function: "fn",
      method: "method",
      class: "class",
      struct: "struct"
    };
    return aliases[kind] || trimMiddle(kind, 8);
  }

  function moduleName(explicit, path) {
    const value = text(explicit).toLocaleLowerCase();
    if (value === "base" || value.includes("base_backend")) return "base";
    if (value === "altrep" || value.includes("altrep_backend")) return "altrep";
    if (value === "shared") return "shared";
    if (value === "runtime" || value === "registration") return "runtime";
    if (value === "other") return "other";
    const source = text(path).toLocaleLowerCase();
    if (source.includes("/altrep_backend/") || source.startsWith("src/altrep_backend/")) return "altrep";
    if (source.includes("/base_backend/") || source.startsWith("src/base_backend/")) return "base";
    if (source.includes("/shared/") || source.startsWith("src/shared/")) return "shared";
    if (source.includes("/runtime/") || source.startsWith("src/runtime/")) return "runtime";
    return value || "other";
  }

  function backendSubcategory(module, path) {
    if (module !== "base" && module !== "altrep") return "";

    const source = text(path).toLocaleLowerCase();
    const directoryMatch = source.match(
      /\/(boundary|collator|fixed|io|regex)\//
    );
    if (directoryMatch) {
      return directoryMatch[1] === "collator" ? "collation" : directoryMatch[1];
    }

    const file = basename(source);
    if (file.startsWith("ci_search_boundaries_") || file === "ci_split_lines.cpp") {
      return "boundary";
    }
    if (file.startsWith("ci_search_class_")) return "character class";
    if (file.startsWith("ci_search_coll_") || file === "ci_order_rank.cpp") {
      return "collation";
    }
    if (file.startsWith("ci_encoding_") || file.startsWith("ci_ucnv") ||
        file.startsWith("ci_escape")) {
      return "encoding";
    }
    if (file.startsWith("ci_search_fixed_")) return "fixed";
    if (file.startsWith("ci_search_regex_")) return "regex";
    if (file.startsWith("ci_trans_")) return "transform";
    return "core";
  }

  function moduleColor(module) {
    const colors = {
      base: "#3d85b2",
      altrep: "#b87543",
      shared: "#4e9269",
      runtime: "#8d68ad",
      other: "#6e7985"
    };
    return colors[module] || stableColor(module);
  }

  function entityColor(entity) {
    if ((entity.module === "base" || entity.module === "altrep") &&
        entity.subcategory) {
      return subcategoryColor(entity.subcategory);
    }
    return moduleColor(entity.module);
  }

  function subcategoryColor(category) {
    const colors = {
      core: "#7d8fa3",
      boundary: "#3fb99b",
      "character class": "#d57aa9",
      collation: "#a978d1",
      encoding: "#e09f3e",
      fixed: "#7fad55",
      io: "#399bd3",
      regex: "#dc6654",
      transform: "#c76fbd"
    };
    return colors[category] || "#8b97a5";
  }

  function legendColorSort(a, b) {
    const aCategory = a.key.startsWith("category:");
    const bCategory = b.key.startsWith("category:");
    if (aCategory && bCategory) {
      return BACKEND_SUBCATEGORIES.indexOf(a.label) -
        BACKEND_SUBCATEGORIES.indexOf(b.label);
    }
    if (aCategory !== bCategory) return aCategory ? -1 : 1;
    return moduleSort(a.label, b.label);
  }

  function stableColor(value) {
    const hue = hash(value) % 360;
    return "hsl(" + hue + " 34% 45%)";
  }

  function lintClass(tag) {
    const value = text(tag).toLocaleLowerCase();
    if (value.includes("entry")) return "entry";
    if (value.includes("abi") || value.includes("shim")) return "abi";
    if (value.includes("cxx") || value.includes("cpp")) return "cxx";
    if (value.includes("r_helper") || value.includes("r_only") || value === "r") return "r";
    if (value.includes("neutral")) return "neutral";
    if (value.includes("trusted_unwind")) return "unwind";
    if (value.includes("unclassified")) return "unclassified";
    return "other";
  }

  function lintLetter(tag) {
    return lintLetterForClass(lintClass(tag));
  }

  function lintLetterForClass(kind) {
    const letters = {
      entry: "E",
      abi: "A",
      cxx: "C",
      r: "R",
      neutral: "N",
      unwind: "W",
      unclassified: "U",
      other: "?"
    };
    return letters[kind] || "?";
  }

  function lintLabel(kind) {
    const labels = {
      entry: "Entrypoint",
      abi: "ABI shim",
      cxx: "C++ helper",
      r: "R helper",
      neutral: "Neutral helper",
      unwind: "Trusted unwind",
      unclassified: "Unclassified",
      other: "Other tag"
    };
    return labels[kind] || kind;
  }

  function formatLocation(path, line) {
    if (!path) return "";
    return line === null || line === undefined ? path : path + ":" + line;
  }

  function booleanLabel(value) {
    if (value === true) return "yes";
    if (value === false) return "no";
    return "";
  }

  function normalizeBoolean(value) {
    if (value === true || value === false) return value;
    if (value === 1 || value === "1" || value === "true" || value === "yes") return true;
    if (value === 0 || value === "0" || value === "false" || value === "no") return false;
    return null;
  }

  function numberOrNull(value) {
    const parsed = Number(value);
    return value === "" || value === null || value === undefined || !Number.isFinite(parsed) ? null : parsed;
  }

  function text(value) {
    return value === null || value === undefined ? "" : String(value);
  }

  function basename(path) {
    const pieces = text(path).split(/[\\/]/);
    return pieces[pieces.length - 1] || path;
  }

  function trimMiddle(value, maxLength) {
    const stringValue = text(value);
    if (stringValue.length <= maxLength) return stringValue;
    const left = Math.ceil((maxLength - 1) / 2);
    const right = Math.floor((maxLength - 1) / 2);
    return stringValue.slice(0, left) + "…" + stringValue.slice(-right);
  }

  function compareText(a, b) {
    return text(a).localeCompare(text(b), undefined, { numeric: true, sensitivity: "base" });
  }

  function moduleSort(a, b) {
    const ai = STANDARD_MODULES.indexOf(a);
    const bi = STANDARD_MODULES.indexOf(b);
    if (ai !== -1 || bi !== -1) {
      if (ai === -1) return 1;
      if (bi === -1) return -1;
      return ai - bi;
    }
    return compareText(a, b);
  }

  function entitySort(a, b) {
    return compareText(a.kind, b.kind) || compareText(a.qualified_name, b.qualified_name);
  }

  function unique(values) {
    return Array.from(new Set(values));
  }

  function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
  }

  function pushMapList(map, key, value) {
    if (!key) return;
    if (!map.has(key)) map.set(key, []);
    map.get(key).push(value);
  }

  function addDirection(map, key, value) {
    if (!map.has(key)) map.set(key, new Set());
    map.get(key).add(value);
  }

  function mostCommon(values) {
    const counts = new Map();
    for (const value of values) counts.set(value, (counts.get(value) || 0) + 1);
    const ranked = Array.from(counts.entries()).sort(function (a, b) {
      return b[1] - a[1] || compareText(a[0], b[0]);
    });
    return ranked.length ? ranked[0][0] : undefined;
  }

  function hash(value) {
    let result = 2166136261;
    const stringValue = text(value);
    for (let i = 0; i < stringValue.length; i += 1) {
      result ^= stringValue.charCodeAt(i);
      result = Math.imul(result, 16777619);
    }
    return result >>> 0;
  }

  function deterministicJitter(value) {
    return (hash(value) % 2001 - 1000) / 100;
  }

  function debounce(fn, delay) {
    let timer = null;
    return function () {
      const args = arguments;
      clearTimeout(timer);
      timer = setTimeout(function () { fn.apply(null, args); }, delay);
    };
  }

  function showFatal(message) {
    els.error.hidden = false;
    els.error.textContent = message;
    els.status.textContent = "Code-map data unavailable";
  }
}());
