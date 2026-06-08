"""
Network simulator.

Drives flows through a topology using a chosen routing strategy, collects
per-flow throughput, and records link utilizations.

Simulation model
----------------
* Time is discretized into slots; each slot processes flows that arrive
  before that slot's deadline.
* Link capacities are shared fairly among concurrent flows (max-min fairness
  approximation via weighted assignment).
* No explicit packet-level queuing — focus is on load-distribution analysis.
"""

from typing import List, Dict
from ..flows.flow_generator import Flow
from ..utils.metrics import MetricsCollector


class NetworkSimulator:
    def __init__(self, topology, router):
        self.topo    = topology
        self.router  = router
        self.metrics = MetricsCollector()

    # ------------------------------------------------------------------
    def run(self, flows: List[Flow]) -> Dict:
        """
        Route all flows, assign bandwidth, record throughput.
        Returns aggregate metrics dict.
        """
        self.topo.reset_utilization()

        link_utils_over_time: List[Dict] = []

        for flow in flows:
            if hasattr(self.router, 'spray_elephant_only'):
                # Packet Spraying router
                paths = self.router.route_flow(flow)
                if paths:
                    tput = self.router.assign_bandwidth(flow, paths)
                    flow.actual_throughput_mbps = tput
                    flow.completion_time = MetricsCollector.flow_completion_time(flow)
            else:
                # ECMP router
                path = self.router.route_flow(flow)
                if path:
                    tput = self.router.assign_bandwidth(flow, path)
                    flow.actual_throughput_mbps = tput
                    flow.completion_time = MetricsCollector.flow_completion_time(flow)

        link_utils = self.router.compute_link_utilizations(flows)
        return self.metrics.aggregate(flows, link_utils), link_utils

    # ------------------------------------------------------------------
    def run_and_report(self, flows: List[Flow], label: str = "") -> Dict:
        metrics, link_utils = self.run(flows)
        tag = label or f"{self.topo.summary()['topology']} / {self.router.name}"
        MetricsCollector.print_report(tag, metrics)
        return metrics, link_utils
