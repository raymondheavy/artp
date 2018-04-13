/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 02/28/2012
*****************************************************************************/

#ifndef WIN32
   #include <unistd.h>
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <cerrno>
   #include <cstring>
   #include <cstdlib>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #ifdef LEGACY_WIN32
      #include <wspiapi.h>
   #endif
#endif
#include <cmath>
#include <sstream>
#include "queue.h"
#include "core.h"

using namespace std;


CUDTUnited CUDT::s_UDTUnited;

const UDTSOCKET CUDT::INVALID_SOCK = -1;
const int CUDT::ERROR = -1;

const UDTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int UDT::ERROR = CUDT::ERROR;

const int32_t CSeqNo::m_iSeqNoTH = 0x3FFFFFFF;
const int32_t CSeqNo::m_iMaxSeqNo = 0x7FFFFFFF;
const int32_t CAckNo::m_iMaxAckSeqNo = 0x7FFFFFFF;
const int32_t CMsgNo::m_iMsgNoTH = 0xFFFFFFF;
const int32_t CMsgNo::m_iMaxMsgNo = 0x1FFFFFFF;

const int CUDT::m_iVersion = 4;
const int CUDT::m_iSYNInterval = 10000;
const int CUDT::m_iSelfClockInterval = 64;


CUDT::CUDT()
{
   m_pSndBuffer = NULL;
   m_pRcvBuffer = NULL;
   m_pSndLossList = NULL;
   m_pRcvLossList = NULL;
   m_pACKWindow = NULL;
   m_pSndTimeWindow = NULL;
   m_pRcvTimeWindow = NULL;

   m_pSndQueue = NULL;
   m_pRcvQueue = NULL;
   m_pPeerAddr = NULL;
   m_pSNode = NULL;
   m_pRNode = NULL;

   // Initilize mutex and condition variables
   initSynch();

   // Default UDT configurations
   m_iMSS = 1500;
   m_bSynSending = true;
   m_bSynRecving = true;
   m_iFlightFlagSize = 25600;
   m_iSndBufSize = 8192;
   m_iRcvBufSize = 8192; //Rcv buffer MUST NOT be bigger than Flight Flag size
   m_Linger.l_onoff = 1;
   m_Linger.l_linger = 180;
   m_iUDPSndBufSize = 65536;
   m_iUDPRcvBufSize = m_iRcvBufSize * m_iMSS;
   m_iSockType = UDT_STREAM;
   m_iIPversion = AF_INET;
   m_bRendezvous = false;
   m_iSndTimeOut = -1;
   m_iRcvTimeOut = -1;
   m_bReuseAddr = true;
   m_llMaxBW = -1;

   m_pCCFactory = new CCCFactory<CUDTCC>;
   m_pCC = NULL;
   m_pCache = NULL;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;
}

CUDT::CUDT(const CUDT& ancestor)
{
   m_pSndBuffer = NULL;
   m_pRcvBuffer = NULL;
   m_pSndLossList = NULL;
   m_pRcvLossList = NULL;
   m_pACKWindow = NULL;
   m_pSndTimeWindow = NULL;
   m_pRcvTimeWindow = NULL;

   m_pSndQueue = NULL;
   m_pRcvQueue = NULL;
   m_pPeerAddr = NULL;
   m_pSNode = NULL;
   m_pRNode = NULL;

   // Initilize mutex and condition variables
   initSynch();

   // Default UDT configurations
   m_iMSS = ancestor.m_iMSS;
   m_bSynSending = ancestor.m_bSynSending;
   m_bSynRecving = ancestor.m_bSynRecving;
   m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
   m_iSndBufSize = ancestor.m_iSndBufSize;
   m_iRcvBufSize = ancestor.m_iRcvBufSize;
   m_Linger = ancestor.m_Linger;
   m_iUDPSndBufSize = ancestor.m_iUDPSndBufSize;
   m_iUDPRcvBufSize = ancestor.m_iUDPRcvBufSize;
   m_iSockType = ancestor.m_iSockType;
   m_iIPversion = ancestor.m_iIPversion;
   m_bRendezvous = ancestor.m_bRendezvous;
   m_iSndTimeOut = ancestor.m_iSndTimeOut;
   m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
   m_bReuseAddr = true;	// this must be true, because all accepted sockets shared the same port with the listener
   m_llMaxBW = ancestor.m_llMaxBW;

   m_pCCFactory = ancestor.m_pCCFactory->clone();
   m_pCC = NULL;
   m_pCache = ancestor.m_pCache;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;
}

CUDT::~CUDT()
{
   // release mutex/condtion variables
   destroySynch();

   // destroy the data structures
   delete m_pSndBuffer;
   delete m_pRcvBuffer;
   delete m_pSndLossList;
   delete m_pRcvLossList;
   delete m_pACKWindow;
   delete m_pSndTimeWindow;
   delete m_pRcvTimeWindow;
   delete m_pCCFactory;
   delete m_pCC;
   delete m_pPeerAddr;
   delete m_pSNode;
   delete m_pRNode;
}

void CUDT::setOpt(UDTOpt optName, const void* optval, int)
{
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);

   CGuard cg(m_ConnectionLock);
   CGuard sendguard(m_SendLock);
   CGuard recvguard(m_RecvLock);

   switch (optName)
   {
   case UDT_MSS:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval < int(28 + CHandShake::m_iContentSize))
         throw CUDTException(5, 3, 0);

      m_iMSS = *(int*)optval;

      // Packet size cannot be greater than UDP buffer size
      if (m_iMSS > m_iUDPSndBufSize)
         m_iMSS = m_iUDPSndBufSize;
      if (m_iMSS > m_iUDPRcvBufSize)
         m_iMSS = m_iUDPRcvBufSize;

      break;

   case UDT_SNDSYN:
      m_bSynSending = *(bool *)optval;
      break;

   case UDT_RCVSYN:
      m_bSynRecving = *(bool *)optval;
      break;

   case UDT_CC:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 1, 0);
      if (NULL != m_pCCFactory)
         delete m_pCCFactory;
      m_pCCFactory = ((CCCVirtualFactory *)optval)->clone();

      break;

   case UDT_FC:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 2, 0);

      if (*(int*)optval < 1)
         throw CUDTException(5, 3);

      // Mimimum recv flight flag size is 32 packets
      if (*(int*)optval > 32)
         m_iFlightFlagSize = *(int*)optval;
      else
         m_iFlightFlagSize = 32;

      break;

   case UDT_SNDBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval <= 0)
         throw CUDTException(5, 3, 0);

      m_iSndBufSize = *(int*)optval / (m_iMSS - 28);

      break;

   case UDT_RCVBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval <= 0)
         throw CUDTException(5, 3, 0);

      // Mimimum recv buffer size is 32 packets
      if (*(int*)optval > (m_iMSS - 28) * 32)
         m_iRcvBufSize = *(int*)optval / (m_iMSS - 28);
      else
         m_iRcvBufSize = 32;

      // recv buffer MUST not be greater than FC size
      if (m_iRcvBufSize > m_iFlightFlagSize)
         m_iRcvBufSize = m_iFlightFlagSize;

      break;

   case UDT_LINGER:
      m_Linger = *(linger*)optval;
      break;

   case UDP_SNDBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      m_iUDPSndBufSize = *(int*)optval;

      if (m_iUDPSndBufSize < m_iMSS)
         m_iUDPSndBufSize = m_iMSS;

      break;

   case UDP_RCVBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      m_iUDPRcvBufSize = *(int*)optval;

      if (m_iUDPRcvBufSize < m_iMSS)
         m_iUDPRcvBufSize = m_iMSS;

      break;

   case UDT_RENDEZVOUS:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 1, 0);
      m_bRendezvous = *(bool *)optval;
      break;

   case UDT_SNDTIMEO: 
      m_iSndTimeOut = *(int*)optval; 
      break; 
    
   case UDT_RCVTIMEO: 
      m_iRcvTimeOut = *(int*)optval; 
      break; 

   case UDT_REUSEADDR:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);
      m_bReuseAddr = *(bool*)optval;
      break;

   case UDT_MAXBW:
      m_llMaxBW = *(int64_t*)optval;
      break;
    
   default:
      throw CUDTException(5, 0, 0);
   }
}

