"""
Fat-Tree topology implementation.

A k-ary fat-tree has:
  - k pods, each with k/2 edge switches and k/2 aggregation switches
  - (k/2)^2 core switches
  - k^3/4 hosts total

Node naming:
  core_i         : core switch i
  pod{p}_agg{a}  : aggregation switch a in pod p
  pod{p}_edge{e} : edge switch e in pod p
  pod{p}_host{e}_{h}: host h under edge switch e in pod p
"""

import networkx as nx
import itertools


class FatTreeTopology:
    def __init__(self, k: int = 4):
        if k % 2 != 0 or k < 2:
            raise ValueError("k must be a positive even integer")
        self.k = k
        self.graph = nx.Graph()
        self._build()

    # ------------------------------------------------------------------
    def _build(self):
        k = self.k
        half = k // 2

        # Core switches: (k/2)^2
        core_switches = [f"core_{i}" for i in range(half * half)]
        for s in core_switches:
            self.graph.add_node(s, layer="core", type="switch")

        # Pods
        for p in range(k):
            # Aggregation switches
            agg_switches = [f"pod{p}_agg{a}" for a in range(half)]
            for s in agg_switches:
                self.graph.add_node(s, layer="aggregation", pod=p, type="switch")

            # Edge switches
            edge_switches = [f"pod{p}_edge{e}" for e in range(half)]
            for s in edge_switches:
                self.graph.add_node(s, layer="edge", pod=p, type="switch")

            # Hosts under each edge switch
            for e in range(half):
                for h in range(half):
                    host_id = f"pod{p}_host{e}_{h}"
                    self.graph.add_node(host_id, layer="host", pod=p, type="host")
                    self.graph.add_edge(
                        f"pod{p}_edge{e}", host_id,
                        capacity=1000, utilization=0.0, link_type="edge-host"
                    )

            # Edge <-> Aggregation links (each edge connects to every agg in pod)
            for e, a in itertools.product(range(half), range(half)):
                self.graph.add_edge(
                    f"pod{p}_edge{e}", f"pod{p}_agg{a}",
                    capacity=10000, utilization=0.0, link_type="edge-agg"
                )

            # Aggregation <-> Core links
            # Agg switch a connects to core switches in group a (stride of half)
            for a in range(half):
                for j in range(half):
                    core_idx = a * half + j
                    self.graph.add_edge(
                        f"pod{p}_agg{a}", f"core_{core_idx}",
                        capacity=10000, utilization=0.0, link_type="agg-core"
                    )

    # ------------------------------------------------------------------
    def get_hosts(self):
        return [n for n, d in self.graph.nodes(data=True) if d.get("type") == "host"]

    def get_switches(self):
        return [n for n, d in self.graph.nodes(data=True) if d.get("type") == "switch"]

    def get_all_paths(self, src: str, dst: str):
        """Return all simple paths between src and dst (switches only in middle)."""
        return list(nx.all_simple_paths(self.graph, src, dst))

    def reset_utilization(self):
        for u, v in self.graph.edges():
            self.graph[u][v]["utilization"] = 0.0

    def summary(self) -> dict:
        hosts = self.get_hosts()
        switches = self.get_switches()
        return {
            "topology": "Fat-Tree",
            "k": self.k,
            "num_hosts": len(hosts),
            "num_switches": len(switches),
            "num_links": self.graph.number_of_edges(),
            "num_core_switches": (self.k // 2) ** 2,
            "num_pods": self.k,
        }
