/**
@file	 ReflectorClient.cpp
@brief   Represents one client connection
@author  Tobias Blomberg / SM0SVX
@date	 2017-02-11

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
Copyright (C) 2003-2017 Tobias Blomberg / SM0SVX

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\endverbatim
*/

/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sstream>
#include <cassert>
#include <iomanip>
#include <algorithm>
#include <cerrno>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncTimer.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "ReflectorClient.h"
#include "Reflector.h"



/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;



/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/




/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/

uint32_t ReflectorClient::next_client_id = 0;


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

ReflectorClient::ReflectorClient(Reflector *ref, Async::FramedTcpConnection *con,
                                 Async::Config *cfg)
  : m_con(con), m_msg_type(0), m_con_state(STATE_EXPECT_PROTO_VER),
    m_disc_timer(10000, Timer::TYPE_ONESHOT, false),
    m_client_id(next_client_id++), m_remote_udp_port(0), m_cfg(cfg),
    m_next_udp_tx_seq(0), m_next_udp_rx_seq(0),
    m_heartbeat_timer(1000, Timer::TYPE_PERIODIC),
    m_heartbeat_tx_cnt(HEARTBEAT_TX_CNT_RESET),
    m_heartbeat_rx_cnt(HEARTBEAT_RX_CNT_RESET),
    m_udp_heartbeat_tx_cnt(UDP_HEARTBEAT_TX_CNT_RESET),
    m_udp_heartbeat_rx_cnt(UDP_HEARTBEAT_RX_CNT_RESET),
    m_reflector(ref), m_blocktime(0), m_remaining_blocktime(0)
{
  m_con->frameReceived.connect(
      mem_fun(*this, &ReflectorClient::onFrameReceived));
  m_disc_timer.expired.connect(
      mem_fun(*this, &ReflectorClient::onDiscTimeout));
  m_heartbeat_timer.expired.connect(
      mem_fun(*this, &ReflectorClient::handleHeartbeat));
} /* ReflectorClient::ReflectorClient */


ReflectorClient::~ReflectorClient(void)
{
} /* ReflectorClient::~ReflectorClient */


int ReflectorClient::sendMsg(const ReflectorMsg& msg)
{
  if (((m_con_state != STATE_CONNECTED) && (msg.type() >= 100)) ||
      !m_con->isConnected())
  {
    errno = ENOTCONN;
    return -1;
  }

  m_heartbeat_tx_cnt = HEARTBEAT_TX_CNT_RESET;

  ReflectorMsg header(msg.type());
  ostringstream ss;
  if (!header.pack(ss) || !msg.pack(ss))
  {
    cerr << "*** ERROR: Failed to pack TCP message\n";
    errno = EBADMSG;
    return -1;
  }
  return m_con->write(ss.str().data(), ss.str().size());
} /* ReflectorClient::sendMsg */


void ReflectorClient::udpMsgReceived(const ReflectorUdpMsg &header)
{
  m_udp_heartbeat_rx_cnt = UDP_HEARTBEAT_RX_CNT_RESET;

  if ((m_blocktime > 0) && (header.type() == MsgUdpAudio::TYPE))
  {
    m_remaining_blocktime = m_blocktime;
  }
} /* ReflectorClient::udpMsgReceived */


void ReflectorClient::sendUdpMsg(const ReflectorUdpMsg &msg)
{
  if (remoteUdpPort() == 0)
  {
    return;
  }

  //cout << "### ReflectorClient::sendUdpMsg: " << client->remoteHost() << ":"
  //     << client->remoteUdpPort() << endl;

  m_udp_heartbeat_tx_cnt = UDP_HEARTBEAT_TX_CNT_RESET;

  ReflectorUdpMsg header(msg.type(), clientId(), nextUdpTxSeq());
  ostringstream ss;
  if (!header.pack(ss) || !msg.pack(ss))
  {
    // FIXME: Better error handling
    cerr << "*** ERROR: Failed to pack reflector UDP message\n";
    return;
  }
  m_reflector->sendUdpDatagram(this, ss.str().data(), ss.str().size());
} /* ReflectorClient::sendUdpMsg */


