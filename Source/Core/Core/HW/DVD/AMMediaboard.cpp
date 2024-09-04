// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma warning(disable : 4189)

#include "Core/HW/EXI/EXI_DeviceAMBaseboard.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigLoaders/NetPlayConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/DVD/AMMediaboard.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/DVD/DVDThread.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/WiiRoot.h"
#include "DiscIO/DirectoryBlob.h"
#include "DiscIO/Enums.h"
#include "DiscIO/VolumeDisc.h"

#if defined(__linux__) or defined(__APPLE__) or defined(__FreeBSD__) or defined(__NetBSD__) or     \
    defined(__HAIKU__)

#include <unistd.h>

#define closesocket close
#define ioctlsocket ioctl

#define WSAEWOULDBLOCK 10035L
#define SOCKET_ERROR (-1)

typedef int SOCKET;

int WSAGetLastError(void)
{
  switch (errno)
  {
  case EWOULDBLOCK:
    return WSAEWOULDBLOCK;
  default:
    break;
  }

  return errno;
}

#endif

namespace AMMediaboard
{

static u32 s_firmwaremap = 0;
static u32 s_segaboot = 0;
static u32 s_namco_cam = 0;
static u32 s_timeouts[3] = {20000, 20000, 20000};
static u32 s_last_error  = SSC_SUCCESS;

static u32 s_GCAM_key_a = 0;
static u32 s_GCAM_key_b = 0;
static u32 s_GCAM_key_c = 0;

static File::IOFile* s_netcfg = nullptr;
static File::IOFile* s_netctrl = nullptr;
static File::IOFile* s_extra = nullptr;
static File::IOFile* s_backup = nullptr;
static File::IOFile* s_dimm = nullptr;

static u8* s_dimm_disc = nullptr;

static u8 s_firmware[2 * 1024 * 1024];
static u8 s_media_buffer[0x300];
static u8 s_network_command_buffer[0x4FFE00];
static u8 s_network_buffer[128 * 1024];

// Sockets FDs are required to go from 0 to 63
// Games use the FD as indexes so we have to workaround it.

static SOCKET s_sockets[64];

SOCKET socket_(int af, int type, int protocol)
{
  for (u32 i = 1; i < 64; ++i)
  {
    if (s_sockets[i] == SOCKET_ERROR)
    {
      s_sockets[i] = socket(af, type, protocol);
      return i;
    }
  }

  // Out of sockets
  return SOCKET_ERROR;
}

SOCKET accept_(int fd, struct sockaddr* addr, int* len)
{
  for (u32 i = 1; i < 64; ++i)
  {
    if (s_sockets[i] == SOCKET_ERROR)
    {
      s_sockets[i] = accept(fd, addr, (socklen_t*)len);
      if (s_sockets[i] == SOCKET_ERROR)
        return SOCKET_ERROR;
      return i;
    }
  }

  // Out of sockets
  return SOCKET_ERROR;
}

static inline void PrintMBBuffer(u32 address, u32 length)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  for (u32 i = 0; i < length; i += 0x10)
  {
    INFO_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}", memory.Read_U32(address + i),
                 memory.Read_U32(address + i + 4), memory.Read_U32(address + i + 8),
                 memory.Read_U32(address + i + 12));
  }
}

void FirmwareMap(bool on)
{
  if (on)
    s_firmwaremap = 1;
  else
    s_firmwaremap = 0;
}

void InitKeys(u32 key_a, u32 key_b, u32 key_c)
{
  s_GCAM_key_a = key_a;
  s_GCAM_key_b = key_b;
  s_GCAM_key_c = key_c;
}

void Init(void)
{
  memset(s_media_buffer, 0, sizeof(s_media_buffer));
  memset(s_network_buffer, 0, sizeof(s_network_buffer));
  memset(s_network_command_buffer, 0, sizeof(s_network_command_buffer));
  memset(s_firmware, -1, sizeof(s_firmware));
  memset(s_sockets, SOCKET_ERROR, sizeof(s_sockets));

  s_segaboot = 0;
  s_firmwaremap = 0;

  s_last_error = SSC_SUCCESS;

  s_GCAM_key_a = 0;
  s_GCAM_key_b = 0;
  s_GCAM_key_c = 0;

  if (File::Exists(File::GetUserPath(D_TRIUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_TRIUSER_IDX));
  }

  std::string netcfg_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetcfg.bin");
  if (File::Exists(netcfg_Filename))
  {
    s_netcfg = new File::IOFile(netcfg_Filename, "rb+");
  }
  else
  {
    s_netcfg = new File::IOFile(netcfg_Filename, "wb+");
  }
  if (!s_netcfg)
  {
    PanicAlertFmt("Failed to open/create:{0}", netcfg_Filename);
  }

  std::string netctrl_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetctrl.bin");
  if (File::Exists(netctrl_Filename))
  {
    s_netctrl = new File::IOFile(netctrl_Filename, "rb+");
  }
  else
  {
    s_netctrl = new File::IOFile(netctrl_Filename, "wb+");
  }

  std::string extra_Filename(File::GetUserPath(D_TRIUSER_IDX) + "triextra.bin");
  if (File::Exists(extra_Filename))
  {
    s_extra = new File::IOFile(extra_Filename, "rb+");
  }
  else
  {
    s_extra = new File::IOFile(extra_Filename, "wb+");
  }

  std::string dimm_Filename(File::GetUserPath(D_TRIUSER_IDX) + "tridimm_" +
                            SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(dimm_Filename))
  {
    s_dimm = new File::IOFile(dimm_Filename, "rb+");
  }
  else
  {
    s_dimm = new File::IOFile(dimm_Filename, "wb+");
  }

  std::string backup_Filename(File::GetUserPath(D_TRIUSER_IDX) + "backup_" +
                              SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(backup_Filename))
  {
    s_backup = new File::IOFile(backup_Filename, "rb+");
  }
  else
  {
    s_backup = new File::IOFile(backup_Filename, "wb+");
  }

  // This is the s_firmware for the Triforce
  std::string sega_boot_Filename(File::GetUserPath(D_TRIUSER_IDX) + "segaboot.gcm");
  if (File::Exists(sega_boot_Filename))
  {
    File::IOFile* sega_boot = new File::IOFile(sega_boot_Filename, "rb+");
    if (sega_boot)
    {
      u64 length = sega_boot->GetSize();
      if (length >= sizeof(s_firmware))
      {
        length = sizeof(s_firmware);
      }
      sega_boot->ReadBytes(s_firmware, length);
      sega_boot->Close();
    }
  }
  else
  {
    PanicAlertFmt("Failed to open segaboot.gcm, which is required for test menus.");
  }
}

