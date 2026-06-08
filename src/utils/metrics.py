"""
Metrics collection and analysis.

Collected metrics
-----------------
* Per-flow: throughput, FCT (flow completion time), path count
* Per-topology: link utilization distribution, max/avg/std util, Jain fairness
* Aggregate: elephant vs mouse throughput, overall FCT
"""

import numpy as np
from typing import List, Dict
from ..flows.flow_generator import Flow


class MetricsCollector:

    @staticmethod
    def flow_completion_time(flow: Flow) -> float:
        """FCT in seconds = size / throughput (lower bound, no queuing model)."""
        if flow.actual_throughput_mbps > 0:
            return (flow.size_bytes * 8) / (flow.actual_throughput_mbps * 1e6)
        return float("inf")

    # ------------------------------------------------------------------
    @staticmethod
    def link_utilization_stats(link_utils: Dict[tuple, float]) -> dict:
        vals = list(link_utils.values())
        if not vals:
            return {}
        arr = np.array(vals)
        return {
            "max_util":  float(arr.max()),
            "avg_util":  float(arr.mean()),
            "std_util":  float(arr.std()),
            "p95_util":  float(np.percentile(arr, 95)),
            "overloaded_links": int((arr > 1.0).sum()),
        }

    @staticmethod
    def jain_fairness(link_utils: Dict[tuple, float]) -> float:
        """Jain's Fairness Index on link utilizations (1.0 = perfectly fair)."""
        vals = np.array(list(link_utils.values()), dtype=float)
        if vals.sum() == 0:
            return 1.0
        n = len(vals)
        return float((vals.sum() ** 2) / (n * (vals ** 2).sum()))

    # ------------------------------------------------------------------
    @staticmethod
    def aggregate(flows: List[Flow], link_utils: Dict[tuple, float]) -> dict:
        elephants = [f for f in flows if f.is_elephant]
        mice      = [f for f in flows if not f.is_elephant]

        def avg_fct(flist):
            fcts = [MetricsCollector.flow_completion_time(f) for f in flist
                    if f.actual_throughput_mbps > 0]
            return float(np.mean(fcts)) if fcts else float("inf")

        def avg_tput(flist):
            t = [f.actual_throughput_mbps for f in flist]
            return float(np.mean(t)) if t else 0.0

        util_stats = MetricsCollector.link_utilization_stats(link_utils)
        fairness   = MetricsCollector.jain_fairness(link_utils)

        return {
            "total_flows":        len(flows),
            "elephant_flows":     len(elephants),
            "mouse_flows":        len(mice),
            "avg_elephant_fct_s": avg_fct(elephants),
            "avg_mouse_fct_s":    avg_fct(mice),
            "avg_elephant_tput_mbps": avg_tput(elephants),
            "avg_mouse_tput_mbps":    avg_tput(mice),
            "jain_fairness":      fairness,
            **util_stats,
        }

    @staticmethod
    def print_report(label: str, metrics: dict):
        print(f"\n{'='*60}")
        print(f"  {label}")
        print(f"{'='*60}")
        for k, v in metrics.items():
            if isinstance(v, float):
                print(f"  {k:35s}: {v:.4f}")
            else:
                print(f"  {k:35s}: {v}")
