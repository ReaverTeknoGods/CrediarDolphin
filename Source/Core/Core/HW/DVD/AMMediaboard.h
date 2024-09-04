// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace Core
{
class System;
}

namespace File
{
class IOFile;
}

enum GameType
{
  FZeroAX = 1,
  FZeroAXMonster,
  MarioKartGP,
  MarioKartGP2,
  VirtuaStriker3,
  VirtuaStriker4,
  GekitouProYakyuu,
  KeyOfAvalon
};
enum MediaType
{
  GDROM = 1,
  NAND,
};

#define SocketCheck(x) (x <= 0x3F ? x : 0)

namespace AMMediaboard
{
enum class AMMBCommand : u16
{
  Unknown_000 = 0x000,
  GetDIMMSize = 0x001,

  GetMediaBoardStatus = 0x100,
  GetSegaBootVersion = 0x101,
  GetSystemFlags = 0x102,
  GetMediaBoardSerial = 0x103,
  Unknown_104 = 0x104,

  TestHardware = 0x301,

  // Network use by Mario Kart GPs
  Accept = 0x401,
  Bind = 0x402,
  Closesocket = 0x403,
  Connect = 0x404,
  GetIPbyDNS = 0x405,
  InetAddr = 0x406,
  Ioctl = 0x407,  // Unused
  Listen = 0x408,
  Recv = 0x409,
  Send = 0x40A,
  Socket = 0x40B,
  Select = 0x40C,
  Shutdown = 0x40D,  // Unused
  SetSockOpt = 0x40E,
  GetSockOpt = 0x40F,  // Unused
  SetTimeOuts = 0x410,
  GetLastError = 0x411,
  RouteAdd = 0x412,     // Unused
  RouteDelete = 0x413,  // Unused
  GetParambyDHCPExec = 0x414,
  ModifyMyIPaddr = 0x415,
  Recvfrom = 0x416,       // Unused
  Sendto = 0x417,         // Unused
  RecvDimmImage = 0x418,  // Unused
  SendDimmImage = 0x419,  // Unused

  // Network used by F-Zero AX
  InitLink = 0x601,
  SetupLink = 0x606,
  SearchDevices = 0x607,
  Unknown_614,

  // NETDIMM Commands
  Unknown_001 = 0x001,
  GetNetworkFirmVersion = 0x101,
  Unknown_103 = 0x103,
};

enum MediaBoardAddress : u32
{
  NetworkCommandAddress = 0x1F800200,
  NetworkCommandAddress2 = 0x89040200,

  NetworkBufferAddress1 = 0x1FA00000,
  NetworkBufferAddress2 = 0x1FD00000,
  NetworkBufferAddress3 = 0x89100000,
  NetworkBufferAddress4 = 0x89180000,
};

// Mario Kart GP2 has a complete list of them
// but in japanese
// They somewhat match WSA errors codes
enum SocketStatusCodes
{
  SSC_E_4 = -4,  // Failure (abnormal argument)
  SSC_E_3 = -3,  // Success (unsupported command)
  SSC_E_2 = -2,  // Failure (failed to send, abnormal argument, or communication condition violation)
  SSC_E_1 = -1,  // Failure (error termination)

  SSC_EINTR = 4,    // An interrupt occurred before data reception was completed
  SSC_EBADF = 9,    // Invalid descriptor
  SSC_E_11 = 11,    // Send operation was blocked on a non-blocking mode socket
  SSC_EACCES = 13,  // The socket does not support broadcast addresses, but the destination address
                    // is a broadcast address
  SSC_EFAULT = 14,  // The name argument specifies a location other than an address used by the process.
  SSC_E_23 = 23,     // System file table is full.
  SSC_AEMFILE = 24,  // Process descriptor table is full.
  SSC_EMSGSIZE = 36,  // Socket tried to send message without splitting, but message size is too large.
  SSC_EAFNOSUPPORT = 47,   // Address prohibited for use on this socket.
  SSC_EADDRINUSE = 48,     // Address already in use.
  SSC_EADDRNOTAVAIL = 49,  // Prohibited address.
  SSC_E_50 = 50,           // Non-socket descriptor.
  SSC_ENETUNREACH = 51,    // Cannot access specified network.
  SSC_ENOBUFS = 55,        // Insufficient buffer
  SSC_EISCONN = 56,        // Already connected socket
  SSC_ENOTCONN = 57,       // No connection for connection-type socket
  SSC_ETIMEDOUT = 60,      // Timeout
  SSC_ECONNREFUSED = 61,   // Connection request forcibly rejected
  SSC_EHOSTUNREACH = 65,   // Remote host cannot be reached
  SSC_EHOSTDOWN = 67,      // Remote host is down
  SSC_EWOULDBLOCK = 68,    // Socket is in non-blocking mode and connection has not been completed
  SSC_E_69 = 69,  // Socket is in non-blocking mode and a previously issued Connect command has not
                  // been completed
  SSC_SUCCESS = 70,
};

void Init(void);
void FirmwareMap(bool on);
u8* InitDIMM(void);
void InitKeys(u32 KeyA, u32 KeyB, u32 KeyC);
u32 ExecuteCommand(std::array<u32, 3>& DICMDBUF, u32 Address, u32 Length);
u32 GetGameType(void);
u32 GetMediaType(void);
void Shutdown(void);
};  // namespace AMMediaboard
