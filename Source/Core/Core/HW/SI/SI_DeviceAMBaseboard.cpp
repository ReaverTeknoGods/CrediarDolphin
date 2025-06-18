// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma warning(disable : 4189)

// TEKNOPARROT SECTION
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
static HANDLE g_jvs_file_mapping = nullptr;
static void* g_jvs_view_ptr = nullptr;
#endif
// TEKNOPARROT SECTION ENDS

#include "Core/HW/SI/SI_DeviceAMBaseboard.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
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
#include "Core/HW/DVD/AMMediaboard.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/SI/SI_DeviceGCController.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/WiiRoot.h"
#include "DiscIO/Enums.h"

// where to put baseboard debug
#define AMBASEBOARDDEBUG SERIALINTERFACE

namespace SerialInterface
{

JVSIOMessage::JVSIOMessage()
{
  m_ptr = 0;
  m_last_start = 0;
}

void JVSIOMessage::start(int node)
{
  m_last_start = m_ptr;
  u8 hdr[3] = {0xE0, (u8)node, 0};
  m_csum = 0;
  addData(hdr, 3, 1);
}

void JVSIOMessage::addData(const u8* dst, size_t len, int sync = 0)
{
  while (len--)
  {
    int c = *dst++;
    if (!sync && ((c == 0xE0) || (c == 0xD0)))
    {
      m_msg[m_ptr++] = 0xD0;
      m_msg[m_ptr++] = c - 1;
    }
    else
    {
      m_msg[m_ptr++] = c;
    }

    if (!sync)
      m_csum += c;
    sync = 0;
    if (m_ptr >= 0x80)
      PanicAlertFmt("JVSIOMessage overrun!");
  }
}

void JVSIOMessage::addData(const void* data, size_t len)
{
  addData((const u8*)data, len);
}

void JVSIOMessage::addData(const char* data)
{
  addData(data, strlen(data));
}

void JVSIOMessage::addData(u32 n)
{
  u8 cs = n;
  addData(&cs, 1);
}

void JVSIOMessage::end()
{
  u32 len = m_ptr - m_last_start;
  m_msg[m_last_start + 2] = len - 2;  // assuming len <0xD0
  addData(m_csum + len - 2);
}

static u8 CheckSumXOR(u8* Data, u32 Length)
{
  u8 check = 0;

  for (u32 i = 0; i < Length; i++)
  {
    check ^= Data[i];
  }

  return check;
}

// AM-Baseboard device on SI
CSIDevice_AMBaseboard::CSIDevice_AMBaseboard(Core::System& system, SIDevices device,
                                             int device_number)
    : ISIDevice(system, device, device_number)
{
  memset(m_coin, 0, sizeof(m_coin));

  // Setup IC-card
  m_ic_card_state = 0x20;
  m_ic_card_session = 0x23;

  m_ic_write_size = 0;
  m_ic_write_offset = 0;

  memset(m_ic_write_buffer, 0, sizeof(m_ic_write_buffer));
  memset(m_ic_card_data, 0, sizeof(m_ic_card_data));

  // Card ID
  m_ic_card_data[0x20] = 0x95;
  m_ic_card_data[0x21] = 0x71;

  if (AMMediaboard::GetGameType() == KeyOfAvalon)
  {
    m_ic_card_data[0x22] = 0x26;
    m_ic_card_data[0x23] = 0x40;
  }
  else if (AMMediaboard::GetGameType() == VirtuaStriker4)
  {
    m_ic_card_data[0x22] = 0x44;
    m_ic_card_data[0x23] = 0x00;
  }

  // Use count
  m_ic_card_data[0x28] = 0xFF;
  m_ic_card_data[0x29] = 0xFF;

  // Setup CARD
  m_card_memory_size = 0;
  m_card_is_inserted = 0;

  m_card_offset = 0;
  m_card_command = 0;
  m_card_clean = 0;

  m_card_write_length = 0;
  m_card_wrote = 0;

  m_card_read_length = 0;
  m_card_read = 0;

  m_card_bit = 0;
  m_card_state_call_count = 0;

  // Serial
  m_wheelinit = 0;

  m_motorinit = 0;
  m_motorforce_x = 0;

  m_fzdx_seatbelt = 1;
  m_fzdx_motion_stop = 0;
  m_fzdx_sensor_right = 0;
  m_fzdx_sensor_left = 0;

  m_fzcc_seatbelt = 1;
  m_fzcc_sensor = 0;
  m_fzcc_emergency = 0;
  m_fzcc_service = 0;

  memset(m_motorreply, 0, sizeof(m_motorreply));

// Initialize JVS State memory mapping
#ifdef _WIN32
  if (!g_jvs_file_mapping)
  {
    g_jvs_file_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE,  // Use paging file
                                            nullptr,               // Default security
                                            PAGE_READWRITE,        // Read/write access
                                            0,   // Maximum object size (high-order DWORD)
                                            64,  // Maximum object size (low-order DWORD) - 64 bytes
                                            "TeknoParrot_JvsState"  // Name of mapping object
    );

    if (g_jvs_file_mapping)
    {
      g_jvs_view_ptr = MapViewOfFile(g_jvs_file_mapping,   // Handle to map object
                                     FILE_MAP_ALL_ACCESS,  // Read/write permission
                                     0,                    // High-order 32 bits of file offset
                                     0,                    // Low-order 32 bits of file offset
                                     64                    // Number of bytes to map
      );
    }
  }
#endif
}

constexpr u32 SI_XFER_LENGTH_MASK = 0x7f;

// Translate [0,1,2,...,126,127] to [128,1,2,...,126,127]
constexpr s32 ConvertSILengthField(u32 field)
{
  return ((field - 1) & SI_XFER_LENGTH_MASK) + 1;
}

void CSIDevice_AMBaseboard::ICCardSendReply(ICCommand* iccommand, u8* buffer, u32* length)
{
  iccommand->length = Common::swap16(iccommand->length);
  iccommand->status = Common::swap16(iccommand->status);

  u16 crc = CheckSumXOR(iccommand->data + 2, iccommand->pktlen - 1);

  for (u32 i = 0; i < iccommand->pktlen + 1; ++i)
  {
    buffer[(*length)++] = iccommand->data[i];
  }

  buffer[(*length)++] = crc;
}

