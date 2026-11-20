# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Coordinate logical boundary slots using every incident home's measured pin interval."""

import math
from bisect import bisect_left
from collections import defaultdict
from copy import copy
from functools import lru_cache
from statistics import median

from chunk_score import contribution, span, union


def shared_edge(a, b):
    for axis in (0, 1):
        fixed = 1 - axis
        if a[fixed + 2] == b[fixed]:
            line = b[fixed]
        elif b[fixed + 2] == a[fixed]:
            line = a[fixed]
        else:
            continue
        lo, hi = max(a[axis], b[axis]), min(a[axis + 2], b[axis + 2])
        if hi > lo:
            return axis, line, lo, hi
    return None


def perimeter(home):
    if "perimeter_dbu" in home:
        return tuple(
            (int(k), line, lo, hi) for k, line, lo, hi in home["perimeter_dbu"]
        )
    a, b, c, d = home["box_dbu"]
    return ((1, a, b, d), (0, b, a, c), (1, c, b, d), (0, d, a, c))


def project(home, point):
    def closest(edge):
        axis, line, lo, hi = edge
        q = [line, line]
        q[axis] = min(hi, max(lo, point[axis]))
        return q

    return min(map(closest, perimeter(home)), key=lambda q: math.dist(point, q))


def common_segments(first, second):
    if "perimeter_dbu" not in first and "perimeter_dbu" not in second:
        edge = shared_edge(first["box_dbu"], second["box_dbu"])
        return (edge,) if edge else ()
    return tuple(
        sorted(
            {
                (k, line, max(lo, u), min(hi, v))
                for k, line, lo, hi in perimeter(first)
                for axis, fixed, u, v in perimeter(second)
                if k == axis and line == fixed and max(lo, u) < min(hi, v)
            }
        )
    )


def distance_to(edge, point):
    axis, line, lo, hi = edge
    return abs(point[1 - axis] - line) + max(0, lo - point[axis], point[axis] - hi)


def lane_coordinates(patterns, axis, lo, hi, pitch, spacing=(1, 0, 0, 0)):
    """Return preferred-layer tracks indexed by coordinate along the seam."""
    lanes = set()
    for p in patterns:
        if p["direction"] == axis:
            continue
        # A horizontal seam needs vertical wires. Its varying coordinate is X.
        tracks, corner_tracks, corner_cap, spread = spacing
        margin = corner_tracks * p["step"]
        if corner_cap > 0:
            margin = min(margin, corner_cap)
        first = max(0, math.ceil((lo + margin - p["start"]) / p["step"]))
        last = min(p["count"] - 1, math.floor((hi - margin - p["start"]) / p["step"]))
        stride = max(1, math.ceil(max(pitch, spread, tracks * p["step"]) / p["step"]))
        first += (-first) % stride
        for i in range(first, last + 1, stride):
            lanes.add((p["start"] + i * p["step"], p["layer"]))
    if not patterns:
        tracks, corner_tracks, corner_cap, spread = spacing
        step = pitch * max(1, math.ceil(max(tracks, spread / pitch)))
        margin = corner_tracks * pitch
        if corner_cap > 0:
            margin = min(margin, corner_cap)
        lanes = {
            (i * step, "")
            for i in range(
                math.ceil((lo + margin) / step), math.floor((hi - margin) / step) + 1
            )
        }
    return lanes


def seam_paths(segments):
    """Orient each contiguous shared polyline by its endpoints. Distance follows the bends."""
    graph = defaultdict(list)
    for axis, line, lo, hi in segments:
        a, b = ((line, lo), (line, hi)) if axis else ((lo, line), (hi, line))
        graph[a].append(b)
        graph[b].append(a)
    paths = []
    for start in sorted(p for p, adj in graph.items() if len(adj) == 1):
        if not graph[start]:
            continue
        p = start
        distance = 0
        path = []
        while graph[p]:
            q = graph[p].pop()
            graph[q].remove(p)
            path.append((p, q, distance))
            distance += abs(p[0] - q[0]) + abs(p[1] - q[1])
            p = q
        paths.append(path)
    return paths