void CUDT::getOpt(UDTOpt optName, void* optval, int& optlen)
{
   CGuard cg(m_ConnectionLock);

   switch (optName)
   {
   case UDT_MSS:
      *(int*)optval = m_iMSS;
      optlen = sizeof(int);
      break;

   case UDT_SNDSYN:
      *(bool*)optval = m_bSynSending;
      optlen = sizeof(bool);
      break;

   case UDT_RCVSYN:
      *(bool*)optval = m_bSynRecving;
      optlen = sizeof(bool);
      break;

   case UDT_CC:
      if (!m_bOpened)
         throw CUDTException(5, 5, 0);
      *(CCC**)optval = m_pCC;
      optlen = sizeof(CCC*);

      break;

   case UDT_FC:
      *(int*)optval = m_iFlightFlagSize;
      optlen = sizeof(int);
      break;

   case UDT_SNDBUF:
      *(int*)optval = m_iSndBufSize * (m_iMSS - 28);
      optlen = sizeof(int);
      break;

   case UDT_RCVBUF:
      *(int*)optval = m_iRcvBufSize * (m_iMSS - 28);
      optlen = sizeof(int);
      break;

   case UDT_LINGER:
      if (optlen < (int)(sizeof(linger)))
         throw CUDTException(5, 3, 0);

      *(linger*)optval = m_Linger;
      optlen = sizeof(linger);
      break;

   case UDP_SNDBUF:
      *(int*)optval = m_iUDPSndBufSize;
      optlen = sizeof(int);
      break;

   case UDP_RCVBUF:
      *(int*)optval = m_iUDPRcvBufSize;
      optlen = sizeof(int);
      break;

   case UDT_RENDEZVOUS:
      *(bool *)optval = m_bRendezvous;
      optlen = sizeof(bool);
      break;

   case UDT_SNDTIMEO: 
      *(int*)optval = m_iSndTimeOut; 
      optlen = sizeof(int); 
      break; 
    
   case UDT_RCVTIMEO: 
      *(int*)optval = m_iRcvTimeOut; 
      optlen = sizeof(int); 
      break; 

   case UDT_REUSEADDR:
      *(bool *)optval = m_bReuseAddr;
      optlen = sizeof(bool);
      break;

   case UDT_MAXBW:
      *(int64_t*)optval = m_llMaxBW;
      optlen = sizeof(int64_t);
      break;

   case UDT_STATE:
      *(int32_t*)optval = s_UDTUnited.getStatus(m_SocketID);
      optlen = sizeof(int32_t);
      break;

   case UDT_EVENT:
   {
      int32_t event = 0;
      if (m_bBroken)
         event |= UDT_EPOLL_ERR;
      else
      {
         if (m_pRcvBuffer && (m_pRcvBuffer->getRcvDataSize() > 0))
            event |= UDT_EPOLL_IN;
         if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
            event |= UDT_EPOLL_OUT;
      }
      *(int32_t*)optval = event;
      optlen = sizeof(int32_t);
      break;
   }

   case UDT_SNDDATA:
      if (m_pSndBuffer)
         *(int32_t*)optval = m_pSndBuffer->getCurrBufSize();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   case UDT_RCVDATA:
      if (m_pRcvBuffer)
         *(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   default:
      throw CUDTException(5, 0, 0);
   }
}

void CUDT::open()
{
   CGuard cg(m_ConnectionLock);

   // Initial sequence number, loss, acknowledgement, etc.
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

   m_iEXPCount = 1;
   m_iBandwidth = 1;
   m_iDeliveryRate = 16;
   m_iAckSeqNo = 0;
   m_ullLastAckTime = 0;

   // trace information
   m_StartTime = CTimer::getTime();
   m_llSentTotal = m_llRecvTotal = m_iSndLossTotal = m_iRcvLossTotal = m_iRetransTotal = m_iSentACKTotal = m_iRecvACKTotal = m_iSentNAKTotal = m_iRecvNAKTotal = 0;
   m_LastSampleTime = CTimer::getTime();
   m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
   m_llSndDuration = m_llSndDurationTotal = 0;

   // structures for queue
   if (NULL == m_pSNode)
      m_pSNode = new CSNode;
   m_pSNode->m_pUDT = this;
   m_pSNode->m_llTimeStamp = 1;
   m_pSNode->m_iHeapLoc = -1;

   if (NULL == m_pRNode)
      m_pRNode = new CRNode;
   m_pRNode->m_pUDT = this;
   m_pRNode->m_llTimeStamp = 1;
   m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
   m_pRNode->m_bOnList = false;

   m_iRTT = 10 * m_iSYNInterval;
   m_iRTTVar = m_iRTT >> 1;
   m_ullCPUFrequency = CTimer::getCPUFrequency();

   // set up the timers
   m_ullSYNInt = m_iSYNInterval * m_ullCPUFrequency;
  
   // set minimum NAK and EXP timeout to 100ms
   m_ullMinNakInt = 300000 * m_ullCPUFrequency;
   m_ullMinExpInt = 300000 * m_ullCPUFrequency;

   m_ullACKInt = m_ullSYNInt;
   m_ullNAKInt = m_ullMinNakInt;

   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;
   m_ullNextACKTime = currtime + m_ullSYNInt;
   m_ullNextNAKTime = currtime + m_ullNAKInt;

   m_iPktCount = 0;
   m_iLightACKCount = 1;

   m_ullTargetTime = 0;
   m_ullTimeDiff = 0;

   // Now UDT is opened.
   m_bOpened = true;
}

void CUDT::listen()
{
   CGuard cg(m_ConnectionLock);

   if (!m_bOpened)
      throw CUDTException(5, 0, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(5, 2, 0);

   // listen can be called more than once
   if (m_bListening)
      return;

   // if there is already another socket listening on the same port
   if (m_pRcvQueue->setListener(this) < 0)
      throw CUDTException(5, 11, 0);

   m_bListening = true;
}

void CUDT::connect(const sockaddr* serv_addr)
{
   CGuard cg(m_ConnectionLock);

   if (!m_bOpened)
      throw CUDTException(5, 0, 0);

   if (m_bListening)
      throw CUDTException(5, 2, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(5, 2, 0);

   // record peer/server address
   delete m_pPeerAddr;
   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, serv_addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // register this socket in the rendezvous queue
   // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this function
   uint64_t ttl = 3000000;
   if (m_bRendezvous)
      ttl *= 10;
   ttl += CTimer::getTime();
   m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

   // This is my current configurations
   m_ConnReq.m_iVersion = m_iVersion;
   m_ConnReq.m_iType = m_iSockType;
   m_ConnReq.m_iMSS = m_iMSS;
   // 客户端的m_iFlightFlagSize
   m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;
   m_ConnReq.m_iReqType = (!m_bRendezvous) ? 1 : 0; // 1: regular 0: rendezvous
   m_ConnReq.m_iID = m_SocketID;
   CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion); // PeerIP：server的IP

   // Random Initial Sequence Number
   srand((unsigned int)CTimer::getTime());
   m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));

   m_iLastDecSeq = m_iISN - 1;
   m_iSndLastAck = m_iISN;
   m_iSndLastDataAck = m_iISN;
   m_iSndCurrSeqNo = m_iISN - 1;
   m_iSndLastAck2 = m_iISN;
   m_ullSndLastAck2Time = CTimer::getTime();

   // Inform the server my configurations.
   CPacket request;
   char* reqdata = new char [m_iPayloadSize];
   request.pack(0, NULL, reqdata, m_iPayloadSize);
   // ID = 0, connection request
   request.m_iID = 0;

   int hs_size = m_iPayloadSize;

   // serialize更新了hs_size为实际大小
   m_ConnReq.serialize(reqdata, hs_size);
   request.setLength(hs_size);
   m_pSndQueue->sendto(serv_addr, request);
   m_llLastReqTime = CTimer::getTime();

   m_bConnecting = true;

   // asynchronous connect, return immediately
   if (!m_bSynRecving)
   {
      delete [] reqdata;
      return;
   }

   // Wait for the negotiated configurations from the peer side.
   CPacket response;
   char* resdata = new char [m_iPayloadSize];
   response.pack(0, NULL, resdata, m_iPayloadSize);

   CUDTException e(0, 0);

   while (!m_bClosing)
   {
      // avoid sending too many requests, at most 1 request per 250ms
      if (CTimer::getTime() - m_llLastReqTime > 250000)
      {
         m_ConnReq.serialize(reqdata, hs_size);
         request.setLength(hs_size);
         if (m_bRendezvous)
            request.m_iID = m_ConnRes.m_iID; // Destination Socket ID 对端的socket id（m_ConnRes为对端发来的请求，携带其本地信息）
         m_pSndQueue->sendto(serv_addr, request);
         m_llLastReqTime = CTimer::getTime();
      }

      response.setLength(m_iPayloadSize);
      if (m_pRcvQueue->recvfrom(m_SocketID, response) > 0)
      {
         // = 0：连接建立成功
         if (connect(response) <= 0)
            break;

         // new request/response should be sent out immediately on receving a response
         m_llLastReqTime = 0;
      }

      if (CTimer::getTime() > ttl)
      {
         // timeout
         e = CUDTException(1, 1, 0);
         break;
      }
   }

   delete [] reqdata;
   delete [] resdata;

   if (e.getErrorCode() == 0)
   {
      if (m_bClosing)                                                 // if the socket is closed before connection...
         e = CUDTException(1);
      else if (1002 == m_ConnRes.m_iReqType)                          // connection request rejected
         e = CUDTException(1, 2, 0);
      else if ((!m_bRendezvous) && (m_iISN != m_ConnRes.m_iISN))      // secuity check
         e = CUDTException(1, 4, 0);
   }

   if (e.getErrorCode() != 0)
      throw e;
}

