"""
Packet Spraying router for elephant flows.

Core idea
---------
For MOUSE flows  → single-path ECMP (preserves TCP ordering).
For ELEPHANT flows → spray packets across ALL equal-cost paths.

The spraying strategy used here is **weighted round-robin** based on
available link capacity, so packets are steered toward less-loaded paths.

References
----------
* Dixit et al., "Is It Time for Networks to Change?" HotNets 2013.
* Cao et al., "Per-packet Load-Balanced, Low-Latency Routing…" CoNEXT 2013.
"""

import hashlib
import networkx as nx
from typing import List, Dict, Optional, Tuple
from ..flows.flow_generator import Flow


class PacketSprayingRouter:
    name = "Packet Spraying"

    def __init__(self, topology, spray_elephant_only: bool = True):
        self.topo = topology
        self.spray_elephant_only = spray_elephant_only
        self._path_cache: Dict[Tuple[str, str], List[List[str]]] = {}

    # ------------------------------------------------------------------
    def _get_shortest_paths(self, src: str, dst: str) -> List[List[str]]:
        key = (src, dst)
        if key not in self._path_cache:
            try:
                shortest = nx.shortest_path_length(self.topo.graph, src, dst)
                paths = list(nx.all_simple_paths(self.topo.graph, src, dst, cutoff=shortest))
            except (nx.NetworkXNoPath, nx.NodeNotFound):
                paths = []
            self._path_cache[key] = paths
        return self._path_cache[key]

    def _path_available_bw(self, path: List[str]) -> float:
        """Minimum available bandwidth (Mbps) along a path."""
        avail = float("inf")
        for u, v in zip(path[:-1], path[1:]):
            cap  = self.topo.graph[u][v]["capacity"]
            util = self.topo.graph[u][v]["utilization"]
            avail = min(avail, cap - util)
        return max(avail, 0.0)

    def _path_weights(self, paths: List[List[str]]) -> List[float]:
        """Capacity-proportional weights for weighted round-robin spray."""
        bws = [self._path_available_bw(p) for p in paths]
        total = sum(bws)
        if total == 0:
            return [1.0 / len(paths)] * len(paths)
        return [b / total for b in bws]

    # ------------------------------------------------------------------
    def route_flow(self, flow: Flow) -> List[List[str]]:
        """
        Return the list of paths this flow's packets will be spread across.
        Mouse flows get a single ECMP-chosen path; elephant flows get all paths.
        """
        paths = self._get_shortest_paths(flow.src, flow.dst)
        if not paths:
            flow.assigned_paths = []
            flow.path_count = 0
            return []

        if not flow.is_elephant or not self.spray_elephant_only:
            # Single-path for mouse flows (preserves ordering)
            h = int(hashlib.md5(f"{flow.src}|{flow.dst}|{flow.flow_id}".encode()).hexdigest(), 16)
            chosen = [paths[h % len(paths)]]
        else:
            # Spray across all equal-cost paths
            chosen = paths

        flow.assigned_paths = chosen
        flow.path_count = len(chosen)
        return chosen

    def assign_bandwidth(self, flow: Flow, paths: List[List[str]]) -> float:
        """
        Distribute flow demand across paths proportional to available bandwidth.
        Returns total achievable throughput.
        """
        if not paths:
            return 0.0

        weights = self._path_weights(paths)
        total_granted = 0.0

        for path, w in zip(paths, weights):
            demand_share = flow.demand_mbps * w
            avail = self._path_available_bw(path)
            granted = min(demand_share, avail)

            for u, v in zip(path[:-1], path[1:]):
                self.topo.graph[u][v]["utilization"] += granted

            total_granted += granted

        return total_granted

    def compute_link_utilizations(self, flows: List[Flow]) -> Dict[tuple, float]:
        utils = {}
        for u, v, d in self.topo.graph.edges(data=True):
            cap  = d["capacity"]
            util = d["utilization"]
            utils[(u, v)] = util / cap if cap > 0 else 0.0
        return utils

    def spray_stats(self, flows: List[Flow]) -> dict:
        elephants = [f for f in flows if f.is_elephant]
        sprayed   = [f for f in elephants if f.path_count > 1]
        avg_paths = (
            sum(f.path_count for f in elephants) / len(elephants) if elephants else 0
        )
        return {
            "total_elephant_flows": len(elephants),
            "sprayed_flows": len(sprayed),
            "avg_paths_per_elephant": avg_paths,
        }
