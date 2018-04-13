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
   Yunhong Gu, last updated 03/12/2011
*****************************************************************************/

#include <cstring>
#include <cmath>
#include "buffer.h"

using namespace std;

CSndBuffer::CSndBuffer(int size, int mss):
m_BufLock(),
m_pBlock(NULL),
m_pFirstBlock(NULL),
m_pCurrBlock(NULL),
m_pLastBlock(NULL),
m_pBuffer(NULL),
m_iNextMsgNo(1),
m_iSize(size),
m_iMSS(mss),
m_iCount(0)
{
   // initial physical buffer of "size"
   m_pBuffer = new Buffer;
   m_pBuffer->m_pcData = new char [m_iSize * m_iMSS];
   m_pBuffer->m_iSize = m_iSize;
   m_pBuffer->m_pNext = NULL;

   // circular linked list for out bound packets
   m_pBlock = new Block;
   // 应该提前赋初值 m_pNext m_iLength m_iMsgNo等。避免m_iSize=1时，没有做初始化

   // 环形
   Block* pb = m_pBlock;
   for (int i = 1; i < m_iSize; ++ i)
   {
      pb->m_pNext = new Block;
      pb->m_iMsgNo = 0;
      pb = pb->m_pNext;
   }
   pb->m_pNext = m_pBlock;

   // 最后一个pb的m_iMsgNo没有赋值

   pb = m_pBlock;
   char* pc = m_pBuffer->m_pcData;
   for (int i = 0; i < m_iSize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   // m_pFirstBlock 待确认指针，下一次要确认的位置，offset必定大于0
   // m_pCurrBlock  待读指针，从发送缓冲区读取数据，下一次要读取的位置
   // m_pLastBlock  待写指针，向发送缓冲区添加数据，下一次要写的位置

   m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

   #ifndef WIN32
      pthread_mutex_init(&m_BufLock, NULL);
   #else
      m_BufLock = CreateMutex(NULL, false, NULL);
   #endif
}

CSndBuffer::~CSndBuffer()
{
   Block* pb = m_pBlock->m_pNext;
   while (pb != m_pBlock)
   {
      Block* temp = pb;
      pb = pb->m_pNext;
      delete temp;
   }
   delete m_pBlock;

   while (m_pBuffer != NULL)
   {
      Buffer* temp = m_pBuffer;
      m_pBuffer = m_pBuffer->m_pNext;
      delete [] temp->m_pcData;
      delete temp;
   }

   #ifndef WIN32
      pthread_mutex_destroy(&m_BufLock);
   #else
      CloseHandle(m_BufLock);
   #endif
}

void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
      increase();

   uint64_t time = CTimer::getTime();
   int32_t inorder = order;
   inorder <<= 29;

   Block* s = m_pLastBlock;
   for (int i = 0; i < size; ++ i)
   {
      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
      s->m_iLength = pktlen;

      s->m_iMsgNo = m_iNextMsgNo | inorder;
      if (i == 0)
         s->m_iMsgNo |= 0x80000000;
      if (i == size - 1)
         s->m_iMsgNo |= 0x40000000;

      s->m_OriginTime = time;
      s->m_iTTL = ttl;

      s = s->m_pNext;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock);
   m_iCount += size;
   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == CMsgNo::m_iMaxMsgNo)
      m_iNextMsgNo = 1;
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
      increase();

   Block* s = m_pLastBlock;
   int total = 0;
   for (int i = 0; i < size; ++ i)
   {
      if (ifs.bad() || ifs.fail() || ifs.eof())
         break;

      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      ifs.read(s->m_pcData, pktlen);
      if ((pktlen = ifs.gcount()) <= 0)
         break;

      // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
      s->m_iMsgNo = m_iNextMsgNo | 0x20000000;
      if (i == 0)
         s->m_iMsgNo |= 0x80000000;
      if (i == size - 1)
         s->m_iMsgNo |= 0x40000000;

      s->m_iLength = pktlen;
      s->m_iTTL = -1;
      s = s->m_pNext;

      total += pktlen;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock);
   m_iCount += size;
   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == CMsgNo::m_iMaxMsgNo)
      m_iNextMsgNo = 1;

   return total;
}

// 按序读取
int CSndBuffer::readData(char** data, int32_t& msgno)
{
   // No data to read
   if (m_pCurrBlock == m_pLastBlock)
      return 0;

   *data = m_pCurrBlock->m_pcData;
   int readlen = m_pCurrBlock->m_iLength;
   msgno = m_pCurrBlock->m_iMsgNo;

   m_pCurrBlock = m_pCurrBlock->m_pNext;

   return readlen;
}

