"""
Elephant flow detector.

In a real network this would run inside the switch using sketch-based
counters (e.g., Count-Min Sketch). Here we implement two modes:

  1. Oracle   – perfect knowledge (used for ground-truth comparison)
  2. Sketch   – Count-Min Sketch approximation (realistic simulation)
"""

import hashlib
import numpy as np
from typing import List, Dict
from .flow_generator import Flow, ELEPHANT_THRESHOLD


class CountMinSketch:
    """Lightweight Count-Min Sketch for byte counting per flow key."""

    def __init__(self, width: int = 1024, depth: int = 4, seed: int = 0):
        self.width = width
        self.depth = depth
        self.table = np.zeros((depth, width), dtype=np.int64)
        self.seeds = [seed + i * 31337 for i in range(depth)]

    def _hash(self, key: str, seed: int) -> int:
        h = hashlib.md5((key + str(seed)).encode()).hexdigest()
        return int(h, 16) % self.width

    def update(self, key: str, value: int):
        for d in range(self.depth):
            col = self._hash(key, self.seeds[d])
            self.table[d][col] += value

    def query(self, key: str) -> int:
        return min(
            self.table[d][self._hash(key, self.seeds[d])]
            for d in range(self.depth)
        )

    def reset(self):
        self.table[:] = 0


class ElephantFlowDetector:
    def __init__(
        self,
        threshold: int = ELEPHANT_THRESHOLD,
        mode: str = "oracle",   # "oracle" | "sketch"
        sketch_width: int = 2048,
        sketch_depth: int = 4,
    ):
        self.threshold = threshold
        self.mode = mode
        self.sketch = CountMinSketch(sketch_width, sketch_depth) if mode == "sketch" else None
        self._byte_counts: Dict[str, int] = {}

    # ------------------------------------------------------------------
    def observe_packet(self, flow_key: str, pkt_size: int):
        """Called for every packet to update byte counters."""
        if self.mode == "sketch":
            self.sketch.update(flow_key, pkt_size)
        else:
            self._byte_counts[flow_key] = self._byte_counts.get(flow_key, 0) + pkt_size

    def is_elephant(self, flow_key: str) -> bool:
        if self.mode == "sketch":
            return self.sketch.query(flow_key) >= self.threshold
        return self._byte_counts.get(flow_key, 0) >= self.threshold

    def classify_flows(self, flows: List[Flow]) -> Dict[str, List[Flow]]:
        """Oracle classification — uses ground-truth flow.is_elephant."""
        return {
            "elephant": [f for f in flows if f.is_elephant],
            "mouse":    [f for f in flows if not f.is_elephant],
        }

    def reset(self):
        if self.sketch:
            self.sketch.reset()
        self._byte_counts.clear()

    def detection_stats(self, flows: List[Flow]) -> dict:
        """Compute TP/FP/FN for sketch vs oracle ground truth."""
        if self.mode != "sketch":
            return {"mode": "oracle", "accuracy": 1.0}

        tp = fp = fn = tn = 0
        for f in flows:
            key = f"{f.src}->{f.dst}:{f.flow_id}"
            detected = self.is_elephant(key)
            actual = f.is_elephant
            if detected and actual:
                tp += 1
            elif detected and not actual:
                fp += 1
            elif not detected and actual:
                fn += 1
            else:
                tn += 1

        precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        recall    = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        accuracy  = (tp + tn) / len(flows) if flows else 0.0
        return {
            "mode": "sketch", "tp": tp, "fp": fp, "fn": fn, "tn": tn,
            "precision": precision, "recall": recall, "accuracy": accuracy,
        }
