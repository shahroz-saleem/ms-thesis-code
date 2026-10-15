import math
UNHEARD = 999


def grid_shape(n):
    """W = round(sqrt N) columns, L = ceil(N / W) rows"""
    cols = max(1, round(math.sqrt(n)))
    return math.ceil(n / cols), cols


def edges(topology, n):
    """Edges between positions 0..n-1."""
    if topology == "line":
        return [(i, i + 1) for i in range(n - 1)]
    if topology == "ring":
        return [(i, (i + 1) % n) for i in range(n)]
    _, cols = grid_shape(n)
    result = []
    for i in range(n):
        if (i + 1) % cols and i + 1 < n:
            result.append((i, i + 1))
        if i + cols < n:
            result.append((i, i + cols))
    return result


def diameter(topology, n):
    if topology == "line":
        return n - 1
    if topology == "ring":
        return n // 2
    rows, cols = grid_shape(n)
    return rows + cols - 2


def link_cost(rssi, a, b):
    """averaged over both directions, so a stronger link costs less"""
    values = [rssi[x][y] for x, y in ((a, b), (b, a)) if y in rssi.get(x, {})]
    return -sum(values) / len(values) if values else UNHEARD


def arrange(nodes, topology, rssi):
    pairs = edges(topology, len(nodes))
    cost = {(a, b): link_cost(rssi, a, b) for a in nodes for b in nodes if a != b}

    def score(order):
        return sorted((cost[order[u], order[v]] for u, v in pairs), reverse=True)

    def moved(order, i, j):
        order = list(order)
        if topology == "mesh":
            order[i], order[j] = order[j], order[i]
        else:
            order[i:j + 1] = reversed(order[i:j + 1])
        return order

    order = strongest_chain(nodes, cost)
    best = score(order)
    improved = True
    while improved:
        improved = False
        for i in range(len(order) - 1):
            for j in range(i + 1, len(order)):
                candidate = moved(order, i, j)
                candidate_score = score(candidate)
                if candidate_score < best:
                    order, best, improved = candidate, candidate_score, True
    return order


def strongest_chain(nodes, cost):
    """Starts anywhere and keep stepping to the strongest unused link"""
    chain = [nodes[0]]
    left = set(nodes[1:])
    while left:
        step = min(left, key=lambda n: (cost[chain[-1], n], n))
        chain.append(step)
        left.remove(step)
    return chain


def neighbors(order, topology):
    result = {node: [] for node in order}
    for u, v in edges(topology, len(order)):
        result[order[u]].append(order[v])
        result[order[v]].append(order[u])
    return {node: sorted(ids) for node, ids in result.items()}


def weakest_link(order, topology, rssi):
    return max(link_cost(rssi, order[u], order[v]) for u, v in edges(topology, len(order)))