void ReflectorClient::setBlock(unsigned blocktime)
{
  m_blocktime = blocktime;
  m_remaining_blocktime = blocktime;
} /* ReflectorClient::setBlock */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

void ReflectorClient::onFrameReceived(FramedTcpConnection *con,
                                      std::vector<uint8_t>& data)
{
  int len = data.size();
  //cout << "### ReflectorClient::onFrameReceived: len=" << len << endl;

  assert(len >= 0);

  if ((m_con_state == STATE_DISCONNECTED) ||
      (m_con_state == STATE_EXPECT_DISCONNECT))
  {
    return;
  }

  char *buf = reinterpret_cast<char*>(&data.front());
  stringstream ss;
  ss.write(buf, len);

  ReflectorMsg header;
  if (!header.unpack(ss))
  {
    cout << "*** ERROR: Unpacking failed for TCP message header\n";
    disconnect("Protocol message header too short");
    return;
  }

  m_heartbeat_rx_cnt = HEARTBEAT_RX_CNT_RESET;

  switch (header.type())
  {
    case MsgHeartbeat::TYPE:
      //cout << "### " << callsign() << ": MsgHeartbeat()" << endl;
      break;
    case MsgProtoVer::TYPE:
      handleMsgProtoVer(ss);
      break;
    case MsgAuthResponse::TYPE:
      handleMsgAuthResponse(ss);
      break;
    default:
      cerr << "*** WARNING: Unknown protocol message received: msg_type="
           << header.type() << endl;
      break;
  }
} /* ReflectorClient::onFrameReceived */


void ReflectorClient::handleMsgProtoVer(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_PROTO_VER)
  {
    disconnect("Protocol version expected");
    return;
  }

  MsgProtoVer msg;
  if (!msg.unpack(is))
  {
    // FIXME: Disconnect
    cerr << "*** ERROR: Could not unpack MsgProtoVer\n";
    return;
  }
  //cout << "### " << m_con->remoteHost() << ":" << m_con->remotePort()
  //     << ": MsgProtoVer(" << msg.majorVer() << ", " << msg.minorVer()
  //     << ")" << endl;
  if ((msg.majorVer() != MsgProtoVer::MAJOR) ||
      (msg.minorVer() != MsgProtoVer::MINOR))
  {
    cerr << "*** ERROR: Incompatible protocol version: "
         << msg.majorVer() << "." << msg.minorVer() << ". Should be "
         << MsgProtoVer::MAJOR << "." << MsgProtoVer::MINOR << endl;
    stringstream ss;
    ss << "Unsupported protocol version " << msg.majorVer() << "."
       << msg.minorVer();
    disconnect(ss.str());
    return;
  }

  MsgAuthChallenge challenge_msg;
  memcpy(m_auth_challenge, challenge_msg.challenge(),
         MsgAuthChallenge::CHALLENGE_LEN);
  sendMsg(challenge_msg);
  m_con_state = STATE_EXPECT_AUTH_RESPONSE;
} /* ReflectorClient::handleMsgProtoVer */


void ReflectorClient::handleMsgAuthResponse(std::istream& is)
{
  if (m_con_state != STATE_EXPECT_AUTH_RESPONSE)
  {
    disconnect("Authentication response expected");
    return;
  }

  MsgAuthResponse msg;
  if (!msg.unpack(is))
  {
    // FIXME: Disconnect
    cerr << "*** ERROR: Could not unpack MsgAuthResponse\n";
    return;
  }

  stringstream ss;
  ss << hex << setw(2) << setfill('0');
  for (int i=0; i<MsgAuthResponse::DIGEST_LEN; ++i)
  {
    ss << (int)msg.digest()[i];
  }
  //cout << "### " << msg.callsign() << ": MsgAuthResponse(" << ss.str() << ")"
  //     << endl;

  string auth_key = lookupUserKey(msg.callsign());
  if (msg.verify(auth_key, m_auth_challenge))
  {
    vector<string> connected_nodes;
    m_reflector->nodeList(connected_nodes);
    if (find(connected_nodes.begin(), connected_nodes.end(),
             msg.callsign()) == connected_nodes.end())
    {
      m_callsign = msg.callsign();
      sendMsg(MsgAuthOk());
      cout << m_callsign << ": Login OK from "
           << m_con->remoteHost() << ":" << m_con->remotePort()
           << endl;
      m_con_state = STATE_CONNECTED;
      sendMsg(MsgServerInfo(m_client_id));
      sendNodeList();
      m_reflector->broadcastMsgExcept(MsgNodeJoined(m_callsign), this);
    }
    else
    {
      cout << msg.callsign() << ": Already connected" << endl;
      disconnect("Access denied");
    }
  }
  else
  {
    cout << msg.callsign() << ": Access denied" << endl;
    disconnect("Access denied");
  }
} /* ReflectorClient::handleMsgProtoVer */


