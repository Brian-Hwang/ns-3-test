#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "udp-reliable-echo-client.h"
#include "seq-ts-header.h"

namespace ns3
{

    NS_LOG_COMPONENT_DEFINE("UdpReliableEchoClientApplication");

    NS_OBJECT_ENSURE_REGISTERED(UdpReliableEchoClient);

    TypeId
    UdpReliableEchoClient::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::UdpReliableEchoClient")
                                .SetParent<Application>()
                                .SetGroupName("Applications")
                                .AddConstructor<UdpReliableEchoClient>()
                                .AddAttribute("MaxPackets",
                                              "The maximum number of packets the application will send",
                                              UintegerValue(100),
                                              MakeUintegerAccessor(&UdpReliableEchoClient::m_count),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("Interval",
                                              "The time to wait between packets",
                                              TimeValue(Seconds(1.0)),
                                              MakeTimeAccessor(&UdpReliableEchoClient::m_interval),
                                              MakeTimeChecker())
                                .AddAttribute("RemoteAddress",
                                              "The destination Address of the outbound packets",
                                              AddressValue(),
                                              MakeAddressAccessor(&UdpReliableEchoClient::m_peerAddress),
                                              MakeAddressChecker())
                                .AddAttribute("RemotePort",
                                              "The destination port of the outbound packets",
                                              UintegerValue(0),
                                              MakeUintegerAccessor(&UdpReliableEchoClient::m_peerPort),
                                              MakeUintegerChecker<uint16_t>())
                                .AddAttribute("PacketSize", "Size of echo data in outbound packets",
                                              UintegerValue(100),
                                              MakeUintegerAccessor(&UdpReliableEchoClient::SetDataSize,
                                                                   &UdpReliableEchoClient::GetDataSize),
                                              MakeUintegerChecker<uint32_t>())
                                .AddTraceSource("Tx", "A new packet is created and is sent",
                                                MakeTraceSourceAccessor(&UdpReliableEchoClient::m_txTrace),
                                                "ns3::Packet::TracedCallback")
                                .AddTraceSource("Rx", "A packet has been received",
                                                MakeTraceSourceAccessor(&UdpReliableEchoClient::m_rxTrace),
                                                "ns3::Packet::TracedCallback")
                                .AddTraceSource("TxWithAddresses", "A new packet is created and is sent",
                                                MakeTraceSourceAccessor(&UdpReliableEchoClient::m_txTraceWithAddresses),
                                                "ns3::Packet::TwoAddressTracedCallback")
                                .AddTraceSource("RxWithAddresses", "A packet has been received",
                                                MakeTraceSourceAccessor(&UdpReliableEchoClient::m_rxTraceWithAddresses),
                                                "ns3::Packet::TwoAddressTracedCallback");
        return tid;
    }

    UdpReliableEchoClient::UdpReliableEchoClient()
    {
        NS_LOG_FUNCTION(this);
        m_sent = 0;
        m_socket = 0;
        m_sendEvent = EventId();
        m_data = 0;
        m_dataSize = 0;
        m_seqNumber = 0;
        m_recvdseqNumber = 0;
        m_resendingNumber = 0;
        m_isResending = false;
    }

    UdpReliableEchoClient::~UdpReliableEchoClient()
    {
        NS_LOG_FUNCTION(this);
        m_socket = 0;

        delete[] m_data;
        m_data = 0;
        m_dataSize = 0;
    }

    void
    UdpReliableEchoClient::SetRemote(Address ip, uint16_t port)
    {
        NS_LOG_FUNCTION(this << ip << port);
        m_peerAddress = ip;
        m_peerPort = port;
    }

    void
    UdpReliableEchoClient::SetRemote(Address addr)
    {
        NS_LOG_FUNCTION(this << addr);
        m_peerAddress = addr;
    }

    void
    UdpReliableEchoClient::DoDispose(void)
    {
        NS_LOG_FUNCTION(this);
        Application::DoDispose();
    }

