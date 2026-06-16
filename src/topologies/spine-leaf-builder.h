#ifndef SPINE_LEAF_BUILDER_H
#define SPINE_LEAF_BUILDER_H

/**
 * spine-leaf-builder.h
 * --------------------
 * Encapsulates the creation and routing setup of a Spine-Leaf (2-tier Clos)
 * data-centre topology in NS-3.
 *
 * Topology
 * --------
 *   numSpine spine switches
 *   numLeaf  leaf  switches — each leaf is fully connected to every spine
 *   hostsPerLeaf hosts per leaf
 *   Total hosts = numLeaf * hostsPerLeaf
 *
 *   Link speeds:
 *     spine <-> leaf : 10 Gbps, 5  us
 *     leaf  <-> host : 1  Gbps, 10 us
 *
 * Usage:
 *   Ipv4AddressHelper addr;
 *   SpineLeafBuilder builder(4, 8, 4, true);  // numSpine, numLeaf, hostsPerLeaf, spray
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

class SpineLeafBuilder
{
public:
    /**
     * \param numSpine     Number of spine switches
     * \param numLeaf      Number of leaf switches
     * \param hostsPerLeaf Hosts attached to each leaf
     * \param useSpray     Install SprayRouting if true, else Ipv4StaticRouting
     */
    SpineLeafBuilder (uint32_t numSpine, uint32_t numLeaf,
                      uint32_t hostsPerLeaf, bool useSpray);

    /**
     * Build the topology: create nodes, install IP stacks, wire P2P links,
     * assign /30 addresses and populate routing tables on every node.
     */
    void Build (Ipv4AddressHelper &addr);

    /** Return a flat NodeContainer of all host nodes. */
    NodeContainer GetHosts () const;

    /** Return the host IP addresses in the same order as GetHosts(). */
    std::vector<Ipv4Address> GetHostAddresses () const;

private:
    uint32_t m_numSpine;
    uint32_t m_numLeaf;
    uint32_t m_hostsPerLeaf;
    bool     m_useSpray;

    // Populated by Build()
    NodeContainer            m_hosts;
    std::vector<Ipv4Address> m_hostAddrs;

    // Node arrays
    std::vector<Ptr<Node>>                       m_spine;
    std::vector<Ptr<Node>>                       m_leaf;
    std::vector<std::vector<Ptr<Node>>>          m_hostNodes;   // [leaf][host]
    std::vector<std::vector<Ipv4Address>>        m_hostAddr2;   // [leaf][host]

    // ── Internal helpers ──────────────────────────────────────────────
    std::pair<Ipv4Address, Ipv4Address>
    MakeLink (Ptr<Node> a, Ptr<Node> b,
              const std::string &bw, const std::string &delay,
              Ipv4AddressHelper &addr);

    void InstallSprayRouting (
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &spineLeafAddr,
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &leafHostAddr);

    void InstallStaticRouting (
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &spineLeafAddr,
        const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &leafHostAddr);
};

} // namespace ns3
#endif // SPINE_LEAF_BUILDER_H