u8* InitDIMM(void)
{
  if (!s_dimm_disc)
  {
    s_dimm_disc = new u8[512 * 1024 * 1024];
  }
  s_firmwaremap = 0;
  return s_dimm_disc;
}

s32 NetDIMMAccept(int fd, struct sockaddr* addr, int* len)
{
  int ret = 0;
  int err = 0;

  u_long val = 1;
  ioctlsocket(fd, FIONBIO, &val);

  ret = accept_(fd, addr, (socklen_t*)len);
  err = WSAGetLastError();

  val = 0;
  ioctlsocket(fd, FIONBIO, &val);

  if (ret == SOCKET_ERROR)
  {
    if (err == WSAEWOULDBLOCK)
    {
      s_last_error = SSC_EWOULDBLOCK;

      fd_set readfds, errfds;

      timeval timeout;
      timeout.tv_sec = 0;
      timeout.tv_usec = 200;

      FD_ZERO(&readfds);
      FD_ZERO(&errfds);

      FD_SET(fd, &readfds);
      FD_SET(fd, &errfds);

      ret = select(fd, &readfds, nullptr, &errfds, &timeout);
      if (ret < 0)
      {
        err = WSAGetLastError();
      }
      else
      {
        if (FD_ISSET(fd, &readfds))
        {
          s_last_error = SSC_SUCCESS;
          ret = 0;
        }
        else
        {
          ret = SOCKET_ERROR;
        }
      }
    }
  }
  else
  {
    s_last_error = SSC_SUCCESS;
  }

  return ret;
}

s32 NetDIMMConnect(int fd, struct sockaddr_in* addr, int len)
{
  // CyCraft Connect IP, change to localhost
  if (addr->sin_addr.s_addr == inet_addr("192.168.11.111"))
  {
    addr->sin_addr.s_addr = inet_addr("127.0.0.1");
  }

  // NAMCO Camera ( IPs are: 192.168.29.104-108 )
  if ((addr->sin_addr.s_addr & 0xFFFFFF00) == 0xC0A81D00)
  {
    addr->sin_addr.s_addr = inet_addr("127.0.0.1");
    addr->sin_family = htons(AF_INET);  // fix family?
    s_namco_cam = fd;
  }

  // Key of Avalon Client
  if (addr->sin_addr.s_addr == inet_addr("192.168.13.1"))
  {
    addr->sin_addr.s_addr = inet_addr("127.0.0.1");
  }

  addr->sin_family = Common::swap16(addr->sin_family);
  //*(u32*)(&addr.sin_addr) = Common::swap32(*(u32*)(&addr.sin_addr));

  u_long val = 1;
  ioctlsocket(fd, FIONBIO, &val);

  int ret = connect(fd, (const sockaddr*)addr, len);
  int err = WSAGetLastError();

  val = 0;
  ioctlsocket(fd, FIONBIO, &val);

  if (ret == SOCKET_ERROR)
  {
    if (err == WSAEWOULDBLOCK)
    {
      s_last_error = SSC_EWOULDBLOCK;

      fd_set writefds, errfds;

      timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      FD_ZERO(&writefds);
      FD_ZERO(&errfds);

      FD_SET(fd, &writefds);
      FD_SET(fd, &errfds);

      ret = select(fd, nullptr, &writefds, &errfds, &timeout);
      if (ret < 0)
      {
        err = WSAGetLastError();
      }
      else
      {
        if (FD_ISSET(fd, &writefds))
        {
          s_last_error = SSC_SUCCESS;
          ret = 0;
        }
        else
        {
          ret = SOCKET_ERROR;
        }
      }
    }
  }
  else
  {
    s_last_error = SSC_SUCCESS;
  }
  return ret;
}

