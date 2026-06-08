"""
Packet Spraying of Elephant Flows — Academic Simulation
========================================================

Compares ECMP vs Packet Spraying on Fat-Tree and Spine-Leaf topologies.

Usage
-----
    python main.py [--flows N] [--k K] [--seed S] [--no-plots]

Parameters
----------
--flows  : total number of flows (default 300)
--k      : fat-tree pod count, must be even (default 4)
--seed   : random seed (default 42)
--no-plots: skip visualization (useful in headless environments)
"""

import argparse
import copy
import sys

from src.topologies import FatTreeTopology, SpineLeafTopology
from src.flows.flow_generator import FlowGenerator
from src.flows.elephant_detector import ElephantFlowDetector
from src.routing.ecmp import ECMPRouter
from src.routing.packet_spraying import PacketSprayingRouter
from src.simulation.simulator import NetworkSimulator
from src.utils.metrics import MetricsCollector
from visualization.plot_results import (
    plot_link_utilization_cdf,
    plot_throughput_comparison,
    plot_fct_comparison,
    plot_fairness_and_overload,
    plot_utilization_heatmap,
    plot_topology,
    plot_spray_vs_ecmp_paths,
)


# ──────────────────────────────────────────────────────────────────────
def parse_args():
    parser = argparse.ArgumentParser(description="Packet Spraying Simulation")
    parser.add_argument("--flows",    type=int,  default=300, help="Number of flows")
    parser.add_argument("--k",        type=int,  default=4,   help="Fat-tree k (even)")
    parser.add_argument("--seed",     type=int,  default=42,  help="Random seed")
    parser.add_argument("--no-plots", action="store_true",    help="Skip plotting")
    return parser.parse_args()


# ──────────────────────────────────────────────────────────────────────
def run_scenario(topo, router, flows_template, label):
    """Deep-copy flows and topology state, run simulation, return metrics."""
    flows = copy.deepcopy(flows_template)
    sim   = NetworkSimulator(topo, router)
    metrics, link_utils = sim.run_and_report(flows, label=label)
    return metrics, link_utils, flows


def main():
    args = parse_args()

    print("\n" + "="*60)
    print("  Packet Spraying of Elephant Flows — Simulation")
    print("="*60)

    # ── Build topologies ──────────────────────────────────────────────
    print("\n[1] Building topologies...")
    fat_tree   = FatTreeTopology(k=args.k)
    spine_leaf = SpineLeafTopology(num_spine=args.k, num_leaf=args.k * 2, hosts_per_leaf=args.k)

    for topo in [fat_tree, spine_leaf]:
        s = topo.summary()
        print(f"    {s['topology']}: {s['num_hosts']} hosts, "
              f"{s['num_switches']} switches, {s['num_links']} links")

    # ── Generate flows ────────────────────────────────────────────────
    print(f"\n[2] Generating {args.flows} flows (seed={args.seed})...")
    ft_gen = FlowGenerator(fat_tree.get_hosts(),   num_flows=args.flows, seed=args.seed)
    sl_gen = FlowGenerator(spine_leaf.get_hosts(), num_flows=args.flows, seed=args.seed)

    ft_flows = ft_gen.generate()
    sl_flows = sl_gen.generate()

    detector = ElephantFlowDetector(mode="oracle")
    ft_classified = detector.classify_flows(ft_flows)
    sl_classified = detector.classify_flows(sl_flows)

    print(f"    Fat-Tree  : {len(ft_classified['elephant'])} elephant, "
          f"{len(ft_classified['mouse'])} mouse")
    print(f"    Spine-Leaf: {len(sl_classified['elephant'])} elephant, "
          f"{len(sl_classified['mouse'])} mouse")

    # ── Run all four scenarios ─────────────────────────────────────────
    print("\n[3] Running simulations...")

    results = {}

    # Fat-Tree ECMP
    fat_tree.reset_utilization()
    m, u, flows = run_scenario(fat_tree, ECMPRouter(fat_tree), ft_flows, "Fat-Tree / ECMP")
    results["ECMP_FatTree"] = (m, u, flows)

    # Fat-Tree Packet Spraying
    fat_tree.reset_utilization()
    m, u, flows = run_scenario(fat_tree, PacketSprayingRouter(fat_tree), ft_flows, "Fat-Tree / Packet Spraying")
    results["Spray_FatTree"] = (m, u, flows)

    # Spine-Leaf ECMP
    spine_leaf.reset_utilization()
    m, u, flows = run_scenario(spine_leaf, ECMPRouter(spine_leaf), sl_flows, "Spine-Leaf / ECMP")
    results["ECMP_SpineLeaf"] = (m, u, flows)

    # Spine-Leaf Packet Spraying
    spine_leaf.reset_utilization()
    m, u, flows = run_scenario(spine_leaf, PacketSprayingRouter(spine_leaf), sl_flows, "Spine-Leaf / Packet Spraying")
    results["Spray_SpineLeaf"] = (m, u, flows)

    # ── Spray path stats ──────────────────────────────────────────────
    spray_ft_router = PacketSprayingRouter(fat_tree)
    spray_sl_router = PacketSprayingRouter(spine_leaf)

    # Re-route to collect path counts
    ft_flows_copy = copy.deepcopy(ft_flows)
    sl_flows_copy = copy.deepcopy(sl_flows)
    for f in ft_flows_copy: spray_ft_router.route_flow(f)
    for f in sl_flows_copy: spray_sl_router.route_flow(f)

    spray_stats_ft = spray_ft_router.spray_stats(ft_flows_copy)
    spray_stats_sl = spray_sl_router.spray_stats(sl_flows_copy)

    print(f"\n  Spray stats (Fat-Tree):   {spray_stats_ft}")
    print(f"  Spray stats (Spine-Leaf): {spray_stats_sl}")

    # ── Summary table ─────────────────────────────────────────────────
    print("\n[4] Summary")
    print(f"  {'Scenario':<28} {'Fairness':>10} {'MaxUtil':>10} {'Overloaded':>11} {'ElephTput(Mbps)':>16}")
    print("  " + "-"*77)
    for label, (m, u, _) in results.items():
        print(f"  {label:<28} {m['jain_fairness']:>10.4f} "
              f"{m['max_util']:>10.4f} {m['overloaded_links']:>11} "
              f"{m['avg_elephant_tput_mbps']:>16.2f}")

    # ── Plots ─────────────────────────────────────────────────────────
    if not args.no_plots:
        print("\n[5] Generating plots → results/")

        metrics_dict = {k: v[0] for k, v in results.items()}
        util_sets    = {k: v[1] for k, v in results.items()}

        plot_link_utilization_cdf(util_sets)
        plot_throughput_comparison(metrics_dict)
        plot_fct_comparison(metrics_dict)
        plot_fairness_and_overload(metrics_dict)
        plot_utilization_heatmap(util_sets)
        plot_topology(fat_tree,   "Fat-Tree")
        plot_topology(spine_leaf, "Spine-Leaf")
        plot_spray_vs_ecmp_paths(spray_stats_ft, spray_stats_sl)

        print("  All plots saved.")

    print("\nDone.\n")
    return results


if __name__ == "__main__":
    main()