def order_bus_slots(
    segments, assignments, bits, skip, intervals, available, reserved, occupied, stats
):
    # Preserve the occupied slot set when possible. Two layers can carry different bits at the
    # same tangent coordinate. Borrow a free slot for those ties to keep geometric order strict.
    groups = defaultdict(list)
    paths = seam_paths(segments)

    def position(point):
        x, y = point
        for component, path in enumerate(paths):
            for (u, v), (r, t), distance in path:
                if min(u, r) <= x <= max(u, r) and min(v, t) <= y <= max(v, t):
                    return component, distance + abs(x - u) + abs(y - v)
        return None

    for i, (net, owners, point, layer) in enumerate(assignments):
        if net not in bits:
            continue
        bus, bit = bits[net]
        at = position(point)
        if at is not None:
            groups[owners, bus, at[0]].append((bit, i, at[1]))
    spare = None
    for (owners, bus, component), group in groups.items():
        if len(group) < 2:
            continue
        stats["groups"] += 1
        if any((h, bus) in skip for h in owners) or len({v[0] for v in group}) != len(
            group
        ):
            stats["fixed_or_ambiguous"] += 1
            continue
        group.sort()
        original = [v[2] for v in group]
        if all(x < y for x, y in zip(original, original[1:])) or all(
            x > y for x, y in zip(original, original[1:])
        ):
            stats["ordered_before"] += 1
        chosen = {}
        duplicates = []
        for _, i, along in group:
            _, _, p, layer = assignments[i]
            if along in chosen:
                duplicates.append(along)
            else:
                chosen[along] = (p, layer)
        if duplicates:
            if spare is None:
                spare = []
                for x, y, layer in sorted(available - occupied):
                    if (x, y) not in reserved and (at := position((x, y))) is not None:
                        spare.append((at[0], at[1], x, y, layer))
                spare.sort()
            for along in duplicates:
                index = bisect_left(spare, (component, along))
                left, right = index - 1, index
                while left >= 0 or right < len(spare):
                    a = (
                        spare[left]
                        if left >= 0 and spare[left][0] == component
                        else None
                    )
                    b = (
                        spare[right]
                        if right < len(spare) and spare[right][0] == component
                        else None
                    )
                    if a is None and b is None:
                        break
                    if b is None or (a is not None and along - a[1] <= b[1] - along):
                        candidate = a
                        left -= 1
                    else:
                        candidate = b
                        right += 1
                    _, t, x, y, layer = candidate
                    if t not in chosen and (x, y, layer) not in occupied:
                        chosen[t] = ([x, y], layer)
                        break
        if len(chosen) != len(group):
            stats["insufficient_distinct_slots"] += 1
            continue
        slots = [v for _, v in sorted(chosen.items())]

        def cost(seq, group=group, owners=owners):
            result = 0
            for (_, i, _), (p, _) in zip(group, seq):
                net = assignments[i][0]
                for h in owners:
                    box = intervals.get(h, {}).get(net)
                    if box:
                        result += sum(
                            max(0, box[k] - p[k], p[k] - box[k + 2]) for k in (0, 1)
                        )
            return result

        reverse = slots[::-1]
        if cost(reverse) < cost(slots):
            slots = reverse
        for _, i, _ in group:
            _, _, p, layer = assignments[i]
            occupied.remove((*p, layer))
        for (_, i, _), (p, layer) in zip(group, slots):
            net, owners, old, old_layer = assignments[i]
            stats["moved_bits"] += int(old != p or old_layer != layer)
            assignments[i] = (net, owners, p, layer)
            occupied.add((*p, layer))
        stats["ordered_after"] += 1