u32 ExecuteCommand(std::array<u32, 3>& DICMDBUF, u32 address, u32 length)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  auto& ppc_state = system.GetPPCState();
  auto& jit_interface = system.GetJitInterface();

  /*
    The triforce IPL sends these commands first
    01010000 00000101 00000000
    01010000 00000000 0000ffff
  */
  if (s_GCAM_key_a == 0)
  {
    /*
      Since it is currently unknown how the seed is created
      we have to patch out the crypto.
    */
    if (memory.Read_U32(0x8131ecf4))
    {
      memory.Write_U32(0, 0x8131ecf4);
      memory.Write_U32(0, 0x8131ecf8);
      memory.Write_U32(0, 0x8131ecfC);
      memory.Write_U32(0, 0x8131ebe0);
      memory.Write_U32(0, 0x8131ed6c);
      memory.Write_U32(0, 0x8131ed70);
      memory.Write_U32(0, 0x8131ed74);

      memory.Write_U32(0x4E800020, 0x813025C8);
      memory.Write_U32(0x4E800020, 0x81302674);

      ppc_state.iCache.Invalidate(memory, jit_interface, 0x813025C8);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x81302674);

      HLE::Patch(system, 0x813048B8, "OSReport");
      HLE::Patch(system, 0x8130095C, "OSReport");  // Apploader
    }
  }

  DICMDBUF[0] ^= s_GCAM_key_a;
  DICMDBUF[1] ^= s_GCAM_key_b;
  // length ^= s_GCAM_key_c; // DMA length is always plain

  u32 seed = DICMDBUF[0] >> 16;

  s_GCAM_key_a *= seed;
  s_GCAM_key_b *= seed;
  s_GCAM_key_c *= seed;

  DICMDBUF[0] <<= 24;
  DICMDBUF[1] <<= 2;

  // SegaBoot adds bits for some reason to offset/length
  // also adds 0x20 to offset
  if (DICMDBUF[1] == 0x00100440)
  {
    s_segaboot = 1;
  }

  u32 command = DICMDBUF[0];
  u32 offset = DICMDBUF[1];

  INFO_LOG_FMT(DVDINTERFACE,
               "GCAM: {:08x} {:08x} DMA=addr:{:08x},len:{:08x} Keys: {:08x} {:08x} {:08x}", command,
               offset, address, length, s_GCAM_key_a, s_GCAM_key_b, s_GCAM_key_c);

  // Test mode
  if (offset == 0x0002440)
  {
    // Set by OSResetSystem
    if (memory.Read_U32(0x811FFF00) == 1)
    {
      // Don't map s_firmware while in SegaBoot
      if (memory.Read_U32(0x8006BF70) != 0x0A536567)
      {
        s_firmwaremap = 1;
      }
    }
  }

  switch (command >> 24)
  {
  // Inquiry
  case 0x12:
    if (s_firmwaremap == 1)
    {
      s_firmwaremap = 0;
      s_segaboot = 0;
    }

    // Returned value is used to set the protocol version.
    switch (GetGameType())
    {
    // Version 2
    case KeyOfAvalon:
    case MarioKartGP:
    case MarioKartGP2:
      return 0x29484100;

    // Version 1
    default:
      return 0x21484100;
    }
    break;
  // Read
  case 0xA8:
    if ((offset & 0x8FFF0000) == 0x80000000)
    {
      switch (offset)
      {
      // Media board status (1)
      case 0x80000000:
        memory.Write_U16(Common::swap16(0x0100), address);
        break;
      // Media board status (2)
      case 0x80000020:
        memset(memory.GetPointer(address), 0, length);
        break;
      // Media board status (3)
      case 0x80000040:
        memset(memory.GetPointer(address), 0xFF, length);
        // DIMM size (512MB)
        memory.Write_U32(Common::swap32(0x20000000), address);
        // GCAM signature
        memory.Write_U32(0x4743414D, address + 4);
        break;
      // ?
      case 0x80000100:
        memory.Write_U32(Common::swap32((u32)0x001F1F1F), address);
        break;
      // Firmware status (1)
      case 0x80000120:
        memory.Write_U32(Common::swap32((u32)0x01FA), address);
        break;
      // Firmware status (2)
      case 0x80000140:
        memory.Write_U32(Common::swap32((u32)1), address);
        break;
      case 0x80000160:
        memory.Write_U32(0x00001E00, address);
        break;
      case 0x80000180:
        memory.Write_U32(0, address);
        break;
      case 0x800001A0:
        memory.Write_U32(0xFFFFFFFF, address);
        break;
      default:
        PrintMBBuffer(address, length);
        PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", offset);
        break;
      }
      return 0;
    }

    // Network configuration
    if ((offset == 0x00000000) && (length == 0x80))
    {
      s_netcfg->Seek(0, File::SeekOrigin::Begin);
      s_netcfg->ReadBytes(memory.GetPointer(address), length);
      return 0;
    }

    // Extra Settings
    // media crc check on/off
    if ((offset == 0x1FFEFFE0) && (length == 0x20))
    {
      s_extra->Seek(0, File::SeekOrigin::Begin);
      s_extra->ReadBytes(memory.GetPointer(address), length);
      return 0;
    }

    // DIMM memory (8MB)
    if ((offset >= 0x1F000000) && (offset <= 0x1F800000))
    {
      u32 dimmoffset = offset - 0x1F000000;
      s_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
      s_dimm->ReadBytes(memory.GetPointer(address), length);
      return 0;
    }

    // DIMM command (V1)
    if ((offset >= 0x1F900000) && (offset <= 0x1F90003F))
    {
      u32 dimmoffset = offset - 0x1F900000;
      memcpy(memory.GetPointer(address), s_media_buffer + dimmoffset, length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (1) ({:08x},{})", offset,
                   length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // Network command
    if ((offset >= NetworkCommandAddress) && (offset <= 0x1FCFFFFF))
    {
      u32 dimmoffset = offset - NetworkCommandAddress;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK COMMAND BUFFER ({:08x},{})", offset, length);
      memcpy(memory.GetPointer(address), s_network_command_buffer + dimmoffset, length);
      return 0;
    }

    // Network command
    if ((offset >= NetworkCommandAddress2) && (offset <= 0x890601FF))
    {
      u32 dimmoffset = offset - NetworkCommandAddress2;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK COMMAND BUFFER (2) ({:08x},{})", offset,
                   length);
      memcpy(memory.GetPointer(address), s_network_command_buffer + dimmoffset, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x1FA00000) && (offset <= 0x1FA0FFFF))
    {
      u32 dimmoffset = offset - 0x1FA00000;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (1) ({:08x},{})", offset, length);
      memcpy(memory.GetPointer(address), s_network_buffer + dimmoffset, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x1FD00000) && (offset <= 0x1FD0FFFF))
    {
      u32 dimmoffset = offset - 0x1FD00000;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (2) ({:08x},{})", offset, length);
      memcpy(memory.GetPointer(address), s_network_buffer + dimmoffset, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x89100000) && (offset <= 0x8910FFFF))
    {
      u32 dimmoffset = offset - 0x89100000;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (3) ({:08x},{})", offset, length);
      memcpy(memory.GetPointer(address), s_network_buffer + dimmoffset, length);
      return 0;
    }

    // NETDIMM command (V2)
    if ((offset >= 0x84000000) && (offset <= 0x8400005F))
    {
      u32 dimmoffset = offset - 0x84000000;
      memcpy(memory.GetPointer(address), s_media_buffer + dimmoffset, length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (2) ({:08x},{})", offset,
                   length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // NETDIMM execute command (V2)
    if (offset == 0x88000000)
    {
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: EXECUTE MEDIA BOARD COMMAND");

      memcpy(s_media_buffer, s_media_buffer + 0x200, 0x20);
      memset(s_media_buffer + 0x200, 0, 0x20);
      s_media_buffer[0x204] = 1;

      // Recast for easier access
      u32* media_buffer_32 = (u32*)(s_media_buffer);
      u16* media_buffer_16 = (u16*)(s_media_buffer);

      switch (AMMBCommand(*(u16*)(s_media_buffer + 2)))
      {
      case AMMBCommand::Unknown_001:
        media_buffer_32[1] = 1;
        break;
      case AMMBCommand::GetNetworkFirmVersion:
        media_buffer_32[1] = Common::swap16(0x1305);  // Version: 13.05
        s_media_buffer[6] = 1;                        // Type: VxWorks
        break;
      case AMMBCommand::GetSystemFlags:
        s_media_buffer[4] = 1;
        s_media_buffer[6] = 2;  // 2: NAND/MASK BOARD(NAND)
        s_media_buffer[7] = 1;
        break;
      // Empty reply
      case AMMBCommand::Unknown_103:
        break;
      // Network Commands
      case AMMBCommand::Accept:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        int ret = -1;

        // Handle optional paramters
        if (media_buffer_32[3] == 0 || media_buffer_32[4] == 0)
        {
          ret = NetDIMMAccept(fd, nullptr, nullptr);
        }
        else
        {
          u32 addr_off = media_buffer_32[3] - NetworkCommandAddress2;
          u32 len_off = media_buffer_32[4] - NetworkCommandAddress2;

          struct sockaddr* addr = (struct sockaddr*)(s_network_command_buffer + addr_off);
          int* len = (int*)(s_network_command_buffer + len_off);

          ret = NetDIMMAccept(fd, addr, len);
        }

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: accept( {}({}) ):{}\n", fd, media_buffer_32[2], ret);
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::Bind:
      {
        struct sockaddr_in addr;

        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 off = media_buffer_32[3] - NetworkCommandAddress2;
        u32 len = media_buffer_32[4];

        memcpy((void*)&addr, s_network_command_buffer + off, sizeof(struct sockaddr_in));

        addr.sin_family = Common::swap16(addr.sin_family);
        *(u32*)(&addr.sin_addr) = Common::swap32(*(u32*)(&addr.sin_addr));

        /*
          Triforce games usually use hardcoded IPs
          This is replaced to listen to the ANY address instead
        */
        addr.sin_addr.s_addr = INADDR_ANY;

        int ret = bind(fd, (const sockaddr*)&addr, len);
        int err = WSAGetLastError();

        // if (ret < 0 )
        //   PanicAlertFmt("Socket Bind Failed with{0}", err);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: bind( {}, ({},{:08x}:{}), {} ):{} ({})\n", fd,
                       addr.sin_family, addr.sin_addr.s_addr, Common::swap16(addr.sin_port), len,
                       ret, err);

        media_buffer_32[1] = ret;
        s_last_error = SSC_SUCCESS;
      }
      break;
      case AMMBCommand::Closesocket:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];

        int ret = closesocket(fd);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: closesocket( {}({}) ):{}\n", fd, media_buffer_32[2],
                       ret);

        s_sockets[media_buffer_32[2]] = SOCKET_ERROR;

        media_buffer_32[1] = ret;
        s_last_error = SSC_SUCCESS;
      }
      break;
      case AMMBCommand::Connect:
      {
        struct sockaddr_in addr;

        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 off = media_buffer_32[3] - NetworkCommandAddress2;
        u32 len = media_buffer_32[4];

        int ret = 0;
        int err = 0;

        memcpy((void*)&addr, s_network_command_buffer + off, sizeof(struct sockaddr_in));

        ret = NetDIMMConnect(fd, &addr, len);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: connect( {}, ({},{}:{}), {} ):{} ({})\n", fd,
                       addr.sin_family, inet_ntoa(addr.sin_addr), Common::swap16(addr.sin_port),
                       len, ret, err);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::Listen:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 backlog = media_buffer_32[3];

        int ret = listen(fd, backlog);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: listen( {}, {} ):{:d}\n", fd, backlog, ret);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::Recv:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 off = media_buffer_32[3];
        u32 len = media_buffer_32[4];

        if (len >= sizeof(s_network_buffer))
        {
          len = sizeof(s_network_buffer);
        }

        int ret = 0;
        char* buffer = (char*)(s_network_buffer + off);

        if (off >= NetworkBufferAddress4 && off < NetworkBufferAddress4 + sizeof(s_network_buffer))
        {
          buffer = (char*)(s_network_buffer + off - NetworkBufferAddress4);
        }
        else
        {
          PanicAlertFmt("RECV: Buffer overrun:{0} {1} ", off, len);
        }

        int err = 0;

        ret = recv(fd, buffer, len, 0);
        err = WSAGetLastError();

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: recv( {}, 0x{:08x}, {} ):{} {}\n", fd, off, len, ret,
                       err);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::Send:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 off = media_buffer_32[3];
        u32 len = media_buffer_32[4];

        int ret = 0;

        if (off >= NetworkBufferAddress3 && off < NetworkBufferAddress3 + sizeof(s_network_buffer))
        {
          off -= NetworkBufferAddress3;
        }
        else
        {
          ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: send(error) unhandled destination:{:08x}\n", off);
        }

        ret = send(fd, (char*)(s_network_buffer + off), len, 0);
        int err = WSAGetLastError();

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: send( {}({}), 0x{:08x}, {} ): {} {}\n", fd,
                       media_buffer_32[2], off, len, ret, err);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::Socket:
      {
        // Protocol is not sent
        u32 af = media_buffer_32[2];
        u32 type = media_buffer_32[3];

        SOCKET fd = socket_(af, type, IPPROTO_TCP);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: socket( {}, {}, 6 ):{}\n", af, type, fd);

        s_media_buffer[1] = 0;
        media_buffer_32[1] = fd;
      }
      break;
      case AMMBCommand::Select:
      {
        u32 nfds = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 ROffset = media_buffer_32[3] - NetworkCommandAddress2;
        u32 WOffset = media_buffer_32[4] - NetworkCommandAddress2;
        u32 EOffset = media_buffer_32[5] - NetworkCommandAddress2;
        u32 TOffset = media_buffer_32[6] - NetworkCommandAddress2;

        /*
          BUG: NAMCAM is hardcoded to call this with socket ID 0x100 which might be some magic
          thing? Winsocks expects a valid socket so we take the socket from the connect.
        */
        if (AMMediaboard::GetGameType() == MarioKartGP2)
        {
          if (nfds == 256)
          {
            nfds = s_namco_cam;
          }
        }

        fd_set* readfds = nullptr;
        fd_set* writefds = nullptr;

        // Either of these can be zero
        if (media_buffer_32[3])
        {
          readfds = (fd_set*)(s_network_command_buffer + ROffset);
          FD_ZERO(readfds);
          FD_SET(nfds, readfds);
        }

        if (media_buffer_32[4])
        {
          writefds = (fd_set*)(s_network_command_buffer + WOffset);
          FD_ZERO(writefds);
          FD_SET(nfds, writefds);
        }

        timeval timeout;
        if (media_buffer_32[6])
        {
          memcpy(&timeout, (timeval*)(s_network_command_buffer + TOffset), sizeof(timeval));
        }
        else
        {
          timeout.tv_sec = s_timeouts[0] / 1000;
          timeout.tv_usec = 0;
        }

        int ret = select(nfds, readfds, writefds, nullptr, &timeout);

        int err = WSAGetLastError();

        NOTICE_LOG_FMT(DVDINTERFACE,
                       "GC-AM: select( {}({}), 0x{:08x} 0x{:08x} 0x{:08x} 0x{:08x} ):{} {} \n",
                       nfds, media_buffer_32[2], media_buffer_32[3], media_buffer_32[4],
                       media_buffer_32[5], media_buffer_32[6], ret, err);
        // hexdump( NetworkCMDBuffer, 0x40 );

        s_media_buffer[1] = 0;
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::SetSockOpt:
      {
        SOCKET fd = (SOCKET)(s_sockets[SocketCheck(media_buffer_32[2])]);
        int level = (int)(media_buffer_32[3]);
        int optname = (int)(media_buffer_32[4]);
        const char* optval =
            (char*)(s_network_command_buffer + media_buffer_32[5] - NetworkCommandAddress2);
        int optlen = (int)(media_buffer_32[6]);

        int ret = setsockopt(fd, level, optname, optval, optlen);

        int err = WSAGetLastError();

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: setsockopt( {:d}, {:04x}, {}, {:p}, {} ):{:d} ({})\n",
                       fd, level, optname, optval, optlen, ret, err);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::SetTimeOuts:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];
        u32 timeoutA = media_buffer_32[3];
        u32 timeoutB = media_buffer_32[4];
        u32 timeoutC = media_buffer_32[5];

        s_timeouts[0] = timeoutA;
        s_timeouts[1] = timeoutB;
        s_timeouts[2] = timeoutC;

        int ret = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeoutB, sizeof(int));
        if (ret < 0)
        {
          ret = WSAGetLastError();
        }
        else
        {
          ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeoutC, sizeof(int));
          if (ret < 0)
            ret = WSAGetLastError();
        }

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: SetTimeOuts( {}, {}, {}, {} ):{}\n", fd, timeoutA,
                       timeoutB, timeoutC, ret);

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = ret;
      }
      break;
      case AMMBCommand::GetParambyDHCPExec:
      {
        u32 value = media_buffer_32[2];

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: GetParambyDHCPExec({})\n", value);

        s_media_buffer[1] = 0;
        media_buffer_32[1] = 0;
      }
      break;
      case AMMBCommand::ModifyMyIPaddr:
      {
        u32 NetBufferOffset = *(u32*)(s_media_buffer + 8) - NetworkCommandAddress2;

        char* IP = (char*)(s_network_command_buffer + NetBufferOffset);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: modifyMyIPaddr({})\n", IP);
      }
      break;
      case AMMBCommand::GetLastError:
      {
        u32 fd = s_sockets[SocketCheck(media_buffer_32[2])];

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: GetLastError( {}({}) )\n", fd, media_buffer_32[2] );

        s_media_buffer[1] = s_media_buffer[8];
        media_buffer_32[1] = s_last_error;
      }
      break;
      case AMMBCommand::InitLink:
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: InitLink");
        break;
      default:
        ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: Command:{:03X}", *(u16*)(s_media_buffer + 2));
        ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: Command Unhandled!");
        break;
      }

      s_media_buffer[3] |= 0x80;  // Command complete flag

      memset(memory.GetPointer(address), 0, length);

      ExpansionInterface::GenerateInterrupt(0x10);
      return 0;
    }

    // NETDIMM command (V2)
    if ((offset >= 0x89000000) && (offset <= 0x89000200))
    {
      u32 dimmoffset = offset - 0x89000000;
      memcpy(memory.GetPointer(address), s_media_buffer + dimmoffset, length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset);
      PrintMBBuffer(address, length);
      return 0;
    }

    // DIMM memory (8MB)
    if ((offset >= 0xFF000000) && (offset <= 0xFF800000))
    {
      u32 dimmoffset = offset - 0xFF000000;
      s_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
      s_dimm->ReadBytes(memory.GetPointer(address), length);
      return 0;
    }
    // Network control
    if ((offset == 0xFFFF0000) && (length == 0x20))
    {
      s_netctrl->Seek(0, File::SeekOrigin::Begin);
      s_netctrl->ReadBytes(memory.GetPointer(address), length);
      return 0;
    }

    // Max GC disc offset
    if (offset >= 0x57058000)
    {
      PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", offset);
      return 0;
    }

    if (s_firmwaremap)
    {
      if (s_segaboot)
      {
        DICMDBUF[1] &= ~0x00100000;
        DICMDBUF[1] -= 0x20;
      }
      memcpy(memory.GetPointer(address), s_firmware + offset, length);
      return 0;
    }

    if (s_dimm_disc)
    {
      memcpy(memory.GetPointer(address), s_dimm_disc + offset, length);
      return 0;
    }

    return 1;
    break;
  // Write
  case 0xAA:
    if ((offset == 0x00600000) && (length == 0x20))
    {
      s_firmwaremap = 1;
      return 0;
    }

    if ((offset == 0x00700000) && (length == 0x20))
    {
      s_firmwaremap = 1;
      return 0;
    }

    if (s_firmwaremap)
    {
      // Firmware memory (2MB)
      if ((offset >= 0x00400000) && (offset <= 0x600000))
      {
        u32 fwoffset = offset - 0x00400000;
        memcpy(s_firmware + fwoffset, memory.GetPointer(address), length);
        return 0;
      }
    }

    // Network configuration
    if ((offset == 0x00000000) && (length == 0x80))
    {
      s_netcfg->Seek(0, File::SeekOrigin::Begin);
      s_netcfg->WriteBytes(memory.GetPointer(address), length);
      s_netcfg->Flush();
      return 0;
    }

    // Extra Settings
    // media crc check on/off
    if ((offset == 0x1FFEFFE0) && (length == 0x20))
    {
      s_extra->Seek(0, File::SeekOrigin::Begin);
      s_extra->WriteBytes(memory.GetPointer(address), length);
      s_extra->Flush();
      return 0;
    }

    // Backup memory (8MB)
    if ((offset >= 0x000006A0) && (offset <= 0x00800000))
    {
      s_backup->Seek(offset, File::SeekOrigin::Begin);
      s_backup->WriteBytes(memory.GetPointer(address), length);
      s_backup->Flush();
      return 0;
    }

    // DIMM memory (8MB)
    if ((offset >= 0x1F000000) && (offset <= 0x1F800000))
    {
      u32 dimmoffset = offset - 0x1F000000;
      s_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
      s_dimm->WriteBytes(memory.GetPointer(address), length);
      s_dimm->Flush();
      return 0;
    }

    // Network command
    if ((offset >= NetworkCommandAddress) && (offset <= 0x1F8003FF))
    {
      u32 dimmoffset = offset - NetworkCommandAddress;

      memcpy(s_network_command_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK COMMAND BUFFER ({:08x},{})", dimmoffset,
                     length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // Network command
    if ((offset >= NetworkCommandAddress2) && (offset <= 0x890601FF))
    {
      u32 dimmoffset = offset - NetworkCommandAddress2;

      memcpy(s_network_command_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK COMMAND BUFFER (2) ({:08x},{})",
                     dimmoffset, length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x1FA00000) && (offset <= 0x1FA1FFFF))
    {
      u32 dimmoffset = offset - 0x1FA00000;

      memcpy(s_network_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (1) ({:08x},{})", dimmoffset,
                     length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x1FD00000) && (offset <= 0x1FD0FFFF))
    {
      u32 dimmoffset = offset - 0x1FD00000;

      memcpy(s_network_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (2) ({:08x},{})", dimmoffset,
                     length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // Network buffer
    if ((offset >= 0x89100000) && (offset <= 0x8910FFFF))
    {
      u32 dimmoffset = offset - 0x89100000;

      memcpy(s_network_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (3) ({:08x},{})", dimmoffset,
                     length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // DIMM command, used when inquiry returns 0x21000000
    if ((offset >= 0x1F900000) && (offset <= 0x1F90003F))
    {
      u32 dimmoffset = offset - 0x1F900000;
      memcpy(s_media_buffer + dimmoffset, memory.GetPointer(address), length);

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (1) ({:08x},{})", offset,
                     length);
      PrintMBBuffer(address, length);
      return 0;
    }

    // DIMM command, used when inquiry returns 0x29000000
    if ((offset >= 0x84000000) && (offset <= 0x8400005F))
    {
      u32 dimmoffset = offset - 0x84000000;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (2) ({:08x},{})", offset,
                     length);
      PrintMBBuffer(address, length);

      u8 cmd_flag = memory.Read_U8(address);

      if (dimmoffset == 0x40 && cmd_flag == 1)
      {
        // Recast for easier access
        u32* media_buffer_in_32 = (u32*)(s_media_buffer + 0x20);
        u16* media_buffer_in_16 = (u16*)(s_media_buffer + 0x20);
        u32* media_buffer_out_32 = (u32*)(s_media_buffer);
        u16* media_buffer_out_16 = (u16*)(s_media_buffer);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Execute command:{:03X}", media_buffer_in_16[1]);

        memset(s_media_buffer, 0, 0x20);

        media_buffer_out_32[0] = media_buffer_in_32[0] | 0x80000000;  // Set command okay flag

        for (u32 i = 0; i < 0x20; i += 4)
        {
          *(u32*)(s_media_buffer + 0x40 + i) = *(u32*)(s_media_buffer);
        }

        switch (media_buffer_in_16[1])
        {
        // ?
        case 0x000:
          media_buffer_out_32[1] = 1;
          break;
        // NAND size
        case 0x001:
          media_buffer_out_32[1] = 0x1FFF8000;
          break;
        // Loading Progress
        case 0x100:
          // Status
          media_buffer_out_32[1] = 5;
          // Progress in %
          media_buffer_out_32[2] = 100;
          break;
        // SegaBoot version: 3.09
        case 0x101:
          // Version
          media_buffer_out_16[2] = Common::swap16(0x0309);
          // Unknown
          media_buffer_out_16[3] = 2;
          media_buffer_out_32[2] = 0x4746;  // "GF"
          media_buffer_out_32[4] = 0xFF;
          break;
        // System flags
        case 0x102:
          // 1: GD-ROM
          s_media_buffer[4] = 0;
          s_media_buffer[5] = 1;

          // Enable development mode (Sega Boot)
          // This also allows region free booting
          s_media_buffer[6] = 1;

          media_buffer_out_16[4] = 0;  // Access Count

          // Only used when inquiry 0x29
          //  0: NAND/MASK BOARD(HDD)
          //  1: NAND/MASK BOARD(MASK)
          //  2: NAND/MASK BOARD(NAND)
          //  3: NAND/MASK BOARD(NAND)
          //  4: DIMM BOARD (TYPE 3)
          //  5: DIMM BOARD (TYPE 3)
          //  6: DIMM BOARD (TYPE 3)
          //  7: N/A
          //  8: Unknown
          s_media_buffer[7] = 1;
          break;
        // Media board serial
        case 0x103:
          memcpy(s_media_buffer + 4, "A85E-01A62204904", 16);
          break;
        case 0x104:
          s_media_buffer[4] = 1;
          break;
        }

        memcpy(memory.GetPointer(address), s_media_buffer, length);

        memset(s_media_buffer + 0x20, 0, 0x20);

        ExpansionInterface::GenerateInterrupt(0x04);
        return 0;
      }
      else
      {
        memcpy(s_media_buffer + dimmoffset, memory.GetPointer(address), length);
      }
      return 0;
    }

    // DIMM command, used when inquiry returns 0x29000000
    if ((offset >= 0x89000000) && (offset <= 0x89000200))
    {
      u32 dimmoffset = offset - 0x89000000;
      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset);
      PrintMBBuffer(address, length);

      memcpy(s_media_buffer + dimmoffset, memory.GetPointer(address), length);

      return 0;
    }

    // Firmware Write
    if ((offset >= 0x84800000) && (offset <= 0x84818000))
    {
      u32 dimmoffset = offset - 0x84800000;

      INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write Firmware ({:08x})", dimmoffset);
      PrintMBBuffer(address, length);
      return 0;
    }

    // DIMM memory (8MB)
    if ((offset >= 0xFF000000) && (offset <= 0xFF800000))
    {
      u32 dimmoffset = offset - 0xFF000000;
      s_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
      s_dimm->WriteBytes(memory.GetPointer(address), length);
      s_dimm->Flush();
      return 0;
    }
    // Network control
    if ((offset == 0xFFFF0000) && (length == 0x20))
    {
      s_netctrl->Seek(0, File::SeekOrigin::Begin);
      s_netctrl->WriteBytes(memory.GetPointer(address), length);
      s_netctrl->Flush();
      return 0;
    }
    // Max GC disc offset
    if (offset >= 0x57058000)
    {
      PrintMBBuffer(address, length);
      PanicAlertFmtT("Unhandled Media Board Write:{0:08x}", offset);
    }
    break;
  // Execute
  case 0xAB:
    if ((offset == 0) && (length == 0))
    {
      // Recast for easier access
      u32* media_buffer_in_32 = (u32*)(s_media_buffer + 0x20);
      u16* media_buffer_in_16 = (u16*)(s_media_buffer + 0x20);
      u32* media_buffer_out_32 = (u32*)(s_media_buffer);
      u16* media_buffer_out_16 = (u16*)(s_media_buffer);

      memset(s_media_buffer, 0, 0x20);

      media_buffer_out_16[0] = media_buffer_in_16[0];

      // Command
      media_buffer_out_16[1] = media_buffer_in_16[1] | 0x8000;  // Set command okay flag

      if (media_buffer_in_16[1])
        INFO_LOG_FMT(DVDINTERFACE, "GCAM: Execute command:{:03X}", media_buffer_in_16[1]);

      switch (static_cast<AMMBCommand>(media_buffer_in_16[1]))
      {
      case AMMBCommand::Unknown_000:
        media_buffer_out_32[1] = 1;
        break;
      case AMMBCommand::GetDIMMSize:
        media_buffer_out_32[1] = 0x20000000;
        break;
      //
      // 0x00: "Initializing media board. Please wait.."
      // 0x01: "Checking network. Please wait..."
      // 0x02: "Found a system disc. Insert a game disc"
      // 0x03: "Testing a game program. {:d}%%"
      // 0x04: "Loading a game program. {:d}%%"
      // 0x05: go
      // 0x06: error xx
      //
      case AMMBCommand::GetMediaBoardStatus:
      {
        // Fake loading the game to have a chance to enter test mode
        static u32 status = 4;
        static u32 progress = 80;
        // Status
        media_buffer_out_32[1] = status;
        // Progress in %
        media_buffer_out_32[2] = progress;
        if (progress < 100)
        {
          progress++;
        }
        else
        {
          status = 5;
        }
      }
      break;
      // SegaBoot version: 3.11
      case AMMBCommand::GetSegaBootVersion:
        // Version
        media_buffer_out_16[2] = Common::swap16(0x1103);
        // Unknown
        media_buffer_out_16[3] = 1;
        media_buffer_out_32[2] = 1;
        media_buffer_out_32[4] = 0xFF;
        break;
      case AMMBCommand::GetSystemFlags:
        // 1: GD-ROM
        s_media_buffer[4] = 1;
        s_media_buffer[5] = 1;
        // Enable development mode (Sega Boot)
        // This also allows region free booting
        s_media_buffer[6] = 1;
        media_buffer_out_16[4] = 0;  // Access Count
        break;
      case AMMBCommand::GetMediaBoardSerial:
        memcpy(s_media_buffer + 4, "A89E-27A50364511", 16);
        break;
      case AMMBCommand::Unknown_104:
        s_media_buffer[4] = 1;
        break;
      case AMMBCommand::TestHardware:
        // Test type

        // 0x01: Media board
        // 0x04: Network

        // ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x301: ({:08x})", *(u32*)(s_media_buffer+0x24) );

        // Pointer to a memory address that is directly displayed on screen as a string
        // ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(s_media_buffer+0x28) );

        // On real system it shows the status about the DIMM/GD-ROM here
        // We just show "TEST OK"
        memory.Write_U32(0x54534554, media_buffer_in_32[4]);
        memory.Write_U32(0x004B4F20, media_buffer_in_32[4] + 4);

        media_buffer_out_32[1] = media_buffer_in_32[1];
        break;
      // Empty reply
      case AMMBCommand::InitLink:
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: 0x601");
        break;
      case AMMBCommand::SetupLink:
      {
        struct sockaddr_in addra, addrb;
        addra.sin_addr.s_addr = media_buffer_in_32[4];
        addrb.sin_addr.s_addr = media_buffer_in_32[5];

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: 0x606:");
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:  Size: ({}) ", media_buffer_in_16[2]);  // size
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:  Port: ({})",
                       Common::swap16(media_buffer_in_16[3]));                         // port
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:LinkNum:({:02x})", s_media_buffer[0x28]);  // linknum
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:02x})", s_media_buffer[0x29]);
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:04x})", media_buffer_in_16[5]);
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})", inet_ntoa(addra.sin_addr));    // IP
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})", inet_ntoa(addrb.sin_addr));  // Target IP
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",
                       Common::swap32(media_buffer_in_32[6]));  // some RAM address
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",
                       Common::swap32(media_buffer_in_32[7]));  // some RAM address

        media_buffer_out_32[1] = 0;
      }
      break;
      // This sends a UDP packet to previously defined Target IP/Port
      case AMMBCommand::SearchDevices:
      {
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: 0x607: ({})", media_buffer_in_16[2]);
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({})", media_buffer_in_16[3]);
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", media_buffer_in_32[2]);

        u8* Data = (u8*)(s_network_buffer + media_buffer_in_32[2] - 0x1FD00000);

        for (u32 i = 0; i < 0x20; i += 0x10)
        {
          INFO_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}", *(u32*)(Data + i),
                         *(u32*)(Data + i + 4), *(u32*)(Data + i + 8), *(u32*)(Data + i + 12));
        }

        media_buffer_out_32[1] = 0;
      }
      break;
      case AMMBCommand::Unknown_614:
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: 0x614");
        break;
      default:
        ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: execute buffer UNKNOWN:{:03x}",
                      *(u16*)(s_media_buffer + 0x22));
        break;
      }

      memset(s_media_buffer + 0x20, 0, 0x20);
      return 0;
    }

    PanicAlertFmtT("Unhandled Media Board Execute:{0:08x}", *(u16*)(s_media_buffer + 0x22));
    break;
  }

  return 0;
}