// connect(const sockaddr* serv_addr)中调用该函数
// 收到对端回复的握手报文后
int CUDT::connect(const CPacket& response) throw ()
{
   // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
   // returning -1 means there is an error.
   // returning 1 or 2 means the connection is in process and needs more handshake

   if (!m_bConnecting)
      return -1;

   // m_bRendezvous模式下，对端发来的报文为数据包或者keep-alive报文
   if (m_bRendezvous && ((0 == response.getFlag()) || (1 == response.getType())) && (0 != m_ConnRes.m_iType))
   {
      //a data packet or a keep-alive packet comes, which means the peer side is already connected
      // in this situation, the previously recorded response will be used
      goto POST_CONNECT;
   }

   // 非控制包，或者非握手包
   if ((1 != response.getFlag()) || (0 != response.getType()))
      return -1;

   m_ConnRes.deserialize(response.m_pcData, response.getLength());

   if (m_bRendezvous)
   {
      // regular connect should NOT communicate with rendezvous connect
      // rendezvous connect require 3-way handshake
      if (1 == m_ConnRes.m_iReqType)
         return -1;

      // m_ConnReq.m_iReqType：收到对端报文的情况下会被更新为-1
      // m_ConnRes.m_iReqType：收到对端第二次发来的报文（确保对端收到本端发送的报文的情况下）会被更新为-1
      // m_ConnReq.m_iReqType m_ConnRes.m_iReqType都更新为-1的情况下，正好为3次握手
      if ((0 == m_ConnReq.m_iReqType) || (0 == m_ConnRes.m_iReqType))
      {
         m_ConnReq.m_iReqType = -1;
         // the request time must be updated so that the next handshake can be sent out immediately.
         m_llLastReqTime = 0;
         return 1;
      }
   }
   else
   {
      // set cookie
      // 准备发送第三次握手报文
      if (1 == m_ConnRes.m_iReqType)
      {
         m_ConnReq.m_iReqType = -1;
         m_ConnReq.m_iCookie = m_ConnRes.m_iCookie;
         m_llLastReqTime = 0;
         return 1;
      }
   }

POST_CONNECT:
   // Remove from rendezvous queue
   m_pRcvQueue->removeConnector(m_SocketID);

   // Re-configure according to the negotiated values.
   m_iMSS = m_ConnRes.m_iMSS;

   // 用对端的m_iFlightFlagSize更新本地的m_iFlowWindowSize
   m_iFlowWindowSize = m_ConnRes.m_iFlightFlagSize;
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
   m_iPeerISN = m_ConnRes.m_iISN;
   m_iRcvLastAck = m_ConnRes.m_iISN;
   m_iRcvLastAckAck = m_ConnRes.m_iISN;
   m_iRcvCurrSeqNo = m_ConnRes.m_iISN - 1;
   m_PeerID = m_ConnRes.m_iID;

   // c/s模式下，收到server发来的第4次握手报文，m_ConnRes.m_piPeerIP为server端看到的对端地址，也就是c端最后一级路由地址
   // m_piSelfIP s端看到的c地址
   memcpy(m_piSelfIP, m_ConnRes.m_piPeerIP, 16);

   // Prepare all data structures
   try
   {
      // UDT实例拥有各自的发送缓存、接收缓存以及丢失链表等
      // 复用器实例拥有发送队列（UDT实例链表）、接收队列（UDT实例链表）、通道
      m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
      m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
      // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
      m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
      m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
      m_pACKWindow = new CACKWindow(1024);
      m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
      m_pSndTimeWindow = new CPktTimeWindow();
   }
   catch (...)
   {
      throw CUDTException(3, 2, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   m_pCC = m_pCCFactory->create();
   m_pCC->m_UDT = m_SocketID;
   m_pCC->setMSS(m_iMSS);
   m_pCC->setMaxCWndSize(m_iFlowWindowSize);
   m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);
   m_pCC->setRcvRate(m_iDeliveryRate);
   m_pCC->setRTT(m_iRTT);
   m_pCC->setBandwidth(m_iBandwidth);
   m_pCC->init();

   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   // And, I am connected too.
   m_bConnecting = false;
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   // acknowledge the management module.
   s_UDTUnited.connect_complete(m_SocketID);

   // acknowledde any waiting epolls to write
   s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

   return 0;
}

// 被newConnection()调用
void CUDT::connect(const sockaddr* peer, CHandShake* hs)
{
   CGuard cg(m_ConnectionLock);

   // Uses the smaller MSS between the peers        
   if (hs->m_iMSS > m_iMSS)
      hs->m_iMSS = m_iMSS;
   else
      m_iMSS = hs->m_iMSS;

   // exchange info for maximum flow window size
   // 用对端的m_iFlightFlagSize更新本地的m_iFlowWindowSize
   m_iFlowWindowSize = hs->m_iFlightFlagSize;
   // 将server端的m_iFlightFlagSize发送给c端
   hs->m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;

   m_iPeerISN = hs->m_iISN;

   m_iRcvLastAck = hs->m_iISN;
   m_iRcvLastAckAck = hs->m_iISN;
   m_iRcvCurrSeqNo = hs->m_iISN - 1;

   m_PeerID = hs->m_iID;
   hs->m_iID = m_SocketID;

   // use peer's ISN and send it back for security check
   m_iISN = hs->m_iISN;

   m_iLastDecSeq = m_iISN - 1;
   m_iSndLastAck = m_iISN;
   m_iSndLastDataAck = m_iISN;
   m_iSndCurrSeqNo = m_iISN - 1;
   m_iSndLastAck2 = m_iISN;
   m_ullSndLastAck2Time = CTimer::getTime();

   // this is a reponse handshake
   hs->m_iReqType = -1;

   // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
   // m_piSelfIP c端发起连接时的目的地址
   memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
   // 更新握手报文中的PeerIP为 收到的握手报文中的源地址（也就是server端能看到的对端地址），并将该地址发给连接发起端
   // server端将向对端发送第4次握手报文
   CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);
  
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

   // Prepare all structures
   try
   {
      m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
      m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
      m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
      m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
      m_pACKWindow = new CACKWindow(1024);
      m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
      m_pSndTimeWindow = new CPktTimeWindow();
   }
   catch (...)
   {
      throw CUDTException(3, 2, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   m_pCC = m_pCCFactory->create();
   m_pCC->m_UDT = m_SocketID;
   m_pCC->setMSS(m_iMSS);
   m_pCC->setMaxCWndSize(m_iFlowWindowSize);
   m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);
   m_pCC->setRcvRate(m_iDeliveryRate);
   m_pCC->setRTT(m_iRTT);
   m_pCC->setBandwidth(m_iBandwidth);
   m_pCC->init();

   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // And of course, it is connected.
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   //send the response to the peer, see listen() for more discussions about this
   CPacket response;
   int size = CHandShake::m_iContentSize;
   char* buffer = new char[size];
   hs->serialize(buffer, size);
   response.pack(0, NULL, buffer, size);
   response.m_iID = m_PeerID; // 目的socket ID
   // 向连接发起端发送第4次（建立连接过程中最后一次）握手报文
   m_pSndQueue->sendto(peer, response);
   delete [] buffer;
}

void CUDT::close()
{
   if (!m_bOpened)
      return;

//    Linux下tcp连接断开的时候调用close()函数，有优雅断开和强制断开两种方式。
//    linger结构体数据结构如下：
//    struct linger {
//        　　int l_onoff;
//          　　int l_linger;
//    };
// 
//    三种断开方式：
//        1. l_onoff = 0; l_linger忽略
//        close()立刻返回，底层会将未发送完的数据发送完成后再释放资源，即优雅退出。
// 
//        2. l_onoff != 0; l_linger = 0;
//        close()立刻返回，但不会发送未发送完成的数据，而是通过一个REST包强制的关闭socket描述符，即强制退出。
// 
//        3. l_onoff != 0; l_linger > 0;
//        close()不会立刻返回，内核会延迟一段时间，这个时间就由l_linger的值来决定。
//        如果超时时间到达之前，发送完未发送的数据(包括FIN包)并得到另一端的确认，close()会返回正确，socket描述符优雅性退出。
//        否则，close()会直接返回错误值，未发送数据丢失，socket描述符被强制性退出。
//        需要注意的时，如果socket描述符被设置为非堵塞型，则close()会直接返回值。

   if (0 != m_Linger.l_onoff)
   {
      uint64_t entertime = CTimer::getTime();

      // 在未被强制关闭（m_bBroken=false）的情况下，关闭套接字需等待发送
      while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) && (CTimer::getTime() - entertime < m_Linger.l_linger * 1000000ULL))
      {
         // linger has been checked by previous close() call and has expired
         if (m_ullLingerExpiration >= entertime)
            break;

         if (!m_bSynSending)
         {
            // if this socket enables asynchronous sending, return immediately and let GC to close it later
            // 异步发送的情况下，设置关闭超时时间
            if (0 == m_ullLingerExpiration)
               m_ullLingerExpiration = entertime + m_Linger.l_linger * 1000000ULL;

            return;
         }

         #ifndef WIN32
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
         #else
            Sleep(1);
         #endif
      }
   }

   // remove this socket from the snd queue
   if (m_bConnected)
      m_pSndQueue->m_pSndUList->remove(this);

   // trigger any pending IO events.
   s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);
   // then remove itself from all epoll monitoring
   try
   {
      for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++ i)
         s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
   }
   catch (...)
   {
   }

   if (!m_bOpened)
      return;

   // Inform the threads handler to stop.
   m_bClosing = true;

   CGuard cg(m_ConnectionLock);

   // Signal the sender and recver if they are waiting for data.
   releaseSynch();

   if (m_bListening)
   {
      m_bListening = false;
      m_pRcvQueue->removeListener(this);
   }
   else if (m_bConnecting)
   {
      m_pRcvQueue->removeConnector(m_SocketID);
   }

   if (m_bConnected)
   {
      if (!m_bShutdown)
         sendCtrl(5);

      m_pCC->close();

      // Store current connection information.
      CInfoBlock ib;
      ib.m_iIPversion = m_iIPversion;
      CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
      ib.m_iRTT = m_iRTT;
      ib.m_iBandwidth = m_iBandwidth;
      m_pCache->update(&ib);

      m_bConnected = false;
   }

   // waiting all send and recv calls to stop
   CGuard sendguard(m_SendLock);
   CGuard recvguard(m_RecvLock);

   // CLOSED.
   m_bOpened = false;
}

int CUDT::send(const char* data, int len)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   // 待发送数据的大小已超过设定的发送缓冲区大小，需等待
   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // 异步模式下抛出异常
      if (!m_bSynSending)
         throw CUDTException(6, 1, 0);
      else
      {
         // wait here during a blocking sending
         #ifndef WIN32
            pthread_mutex_lock(&m_SendBlockLock);
            if (m_iSndTimeOut < 0) 
            { 
               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
               timespec locktime; 
    
               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
            }
            pthread_mutex_unlock(&m_SendBlockLock);
         #else
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
                  WaitForSingleObject(m_SendBlockCond, INFINITE);
            }
            else 
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
                  WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000)); 
            }
         #endif

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(2, 1, 0);
         else if (!m_bConnected)
            throw CUDTException(2, 2, 0);
         else if (!m_bPeerHealth)
         {
            m_bPeerHealth = true;
            throw CUDTException(7);
         }
      }
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(6, 3, 0); 

      return 0;
   }

   int size = (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize;
   if (size > len)
      size = len;

   // record total time used for sending
   if (0 == m_pSndBuffer->getCurrBufSize())
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sening list
   m_pSndBuffer->addBuffer(data, size);

   // insert this socket to snd list if it is not on the list yet
   m_pSndQueue->m_pSndUList->update(this, false);

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size;
}