int CSIDevice_AMBaseboard::RunBuffer(u8* _pBuffer, int request_length)
{
  // Math inLength
  const auto& si = m_system.GetSerialInterface();
  u32 _iLength = ConvertSILengthField(si.GetInLength());

  // for debug logging only
  ISIDevice::RunBuffer(_pBuffer, _iLength);

  u32 iPosition = 0;
  while (iPosition < _iLength)
  {
    // read the command
    BaseBoardCommands command = static_cast<BaseBoardCommands>(_pBuffer[iPosition]);
    iPosition++;

    // handle it
    switch (command)
    {
    case CMD_RESET:  // returns ID and dip switches
    {
      u32 id = Common::swap32(SI_AM_BASEBOARD | 0x100);
      std::memcpy(_pBuffer, &id, sizeof(id));
      return sizeof(id);
    }
    break;
    case CMD_GCAM:
    {
      // calculate checksum over buffer
      u32 csum = 0;
      for (u32 i = 0; i < _iLength; ++i)
        csum += _pBuffer[i];

      u8 res[0x80];
      u32 resp = 0;

      u32 real_len = _pBuffer[iPosition];
      u32 p = 2;

      static u32 d10_1 = 0xFE;
      static u32 d10_0 = 0xFF;

      memset(res, 0, sizeof(res));
      res[resp++] = 1;
      res[resp++] = 1;

#define ptr(x) _pBuffer[(p + x)]
      while (p < real_len + 2)
      {
        switch (ptr(0))
        {
        case 0x10:
        {
          DEBUG_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 10, {:02x} (READ STATUS&SWITCHES)",
                        ptr(1));

          GCPadStatus PadStatus;
          PadStatus = Pad::GetStatus(ISIDevice::m_device_number);
          res[resp++] = 0x10;
          res[resp++] = 0x2;

          /*baseboard test/service switches ???, disabled for a while
          if (PadStatus.button & PAD_BUTTON_Y)	// Test
            d10_0 &= ~0x80;
          if (PadStatus.button & PAD_BUTTON_X)	// Service
            d10_0 &= ~0x40;
          */

          // Horizontal Scanning Frequency switch
          // Required for F-Zero AX booting via Sega Boot
          d10_0 &= ~0x20;

          res[resp++] = d10_0;
          res[resp++] = d10_1;
          break;
        }
        case 0x11:
        {
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 11, {:02x} (READ SERIAL NR)", ptr(1));
          char string[] = "AADE-01B98394904";
          res[resp++] = 0x11;
          res[resp++] = 0x10;
          memcpy(res + resp, string, 0x10);
          resp += 0x10;
          break;
        }
        case 0x12:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 12, {:02x} {:02x}", ptr(1), ptr(2));
          res[resp++] = 0x12;
          res[resp++] = 0x00;
          break;
        case 0x14:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 14, {:02x} {:02x}", ptr(1), ptr(2));
          res[resp++] = 0x14;
          res[resp++] = 0x00;
          break;
        case 0x15:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 15, {:02x} (READ FIRM VERSION)", ptr(1));
          res[resp++] = 0x15;
          res[resp++] = 0x02;
          // FIRM VERSION
          // 00.26
          res[resp++] = 0x00;
          res[resp++] = 0x26;
          break;
        case 0x16:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 16, {:02x} (READ FPGA VERSION)", ptr(1));
          res[resp++] = 0x16;
          res[resp++] = 0x02;
          // FPGA VERSION
          // 07.06
          res[resp++] = 0x07;
          res[resp++] = 0x06;
          break;
        case 0x1f:
        {
          // Used by SegaBoot for region checks (dev mode skips this check)
          // In some games this also controls the displayed language
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG,
                         "GC-AM: Command 1f, {:02x} {:02x} {:02x} {:02x} {:02x} (REGION)", ptr(1),
                         ptr(2), ptr(3), ptr(4), ptr(5));
          u8 string[] = "\x00\x00\x30\x00"
                        //   "\x01\xfe\x00\x00"  // JAPAN
                        "\x02\xfd\x00\x00"  // USA
                        // "\x03\xfc\x00\x00"  // export
                        "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
          res[resp++] = 0x1f;
          res[resp++] = 0x14;

          for (int i = 0; i < 0x14; ++i)
            res[resp++] = string[i];
          p += 5;
        }
        break;
        /* No reply */
        case 0x21:
        {
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 0x21, {:02x}", ptr(1));
          resp += ptr(1) + 2;
        }
        break;
        /* No reply */
        case 0x22:
        {
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 0x22, {:02x}", ptr(1));
          resp += ptr(1) + 2;
        }
        break;
        case 0x23:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 0x23, {:02x} {:02x}", ptr(1), ptr(2));
          if (ptr(1))
          {
            res[resp++] = 0x23;
            res[resp++] = 0x00;
          }
          else
          {
            res[resp++] = 0x23;
            res[resp++] = 0x00;
          }
          break;
        case 0x24:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 0x24, {:02x} {:02x}", ptr(1), ptr(2));
          if (ptr(1))
          {
            res[resp++] = 0x24;
            res[resp++] = 0x00;
          }
          else
          {
            res[resp++] = 0x24;
            res[resp++] = 0x00;
          }
          break;
        case 0x31:
        {
          if (ptr(1))
          {
            NOTICE_LOG_FMT(AMBASEBOARDDEBUG,
                           "GC-AM: Command 31 {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}{:02x} "
                           "{:02x}{:02x} {:02x}{:02x} {:02x} {:02x} {:02x}",
                           ptr(1), ptr(2), ptr(3), ptr(4), ptr(5), ptr(6), ptr(7), ptr(8), ptr(9),
                           ptr(10), ptr(11), ptr(12), ptr(13), ptr(14));

            // Serial - Wheel
            if (AMMediaboard::GetGameType() == MarioKartGP ||
                AMMediaboard::GetGameType() == MarioKartGP2)
            {
              INFO_LOG_FMT(AMBASEBOARDDEBUG,
                           "GC-AM: Command 31 (WHEEL) {:02x}{:02x} {:02x}{:02x} {:02x} {:02x} "
                           "{:02x} {:02x} {:02x} {:02x}",
                           ptr(2), ptr(3), ptr(4), ptr(5), ptr(6), ptr(7), ptr(8), ptr(9), ptr(10),
                           ptr(11));

              res[resp++] = 0x31;
              res[resp++] = 0x03;

              switch (m_wheelinit)
              {
              case 0:
                res[resp++] = 'E';  // Error
                res[resp++] = '0';
                res[resp++] = '0';
                m_wheelinit++;
                break;
              case 1:
                res[resp++] = 'C';  // Power Off
                res[resp++] = '0';
                res[resp++] = '6';
                // Only turn on when a wheel is connected
                if (si.GetDeviceType(1) == SerialInterface::SIDEVICE_GC_STEERING)
                {
                  m_wheelinit++;
                }
                break;
              case 2:
                res[resp++] = 'C';  // Power On
                res[resp++] = '0';
                res[resp++] = '1';
                break;
              default:
                break;
              }
              /*
              u16 CenteringForce= ptr(6);
              u16 FrictionForce = ptr(8);
              u16 Roll          = ptr(10);
              */
              break;
            }

            // Serial Unknown
            if (AMMediaboard::GetGameType() == GekitouProYakyuu)
            {
              u32 cmd = ptr(2) << 24;
              cmd |= ptr(3) << 16;
              cmd |= ptr(4) << 8;
              cmd |= ptr(5);

              if (cmd == 0x00100000)
              {
                res[resp++] = 0x31;
                res[resp++] = 0x03;
                res[resp++] = 1;
                res[resp++] = 2;
                res[resp++] = 3;
                break;
              }
              break;
            }

            // Serial IC-CARD
            if (AMMediaboard::GetGameType() == VirtuaStriker4 ||
                AMMediaboard::GetGameType() == KeyOfAvalon)
            {
              u32 cmd = ptr(3);

              ICCommand icco;

              // Set default reply
              icco.pktcmd = 0x31;
              icco.pktlen = 7;
              icco.fixed = 0x10;
              icco.command = cmd;
              icco.length = 2;
              icco.status = 0;
              icco.extlen = 0;

              // Check for rest of data from the write pages command
              if (m_ic_write_size && m_ic_write_offset)
              {
                u32 size = ptr(1);

                char logptr[1024];
                char* log = logptr;

                for (u32 i = 0; i < (u32)(ptr(1) + 2); ++i)
                {
                  log += sprintf(log, "%02X ", ptr(i));
                }

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "Command: {}", logptr);

                INFO_LOG_FMT(
                    AMBASEBOARDDEBUG,
                    "GC-AM: Command 31 (IC-CARD) Write Pages: Off:{:x} Size:{:x} PSize:{:x}",
                    m_ic_write_offset, m_ic_write_size, size);

                memcpy(m_ic_write_buffer + m_ic_write_offset, _pBuffer + p + 2, size);

                m_ic_write_offset += size;

                if (m_ic_write_offset > m_ic_write_size)
                {
                  m_ic_write_offset = 0;

                  u16 page = m_ic_write_buffer[7];
                  u16 count = m_ic_write_buffer[9];

                  memcpy(m_ic_card_data + page * 8, m_ic_write_buffer + 10, count * 8);

                  INFO_LOG_FMT(AMBASEBOARDDEBUG,
                               "GC-AM: Command 31 (IC-CARD) Write Pages:{} Count:{}({:x})", page,
                               count, size);

                  icco.command = WritePages;

                  ICCardSendReply(&icco, res, &resp);
                }
                break;
              }

              switch (ICCARDCommands(cmd))
              {
              case GetStatus:
              {
                icco.status = m_ic_card_state;

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Get Status:{:02x}",
                             m_ic_card_state);
              }
              break;
              case SetBaudrate:
                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Set Baudrate");
                break;
              case FieldOn:
              {
                m_ic_card_state |= 0x10;
                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Field On");
              }
              break;
              case InsertCheck:
                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Insert Check");
                break;
              case AntiCollision:
              {
                icco.extlen = 8;
                icco.length += icco.extlen;
                icco.pktlen += icco.extlen;

                // Card ID
                icco.extdata[0] = 0x00;
                icco.extdata[1] = 0x00;
                icco.extdata[2] = 0x54;
                icco.extdata[3] = 0x4D;
                icco.extdata[4] = 0x50;
                icco.extdata[5] = 0x00;
                icco.extdata[6] = 0x00;
                icco.extdata[7] = 0x00;

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Anti Collision");
              }
              break;
              case SelectCard:
              {
                icco.extlen = 8;
                icco.length += icco.extlen;
                icco.pktlen += icco.extlen;

                // Session
                icco.extdata[0] = 0x00;
                icco.extdata[1] = m_ic_card_session;
                icco.extdata[2] = 0x00;
                icco.extdata[3] = 0x00;
                icco.extdata[4] = 0x00;
                icco.extdata[5] = 0x00;
                icco.extdata[6] = 0x00;
                icco.extdata[7] = 0x00;

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Select Card:{}",
                             m_ic_card_session);
              }
              break;
              case ReadPage:
              case ReadUseCount:
              {
                u16 page = (ptr(8) << 8) | ptr(9);

                icco.extlen = 8;
                icco.length += icco.extlen;
                icco.pktlen += icco.extlen;

                memcpy(icco.extdata, m_ic_card_data + page * 8, 8);

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Read Page:{}", page);
              }
              break;
              case WritePage:
              {
                u16 page = (ptr(10) << 8) | ptr(11);

                // Read Only Page
                if (!(page == 4))
                {
                  memcpy(m_ic_card_data + page * 8, _pBuffer + p + 12, 8);
                }

                // status
                if (page == 4)  // Read Only Page
                {
                  icco.status = 0x80;
                }

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Write Page:{}", page);
              }
              break;
              case DecreaseUseCount:
              {
                u16 page = (ptr(10) << 8) | ptr(11);

                icco.extlen = 2;
                icco.length += icco.extlen;
                icco.pktlen += icco.extlen;

                *(u16*)(m_ic_card_data + 0x28) = *(u16*)(m_ic_card_data + 0x28) - 1;

                // Counter
                icco.extdata[0] = m_ic_card_data[0x28];
                icco.extdata[1] = m_ic_card_data[0x29];

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Decrease Use Count:{}",
                             page);
              }
              break;
              case ReadPages:
              {
                u16 page = (ptr(8) << 8) | ptr(9);
                u16 count = (ptr(10) << 8) | ptr(11);

                u32 offs = page * 8;
                u32 cnt = count * 8;

                // Limit read size to not overwrite the reply buffer
                if (cnt > (u32)0x50 - resp)
                {
                  cnt = 5 * 8;
                }

                icco.extlen = cnt;
                icco.length += icco.extlen;
                icco.pktlen += icco.extlen;

                memcpy(icco.extdata, m_ic_card_data + offs, cnt);

                INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (IC-CARD) Read Pages:{} Count:{}",
                             page, count);
              }
              break;
              case WritePages:
              {
                u16 page = (ptr(8) << 8) | ptr(9);
                u16 count = (ptr(10) << 8) | ptr(11);
                u16 pksize = ptr(1);
                u16 size = ptr(5);

                // We got a complete packet
                if (pksize - 5 == size)
                {
                  // status
                  if (page == 4)  // Read Only Page
                  {
                    icco.status = 0x80;
                  }
                  else
                  {
                    memcpy(m_ic_card_data + page * 8, _pBuffer + p + 13, count * 8);
                  }

                  INFO_LOG_FMT(AMBASEBOARDDEBUG,
                               "GC-AM: Command 31 (IC-CARD) Write Pages:{} Count:{}({:x})", page,
                               count, size);
                }
                // VS4 splits the writes over multiple packets
                else
                {
                  memcpy(m_ic_write_buffer, _pBuffer + p + 2, pksize);
                  m_ic_write_offset += pksize;
                  m_ic_write_size = size;
                }
              }
              break;
              default:
                WARN_LOG_FMT(AMBASEBOARDDEBUG,
                             "GC-AM: Command 31 (IC-Card) {:02x} {:02x} {:02x} {:02x} {:02x} "
                             "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                             ptr(3), ptr(4), ptr(5), ptr(6), ptr(7), ptr(8), ptr(9), ptr(10),
                             ptr(11), ptr(12), ptr(13), ptr(14));
                break;
              }

              ICCardSendReply(&icco, res, &resp);

              break;
            }
          }

          u32 cmd_off = 0;
          // Command Length
          while (cmd_off < ptr(1))
          {
            // All commands are OR'd with 0x80
            // Last byte (ptr(5)) is checksum which we don't care about
            u32 cmd = 0;
            if (AMMediaboard::GetGameType() == FZeroAX ||
                AMMediaboard::GetGameType() == FZeroAXMonster)
            {
              cmd = ptr(cmd_off + 2) << 24;
              cmd |= ptr(cmd_off + 3) << 16;
              cmd |= ptr(cmd_off + 4) << 8;
              cmd |= ptr(cmd_off + 5);
              cmd ^= 0x80000000;

              INFO_LOG_FMT(AMBASEBOARDDEBUG,
                           "GC-AM: Command 31 (MOTOR) Length:{:02x} Command:{:06x}({:02x})", ptr(1),
                           cmd >> 8, cmd & 0xFF);
            }
            else
            {
              cmd = (ptr(2) ^ 0x80) << 16;
              cmd |= ptr(3) << 8;
              cmd |= ptr(4);

              INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (SERIAL) Command:{:06x}", cmd);

              if (/*cmd == 0xf43200 || */ cmd == 0x801000)
              {
                // u32 PC = m_system.GetPowerPC().GetPPCState().pc;

                // INFO_LOG_FMT(AMBASEBOARDDEBUG, "GCAM: PC:{:08x}", PC);

                // m_system.GetPowerPC().GetBreakPoints().Add(PC + 8, true, true, std::nullopt);

                res[resp++] = 0x31;
                res[resp++] = 0x02;
                res[resp++] = 0xFF;
                res[resp++] = 0x01;
              }
            }

            cmd_off += 4;

            if (AMMediaboard::GetGameType() == FZeroAX ||
                AMMediaboard::GetGameType() == FZeroAXMonster)
            {
              // Status
              m_motorreply[cmd_off + 2] = 0;
              m_motorreply[cmd_off + 3] = 0;

              // error
              m_motorreply[cmd_off + 4] = 0;

              switch (cmd >> 24)
              {
              case 0:
                break;
              case 1:  // Set Maximum?
                break;
              case 2:
                break;
                /*
                  0x00-0x40: left
                  0x40-0x80: right
                */
              case 4:  // Move Steering Wheel
                // Left
                if (cmd & 0x010000)
                {
                  m_motorforce_x = -((s16)cmd & 0xFF00);
                }
                else  // Right
                {
                  m_motorforce_x = (cmd - 0x4000) & 0xFF00;
                }

                m_motorforce_x *= 2;

                // FFB?
                if (m_motorinit == 2)
                {
                  if (si.GetDeviceType(1) == SerialInterface::SIDEVICE_GC_STEERING)
                  {
                    GCPadStatus PadStatus;
                    PadStatus = Pad::GetStatus(1);
                    if (PadStatus.isConnected)
                    {
                      ControlState mapped_strength = (double)(m_motorforce_x >> 8);
                      mapped_strength /= 127.f;
                      Pad::Rumble(1, mapped_strength);
                      INFO_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 31 (MOTOR) mapped_strength:{}",
                                   mapped_strength);
                    }
                  }
                }
                break;
              case 6:  // nice
              case 9:
              default:
                break;
              case 7:
                // switch back to normal controls
                m_motorinit = 2;
                break;
              // reset
              case 0x7F:
                m_motorinit = 1;
                memset(m_motorreply, 0, sizeof(m_motorreply));
                break;
              }

              // Checksum
              m_motorreply[cmd_off + 5] =
                  m_motorreply[cmd_off + 2] ^ m_motorreply[cmd_off + 3] ^ m_motorreply[cmd_off + 4];
            }
          }

          if (ptr(1) == 0)
          {
            res[resp++] = 0x31;
            res[resp++] = 0x00;
          }
          else
          {
            if (m_motorinit)
            {
              // Motor
              m_motorreply[0] = 0x31;
              m_motorreply[1] = ptr(1);  // Same Out as In size

              memcpy(res + resp, m_motorreply, m_motorreply[1] + 2);
              resp += m_motorreply[1] + 2;
            }
          }
        }
        break;
        case 0x32:
          //	NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 32 (CARD-Interface)");
          if (ptr(1))
          {
            if (ptr(1) == 1 && ptr(2) == 0x05)
            {
              if (m_card_read_length)
              {
                res[resp++] = 0x32;
                u32 ReadLength = m_card_read_length - m_card_read;

                if (AMMediaboard::GetGameType() == FZeroAX)
                {
                  if (ReadLength > 0x2F)
                    ReadLength = 0x2F;
                }

                res[resp++] = ReadLength;  // 0x2F (max size per packet)

                memcpy(res + resp, m_card_read_packet + m_card_read, ReadLength);

                resp += ReadLength;
                m_card_read += ReadLength;

                if (m_card_read >= m_card_read_length)
                  m_card_read_length = 0;

                break;
              }

              res[resp++] = 0x32;
              u32 CMDLenO = resp;
              res[resp++] = 0x00;  // len

              res[resp++] = 0x02;  //
              u32 ChkStart = resp;

              res[resp++] = 0x00;  // 0x00 len

              res[resp++] = m_card_command;  // 0x01 cmd

              switch (CARDCommands(m_card_command))
              {
              case CARDCommands::Init:
                res[resp++] = 0x00;  // 0x02
                res[resp++] = 0x30;  // 0x03
                break;
              case CARDCommands::GetState:
                res[resp++] = 0x20 | m_card_bit;  // 0x02
                /*
                  bit 0: Please take your card
                  bit 1: endless waiting causes UNK_E to be called
                */
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::Read:
                res[resp++] = 0x02;  // 0x02
                res[resp++] = 0x53;  // 0x03
                break;
              case CARDCommands::IsPresent:
                res[resp++] = 0x22;  // 0x02
                res[resp++] = 0x30;  // 0x03
                break;
              case CARDCommands::Write:
                res[resp++] = 0x02;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::SetPrintParam:
                res[resp++] = 0x00;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::RegisterFont:
                res[resp++] = 0x00;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::WriteInfo:
                res[resp++] = 0x02;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::Eject:
                if (AMMediaboard::GetGameType() == FZeroAX)
                {
                  res[resp++] = 0x01;  // 0x02
                }
                else
                {
                  res[resp++] = 0x31;  // 0x02
                }
                res[resp++] = 0x30;  // 0x03
                break;
              case CARDCommands::Clean:
                res[resp++] = 0x02;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              case CARDCommands::Load:
                res[resp++] = 0x02;  // 0x02
                res[resp++] = 0x30;  // 0x03
                break;
              case CARDCommands::SetShutter:
                res[resp++] = 0x00;  // 0x02
                res[resp++] = 0x00;  // 0x03
                break;
              }

              res[resp++] = 0x30;  // 0x04
              res[resp++] = 0x00;  // 0x05

              res[resp++] = 0x03;  // 0x06

              res[ChkStart] = resp - ChkStart;  // 0x00 len

              u32 i;
              res[resp] = 0;  // 0x07
              for (i = 0; i < res[ChkStart]; ++i)
                res[resp] ^= res[ChkStart + i];

              resp++;

              res[CMDLenO] = res[ChkStart] + 2;
            }
            else
            {
              for (u32 i = 0; i < ptr(1); ++i)
                m_card_buffer[m_card_offset + i] = ptr(2 + i);

              m_card_offset += ptr(1);

              // Check if we got a complete command

              if (m_card_buffer[0] == 0x02)
                if (m_card_buffer[1] == m_card_offset - 2)
                {
                  if (m_card_buffer[m_card_offset - 2] == 0x03)
                  {
                    m_card_command = m_card_buffer[2];

                    switch (CARDCommands(m_card_command))
                    {
                    case CARDCommands::Init:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Init");

                      m_card_write_length = 0;
                      m_card_bit = 0;
                      m_card_memory_size = 0;
                      m_card_state_call_count = 0;
                      break;
                    case CARDCommands::GetState:
                    {
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD GetState({:02X})",
                                     m_card_bit);

                      if (m_card_memory_size == 0)
                      {
                        std::string card_filename(File::GetUserPath(D_TRIUSER_IDX) + "tricard_" +
                                                  SConfig::GetInstance().GetGameID().c_str() +
                                                  ".bin");

                        if (File::Exists(card_filename))
                        {
                          File::IOFile card = File::IOFile(card_filename, "rb+");
                          m_card_memory_size = (u32)card.GetSize();

                          card.ReadBytes(m_card_memory, m_card_memory_size);
                          card.Close();

                          m_card_is_inserted = 1;
                        }
                      }

                      if (AMMediaboard::GetGameType() == FZeroAX && m_card_memory_size)
                      {
                        m_card_state_call_count++;
                        if (m_card_state_call_count > 10)
                        {
                          if (m_card_bit & 2)
                            m_card_bit &= ~2;
                          else
                            m_card_bit |= 2;

                          m_card_state_call_count = 0;
                        }
                      }

                      if (m_card_clean == 1)
                      {
                        m_card_clean = 2;
                      }
                      else if (m_card_clean == 2)
                      {
                        std::string card_filename(File::GetUserPath(D_TRIUSER_IDX) + "tricard_" +
                                                  SConfig::GetInstance().GetGameID().c_str() +
                                                  ".bin");

                        if (File::Exists(card_filename))
                        {
                          m_card_memory_size = (u32)File::GetSize(card_filename);
                          if (m_card_memory_size)
                          {
                            if (AMMediaboard::GetGameType() == FZeroAX)
                            {
                              m_card_bit = 2;
                            }
                            else
                            {
                              m_card_bit = 1;
                            }
                          }
                        }
                        m_card_clean = 0;
                      }
                      break;
                    }
                    case CARDCommands::IsPresent:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD IsPresent");
                      break;
                    case CARDCommands::RegisterFont:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD RegisterFont");
                      break;
                    case CARDCommands::Load:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Load");
                      break;
                    case CARDCommands::Clean:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Clean");
                      m_card_clean = 1;
                      break;
                    case CARDCommands::Read:
                    {
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Read");
                      // Prepare read packet
                      memset(m_card_read_packet, 0, 0xDB);
                      u32 POff = 0;

                      std::string card_filename(File::GetUserPath(D_TRIUSER_IDX) + "tricard_" +
                                                SConfig::GetInstance().GetGameID().c_str() +
                                                ".bin");

                      if (File::Exists(card_filename))
                      {
                        File::IOFile card = File::IOFile(card_filename, "rb+");
                        if (m_card_memory_size == 0)
                        {
                          m_card_memory_size = (u32)card.GetSize();
                        }

                        card.ReadBytes(m_card_memory, m_card_memory_size);
                        card.Close();

                        m_card_is_inserted = 1;
                      }

                      m_card_read_packet[POff++] = 0x02;  // SUB CMD
                      m_card_read_packet[POff++] = 0x00;  // SUB CMDLen

                      m_card_read_packet[POff++] = 0x33;  // CARD CMD

                      if (m_card_is_inserted)  // CARD Status
                      {
                        m_card_read_packet[POff++] = 0x31;
                      }
                      else
                      {
                        m_card_read_packet[POff++] = 0x30;
                      }

                      m_card_read_packet[POff++] = 0x30;  //
                      m_card_read_packet[POff++] = 0x30;  //

                      // Data reply
                      memcpy(m_card_read_packet + POff, m_card_memory, m_card_memory_size);
                      POff += m_card_memory_size;

                      m_card_read_packet[POff++] = 0x03;

                      m_card_read_packet[1] = POff - 1;  // SUB CMDLen

                      u32 i;
                      for (i = 0; i < POff - 1; ++i)
                        m_card_read_packet[POff] ^= m_card_read_packet[1 + i];

                      POff++;

                      m_card_read_length = POff;
                      m_card_read = 0;
                      break;
                    }
                    case CARDCommands::Write:
                    {
                      m_card_memory_size = m_card_buffer[1] - 9;

                      memcpy(m_card_memory, m_card_buffer + 9, m_card_memory_size);

                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Write: {}",
                                     m_card_memory_size);

                      std::string card_filename(File::GetUserPath(D_TRIUSER_IDX) + "tricard_" +
                                                SConfig::GetInstance().GetGameID().c_str() +
                                                ".bin");

                      File::IOFile card = File::IOFile(card_filename, "wb+");
                      card.WriteBytes(m_card_memory, m_card_memory_size);
                      card.Close();

                      m_card_bit = 2;

                      m_card_state_call_count = 0;
                      break;
                    }
                    case CARDCommands::SetPrintParam:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD SetPrintParam");
                      break;
                    case CARDCommands::WriteInfo:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD WriteInfo");
                      break;
                    case CARDCommands::Erase:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Erase");
                      break;
                    case CARDCommands::Eject:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD Eject");
                      if (AMMediaboard::GetGameType() != FZeroAX)
                      {
                        m_card_bit = 0;
                      }
                      break;
                    case CARDCommands::SetShutter:
                      NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command CARD SetShutter");
                      if (AMMediaboard::GetGameType() != FZeroAX)
                      {
                        m_card_bit = 0;
                      }
                      break;
                    default:
                      ERROR_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: CARD:Unhandled cmd!");
                      ERROR_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: CARD:[{:08X}]", m_card_command);
                      // hexdump( m_card_buffer, m_card_offset );
                      break;
                    }
                    m_card_offset = 0;
                  }
                }

              res[resp++] = 0x32;
              res[resp++] = 0x01;  // len
              res[resp++] = 0x06;  // OK
            }
          }
          else
          {
            res[resp++] = 0x32;
            res[resp++] = 0x00;  // len
          }
          break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
        {
          DEBUG_LOG_FMT(
              AMBASEBOARDDEBUG,
              "GC-AM: Command {:02x}, {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} (JVS IO)",
              ptr(0), ptr(1), ptr(2), ptr(3), ptr(4), ptr(5), ptr(6), ptr(7));
          int pptr = 2;
          JVSIOMessage msg;

          msg.start(0);
          msg.addData(1);

          static u8 RXReply = 0;
          static int delay = 0;

          unsigned char jvs_io_buffer[0x80];
          int nr_bytes = ptr(pptr + 2);  // byte after E0 xx
          int jvs_io_length = 0;

          for (int i = 0; i < nr_bytes + 3; ++i)
            jvs_io_buffer[jvs_io_length++] = ptr(pptr + i);

          int node = jvs_io_buffer[1];

          unsigned char* jvs_io = jvs_io_buffer + 3;
          jvs_io_length--;  // checksum

          while (jvs_io < (jvs_io_buffer + jvs_io_length))
          {
            int cmd = *jvs_io++;
            DEBUG_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:node={}, command={:02x}", node, cmd);

            switch (JVSIOCommands(cmd))
            {
            case JVSIOCommands::IOID:
              msg.addData(1);
              switch (AMMediaboard::GetGameType())
              {
              case FZeroAX:
              case FZeroAXMonster:
                // Specific version that enables DX mode on AX machines
                msg.addData("SEGA ENTERPRISES,LTD.;837-13844-01 I/O CNTL BD2 ;");
                break;
              case MarioKartGP:
              case MarioKartGP2:
              default:
                msg.addData("namco ltd.;FCA-1;Ver1.01;JPN,Multipurpose + Rotary Encoder");
                break;
              case VirtuaStriker3:
              case VirtuaStriker4:
                msg.addData("SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551;Ver1.00");
                break;
              }
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO: Command 10, BoardID");
              msg.addData((u32)0);
              break;
            case JVSIOCommands::CommandRevision:
              msg.addData(1);
              msg.addData(0x11);
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO: Command 11, CommandRevision");
              break;
            case JVSIOCommands::JVRevision:
              msg.addData(1);
              msg.addData(0x20);
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:  Command 12, JVRevision");
              break;
            case JVSIOCommands::CommunicationVersion:
              msg.addData(1);
              msg.addData(0x10);
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:  Command 13, CommunicationVersion");
              break;

              // Slave features
              /*
                0x01: Player count, Bit per channel
                0x02: Coin slots
                0x03: Analog-in
                0x04: Rotary
                0x05: Keycode
                0x06: Screen, x, y, ch
                ....: unused
                0x10: Card
                0x11: Hopper-out
                0x12: Driver-out
                0x13: Analog-out
                0x14: Character, Line (?)
                0x15: Backup
              */
            case JVSIOCommands::CheckFunctionality:
              msg.addData(1);
              switch (AMMediaboard::GetGameType())
              {
              case FZeroAX:
              case FZeroAXMonster:
                // 2 Player (12bit) (p2=paddles), 1 Coin slot, 6 Analog-in
                // msg.addData((void *)"\x01\x02\x0C\x00", 4);
                // msg.addData((void *)"\x02\x01\x00\x00", 4);
                // msg.addData((void *)"\x03\x06\x00\x00", 4);
                // msg.addData((void *)"\x00\x00\x00\x00", 4);

                /*
                  01 02 0c 00
                  02 02 00 00
                  03 08 00 00
                  12 16 00 00
                */

                // DX Version: 2 Player (22bit) (p2=paddles), 2 Coin slot, 8 Analog-in,
                // 22 Driver-out
                msg.addData((void*)"\x01\x02\x12\x00", 4);
                msg.addData((void*)"\x02\x02\x00\x00", 4);
                msg.addData((void*)"\x03\x08\x0A\x00", 4);
                msg.addData((void*)"\x12\x16\x00\x00", 4);
                msg.addData((void*)"\x00\x00\x00\x00", 4);
                break;
              case VirtuaStriker3:
              case GekitouProYakyuu:
                // 2 Player (13bit), 2 Coin slot, 4 Analog-in, 1 CARD, 8 Driver-out
                msg.addData((void*)"\x01\x02\x0D\x00", 4);
                msg.addData((void*)"\x02\x02\x00\x00", 4);
                msg.addData((void*)"\x03\x04\x00\x00", 4);
                msg.addData((void*)"\x10\x01\x00\x00", 4);
                msg.addData((void*)"\x12\x08\x00\x00", 4);
                msg.addData((void*)"\x00\x00\x00\x00", 4);
                break;
              case MarioKartGP:
              case MarioKartGP2:
              default:
                // 1 Player (15bit), 1 Coin slot, 3 Analog-in, 1 CARD, 1 Driver-out
                msg.addData((void*)"\x01\x01\x0F\x00", 4);
                msg.addData((void*)"\x02\x01\x00\x00", 4);
                msg.addData((void*)"\x03\x03\x00\x00", 4);
                msg.addData((void*)"\x10\x01\x00\x00", 4);
                msg.addData((void*)"\x12\x01\x00\x00", 4);
                msg.addData((void*)"\x00\x00\x00\x00", 4);
                break;
              case VirtuaStriker4:
                // 2 Player (13bit), 1 Coin slot, 4 Analog-in, 1 CARD
                msg.addData((void*)"\x01\x02\x0D\x00", 4);
                msg.addData((void*)"\x02\x01\x00\x00", 4);
                msg.addData((void*)"\x03\x04\x00\x00", 4);
                msg.addData((void*)"\x10\x01\x00\x00", 4);
                msg.addData((void*)"\x00\x00\x00\x00", 4);
                break;
              }
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:  Command 14, CheckFunctionality");
              break;
            case JVSIOCommands::MainID:
              while (*jvs_io++)
              {
              };
              msg.addData(1);
              break;
            case JVSIOCommands::SwitchesInput:
            {
              int player_count = *jvs_io++;
              int player_byte_count = *jvs_io++;

              DEBUG_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:  Command 20, SwitchInputs: {} {}",
                            player_count, player_byte_count);

              msg.addData(1);

              // Read digital button control from shared memory (StateView[8] - DWORD)
              u32 control = 0;
#ifdef _WIN32
              if (g_jvs_view_ptr)
              {
                control = *reinterpret_cast<u32*>(static_cast<u8*>(g_jvs_view_ptr) + 8);
              }
#endif

              /*
              Full 32-bit DWORD mapping:
              0x01:       Coin (shared)
              0x02:       Player 1 Start
              0x04:       Player 1 Button1 (Primary: Boost/Long Pass/Item/A)
              0x08:       Player 2 Start
              0x10:       Player 2 Button1 (Primary: Boost/Long Pass/Item/A)
              0x20:       Player 1 Button2 (Secondary: Paddle Right/Short Pass/VS-Cancel/B)
              0x40:       Player 1 Service
              0x80:       Player 2 Button2 (Secondary: Paddle Right/Short Pass/VS-Cancel/B)
              0x100:      Player 2 Service
              0x200:      Player 1 Button3 (Tertiary: Shoot/Dash/Gekitou)
              0x400:      Player 1 Left
              0x800:      Player 1 Up
              0x1000:     Player 1 Right
              0x2000:     Player 1 Down
              0x4000:     Player 2 Button3 (Tertiary: Shoot/Dash/Gekitou)
              0x8000:     Player 2 Left
              0x10000:    Player 2 Up
              0x20000:    Player 2 Right
              0x40000:    Player 2 Down
              0x80000:    Player 1 Button4 (View Change 1/Tactics U)
              0x100000:   Player 1 Button5 (View Change 2/Tactics M)
              0x200000:   Player 1 Button6 (View Change 3/Tactics D)
              0x400000:   Player 1 Button7 (View Change 4/IC-Card Lock)
              0x800000:   Player 2 Button4 (View Change 1/Tactics U)
              0x1000000:  Player 2 Button5 (View Change 2/Tactics M)
              0x2000000:  Player 2 Button6 (View Change 3/Tactics D)
              0x4000000:  Player 2 Button7 (View Change 4/IC-Card Lock)
              0x8000000:  Player 1 Button8 (Paddle Left)
              0x10000000: Player 1 Button9 (Extra)
              0x20000000: Player 2 Button8 (Paddle Left)
              0x40000000: Player 2 Button9 (Extra)
              0x80000000: Reserved
              */

              // Test button - check for coin input from shared memory only
              if (control & 0x01)
                msg.addData(0x80);
              else
                msg.addData((u32)0x00);

              for (int i = 0; i < player_count; ++i)
              {
                unsigned char player_data[3] = {0, 0, 0};

                // Extract button states from shared memory based on player index
                bool start_pressed = false;
                bool service_pressed = false;
                bool button1_pressed = false;  // Primary action
                bool button2_pressed = false;  // Secondary action
                bool button3_pressed = false;  // Tertiary action
                bool left_pressed = false;
                bool up_pressed = false;
                bool right_pressed = false;
                bool down_pressed = false;
                bool button4_pressed = false;  // View Change 1/Tactics U
                bool button5_pressed = false;  // View Change 2/Tactics M
                bool button6_pressed = false;  // View Change 3/Tactics D
                bool button7_pressed = false;  // View Change 4/IC-Card Lock
                bool button8_pressed = false;  // Paddle Left
                bool button9_pressed = false;  // Extra

                if (i == 0)  // Player 1
                {
                  start_pressed = (control & 0x02) != 0;
                  service_pressed = (control & 0x40) != 0;
                  button1_pressed = (control & 0x04) != 0;
                  button2_pressed = (control & 0x20) != 0;
                  button3_pressed = (control & 0x200) != 0;
                  left_pressed = (control & 0x400) != 0;
                  up_pressed = (control & 0x800) != 0;
                  right_pressed = (control & 0x1000) != 0;
                  down_pressed = (control & 0x2000) != 0;
                  button4_pressed = (control & 0x80000) != 0;
                  button5_pressed = (control & 0x100000) != 0;
                  button6_pressed = (control & 0x200000) != 0;
                  button7_pressed = (control & 0x400000) != 0;
                  button8_pressed = (control & 0x8000000) != 0;
                  button9_pressed = (control & 0x10000000) != 0;
                }
                else if (i == 1)  // Player 2
                {
                  start_pressed = (control & 0x08) != 0;
                  service_pressed = (control & 0x100) != 0;
                  button1_pressed = (control & 0x10) != 0;
                  button2_pressed = (control & 0x80) != 0;
                  button3_pressed = (control & 0x4000) != 0;
                  left_pressed = (control & 0x8000) != 0;
                  up_pressed = (control & 0x10000) != 0;
                  right_pressed = (control & 0x20000) != 0;
                  down_pressed = (control & 0x40000) != 0;
                  button4_pressed = (control & 0x800000) != 0;
                  button5_pressed = (control & 0x1000000) != 0;
                  button6_pressed = (control & 0x2000000) != 0;
                  button7_pressed = (control & 0x4000000) != 0;
                  button8_pressed = (control & 0x20000000) != 0;
                  button9_pressed = (control & 0x40000000) != 0;
                }
                // Player 3+ would need additional DWORD or different mapping

                switch (AMMediaboard::GetGameType())
                {
                // Controller configuration for F-Zero AX (DX)
                case FZeroAX:
                  if (i == 0)
                  {
                    if (m_fzdx_seatbelt)
                    {
                      player_data[0] |= 0x01;
                    }

                    // Start
                    if (start_pressed)
                      player_data[0] |= 0x80;
                    // Service button
                    if (service_pressed)
                      player_data[0] |= 0x40;
                    // Boost
                    if (button1_pressed)
                      player_data[0] |= 0x02;
                    // View Change 1
                    if (button4_pressed)
                      player_data[0] |= 0x20;
                    // View Change 2
                    if (button5_pressed)
                      player_data[0] |= 0x10;
                    // View Change 3
                    if (button6_pressed)
                      player_data[0] |= 0x08;
                    // View Change 4
                    if (button7_pressed)
                      player_data[0] |= 0x04;
                    player_data[1] = RXReply & 0xF0;
                  }
                  else if (i == 1)
                  {
                    // Paddle left
                    if (button8_pressed)
                      player_data[0] |= 0x20;
                    // Paddle right
                    if (button2_pressed)
                      player_data[0] |= 0x10;

                    if (m_fzdx_motion_stop)
                    {
                      player_data[0] |= 2;
                    }
                    if (m_fzdx_sensor_right)
                    {
                      player_data[0] |= 4;
                    }
                    if (m_fzdx_sensor_left)
                    {
                      player_data[0] |= 8;
                    }

                    player_data[1] = RXReply << 4;
                  }
                  break;
                // Controller configuration for F-Zero AX MonsterRide
                case FZeroAXMonster:
                  if (i == 0)
                  {
                    if (m_fzcc_sensor)
                    {
                      player_data[0] |= 0x01;
                    }

                    // Start
                    if (start_pressed)
                      player_data[0] |= 0x80;
                    // Service button
                    if (service_pressed)
                      player_data[0] |= 0x40;
                    // Boost
                    if (button1_pressed)
                      player_data[0] |= 0x02;
                    // View Change 1
                    if (button4_pressed)
                      player_data[0] |= 0x20;
                    // View Change 2
                    if (button5_pressed)
                      player_data[0] |= 0x10;
                    // View Change 3
                    if (button6_pressed)
                      player_data[0] |= 0x08;
                    // View Change 4
                    if (button7_pressed)
                      player_data[0] |= 0x04;

                    player_data[1] = RXReply & 0xF0;
                  }
                  else if (i == 1)
                  {
                    // Paddle left
                    if (button8_pressed)
                      player_data[0] |= 0x20;
                    // Paddle right
                    if (button2_pressed)
                      player_data[0] |= 0x10;

                    if (m_fzcc_seatbelt)
                    {
                      player_data[0] |= 2;
                    }
                    if (m_fzcc_service)
                    {
                      player_data[0] |= 4;
                    }
                    if (m_fzcc_emergency)
                    {
                      player_data[0] |= 8;
                    }
                  }
                  break;
                // Controller configuration for Virtua Striker 3 games
                case VirtuaStriker3:
                  // Start
                  if (start_pressed)
                    player_data[0] |= 0x80;
                  // Service button
                  if (service_pressed)
                    player_data[0] |= 0x40;
                  // Long Pass
                  if (button1_pressed)
                    player_data[0] |= 0x01;
                  // Short Pass
                  if (button2_pressed)
                    player_data[1] |= 0x80;
                  // Shoot
                  if (button3_pressed)
                    player_data[0] |= 0x02;
                  // Left
                  if (left_pressed)
                    player_data[0] |= 0x08;
                  // Up
                  if (up_pressed)
                    player_data[0] |= 0x20;
                  // Right
                  if (right_pressed)
                    player_data[0] |= 0x04;
                  // Down
                  if (down_pressed)
                    player_data[0] |= 0x10;
                  break;
                // Controller configuration for Virtua Striker 4 games
                case VirtuaStriker4:
                {
                  // Start
                  if (start_pressed)
                    player_data[0] |= 0x80;
                  // Service button
                  if (service_pressed)
                    player_data[0] |= 0x40;
                  // Long Pass
                  if (button1_pressed)
                    player_data[0] |= 0x01;
                  // Short Pass
                  if (button2_pressed)
                    player_data[0] |= 0x02;
                  // Shoot
                  if (button3_pressed)
                    player_data[1] |= 0x80;
                  // Dash
                  if (button9_pressed)  // Using button9 for Dash
                    player_data[1] |= 0x40;
                  // Tactics (U)
                  if (button4_pressed)
                    player_data[0] |= 0x20;
                  // Tactics (M)
                  if (button5_pressed)
                    player_data[0] |= 0x08;
                  // Tactics (D)
                  if (button6_pressed)
                    player_data[0] |= 0x04;

                  if (i == 0)
                  {
                    player_data[0] |= 0x10;  // IC-Card Switch ON

                    // IC-Card Lock
                    if (button7_pressed)
                      player_data[1] |= 0x20;
                  }
                }
                break;
                // Controller configuration for Gekitou Pro Yakyuu
                case GekitouProYakyuu:
                  // Start
                  if (start_pressed)
                    player_data[0] |= 0x80;
                  // Service button
                  if (service_pressed)
                    player_data[0] |= 0x40;
                  // A
                  if (button1_pressed)
                    player_data[0] |= 0x01;
                  // B
                  if (button2_pressed)
                    player_data[0] |= 0x02;
                  // Gekitou
                  if (button3_pressed)
                    player_data[1] |= 0x80;
                  // Left
                  if (left_pressed)
                    player_data[0] |= 0x08;
                  // Up
                  if (up_pressed)
                    player_data[0] |= 0x20;
                  // Right
                  if (right_pressed)
                    player_data[0] |= 0x04;
                  // Down
                  if (down_pressed)
                    player_data[0] |= 0x10;
                  break;
                // Controller configuration for Mario Kart and other games
                default:
                case MarioKartGP:
                case MarioKartGP2:
                {
                  // Start
                  if (start_pressed)
                    player_data[0] |= 0x80;
                  // Service button
                  if (service_pressed)
                    player_data[0] |= 0x40;
                  // Item button
                  if (button1_pressed)
                    player_data[1] |= 0x20;
                  // VS-Cancel button
                  if (button2_pressed)
                    player_data[1] |= 0x02;
                }
                break;
                }

                for (int j = 0; j < player_byte_count; ++j)
                  msg.addData(player_data[j]);
              }
              break;
            }
            case JVSIOCommands::CoinInput:
            {
              int slots = *jvs_io++;
              msg.addData(1);

              // Read coin button state from shared memory (StateView[32] - separate coin offset)
              u32 coin_state = 0;
#ifdef _WIN32
              if (g_jvs_view_ptr)
              {
                coin_state = *reinterpret_cast<u32*>(static_cast<u8*>(g_jvs_view_ptr) + 32);
              }
#endif
              // Check coin button state from shared memory (direct value, not bit flag)
              bool coin_pressed_now = (coin_state != 0);

              for (int i = 0; i < slots; i++)
              {
                // Increment coin counter on rising edge (when coin button goes from not pressed to
                // pressed) For multiple slots, we use the same coin input for all slots
                if (coin_pressed_now && !g_coin_pressed_prev)
                {
                  m_coin[i]++;
                }

                msg.addData((m_coin[i] >> 8) & 0x3f);
                msg.addData(m_coin[i] & 0xff);
              }

              // Update previous state for next frame
              g_coin_pressed_prev = coin_pressed_now;

              break;
            }
            case JVSIOCommands::AnalogInput:
            {
              msg.addData(1);  // status

              int analogs = *jvs_io++;
              GCPadStatus PadStatus;
              GCPadStatus PadStatus2;
              PadStatus = Pad::GetStatus(0);
              auto gameType = AMMediaboard::GetGameType();
              if (gameType == VirtuaStriker3 || gameType == VirtuaStriker4)
              {
                PadStatus2 = Pad::GetStatus(1);
              }

              // Override with values from shared memory if available
#ifdef _WIN32
              if (g_jvs_view_ptr)
              {
                if (gameType == VirtuaStriker3 || gameType == VirtuaStriker4)
                {
                  PadStatus.stickY = static_cast<u8*>(g_jvs_view_ptr)[12];   // StateView[12]
                  PadStatus.stickX = static_cast<u8*>(g_jvs_view_ptr)[13];   // StateView[13]
                  PadStatus2.stickY = static_cast<u8*>(g_jvs_view_ptr)[14];  // StateView[14]
                  PadStatus2.stickX = static_cast<u8*>(g_jvs_view_ptr)[15];  // StateView[15]
                }
                if (gameType == MarioKartGP || gameType == MarioKartGP2)
                {
                  PadStatus.stickX = static_cast<u8*>(g_jvs_view_ptr)[12];        // StateView[12]
                  PadStatus.triggerRight = static_cast<u8*>(g_jvs_view_ptr)[14];  // StateView[12]
                  PadStatus.triggerLeft = static_cast<u8*>(g_jvs_view_ptr)[15];   // StateView[13]
                }
                if (gameType == FZeroAX || gameType == FZeroAXMonster)
                {
                  PadStatus.stickX = static_cast<u8*>(g_jvs_view_ptr)[12];        // StateView[12]
                  PadStatus.stickY = static_cast<u8*>(g_jvs_view_ptr)[13];        // StateView[13]
                  PadStatus.triggerRight = static_cast<u8*>(g_jvs_view_ptr)[14];  // StateView[12]
                  PadStatus.triggerLeft = static_cast<u8*>(g_jvs_view_ptr)[15];   // StateView[13]
                }
              }
#endif
              DEBUG_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:Get Analog Inputs Analogs:{}", analogs);

              switch (AMMediaboard::GetGameType())
              {
              case FZeroAX:
              case FZeroAXMonster:
                // Steering
                if (m_motorinit == 1)
                {
                  if (m_motorforce_x > 0)
                  {
                    msg.addData(0x80 - (m_motorforce_x >> 8));
                  }
                  else
                  {
                    msg.addData((m_motorforce_x >> 8));
                  }
                  msg.addData((u8)0);

                  msg.addData(PadStatus.stickY);
                  msg.addData((u8)0);
                }
                else
                {
                  msg.addData(PadStatus.stickX);
                  msg.addData((u8)0);

                  msg.addData(PadStatus.stickY);
                  msg.addData((u8)0);
                }

                // Unused
                msg.addData((u8)0);
                msg.addData((u8)0);
                msg.addData((u8)0);
                msg.addData((u8)0);

                // Gas
                msg.addData(PadStatus.triggerRight);
                msg.addData((u8)0);

                // Brake
                msg.addData(PadStatus.triggerLeft);
                msg.addData((u8)0);

                msg.addData((u8)0x80);  // Motion Stop
                msg.addData((u8)0);

                msg.addData((u8)0);
                msg.addData((u8)0);

                break;
              case VirtuaStriker3:
              case VirtuaStriker4:
              {
                msg.addData(PadStatus.stickX);
                msg.addData((u8)0);
                msg.addData(PadStatus.stickY);
                msg.addData((u8)0);

                msg.addData(PadStatus2.stickX);
                msg.addData((u8)0);
                msg.addData(PadStatus2.stickY);
                msg.addData((u8)0);
              }
              break;
              default:
              case MarioKartGP:
              case MarioKartGP2:
                // Steering
                msg.addData(PadStatus.stickX);
                msg.addData((u8)0);

                // Gas
                msg.addData(PadStatus.triggerRight);
                msg.addData((u8)0);

                // Brake
                msg.addData(PadStatus.triggerLeft);
                msg.addData((u8)0);
                break;
              }
              break;
            }
            case JVSIOCommands::CoinSubOutput:
            {
              u32 slot = *jvs_io++;
              m_coin[slot] -= (*jvs_io++ << 8) | *jvs_io++;
              msg.addData(1);
              break;
            }
            case JVSIOCommands::GeneralDriverOutput:
            {
              u32 bytes = *jvs_io++;
              if (bytes)
              {
                u8* buf = new u8[bytes];

                for (u32 i = 0; i < bytes; ++i)
                {
                  buf[i] = *jvs_io++;
                }

                DEBUG_LOG_FMT(
                    AMBASEBOARDDEBUG,
                    "JVS-IO: Command 32, GPO: {:02x} {:02x} {} {:02x}{:02x}{:02x} ({:02x})", delay,
                    RXReply, bytes, buf[0], buf[1], buf[2], Common::swap16(*(u16*)(buf + 1)) >> 2);

                // TODO: figure this out

                u8 trepl[] = {
                    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0,
                    0xD0, 0xE0, 0xF0, 0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91,
                    0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1, 0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62,
                    0x72, 0x82, 0x92, 0xA2, 0xB2, 0xC2, 0xD2, 0xE2, 0xF2, 0x04, 0x14, 0x24, 0x34,
                    0x44, 0x54, 0x64, 0x74, 0x84, 0x94, 0xA4, 0xB4, 0xC4, 0xD4, 0xE4, 0xF4, 0x05,
                    0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0x85, 0x95, 0xA5, 0xB5, 0xC5, 0xD5,
                    0xE5, 0xF5, 0x06, 0x16, 0x26, 0x36, 0x46, 0x56, 0x66, 0x76, 0x86, 0x96, 0xA6,
                    0xB6, 0xC6, 0xD6, 0xE6, 0xF6, 0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78,
                    0x88, 0x98, 0xA8, 0xB8, 0xC8, 0xD8, 0xE8, 0xF8, 0x09, 0x19, 0x29, 0x39, 0x49,
                    0x59, 0x69, 0x79, 0x89, 0x99, 0xA9, 0xB9, 0xC9, 0xD9, 0xE9, 0xF9, 0x0A, 0x1A,
                    0x2A, 0x3A, 0x4A, 0x5A, 0x6A, 0x7A, 0x8A, 0x9A, 0xAA, 0xBA, 0xCA, 0xDA, 0xEA,
                    0xFA, 0x0C, 0x1C, 0x2C, 0x3C, 0x4C, 0x5C, 0x6C, 0x7C, 0x8C, 0x9C, 0xAC, 0xBC,
                    0xCC, 0xDC, 0xEC, 0xFC, 0x0D, 0x1D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D, 0x8D,
                    0x9D, 0xAD, 0xBD, 0xCD, 0xDD, 0xED, 0xFD, 0x0E, 0x1E, 0x2E, 0x3E, 0x4E, 0x5E,
                    0x6E, 0x7E, 0x8E, 0x9E, 0xAE, 0xBE, 0xCE, 0xDE, 0xEE, 0xFE};

                static u32 off = 0;
                if (off > sizeof(trepl))
                  off = 0;

                switch (Common::swap16(*(u16*)(buf + 1)) >> 2)
                {
                case 0x70:
                  delay++;
                  if ((delay % 10) == 0)
                  {
                    RXReply = trepl[off++];
                  }
                  break;
                default:
                case 0x60:
                case 0xA0:
                case 0xF0:
                  RXReply = 0;
                  break;
                }
                ////if( buf[1] == 1 && buf[2] == 0x80 )
                ////{
                ////  INFO_LOG_FMT(DVDINTERFACE, "GCAM: PC:{:08x}", PC);
                ////  PowerPC::breakpoints.Add( PC+8, false );
                ////}

                delete[] buf;
              }
              msg.addData(1);
              break;
            }
            case JVSIOCommands::CoinAddOutput:
            {
              int slot = *jvs_io++;
              m_coin[slot] += (*jvs_io++ << 8) | *jvs_io++;
              msg.addData(1);
              break;
            }
            case JVSIOCommands::NAMCOCommand:
            {
              int cmd_ = *jvs_io++;
              if (cmd_ == 0x18)
              {  // id check
                jvs_io += 4;
                msg.addData(1);
                msg.addData(0xff);
              }
              else
              {
                msg.addData(1);
                // ERROR_LOG(AMBASEBOARDDEBUG, "JVS-IO:Unknown");
              }
              break;
            }
            case JVSIOCommands::Reset:
              if (*jvs_io++ == 0xD9)
              {
                NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:RESET");
                delay = 0;
                m_wheelinit = 0;
                m_ic_card_state = 0x20;
              }
              msg.addData(1);

              d10_1 |= 1;
              break;
            case JVSIOCommands::SetAddress:
              node = *jvs_io++;
              NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO:SET ADDRESS, node={}", node);
              msg.addData(node == 1);
              d10_1 &= ~1;
              break;
            default:
              ERROR_LOG_FMT(AMBASEBOARDDEBUG, "JVS-IO: node={}, command={:02x}", node, cmd);
              break;
            }

            pptr += jvs_io_length;
          }

          msg.end();

          res[resp++] = ptr(0);

          unsigned char* buf = msg.m_msg;
          int len = msg.m_ptr;
          res[resp++] = len;

          for (int i = 0; i < len; ++i)
            res[resp++] = buf[i];
          break;
        }
        case 0x60:
          NOTICE_LOG_FMT(AMBASEBOARDDEBUG, "GC-AM: Command 60, {:02x} {:02x} {:02x}", ptr(1),
                         ptr(2), ptr(3));
          break;
        default:
          ERROR_LOG_FMT(AMBASEBOARDDEBUG,
                        "GC-AM: Command {:02x} (unknown) {:02x} {:02x} {:02x} {:02x} {:02x}",
                        ptr(0), ptr(1), ptr(2), ptr(3), ptr(4), ptr(5));
          break;
        }
        p += ptr(1) + 2;
      }
      memset(_pBuffer, 0, _iLength);

      int len = resp - 2;

      p = 0;
      res[1] = len;
      csum = 0;
      char logptr[1024];
      char* log = logptr;

      for (int i = 0; i < 0x7F; ++i)
      {
        csum += ptr(i) = res[i];
        log += sprintf(log, "%02X", ptr(i));
      }
      ptr(0x7f) = ~csum;
      DEBUG_LOG_FMT(AMBASEBOARDDEBUG, "Command send back: {}", logptr);