def proposal(
    work, state, pitch=None, nets=None, *, intervals=None, spread=None, bus_order=False
):
    if pitch is None:
        pitch = work.plan.get("coarse_routing", {}).get("track_pitch_dbu")
    if not pitch or not math.isfinite(pitch) or pitch <= 0:
        raise ValueError("supply a positive --pitch in die DBU for this floorplan")
    pitch = max(1, round(pitch))
    selected = None if nets is None else set(nets)
    if selected and selected - work.incident.keys():
        raise ValueError("unknown interface net")

    @lru_cache(None)
    def shared(h, peer):
        return common_segments(work.chunks[h], work.chunks[peer])

    if intervals is None:
        intervals = {
            h: dict((contribution(work, state, h)[1] or {}).get("bounds", []))
            for h in work.jobs
        }
    patterns = work.plan.get("coarse_routing", {}).get("track_patterns", [])
    timing = {r[0]: r[1] for r in work.index.get("assembly", {}).get("timing", [])}
    policy = work.plan.get("coarse_routing", {}).get("io_spacing", {})
    use_spread = policy.get("enabled", False) if spread is None else spread
    if use_spread and not policy:
        raise ValueError(
            "re-export the floorplan to obtain the top-level IO spacing policy"
        )
    bits = (
        {
            p["net_index"]: (p["bus"], p["bit"])
            for c in work.plan["chunks"]
            for p in c["ports"]
            if "bit" in p
        }
        if bus_order
        else {}
    )
    skip = set()
    bus_stats = {
        "groups": 0,
        "ordered_before": 0,
        "ordered_after": 0,
        "fixed_or_ambiguous": 0,
        "insufficient_distinct_slots": 0,
        "moved_bits": 0,
    }
    edges = defaultdict(list)
    occupied = set()
    reserved = set()
    for net, homes in sorted(work.incident.items()):
        current = {
            h: state["contacts"].get(h, {}).get(str(net), p)
            for h, (_, p, _) in homes.items()
        }
        if net in work.anchored or (selected is not None and net not in selected):
            for h, p in current.items():
                if net in bits:
                    skip.add((h, bits[net][0]))
                layer = state.get("contact_layers", {}).get(h, {}).get(str(net))
                if layer:
                    occupied.add((*p, layer))
                else:
                    reserved.add(tuple(p))
            continue
        points = []
        for h, p in current.items():
            b = intervals.get(h, {}).get(net)
            points.append([(b[k] + b[k + 2]) / 2 for k in range(2)] if b else p)
        hub = [median(p[k] for p in points) for k in range(2)]
        # A multi-home net may enter several distinct seams. Each pair consumes one slot on its shared edge.
        pending = set(homes)
        while pending:
            h = min(pending)
            pending.remove(h)
            choices = []
            for peer in sorted(pending):
                segments = shared(h, peer)
                if segments:
                    choices.append(
                        (min(distance_to(e, hub) for e in segments), peer, segments)
                    )
            if choices:
                _, peer, segments = min(choices)
                pending.remove(peer)
                owners = (h, peer)
            else:
                home = work.chunks[h]
                p = project(home, hub)
                # At a corner retain the original vertical-edge tie break.
                edge = min(
                    (e for e in perimeter(home) if distance_to(e, p) == 0),
                    key=lambda e: -e[0],
                )
                segments = (edge,)
                owners = (h,)
            edges[segments].append((hub, net, owners))
    edits = defaultdict(dict)
    layers = defaultdict(dict)
    conflicts = []
    counts = []
    changed = set()
    for segments, bank in sorted(edges.items()):
        levels = [(1, 0, 0, 0)]
        if use_spread:
            length = sum(hi - lo for _, _, lo, hi in segments)
            floor = (
                min(policy["max_pitch_dbu"], policy["share"] * length / len(bank))
                if len(bank) >= policy["wide_count"]
                else 0
            )
            tracks, corner, cap = (
                policy[k] for k in ("tracks", "corner_tracks", "corner_cap_dbu")
            )
            levels = [
                (tracks, corner, cap, floor),
                (tracks, corner, cap, 0),
                (tracks, 0, 0, 0),
                *levels,
            ]
        for level, spacing in enumerate(levels):
            banks = [
                sorted(lane_coordinates(patterns, k, lo, hi, pitch, spacing))
                for k, _, lo, hi in segments
            ]
            slots = {
                (coord, fixed, layer) if k == 0 else (fixed, coord, layer)
                for (k, fixed, _, _), lanes in zip(segments, banks)
                for coord, layer in lanes
            }
            if not use_spread or sum(
                p not in occupied and p[:2] not in reserved for p in slots
            ) >= len(bank):
                break
        axis, line, lo, hi = segments[0]
        counts.append(
            {
                "axis": axis,
                "line": line,
                "span": [lo, hi],
                "signals": len(bank),
                "slots": len(slots),
                "segments": segments,
                "spacing_level": level,
            }
        )
        assignments = []
        for hub, net, owners in sorted(
            bank,
            key=lambda row: (
                timing.get(row[1], math.inf),
                row[0][axis],
                row[1],
                row[2],
            ),
        ):
            options = []
            for i, ((k, fixed, _, _), free) in enumerate(zip(segments, banks)):
                while free:
                    at = bisect_left(free, (hub[k], ""))
                    if at == len(free) or (
                        at and hub[k] - free[at - 1][0] <= free[at][0] - hub[k]
                    ):
                        at -= 1
                    coord, layer = free[at]
                    pt = (coord, fixed) if k == 0 else (fixed, coord)
                    if pt in reserved or (*pt, layer) in occupied:
                        free.pop(at)
                        continue
                    options.append(
                        (
                            abs(coord - hub[k]) + abs(fixed - hub[1 - k]),
                            i,
                            at,
                            pt,
                            layer,
                        )
                    )
                    break
            if not options:
                conflicts.append(
                    {
                        "net": net,
                        "homes": owners,
                        "line": line,
                        "span": [lo, hi],
                        "segments": segments,
                    }
                )
                continue
            _, i, at, pt, layer = min(options)
            banks[i].pop(at)
            point = list(pt)
            occupied.add((*point, layer))
            assignments.append((net, owners, point, layer))
        if bus_order:
            order_bus_slots(
                segments,
                assignments,
                bits,
                skip,
                intervals,
                slots,
                reserved,
                occupied,
                bus_stats,
            )
        for net, owners, point, layer in assignments:
            for h in owners:
                key = str(net)
                old = state["contacts"].get(h, {}).get(key, work.incident[net][h][1])
                if point != old or (layer or None) != state.get(
                    "contact_layers", {}
                ).get(h, {}).get(key):
                    changed.add(net)
                    edits[h][key] = point
                    if layer:
                        layers[h][key] = layer
    # Version the complete hypernet contract, including peers whose tap stayed at the same point.
    for net in changed:
        for h, (_, xy, _) in work.incident[net].items():
            edits[h].setdefault(
                str(net), state["contacts"].get(h, {}).get(str(net), xy)
            )
    return {
        "pitch_dbu": pitch,
        "spread": use_spread,
        "bus_order": bus_stats if bus_order else None,
        "feasible": not conflicts,
        "edges": counts,
        "conflicts": conflicts,
        "updates": dict(edits),
        "layers": dict(layers),
    }