int CUDT::recv(char* data, int len)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   if (len <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   if (0 == m_pRcvBuffer->getRcvDataSize())
   {
      if (!m_bSynRecving)
         throw CUDTException(6, 2, 0);
      else
      {
         #ifndef WIN32
            pthread_mutex_lock(&m_RecvDataLock);
            if (m_iRcvTimeOut < 0) 
            { 
               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
                  pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL; 
               timespec locktime; 
    
               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
               {
                  pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime); 
                  if (CTimer::getTime() >= exptime)
                     break;
               }
            }
            pthread_mutex_unlock(&m_RecvDataLock);
         #else
            if (m_iRcvTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
                  WaitForSingleObject(m_RecvDataCond, INFINITE);
            }
            else
            {
               uint64_t enter_time = CTimer::getTime();

               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
               {
                  int diff = int(CTimer::getTime() - enter_time) / 1000;
                  if (diff >= m_iRcvTimeOut)
                      break;
                  WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut - diff ));
               }
            }
         #endif
      }
   }

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   int res = m_pRcvBuffer->readBuffer(data, len);

   if (m_pRcvBuffer->getRcvDataSize() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(6, 3, 0);

   return res;
}

int CUDT::sendmsg(const char* data, int len, int msttl, bool inorder)
{
   if (UDT_STREAM == m_iSockType)
      throw CUDTException(5, 9, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   if (len > m_iSndBufSize * m_iPayloadSize)
      throw CUDTException(5, 12, 0);

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      if (!m_bSynSending)
         throw CUDTException(6, 1, 0);
      else
      {
         // wait here during a blocking sending
         #ifndef WIN32
            pthread_mutex_lock(&m_SendBlockLock);
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
               timespec locktime;

               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
            }
            pthread_mutex_unlock(&m_SendBlockLock);
         #else
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
                  WaitForSingleObject(m_SendBlockCond, INFINITE);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
                  WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000));
            }
         #endif

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(2, 1, 0);
         else if (!m_bConnected)
            throw CUDTException(2, 2, 0);
      }
   }

   // 剩余缓冲区大小
   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(6, 3, 0);

      return 0;
   }

   // record total time used for sending
   if (0 == m_pSndBuffer->getCurrBufSize())
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sening list
   m_pSndBuffer->addBuffer(data, len, msttl, inorder);

   // insert this socket to the snd list if it is not on the list yet
   m_pSndQueue->m_pSndUList->update(this, false);

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return len;   
}

int CUDT::recvmsg(char* data, int len)
{
   if (UDT_STREAM == m_iSockType)
      throw CUDTException(5, 9, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   if (m_bBroken || m_bClosing)
   {
      int res = m_pRcvBuffer->readMsg(data, len);

      if (m_pRcvBuffer->getRcvMsgNum() <= 0)
      {
         // read is not available any more
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
      }

      if (0 == res)
         throw CUDTException(2, 1, 0);
      else
         return res;
   }

   if (!m_bSynRecving)
   {
      int res = m_pRcvBuffer->readMsg(data, len);
      if (0 == res)
         throw CUDTException(6, 2, 0);
      else
         return res;
   }

   int res = 0;
   bool timeout = false;

   do
   {
      #ifndef WIN32
         pthread_mutex_lock(&m_RecvDataLock);

         if (m_iRcvTimeOut < 0)
         {
            while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
               pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
         }
         else
         {
            uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL;
            timespec locktime;

            locktime.tv_sec = exptime / 1000000;
            locktime.tv_nsec = (exptime % 1000000) * 1000;

            if (pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime) == ETIMEDOUT)
               timeout = true;

            res = m_pRcvBuffer->readMsg(data, len);           
         }
         pthread_mutex_unlock(&m_RecvDataLock);
      #else
         if (m_iRcvTimeOut < 0)
         {
            while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
               WaitForSingleObject(m_RecvDataCond, INFINITE);
         }
         else
         {
            if (WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut)) == WAIT_TIMEOUT)
               timeout = true;

            res = m_pRcvBuffer->readMsg(data, len);
         }
      #endif

      if (m_bBroken || m_bClosing)
         throw CUDTException(2, 1, 0);
      else if (!m_bConnected)
         throw CUDTException(2, 2, 0);
   } while ((0 == res) && !timeout);

   if (m_pRcvBuffer->getRcvMsgNum() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(6, 3, 0);

   return res;
}

int64_t CUDT::sendfile(fstream& ifs, int64_t& offset, int64_t size, int block)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (size <= 0)
      return 0;

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   int64_t tosend = size;
   int unitsize;

   // positioning...
   try
   {
      ifs.seekg((streamoff)offset);
   }
   catch (...)
   {
      throw CUDTException(4, 1);
   }

   // sending block by block
   while (tosend > 0)
   {
      if (ifs.fail())
         throw CUDTException(4, 4);

      if (ifs.eof())
         break;

      unitsize = int((tosend >= block) ? block : tosend);

      #ifndef WIN32
         pthread_mutex_lock(&m_SendBlockLock);
         while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
            pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
         pthread_mutex_unlock(&m_SendBlockLock);
      #else
         while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
            WaitForSingleObject(m_SendBlockCond, INFINITE);
      #endif

      if (m_bBroken || m_bClosing)
         throw CUDTException(2, 1, 0);
      else if (!m_bConnected)
         throw CUDTException(2, 2, 0);
      else if (!m_bPeerHealth)
      {
         // reset peer health status, once this error returns, the app should handle the situation at the peer side
         m_bPeerHealth = true;
         throw CUDTException(7);
      }

      // record total time used for sending
      if (0 == m_pSndBuffer->getCurrBufSize())
         m_llSndDurationCounter = CTimer::getTime();

      int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

      if (sentsize > 0)
      {
         tosend -= sentsize;
         offset += sentsize;
      }

      // insert this socket to snd list if it is not on the list yet
      m_pSndQueue->m_pSndUList->update(this, false);
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size - tosend;
}