void ReflectorClient::sendNodeList(void)
{
  MsgNodeList msg;
  m_reflector->nodeList(msg.nodes());
  sendMsg(msg);
} /* ReflectorClient::sendNodeList */


void ReflectorClient::disconnect(const std::string& msg)
{
  //cout << "### ReflectorClient::disconnect: " << msg << endl;
  sendMsg(MsgError(msg));
  m_heartbeat_timer.setEnable(false);
  m_remote_udp_port = 0;
  m_disc_timer.setEnable(true);
  m_con_state = STATE_EXPECT_DISCONNECT;
} /* ReflectorClient::disconnect */


#if 0
void ReflectorClient::onDisconnected(TcpConnection* con,
                                     TcpConnection::DisconnectReason)
{
  if (!m_callsign.empty())
  {
    cout << m_callsign << ": ";
  }
  cout << "Client " << con->remoteHost() << ":" << con->remotePort()
       << " disconnected" << endl;
  m_heartbeat_timer.setEnable(false);
  m_remote_udp_port = 0;
  m_disc_timer.setEnable(false);
  m_con_state = STATE_DISCONNECTED;
} /* ReflectorClient::onDisconnected */
#endif


void ReflectorClient::onDiscTimeout(Timer *t)
{
  //cout << "### ReflectorClient::onDiscTimeout" << endl;
  assert(m_con_state == STATE_EXPECT_DISCONNECT);
  m_con->disconnect();
  m_con_state = STATE_DISCONNECTED;
  m_con->disconnected(m_con, FramedTcpConnection::DR_ORDERED_DISCONNECT);
} /* ReflectorClient::onDiscTimeout */


void ReflectorClient::handleHeartbeat(Async::Timer *t)
{
  if (--m_heartbeat_tx_cnt == 0)
  {
    sendMsg(MsgHeartbeat());
  }

  if (--m_udp_heartbeat_tx_cnt == 0)
  {
    sendUdpMsg(MsgUdpHeartbeat());
  }

  if (--m_heartbeat_rx_cnt == 0)
  {
    cout << callsign() << ": Heartbeat timeout" << endl;
    disconnect("Heartbeat timeout");
  }

  if (--m_udp_heartbeat_rx_cnt == 0)
  {
    cout << callsign() << ": UDP heartbeat timeout" << endl;
    disconnect("UDP heartbeat timeout");
  }

  if (m_blocktime > 0)
  {
    if (m_remaining_blocktime == 0)
    {
      m_blocktime = 0;
    }
    else
    {
      m_remaining_blocktime -= 1;
    }

  }
} /* ReflectorClient::handleHeartbeat */


std::string ReflectorClient::lookupUserKey(const std::string& callsign)
{
  string auth_group;
  if (!m_cfg->getValue("USERS", callsign, auth_group) || auth_group.empty())
  {
    cout << "*** WARNING: Unknown user \"" << callsign << "\" in SvxReflector"
         << endl;
    return "";
  }
  string auth_key;
  if (!m_cfg->getValue("PASSWORDS", auth_group, auth_key) || auth_key.empty())
  {
    cout << "*** ERROR: User \"" << callsign << "\" found in SvxReflector "
         << "configuration but password with groupname \"" << auth_group
         << "\" not found." << endl;
    return "";
  }
  return auth_key;
} /* ReflectorClient::lookupUserKey */


/*
 * This file has not been truncated
 */