// 读取指定位置的报文
// offset 为相对于上次确认位置的偏移量
int CSndBuffer::readData(char** data, const int offset, int32_t& msgno, int& msglen)
{
   CGuard bufferguard(m_BufLock);

   Block* p = m_pFirstBlock;

   for (int i = 0; i < offset; ++ i)
      p = p->m_pNext;

   if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_OriginTime) / 1000 > (uint64_t)p->m_iTTL))
   {
      // 数据已过期

      msgno = p->m_iMsgNo & 0x1FFFFFFF;

      msglen = 1;
      p = p->m_pNext;
      bool move = false;
      while (msgno == (p->m_iMsgNo & 0x1FFFFFFF))
      {
         if (p == m_pCurrBlock) // 是否需要更新读指针，让读指针跳过该MSG所占区间
            move = true;
         p = p->m_pNext;
         if (move)
            m_pCurrBlock = p;
         msglen ++;
      }

      return -1;
   }

   *data = p->m_pcData;
   int readlen = p->m_iLength;
   msgno = p->m_iMsgNo;

   return readlen;
}

// 确认的报文区间[m_pFirstBlock, m_pFirstBlock + offset)，m_pFirstBlock = m_pFirstBlock + offset
void CSndBuffer::ackData(int offset)
{
   CGuard bufferguard(m_BufLock);

   for (int i = 0; i < offset; ++ i)
      m_pFirstBlock = m_pFirstBlock->m_pNext;

   m_iCount -= offset;

   CTimer::triggerEvent();
}

int CSndBuffer::getCurrBufSize() const
{
   return m_iCount;
}

void CSndBuffer::increase()
{
   int unitsize = m_pBuffer->m_iSize;

   // new physical buffer
   Buffer* nbuf = NULL;
   try
   {
      nbuf  = new Buffer;
      nbuf->m_pcData = new char [unitsize * m_iMSS];
   }
   catch (...)
   {
      delete nbuf;
      throw CUDTException(3, 2, 0);
   }
   nbuf->m_iSize = unitsize;
   nbuf->m_pNext = NULL;

   // insert the buffer at the end of the buffer list
   Buffer* p = m_pBuffer;
   while (NULL != p->m_pNext)
      p = p->m_pNext;
   p->m_pNext = nbuf;

   // new packet blocks
   Block* nblk = NULL;
   try
   {
      nblk = new Block;
   }
   catch (...)
   {
      delete nblk;
      throw CUDTException(3, 2, 0);
   }
   Block* pb = nblk;
   for (int i = 1; i < unitsize; ++ i)
   {
      pb->m_pNext = new Block;
      pb = pb->m_pNext;
   }

   // insert the new blocks onto the existing one
   pb->m_pNext = m_pLastBlock->m_pNext;
   m_pLastBlock->m_pNext = nblk;

   pb = nblk;
   char* pc = nbuf->m_pcData;
   for (int i = 0; i < unitsize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_iSize += unitsize;
}

////////////////////////////////////////////////////////////////////////////////

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize):
m_pUnit(NULL),
m_iSize(bufsize),
m_pUnitQueue(queue),
m_iStartPos(0),
m_iLastAckPos(0),
m_iMaxPos(0),
m_iNotch(0)
{
   m_pUnit = new CUnit* [m_iSize];
   for (int i = 0; i < m_iSize; ++ i)
      m_pUnit[i] = NULL;
}

CRcvBuffer::~CRcvBuffer()
{
   for (int i = 0; i < m_iSize; ++ i)
   {
      if (NULL != m_pUnit[i])
      {
         m_pUnit[i]->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;
      }
   }

   delete [] m_pUnit;
}

int CRcvBuffer::addData(CUnit* unit, int offset)
{
   int pos = (m_iLastAckPos + offset) % m_iSize;
   if (offset > m_iMaxPos)
      m_iMaxPos = offset;

   if (NULL != m_pUnit[pos])
      return -1;
   
   m_pUnit[pos] = unit;

   unit->m_iFlag = 1;
   ++ m_pUnitQueue->m_iCount;

   return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
   // m_iStartPos 上一次读取的位置
   int p = m_iStartPos; // 读指针
   int lastack = m_iLastAckPos; // 写指针
   int rs = len;

   // m_iNotch 上一次读取数据时，如果单元内的数据未完全读取，记录读取位置，以被后续读取。

   while ((p != lastack) && (rs > 0))
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;
      // rs 剩余要读取的大小
      // unitsize 本单元本次要读取的大小

      memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      data += unitsize;

      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         // 该单元的数据被完整读取后，置空
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;

         if (++ p == m_iSize)
            p = 0;

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

   m_iStartPos = p;
   return len - rs;
}