u32 GetMediaType(void)
{
  switch (GetGameType())
  {
  default:
  case FZeroAX:
  case VirtuaStriker3:
  case VirtuaStriker4:
  case GekitouProYakyuu:
  case KeyOfAvalon:
    return GDROM;
    break;

  case MarioKartGP:
  case MarioKartGP2:
  case FZeroAXMonster:
    return NAND;
    break;
  }
  // Never reached
}

u32 GetGameType(void)
{
  u64 game_id = 0;

  // Convert game ID into hex
  if (strlen(SConfig::GetInstance().GetGameID().c_str()) > 4)
  {
    game_id = 0x30303030;
  }
  else
  {
    sscanf(SConfig::GetInstance().GetGameID().c_str(), "%s", (char*)&game_id);
  }

  // This is checking for the real game IDs (See boot.id within the game)
  switch (Common::swap32((u32)game_id))
  {
  // SBGG - F-ZERO AX
  case 0x53424747:
    return FZeroAX;
  // SBHA - F-ZERO AX (Monster)
  case 0x53424841:
    return FZeroAXMonster;
  // SBKJ/SBKP - MARIOKART ARCADE GP
  case 0x53424B50:
  case 0x53424B5A:
    return MarioKartGP;
  // SBNJ/SBNL - MARIOKART ARCADE GP2
  case 0x53424E4A:
  case 0x53424E4C:
    return MarioKartGP2;
  // SBEJ/SBEY - Virtua Striker 2002
  case 0x5342454A:
  case 0x53424559:
    return VirtuaStriker3;
  // SBLJ/SBLK/SBLL - VIRTUA STRIKER 4 Ver.2006
  case 0x53424C4A:
  case 0x53424C4B:
  case 0x53424C4C:
  // SBHJ/SBHN/SBHZ - VIRTUA STRIKER 4 VER.A
  case 0x5342484A:
  case 0x5342484E:
  case 0x5342485A:
  // SBJA/SBJJ  - VIRTUA STRIKER 4
  case 0x53424A41:
  case 0x53424A4A:
    return VirtuaStriker4;
  // SBFX/SBJN - Key of Avalon
  case 0x53424658:
  case 0x53424A4E:
    return KeyOfAvalon;
  // SBGX - Gekitou Pro Yakyuu (DIMM Upgrade 3.17)
  case 0x53424758:
    return GekitouProYakyuu;
  default:
    PanicAlertFmtT("Unknown game ID:{0:08x}, using default controls.", game_id);
  // GSBJ/G12U - VIRTUA STRIKER 3
  // RELS/RELJ - SegaBoot (does not have a boot.id)
  case 0x4753424A:
  case 0x47313255:
  case 0x52454C53:
  case 0x52454c4a:
    return VirtuaStriker3;
  }
  // never reached
}

void Shutdown(void)
{
  s_netcfg->Close();
  s_netctrl->Close();
  s_extra->Close();
  s_backup->Close();
  s_dimm->Close();

  if (s_dimm_disc)
  {
    delete[] s_dimm_disc;
    s_dimm_disc = nullptr;
  }
}

}  // namespace AMMediaboard
