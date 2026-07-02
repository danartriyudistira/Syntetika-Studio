#include "SSDPDiscoverer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

// ------- DNS packet helpers for mDNS -------

static void Write16(uint8_t*& p, uint16_t v)
{
   *p++ = (uint8_t)(v >> 8);
   *p++ = (uint8_t)(v & 0xFF);
}

static uint16_t Read16(const uint8_t*& p)
{
   uint16_t v = ((uint16_t)p[0] << 8) | p[1];
   p += 2;
   return v;
}

static uint32_t Read32(const uint8_t*& p)
{
   uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
   p += 4;
   return v;
}

// Encode a domain name like "_googlecast._tcp.local" into DNS label format
static std::vector<uint8_t> EncodeDNSName(const std::string& name)
{
   std::vector<uint8_t> out;
   size_t pos = 0;
   while (pos < name.size())
   {
      size_t dot = name.find('.', pos);
      if (dot == std::string::npos)
         dot = name.size();
      size_t len = dot - pos;
      out.push_back((uint8_t)len);
      for (size_t i = 0; i < len; ++i)
         out.push_back((uint8_t)name[pos + i]);
      pos = dot + 1;
   }
   out.push_back(0); // root label
   return out;
}

// Decode a DNS name from packet, handling compression (pointer = 0xC0 + 14-bit offset)
static std::string DecodeDNSName(const uint8_t* packet, const uint8_t*& pos)
{
   std::string name;
   bool jumped = false;
   const uint8_t* saved = nullptr;

   while (true)
   {
      uint8_t len = *pos++;
      if (len == 0)
         break;
      if ((len & 0xC0) == 0xC0)
      {
         uint16_t offset = (((uint16_t)(len & 0x3F)) << 8) | *pos++;
         if (!jumped)
         {
            saved = pos; // save position after jump for later
            jumped = true;
         }
         pos = packet + offset;
         continue;
      }
      if (!name.empty())
         name += '.';
      name.append((const char*)pos, len);
      pos += len;
   }

   if (jumped && saved)
      pos = saved; // restore position for caller

   return name;
}

// Build a DNS query packet for a given name/type/class
static std::vector<uint8_t> BuildDNSQuery(const std::string& name, uint16_t type, uint16_t qclass)
{
   std::vector<uint8_t> p;
   p.resize(12);
   uint8_t* ptr = p.data();
   Write16(ptr, 0);       // ID = 0
   Write16(ptr, 0x0100);  // flags: standard query, recursion desired
   Write16(ptr, 1);       // QDCOUNT = 1
   Write16(ptr, 0);       // ANCOUNT = 0
   Write16(ptr, 0);       // NSCOUNT = 0
   Write16(ptr, 0);       // ARCOUNT = 0

   auto qname = EncodeDNSName(name);
   p.insert(p.end(), qname.begin(), qname.end());
   ptr = p.data() + p.size();
   p.resize(p.size() + 4);
   ptr = p.data() + p.size() - 4;
   Write16(ptr, type);
   Write16(ptr, qclass);

   return p;
}

// ------- SSDP -------

static std::string Trim(const std::string& s)
{
   size_t start = s.find_first_not_of(" \t\r\n");
   size_t end = s.find_last_not_of(" \t\r\n");
   if (start == std::string::npos || end == std::string::npos)
      return "";
   return s.substr(start, end - start + 1);
}

static std::string GetHeaderValue(const std::string& response, const std::string& header)
{
   std::istringstream stream(response);
   std::string line;
   while (std::getline(stream, line))
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      size_t colon = line.find(':');
      if (colon != std::string::npos)
      {
         std::string key = line.substr(0, colon);
         if (_stricmp(key.c_str(), header.c_str()) == 0)
            return Trim(line.substr(colon + 1));
      }
   }
   return "";
}

