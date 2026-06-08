"""
Visualization module — generates all plots for the academic report.
"""

import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import networkx as nx
import seaborn as sns

OUTPUT_DIR = "results"
os.makedirs(OUTPUT_DIR, exist_ok=True)

PALETTE = {
    "ECMP_FatTree":       "#E74C3C",
    "Spray_FatTree":      "#2ECC71",
    "ECMP_SpineLeaf":     "#3498DB",
    "Spray_SpineLeaf":    "#F39C12",
}

# ──────────────────────────────────────────────────────────────────────
def _save(fig, name):
    path = os.path.join(OUTPUT_DIR, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  [saved] {path}")


# ──────────────────────────────────────────────────────────────────────
def plot_link_utilization_cdf(util_sets: dict, title_suffix: str = ""):
    """CDF of link utilization ratios for each scenario."""
    fig, ax = plt.subplots(figsize=(8, 5))

    for label, utils in util_sets.items():
        vals = sorted(utils.values())
        cdf  = np.arange(1, len(vals) + 1) / len(vals)
        color = PALETTE.get(label, None)
        ax.plot(vals, cdf, label=label, linewidth=2, color=color)

    ax.axvline(1.0, color="black", linestyle="--", linewidth=1, alpha=0.6, label="100% capacity")
    ax.set_xlabel("Link Utilization Ratio", fontsize=12)
    ax.set_ylabel("CDF", fontsize=12)
    ax.set_title(f"Link Utilization CDF{' — ' + title_suffix if title_suffix else ''}", fontsize=13)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    _save(fig, f"link_util_cdf{'_' + title_suffix.replace(' ', '_') if title_suffix else ''}.png")


def plot_throughput_comparison(metrics_dict: dict):
    """Bar chart comparing elephant and mouse throughput per scenario."""
    labels = list(metrics_dict.keys())
    el_tput = [metrics_dict[l]["avg_elephant_tput_mbps"] for l in labels]
    mo_tput = [metrics_dict[l]["avg_mouse_tput_mbps"]    for l in labels]

    x = np.arange(len(labels))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 5))
    bars1 = ax.bar(x - width / 2, el_tput, width, label="Elephant flows", color="#E74C3C", alpha=0.85)
    bars2 = ax.bar(x + width / 2, mo_tput, width, label="Mouse flows",    color="#3498DB", alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=20, ha="right", fontsize=10)
    ax.set_ylabel("Avg Throughput (Mbps)", fontsize=12)
    ax.set_title("Average Flow Throughput by Scenario", fontsize=13)
    ax.legend(fontsize=10)
    ax.grid(axis="y", alpha=0.3)

    for bar in list(bars1) + list(bars2):
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2, h + 0.5, f"{h:.1f}",
                ha="center", va="bottom", fontsize=8)

    _save(fig, "throughput_comparison.png")


def plot_fct_comparison(metrics_dict: dict):
    """Bar chart of average FCT for elephant and mouse flows."""
    labels = list(metrics_dict.keys())
    el_fct = [metrics_dict[l]["avg_elephant_fct_s"] for l in labels]
    mo_fct = [metrics_dict[l]["avg_mouse_fct_s"]    for l in labels]

    # Cap inf values for display
    el_fct_disp = [v if v != float("inf") else 0 for v in el_fct]
    mo_fct_disp = [v if v != float("inf") else 0 for v in mo_fct]

    x = np.arange(len(labels))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar(x - width / 2, el_fct_disp, width, label="Elephant flows", color="#E74C3C", alpha=0.85)
    ax.bar(x + width / 2, mo_fct_disp, width, label="Mouse flows",    color="#3498DB", alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=20, ha="right", fontsize=10)
    ax.set_ylabel("Avg FCT (seconds)", fontsize=12)
    ax.set_title("Average Flow Completion Time by Scenario", fontsize=13)
    ax.legend(fontsize=10)
    ax.grid(axis="y", alpha=0.3)
    _save(fig, "fct_comparison.png")


def plot_fairness_and_overload(metrics_dict: dict):
    """Two-panel: Jain's fairness index + overloaded link count."""
    labels  = list(metrics_dict.keys())
    fairness = [metrics_dict[l]["jain_fairness"]    for l in labels]
    overload = [metrics_dict[l]["overloaded_links"] for l in labels]
    colors   = [PALETTE.get(l, "#95A5A6") for l in labels]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    ax1.bar(labels, fairness, color=colors, alpha=0.85)
    ax1.axhline(1.0, linestyle="--", color="black", linewidth=1)
    ax1.set_ylim(0, 1.1)
    ax1.set_ylabel("Jain's Fairness Index", fontsize=12)
    ax1.set_title("Load Balancing Fairness", fontsize=13)
    ax1.tick_params(axis="x", rotation=20)
    ax1.grid(axis="y", alpha=0.3)

    ax2.bar(labels, overload, color=colors, alpha=0.85)
    ax2.set_ylabel("# Overloaded Links (util > 100%)", fontsize=12)
    ax2.set_title("Link Congestion", fontsize=13)
    ax2.tick_params(axis="x", rotation=20)
    ax2.grid(axis="y", alpha=0.3)

    fig.tight_layout()
    _save(fig, "fairness_and_overload.png")


