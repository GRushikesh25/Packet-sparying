"""
Equal-Cost Multi-Path (ECMP) routing.

ECMP hashes the 5-tuple (src, dst, sport, dport, proto) to select ONE path
from the set of equal-cost shortest paths. Every packet in a flow uses the
same path — good for TCP ordering, bad for elephant flow load balancing.
"""

import hashlib
import networkx as nx
from typing import List, Dict, Optional
from ..flows.flow_generator import Flow


class ECMPRouter:
    name = "ECMP"

    def __init__(self, topology):
        self.topo = topology
        self._path_cache: Dict[tuple, List[List[str]]] = {}

    # ------------------------------------------------------------------
    def _get_ecmp_paths(self, src: str, dst: str) -> List[List[str]]:
        key = (src, dst)
        if key not in self._path_cache:
            try:
                shortest = nx.shortest_path_length(self.topo.graph, src, dst)
                paths = [
                    p for p in nx.all_simple_paths(self.topo.graph, src, dst, cutoff=shortest)
                ]
            except (nx.NetworkXNoPath, nx.NodeNotFound):
                paths = []
            self._path_cache[key] = paths
        return self._path_cache[key]

    def _hash_flow(self, flow: Flow) -> int:
        key = f"{flow.src}|{flow.dst}|{flow.flow_id}"
        return int(hashlib.md5(key.encode()).hexdigest(), 16)

    # ------------------------------------------------------------------
    def route_flow(self, flow: Flow) -> Optional[List[str]]:
        """Return the single path selected for this flow."""
        paths = self._get_ecmp_paths(flow.src, flow.dst)
        if not paths:
            return None
        idx = self._hash_flow(flow) % len(paths)
        chosen = paths[idx]
        flow.assigned_paths = [chosen]
        flow.path_count = 1
        return chosen

    def assign_bandwidth(self, flow: Flow, path: List[str]) -> float:
        """
        Assign bandwidth on every link of the path.
        Returns the achievable throughput (limited by bottleneck link).
        """
        min_avail = float("inf")
        for u, v in zip(path[:-1], path[1:]):
            cap   = self.topo.graph[u][v]["capacity"]
            util  = self.topo.graph[u][v]["utilization"]
            avail = cap - util
            min_avail = min(min_avail, avail)

        granted = min(flow.demand_mbps, min_avail)
        for u, v in zip(path[:-1], path[1:]):
            self.topo.graph[u][v]["utilization"] += granted
        return granted

    def compute_link_utilizations(self, flows: List[Flow]) -> Dict[tuple, float]:
        utils = {}
        for u, v, d in self.topo.graph.edges(data=True):
            cap = d["capacity"]
            util = d["utilization"]
            utils[(u, v)] = util / cap if cap > 0 else 0.0
        return utils