    void
    UdpReliableEchoClient::StartApplication(void)
    {
        NS_LOG_FUNCTION(this);

        if (m_socket == 0)
        {
            TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
            m_socket = Socket::CreateSocket(GetNode(), tid);
            if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
            {
                if (m_socket->Bind() == -1)
                {
                    NS_FATAL_ERROR("Failed to bind socket");
                }
                m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
            }
            else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
            {
                if (m_socket->Bind6() == -1)
                {
                    NS_FATAL_ERROR("Failed to bind socket");
                }
                m_socket->Connect(Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
            }
            else if (InetSocketAddress::IsMatchingType(m_peerAddress) == true)
            {
                if (m_socket->Bind() == -1)
                {
                    NS_FATAL_ERROR("Failed to bind socket");
                }
                m_socket->Connect(m_peerAddress);
            }
            else if (Inet6SocketAddress::IsMatchingType(m_peerAddress) == true)
            {
                if (m_socket->Bind6() == -1)
                {
                    NS_FATAL_ERROR("Failed to bind socket");
                }
                m_socket->Connect(m_peerAddress);
            }
            else
            {
                NS_ASSERT_MSG(false, "Incompatible address type: " << m_peerAddress);
            }
        }

        m_socket->SetRecvCallback(MakeCallback(&UdpReliableEchoClient::HandleRead, this));
        m_socket->SetAllowBroadcast(true);
        ScheduleTransmit(Seconds(0.));
    }

    void
    UdpReliableEchoClient::StopApplication()
    {
        NS_LOG_FUNCTION(this);

        if (m_socket != 0)
        {
            m_socket->Close();
            m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
            m_socket = 0;
        }

        Simulator::Cancel(m_sendEvent);
    }

    void
    UdpReliableEchoClient::SetDataSize(uint32_t dataSize)
    {
        NS_LOG_FUNCTION(this << dataSize);

        //
        // If the client is setting the echo packet data size this way, we infer
        // that she doesn't care about the contents of the packet at all, so
        // neither will we.
        //
        delete[] m_data;
        m_data = 0;
        m_dataSize = 0;
        m_size = dataSize;
    }

    uint32_t
    UdpReliableEchoClient::GetDataSize(void) const
    {
        NS_LOG_FUNCTION(this);
        return m_size;
    }

    void
    UdpReliableEchoClient::SetFill(std::string fill)
    {
        NS_LOG_FUNCTION(this << fill);

        uint32_t dataSize = fill.size() + 1;

        if (dataSize != m_dataSize)
        {
            delete[] m_data;
            m_data = new uint8_t[dataSize];
            m_dataSize = dataSize;
        }

        memcpy(m_data, fill.c_str(), dataSize);

        //
        // Overwrite packet size attribute.
        //
        m_size = dataSize;
    }

    void
    UdpReliableEchoClient::SetFill(uint8_t fill, uint32_t dataSize)
    {
        NS_LOG_FUNCTION(this << fill << dataSize);
        if (dataSize != m_dataSize)
        {
            delete[] m_data;
            m_data = new uint8_t[dataSize];
            m_dataSize = dataSize;
        }

        memset(m_data, fill, dataSize);

        //
        // Overwrite packet size attribute.
        //
        m_size = dataSize;
    }

    void
    UdpReliableEchoClient::SetFill(uint8_t *fill, uint32_t fillSize, uint32_t dataSize)
    {
        NS_LOG_FUNCTION(this << fill << fillSize << dataSize);
        if (dataSize != m_dataSize)
        {
            delete[] m_data;
            m_data = new uint8_t[dataSize];
            m_dataSize = dataSize;
        }

        if (fillSize >= dataSize)
        {
            memcpy(m_data, fill, dataSize);
            m_size = dataSize;
            return;
        }

        //
        // Do all but the final fill.
        //
        uint32_t filled = 0;
        while (filled + fillSize < dataSize)
        {
            memcpy(&m_data[filled], fill, fillSize);
            filled += fillSize;
        }

        //
        // Last fill may be partial
        //
        memcpy(&m_data[filled], fill, dataSize - filled);

        //
        // Overwrite packet size attribute.
        //
        m_size = dataSize;
    }

    void
    UdpReliableEchoClient::ScheduleTransmit(Time dt)
    {
        NS_LOG_FUNCTION(this << dt);
        m_sendEvent = Simulator::Schedule(dt, &UdpReliableEchoClient::Send, this);
    }

    void
    UdpReliableEchoClient::Send(void)
    {
        NS_LOG_FUNCTION(this);

        NS_ASSERT(m_sendEvent.IsExpired());

        Ptr<Packet> p;
        if (m_dataSize)
        {
            //
            // If m_dataSize is non-zero, we have a data buffer of the same size that we
            // are expected to copy and send.  This state of affairs is created if one of
            // the Fill functions is called.  In this case, m_size must have been set
            // to agree with m_dataSize
            //
            NS_ASSERT_MSG(m_dataSize == m_size, "UdpReliableEchoClient::Send(): m_size and m_dataSize inconsistent");
            NS_ASSERT_MSG(m_data, "UdpReliableEchoClient::Send(): m_dataSize but no m_data");
            p = Create<Packet>(m_data, m_dataSize);
        }
        else
        {
            //
            // If m_dataSize is zero, the client has indicated that it doesn't care
            // about the data itself either by specifying the data size by setting
            // the corresponding attribute or by not calling a SetFill function.  In
            // this case, we don't worry about it either.  But we do allow m_size
            // to have a value different from the (zero) m_dataSize.
            //
            p = Create<Packet>(m_size);
        }
        Address localAddress;
        m_socket->GetSockName(localAddress);
        // call to the trace sinks before the packet is actually sent,
        // so that tags added to the packet can be sent as well
        m_txTrace(p);
        if (Ipv4Address::IsMatchingType(m_peerAddress))
        {
            m_txTraceWithAddresses(p, localAddress, InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
        else if (Ipv6Address::IsMatchingType(m_peerAddress))
        {
            m_txTraceWithAddresses(p, localAddress, Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
        // HEADER
        SeqTsHeader seqTs;
        seqTs.SetSeq(m_seqNumber++);
        p->AddHeader(seqTs);

        m_socket->Send(p);
        ++m_sent;

        if (Ipv4Address::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Ipv4Address::ConvertFrom(m_peerAddress) << " port " << m_peerPort);
        }
        else if (Ipv6Address::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Ipv6Address::ConvertFrom(m_peerAddress) << " port " << m_peerPort);
        }
        else if (InetSocketAddress::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << InetSocketAddress::ConvertFrom(m_peerAddress).GetIpv4() << " port " << InetSocketAddress::ConvertFrom(m_peerAddress).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetIpv6() << " port " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetPort());
        }

        if (m_sent < m_count)
        {
            ScheduleTransmit(m_interval);
        }
    }

    void
    UdpReliableEchoClient::ReSend(void)
    {
        NS_LOG_FUNCTION(this);

        NS_ASSERT(m_sendEvent.IsExpired());

        Ptr<Packet> p;
        if (m_dataSize)
        {
            NS_ASSERT_MSG(m_dataSize == m_size, "UdpReliableEchoClient::Send(): m_size and m_dataSize inconsistent");
            NS_ASSERT_MSG(m_data, "UdpReliableEchoClient::Send(): m_dataSize but no m_data");
            p = Create<Packet>(m_data, m_dataSize);
        }
        else
        {
            p = Create<Packet>(m_size);
        }
        Address localAddress;
        m_socket->GetSockName(localAddress);
        // call to the trace sinks before the packet is actually sent,
        // so that tags added to the packet can be sent as well
        m_txTrace(p);
        if (Ipv4Address::IsMatchingType(m_peerAddress))
        {
            m_txTraceWithAddresses(p, localAddress, InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
        else if (Ipv6Address::IsMatchingType(m_peerAddress))
        {
            m_txTraceWithAddresses(p, localAddress, Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
        // HEADER
        SeqTsHeader seqTs;
        if (!m_isResending)
            seqTs.SetSeq(m_seqNumber++);
        else
            seqTs.SetSeq(m_resendingNumber++);
        p->AddHeader(seqTs);
        if (!m_isResending)
        {
            m_socket->Send(p);
            ++m_sent;
        }
        else
        {
            m_socket->Send(p);
            uint32_t seq = m_resendingNumber - 1;
            NS_LOG_INFO("Packet Retrans:" << seq);
        }

        if (Ipv4Address::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Ipv4Address::ConvertFrom(m_peerAddress) << " port " << m_peerPort);
        }
        else if (Ipv6Address::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Ipv6Address::ConvertFrom(m_peerAddress) << " port " << m_peerPort);
        }
        else if (InetSocketAddress::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << InetSocketAddress::ConvertFrom(m_peerAddress).GetIpv4() << " port " << InetSocketAddress::ConvertFrom(m_peerAddress).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(m_peerAddress))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client sent " << m_size << " bytes to " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetIpv6() << " port " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetPort());
        }

        if (m_sent < m_count)
        {
            ScheduleTransmit(m_interval);
        }
    }

    void
    UdpReliableEchoClient::HandleRead(Ptr<Socket> socket)
    {
        NS_LOG_FUNCTION(this << socket);
        Ptr<Packet> packet;
        Address from;
        Address localAddress;
        while ((packet = socket->RecvFrom(from)))
        {
            if (InetSocketAddress::IsMatchingType(from))
            {
                NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client received " << packet->GetSize() << " bytes from " << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port " << InetSocketAddress::ConvertFrom(from).GetPort());
            }
            else if (Inet6SocketAddress::IsMatchingType(from))
            {
                NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s client received " << packet->GetSize() << " bytes from " << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port " << Inet6SocketAddress::ConvertFrom(from).GetPort());
            }
            socket->GetSockName(localAddress);
            // assn2
            SeqTsHeader seqTs;
            packet->RemoveHeader(seqTs);
            uint32_t seq = seqTs.GetSeq();
            //seq = 7, m_recvdseqNumber=3
            if (seq == m_recvdseqNumber)
            {
                m_recvdseqNumber++;
            }
            else if (seq == m_recvdseqNumber)
            else
            {
                NS_LOG_INFO("Packet Loss:" << seq);
                m_isResending = true;

                m_resendingNumber = m_recvdseqNumber;
                for (uint32_t i = m_recvdseqNumber; i < seq; i++)
                {
                    ScheduleTransmit(m_interval);
                }
                m_isResending = false;
            }
            // assn2
            m_rxTrace(packet);
            m_rxTraceWithAddresses(packet, from, localAddress);
        }
    }

} // Namespace ns3