static std::string FetchFriendlyName(const std::string& location)
{
   if (location.empty())
      return "";

   std::string url = location;
   std::string host;
   std::string path = "/";
   int port = 80;

   size_t protoEnd = url.find("://");
   if (protoEnd != std::string::npos)
      url = url.substr(protoEnd + 3);

   size_t colon = url.find(':');
   size_t slash = url.find('/');
   if (colon != std::string::npos && (slash == std::string::npos || colon < slash))
   {
      host = url.substr(0, colon);
      port = atoi(url.substr(colon + 1, slash - colon - 1).c_str());
      if (slash != std::string::npos)
         path = url.substr(slash);
   }
   else if (slash != std::string::npos)
   {
      host = url.substr(0, slash);
      path = url.substr(slash);
   }
   else
   {
      host = url;
   }

   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock == INVALID_SOCKET)
      return "";

   sockaddr_in addr;
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

   if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0)
   {
      closesocket(sock);
      return "";
   }

   std::string request = "GET " + path + " HTTP/1.1\r\n"
                         "Host: " + host + ":" + std::to_string(port) + "\r\n"
                         "Connection: close\r\n\r\n";

   send(sock, request.c_str(), (int)request.size(), 0);

   int timeout = 2000;
   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

   std::string response;
   char buf[4096];
   int bytes;
   while ((bytes = recv(sock, buf, sizeof(buf) - 1, 0)) > 0)
   {
      buf[bytes] = 0;
      response += buf;
   }

   closesocket(sock);

   const std::string openTag = "<friendlyName>";
   const std::string closeTag = "</friendlyName>";
   size_t start = response.find(openTag);
   if (start == std::string::npos)
      return "";
   start += openTag.size();
   size_t end = response.find(closeTag, start);
   if (end == std::string::npos)
      return "";

   std::string name = response.substr(start, end - start);
   auto replaceAll = [&](const std::string& from, const std::string& to)
   {
      size_t pos = 0;
      while ((pos = name.find(from, pos)) != std::string::npos)
      {
         name.replace(pos, from.length(), to);
         pos += to.length();
      }
   };
   replaceAll("&amp;", "&");
   replaceAll("&lt;", "<");
   replaceAll("&gt;", ">");
   replaceAll("&quot;", "\"");
   replaceAll("&apos;", "'");

   return name;
}

static bool IEquals(const std::string& a, const std::string& b)
{
   if (a.size() != b.size())
      return false;
   for (size_t i = 0; i < a.size(); ++i)
   {
      if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]))
         return false;
   }
   return true;
}

// ------- mDNS query for Google Cast devices -------
// Queries _googlecast._tcp.local PTR, parses responses for instance names,
// then extracts hostname -> IP from SRV/A records in the same packet.

static void SendMDNSQuery(SOCKET sock, const std::string& serviceType)
{
   auto query = BuildDNSQuery(serviceType, 12 /*PTR*/, 1 /*IN*/);

   sockaddr_in dst;
   dst.sin_family = AF_INET;
   dst.sin_port = htons(5353);
   inet_pton(AF_INET, "224.0.0.251", &dst.sin_addr);

   sendto(sock, (const char*)query.data(), (int)query.size(), 0,
          (sockaddr*)&dst, sizeof(dst));
}

