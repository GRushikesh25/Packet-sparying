/**
 * fat-tree-builder.cc
 * -------------------
 * Implementation of FatTreeBuilder: node creation, link wiring, IP
 * addressing and routing-table population for a k-ary Fat-Tree topology.
 */

#include "ns3/fat-tree-builder.h"

#include "ns3/spray-routing.h"
#include "ns3/spray-routing-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/net-device-container.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"

#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("FatTreeBuilder");

// Static subnet counter shared across all topology builders so that
// every point-to-point link gets a unique /30 from 10.x.y.0/30.
static uint32_t g_ftSubnet = 0;

// ── Constructor ──────────────────────────────────────────────────────
FatTreeBuilder::FatTreeBuilder (uint32_t k, bool useSpray)
    : m_k (k), m_half (k / 2), m_useSpray (useSpray)
{
    NS_ASSERT_MSG (k >= 2 && k % 2 == 0, "k must be even and >= 2");
}

// ── Build ────────────────────────────────────────────────────────────
void
FatTreeBuilder::Build (Ipv4AddressHelper &addr)
{
    uint32_t k    = m_k;
    uint32_t half = m_half;

    // ── Create node objects ────────────────────────────────────────────
    m_core.resize (half * half);
    for (auto &n : m_core) n = CreateObject<Node> ();

    m_agg.resize (k);
    m_edge.resize (k);
    m_hostNodes.resize (k);
    m_hostAddr3.resize (k);

    for (uint32_t p = 0; p < k; ++p) {
        m_agg[p].resize (half);
        m_edge[p].resize (half);
        m_hostNodes[p].resize (half);
        m_hostAddr3[p].resize (half);
        for (auto &n : m_agg[p])  n = CreateObject<Node> ();
        for (auto &n : m_edge[p]) n = CreateObject<Node> ();
        for (uint32_t e = 0; e < half; ++e) {
            m_hostNodes[p][e].resize (half);
            m_hostAddr3[p][e].resize (half);
            for (auto &n : m_hostNodes[p][e]) n = CreateObject<Node> ();
        }
    }

    // ── Install Internet stack ─────────────────────────────────────────
    NodeContainer allNodes;
    for (auto &n : m_core) allNodes.Add (n);
    for (uint32_t p = 0; p < k; ++p) {
        for (auto &n : m_agg[p])  allNodes.Add (n);
        for (auto &n : m_edge[p]) allNodes.Add (n);
        for (uint32_t e = 0; e < half; ++e)
            for (auto &n : m_hostNodes[p][e]) allNodes.Add (n);
    }

    if (m_useSpray) {
        SprayRoutingHelper sh;
        InternetStackHelper internet;
        internet.SetRoutingHelper (sh);
        internet.Install (allNodes);
    } else {
        InternetStackHelper internet;
        internet.Install (allNodes);
    }

    // ── Wire links ────────────────────────────────────────────────────
    // coreAggAddr[c][p]   = {coreIP, aggIP}
    std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>
        coreAggAddr (half * half, std::vector<std::pair<Ipv4Address,Ipv4Address>>(k));

    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t a = 0; a < half; ++a)
            for (uint32_t j = 0; j < half; ++j) {
                uint32_t c = a * half + j;
                auto [cA, aA] = MakeLink (m_core[c], m_agg[p][a], "10Gbps", "5us", addr);
                coreAggAddr[c][p] = {cA, aA};
            }

    // aggEdgeAddr[p][a][e] = {aggIP, edgeIP}
    std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>>
        aggEdgeAddr (k,
            std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>(
                half, std::vector<std::pair<Ipv4Address,Ipv4Address>>(half)));

    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t a = 0; a < half; ++a)
            for (uint32_t e = 0; e < half; ++e) {
                auto [aA, eA] = MakeLink (m_agg[p][a], m_edge[p][e], "10Gbps", "5us", addr);
                aggEdgeAddr[p][a][e] = {aA, eA};
            }

    // edgeHostAddr[p][e][h] = {edgeIP, hostIP}
    std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>>
        edgeHostAddr (k,
            std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>(
                half, std::vector<std::pair<Ipv4Address,Ipv4Address>>(half)));

    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e)
            for (uint32_t h = 0; h < half; ++h) {
                auto [eA, hA] = MakeLink (m_edge[p][e], m_hostNodes[p][e][h],
                                           "1Gbps", "10us", addr);
                edgeHostAddr[p][e][h] = {eA, hA};
                m_hostAddr3[p][e][h]  = hA;
            }

    // ── Populate routing tables ────────────────────────────────────────
    if (m_useSpray) {
        InstallSprayRouting (coreAggAddr, aggEdgeAddr, edgeHostAddr);
    } else {
        InstallStaticRouting (coreAggAddr, aggEdgeAddr, edgeHostAddr);
    }

    // ── Build flat host list ───────────────────────────────────────────
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e)
            for (uint32_t h = 0; h < half; ++h) {
                m_hosts.Add (m_hostNodes[p][e][h]);
                m_hostAddrs.push_back (m_hostAddr3[p][e][h]);
            }

    NS_LOG_INFO ("FatTreeBuilder: k=" << k
        << "  hosts=" << m_hosts.GetN ()
        << "  routing=" << (m_useSpray ? "spray" : "ecmp"));
}

// ── Accessors ────────────────────────────────────────────────────────
NodeContainer
FatTreeBuilder::GetHosts () const
{
    return m_hosts;
}

