"""
Spine-Leaf (Clos) topology implementation.

Structure:
  - `num_spine` spine switches (fully connected to every leaf)
  - `num_leaf`  leaf switches
  - `hosts_per_leaf` hosts attached to each leaf

Every leaf switch connects to every spine switch, giving
num_leaf * num_spine uplinks for even load distribution.
"""

import networkx as nx


class SpineLeafTopology:
    def __init__(self, num_spine: int = 4, num_leaf: int = 8, hosts_per_leaf: int = 4):
        self.num_spine = num_spine
        self.num_leaf = num_leaf
        self.hosts_per_leaf = hosts_per_leaf
        self.graph = nx.Graph()
        self._build()

    # ------------------------------------------------------------------
    def _build(self):
        # Spine switches
        for s in range(self.num_spine):
            self.graph.add_node(f"spine_{s}", layer="spine", type="switch")

        # Leaf switches
        for l in range(self.num_leaf):
            self.graph.add_node(f"leaf_{l}", layer="leaf", type="switch")

            # Hosts
            for h in range(self.hosts_per_leaf):
                host_id = f"leaf{l}_host{h}"
                self.graph.add_node(host_id, layer="host", type="host", leaf=l)
                self.graph.add_edge(
                    f"leaf_{l}", host_id,
                    capacity=1000, utilization=0.0, link_type="leaf-host"
                )

            # Leaf <-> Spine links (full mesh)
            for s in range(self.num_spine):
                self.graph.add_edge(
                    f"leaf_{l}", f"spine_{s}",
                    capacity=10000, utilization=0.0, link_type="leaf-spine"
                )

    # ------------------------------------------------------------------
    def get_hosts(self):
        return [n for n, d in self.graph.nodes(data=True) if d.get("type") == "host"]

    def get_switches(self):
        return [n for n, d in self.graph.nodes(data=True) if d.get("type") == "switch"]

    def get_all_paths(self, src: str, dst: str):
        return list(nx.all_simple_paths(self.graph, src, dst))

    def reset_utilization(self):
        for u, v in self.graph.edges():
            self.graph[u][v]["utilization"] = 0.0

    def summary(self) -> dict:
        return {
            "topology": "Spine-Leaf",
            "num_spine": self.num_spine,
            "num_leaf": self.num_leaf,
            "hosts_per_leaf": self.hosts_per_leaf,
            "num_hosts": len(self.get_hosts()),
            "num_switches": len(self.get_switches()),
            "num_links": self.graph.number_of_edges(),
        }
