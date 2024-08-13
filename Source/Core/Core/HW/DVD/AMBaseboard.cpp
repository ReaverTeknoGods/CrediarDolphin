// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma warning(disable : 4189)

#include "Core/BootManager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/IOFile.h"
#include "DiscIO/DirectoryBlob.h"

#include "Core/Boot/Boot.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigLoaders/NetPlayConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/Sram.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/DVD/DVDThread.h"
#include "Core/HW/DVD/AMBaseboard.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/WiiRoot.h"
#include "Core/HLE/HLE.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/PowerPC/PPCSymbolDB.h" 

#include "DiscIO/Enums.h"
#include "DiscIO/VolumeDisc.h"

#if defined(__linux__) or defined(__APPLE__) or defined(__FreeBSD__) or defined(__NetBSD__) or defined(__HAIKU__)

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


namespace AMBaseboard
{

static u32 m_game_type = 0;
static u32 FIRMWAREMAP = 0;
static u32 m_segaboot = 0;
static u32 m_v2_dma_adr = 0;
static u32 m_namco_cam = 0;
static u32 Timeouts[3] = {20000, 20000, 20000};

static u32 GCAMKeyA;
static u32 GCAMKeyB;
static u32 GCAMKeyC;

static File::IOFile* m_netcfg = nullptr;
static File::IOFile* m_netctrl = nullptr;
static File::IOFile* m_extra = nullptr;
static File::IOFile* m_backup = nullptr;
static File::IOFile* m_dimm = nullptr;

unsigned char* m_dimm_disc = nullptr;

static unsigned char firmware[2*1024*1024];
static unsigned char media_buffer[0x300];
static unsigned char network_command_buffer[0x4FFE00];
static unsigned char network_buffer[128*1024];

enum BaseBoardAddress : u32
{
  NetworkCommandAddress = 0x1F800200,
  NetworkBufferAddress1 = 0x1FA00000,
  NetworkBufferAddress2 = 0x1FD00000,
};

static inline void PrintMBBuffer( u32 Address, u32 Length )
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

	for( u32 i=0; i < Length; i+=0x10 )
	{
    INFO_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}",  memory.Read_U32(Address + i),
																                                      memory.Read_U32(Address+i+4),
																                                      memory.Read_U32(Address+i+8),
																                                      memory.Read_U32(Address+i+12) );
	}
}
void FirmwareMap(bool on)
{
  if (on)
    FIRMWAREMAP = 1;
  else
    FIRMWAREMAP = 0;
}