int64_t CUDT::recvfile(fstream& ofs, int64_t& offset, int64_t size, int block)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   if (size <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   int64_t torecv = size;
   int unitsize = block;
   int recvsize;

   // positioning...
   try
   {
      ofs.seekp((streamoff)offset);
   }
   catch (...)
   {
      throw CUDTException(4, 3);
   }

   // receiving... "recvfile" is always blocking
   while (torecv > 0)
   {
      if (ofs.fail())
      {
         // send the sender a signal so it will not be blocked forever
         int32_t err_code = CUDTException::EFILE;
         sendCtrl(8, &err_code);

         throw CUDTException(4, 4);
      }

      #ifndef WIN32
         pthread_mutex_lock(&m_RecvDataLock);
         while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
            pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
         pthread_mutex_unlock(&m_RecvDataLock);
      #else
         while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
            WaitForSingleObject(m_RecvDataCond, INFINITE);
      #endif

      if (!m_bConnected)
         throw CUDTException(2, 2, 0);
      else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
         throw CUDTException(2, 1, 0);

      unitsize = int((torecv >= block) ? block : torecv);
      recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

      if (recvsize > 0)
      {
         torecv -= recvsize;
         offset += recvsize;
      }
   }

   if (m_pRcvBuffer->getRcvDataSize() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   return size - torecv;
}

void CUDT::sample(CPerfMon* perf, bool clear)
{
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);

   uint64_t currtime = CTimer::getTime();
   perf->msTimeStamp = (currtime - m_StartTime) / 1000;

   perf->pktSent = m_llTraceSent;
   perf->pktRecv = m_llTraceRecv;
   perf->pktSndLoss = m_iTraceSndLoss;
   perf->pktRcvLoss = m_iTraceRcvLoss;
   perf->pktRetrans = m_iTraceRetrans;
   perf->pktSentACK = m_iSentACK;
   perf->pktRecvACK = m_iRecvACK;
   perf->pktSentNAK = m_iSentNAK;
   perf->pktRecvNAK = m_iRecvNAK;
   perf->usSndDuration = m_llSndDuration;

   perf->pktSentTotal = m_llSentTotal;
   perf->pktRecvTotal = m_llRecvTotal;
   perf->pktSndLossTotal = m_iSndLossTotal;
   perf->pktRcvLossTotal = m_iRcvLossTotal;
   perf->pktRetransTotal = m_iRetransTotal;
   perf->pktSentACKTotal = m_iSentACKTotal;
   perf->pktRecvACKTotal = m_iRecvACKTotal;
   perf->pktSentNAKTotal = m_iSentNAKTotal;
   perf->pktRecvNAKTotal = m_iRecvNAKTotal;
   perf->usSndDurationTotal = m_llSndDurationTotal;

   double interval = double(currtime - m_LastSampleTime);

   perf->mbpsSendRate = double(m_llTraceSent) * m_iPayloadSize * 8.0 / interval;
   perf->mbpsRecvRate = double(m_llTraceRecv) * m_iPayloadSize * 8.0 / interval;

   perf->usPktSndPeriod = m_ullInterval / double(m_ullCPUFrequency);
   perf->pktFlowWindow = m_iFlowWindowSize;
   perf->pktCongestionWindow = (int)m_dCongestionWindow;
   perf->pktFlightSize = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
   perf->msRTT = m_iRTT/1000.0;
   perf->mbpsBandwidth = m_iBandwidth * m_iPayloadSize * 8.0 / 1000000.0;

   #ifndef WIN32
      if (0 == pthread_mutex_trylock(&m_ConnectionLock))
   #else
      if (WAIT_OBJECT_0 == WaitForSingleObject(m_ConnectionLock, 0))
   #endif
   {
      perf->byteAvailSndBuf = (NULL == m_pSndBuffer) ? 0 : (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iMSS;
      perf->byteAvailRcvBuf = (NULL == m_pRcvBuffer) ? 0 : m_pRcvBuffer->getAvailBufSize() * m_iMSS;

      #ifndef WIN32
         pthread_mutex_unlock(&m_ConnectionLock);
      #else
         ReleaseMutex(m_ConnectionLock);
      #endif
   }
   else
   {
      perf->byteAvailSndBuf = 0;
      perf->byteAvailRcvBuf = 0;
   }

   if (clear)
   {
      m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
      m_llSndDuration = 0;
      m_LastSampleTime = currtime;
   }
}

void CUDT::CCUpdate()
{
   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   if (m_llMaxBW <= 0)
      return;
   const double minSP = 1000000.0 / (double(m_llMaxBW) / m_iMSS) * m_ullCPUFrequency;
   if (m_ullInterval < minSP)
       m_ullInterval = minSP;
}

void CUDT::initSynch()
{
   #ifndef WIN32
      pthread_mutex_init(&m_SendBlockLock, NULL);
      pthread_cond_init(&m_SendBlockCond, NULL);
      pthread_mutex_init(&m_RecvDataLock, NULL);
      pthread_cond_init(&m_RecvDataCond, NULL);
      pthread_mutex_init(&m_SendLock, NULL);
      pthread_mutex_init(&m_RecvLock, NULL);
      pthread_mutex_init(&m_AckLock, NULL);
      pthread_mutex_init(&m_ConnectionLock, NULL);
   #else
      m_SendBlockLock = CreateMutex(NULL, false, NULL);
      m_SendBlockCond = CreateEvent(NULL, false, false, NULL);
      m_RecvDataLock = CreateMutex(NULL, false, NULL);
      m_RecvDataCond = CreateEvent(NULL, false, false, NULL);
      m_SendLock = CreateMutex(NULL, false, NULL);
      m_RecvLock = CreateMutex(NULL, false, NULL);
      m_AckLock = CreateMutex(NULL, false, NULL);
      m_ConnectionLock = CreateMutex(NULL, false, NULL);
   #endif
}

void CUDT::destroySynch()
{
   #ifndef WIN32
      pthread_mutex_destroy(&m_SendBlockLock);
      pthread_cond_destroy(&m_SendBlockCond);
      pthread_mutex_destroy(&m_RecvDataLock);
      pthread_cond_destroy(&m_RecvDataCond);
      pthread_mutex_destroy(&m_SendLock);
      pthread_mutex_destroy(&m_RecvLock);
      pthread_mutex_destroy(&m_AckLock);
      pthread_mutex_destroy(&m_ConnectionLock);
   #else
      CloseHandle(m_SendBlockLock);
      CloseHandle(m_SendBlockCond);
      CloseHandle(m_RecvDataLock);
      CloseHandle(m_RecvDataCond);
      CloseHandle(m_SendLock);
      CloseHandle(m_RecvLock);
      CloseHandle(m_AckLock);
      CloseHandle(m_ConnectionLock);
   #endif
}

void CUDT::releaseSynch()
{
   #ifndef WIN32
      // wake up user calls
      pthread_mutex_lock(&m_SendBlockLock);
      pthread_cond_signal(&m_SendBlockCond);
      pthread_mutex_unlock(&m_SendBlockLock);

      pthread_mutex_lock(&m_SendLock);
      pthread_mutex_unlock(&m_SendLock);

      pthread_mutex_lock(&m_RecvDataLock);
      pthread_cond_signal(&m_RecvDataCond);
      pthread_mutex_unlock(&m_RecvDataLock);

      pthread_mutex_lock(&m_RecvLock);
      pthread_mutex_unlock(&m_RecvLock);
   #else
      SetEvent(m_SendBlockCond);
      WaitForSingleObject(m_SendLock, INFINITE);
      ReleaseMutex(m_SendLock);
      SetEvent(m_RecvDataCond);
      WaitForSingleObject(m_RecvLock, INFINITE);
      ReleaseMutex(m_RecvLock);
   #endif
}

void CUDT::sendCtrl(int pkttype, void* lparam, void* rparam, int size)
{
   CPacket ctrlpkt;

   switch (pkttype)
   {
   case 2: //010 - Acknowledgement
      {
      int32_t ack;

      // If there is no loss, the ACK is the current largest sequence number plus 1;
      // Otherwise it is the smallest sequence number in the receiver loss list.
      if (0 == m_pRcvLossList->getLossLength())
         ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
      else
         ack = m_pRcvLossList->getFirstLostSeq();

      // 如果对端已经收到过这个ACK，不再重复发送
      if (ack == m_iRcvLastAckAck)
         break;

      // send out a lite ACK
      // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
      if (4 == size)
      {
         ctrlpkt.pack(pkttype, NULL, &ack, size);
         ctrlpkt.m_iID = m_PeerID;
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         break;
      }

      uint64_t currtime;
      CTimer::rdtsc(currtime);

      // There are new received packets to acknowledge, update related information.
      // ack：序列号小于ack的报文都已收到
      // m_iRcvLastAck：上次发送ACK时确认的序列号
      if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
      {
         int acksize = CSeqNo::seqoff(m_iRcvLastAck, ack);

         m_iRcvLastAck = ack;

         // 在接收缓冲区中确认新收到的数据，并触发事件（select epoll中等待了该事件，该事件作用？）
         m_pRcvBuffer->ackData(acksize);

         // signal a waiting "recv" call if there is any data available
         #ifndef WIN32
            pthread_mutex_lock(&m_RecvDataLock);
            if (m_bSynRecving)
               pthread_cond_signal(&m_RecvDataCond);
            pthread_mutex_unlock(&m_RecvDataLock);
         #else
            if (m_bSynRecving)
               SetEvent(m_RecvDataCond);
         #endif

         // acknowledge any waiting epolls to read
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
      }
      else if (ack == m_iRcvLastAck)
      {
         // 判断是否需要重传该序列号的ACK数据报。RTO：超时重传。
         if ((currtime - m_ullLastAckTime) < ((m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency))
            break;
      }
      else
         break;

      // Send out the ACK only if has not been received by the sender before
      // m_iRcvLastAck：本次ACK序列号
      // m_iRcvLastAckAck：上一次向发送端成功发送的ACK序列号
      if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
      {
         int32_t data[6];

         m_iAckSeqNo = CAckNo::incack(m_iAckSeqNo);
         data[0] = m_iRcvLastAck;
         data[1] = m_iRTT;
         data[2] = m_iRTTVar;
         data[3] = m_pRcvBuffer->getAvailBufSize();
         // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
         if (data[3] < 2)
            data[3] = 2;

         // 一个速率控制周期内只向对方发送一次收包速率与带宽
         if (currtime - m_ullLastAckTime > m_ullSYNInt)
         {
            data[4] = m_pRcvTimeWindow->getPktRcvSpeed();
            data[5] = m_pRcvTimeWindow->getBandwidth();
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 24);

            CTimer::rdtsc(m_ullLastAckTime);
         }
         else
         {
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 16);
         }

         ctrlpkt.m_iID = m_PeerID;
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         // 记录下该ACK报文的发送时间
         m_pACKWindow->store(m_iAckSeqNo, m_iRcvLastAck);

         ++ m_iSentACK;
         ++ m_iSentACKTotal;
      }

      break;
      }

   case 6: //110 - Acknowledgement of Acknowledgement
      ctrlpkt.pack(pkttype, lparam);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 3: //011 - Loss Report
      {
      if (NULL != rparam)
      {
         if (1 == size)
         {
            // only 1 loss packet
            ctrlpkt.pack(pkttype, NULL, (int32_t *)rparam + 1, 4);
         }
         else
         {
            // more than 1 loss packets
            ctrlpkt.pack(pkttype, NULL, rparam, 8);
         }

         ctrlpkt.m_iID = m_PeerID;
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         ++ m_iSentNAK;
         ++ m_iSentNAKTotal;
      }
      else if (m_pRcvLossList->getLossLength() > 0)
      {
         // this is periodically NAK report; make sure NAK cannot be sent back too often

         // read loss list from the local receiver loss list
         int32_t* data = new int32_t[m_iPayloadSize / 4];
         int losslen;
         m_pRcvLossList->getLossArray(data, losslen, m_iPayloadSize / 4);

         if (0 < losslen)
         {
            ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
            ctrlpkt.m_iID = m_PeerID;
            m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

            ++ m_iSentNAK;
            ++ m_iSentNAKTotal;
         }

         delete [] data;
      }

      // update next NAK time, which should wait enough time for the retansmission, but not too long
      m_ullNAKInt = (m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency;
      int rcv_speed = m_pRcvTimeWindow->getPktRcvSpeed();
      if (rcv_speed > 0)
         m_ullNAKInt += (m_pRcvLossList->getLossLength() * 1000000ULL / rcv_speed) * m_ullCPUFrequency;
      if (m_ullNAKInt < m_ullMinNakInt)
         m_ullNAKInt = m_ullMinNakInt;

      break;
      }

   case 4: //100 - Congestion Warning
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      CTimer::rdtsc(m_ullLastWarningTime);

      break;

   case 1: //001 - Keep-alive
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
 
      break;

   case 0: //000 - Handshake
      ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 5: //101 - Shutdown
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 7: //111 - Msg drop request
      ctrlpkt.pack(pkttype, lparam, rparam, 8);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 8: //1000 - acknowledge the peer side a special error
      ctrlpkt.pack(pkttype, lparam);
      ctrlpkt.m_iID = m_PeerID;
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 32767: //0x7FFF - Resevered for future use
      break;

   default:
      break;
   }
}

void CUDT::processCtrl(CPacket& ctrlpkt)
{
   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

   switch (ctrlpkt.getType())
   {
   case 2: //010 - Acknowledgement
      {
      int32_t ack;

      // m_iSndLastAck 收到的数值最大的ack，m_iSndLastAck

      // process a lite ACK
      if (4 == ctrlpkt.getLength())
      {
         ack = *(int32_t *)ctrlpkt.m_pcData;
         if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
         {
            m_iFlowWindowSize -= CSeqNo::seqoff(m_iSndLastAck, ack);
            m_iSndLastAck = ack;
         }

         break;
      }

       // read ACK seq. no.
      ack = ctrlpkt.getAckSeqNo();

      // send ACK acknowledgement
      // number of ACK2 can be much less than number of ACK
      uint64_t now = CTimer::getTime();
      // 间隔一个速率控制周期 或者 ack2报文需要重发（接收端没有收到该ACK的ACK2）
      if ((currtime - m_ullSndLastAck2Time > (uint64_t)m_iSYNInterval) || (ack == m_iSndLastAck2))
      {
         sendCtrl(6, &ack);
         m_iSndLastAck2 = ack;
         m_ullSndLastAck2Time = now;
      }

      // Got data ACK
      ack = *(int32_t *)ctrlpkt.m_pcData;

      // 校验ACK报文所确认的报文序列号（不可能大于目前已经发送出去的报文的最大序列号）
      // check the validation of the ack
      if (CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
      {
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
      {
         // Update Flow Window Size, must update before and together with m_iSndLastAck
         m_iFlowWindowSize = *((int32_t *)ctrlpkt.m_pcData + 3);
         m_iSndLastAck = ack;
      }

      // protect packet retransmission
      CGuard::enterCS(m_AckLock);

      // m_iSndLastAck记录最大的ack，包括了lite ack。代表对端确认小于ack的报文都已收到
      // 上次收到的m_iSndLastDataAck，最大的普通ack。在不丢失普通ack的情况下，同m_iSndLastAck。
      // m_iSndLastDataAck是用来更新send buffer的依据。
      int offset = CSeqNo::seqoff(m_iSndLastDataAck, ack);
      if (offset <= 0)
      {
         // discard it if it is a repeated ACK
         CGuard::leaveCS(m_AckLock);
         break;
      }

      // acknowledge the sending buffer
      m_pSndBuffer->ackData(offset);

      // record total time used for sending
      m_llSndDuration += currtime - m_llSndDurationCounter;
      m_llSndDurationTotal += currtime - m_llSndDurationCounter;
      m_llSndDurationCounter = currtime;

      // update sending variables
      m_iSndLastDataAck = ack;
      m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));

      CGuard::leaveCS(m_AckLock);

      #ifndef WIN32
         pthread_mutex_lock(&m_SendBlockLock);
         if (m_bSynSending)
            pthread_cond_signal(&m_SendBlockCond);
         pthread_mutex_unlock(&m_SendBlockLock);
      #else
         if (m_bSynSending)
            SetEvent(m_SendBlockCond);
      #endif

      // acknowledde any waiting epolls to write
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

      // insert this socket to snd list if it is not on the list yet
      m_pSndQueue->m_pSndUList->update(this, false);

      // Update RTT
      //m_iRTT = *((int32_t *)ctrlpkt.m_pcData + 1);
      //m_iRTTVar = *((int32_t *)ctrlpkt.m_pcData + 2);
      int rtt = *((int32_t *)ctrlpkt.m_pcData + 1);
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;

      m_pCC->setRTT(m_iRTT);

      if (ctrlpkt.getLength() > 16)
      {
         // 根据对端传过来的数据更新
         // Update Estimated Bandwidth and packet delivery rate
         if (*((int32_t *)ctrlpkt.m_pcData + 4) > 0)
            m_iDeliveryRate = (m_iDeliveryRate * 7 + *((int32_t *)ctrlpkt.m_pcData + 4)) >> 3;

         if (*((int32_t *)ctrlpkt.m_pcData + 5) > 0)
            m_iBandwidth = (m_iBandwidth * 7 + *((int32_t *)ctrlpkt.m_pcData + 5)) >> 3;

         m_pCC->setRcvRate(m_iDeliveryRate);
         m_pCC->setBandwidth(m_iBandwidth);
      }

      // 发送速率调整
      m_pCC->onACK(ack);
      CCUpdate();

      ++ m_iRecvACK;
      ++ m_iRecvACKTotal;

      break;
      }

   case 6: //110 - Acknowledgement of Acknowledgement
      {
      int32_t ack;
      int rtt = -1;

      // update RTT
      // 找到对应的ACK报文发送出去的时间，计算RTT
      rtt = m_pACKWindow->acknowledge(ctrlpkt.getAckSeqNo(), ack);
      if (rtt <= 0)
         break;

      //if increasing delay detected...
      //   sendCtrl(4);

      // RTT EWMA
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;

      m_pCC->setRTT(m_iRTT);

      // update last ACK that has been received by the sender
      if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
         m_iRcvLastAckAck = ack;

      break;
      }

   case 3: //011 - Loss Report
      {
      int32_t* losslist = (int32_t *)(ctrlpkt.m_pcData);

      m_pCC->onLoss(losslist, ctrlpkt.getLength() / 4);
      CCUpdate();

      bool secure = true;

      // 接收方Reneging，所谓Reneging的意思就是接收方有权把已经报给发送端SACK里的数据给丢了。
      // 这样干是不被鼓励的，因为这个事会把问题复杂化了，但是，接收方这么做可能会有些极端情况，比如要把内存给别的更重要的东西。
      // 所以，发送方也不能完全依赖SACK，还是要依赖ACK，并维护Time-Out，如果后续的ACK没有增长，那么还是要把SACK的东西重传，
      // 另外，接收端这边永远不能把SACK的包标记为Ack。

      // decode loss list message and insert loss into the sender loss list
      for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++ i)
      {
         // 序列号区间(a, b) a最高位为1
         if (0 != (losslist[i] & 0x80000000))
         {
            if ((CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, losslist[i + 1]) > 0) || (CSeqNo::seqcmp(losslist[i + 1], m_iSndCurrSeqNo) > 0))
            {
               // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
               secure = false;
               break;
            }

            int num = 0;

            // 已经确认收到过的不再添加进丢失列表
            // m_iSndLastAck=2000 losslist[i]=3000 losslist[i+1]=4000 insert[3000, 4000]
            // m_iSndLastAck=2000 losslist[i]=1000 losslist[i+1]=3000 insert[2000, 3000]
            // 当前模式下，接收端没有定时发送NAK消息的机制，如果NAK消息有丢失，则丢失的NAK消息中的序列号所标识的报文只能等超时发送
            if (CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, m_iSndLastAck) >= 0)
               num = m_pSndLossList->insert(losslist[i] & 0x7FFFFFFF, losslist[i + 1]);
            else if (CSeqNo::seqcmp(losslist[i + 1], m_iSndLastAck) >= 0)
               num = m_pSndLossList->insert(m_iSndLastAck, losslist[i + 1]);

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;

            ++ i;
         }
         else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
         {
            if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
            {
               //seq_a must not be greater than the most recent sent seq
               secure = false;
               break;
            }

            int num = m_pSndLossList->insert(losslist[i], losslist[i]);

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;
         }
      }

      if (!secure)
      {
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      // the lost packet (retransmission) should be sent out immediately
      m_pSndQueue->m_pSndUList->update(this);

      ++ m_iRecvNAK;
      ++ m_iRecvNAKTotal;

      break;
      }

   case 4: //100 - Delay Warning
      // One way packet delay is increasing, so decrease the sending rate
      m_ullInterval = (uint64_t)ceil(m_ullInterval * 1.125);
      m_iLastDecSeq = m_iSndCurrSeqNo;

      break;

   case 1: //001 - Keep-alive
      // The only purpose of keep-alive packet is to tell that the peer is still alive
      // nothing needs to be done.

      break;

   case 0: //000 - Handshake
      {
      CHandShake req;
      req.deserialize(ctrlpkt.m_pcData, ctrlpkt.getLength());
      if ((req.m_iReqType > 0) || (m_bRendezvous && (req.m_iReqType != -2)))
      {
         // The peer side has not received the handshake message, so it keeps querying
         // resend the handshake packet

         // 对端没有收到握手消息，所以一直在查询
         // 重发握手数据包
         // 重发的握手数据包中不包含版本信息

         CHandShake initdata;
         initdata.m_iISN = m_iISN;
         initdata.m_iMSS = m_iMSS;
         // m_iFlightFlagSize 重发时没有比较m_iRcvBufSize与m_iFlightFlagSize的大小
         initdata.m_iFlightFlagSize = m_iFlightFlagSize;
         initdata.m_iReqType = (!m_bRendezvous) ? -1 : -2;
         initdata.m_iID = m_SocketID;

         char* hs = new char [m_iPayloadSize];
         int hs_size = m_iPayloadSize;
         initdata.serialize(hs, hs_size);
         sendCtrl(0, NULL, hs, hs_size);
         delete [] hs;
      }

      break;
      }

   case 5: //101 - Shutdown
      m_bShutdown = true;
      m_bClosing = true;
      m_bBroken = true;
      m_iBrokenCounter = 60;

      // Signal the sender and recver if they are waiting for data.
      releaseSynch();

      CTimer::triggerEvent();

      break;

   case 7: //111 - Msg drop request
      m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq());
      m_pRcvLossList->remove(*(int32_t*)ctrlpkt.m_pcData, *(int32_t*)(ctrlpkt.m_pcData + 4));

      // move forward with current recv seq no.
      // 如果下一个收取的为MSG或正处于MSG时，跳过Msg所覆盖的 seq no 区间
      // seq1 <= (m_iRcvCurrSeqNo + 1) && seq2 > m_iRcvCurrSeqNo
      if ((CSeqNo::seqcmp(*(int32_t*)ctrlpkt.m_pcData, CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
         && (CSeqNo::seqcmp(*(int32_t*)(ctrlpkt.m_pcData + 4), m_iRcvCurrSeqNo) > 0))
      {
         m_iRcvCurrSeqNo = *(int32_t*)(ctrlpkt.m_pcData + 4);
      }

      break;

   case 8: // 1000 - An error has happened to the peer side
      //int err_type = packet.getAddInfo();

      // currently only this error is signalled from the peer side
      // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
      // giving the app a chance to fix the issue

      m_bPeerHealth = false;

      break;

   case 32767: //0x7FFF - reserved and user defined messages
      m_pCC->processCustomMsg(&ctrlpkt);
      CCUpdate();

      break;

   default:
      break;
   }
}

