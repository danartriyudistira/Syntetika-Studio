#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

class CastHTTPServer
{
public:
   CastHTTPServer();
   ~CastHTTPServer();

   bool Start(int port, const std::string& docRoot);
   void Stop();
   bool IsRunning() const { return mRunning; }
   int GetPort() const { return mPort; }

private:
   void ServerThread();

   std::thread mThread;
   std::atomic<bool> mRunning{ false };
   int mPort{ 0 };
   intptr_t mListenSocket{ -1 };
   std::string mDocRoot;
};