// Parse mDNS response and extract device info (instance name + IP)
static void ParseMDNSResponse(const uint8_t* packet, int len,
                              const sockaddr_in& sender,
                              std::vector<SSDPDevice>& devices,
                              std::set<std::string>& seenIPs)
{
   if (len < 12)
      return;

   const uint8_t* pos = packet;
   Read16(pos); // ID (skip)
   uint16_t flags = Read16(pos);
   if (!(flags & 0x8000))
      return;

   uint16_t qdcount = Read16(pos);
   uint16_t ancount = Read16(pos);
   uint16_t nscount = Read16(pos);
   uint16_t arcount = Read16(pos);

   for (uint16_t i = 0; i < qdcount; ++i)
   {
      DecodeDNSName(packet, pos);
      pos += 4;
   }

   struct InstanceInfo
   {
      std::string fullName;  // "Instance._googlecast._tcp.local"
      std::string hostname;
      int port{ 0 };
      std::string ip;
      std::string txtFN;     // friendly name from TXT fn= key
   };
   std::vector<InstanceInfo> instances;
   std::map<std::string, std::string> hostnameToIP; // hostname -> IP

   auto processRecord = [&](const uint8_t*& rpos) -> bool
   {
      std::string rname = DecodeDNSName(packet, rpos);
      uint16_t rtype = Read16(rpos);
      Read16(rpos); // class (skip, including cache-flush)
      Read32(rpos); // TTL (skip)
      uint16_t rdlength = Read16(rpos);
      const uint8_t* rdata = rpos;
      rpos += rdlength;

      switch (rtype)
      {
         case 12: // PTR
         {
            const uint8_t* ptrPos = rdata;
            std::string ptrTarget = DecodeDNSName(packet, ptrPos);
            InstanceInfo inst;
            inst.fullName = ptrTarget;
            instances.push_back(inst);
            break;
         }
         case 33: // SRV
         {
            const uint8_t* srvPos = rdata;
            Read16(srvPos); // priority
            Read16(srvPos); // weight
            uint16_t port = Read16(srvPos);
            std::string target = DecodeDNSName(packet, srvPos);
            if (!target.empty())
            {
               for (auto& inst : instances)
               {
                  if (IEquals(inst.fullName, rname))
                  {
                     inst.hostname = target;
                     inst.port = port;
                  }
               }
            }
            break;
         }
         case 1: // A
         {
            if (rdlength >= 4)
            {
               char ip[64];
               snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
                        rdata[0], rdata[1], rdata[2], rdata[3]);
               hostnameToIP[rname] = ip;
            }
            break;
         }
         case 16: // TXT
         {
            const uint8_t* txtPos = rdata;
            int remaining = rdlength;
            while (remaining > 0)
            {
               uint8_t slen = *txtPos++;
               remaining--;
               if (slen > (uint8_t)remaining)
                  break;
               std::string kv((const char*)txtPos, slen);
               txtPos += slen;
               remaining -= slen;
               // Check for fn= key
               if (kv.find("fn=") == 0 || kv.find("FN=") == 0)
               {
                  std::string val = kv.substr(3);
               for (auto& inst : instances)
               {
                  if (IEquals(inst.fullName, rname))
                     inst.txtFN = val;
               }
               }
            }
            break;
         }
      }
      return true;
   };

   // Parse all sections
   for (uint16_t i = 0; i < ancount; ++i)
      processRecord(pos);
   for (uint16_t i = 0; i < nscount; ++i)
      processRecord(pos);
   for (uint16_t i = 0; i < arcount; ++i)
      processRecord(pos);

   // Resolve IPs
   char senderIP[64];
   inet_ntop(AF_INET, &sender.sin_addr, senderIP, sizeof(senderIP));

   for (auto& inst : instances)
   {
      // Resolve IP from hostname map
      if (!inst.hostname.empty())
      {
         auto it = hostnameToIP.find(inst.hostname);
         if (it != hostnameToIP.end())
            inst.ip = it->second;
      }

      // Fallback: use sender IP (mDNS response comes from device)
      if (inst.ip.empty())
         inst.ip = senderIP;
   }

   // Add discovered devices
   for (const auto& inst : instances)
   {
      if (inst.ip.empty())
         continue;
      if (seenIPs.find(inst.ip) != seenIPs.end())
         continue;
      seenIPs.insert(inst.ip);

      size_t dot = inst.fullName.find('.');
      std::string name = (dot != std::string::npos) ? inst.fullName.substr(0, dot) : inst.fullName;
      if (!inst.txtFN.empty())
         name = inst.txtFN;

      SSDPDevice dev;
      dev.mFriendlyName = name;
      dev.mIP = inst.ip;
      dev.mServer = "Google Cast";
      devices.push_back(dev);
   }
}

// ------- Main discovery -------

void SSDPDiscoverer::AddDevice(DiscoverContext& ctx, const std::string& name,
                               const std::string& ip)
{
   if (HasIP(ctx, ip))
      return;
   SSDPDevice dev;
   dev.mFriendlyName = name;
   dev.mIP = ip;
   ctx.devices.push_back(dev);
}

bool SSDPDiscoverer::HasIP(const DiscoverContext& ctx, const std::string& ip)
{
   for (const auto& d : ctx.devices)
   {
      if (d.mIP == ip)
         return true;
   }
   return false;
}

