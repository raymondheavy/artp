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

// ���������ӣ���������
// ����ACK��ʱ����Ĭ��ΪSYN���ɵ�����Ҳ�����÷��Ͷ��ٸ���֮�󣬷���һ��ACK��
void CUDTCC::onACK(int32_t ack)
{
   int64_t B = 0;
   double inc = 0;
   // Note: 1/24/2012
   // The minimum increase parameter is increased from "1.0 / m_iMSS" to 0.01
   // because the original was too small and caused sending rate to stay at low level
   // for long time.
   const double min_inc = 0.01;
    
   // �ж��Ƿ񵽴����ʵ���ʱ�䣬�����ǰʱ��δ�����򷵻ء�ÿ��һ��ʱ�䣨10ms���������ʵ�����
   uint64_t currtime = CTimer::getTime();
   if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
      return;
   
   // ��¼�������ʵ�����������ʱ��
   m_LastRCTime = currtime;

   // �������׶�
   // �������׶Σ�ͨ���������ڴ�С�����Ʒ������䷢�����ʹ��Ĭ��ֵ1us�������ܿ������ӵ���󴰿�
   if (m_bSlowStart)
   {
      // ����ӵ�����ڣ���ʼĬ��ֵΪ16�������ӵ�ֵΪ�ϴ�ACK�뱾��ACK֮��Զ��յ��İ��ĸ���
      m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
      m_iLastAck = ack;
    
      // m_dMaxCWndSize��������ʱ����Ϊ�Զ˵Ľ��մ��ڴ�С��
      // ӵ�����ڴ�С����Ϊ��󲻵ó����Զ˵Ľ��մ��ڴ�С

      // ӵ���������Ӻ�������������ӵ������
      if (m_dCWndSize > m_dMaxCWndSize)
      {
         // �������׶ν���
         m_bSlowStart = false;
         
         // ���°����ͼ��
         // ���ݶԶ��յ��������ʣ����±��ط������ʡ��Զ��յ�����������Ч�����������м��㡣�����Ч�򱾵ؼ��㡣

         // m_iRcvRate����·�ϵ�·������δ�����ת�����
         // ����Զ˽�����������ʵ������Ӧ��m_iRcvRate���ܴܺ󣨶�Ӧ��������ʮ������ʵ�������������ʵ����m_dPktSndPeriod�Ի��С�������Ӿ�ӵ��
         // ��μ���һ�����ʵ���ʼֵ��
         if (m_iRcvRate > 0)
            m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
         else
            // ������ʱ�����RTT������ʱ�ӣ��������ʵ���ʱ������������������ӵ�����ڴ�С
            m_dPktSndPeriod = (m_iRTT + m_iRCInterval) / m_dCWndSize;
      }
   }
   else
      // �����������׶Σ����ݶԶ��յ��������ʸ���ӵ�����ڴ�С��
      // ��λʱ�䣨΢�룩�ڶԶ��հ����� * ������ʱ�� + 16 ��16�ǲ�����;�İ��أ���
      // �Զ��ڷ�����ʱ���ڿ��յ��İ��ĸ��� +��һ��������
      // ACKʱ����������ӵ�����ڴ�С�������󷢰�Ƶ��
      m_dCWndSize = m_iRcvRate / 1000000.0 * (m_iRTT + m_iRCInterval) + 16;

   // During Slow Start, no rate increase
   // �����Ϊ�������׶Σ��򷵻ء����������׶β���������ʱ������
   if (m_bSlowStart)
      return;

   // ������ϴ����ʵ�����������ʧ�¼����򷵻أ����ٵ�������ʱ���������ܷ���̫���ˣ����ֶ�ʧ��
   if (m_bLoss)
   {
      m_bLoss = false;
      return;
   }
   
   // �����¸����ʵ��������ڣ����Ͱ��������Ӷ��ٸ����������ǰ����������С����λ��Packets/s����ֱ�ӹ�ϵ��
   // ����ʣ�����
   B = (int64_t)(m_iBandwidth - 1000000.0 / m_dPktSndPeriod);

   // �������±���������m_dPktSndPeriod����onLoss()֮�����
   // �����ǰ�����ͼ�������ϴ������½�ʱ�İ����ͼ�����ȷ�������ʱ���ٶ�Ҫ����������������õ�ʣ�����Ϊ(m_iBandwidth / 9)
   // ������m_dPktSndPeriod�½�����죩�ķ���
   if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((m_iBandwidth / 9) < B))
      B = m_iBandwidth / 9;

   // ��������ٶȹ��죬��������·���������������ٶȣ�����NAK�����н��١�
   // ���ӳٵ����绷����NAK��������ʱ����ɶ��������������ʲ�����
   // m_iBandwidth ���ֵ������Ӫ�����ƵĴ�����������·��������촫���ٶȣ����ڵ�λʱ�����ܴ�����ٲ�����Ӫ�����ƵĴ���

   // B���൱���¸�SYN����Ҫ���ӵİ�����ռ�õĴ���
   if (B <= 0)
      inc = min_inc;
   else
   {
      // ��B * MSS * 8�� ��B����λ��Packets/s��ת��Ϊbits/s
      // 0.0000015 = 10^(-9) * 1500
      // �� is a protocol parameter, which is 9 in the current protocol specification.

      // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
      // Beta = 1.5 * 10^(-6)
    
      // �����¸�SYN���ڿ����Ӱ��ĸ���
      // inc���ŷ������ʵĵ���������0��min_inc�����ڷ�������=0ֵ������ֵ�����������Ч�ʣ������ܿ��ٵؽ��ͣ����Խ����𵴡�
      inc = pow(10.0, ceil(log10(B * m_iMSS * 8.0))) * 0.0000015 / m_iMSS;

      if (inc < min_inc)
         inc = min_inc;
   }
   
   // m_dPktSndPeriod * inc Ϊ���ӵİ���Ҫռ�õ�ʱ��
   // m_dPktSndPeriod * inc + m_iRCInterval Ϊ����inc����֮����ռ����ʱ��
   m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);
}