// 打包出一个报文，返回值大于0才说明打包成功。
// 返回值 = 0 说明无数据可打包。

// 发送丢失链表不为空，则从中取出第一个报文序列号，并将该序列号删除（也就意味着如果该报文再次丢失，则只能在再次被添加到发送丢失链表中时重发）
// !!!重传丢失报文不受拥塞窗口的限制，拥塞窗口代表了接收端的接收能力。既然报文被标示为丢失，说明接收端有能力接收该报文。

// ts代表该UDT下一次被调度的时间
// ts = 0 : 发送丢失链表为空的情况下，发送窗口已满或发送缓冲区无数据发送。
// pop调用packData后发现如果此次没有可发送数据，将不再把该U放到待发送UDT列表中。Receive Queue的超时事件会判断是否有待发送数据，如果有，则重新将U加入。
int CUDT::packData(CPacket& packet, uint64_t& ts)
{
   int payload = 0;
   bool probe = false;

   uint64_t entertime;
   CTimer::rdtsc(entertime);

   if ((0 != m_ullTargetTime) && (entertime > m_ullTargetTime))
      m_ullTimeDiff += entertime - m_ullTargetTime;

   // Loss retransmission always has higher priority.
   if ((packet.m_iSeqNo = m_pSndLossList->getLostSeq()) >= 0)
   {
      // protect m_iSndLastDataAck from updating by ACK processing
      CGuard ackguard(m_AckLock);

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, packet.m_iSeqNo);
      if (offset < 0)
         return 0;

      int msglen;

      payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, msglen);

      if (-1 == payload) // 数据过期
      {
         int32_t seqpair[2];
         seqpair[0] = packet.m_iSeqNo;
/*
此处加msglen，seqpair[1]指向的可能为下一个MSG的第一个数据报的序列号
发送到对端后，对端将认为下一个MSG的第一个数据报为收到，造成混乱。
需改为：
seqpair[1] = CSeqNo::incseq(seqpair[0], ((msglen > 1) ? (msglen - 1) : 0));
*/
         seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);
         sendCtrl(7, &packet.m_iMsgNo, seqpair, 8);

         // only one msg drop request is necessary
         m_pSndLossList->remove(seqpair[1]);

         // skip all dropped packets
         if (CSeqNo::seqcmp(m_iSndCurrSeqNo, CSeqNo::incseq(seqpair[1])) < 0)
