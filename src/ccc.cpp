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
   Yunhong Gu, last updated 02/21/2013
*****************************************************************************/


#include "core.h"
#include "ccc.h"
#include <cmath>
#include <cstring>

CCC::CCC():
m_iSYNInterval(CUDT::m_iSYNInterval),
m_dPktSndPeriod(1.0),
m_dCWndSize(16.0),
m_iBandwidth(),
m_dMaxCWndSize(),
m_iMSS(),
m_iSndCurrSeqNo(),
m_iRcvRate(),
m_iRTT(),
m_pcParam(NULL),
m_iPSize(0),
m_UDT(),
m_iACKPeriod(0),
m_iACKInterval(0),
m_bUserDefinedRTO(false),
m_iRTO(-1),
m_PerfInfo()
{
}

CCC::~CCC()
{
   delete [] m_pcParam;
}

void CCC::setACKTimer(int msINT)
{
   m_iACKPeriod = msINT > m_iSYNInterval ? m_iSYNInterval : msINT;
}

void CCC::setACKInterval(int pktINT)
{
   m_iACKInterval = pktINT;
}

void CCC::setRTO(int usRTO)
{
   m_bUserDefinedRTO = true;
   m_iRTO = usRTO;
}

void CCC::sendCustomMsg(CPacket& pkt) const
{
   CUDT* u = CUDT::getUDTHandle(m_UDT);

   if (NULL != u)
   {
      pkt.m_iID = u->m_PeerID;
      u->m_pSndQueue->sendto(u->m_pPeerAddr, pkt);
   }
}

const CPerfMon* CCC::getPerfInfo()
{
   try
   {
      CUDT* u = CUDT::getUDTHandle(m_UDT);
      if (NULL != u)
         u->sample(&m_PerfInfo, false);
   }
   catch (...)
   {
      return NULL;
   }

   return &m_PerfInfo;
}

void CCC::setMSS(int mss)
{
   m_iMSS = mss;
}

void CCC::setBandwidth(int bw)
{
   m_iBandwidth = bw;
}

void CCC::setSndCurrSeqNo(int32_t seqno)
{
   m_iSndCurrSeqNo = seqno;
}

void CCC::setRcvRate(int rcvrate)
{
   m_iRcvRate = rcvrate;
}

void CCC::setMaxCWndSize(int cwnd)
{
   m_dMaxCWndSize = cwnd;
}

void CCC::setRTT(int rtt)
{
   m_iRTT = rtt;
}

void CCC::setUserParam(const char* param, int size)
{
   delete [] m_pcParam;
   m_pcParam = new char[size];
   memcpy(m_pcParam, param, size);
   m_iPSize = size;
}

//
CUDTCC::CUDTCC():
m_iRCInterval(),
m_LastRCTime(),
m_bSlowStart(),
m_iLastAck(),
m_bLoss(),
m_iLastDecSeq(),
m_dLastDecPeriod(),
m_iNAKCount(),
m_iDecRandom(),
m_iAvgNAKNum(),
m_iDecCount()
{
}

void CUDTCC::init()
{
   m_iRCInterval = m_iSYNInterval;
   m_LastRCTime = CTimer::getTime();
   setACKTimer(m_iRCInterval);

   m_bSlowStart = true;
   m_iLastAck = m_iSndCurrSeqNo;
   m_bLoss = false;
   m_iLastDecSeq = CSeqNo::decseq(m_iLastAck);
   m_dLastDecPeriod = 1;
   m_iAvgNAKNum = 0;
   m_iNAKCount = 0;
   m_iDecRandom = 1;

   m_dCWndSize = 16;
   m_dPktSndPeriod = 1;
}