#undef ptr

      // (tmbinc) hotfix: delay output by one command to work around their broken parser. this took
      // me a month to find. ARG!
      static unsigned char last[2][0x80];
      static int lastptr[2];

      {
        memcpy(last + 1, _pBuffer, 0x80);
        memcpy(_pBuffer, last, 0x80);
        memcpy(last, last + 1, 0x80);

        lastptr[1] = _iLength;
        _iLength = lastptr[0];
        lastptr[0] = lastptr[1];
      }

      iPosition = _iLength;
      break;
    }
      // DEFAULT
    default:
    {
      ERROR_LOG_FMT(SERIALINTERFACE, "Unknown SI command     (0x{:08x})", (u32)command);
      PanicAlertFmt("SI: Unknown command");
      iPosition = _iLength;
    }
    break;
    }
  }

  return iPosition;
}

// Unused
bool CSIDevice_AMBaseboard::GetData(u32& _Hi, u32& _Low)
{
  _Low = 0;
  _Hi = 0x00800000;

  return true;
}

void CSIDevice_AMBaseboard::SendCommand(u32 _Cmd, u8 _Poll)
{
  ERROR_LOG_FMT(SERIALINTERFACE, "Unknown direct command     (0x{})", _Cmd);
  PanicAlertFmt("SI: (GCAM) Unknown direct command");
}

}  // namespace SerialInterface