/*
m_iSndCurrSeqNo再次加1，相当于MSG最后一个报文的序列号+2
但下次发送时分配给报文的序列号将再次加1(+3)，造成+2的报文没有发送。对端将受到+3的报文，认为+2丢失，发送+2的NAK。

如果发送端应接收端重发的要求重发+2报文，则+2报文的内容实际为第二个MSG的第一个报文或者第二个报文。如果没有第二个MSG将出现空包或者读错误。
+2报文的内容：是根据与上次确认的报文序列号的偏移量算出的。因为相对于MSG最后一个报文的序列号+2，故造成读取下一个MSG的报文。
*/
             m_iSndCurrSeqNo = CSeqNo::incseq(seqpair[1]);

         return 0;
      }
      else if (0 == payload) // 为什么有可能等于0？
         return 0;

      ++ m_iTraceRetrans;
      ++ m_iRetransTotal;
   }
   else
   {
      // If no loss, pack a new packet.

      // check congestion/flow window limit
      // 发送窗口取m_iFlowWindowSize和m_dCongestionWindow的较小值
      // 按序读取发送缓冲区中待发送的数据
      // 窗口起始值以m_iSndLastAck为准
      int cwnd = (m_iFlowWindowSize < (int)m_dCongestionWindow) ? m_iFlowWindowSize : (int)m_dCongestionWindow;
      if (cwnd >= CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)))
      {
         if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo)))
         {
            m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
            m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);

            packet.m_iSeqNo = m_iSndCurrSeqNo;

            // every 16 (0xF) packets, a packet pair is sent
            if (0 == (packet.m_iSeqNo & 0xF))
               probe = true;
         }
         else
         {
            m_ullTargetTime = 0;
            m_ullTimeDiff = 0;
            ts = 0;
            return 0;
         }
      }
      else
      {
         m_ullTargetTime = 0;
         m_ullTimeDiff = 0;
         ts = 0;
         return 0;
      }
   }

   packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
   packet.m_iID = m_PeerID;
   packet.setLength(payload);

   m_pCC->onPktSent(&packet);
   //m_pSndTimeWindow->onPktSent(packet.m_iTimeStamp);

   ++ m_llTraceSent;
   ++ m_llSentTotal;

   if (probe)
   {
/*
包对：两个连续发送的探测包。
为了让接收端估算链路带宽，需要连续发送序号为16n和16n+1的包。接收端记录这两个包达到时的时间间隔。
*/
      // sends out probing packet pair
      ts = entertime;
      probe = false;
   }
   else
   {
/*
为了保证以接近理想的速率发送报文，必须根据实际情况修正下次报文发送时间。
首先判断下个包是否立即发送，如果是，则为当前时间。
否则，累积延滞时间（预期发送时间与当前时间的差值）大于报文发送间隔时间，则更新为当前时间。
反之，则为当前时间加上（报文发送间隔时间-累积延滞时间）。
*/
      #ifndef NO_BUSY_WAITING
         ts = entertime + m_ullInterval;
      #else
         if (m_ullTimeDiff >= m_ullInterval)
         {
            ts = entertime;
            m_ullTimeDiff -= m_ullInterval;
         }
         else
         {
            ts = entertime + m_ullInterval - m_ullTimeDiff;
            m_ullTimeDiff = 0;
         }
      #endif
   }

   m_ullTargetTime = ts;

   return payload;
}

