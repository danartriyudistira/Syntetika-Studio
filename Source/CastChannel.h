#pragma once

#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <schannel.h>
#include <security.h>
#endif

class CastChannel
{
public:
   struct Message
   {
      std::string sourceId;
      std::string destId;
      std::string namespace_;
      std::string payload;
   };

   CastChannel();
   ~CastChannel();

   bool Connect(const std::string& ip, int port);
   void Disconnect();
   bool IsConnected() const;

   using OnMessageCallback = std::function<void(const Message&)>;
   void SetOnMessage(OnMessageCallback cb) { mOnMessage = cb; }

   bool Send(const std::string& sourceId, const std::string& destId,
             const std::string& namespace_, const std::string& payload);
   bool SendMessage(const Message& msg);

   bool PumpMessage(int timeoutMs = 100);

   // High-level Cast flow
   bool StartCast(const std::string& streamUrl, int& outAppPort);

private:
   bool TLSHandshake();
   bool SendEncrypted(const uint8_t* data, int len);
   bool ReceiveDecrypted();
   std::vector<uint8_t> SerializeCastMessage(const std::string& sourceId,
                                             const std::string& destId,
                                             const std::string& namespace_,
                                             const std::string& payload);
   bool SendFrame(const std::vector<uint8_t>& msg);
   bool ReadFrame(std::vector<uint8_t>& msg, int timeoutMs);

   // Socket
   intptr_t mSocket{ -1 };

   // SChannel TLS
#ifdef _WIN32
   CredHandle mCredHandle{};
   CtxtHandle mContextHandle{};
   SecPkgContext_StreamSizes mStreamSizes{};
#else
   void* mCredHandle{ nullptr };
   void* mContextHandle{ nullptr };
   int mStreamSizes{ 0 };
#endif
   bool mTlsEstablished{ false };
   std::vector<uint8_t> mRecvBuffer;
   std::vector<uint8_t> mDecryptBuffer;

   OnMessageCallback mOnMessage;
};
