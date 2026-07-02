#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <schannel.h>
#include <security.h>

// Undef Windows macros that conflict with method names
#ifdef SendMessage
#undef SendMessage
#endif

#include "CastChannel.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

// ---------- protobuf helpers ----------

static void WriteVarint(std::vector<uint8_t>& buf, uint64_t v)
{
   while (v > 0x7F)
   {
      buf.push_back((uint8_t)((v & 0x7F) | 0x80));
      v >>= 7;
   }
   buf.push_back((uint8_t)(v & 0x7F));
}

static void WriteTag(std::vector<uint8_t>& buf, int field, int wireType)
{
   WriteVarint(buf, (field << 3) | wireType);
}

static void WriteLengthDelimited(std::vector<uint8_t>& buf, int field, const std::string& s)
{
   WriteTag(buf, field, 2);
   WriteVarint(buf, s.size());
   buf.insert(buf.end(), s.begin(), s.end());
}

static void WriteVarintField(std::vector<uint8_t>& buf, int field, uint64_t v)
{
   WriteTag(buf, field, 0);
   WriteVarint(buf, v);
}

// ---------- CastMessage serialization ----------

std::vector<uint8_t> CastChannel::SerializeCastMessage(const std::string& sourceId,
                                                        const std::string& destId,
                                                        const std::string& namespace_,
                                                        const std::string& payload)
{
   std::vector<uint8_t> m;
   WriteVarintField(m, 1, 0); // protocol_version = CASTV2_1_0
   WriteLengthDelimited(m, 2, sourceId);
   WriteLengthDelimited(m, 3, destId);
   WriteLengthDelimited(m, 4, namespace_);
   WriteVarintField(m, 5, 0); // payload_type = STRING
   WriteLengthDelimited(m, 6, payload);

   // Frame: 4-byte big-endian length prefix
   std::vector<uint8_t> frame;
   uint32_t len = (uint32_t)m.size();
   frame.push_back((uint8_t)(len >> 24));
   frame.push_back((uint8_t)(len >> 16));
   frame.push_back((uint8_t)(len >> 8));
   frame.push_back((uint8_t)(len & 0xFF));
   frame.insert(frame.end(), m.begin(), m.end());
   return frame;
}

static std::string ReadLengthDelimitedString(const uint8_t*& data, int& remaining)
{
   uint64_t len = 0;
   int shift = 0;
   while (remaining > 0)
   {
      uint8_t b = *data++;
      remaining--;
      len |= (uint64_t)(b & 0x7F) << shift;
      shift += 7;
      if (!(b & 0x80))
         break;
   }
   if (len > (uint64_t)remaining)
      len = remaining;
   std::string s((const char*)data, (size_t)len);
   data += len;
   remaining -= (int)len;
   return s;
}

static bool DecodeCastMessage(const uint8_t* data, int len, CastChannel::Message& msg)
{
   const uint8_t* pos = data;
   while (len > 0)
   {
      uint64_t tagVal = 0;
      int shift = 0;
      while (len > 0)
      {
         uint8_t b = *pos++;
         len--;
         tagVal |= (uint64_t)(b & 0x7F) << shift;
         shift += 7;
         if (!(b & 0x80))
            break;
      }
      int fieldNum = (int)(tagVal >> 3);
      int wireType = (int)(tagVal & 0x7);

      if (wireType == 2)
      {
         std::string val = ReadLengthDelimitedString(pos, len);
         switch (fieldNum)
         {
            case 2: msg.sourceId = val; break;
            case 3: msg.destId = val; break;
            case 4: msg.namespace_ = val; break;
            case 6: msg.payload = val; break;
         }
      }
      else if (wireType == 0)
      {
         uint64_t v = 0;
         shift = 0;
         while (len > 0)
         {
            uint8_t b = *pos++;
            len--;
            v |= (uint64_t)(b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80))
               break;
         }
      }
      else
      {
         break;
      }
   }
   return !msg.namespace_.empty();
}

// ---------- CastChannel ----------

