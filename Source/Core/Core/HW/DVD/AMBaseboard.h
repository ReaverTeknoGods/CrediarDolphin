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

namespace DVDInterface
{
class DVDInterfaceState;
struct Data;
}  // namespace DVDInterface

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

namespace AMBaseboard
{
  enum class AMBBCommand : u16
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
    SetUnknownFlag = 0x411,
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
  };

  void  Init(void);
  void  FirmwareMap(bool on);
  u8*   InitDIMM(void);
  void  InitKeys(u32 KeyA, u32 KeyB, u32 KeyC);
  u32   ExecuteCommand(std::array<u32, 3>& DICMDBUF, u32 Address, u32 Length);
	u32		GetGameType( void );
  u32   GetMediaType( void );
	void	Shutdown( void );
};

