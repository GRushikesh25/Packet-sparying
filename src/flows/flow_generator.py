"""
Traffic flow generator.

Generates a mix of:
  - Mouse flows  : short-lived, small (< ELEPHANT_THRESHOLD bytes)
  - Elephant flows: long-lived, large (>= ELEPHANT_THRESHOLD bytes)

Flow model based on empirical datacenter traffic studies (Benson et al. 2010).
"""

import random
import numpy as np
from dataclasses import dataclass, field
from typing import List, Optional

ELEPHANT_THRESHOLD = 1_000_000   # 1 MB — flows above this are elephant flows


@dataclass
class Flow:
    flow_id: int
    src: str
    dst: str
    size_bytes: int              # total flow size
    demand_mbps: float           # requested bandwidth in Mbps
    start_time: float            # simulation time (seconds)
    is_elephant: bool = False
    packets: List[dict] = field(default_factory=list)
    assigned_paths: List[List[str]] = field(default_factory=list)

    # filled in by simulator
    completion_time: Optional[float] = None
    actual_throughput_mbps: float = 0.0
    path_count: int = 0


class FlowGenerator:
    def __init__(
        self,
        hosts: List[str],
        num_flows: int = 200,
        elephant_ratio: float = 0.20,   # 20% elephant, 80% mouse
        seed: int = 42,
    ):
        self.hosts = hosts
        self.num_flows = num_flows
        self.elephant_ratio = elephant_ratio
        self.rng = random.Random(seed)
        self.np_rng = np.random.default_rng(seed)

    # ------------------------------------------------------------------
    def generate(self) -> List[Flow]:
        flows: List[Flow] = []
        num_elephant = int(self.num_flows * self.elephant_ratio)
        num_mouse = self.num_flows - num_elephant

        arrival_times = np.cumsum(
            self.np_rng.exponential(scale=0.05, size=self.num_flows)
        )

        flow_id = 0
        for i in range(self.num_flows):
            src, dst = self._random_pair()
            t = float(arrival_times[i])
            is_elephant = (i < num_elephant)

            if is_elephant:
                # Pareto-distributed size: heavy tail, mean ~50 MB
                size = int(np.clip(
                    self.np_rng.pareto(1.2) * 10_000_000 + ELEPHANT_THRESHOLD,
                    ELEPHANT_THRESHOLD, 500_000_000
                ))
                demand = float(self.np_rng.uniform(100, 1000))  # Mbps
            else:
                # Exponential mouse flow: mean 100 KB
                size = int(np.clip(
                    self.np_rng.exponential(100_000),
                    1_000, ELEPHANT_THRESHOLD - 1
                ))
                demand = float(self.np_rng.uniform(1, 50))  # Mbps

            flows.append(Flow(
                flow_id=flow_id,
                src=src,
                dst=dst,
                size_bytes=size,
                demand_mbps=demand,
                start_time=t,
                is_elephant=is_elephant,
            ))
            flow_id += 1

        # Shuffle so elephants are not all at the start
        self.rng.shuffle(flows)
        for i, f in enumerate(flows):
            f.flow_id = i
        return flows

    # ------------------------------------------------------------------
    def _random_pair(self):
        src = self.rng.choice(self.hosts)
        dst = self.rng.choice(self.hosts)
        while dst == src:
            dst = self.rng.choice(self.hosts)
        return src, dst