// 调整（增加）发送速率
// 发送ACK的时间间隔默认为SYN，可调整。也可设置发送多少个包之后，发送一次ACK。
void CUDTCC::onACK(int32_t ack)
{
   int64_t B = 0;
   double inc = 0;
   // Note: 1/24/2012
   // The minimum increase parameter is increased from "1.0 / m_iMSS" to 0.01
   // because the original was too small and caused sending rate to stay at low level
   // for long time.
   const double min_inc = 0.01;
    
   // 判断是否到达速率调整时间，如果当前时间未到，则返回。每隔一定时间（10ms）进行速率调整。
   uint64_t currtime = CTimer::getTime();
   if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
      return;
   
   // 记录本次速率调整（增长）时间
   m_LastRCTime = currtime;

   // 慢启动阶段
   // 慢启动阶段：通过调整窗口大小来控制发包，其发包间隔使用默认值1us，进而能快速增加到最大窗口
   if (m_bSlowStart)
   {
      // 增加拥塞窗口（初始默认值为16），增加的值为上次ACK与本次ACK之间对端收到的包的个数
      m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
      m_iLastAck = ack;
    
      // m_dMaxCWndSize：在握手时设置为对端的接收窗口大小。
      // 拥塞窗口大小限制为最大不得超过对端的接收窗口大小

      // 拥塞窗口增加后如果大于了最大拥塞窗口
      if (m_dCWndSize > m_dMaxCWndSize)
      {
         // 慢启动阶段结束
         m_bSlowStart = false;
         
         // 更新包发送间隔
         // 根据对端收到包的速率，更新本地发包速率。对端收到包的速率有效，则根据其进行计算。如果无效则本地计算。

         // m_iRcvRate跟链路上的路由器如何处理报文转发相关
         // 如果对端接收速率与真实带宽不对应，m_iRcvRate可能很大（对应数倍或数十倍于真实带宽），相对于真实带宽m_dPktSndPeriod仍会很小，继续加剧拥塞
         // 如何计算一个合适的起始值？
         if (m_iRcvRate > 0)
            m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
         else
            // 发包总时间等于RTT（往返时延）加上速率调整时间间隔，发包总量等于拥塞窗口大小
            m_dPktSndPeriod = (m_iRTT + m_iRCInterval) / m_dCWndSize;
      }
   }
   else
      // 不在慢启动阶段，根据对端收到包的速率更新拥塞窗口大小。
      // 单位时间（微秒）内对端收包个数 * 发包总时间 + 16 （16是不是在途的包呢？）
      // 对端在发包总时间内可收到的包的个数 +　一个步长？
      // ACK时，尝试增加拥塞窗口大小，已增大发包频率
      m_dCWndSize = m_iRcvRate / 1000000.0 * (m_iRTT + m_iRCInterval) + 16;

   // During Slow Start, no rate increase
   // 如果仍为慢启动阶段，则返回。（慢启动阶段不调整发包时间间隔）
   if (m_bSlowStart)
      return;

   // 如果自上次速率调整后发生过丢失事件，则返回，不再调整发包时间间隔。可能发包太快了，出现丢失？
   if (m_bLoss)
   {
      m_bLoss = false;
      return;
   }
   
   // 计算下个速率调整周期内，发送包可以增加多少个？这个跟当前带宽容量大小（单位：Packets/s）有直接关系。
   // 理论剩余带宽
   B = (int64_t)(m_iBandwidth - 1000000.0 / m_dPktSndPeriod);

   // 丢包导致变慢，所以m_dPktSndPeriod经过onLoss()之后会变大
   // 如果当前包发送间隔大于上次速率下降时的包发送间隔（比发生丢包时的速度要慢），限制最多利用的剩余带宽为(m_iBandwidth / 9)
   // 限制速m_dPktSndPeriod下降（变快）的幅度
   if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((m_iBandwidth / 9) < B))
      B = m_iBandwidth / 9;

   // 如果发送速度过快，超过了链路中所允许的最大传输速度，利用NAK包进行降速。
   // 高延迟的网络环境，NAK反馈不及时，造成丢包，带宽利用率波动大
   // m_iBandwidth 这个值不是运营商限制的带宽，可能是链路中数据最快传输速度，至于单位时间内能传输多少才是运营商限制的带宽

   // B就相当于下个SYN周期要增加的包可以占用的带宽
   if (B <= 0)
      inc = min_inc;
   else
   {
      // （B * MSS * 8） 将B（单位：Packets/s）转换为bits/s
      // 0.0000015 = 10^(-9) * 1500
      // τ is a protocol parameter, which is 9 in the current protocol specification.

      // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
      // Beta = 1.5 * 10^(-6)
    
      // 计算下个SYN周期可增加包的个数
      // inc随着发送速率的递增趋近于0（min_inc）。在发送速率=0值附近，值极大用以提高效率；并且能快速地降低，用以降低震荡。
      inc = pow(10.0, ceil(log10(B * m_iMSS * 8.0))) * 0.0000015 / m_iMSS;

      if (inc < min_inc)
         inc = min_inc;
   }
   
   // m_dPktSndPeriod * inc 为增加的包所要占用的时间
   // m_dPktSndPeriod * inc + m_iRCInterval 为增加inc个包之后所占用总时间
   m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);
}