CastChannel::CastChannel()
{
   memset(&mCredHandle, 0, sizeof(mCredHandle));
   memset(&mContextHandle, 0, sizeof(mContextHandle));
   memset(&mStreamSizes, 0, sizeof(mStreamSizes));

   WSADATA wsa;
   WSAStartup(MAKEWORD(2, 2), &wsa);
}

CastChannel::~CastChannel()
{
   Disconnect();
}

bool CastChannel::Connect(const std::string& ip, int port)
{
   Disconnect();

   mSocket = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
   if (mSocket == -1)
      return false;

   sockaddr_in addr;
   addr.sin_family = AF_INET;
   addr.sin_port = htons((short)port);
   inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

   if (connect((SOCKET)mSocket, (sockaddr*)&addr, sizeof(addr)) != 0)
   {
      closesocket((SOCKET)mSocket);
      mSocket = -1;
      return false;
   }

   int opt = 1;
   setsockopt((SOCKET)mSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

   if (!TLSHandshake())
   {
      Disconnect();
      return false;
   }

   return true;
}

void CastChannel::Disconnect()
{
   if (mTlsEstablished)
   {
      DWORD type = SCHANNEL_SHUTDOWN;
      SecBuffer shutBuf;
      shutBuf.BufferType = SECBUFFER_TOKEN;
      shutBuf.pvBuffer = &type;
      shutBuf.cbBuffer = sizeof(type);

      SecBufferDesc shutDesc;
      shutDesc.ulVersion = SECBUFFER_VERSION;
      shutDesc.cBuffers = 1;
      shutDesc.pBuffers = &shutBuf;

      ApplyControlToken(&mContextHandle, &shutDesc);
   }

   mTlsEstablished = false;

   DeleteSecurityContext(&mContextHandle);
   FreeCredentialsHandle(&mCredHandle);
   memset(&mCredHandle, 0, sizeof(mCredHandle));
   memset(&mContextHandle, 0, sizeof(mContextHandle));
   memset(&mStreamSizes, 0, sizeof(mStreamSizes));

   if (mSocket != -1)
   {
      closesocket((SOCKET)mSocket);
      mSocket = -1;
   }

   mRecvBuffer.clear();
   mDecryptBuffer.clear();
}

bool CastChannel::IsConnected() const
{
   return mSocket != -1 && mTlsEstablished;
}

bool CastChannel::Send(const std::string& sourceId, const std::string& destId,
                        const std::string& namespace_, const std::string& payload)
{
   if (!IsConnected())
      return false;

   auto frame = SerializeCastMessage(sourceId, destId, namespace_, payload);
   return SendEncrypted(frame.data(), (int)frame.size());
}

bool CastChannel::SendMessage(const Message& msg)
{
   return Send(msg.sourceId, msg.destId, msg.namespace_, msg.payload);
}

bool CastChannel::PumpMessage(int timeoutMs)
{
   return ReceiveDecrypted();
}

// ---------- SChannel TLS ----------

bool CastChannel::TLSHandshake()
{
   SCHANNEL_CRED schCred = {};
   schCred.dwVersion = SCHANNEL_CRED_VERSION;
   schCred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
   schCred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;

   TimeStamp expiry;
   SECURITY_STATUS err = AcquireCredentialsHandleW(
      NULL, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, NULL, &schCred,
      NULL, NULL, &mCredHandle, &expiry);

   if (err != SEC_E_OK)
      return false;

   DWORD flags = ISC_REQ_CONFIDENTIALITY | ISC_REQ_INTEGRITY |
                 ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
                 ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION;

   SecBuffer outBuf;
   outBuf.BufferType = SECBUFFER_EMPTY;
   outBuf.cbBuffer = 0;
   outBuf.pvBuffer = NULL;

   SecBufferDesc outDesc;
   outDesc.ulVersion = SECBUFFER_VERSION;
   outDesc.cBuffers = 1;
   outDesc.pBuffers = &outBuf;

   ULONG contextAttr = 0;
   bool firstCall = true;

   while (true)
   {
      err = InitializeSecurityContextW(
         &mCredHandle,
         firstCall ? NULL : &mContextHandle,
         NULL, flags, 0, 0,
         NULL, 0,
         &mContextHandle, &outDesc, &contextAttr, &expiry);

      firstCall = false;

      if (outBuf.cbBuffer > 0 && outBuf.pvBuffer)
      {
         send((SOCKET)mSocket, (const char*)outBuf.pvBuffer, outBuf.cbBuffer, 0);
         FreeContextBuffer(outBuf.pvBuffer);
         outBuf.pvBuffer = NULL;
         outBuf.cbBuffer = 0;
      }

      if (err == SEC_E_OK)
      {
         PCCERT_CONTEXT certContext = NULL;
         QueryContextAttributesW(&mContextHandle, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certContext);
         if (certContext)
            CertFreeCertificateContext(certContext);

         QueryContextAttributesW(&mContextHandle, SECPKG_ATTR_STREAM_SIZES, &mStreamSizes);

         mTlsEstablished = true;
         return true;
      }
      else if (err == SEC_I_CONTINUE_NEEDED)
      {
         char buf[8192];
         int bytes = recv((SOCKET)mSocket, buf, sizeof(buf), 0);
         if (bytes <= 0)
            return false;

         SecBuffer inBufs[2];
         inBufs[0].BufferType = SECBUFFER_TOKEN;
         inBufs[0].pvBuffer = buf;
         inBufs[0].cbBuffer = bytes;
         inBufs[1].BufferType = SECBUFFER_EMPTY;

         SecBufferDesc inDesc;
         inDesc.ulVersion = SECBUFFER_VERSION;
         inDesc.cBuffers = 2;
         inDesc.pBuffers = inBufs;

         // Reuse outBuf for next iteration
         outBuf.BufferType = SECBUFFER_EMPTY;
         outBuf.cbBuffer = 0;
         outBuf.pvBuffer = NULL;
      }
      else
      {
         return false;
      }
   }
}

bool CastChannel::SendEncrypted(const uint8_t* data, int len)
{
   if (!mTlsEstablished)
      return false;

   if (len > mStreamSizes.cbMaximumMessage)
      return false;

   std::vector<uint8_t> buf(mStreamSizes.cbHeader + len + mStreamSizes.cbTrailer);

   SecBuffer bufs[4];
   bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
   bufs[0].cbBuffer = mStreamSizes.cbHeader;
   bufs[0].pvBuffer = buf.data();

   bufs[1].BufferType = SECBUFFER_DATA;
   bufs[1].cbBuffer = len;
   bufs[1].pvBuffer = buf.data() + mStreamSizes.cbHeader;
   memcpy(bufs[1].pvBuffer, data, len);

   bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
   bufs[2].cbBuffer = mStreamSizes.cbTrailer;
   bufs[2].pvBuffer = buf.data() + mStreamSizes.cbHeader + len;

   bufs[3].BufferType = SECBUFFER_EMPTY;

   SecBufferDesc desc;
   desc.ulVersion = SECBUFFER_VERSION;
   desc.cBuffers = 4;
   desc.pBuffers = bufs;

   SECURITY_STATUS err = EncryptMessage(&mContextHandle, 0, &desc, 0);
   if (err != SEC_E_OK)
      return false;

   int total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
   int sent = send((SOCKET)mSocket, (const char*)buf.data(), total, 0);
   return sent == total;
}

bool CastChannel::ReceiveDecrypted()
{
   if (!mTlsEstablished)
      return false;

   char tmp[8192];
   int bytes = recv((SOCKET)mSocket, tmp, sizeof(tmp), 0);
   if (bytes <= 0)
      return false;

   mDecryptBuffer.insert(mDecryptBuffer.end(), tmp, tmp + bytes);

   bool gotMessage = false;

   while (true)
   {
      SecBuffer bufs[4];
      bufs[0].BufferType = SECBUFFER_DATA;
      bufs[0].cbBuffer = (int)mDecryptBuffer.size();
      bufs[0].pvBuffer = mDecryptBuffer.data();

      bufs[1].BufferType = SECBUFFER_EMPTY;
      bufs[2].BufferType = SECBUFFER_EMPTY;
      bufs[3].BufferType = SECBUFFER_EMPTY;

      SecBufferDesc desc;
      desc.ulVersion = SECBUFFER_VERSION;
      desc.cBuffers = 4;
      desc.pBuffers = bufs;

      ULONG qop = 0;
      SECURITY_STATUS err = DecryptMessage(&mContextHandle, &desc, 0, &qop);

      if (err == SEC_E_OK)
      {
         uint8_t* plaintext = NULL;
         int plainLen = 0;
         for (int i = 0; i < 4; ++i)
         {
            if (bufs[i].BufferType == SECBUFFER_DATA)
            {
               plaintext = (uint8_t*)bufs[i].pvBuffer;
               plainLen = bufs[i].cbBuffer;
               break;
            }
         }

         uint8_t* extraData = NULL;
         int extraLen = 0;
         for (int i = 0; i < 4; ++i)
         {
            if (bufs[i].BufferType == SECBUFFER_EXTRA)
            {
               extraData = (uint8_t*)bufs[i].pvBuffer;
               extraLen = bufs[i].cbBuffer;
               break;
            }
         }

         if (plaintext && plainLen > 0)
         {
            if (plainLen >= 4)
            {
               uint32_t frameLen = ((uint32_t)plaintext[0] << 24) |
                                   ((uint32_t)plaintext[1] << 16) |
                                   ((uint32_t)plaintext[2] << 8) |
                                   plaintext[3];

               if (frameLen > 0 && frameLen <= (uint32_t)(plainLen - 4))
               {
                  Message msg;
                  if (DecodeCastMessage(plaintext + 4, frameLen, msg))
                  {
                     if (mOnMessage)
                        mOnMessage(msg);
                     gotMessage = true;
                  }
               }
            }
         }

         if (extraData && extraLen > 0)
         {
            mDecryptBuffer.clear();
            mDecryptBuffer.insert(mDecryptBuffer.end(), extraData, extraData + extraLen);
         }
         else
         {
            mDecryptBuffer.clear();
         }
      }
      else if (err == SEC_E_INCOMPLETE_MESSAGE)
      {
         break;
      }
      else
      {
         mDecryptBuffer.clear();
         break;
      }
   }

   return gotMessage;
}

// ---------- High-level Cast flow ----------

bool CastChannel::StartCast(const std::string& streamUrl, int& outAppPort)
{
   if (!Send("sender-0", "receiver-0",
             "urn:x-cast:com.google.cast.tp.connection",
             "{\"type\":\"CONNECT\"}"))
      return false;

   if (!PumpMessage(1000))
      return false;

   if (!Send("sender-0", "receiver-0",
             "urn:x-cast:com.google.cast.tp.receiver",
             "{\"type\":\"GET_STATUS\"}"))
      return false;

   for (int i = 0; i < 20; ++i)
   {
      PumpMessage(500);
   }

   if (!Send("sender-0", "receiver-0",
             "urn:x-cast:com.google.cast.tp.receiver",
             "{\"type\":\"LAUNCH\",\"appId\":\"CC1AD845\"}"))
      return false;

   for (int i = 0; i < 20; ++i)
   {
      PumpMessage(500);
   }

   if (!Send("sender-0", "web-4",
             "urn:x-cast:com.google.cast.tp.connection",
             "{\"type\":\"CONNECT\"}"))
      return false;

   PumpMessage(500);

   std::string loadMsg = "{\"type\":\"LOAD\",\"autoplay\":true,"
                          "\"media\":{"
                          "\"contentId\":\"" + streamUrl + "\","
                          "\"streamType\":\"LIVE\","
                          "\"contentType\":\"video/mp2t\""
                          "}}";

   if (!Send("sender-0", "web-4",
             "urn:x-cast:com.google.cast.tp.media",
             loadMsg))
      return false;

   PumpMessage(1000);

   outAppPort = 0;
   return true;
}
