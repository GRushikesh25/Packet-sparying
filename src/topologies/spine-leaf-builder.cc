/**
 * spine-leaf-builder.cc
 * ---------------------
 * Implementation of SpineLeafBuilder: node creation, link wiring, IP
 * addressing and routing-table population for a Spine-Leaf topology.
 */

#include "ns3/spine-leaf-builder.h"

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

NS_LOG_COMPONENT_DEFINE ("SpineLeafBuilder");

// Static subnet counter (shared with fat-tree-builder.cc via different
// translation unit — each starts from 0; they must not be used together
// with a shared Ipv4AddressHelper, but in practice separate simulations
// run one builder at a time so this is fine).
static uint32_t g_slSubnet = 0;

// ── Constructor ──────────────────────────────────────────────────────
SpineLeafBuilder::SpineLeafBuilder (uint32_t numSpine, uint32_t numLeaf,
                                     uint32_t hostsPerLeaf, bool useSpray)
    : m_numSpine (numSpine),
      m_numLeaf  (numLeaf),
      m_hostsPerLeaf (hostsPerLeaf),
      m_useSpray (useSpray)
{
    NS_ASSERT_MSG (numSpine >= 1, "numSpine must be >= 1");
    NS_ASSERT_MSG (numLeaf  >= 1, "numLeaf must be >= 1");
    NS_ASSERT_MSG (hostsPerLeaf >= 1, "hostsPerLeaf must be >= 1");
}

// ── Build ────────────────────────────────────────────────────────────
void
SpineLeafBuilder::Build (Ipv4AddressHelper &addr)
{
    uint32_t nS = m_numSpine;
    uint32_t nL = m_numLeaf;
    uint32_t nH = m_hostsPerLeaf;

    // ── Create nodes ──────────────────────────────────────────────────
    m_spine.resize (nS);
    m_leaf.resize  (nL);
    m_hostNodes.resize (nL);
    m_hostAddr2.resize (nL);

    for (auto &n : m_spine) n = CreateObject<Node> ();
    for (uint32_t l = 0; l < nL; ++l) {
        m_leaf[l] = CreateObject<Node> ();
        m_hostNodes[l].resize (nH);
        m_hostAddr2[l].resize (nH);
        for (auto &n : m_hostNodes[l]) n = CreateObject<Node> ();
    }

    // ── Install Internet stack ─────────────────────────────────────────
    NodeContainer allNodes;
    for (auto &n : m_spine) allNodes.Add (n);
    for (uint32_t l = 0; l < nL; ++l) {
        allNodes.Add (m_leaf[l]);
        for (auto &n : m_hostNodes[l]) allNodes.Add (n);
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
    // spineLeafAddr[s][l] = {spineIP, leafIP}
    std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>
        spineLeafAddr (nS, std::vector<std::pair<Ipv4Address,Ipv4Address>>(nL));

    for (uint32_t s = 0; s < nS; ++s)
        for (uint32_t l = 0; l < nL; ++l) {
            auto [sA, lA] = MakeLink (m_spine[s], m_leaf[l], "10Gbps", "5us", addr);
            spineLeafAddr[s][l] = {sA, lA};
        }

    // leafHostAddr[l][h] = {leafIP, hostIP}
    std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>
        leafHostAddr (nL, std::vector<std::pair<Ipv4Address,Ipv4Address>>(nH));

    for (uint32_t l = 0; l < nL; ++l)
        for (uint32_t h = 0; h < nH; ++h) {
            auto [lA, hA] = MakeLink (m_leaf[l], m_hostNodes[l][h], "1Gbps", "10us", addr);
            leafHostAddr[l][h]  = {lA, hA};
            m_hostAddr2[l][h]   = hA;
        }

    // ── Populate routing tables ────────────────────────────────────────
    if (m_useSpray) {
        InstallSprayRouting (spineLeafAddr, leafHostAddr);
    } else {
        InstallStaticRouting (spineLeafAddr, leafHostAddr);
    }

    // ── Build flat host list ───────────────────────────────────────────
    for (uint32_t l = 0; l < nL; ++l)
        for (uint32_t h = 0; h < nH; ++h) {
            m_hosts.Add (m_hostNodes[l][h]);
            m_hostAddrs.push_back (m_hostAddr2[l][h]);
        }

    NS_LOG_INFO ("SpineLeafBuilder: spine=" << nS
        << "  leaf=" << nL << "  hostsPerLeaf=" << nH
        << "  hosts=" << m_hosts.GetN ()
        << "  routing=" << (m_useSpray ? "spray" : "ecmp"));
}

// ── Accessors ────────────────────────────────────────────────────────
NodeContainer
SpineLeafBuilder::GetHosts () const
{
    return m_hosts;
}

std::vector<Ipv4Address>
SpineLeafBuilder::GetHostAddresses () const
{
    return m_hostAddrs;
}

// ── MakeLink ─────────────────────────────────────────────────────────
std::pair<Ipv4Address, Ipv4Address>
SpineLeafBuilder::MakeLink (Ptr<Node> a, Ptr<Node> b,
                              const std::string &bw, const std::string &delay,
                              Ipv4AddressHelper &addrHelper)
{
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute  ("DataRate", StringValue (bw));
    p2p.SetChannelAttribute ("Delay",    StringValue (delay));

    NetDeviceContainer devs = p2p.Install (a, b);

    uint32_t A = (g_slSubnet >> 8) & 0xFF;
    uint32_t B =  g_slSubnet       & 0xFF;
    ++g_slSubnet;

    std::ostringstream base;
    base << "10." << A << "." << B << ".0";
    addrHelper.SetBase (base.str ().c_str (), "255.255.255.252");

    Ipv4InterfaceContainer ifc = addrHelper.Assign (devs);
    return {ifc.GetAddress (0), ifc.GetAddress (1)};
}

// ── InstallSprayRouting ───────────────────────────────────────────────
void
SpineLeafBuilder::InstallSprayRouting (
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &spineLeafAddr,
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &leafHostAddr)
{
    uint32_t nS = m_numSpine;
    uint32_t nL = m_numLeaf;
    uint32_t nH = m_hostsPerLeaf;

    // Hosts: default route via leaf
    for (uint32_t l = 0; l < nL; ++l)
        for (uint32_t h = 0; h < nH; ++h) {
            Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                m_hostNodes[l][h]->GetObject<Ipv4>()->GetRoutingProtocol());
            sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                          leafHostAddr[l][h].first, 1);
        }

    // Leaf switches
    for (uint32_t l = 0; l < nL; ++l) {
        Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
            m_leaf[l]->GetObject<Ipv4>()->GetRoutingProtocol());
        // Direct host routes
        for (uint32_t h = 0; h < nH; ++h)
            sr->AddRoute (m_hostAddr2[l][h], Ipv4Mask ("255.255.255.255"),
                          m_hostAddr2[l][h], 1 + h);
        // ECMP default routes to all spine switches
        uint32_t baseIface = 1 + nH;
        for (uint32_t s = 0; s < nS; ++s)
            sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                          spineLeafAddr[s][l].first, baseIface + s);
    }

    // Spine switches: default route to each leaf
    for (uint32_t s = 0; s < nS; ++s) {
        Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
            m_spine[s]->GetObject<Ipv4>()->GetRoutingProtocol());
        for (uint32_t l = 0; l < nL; ++l)
            sr->AddRoute (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                          spineLeafAddr[s][l].second, 1 + l);
    }
}