// 减小发送速率
void CUDTCC::onLoss(const int32_t* losslist, int)
{
   //Slow Start stopped, if it hasn't yet
   if (m_bSlowStart)
   {
      m_bSlowStart = false;
      if (m_iRcvRate > 0)
      {
         // Set the sending rate to the receiving rate.
         m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
         return;
      }
      // If no receiving rate is observed, we have to compute the sending
      // rate according to the current window size, and decrease it
      // using the method below.
      m_dPktSndPeriod = m_dCWndSize / (m_iRTT + m_iRCInterval);
   }

   m_bLoss = true;

   // We define a congestion period as the period between two NAKs in which 
   // the first biggest lost packet sequence number is greater than the 
   // LastDecSeq, which is the biggest sequence number when last time the 
   // packet sending rate is decreased. 
   // 判断是否开启了新的一个拥塞周期
   if (CSeqNo::seqcmp(losslist[0] & 0x7FFFFFFF, m_iLastDecSeq) > 0)
   {
      m_dLastDecPeriod = m_dPktSndPeriod;

      // 调整（增加）包发送时间间隔，速率随之下降
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
        
      // m_iAvgNAKNum 记录平均一个拥塞周期内收到NAK的个数
      m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);

      // m_iNAKCount 拥塞周期内NAK个数
      m_iNAKCount = 1;

      // 本拥塞周期内速率调整次数
      m_iDecCount = 1;

      m_iLastDecSeq = m_iSndCurrSeqNo;
      
      // 采用随机采样算法是为了在减少网络质量变化（变好或变坏）时，避免并发流在同一时刻增加或减少速率（带宽利用率瞬间波动很大）。
      // 不在同一时刻出现速率下降
      // remove global synchronization using randomization
      srand(m_iLastDecSeq);
      m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
      if (m_iDecRandom < 1)
         m_iDecRandom = 1;
   }
   // 速率下降不能超过之前的一半。
   // 并且根据当前收到的NAK个数是否为某一个随机数的整数倍进行速率调整
   else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
   {
      // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
      m_iLastDecSeq = m_iSndCurrSeqNo;
   }
}

void CUDTCC::onTimeout()
{
   // 高延迟
   // RTT越大MAX_WND越大，这样做的目的是提高高延迟稳定网络之间的吞吐量。
   // 超时发生时，仍在慢启动阶段，更新发送速率
   if (m_bSlowStart)
   {
      m_bSlowStart = false;
      if (m_iRcvRate > 0)
         // 在未收到ACK的情况下，超时事件发生了
         // m_iRcvRate初始值被设置为16，因此m_dPktSndPeriod = 1000000.0/16 = 62500us
         // m_dPktSndPeriod初始值为1us，1us -> 62500us，发送速率严重下降，进而导致对端接收速率严重下降 

         // 为什么慢启动阶段，在未收到ACk的情况下发生了超时？
         // RTT默认值相对于高延迟的网络环境来说，太小，以致超时事件间隔(m_iRTT + 4 * m_iRTTVar)也小。
         // 最小超时事件间隔默认为300ms。

         // 连接的时候应该就应该确定RTT，以便超时能够以实际网络的RTT为准。
         m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
      else
         m_dPktSndPeriod = m_dCWndSize / (m_iRTT + m_iRCInterval);
   }
   else
   {
      /*
      m_dLastDecPeriod = m_dPktSndPeriod;
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
      m_iLastDecSeq = m_iLastAck;
      */
   }
}
