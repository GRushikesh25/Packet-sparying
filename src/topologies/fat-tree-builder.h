#ifndef FAT_TREE_BUILDER_H
#define FAT_TREE_BUILDER_H

/**
 * fat-tree-builder.h
 * ------------------
 * Encapsulates the creation and routing setup of a k-ary Fat-Tree
 * data-centre topology in NS-3.
 *
 * Topology (k=4 default)
 * ----------------------
 *   (k/2)^2  core switches
 *   k pods, each with k/2 aggregation and k/2 edge switches
 *   k^3/4    hosts  (k/2 hosts per edge switch)
 *
 *   Link speeds:
 *     core  <-> agg  : 10 Gbps, 5  us
 *     agg   <-> edge : 10 Gbps, 5  us
 *     edge  <-> host : 1  Gbps, 10 us
 *
 * Usage:
 *   Ipv4AddressHelper addr;
 *   FatTreeBuilder builder(4, true);   // k=4, spray routing
 *   builder.Build(addr);
 *   NodeContainer hosts = builder.GetHosts();
 *   std::vector<Ipv4Address> addrs = builder.GetHostAddresses();
 */

#include "ns3/node-container.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-address.h"

#include <vector>
#include <string>
#include <utility>

namespace ns3 {

class FatTreeBuilder
{
public:
    /**
     * \param k        Pod count (must be even and >= 2)
     * \param useSpray Install SprayRouting if true, else Ipv4StaticRouting
     */
    FatTreeBuilder (uint32_t k, bool useSpray);

    /**
     * Build the topology: create nodes, install IP stacks, wire P2P links,
     * assign /30 addresses and populate routing tables on every node.
     *
     * \param addr  Address helper (base address is managed internally via a
     *              static subnet counter shared with SpineLeafBuilder)
     */
    void Build (Ipv4AddressHelper &addr);

    /** Return a flat NodeContainer of all host nodes. */
    NodeContainer GetHosts () const;

    /** Return the host IP addresses in the same order as GetHosts(). */
    std::vector<Ipv4Address> GetHostAddresses () const;

private:
    uint32_t m_k;
    uint32_t m_half;
    bool     m_useSpray;

    // Populated by Build()
    NodeContainer            m_hosts;          ///< flat list of host nodes
    std::vector<Ipv4Address> m_hostAddrs;      ///< parallel address list

    // Internal node arrays (3-D indexing mirrors topology)
    std::vector<Ptr<Node>>                               m_core;
    std::vector<std::vector<Ptr<Node>>>                  m_agg;
    std::vector<std::vector<Ptr<Node>>>                  m_edge;
    std::vector<std::vector<std::vector<Ptr<Node>>>>     m_hostNodes;
    std::vector<std::vector<std::vector<Ipv4Address>>>   m_hostAddr3;

    // ── Internal helpers ──────────────────────────────────────────────
    std::pair<Ipv4Address, Ipv4Address>
    MakeLink (Ptr<Node> a, Ptr<Node> b,
              const std::string &bw, const std::string &delay,
              Ipv4AddressHelper &addr);

    void InstallSprayRouting (
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &coreAggAddr,
        const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &aggEdgeAddr,
        const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &edgeHostAddr);

    void InstallStaticRouting (
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &coreAggAddr,
        const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &aggEdgeAddr,
        const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &edgeHostAddr);
};

} // namespace ns3
#endif // FAT_TREE_BUILDER_H