void InitKeys(u32 KeyA, u32 KeyB, u32 KeyC)
{
  GCAMKeyA = KeyA;
  GCAMKeyB = KeyB;
  GCAMKeyC = KeyC;
}
void Init(void)
{
  memset(media_buffer, 0, sizeof(media_buffer));
  memset(network_buffer, 0, sizeof(network_buffer));
  memset(network_command_buffer, 0, sizeof(network_command_buffer));
  memset(firmware, -1, sizeof(firmware));

  m_game_type = 0;
  m_segaboot = 0;
  FIRMWAREMAP = 0;

  GCAMKeyA = 0;
  GCAMKeyB = 0;
  GCAMKeyC = 0;

  if (File::Exists(File::GetUserPath(D_TRIUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_TRIUSER_IDX));
  }

  std::string netcfg_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetcfg.bin");
  if (File::Exists(netcfg_Filename))
  {
    m_netcfg = new File::IOFile(netcfg_Filename, "rb+");
  }
  else
  {
    m_netcfg = new File::IOFile(netcfg_Filename, "wb+");
  }
  if (!m_netcfg)
  {
    PanicAlertFmt("Failed to open/create:{0}", netcfg_Filename);
  }

  std::string netctrl_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetctrl.bin");
  if (File::Exists(netctrl_Filename))
  {
    m_netctrl = new File::IOFile(netctrl_Filename, "rb+");
  }
  else
  {
    m_netctrl = new File::IOFile(netctrl_Filename, "wb+");
  }

  std::string extra_Filename(File::GetUserPath(D_TRIUSER_IDX) + "triextra.bin");
  if (File::Exists(extra_Filename))
  {
    m_extra = new File::IOFile(extra_Filename, "rb+");
  }
  else
  {
    m_extra = new File::IOFile(extra_Filename, "wb+");
  }

  std::string dimm_Filename(File::GetUserPath(D_TRIUSER_IDX) + "tridimm_" + SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(dimm_Filename))
  {
    m_dimm = new File::IOFile(dimm_Filename, "rb+");
  }
  else
  {
    m_dimm = new File::IOFile(dimm_Filename, "wb+");
  }

  std::string backup_Filename(File::GetUserPath(D_TRIUSER_IDX) + "backup_" + SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(backup_Filename))
  {
    m_backup = new File::IOFile(backup_Filename, "rb+");
  }
  else
  {
    m_backup = new File::IOFile(backup_Filename, "wb+");
  }

  // This is the firmware for the Triforce
  std::string sega_boot_Filename(File::GetUserPath(D_TRIUSER_IDX) + "segaboot.gcm");
  if (File::Exists(sega_boot_Filename))
  {
    File::IOFile* sega_boot = new File::IOFile(sega_boot_Filename, "rb+");
    if (sega_boot)
    {
      u64 Length = sega_boot->GetSize();
      if (Length >= sizeof(firmware))
      {
        Length = sizeof(firmware);
      }
      sega_boot->ReadBytes(firmware, Length);
      sega_boot->Close();
    }
  }
  else
  {
    PanicAlertFmt("Failed to open segaboot.gcm, which is required for test menus.");
  }
}
u8 *InitDIMM( void )
{
  if (!m_dimm_disc)
  {
    m_dimm_disc = new u8[512 * 1024 * 1024];
  }
  FIRMWAREMAP = 0;
  return m_dimm_disc;
}
u32 ExecuteCommand(std::array<u32, 3>& DICMDBUF, u32 Address, u32 Length)
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
  if (GCAMKeyA == 0)
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

  DICMDBUF[0] ^= GCAMKeyA;
  DICMDBUF[1] ^= GCAMKeyB;
  //Length ^= GCAMKeyC; DMA Length is always plain

  u32 seed = DICMDBUF[0] >> 16;

  GCAMKeyA *= seed;
  GCAMKeyB *= seed;
  GCAMKeyC *= seed;

  DICMDBUF[0] <<= 24;
  DICMDBUF[1] <<= 2;
 
  // SegaBoot adds bits for some reason to offset/length
  // also adds 0x20 to offset
  if (DICMDBUF[1] == 0x00100440)
  {
    m_segaboot = 1;
  }

  u32 Command = DICMDBUF[0];
  u32 Offset  = DICMDBUF[1];
 
	INFO_LOG_FMT(DVDINTERFACE, "GCAM: {:08x} {:08x} DMA=addr:{:08x},len:{:08x} Keys: {:08x} {:08x} {:08x}",
                                Command, Offset, Address, Length, GCAMKeyA, GCAMKeyB, GCAMKeyC );

  // Test mode
  if (Offset == 0x0002440)
  { 
    // Set by OSResetSystem
    if(memory.Read_U32(0x811FFF00) == 1)
    {
      // Don't map firmware while in SegaBoot
      if (memory.Read_U32(0x8006BF70) != 0x0A536567)
      {
        FIRMWAREMAP = 1;
      }
    }
  }
  
	switch(Command>>24)
	{
		// Inquiry
    case 0x12:
      if(FIRMWAREMAP == 1)
      {
        FIRMWAREMAP = 0;
        m_segaboot = 0;
      }

      //Avalon expects a higher version
      if (GetGameType() == KeyOfAvalon )
      {
        return 0x29484100;
      }

			return 0x21484100;
			break;
		// Read
		case 0xA8:
      if ((Offset & 0x8FFF0000) == 0x80000000 )
			{
				switch(Offset)
				{
				// Media board status (1)
        case 0x80000000: 
					memory.Write_U16( Common::swap16( 0x0100 ), Address );
					break;
				// Media board status (2)
        case 0x80000020: 
					memset( memory.GetPointer(Address), 0, Length );
					break;
				// Media board status (3)
        case 0x80000040: 
					memset( memory.GetPointer(Address), 0xFF, Length );
					// DIMM size (512MB)
					memory.Write_U32( Common::swap32( 0x20000000 ), Address );
					// GCAM signature
					memory.Write_U32( 0x4743414D, Address+4 );
          break;
        // ?
        case 0x80000100:
          memory.Write_U32(Common::swap32( (u32)0x001F1F1F ), Address);
          break;
        // Firmware status (1)
        case 0x80000120:
          memory.Write_U32(Common::swap32( (u32)0x01FA ), Address);
          break;
				// Firmware status (2)
        case 0x80000140:
          memory.Write_U32(Common::swap32((u32)1), Address);
          break;
        case 0x80000160:
          memory.Write_U32( 0x00001E00, Address);
          break;
        case 0x80000180:
          memory.Write_U32( 0, Address);
          break;
        case 0x800001A0:
          memory.Write_U32( 0xFFFFFFFF , Address);
          break;
				default:
					PrintMBBuffer( Address, Length );
          PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", Offset);
					break;
				}
				return 0;
      }
			// Network configuration
			if( (Offset == 0x00000000) && (Length == 0x80) )
			{
        m_netcfg->Seek(0, File::SeekOrigin::Begin);
				m_netcfg->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
      // Extra Settings
      // media crc check on/off
      if ((Offset == 0x1FFEFFE0) && (Length == 0x20))
      {
        m_extra->Seek(0, File::SeekOrigin::Begin);
        m_extra->ReadBytes(memory.GetPointer(Address), Length);
        return 0;
      }      
			// DIMM memory (8MB)
			if( (Offset >= 0x1F000000) && (Offset <= 0x1F800000) )
			{
        u32 dimmoffset = Offset - 0x1F000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
				m_dimm->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
			// DIMM command (V1)
			if( (Offset >= 0x1F900000) && (Offset <= 0x1F90003F) )
			{
        u32 dimmoffset = Offset - 0x1F900000;
				memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );
				
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (1) ({:08x},{})", Offset, Length );
				PrintMBBuffer( Address, Length );
				return 0;
			}
			// Network command
			if( (Offset >= NetworkCommandAddress) && (Offset <= 0x1FCFFFFF) )
      {
        u32 dimmoffset = Offset - NetworkCommandAddress;
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK COMMAND BUFFER ({:08x},{})", Offset, Length );
        memcpy(memory.GetPointer(Address), network_command_buffer + dimmoffset, Length); 		 
				return 0;
      }

      // Network buffer 
      if ((Offset >= 0x1FA00000) && (Offset <= 0x1FA0FFFF))
      {
        u32 dimmoffset = Offset - 0x1FA00000;
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (1) ({:08x},{})", Offset, Length);
        memcpy(memory.GetPointer(Address), network_buffer + dimmoffset, Length);
        return 0;
      }

      // Network buffer
      if ((Offset >= 0x1FD00000) && (Offset <= 0x1FD0FFFF))
      {
        u32 dimmoffset = Offset - 0x1FD00000;
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (2) ({:08x},{})", Offset, Length);
        memcpy(memory.GetPointer(Address), network_buffer + dimmoffset, Length);
        return 0;
      }
      
			// DIMM command (V2)
			if( (Offset >= 0x84000000) && (Offset <= 0x8400005F) )
			{
				u32 dimmoffset = Offset - 0x84000000;
				memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );
				
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (2) ({:08x},{})", Offset, Length );
				PrintMBBuffer( Address, Length );
				return 0;
      }

      // DIMM command (V2)
      if( Offset == 0x88000000 )
      {
        u32 dimmoffset = Offset - 0x88000000;

        memset(media_buffer, 0, 0x20);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Execute command:{:03X}", *(u16*)(media_buffer + 0x202));

        switch( *(u16*)(media_buffer + 0x202) )
        {
          case 1:
            *(u32*)(media_buffer) = 0x1FFF8000;
            break;
          default:
            break;
        }

        memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (?) ({:08x})", dimmoffset);
        PrintMBBuffer(Address, Length);
        return 0;
      }

      // DIMM command (V2)
      if ((Offset >= 0x89000000) && (Offset <= 0x89000200))
      {
        u32 dimmoffset = Offset - 0x89000000;
        memcpy(memory.GetPointer(Address), media_buffer + dimmoffset, Length);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset);
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM memory (8MB)
			if( (Offset >= 0xFF000000) && (Offset <= 0xFF800000) )
			{
				u32 dimmoffset = Offset - 0xFF000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
				m_dimm->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
			// Network control
			if( (Offset == 0xFFFF0000) && (Length == 0x20) )
			{
        m_netctrl->Seek(0, File::SeekOrigin::Begin);
				m_netctrl->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}

			// Max GC disc offset
			if( Offset >= 0x57058000 )
			{
				PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", Offset );
			}

      if (FIRMWAREMAP)
      {
        if (m_segaboot)
        {
          DICMDBUF[1] &= ~0x00100000;
          DICMDBUF[1] -= 0x20;
        }
        memcpy(memory.GetPointer(Address), firmware + Offset, Length);
        return 0;
      }

      if (m_dimm_disc)
      {
        memcpy(memory.GetPointer(Address), m_dimm_disc + Offset, Length);
        return 0;
      }

      return 1;			 
			break;
		// Write
		case 0xAA:
			if( (Offset == 0x00600000 ) && (Length == 0x20) )
			{
				FIRMWAREMAP = 1;
				return 0;
			}
			if( (Offset == 0x00700000 ) && (Length == 0x20) )
			{
				FIRMWAREMAP = 1;
				return 0;
			}
			if(FIRMWAREMAP)
			{
				// Firmware memory (2MB)
				if( (Offset >= 0x00400000) && (Offset <= 0x600000) )
				{
					u32 fwoffset = Offset - 0x00400000;
					memcpy( firmware + fwoffset, memory.GetPointer(Address), Length );
					return 0;
				}
			}
			// Network configuration
			if( (Offset == 0x00000000) && (Length == 0x80) )
			{
        m_netcfg->Seek(0, File::SeekOrigin::Begin);
				m_netcfg->WriteBytes( memory.GetPointer(Address), Length );
        m_netcfg->Flush();
				return 0;
      }
      // Extra Settings
      // media crc check on/off
      if ((Offset == 0x1FFEFFE0) && (Length == 0x20))
      {
        m_extra->Seek(0, File::SeekOrigin::Begin);
        m_extra->WriteBytes(memory.GetPointer(Address), Length);
        m_extra->Flush();
        return 0;
      }      
			// Backup memory (8MB)
			if( (Offset >= 0x000006A0) && (Offset <= 0x00800000) )
			{
        m_backup->Seek(Offset, File::SeekOrigin::Begin);
        m_backup->WriteBytes(memory.GetPointer(Address), Length);
        m_backup->Flush();
				return 0;
			}
			// DIMM memory (8MB)
			if( (Offset >= 0x1F000000) && (Offset <= 0x1F800000) )
			{
				u32 dimmoffset = Offset - 0x1F000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
        m_dimm->WriteBytes(memory.GetPointer(Address), Length);
        m_dimm->Flush();
				return 0;
			}
			// Network command
			if( (Offset >= NetworkCommandAddress) && (Offset <= 0x1F8003FF) )
			{
				u32 offset = Offset - NetworkCommandAddress;

				memcpy(network_command_buffer + offset, memory.GetPointer(Address), Length);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK COMMAND BUFFER ({:08x},{})", Offset, Length);
				PrintMBBuffer( Address, Length );
				return 0;
      }
      // Network buffer
      if ((Offset >= 0x1FA00000) && (Offset <= 0x1FA1FFFF))
      {
        u32 offset = Offset - 0x1FA00000;

        memcpy(network_buffer + offset, memory.GetPointer(Address), Length);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (1) ({:08x},{})", Offset, Length);
        PrintMBBuffer(Address, Length);
        return 0;
      }
      // Network buffer
      if ((Offset >= 0x1FD00000) && (Offset <= 0x1FD0FFFF))
      {
        u32 offset = Offset - 0x1FD00000;

        memcpy(network_buffer + offset, memory.GetPointer(Address), Length);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (2) ({:08x},{})", Offset, Length);
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM command, used when inquiry returns 0x21000000
			if( (Offset >= 0x1F900000) && (Offset <= 0x1F90003F) )
			{
				u32 dimmoffset = Offset - 0x1F900000;
				memcpy( media_buffer + dimmoffset, memory.GetPointer(Address), Length );
				
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (1) ({:08x},{})", Offset, Length);
				PrintMBBuffer( Address, Length );
				return 0;
			}

			// DIMM command, used when inquiry returns 0x29000000
			if( (Offset >= 0x84000000) && (Offset <= 0x8400005F) )
      {
        u32 dimmoffset = Offset - 0x84000000;
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (2) ({:08x},{})", Offset, Length);
        PrintMBBuffer(Address, Length);

        u8 cmd_flag = memory.Read_U8(Address);

        if (dimmoffset == 0x20 && cmd_flag != 0 )
        {
          m_v2_dma_adr = Address;
        }

        //if (dimmoffset == 0x40 && cmd_flag == 0)
        //{
        //  INFO_LOG_FMT(DVDINTERFACE, "GCAM: PC:{:08x}", PC);
        //  PowerPC::breakpoints.Add( PC+8, false );
        //}

				if( dimmoffset == 0x40 && cmd_flag == 1 )
        { 
          // Recast for easier access
          u32* media_buffer_in_32 = (u32*)(media_buffer + 0x20);
          u16* media_buffer_in_16 = (u16*)(media_buffer + 0x20);
          u32* media_buffer_out_32 = (u32*)(media_buffer);
          u16* media_buffer_out_16 = (u16*)(media_buffer);

					INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Execute command:{:03X}", media_buffer_in_16[1] );
					
					memset(media_buffer, 0, 0x20);

          media_buffer_out_32[0] = media_buffer_in_32[0] | 0x80000000;  // Set command okay flag

          for (u32 i = 0; i < 0x20; i += 4)
          {
            *(u32*)(media_buffer + 0x40 + i) = *(u32*)(media_buffer);
          }

          //memory.Write_U8( 0xA9, m_v2_dma_adr + 0x20 );

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
            media_buffer_out_32[2] = 0x4746; // "GF" 
            media_buffer_out_32[4] = 0xFF;
            break;
          // System flags
          case 0x102:
            // 1: GD-ROM
            media_buffer[4] = 0;
            media_buffer[5] = 1;
            // enable development mode (Sega Boot)
            // This also allows region free booting
            media_buffer[6] = 1;
            media_buffer_out_16[4] = 0;  // Access Count
            // Only used when inquiry 0x29
            /*
              0: NAND/MASK BOARD(HDD)
              1: NAND/MASK BOARD(MASK)
              2: NAND/MASK BOARD(NAND)
              3: NAND/MASK BOARD(NAND)
              4: DIMM BOARD (TYPE 3)
              5: DIMM BOARD (TYPE 3)
              6: DIMM BOARD (TYPE 3)
              7: N/A
              8: Unknown
            */
            media_buffer[7] = 1;
            break;
          // Media board serial
          case 0x103:
            memcpy(media_buffer + 4, "A85E-01A62204904", 16);
            break;
          case 0x104:
            media_buffer[4] = 1;
            break;
          }

          memcpy(memory.GetPointer(Address), media_buffer, Length);

          memset(media_buffer + 0x20, 0, 0x20 );
          return 0;
				}
        else
        {
          memcpy(media_buffer + dimmoffset, memory.GetPointer(Address), Length);
        }
        return 0;
			}

      // DIMM command, used when inquiry returns 0x29000000
      if ((Offset >= 0x89000000) && (Offset <= 0x89000200))
      {
        u32 dimmoffset = Offset - 0x89000000;
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset );
        PrintMBBuffer(Address, Length);

        memcpy(media_buffer + dimmoffset, memory.GetPointer(Address), Length);

        return 0;
      }

      // Firmware Write
      if ((Offset >= 0x84800000) && (Offset <= 0x84818000))
      {
        u32 dimmoffset = Offset - 0x84800000;

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write Firmware ({:08x})", dimmoffset );
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM memory (8MB)
			if( (Offset >= 0xFF000000) && (Offset <= 0xFF800000) )
			{
				u32 dimmoffset = Offset - 0xFF000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
        m_dimm->WriteBytes(memory.GetPointer(Address), Length);
        m_dimm->Flush();
				return 0;
			}
			// Network control
			if( (Offset == 0xFFFF0000) && (Length == 0x20) )
			{
				m_netctrl->Seek( 0, File::SeekOrigin::Begin );
        m_netctrl->WriteBytes(memory.GetPointer(Address), Length);
        m_netctrl->Flush();
				return 0;
			}
			// Max GC disc offset
			if( Offset >= 0x57058000 )
			{
				PrintMBBuffer( Address, Length );
				PanicAlertFmtT("Unhandled Media Board Write:{0:08x}", Offset );
			}
			break;
		// Execute
    case 0xAB:
			if( (Offset == 0) && (Length == 0) )
      {
        // Recast for easier access
        u32* media_buffer_in_32  = (u32*)(media_buffer + 0x20);
        u16* media_buffer_in_16  = (u16*)(media_buffer + 0x20);
        u32* media_buffer_out_32 = (u32*)(media_buffer);
        u16* media_buffer_out_16 = (u16*)(media_buffer);

				memset( media_buffer, 0, 0x20 );

				media_buffer_out_16[0] = media_buffer_in_16[0];

				// Command
        media_buffer_out_16[1] = media_buffer_in_16[1] | 0x8000;  // Set command okay flag
				
				if (media_buffer_in_16[1])
          NOTICE_LOG_FMT(DVDINTERFACE, "GCAM: Execute command:{:03X}", media_buffer_in_16[1]);
        
				switch (static_cast<AMBBCommand>(media_buffer_in_16[1]))
        {
          case AMBBCommand::Unknown_000:
            media_buffer_out_32[1] = 1;
						break;
          case AMBBCommand::GetDIMMSize:
            media_buffer_out_32[1] = 0x20000000;
						break;
					/*
					0x00: "Initializing media board. Please wait.."
					0x01: "Checking network. Please wait..." 
					0x02: "Found a system disc. Insert a game disc"
					0x03: "Testing a game program. {:d}%%"
					0x04: "Loading a game program. {:d}%%"
					0x05: go
					0x06: error xx
					*/
          case AMBBCommand::GetMediaBoardStatus:
          {
            // Fake loading the game to have a chance to enter test mode
            static u32 status   = 4;
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
          case AMBBCommand::GetSegaBootVersion:
						// Version
            media_buffer_out_16[2] = Common::swap16(0x1103);
						// Unknown
            media_buffer_out_16[3] = 1;
            media_buffer_out_32[2] = 1;
            media_buffer_out_32[4] = 0xFF;
            break;
          case AMBBCommand::GetSystemFlags:
            // 1: GD-ROM
            media_buffer[4] = 1;
            media_buffer[5] = 1;
						// Enable development mode (Sega Boot)
            // This also allows region free booting
						media_buffer[6] = 1; 
            media_buffer_out_16[4] = 0;  // Access Count
            break;
          case AMBBCommand::GetMediaBoardSerial:
						memcpy( media_buffer + 4, "A89E-27A50364511", 16 );
            break;
          case AMBBCommand::Unknown_104:
						media_buffer[4] = 1;
            break;
          case AMBBCommand::TestHardware:
						// Test type
						/*
							0x01: Media board
							0x04: Network
						*/
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x301: ({:08x})", *(u32*)(media_buffer+0x24) );

						//Pointer to a memory address that is directly displayed on screen as a string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x28) );

						// On real system it shows the status about the DIMM/GD-ROM here
						// We just show "TEST OK"
            memory.Write_U32(0x54534554, media_buffer_in_32[4] );
            memory.Write_U32(0x004B4F20, media_buffer_in_32[4] + 4 );

						media_buffer_out_32[1] = media_buffer_in_32[1];
						break;
          case AMBBCommand::Accept:
					{
            u32 fd        = media_buffer_in_32[2];
            u32 addr_off  = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 len_off   = media_buffer_in_32[4] - NetworkCommandAddress;

            struct sockaddr* addr = (struct sockaddr*)(network_command_buffer + addr_off);
            int* len              = (int*)(network_command_buffer + len_off);

            int ret = 0;
            int err = 0;

            u_long val = 1;
            ioctlsocket(fd, FIONBIO, &val);

            ret = accept(fd, addr, (socklen_t*)len);
            err = WSAGetLastError();

            val = 0;
            ioctlsocket(fd, FIONBIO, &val);

            if (ret == SOCKET_ERROR)
            {
              if (err == WSAEWOULDBLOCK)
              {
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
                    ret = 0;
                  }
                  else
                  {
                    ret = SOCKET_ERROR;
                  }
                }
              }
            }

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: accept( {} ):{} ({})\n", fd, ret, err );
            media_buffer_out_32[1] = ret;
					} break;
          case AMBBCommand::Bind:
					{
						struct sockaddr_in addr;

						u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3] - NetworkCommandAddress; 
            u32 len = media_buffer_in_32[4];

						memcpy( (void*)&addr, network_command_buffer + off, sizeof(struct sockaddr_in) );
						
						addr.sin_family					= Common::swap16(addr.sin_family);
						*(u32*)(&addr.sin_addr)	= Common::swap32(*(u32*)(&addr.sin_addr));

            /*
              Triforce games usually use hardcoded IPs
              This is replaced to listen to the ANY address instead
            */
            addr.sin_addr.s_addr = INADDR_ANY;

						int ret = bind( fd, (const sockaddr*)&addr, len );
						int err = WSAGetLastError();

            //if (ret < 0 )
            //  PanicAlertFmt("Socket Bind Failed with{0}", err);

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: bind( {}, ({},{:08x}:{}), {} ):{} ({})\n", fd,
                           addr.sin_family, addr.sin_addr.s_addr,
                           Common::swap16(addr.sin_port), len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
          case AMBBCommand::Closesocket:
					{
            u32 fd = media_buffer_in_32[2];

            int ret = closesocket(fd);

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: closesocket( {} ):{}\n", fd, ret);

            media_buffer_out_32[1] = ret;
					} break;
          case AMBBCommand::Connect:
					{
						struct sockaddr_in addr;

						u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 len = media_buffer_in_32[4];

            int ret = 0;
            int err = 0;

						memcpy( (void*)&addr, network_command_buffer + off , sizeof(struct sockaddr_in) );

            //sockaddr_in clientService;
            //clientService.sin_family      = AF_INET;
            //clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
            //clientService.sin_port        = htons(27015);

            // CyCraft Connect IP, change to localhost
            if (addr.sin_addr.s_addr == inet_addr("192.168.11.111") )
            {
              addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            }

            // NAMCO Camera ( IPs are: 192.168.29.104-108 )
            if ((addr.sin_addr.s_addr & 0xFFFFFF00) == 0xC0A81D00)
            {
              addr.sin_addr.s_addr = inet_addr("127.0.0.1");
              addr.sin_family      = htons(AF_INET); // fix family?
              m_namco_cam          = fd;
            }

            addr.sin_family = Common::swap16(addr.sin_family);
            //*(u32*)(&addr.sin_addr) = Common::swap32(*(u32*)(&addr.sin_addr));

            u_long val = 1;
            ioctlsocket(fd, FIONBIO, &val);

            ret = connect(fd, (const sockaddr*)&addr, len);
            err = WSAGetLastError();

            val = 0;
            ioctlsocket(fd, FIONBIO, &val);

            if ( ret == SOCKET_ERROR )
            {
              if ( err == WSAEWOULDBLOCK )
              {
                fd_set writefds,errfds;

                timeval timeout;
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;

                FD_ZERO(&writefds);
                FD_ZERO(&errfds);

                FD_SET(fd, &writefds);
                FD_SET(fd, &errfds); 

                ret = select(fd, nullptr, &writefds, &errfds, &timeout);
                if (ret <0)
                {
                  err = WSAGetLastError();
                }
                else
                {
                  if( FD_ISSET(fd, &writefds) )
                  {
                    ret = 0;
                  }
                  else
                  {
                    ret = SOCKET_ERROR;
                  }
                }
              }
            }


						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: connect( {}, ({},{}:{}), {} ):{} ({})\n", fd, addr.sin_family, inet_ntoa(addr.sin_addr), Common::swap16(addr.sin_port), len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
					// getIPbyDNS
          case AMBBCommand::GetIPbyDNS:
					{
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x405: ({:08x})", *(u32*)(media_buffer+0x24) );
						// Address of string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x28) );
						// Length of string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x2C) );

						u32 offset = media_buffer_in_32[2] - NetworkCommandAddress;
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: getIPbyDNS({})", (char*)(network_command_buffer + offset) );						
					} break;
					// inet_addr
          case AMBBCommand::InetAddr:
					{
            char *IP			= (char*)(network_command_buffer + (media_buffer_in_32[2] - NetworkCommandAddress) );
            u32 IPLength = media_buffer_in_32[3];

						memcpy( media_buffer + 8, IP, IPLength );
							
						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: inet_addr({})\n", IP );
            media_buffer_out_32[1] = Common::swap32(inet_addr(IP));
					} break;
          /*
            0x407: ioctl
          */
          case AMBBCommand::Listen:
					{
            u32 fd      = media_buffer_in_32[2];
            u32 backlog = media_buffer_in_32[3];

						int ret = listen( fd, backlog );

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: listen( {}, {} ):{:d}\n", fd, backlog, ret);

            media_buffer_out_32[1] = ret;
          }
          break;
          case AMBBCommand::Recv:
					{
            u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3];
            u32 len = media_buffer_in_32[4];

            if( len >= sizeof(network_buffer) )
            {
              len = sizeof(network_buffer);
            }

            int ret = 0; 
            char* buffer = (char*)(network_buffer+off);

            if( off >= NetworkCommandAddress && off < 0x1FD00000 )
            {
              buffer = (char*)(network_command_buffer + off - NetworkCommandAddress);

              if( off + len > sizeof(network_command_buffer) )
              {
                PanicAlertFmt("RECV: Buffer overrun:{0} {1} ", off, len);
              }
            }
            else
            {
              if (off + len > sizeof(network_buffer))
              {
                PanicAlertFmt("RECV: Buffer overrun:{0} {1} ", off, len);
              }
            }

            //u32 timeout = Timeouts[0] / 1000;
            int err = 0;
            //while (timeout--)
            //{
              ret = recv(fd, buffer, len, 0);
            //  if (ret >= 0)
            //    break;

            //  if (ret == SOCKET_ERROR)
            //  {
                err = WSAGetLastError();
            //    if (err == WSAEWOULDBLOCK)
            //    {
            //      Sleep(1);
            //      continue;
            //    }
            //    break;
            //  }
            //}

            //if( err == WSAEWOULDBLOCK )
            //{
            //  ret = 0;
            //}

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: recv( {}, 0x{:08x}, {} ):{} {}\n", fd, off, len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
					// send
          case AMBBCommand::Send:
					{
            u32 fd     = media_buffer_in_32[2];
            u32 offset = media_buffer_in_32[3];
            u32 len    = media_buffer_in_32[4];
            
            int ret = 0;

            if (offset >= NetworkBufferAddress1 && offset < 0x1FA20000)
            {
              offset -= NetworkBufferAddress1;
            }
            else if (offset >= NetworkBufferAddress2 && offset < 0x1FD10000)
            {
              offset -= NetworkBufferAddress2;
            }
            else 
            {
              ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: send(error) unhandled destination:{:08x}\n", offset  );
            }

            ret = send(fd, (char*)(network_buffer + offset), len, 0);
            int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: send( {}, 0x{:08x}, {} ): {} {}\n", fd, offset, len, ret ,err );

						media_buffer_out_32[1] = ret;
					} break;
          case AMBBCommand::Socket:
					{
					  // Protocol is not sent
            u32 af   = media_buffer_in_32[2];
            u32 type = media_buffer_in_32[3];

						SOCKET fd = socket(af, type, IPPROTO_TCP);

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: socket( {}, {}, 6 ):{}\n", af, type, fd);
							
						media_buffer_out_32[1] = fd;
            media_buffer_out_32[2] = media_buffer_in_32[2];
            media_buffer_out_32[3] = media_buffer_in_32[3];
            media_buffer_out_32[4] = media_buffer_in_32[4];
					} break;
          case AMBBCommand::Select:
					{
            u32 nfds     = media_buffer_in_32[2];
            u32 ROffset  = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 WOffset  = media_buffer_in_32[4] - NetworkCommandAddress;
            u32 EOffset  = media_buffer_in_32[5] - NetworkCommandAddress;
            u32 TOffset  = media_buffer_in_32[6] - NetworkCommandAddress;

            /*
              BUG: NAMCAM is hardcoded to call this with socket ID 0x100 which might be some magic thing?
              Winsocks expects a valid socket so we take the socket from the connect.
            */
            if (AMBaseboard::GetGameType() == MarioKartGP2)
            {
              if (nfds == 256)
              {
                nfds = m_namco_cam;
              }
            }

            fd_set* readfds  = nullptr;     
            fd_set* writefds = nullptr;

            // Either of these can be zero
            if (media_buffer_in_32[3])
            {
              readfds = (fd_set*)(network_command_buffer + ROffset);
              FD_ZERO(readfds);
              FD_SET(nfds, readfds);
            }

            if (media_buffer_in_32[4])
            {
              writefds = (fd_set*)(network_command_buffer + WOffset);
              FD_ZERO(writefds);
              FD_SET(nfds, writefds);
            }

            timeval timeout;
            if( media_buffer_in_32[6] )
            {
              memcpy( &timeout, (timeval*)(network_command_buffer + TOffset), sizeof(timeval));
            }
            else
            {
              timeout.tv_sec = Timeouts[0] / 1000;
              timeout.tv_usec = 0;
            }

            int ret = select( nfds, readfds, writefds, nullptr, &timeout );

            int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: select( {} 0x{:08x} 0x{:08x} 0x{:08x} 0x{:08x} ):{} {} \n", nfds, media_buffer_in_32[3], media_buffer_in_32[4], media_buffer_in_32[5], media_buffer_in_32[6], ret, err);
						//hexdump( NetworkCMDBuffer, 0x40 );

						media_buffer_out_32[1] = ret;
					} break;
          /*
            0x40D: shutdown
          */
          case AMBBCommand::SetSockOpt:
					{ 
						SOCKET s            = (SOCKET)(media_buffer_in_32[2]);
            int level           =    (int)(media_buffer_in_32[3]);
            int optname         =    (int)(media_buffer_in_32[4]);
            const char* optval  =  (char*)(network_command_buffer + media_buffer_in_32[5] - NetworkCommandAddress );
            int optlen          =    (int)(media_buffer_in_32[6]);

						int ret = setsockopt( s, level, optname, optval, optlen );

						int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: setsockopt( {:d}, {:04x}, {}, {:p}, {} ):{:d} ({})\n", s, level, optname, optval, optlen, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
          /*
            0x40F: getsockopt
          */
          case AMBBCommand::SetTimeOuts:
					{
						u32 fd       = media_buffer_in_32[2];
            u32 timeoutA = media_buffer_in_32[3];
            u32 timeoutB = media_buffer_in_32[4];
            u32 timeoutC = media_buffer_in_32[5];

            Timeouts[0] = timeoutA;
            Timeouts[1] = timeoutB;
            Timeouts[2] = timeoutC;

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

            NOTICE_LOG_FMT( DVDINTERFACE, "GC-AM: SetTimeOuts( {}, {}, {}, {} ):{}\n", fd, timeoutA, timeoutB, timeoutC, ret );
							
						media_buffer_out_32[1] = 0;
            media_buffer_out_32[3] = media_buffer_in_32[3];
            media_buffer_out_32[4] = media_buffer_in_32[4];
            media_buffer_out_32[5] = media_buffer_in_32[5];
					} break;
          /*
            This expects 0x46 to be returned
          */
          case AMBBCommand::SetUnknownFlag:
					{
            u32 fd = media_buffer_in_32[2];

            //int ret = closesocket(fd);

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: SetUnknownFlag( {} )\n", fd);
            
						media_buffer_out_32[1] = 0x46;
					} break;
          /*
            0x412: routeAdd
            0x413: routeDelete
            0x414: getParambyDHCPExec
          */
					// Set IP
          case AMBBCommand::ModifyMyIPaddr:
					{
            char* IP = (char*)( network_command_buffer + media_buffer_in_32[2] - NetworkCommandAddress );
						//u32 IPLength	= (*(u32*)(media_buffer+0x2C));
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: modifyMyIPaddr({})\n", IP);
					} break;
          /*
*           0x416: recvfrom
            0x417: sendto
            0x418: recvDimmImage
            0x419: sendDimmImage
          */
          /*

            This group of commands is used to establish a link connection.
            Only used by F-Zero AX.
            Uses UDP for connections.
          */

					// Empty reply
          case AMBBCommand::InitLink:
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x601");
            break;
          case AMBBCommand::SetupLink:
					{
            struct sockaddr_in addra, addrb;
            addra.sin_addr.s_addr = media_buffer_in_32[4];
            addrb.sin_addr.s_addr = media_buffer_in_32[5];

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x606:");
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:  Size: ({}) ",   media_buffer_in_16[2] );                 // size
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:  Port: ({})",    Common::swap16(media_buffer_in_16[3]) ); // port
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:LinkNum:({:02x})",media_buffer[0x28] );                    // linknum
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:02x})",media_buffer[0x29] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:04x})",media_buffer_in_16[5] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})",    inet_ntoa( addra.sin_addr ) );           // IP
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})",    inet_ntoa( addrb.sin_addr ) );           // Target IP
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",Common::swap32(media_buffer_in_32[6]) ); // some RAM address
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",Common::swap32(media_buffer_in_32[7]) ); // some RAM address
            
						media_buffer_out_32[1] = 0;
					} break;
          /*
            This sends a UDP packet to previously defined Target IP/Port
          */
          case AMBBCommand::SearchDevices:
					{
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x607: ({})", media_buffer_in_16[2] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({})", media_buffer_in_16[3] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", media_buffer_in_32[2] );

						u8* Data = (u8*)(network_buffer + media_buffer_in_32[2] - 0x1FD00000 );
						
						for( u32 i=0; i < 0x20; i+=0x10 )
						{
							NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}",	*(u32*)(Data+i),
																																			            *(u32*)(Data+i+4),
																																			            *(u32*)(Data+i+8),
																																			            *(u32*)(Data+i+12) );
						}

            media_buffer_out_32[1] = 0;
          }
          break;
          case AMBBCommand::Unknown_614:
					{
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x614");
					}	break;
					default:
						ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: execute buffer UNKNOWN:{:03x}", *(u16*)(media_buffer+0x22) );
						break;
					}

				memset( media_buffer + 0x20, 0, 0x20 );
				return 0;
			}

			PanicAlertFmtT("Unhandled Media Board Execute:{0:08x}", *(u16*)(media_buffer + 0x22) );
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
// never reached
}
u32 GetGameType(void)
{
  u64 gameid = 0;
  // Convert game ID into hex
  if (strlen(SConfig::GetInstance().GetGameID().c_str()) > 4)
  {
    gameid = 0x30303030;
  }
  else
  {
    sscanf(SConfig::GetInstance().GetGameID().c_str(), "%s", (char*)&gameid);
  }

  // This is checking for the real game IDs (not those people made up) (See boot.id within the game)
  switch (Common::swap32((u32)gameid))
  {
  // SBGG - F-ZERO AX
  case 0x53424747:
      m_game_type = FZeroAX;
      break;
  // SBHA - F-ZERO AX (Monster)
  case 0x53424841: 
      m_game_type = FZeroAXMonster;
      break;  
  // SBKJ/SBKP - MARIOKART ARCADE GP
  case 0x53424B50:
  case 0x53424B5A:
      m_game_type = MarioKartGP;
      break;
  // SBNJ/SBNL - MARIOKART ARCADE GP2
  case 0x53424E4A:
  case 0x53424E4C:
      m_game_type = MarioKartGP2;
      break;
  // SBEJ/SBEY - Virtua Striker 2002
  case 0x5342454A:
  case 0x53424559:
      m_game_type = VirtuaStriker3;
      break;
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
      m_game_type = VirtuaStriker4;
      break;
  // SBFX/SBJN - Key of Avalon
  case 0x53424658:
  case 0x53424A4E:
      m_game_type = KeyOfAvalon;
      break;
  // SBGX - Gekitou Pro Yakyuu (DIMM Uprade 3.17 shares the same ID)
  case 0x53424758:
      m_game_type = GekitouProYakyuu;
      break;      
  default:
      PanicAlertFmtT("Unknown game ID:{0:08x}, using default controls.", gameid);
  // GSBJ/G12U/RELS/RELJ - SegaBoot (does not have a boot.id)
  case 0x4753424A:
  case 0x47313255:
  case 0x52454C53:
  case 0x52454c4a:
      m_game_type = VirtuaStriker3;
      break;
  }
	return m_game_type;
}
void Shutdown( void )
{
  m_netcfg->Close();
  m_netctrl->Close();
  m_extra->Close();
  m_backup->Close();
	m_dimm->Close();

  if (m_dimm_disc)
  {
      delete[] m_dimm_disc;
      m_dimm_disc = nullptr;
  }
}

}