int CRcvBuffer::readBufferToFile(fstream& ofs, int len)
{
   int p = m_iStartPos;
   int lastack = m_iLastAckPos;
   int rs = len;

   while ((p != lastack) && (rs > 0))
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;

      ofs.write(m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      if (ofs.fail())
         break;

      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;

         if (++ p == m_iSize)
            p = 0;

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

   m_iStartPos = p;

   return len - rs;
}

void CRcvBuffer::ackData(int len)
{
   m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
   m_iMaxPos -= len;
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;

   CTimer::triggerEvent();
}

int CRcvBuffer::getAvailBufSize() const
{
   // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
   return m_iSize - getRcvDataSize() - 1;
}

int CRcvBuffer::getRcvDataSize() const
{
   if (m_iLastAckPos >= m_iStartPos)
      return m_iLastAckPos - m_iStartPos;

   return m_iSize + m_iLastAckPos - m_iStartPos;
}

void CRcvBuffer::dropMsg(int32_t msgno)
{
   for (int i = m_iStartPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
      if ((NULL != m_pUnit[i]) && (msgno == m_pUnit[i]->m_Packet.m_iMsgNo))
         m_pUnit[i]->m_iFlag = 3;
}

int CRcvBuffer::readMsg(char* data, int len)
{
   int p, q;
   bool passack;
   if (!scanMsg(p, q, passack))
      return 0;

   int rs = len;
   while (p != (q + 1) % m_iSize)
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength();
      if ((rs >= 0) && (unitsize > rs))
         unitsize = rs;

      if (unitsize > 0)
      {
         memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
         data += unitsize;
         rs -= unitsize;
      }

      if (!passack) // 数据已得到确认
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;
      }
      else
         m_pUnit[p]->m_iFlag = 2;

      if (++ p == m_iSize)
         p = 0;
   }

   // 数据已得到确认（保证读指针始终不大于写指针？）
   // if the buf is not enough to hold the first message, 
   // only part of the message will be copied into the buffer, 
   // but the message will still be discarded after this recvmsg call.
   if (!passack)
      m_iStartPos = (q + 1) % m_iSize;

   return len - rs;
}

int CRcvBuffer::getRcvMsgNum()
{
   int p, q;
   bool passack;
   return scanMsg(p, q, passack) ? 1 : 0;
}

bool CRcvBuffer::scanMsg(int& p, int& q, bool& passack)
{
   // m_iMaxPos代表的是最远packet与已确认数据区之间的距离
   // empty buffer
   if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
      return false;

   //skip all bad msgs at the beginning
   while (m_iStartPos != m_iLastAckPos)
   {
      if (NULL == m_pUnit[m_iStartPos])
      {
         if (++ m_iStartPos == m_iSize)
            m_iStartPos = 0;
         continue;
      }
/*
The next 32-bit field in the header is for the messaging. The first
two bits "FF" flags the position of the packet is a message. "10" is
the first packet, "01" is the last one, "11" is the only packet, and
"00" is any packets in the middle. The third bit "O" means if the
message should be delivered in order (1) or not (0). A message to be
delivered in order requires that all previous messages must be either
delivered or dropped. The rest 29 bits is the message number, similar
to packet sequence number (but independent). A UDT message may
contain multiple UDT packets.
*/
      // 是否为Msg的第一个packet
      if ((1 == m_pUnit[m_iStartPos]->m_iFlag) && (m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() > 1))
      {
         bool good = true;

         // look ahead for the whole message
         for (int i = m_iStartPos; i != m_iLastAckPos;)
         {
            // 当前CUnit是否为null或还未处于被占用状态（数据是否可读）
            if ((NULL == m_pUnit[i]) || (1 != m_pUnit[i]->m_iFlag))
            {
               // 表明Msg中有洞，不完整，跳出内循环，重新寻找Msg头
               good = false;
               break;
            }

            // 是否为Msg最后一个packet
            if ((m_pUnit[i]->m_Packet.getMsgBoundary() == 1) || (m_pUnit[i]->m_Packet.getMsgBoundary() == 3))
               break;

            if (++ i == m_iSize)
               i = 0;
         }

         // 是否找到一段完整的Msg
         if (good)
            break;
      }

      // 单元位置置空，返还单元给m_pUnitQueue
      CUnit* tmp = m_pUnit[m_iStartPos];
      m_pUnit[m_iStartPos] = NULL;
      tmp->m_iFlag = 0;
      -- m_pUnitQueue->m_iCount;

      if (++ m_iStartPos == m_iSize)
         m_iStartPos = 0;
   }

   p = -1;                  // message head
   q = m_iStartPos;         // message tail
   passack = m_iStartPos == m_iLastAckPos; // 是否已确认
   bool found = false;

   // m_iMaxPos + getRcvDataSize() 在尽可能大的范围内查找。
   // looking for the first message
   for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i <= n; ++ i)
   {
      if ((NULL != m_pUnit[q]) && (1 == m_pUnit[q]->m_iFlag))
      {
         switch (m_pUnit[q]->m_Packet.getMsgBoundary())
         {
         case 3: // 11 只有一个packet
            p = q;
            found = true;
            break;

         case 2: // 10 第一个
            p = q;
            break;

         case 1: // 01 最后一个
            if (p != -1) // 找到第一个的情况下
               found = true;
         }
      }
      else
      {
         // a hole in this message, not valid, restart search
         // 出现无效数据，则重置Msg头，重新查找
         p = -1;
      }

      if (found)
      {
         // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
         // Msg都已得到确认 或者 Msg可不按序读取
         if (!passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
            break;

         found = false;
      }

      if (++ q == m_iSize)
         q = 0;

      if (q == m_iLastAckPos) // 如果msg所占区间跨过了上次确认位置，也就代表msg的部分数据还未得到确认
         passack = true;
   }

   // no msg found
   if (!found)
   {
      // if the message is larger than the receiver buffer, return part of the message
      if ((p != -1) && ((q + 1) % m_iSize == p))
         found = true;
   }

   return found;
}
