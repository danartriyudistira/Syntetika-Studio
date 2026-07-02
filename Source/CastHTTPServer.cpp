#include "CastHTTPServer.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

CastHTTPServer::CastHTTPServer()
{
   WSADATA wsa;
   WSAStartup(MAKEWORD(2, 2), &wsa);
}

CastHTTPServer::~CastHTTPServer()
{
   Stop();
}

bool CastHTTPServer::Start(int port, const std::string& docRoot)
{
   Stop();

   mPort = port;
   mDocRoot = docRoot;

   // Remove trailing backslash
   while (!mDocRoot.empty() && (mDocRoot.back() == '\\' || mDocRoot.back() == '/'))
      mDocRoot.pop_back();

   mListenSocket = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
   if (mListenSocket == -1)
      return false;

   int opt = 1;
   setsockopt((SOCKET)mListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

   sockaddr_in addr;
   addr.sin_family = AF_INET;
   addr.sin_port = htons((short)port);
   addr.sin_addr.s_addr = INADDR_ANY;

   if (bind((SOCKET)mListenSocket, (sockaddr*)&addr, sizeof(addr)) != 0)
   {
      closesocket((SOCKET)mListenSocket);
      mListenSocket = -1;
      return false;
   }

   listen((SOCKET)mListenSocket, 5);

   mRunning = true;
   mThread = std::thread(&CastHTTPServer::ServerThread, this);

   return true;
}

void CastHTTPServer::Stop()
{
   mRunning = false;
   if (mListenSocket != -1)
   {
      closesocket((SOCKET)mListenSocket);
      mListenSocket = -1;
   }
   if (mThread.joinable())
      mThread.join();
}

void CastHTTPServer::ServerThread()
{
   fd_set readSet;
   TIMEVAL tv;
   tv.tv_sec = 1;
   tv.tv_usec = 0;

   while (mRunning)
   {
      FD_ZERO(&readSet);
      FD_SET((SOCKET)mListenSocket, &readSet);

      int ret = select(0, &readSet, NULL, NULL, &tv);
      if (ret <= 0)
         continue;

      sockaddr_in client;
      int clientLen = sizeof(client);
      SOCKET clientSock = accept((SOCKET)mListenSocket, (sockaddr*)&client, &clientLen);
      if (clientSock == INVALID_SOCKET)
         continue;

      // Set receive timeout
      int timeout = 5000;
      setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

      // Read request
      char reqBuf[8192];
      int bytes = recv(clientSock, reqBuf, sizeof(reqBuf) - 1, 0);
      if (bytes > 0)
      {
         reqBuf[bytes] = 0;
         std::string request(reqBuf);

         // Parse GET path
         std::string path = "/";
         size_t getPos = request.find("GET ");
         if (getPos != std::string::npos)
         {
            size_t pathStart = getPos + 4;
            size_t pathEnd = request.find(' ', pathStart);
            if (pathEnd != std::string::npos)
            {
               path = request.substr(pathStart, pathEnd - pathStart);
               // URL decode
               size_t qPos = path.find('?');
               if (qPos != std::string::npos)
                  path = path.substr(0, qPos);
               if (path == "/")
                  path = "/index.html";
            }
         }

         // Build file path
         std::string filePath = mDocRoot + path;
         // Replace forward slashes with backslashes
         std::replace(filePath.begin(), filePath.end(), '/', '\\');

         // Read file
         std::ifstream file(filePath, std::ios::binary);
         std::string response;
         if (file.good())
         {
            std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
            file.close();

            // Determine content type
            std::string contentType = "application/octet-stream";
            if (path.find(".m3u8") != std::string::npos)
               contentType = "application/vnd.apple.mpegurl";
            else if (path.find(".ts") != std::string::npos)
               contentType = "video/mp2t";

            std::ostringstream resp;
            resp << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "Cache-Control: no-cache\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
            std::string headers = resp.str();
            send(clientSock, headers.c_str(), (int)headers.size(), 0);
            send(clientSock, content.data(), (int)content.size(), 0);
         }
         else
         {
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(clientSock, resp.c_str(), (int)resp.size(), 0);
         }
      }

      closesocket(clientSock);
   }
}