void SSDPDiscoverer::DiscoverSSDP(DiscoverContext& ctx)
{
   SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock == INVALID_SOCKET)
      return;

   int opt = 1;
   setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

   sockaddr_in bindAddr;
   bindAddr.sin_family = AF_INET;
   bindAddr.sin_port = 0;
   bindAddr.sin_addr.s_addr = INADDR_ANY;
   bind(sock, (sockaddr*)&bindAddr, sizeof(bindAddr));

   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ctx.timeoutMs, sizeof(ctx.timeoutMs));

   const char* msearch =
      "M-SEARCH * HTTP/1.1\r\n"
      "HOST: 239.255.255.250:1900\r\n"
      "MAN: \"ssdp:discover\"\r\n"
      "ST: ssdp:all\r\n"
      "MX: 3\r\n\r\n";

   sockaddr_in multicastAddr;
   multicastAddr.sin_family = AF_INET;
   multicastAddr.sin_port = htons(1900);
   inet_pton(AF_INET, "239.255.255.250", &multicastAddr.sin_addr);
   sendto(sock, msearch, (int)strlen(msearch), 0,
          (sockaddr*)&multicastAddr, sizeof(multicastAddr));

   char buf[8192];
   sockaddr_in sender;
   int senderLen = sizeof(sender);

   std::set<std::string> seenIPs;
   for (const auto& d : ctx.devices)
      seenIPs.insert(d.mIP);

   while (true)
   {
      int bytes = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (sockaddr*)&sender, &senderLen);
      if (bytes <= 0)
         break;

      buf[bytes] = 0;
      std::string response(buf);

      if (response.find("HTTP/1.1 200 OK") == std::string::npos)
         continue;

      char senderIP[64];
      inet_ntop(AF_INET, &sender.sin_addr, senderIP, sizeof(senderIP));

      if (seenIPs.find(senderIP) != seenIPs.end())
         continue;
      seenIPs.insert(senderIP);

      std::string st = GetHeaderValue(response, "ST");
      std::string server = GetHeaderValue(response, "SERVER");
      std::string location = GetHeaderValue(response, "LOCATION");

      bool isMediaRenderer = st.find("MediaRenderer") != std::string::npos ||
                             st.find("dial") != std::string::npos ||
                             st.find("mediarenderer") != std::string::npos;
      if (!isMediaRenderer)
         continue;

      std::string friendlyName = server.empty() ? senderIP : server;
      if (!location.empty())
      {
         std::string xmlName = FetchFriendlyName(location);
         if (!xmlName.empty())
            friendlyName = xmlName;
      }

      SSDPDevice dev;
      dev.mFriendlyName = friendlyName;
      dev.mIP = senderIP;
      dev.mLocation = location;
      dev.mServer = server;
      ctx.devices.push_back(dev);
   }

   closesocket(sock);
}

void SSDPDiscoverer::DiscoverMDNS(DiscoverContext& ctx)
{
   SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock == INVALID_SOCKET)
      return;

   int opt = 1;
   setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

   sockaddr_in bindAddr;
   bindAddr.sin_family = AF_INET;
   bindAddr.sin_port = 0;
   bindAddr.sin_addr.s_addr = INADDR_ANY;
   bind(sock, (sockaddr*)&bindAddr, sizeof(bindAddr));

   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ctx.timeoutMs, sizeof(ctx.timeoutMs));

   // Query for Google Cast devices
   SendMDNSQuery(sock, "_googlecast._tcp.local");
   // Also query for Chromecast BASE (older protocol)
   SendMDNSQuery(sock, "_googlezone._tcp.local");
   // Also query generic media renderers via DNS-SD
   SendMDNSQuery(sock, "_mediarenderer._tcp.local");

   std::set<std::string> seenIPs;
   for (const auto& d : ctx.devices)
      seenIPs.insert(d.mIP);

   char buf[4096];
   sockaddr_in sender;
   int senderLen = sizeof(sender);

   while (true)
   {
      int bytes = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (sockaddr*)&sender, &senderLen);
      if (bytes <= 0)
         break;

      buf[bytes] = 0;
      ParseMDNSResponse((const uint8_t*)buf, bytes, sender, ctx.devices, seenIPs);
   }

   closesocket(sock);
}

std::vector<SSDPDevice> SSDPDiscoverer::Discover(int timeoutMs)
{
   WSADATA wsaData;
   if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
      return {};

   DiscoverContext ctx;
   ctx.timeoutMs = timeoutMs;

   DiscoverSSDP(ctx);
   DiscoverMDNS(ctx);

   WSACleanup();
   return ctx.devices;
}