def plot_utilization_heatmap(util_sets: dict):
    """Heatmap of link utilization (rows = links, cols = scenarios)."""
    labels = list(util_sets.keys())
    # Intersect link keys
    all_keys = sorted(set(k for u in util_sets.values() for k in u.keys()))
    matrix   = np.array([[util_sets[l].get(k, 0.0) for l in labels] for k in all_keys])

    if matrix.shape[0] > 80:
        # Sample for readability
        idx    = np.random.choice(matrix.shape[0], 80, replace=False)
        matrix = matrix[idx]

    fig, ax = plt.subplots(figsize=(max(8, len(labels) * 2), 10))
    im = ax.imshow(matrix, aspect="auto", cmap="RdYlGn_r", vmin=0, vmax=1)
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=20, ha="right")
    ax.set_yticks([])
    ax.set_ylabel("Links (sampled)", fontsize=11)
    ax.set_title("Link Utilization Heatmap (green=low, red=high)", fontsize=13)
    fig.colorbar(im, ax=ax, label="Utilization Ratio")
    _save(fig, "utilization_heatmap.png")


def plot_topology(topo, name: str):
    """Draw topology graph with layer-based layout."""
    G = topo.graph
    pos = {}
    layers = {"core": [], "spine": [], "aggregation": [], "leaf": [], "edge": [], "host": []}
    for node, data in G.nodes(data=True):
        layer = data.get("layer", "host")
        layers[layer].append(node)

    layer_order = ["core", "spine", "aggregation", "leaf", "edge", "host"]
    y_map = {l: (len(layer_order) - 1 - i) * 2 for i, l in enumerate(layer_order)}

    for layer, nodes in layers.items():
        if not nodes:
            continue
        y = y_map.get(layer, 0)
        for i, node in enumerate(nodes):
            x = (i - len(nodes) / 2) * 1.5
            pos[node] = (x, y)

    layer_colors = {
        "core": "#E74C3C", "spine": "#E74C3C",
        "aggregation": "#F39C12", "leaf": "#3498DB",
        "edge": "#2ECC71", "host": "#BDC3C7",
    }
    node_colors = [layer_colors.get(G.nodes[n].get("layer", "host"), "#BDC3C7") for n in G.nodes()]

    fig, ax = plt.subplots(figsize=(14, 8))
    nx.draw(
        G, pos, ax=ax,
        node_color=node_colors, node_size=120,
        edge_color="#95A5A6", width=0.5, alpha=0.8,
        with_labels=False,
    )

    legend_patches = [
        mpatches.Patch(color=c, label=l)
        for l, c in layer_colors.items() if any(
            G.nodes[n].get("layer") == l for n in G.nodes()
        )
    ]
    ax.legend(handles=legend_patches, loc="upper right", fontsize=9)
    ax.set_title(f"{name} Topology", fontsize=14)
    _save(fig, f"topology_{name.lower().replace(' ', '_').replace('-', '_')}.png")


def plot_spray_vs_ecmp_paths(spray_stats_ft: dict, spray_stats_sl: dict):
    """Bar chart showing avg paths per elephant flow (ECMP=1 vs Spray)."""
    scenarios = ["Fat-Tree\nECMP", "Fat-Tree\nSpray", "Spine-Leaf\nECMP", "Spine-Leaf\nSpray"]
    avg_paths = [
        1,
        spray_stats_ft.get("avg_paths_per_elephant", 1),
        1,
        spray_stats_sl.get("avg_paths_per_elephant", 1),
    ]
    colors = [PALETTE["ECMP_FatTree"], PALETTE["Spray_FatTree"],
              PALETTE["ECMP_SpineLeaf"], PALETTE["Spray_SpineLeaf"]]

    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar(scenarios, avg_paths, color=colors, alpha=0.85)
    ax.set_ylabel("Avg Paths per Elephant Flow", fontsize=12)
    ax.set_title("Path Diversity: ECMP vs Packet Spraying", fontsize=13)
    ax.grid(axis="y", alpha=0.3)

    for bar, v in zip(bars, avg_paths):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 0.02, f"{v:.2f}",
                ha="center", va="bottom", fontsize=10)
    _save(fig, "path_diversity.png")