def install(state, contract):
    if not contract["feasible"]:
        raise ValueError("insufficient interface slots at this pitch")
    for home, changes in contract["updates"].items():
        state["contacts"].setdefault(home, {}).update(changes)
        state.setdefault("contact_layers", {}).setdefault(home, {}).update(
            contract.get("layers", {}).get(home, {})
        )
        revisions = state.setdefault("interface_revision", {})
        revisions[home] = revisions.get(home, 0) + 1
        for result in state["results"].get(home, {}).values():
            result.update(status="stale", reason="shared interface contract changed")


def screen_shapes(work, state, pitch=None):
    """Project each home's measured pin spans onto candidate boxes before buying another local solve."""
    from chunk_flow import boundary

    options = work.plan.get("shape_candidates", [])
    order = list(work.chunks)
    parts = {h: contribution(work, state, h)[1] for h in work.jobs}
    if set(parts) != set(order) or any(p is None for p in parts.values()):
        raise ValueError(
            "a full export with assembly pin bounds is required for shape screening"
        )
    assembly = work.index["assembly"]
    anchor_boxes = dict(assembly.get("anchors", []))
    fixed_taps = {
        p["net_index"]: p["box_dbu"]
        for p in work.plan["communication"]["fixed_contacts"]
    }
    results = []
    for rank, candidate in enumerate(options):
        projected = copy(work)
        projected.chunks = {
            h: {"box_dbu": b} for h, b in zip(order, candidate["boxes_dbu"])
        }
        projected.incident = defaultdict(dict)
        intervals = {}
        whole = dict(anchor_boxes)
        for h in order:
            old = work.chunks[h]["box_dbu"]
            box = projected.chunks[h]["box_dbu"]
            scale = [(box[i + 2] - box[i]) / (old[i + 2] - old[i]) for i in range(2)]

            def transform(coords, box=box, old=old, scale=scale):
                return [
                    box[i % 2] + (v - old[i % 2]) * scale[i % 2]
                    for i, v in enumerate(coords)
                ]

            bounds = intervals[h] = {n: transform(b) for n, b in parts[h]["bounds"]}
            for net, b in bounds.items():
                whole[net] = union(whole.get(net), b)
            for p in work.chunks[h]["ports"]:
                n = p["net_index"]
                anchor = fixed_taps.get(n)
                tap = (
                    boundary(box, [(anchor[i] + anchor[i + 2]) / 2 for i in range(2)])
                    if anchor
                    else transform(
                        state["contacts"].get(h, {}).get(str(n), p["xy_dbu"])
                    )
                )
                projected.incident[n][h] = (p["net"], tap, box)
        contract = proposal(projected, {"contacts": {}}, pitch, intervals=intervals)
        results.append(
            {
                "rank": rank,
                "center_hpwl_dbu": candidate["center_hpwl_dbu"],
                "projected_shared_hpwl_um": sum(span(b) for b in whole.values())
                / work.index["dbu_per_um"],
                "unassigned_contacts": len(contract["conflicts"]),
                "feasible": contract["feasible"],
                "busiest_seam": max(
                    (e["signals"] / max(1, e["slots"]) for e in contract["edges"]),
                    default=0,
                ),
            }
        )
    ordered = sorted(
        results, key=lambda r: (r["unassigned_contacts"], r["projected_shared_hpwl_um"])
    )
    lowest_span = math.inf
    frontier = []
    for row in ordered:
        if row["projected_shared_hpwl_um"] < lowest_span:
            frontier.append(row["rank"])
            lowest_span = row["projected_shared_hpwl_um"]
    return {
        "rank": work.plan.get("shape_rank"),
        "home_order": order,
        "shortlist": ordered,
        "pareto_ranks": frontier,
    }