// ── InstallStaticRouting ──────────────────────────────────────────────
void
SpineLeafBuilder::InstallStaticRouting (
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &spineLeafAddr,
    const std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>> &leafHostAddr)
{
    uint32_t nS = m_numSpine;
    uint32_t nL = m_numLeaf;
    uint32_t nH = m_hostsPerLeaf;

    Ipv4StaticRoutingHelper staticHelper;
    auto getStatic = [&](Ptr<Node> node) {
        return staticHelper.GetStaticRouting (node->GetObject<Ipv4> ());
    };

    // Hosts
    for (uint32_t l = 0; l < nL; ++l)
        for (uint32_t h = 0; h < nH; ++h)
            getStatic (m_hostNodes[l][h])->AddNetworkRouteTo (
                Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                leafHostAddr[l][h].first, 1);

    // Leaf switches
    for (uint32_t l = 0; l < nL; ++l) {
        auto sr = getStatic (m_leaf[l]);
        for (uint32_t h = 0; h < nH; ++h)
            sr->AddHostRouteTo (m_hostAddr2[l][h], m_hostAddr2[l][h], 1 + h);
        uint32_t baseIface = 1 + nH;
        for (uint32_t s = 0; s < nS; ++s)
            sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                   spineLeafAddr[s][l].first, baseIface + s, s + 1);
    }

    // Spine switches
    for (uint32_t s = 0; s < nS; ++s) {
        auto sr = getStatic (m_spine[s]);
        for (uint32_t l = 0; l < nL; ++l)
            sr->AddNetworkRouteTo (Ipv4Address ("0.0.0.0"), Ipv4Mask ("0.0.0.0"),
                                   spineLeafAddr[s][l].second, 1 + l, l + 1);
    }
}

} // namespace ns3