int CUDT::processData(CUnit* unit)
{
   CPacket& packet = unit->m_Packet;

   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

   m_pCC->onPktReceived(&packet);
   ++ m_iPktCount;
   // update time information
   m_pRcvTimeWindow->onPktArrival();

   // 32 33 48 49
   // 32       49
   //    33 48
   // 异常情况，中值过滤
   // check if it is probing packet pair
   if (0 == (packet.m_iSeqNo & 0xF))
      m_pRcvTimeWindow->probe1Arrival();
   else if (1 == (packet.m_iSeqNo & 0xF))
      m_pRcvTimeWindow->probe2Arrival();

   ++ m_llTraceRecv;
   ++ m_llRecvTotal;

   int32_t offset = CSeqNo::seqoff(m_iRcvLastAck, packet.m_iSeqNo);
   // 已收 或 接收缓冲区中没有相应的地方存放
   if ((offset < 0) || (offset >= m_pRcvBuffer->getAvailBufSize()))
      return -1;

   if (m_pRcvBuffer->addData(unit, offset) < 0)
      return -1;

   // Loss detection.
   // 如果收到包的序列号大于(上次收到的+1)，说明中间有丢失

   // 1 2 3 4 5 6 7 8
   // 如果 1 3 收到，则 2 丢失，接收端只发送一次 2 的NAK。发送端即使没有收到这个NAK，也会在超时事件发生时，把 2 加入到自己的发送丢失列表中。
   // 利用超时重发数据，数据发送效率会变低。
   // 超时：在规定的时间（m_iRTT + 4 * m_iRTTVar）内，发送端没有收到对端响应。
   // 高延迟的网络环境重发效率将极其低下。

// 1 2 3 4 5 6 7 8 9
// 1   3       7
// 收到3，则更新m_iRcvCurrSeqNo为3，NAK消息中丢包序号为2，个数为1
// 收到7，则更新m_iRcvCurrSeqNo为7，NAK消息中丢包序号为[4, 6]，个数为3
// 后收到2 4 5 6，接收端都不会发送NAK消息
   if (CSeqNo::seqcmp(packet.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
   {
      // If loss found, insert them to the receiver loss list
      m_pRcvLossList->insert(CSeqNo::incseq(m_iRcvCurrSeqNo), CSeqNo::decseq(packet.m_iSeqNo));

      // pack loss list for NAK
      int32_t lossdata[2];
      lossdata[0] = CSeqNo::incseq(m_iRcvCurrSeqNo) | 0x80000000;
      lossdata[1] = CSeqNo::decseq(packet.m_iSeqNo);

      // Generate loss report immediately.
      sendCtrl(3, NULL, lossdata, (CSeqNo::incseq(m_iRcvCurrSeqNo) == CSeqNo::decseq(packet.m_iSeqNo)) ? 1 : 2);

      int loss = CSeqNo::seqlen(m_iRcvCurrSeqNo, packet.m_iSeqNo) - 2;
      m_iTraceRcvLoss += loss;
      m_iRcvLossTotal += loss;
   }

   // This is not a regular fixed size packet...   
   // an irregular sized packet usually indicates the end of a message, so send an ACK immediately
   // 这个地方体现出它只能用于大数据传输
   if (packet.getLength() != m_iPayloadSize)   
      CTimer::rdtsc(m_ullNextACKTime); 

   // Update the current largest sequence number that has been received.
   // Or it is a retransmitted packet, remove it from receiver loss list.
   if (CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
      m_iRcvCurrSeqNo = packet.m_iSeqNo;
   else
      m_pRcvLossList->remove(packet.m_iSeqNo);

   return 0;
}

// addr：UDP包的源地址
int CUDT::listen(sockaddr* addr, CPacket& packet)
{
   if (m_bClosing)
      return 1002;

   if (packet.getLength() != CHandShake::m_iContentSize)
      return 1004;

   CHandShake hs;
   hs.deserialize(packet.m_pcData, packet.getLength());

   // SYN cookie
   char clienthost[NI_MAXHOST];
   char clientport[NI_MAXSERV];

   // The getnameinfo() function is the inverse of getaddrinfo(3): it
   // converts a socket address to a corresponding host and service, in a
   // protocol-independent manner.  It combines the functionality of
   // gethostbyaddr(3) and getservbyport(3), but unlike those functions,
   // getnameinfo() is reentrant and allows programs to eliminate
   // IPv4-versus-IPv6 dependencies.

   // getnameinfo() converts the internal binary representation of an IP address
   // in the form of a struct sockaddr pointer into text strings consisting of the hostname or,
   // if the address cannot be resolved into a name, a textual IP address representation,
   // as well as the service port name or number. 

   // cookie 根据当前时间计算，每分钟一个。验证cookie时，会和当前分钟数以及上一分钟数对应的cookie进行比较。
   getnameinfo(addr, (AF_INET == m_iVersion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), clienthost, sizeof(clienthost), clientport, sizeof(clientport), NI_NUMERICHOST|NI_NUMERICSERV);
   int64_t timestamp = (CTimer::getTime() - m_StartTime) / 60000000; // secret changes every one minute
   stringstream cookiestr;
   cookiestr << clienthost << ":" << clientport << ":" << timestamp;
   unsigned char cookie[16];
   CMD5::compute(cookiestr.str().c_str(), cookie);

   if (1 == hs.m_iReqType)
   {
      hs.m_iCookie = *(int*)cookie;
      packet.m_iID = hs.m_iID;
      int size = packet.getLength();
      hs.serialize(packet.m_pcData, size);
      m_pSndQueue->sendto(addr, packet);
      return 0;
   }
   else
   {
      if (hs.m_iCookie != *(int*)cookie)
      {
         timestamp --;
         cookiestr << clienthost << ":" << clientport << ":" << timestamp;
         CMD5::compute(cookiestr.str().c_str(), cookie);

         if (hs.m_iCookie != *(int*)cookie)
            return -1;
      }
   }

   int32_t id = hs.m_iID;

   // When a peer side connects in...
   if ((1 == packet.getFlag()) && (0 == packet.getType()))
   {
      if ((hs.m_iVersion != m_iVersion) || (hs.m_iType != m_iSockType))
      {
         // mismatch, reject the request
         hs.m_iReqType = 1002;
         int size = CHandShake::m_iContentSize;
         hs.serialize(packet.m_pcData, size);
         packet.m_iID = id;
         m_pSndQueue->sendto(addr, packet);
      }
      else
      {
         int result = s_UDTUnited.newConnection(m_SocketID, addr, &hs);
         if (result == -1)
            hs.m_iReqType = 1002;

         // send back a response if connection failed or connection already existed
         // new connection response should be sent in connect()
         if (result != 1)
         {
            int size = CHandShake::m_iContentSize;
            hs.serialize(packet.m_pcData, size);
            packet.m_iID = id;
            m_pSndQueue->sendto(addr, packet);
         }
         else
         {
            // a new connection has been created, enable epoll for write 
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
         }
      }
   }

   return hs.m_iReqType;
}

void CUDT::checkTimers()
{
   // update CC parameters
   CCUpdate();
   //uint64_t minint = (uint64_t)(m_ullCPUFrequency * m_pSndTimeWindow->getMinPktSndInt() * 0.9);
   //if (m_ullInterval < minint)
   //   m_ullInterval = minint;

   uint64_t currtime;
   CTimer::rdtsc(currtime);

   // ACK事件（是否发送ACK数据报 根据时间或者间隔报文个数）
   if ((currtime > m_ullNextACKTime) || ((m_pCC->m_iACKInterval > 0) && (m_pCC->m_iACKInterval <= m_iPktCount)))
   {
      // ACK timer expired or ACK interval is reached

      // 触发发送ACK包，对已收到的数据进行确认。sendCtrl内部将决定是否发送（是重传还是一个新的）、发送的内容等。
      sendCtrl(2);

      // 更新下次ACK事件时间
      CTimer::rdtsc(currtime);
      if (m_pCC->m_iACKPeriod > 0)
         m_ullNextACKTime = currtime + m_pCC->m_iACKPeriod * m_ullCPUFrequency;
      else
         m_ullNextACKTime = currtime + m_ullACKInt;

      m_iPktCount = 0;
      m_iLightACKCount = 1;
   }
   // 通过CCC设置的应答时间间隔和应答个数间隔都非常大，内部会自己发送一个轻量的ACK
   else if (m_iSelfClockInterval * m_iLightACKCount <= m_iPktCount)
   {
      //send a "light" ACK
      sendCtrl(2, NULL, NULL, 4);
      ++ m_iLightACKCount;
   }

   // we are not sending back repeated NAK anymore and rely on the sender's EXP for retransmission
   //if ((m_pRcvLossList->getLossLength() > 0) && (currtime > m_ullNextNAKTime))
   //{
   //   // NAK timer expired, and there is loss to be reported.
   //   sendCtrl(3);
   //
   //   CTimer::rdtsc(currtime);
   //   m_ullNextNAKTime = currtime + m_ullNAKInt;
   //}

   // m_ullLastRspTime 上次收到报文的时间
   uint64_t next_exp_time;
   if (m_pCC->m_bUserDefinedRTO)
      next_exp_time = m_ullLastRspTime + m_pCC->m_iRTO * m_ullCPUFrequency;
   else
   {
      uint64_t exp_int = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + m_iSYNInterval) * m_ullCPUFrequency;
      if (exp_int < m_iEXPCount * m_ullMinExpInt)
         exp_int = m_iEXPCount * m_ullMinExpInt;
      next_exp_time = m_ullLastRspTime + exp_int;
   }

   // 超时事件，一段时间内未收到对端数据（包括对端发来的数据以及控制报文）。

   // 高延迟（RTT超过1s）的网络环境，连接请求每个250ms发送一次，收到第一个响应后，连接建立成功。但后续仍会收到响应，这些响应也会刷新m_ullLastRspTime
   // 对端只要收到握手请求，就会发出响应。
   // 后续的重复响应导致超时事件的起始时间被覆盖。

   // 用来结束慢启动的超时事件应该单独计算（以第一次发送数据的时间戳为准）

   // EXP is used to trigger data packets retransmission and maintain connection status.
   // 超时事件 （清理长期不活动的连接，重发数据 or 保活）
   if (currtime > next_exp_time)
   {
      // m_iEXPCount在收到数据包或者控制包时均会置为1
      // 如果过期次数累积超过16次，且距离上次收到数据已经过去了5s
      // Haven't receive any information from the peer, is it dead?!
      // timeout: at least 16 expirations and must be greater than 10 seconds
      if ((m_iEXPCount > 16) && (currtime - m_ullLastRspTime > 5000000 * m_ullCPUFrequency))
      {
         //
         // Connection is broken. 
         // UDT does not signal any information about this instead of to stop quietly.
         // Application will detect this when it calls any UDT methods next time.
         //
         m_bClosing = true;
         m_bBroken = true;
         m_iBrokenCounter = 30;

         // 该UDT实例的 m_bBroken 已被设置，在下一个处理周期时将会被清理
         // update snd U list to remove this socket
         m_pSndQueue->m_pSndUList->update(this);

         // 释放同步条件
         releaseSynch();

         // app can call any UDT API to learn the connection_broken error
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR, true);

         // 触发事件(select selectEx epoll 在等待该事件发生)
         CTimer::triggerEvent();

         return;
      }

      // 重发 or 保活
      // 如果仍有未发送数据，则优先安排发送；反之，则向对端报送keep-alive报文
      // sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
      // recver: Send out a keep-alive packet
      if (m_pSndBuffer->getCurrBufSize() > 0)
      {
         if ((CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && (m_pSndLossList->getLossLength() == 0))
         {
            // resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
            // 把发送出去但没有确认的数据包都加入到丢失链表中
            // 但m_iSndLastAck为累积确认，所以虽然对端接收到一些乱序的数据包，但这些乱序的数据包仍然会被重发。（效率低！！！）

            // m_iSndLastAck代表对端确认
            int32_t csn = m_iSndCurrSeqNo;
            int num = m_pSndLossList->insert(m_iSndLastAck, csn);
            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;
         }

         // 拥塞控制处理超时，更新报文发送间隔。拥塞控制处理超时，定是在超时发生，但数据仍未发送完毕的情况下。
         // 很长时间没有收到对端的报文了，并且是在有数据未发送完毕的情况下，此时定义为超时
         m_pCC->onTimeout();
         CCUpdate();

         // 保证该U在有未发送数据的情况下，一定能得到处理。
         // 将该UDT实例放到列表顶端，让其在下一个处理周期立即可被处理。
         // immediately restart transmission
         m_pSndQueue->m_pSndUList->update(this);
      }
      else
      {
         // 未有数据发送的情况下
         // keep-alive时间间隔为RTO，所以保活时间间隔相当小
         sendCtrl(1);
      }

      ++ m_iEXPCount;
      // Reset last response time since we just sent a heart-beat.
      m_ullLastRspTime = currtime;
   }
}

void CUDT::addEPoll(const int eid)
{
   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.insert(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

   if (!m_bConnected || m_bBroken || m_bClosing)
      return;

   if (((UDT_STREAM == m_iSockType) && (m_pRcvBuffer->getRcvDataSize() > 0)) ||
      ((UDT_DGRAM == m_iSockType) && (m_pRcvBuffer->getRcvMsgNum() > 0)))
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
   if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
   }
}

void CUDT::removeEPoll(const int eid)
{
   // clear IO events notifications;
   // since this happens after the epoll ID has been removed, they cannot be set again
   set<int> remove;
   remove.insert(eid);
   s_UDTUnited.m_EPoll.update_events(m_SocketID, remove, UDT_EPOLL_IN | UDT_EPOLL_OUT, false);

   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.erase(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);
}