std::vector<Ipv4Address>
FatTreeBuilder::GetHostAddresses () const
{
    return m_hostAddrs;
}

// ── MakeLink ─────────────────────────────────────────────────────────
std::pair<Ipv4Address, Ipv4Address>
FatTreeBuilder::MakeLink (Ptr<Node> a, Ptr<Node> b,
                           const std::string &bw, const std::string &delay,
                           Ipv4AddressHelper &addrHelper)
{
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute  ("DataRate", StringValue (bw));
    p2p.SetChannelAttribute ("Delay",    StringValue (delay));

    NetDeviceContainer devs = p2p.Install (a, b);

    uint32_t A = (g_ftSubnet >> 8) & 0xFF;
    uint32_t B =  g_ftSubnet       & 0xFF;
    ++g_ftSubnet;

    std::ostringstream base;
    base << "10." << A << "." << B << ".0";
    addrHelper.SetBase (base.str ().c_str (), "255.255.255.252");

    Ipv4InterfaceContainer ifc = addrHelper.Assign (devs);
    return {ifc.GetAddress (0), ifc.GetAddress (1)};
}

// ── InstallSprayRouting ───────────────────────────────────────────────
void
FatTreeBuilder::InstallSprayRouting (
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &coreAggAddr,
    const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &aggEdgeAddr,
    const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &edgeHostAddr)
{
    uint32_t k    = m_k;
    uint32_t half = m_half;

    // Hosts: default route via edge switch
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e)
            for (uint32_t h = 0; h < half; ++h) {
                Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                    m_hostNodes[p][e][h]->GetObject<Ipv4>()->GetRoutingProtocol());
                sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                              edgeHostAddr[p][e][h].first, 1);
            }

    // Edge switches
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e) {
            Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                m_edge[p][e]->GetObject<Ipv4>()->GetRoutingProtocol());
            // Host routes (direct)
            for (uint32_t h = 0; h < half; ++h)
                sr->AddRoute (m_hostAddr3[p][e][h], Ipv4Mask ("255.255.255.255"),
                              m_hostAddr3[p][e][h], 1 + h);
            // ECMP default routes to agg switches
            uint32_t baseIface = 1 + half;
            for (uint32_t a = 0; a < half; ++a)
                sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                              aggEdgeAddr[p][a][e].first, baseIface + a);
        }

    // Aggregation switches
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t a = 0; a < half; ++a) {
            Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                m_agg[p][a]->GetObject<Ipv4>()->GetRoutingProtocol());
            // Routes down to edge switches in same pod
            for (uint32_t e = 0; e < half; ++e)
                sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                              aggEdgeAddr[p][a][e].second, 1 + e);
            // ECMP default routes up to core switches
            uint32_t baseIface = 1 + half;
            for (uint32_t j = 0; j < half; ++j) {
                uint32_t c = a * half + j;
                sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                              coreAggAddr[c][p].first, baseIface + j);
            }
        }

    // Core switches
    for (uint32_t c = 0; c < half * half; ++c) {
        Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
            m_core[c]->GetObject<Ipv4>()->GetRoutingProtocol());
        for (uint32_t p = 0; p < k; ++p)
            sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                          coreAggAddr[c][p].second, 1 + p);
    }
}

// ── InstallStaticRouting ──────────────────────────────────────────────
void
FatTreeBuilder::InstallStaticRouting (
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &coreAggAddr,
    const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &aggEdgeAddr,
    const std::vector<std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>> &edgeHostAddr)
{
    uint32_t k    = m_k;
    uint32_t half = m_half;

    Ipv4StaticRoutingHelper staticHelper;
    auto getStatic = [&](Ptr<Node> node) {
        return staticHelper.GetStaticRouting (node->GetObject<Ipv4> ());
    };

    // Hosts
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e)
            for (uint32_t h = 0; h < half; ++h)
                getStatic (m_hostNodes[p][e][h])->AddNetworkRouteTo (
                    Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                    edgeHostAddr[p][e][h].first, 1);

    // Edge switches
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t e = 0; e < half; ++e) {
            auto sr = getStatic (m_edge[p][e]);
            for (uint32_t h = 0; h < half; ++h)
                sr->AddHostRouteTo (m_hostAddr3[p][e][h], m_hostAddr3[p][e][h], 1 + h);
            uint32_t baseIface = 1 + half;
            for (uint32_t a = 0; a < half; ++a)
                sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                       aggEdgeAddr[p][a][e].first, baseIface + a, a + 1);
        }

    // Aggregation switches
    for (uint32_t p = 0; p < k; ++p)
        for (uint32_t a = 0; a < half; ++a) {
            auto sr = getStatic (m_agg[p][a]);
            for (uint32_t e = 0; e < half; ++e)
                sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                       aggEdgeAddr[p][a][e].second, 1 + e, e + 1);
            uint32_t baseIface = 1 + half;
            for (uint32_t j = 0; j < half; ++j) {
                uint32_t c = a * half + j;
                sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                       coreAggAddr[c][p].first, baseIface + j, j + 1);
            }
        }

    // Core switches
    for (uint32_t c = 0; c < half * half; ++c) {
        auto sr = getStatic (m_core[c]);
        for (uint32_t p = 0; p < k; ++p)
            sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                   coreAggAddr[c][p].second, 1 + p, p + 1);
    }
}

} // namespace ns3