// ��С��������
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
   // �ж��Ƿ������µ�һ��ӵ������
   if (CSeqNo::seqcmp(losslist[0] & 0x7FFFFFFF, m_iLastDecSeq) > 0)
   {
      m_dLastDecPeriod = m_dPktSndPeriod;

      // ���������ӣ�������ʱ������������֮�½�
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
        
      // m_iAvgNAKNum ��¼ƽ��һ��ӵ���������յ�NAK�ĸ���
      m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);

      // m_iNAKCount ӵ��������NAK����
      m_iNAKCount = 1;

      // ��ӵ�����������ʵ�������
      m_iDecCount = 1;

      m_iLastDecSeq = m_iSndCurrSeqNo;
      
      // ������������㷨��Ϊ���ڼ������������仯����û�仵��ʱ�����Ⲣ������ͬһʱ�����ӻ�������ʣ�����������˲�䲨���ܴ󣩡�
      // ����ͬһʱ�̳��������½�
      // remove global synchronization using randomization
      srand(m_iLastDecSeq);
      m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
      if (m_iDecRandom < 1)
         m_iDecRandom = 1;
   }
   // �����½����ܳ���֮ǰ��һ�롣
   // ���Ҹ��ݵ�ǰ�յ���NAK�����Ƿ�Ϊĳһ����������������������ʵ���
   else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
   {
      // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
      m_iLastDecSeq = m_iSndCurrSeqNo;
   }
}

void CUDTCC::onTimeout()
{
   // ���ӳ�
   // RTTԽ��MAX_WNDԽ����������Ŀ������߸��ӳ��ȶ�����֮�����������
   // ��ʱ����ʱ�������������׶Σ����·�������
   if (m_bSlowStart)
   {
      m_bSlowStart = false;
      if (m_iRcvRate > 0)
         // ��δ�յ�ACK������£���ʱ�¼�������
         // m_iRcvRate��ʼֵ������Ϊ16�����m_dPktSndPeriod = 1000000.0/16 = 62500us
         // m_dPktSndPeriod��ʼֵΪ1us��1us -> 62500us���������������½����������¶Զ˽������������½� 

         // Ϊʲô�������׶Σ���δ�յ�ACk������·����˳�ʱ��
         // RTTĬ��ֵ����ڸ��ӳٵ����绷����˵��̫С�����³�ʱ�¼����(m_iRTT + 4 * m_iRTTVar)ҲС��
         // ��С��ʱ�¼����Ĭ��Ϊ300ms��

         // ���ӵ�ʱ��Ӧ�þ�Ӧ��ȷ��RTT���Ա㳬ʱ�ܹ���ʵ�������RTTΪ׼��
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
