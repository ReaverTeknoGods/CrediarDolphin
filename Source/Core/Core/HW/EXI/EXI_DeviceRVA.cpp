// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceRVA.h"

#include <locale>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/IOFile.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Channel.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI_DeviceGCController.h"
#include "Core/IOS/ES/ES.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

// Teknoparrot Control stuff
#ifdef _WIN32
static HANDLE g_jvs_file_mapping = nullptr;
static void* g_jvs_view_ptr = nullptr;
#endif

namespace ExpansionInterface
{

bool g_coin_pressed_prev_wii = false;
// clang-format off
static const wchar_t character_display_code[] =
{                                       /*0x04                    0x08                       0x0C*/
/* 0x00 */	L' ', L' ', L' ', L' ',	    L' ', L' ', L' ', L' ',	  L' ', L' ', L' ', L' ',	  L' ', L' ', L' ', L' ', /* 0x00 Display Commands */
/* 0x10 */	L' ', L' ',  ' ', L' ',		  L' ', L' ', L' ', L' ',	  L' ', L' ', L' ', L' ',	  L' ', L' ', L' ', L' ',	/* 0x10 Display Commands */
/* 0x20 */	L'　', L'!', L'\"',L'#',		  L'$', L'%', L'&', L'\'',  L'(', L')', L'*', L'+',		L',', L'-', L'.', L'/', /* 0x20 */
/* 0x30 */	L'0', L'1', L'2', L'3',			L'4', L'5', L'6', L'7',		L'8', L'9', L':', L';',   L'<', L'=', L'>', L'?',	/* 0x30 */
/* 0x40 */	L'@', L'A', L'B', L'C',	    L'D', L'E', L'F', L'G',	  L'H', L'I', L'J', L'K',	  L'L', L'M', L'N', L'O',	/* 0x40 */
/* 0x50 */	L'P', L'Q', L'R', L'S', 	  L'T', L'U', L'V', L'W',	  L'X', L'Y', L'Z', L'[', 	L'Y', L']', L'^', L'_',	/* 0x50 */
/* 0x60 */	L'`',L'a', L'b', L'c',	    L'd', L'e', L'f', L'g',	  L'h', L'i', L'j', L'k',	  L'L', L'm', L'n', L'o',	/* 0x60 */
/* 0x70 */	L'p', L'q', L'r', L's', 	  L't', L'u', L'v', L'w',	  L'x', L'y', L'z', L'{',   L'|', L'}', L'?', L'?', /* 0x70 */
/* 0x80 */	L'α', L'β', L'γ', L'?',     L'?', L'?', L'?', L'?',   L'µ', L'?', L'?', L'?',   L'?', L'?', L'?', L'?', /* 0x80 */
/* 0x90 */	L'?', L'?', L'?', L'?',     L'?', L'?', L'?', L'?',   L'²', L'?', L'?', L'?',   L'?', L'?', L'?', L'?', /* 0x90 */
/* 0xA0 */	L'！', L'。', L'┌', L'┘',   L'、', L'・', L'ヲ', L'ァ', L'ィ', L'ゥ',L'ェ',L'ォ',   L'ャ', L'ュ', L'ョ', L'ツ', /* 0xA0 */
/* 0xB0 */	L'ー', L'ア',L'イ',L'ウ',    L'エ', L'オ',L'カ',L'キ',   L'ク', L'ケ', L'コ', L'サ',L'シ',L'ス',L'セ', L'ソ',  /* 0xB0 */
/* 0xC0 */	L'タ', L'チ',L'ツ',L'テ',    L'ト', L'ナ',L'二', L'ヌ',  L'ネ', L'ノ', L'ハ', L'ヒ',L'フ',L'へ',L'ホ', L'マ', /* 0xC0 */
/* 0xD0 */	L'ミ', L'ム',L'メ',L'モ',    L'ヤ', L'ユ',L'ヨ', L'ラ',  L'リ', L'ル', L'レ', L'ロ', L'ワ',L'ン',L'゛', L'゜',  /* 0xD0 */
/* 0xE0 */	L'↑', L'↓', L'?', L'?',    L'?', L'日', L'月', L'?',    L'?', L'?', L'?', L'?',    L'?', L'?', L'?', L'?', /* 0xE0 */
/* 0xF0 */	L'?', L'?', L'?', L'?',    L'?', L'?', L'?', L'?',    L'?', L'?', L'?', L'?',    L'?', L'?', L'?', L'?', /* 0xF0 */
};


static const u8 matrix_display_ID_reply[] = {"\x02M202MD12BA\x03"};

u8 version_check_reply[] = {2, '0', 'C', 'V', 'R', '0', '1', '0', '4', 'E', '5', 3};

static const u8 board_status_reply[] = {2, '0', 'C', 'S', 'T', '8', '0', '0', '0', 'E', '7', 3};

static const u8 update_info_reply[] = {2, '0', 'A', 'U', 'P', '0', '0', '7', 'B', 3};

// TODO: figure out correct reply
u8 ConfigReply[] = {
    2, '1', '2', 'C', 'F', 'F', 'F', 'F', 'F', 'F', 'F', 'F', 'F', 'F', 'F', 'A', 'D', 3,
};

u8 SSReply[28] = {

    0x02, 0x31, 0x43, 0x53, 0x53, 0x38, 0x30, 0x30, 0x30, 0x38, 0x30, 0x30, 0x30, 0x38,
    0x30, 0x30, 0x30, 0x38, 0x30, 0x30, 0x30, 0x38, 0x30, 0x30, 0x30, 0x30, 0x37, 0x03,

};

static const u8 AMO_reply_se[] = {3, '/', 'S', '\n', '/', 'S', 'V', ':', '\n', '/', 'E', '\n'};

static const u8 AMO_reply_version[] = {3,   '/', 'S', '\n', 'V', ':', 'M', 'P',
                                       '8', '2', 0,   '\n', '/', 'E', '\n'};

static const u8 AMO_reply_client_version[] = {3,   '/', 'S', '\n', 'V', ':', 'M', 'K',
                                              'F', ' ', 0,   '\n', '/', 'E', '\n'};

static const u8 AMO_reply_ie[] = {3,   '/', 'S', '\n', 'I', ':', 'T',
                                  'E', 'T', 'E', '\n', '/', 'E', '\n'};  // ??

static const u8 SI0_reply_version[] = {"\x02\x05\x00\x06MP8\x00\x01\x5F\x42"};

static const u8 SI0_reply_6[] = {"\x02\x01\x00\x06TEST12\xAC"};

// TMD hashes to detect versious versions

static const u8 mario_party_FKC_2_server_1302141211[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 mario_party_FKC_2_client_1301080914[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 mario_party_FKC_2_client_1302141156[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 tatsunoko_VS_capcom_0811051625[20] = {
    0x90, 0x2D, 0xDE, 0x93, 0xAB, 0xBF, 0x20, 0xD2, 0xB9, 0x6A,
    0x2D, 0xAB, 0xC3, 0x9E, 0x06, 0x74, 0xB7, 0x1C, 0x60, 0x30,
};

// clang-format on

bool g_have_IRQ;
u32 g_IRQ_delay;
u32 g_IRQ_type;

u32 g_write_mode;
u32 g_write_size;

GameType g_game_type;

static void InterruptSet(RVAIRQMasks Interrupt)
{
  g_have_IRQ = true;
  g_IRQ_type = RVA_IRQ_MASK & ~Interrupt;
  g_IRQ_delay = 10;
}

static u8 CheckSum(u8* data, u32 length)
{
  u8 check = 0;

  for (u32 i = 0; i < length; i++)
  {
    check += data[i];
  }

  return check;
}

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

CEXIJVS::CEXIJVS()
{
  m_csr = 0;
  m_tx_count = 0;
  m_rx_data = 0;
  m_tx_len = 0;
  m_rx_len = 0;
  m_dscr = 0;

  memset(m_coin, 0, sizeof(m_coin));
  memset(m_coin_pressed, 0, sizeof(m_coin_pressed));

  m_JVS_offset = 0;
  memset(m_JVS_data, 0, sizeof(m_JVS_data));

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

u32 CEXIJVS::Read(RVAMemoryMap address, u32 size)
{
  u32 switch_status = 0xFFFFFFFF;
  u32 data = 0;
  GCPadStatus pad_status = Pad::GetStatus(0);

  switch (address)
  {
  case RVAMemoryMap::JVS_SWITCHES:
    if (pad_status.button & PAD_TRIGGER_Z)
      switch_status &= ~0x10000000;

    if (pad_status.button & PAD_TRIGGER_L)
      switch_status &= ~0x20000000;

    data = switch_status;
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    data = m_csr | JVS_IO_CSR_TX_INT;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: CSR(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_TX_CNT:
    data = m_tx_count;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: TX_CNT(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    data = m_dscr;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: DCSR(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_RX_CNT:
    data = m_rx_count;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: RX_CNT(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_TX_LEN:
    data = m_tx_len;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: TX_LEN(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_RX_LEN:
    data = m_rx_len;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: RX_LEN(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_RXD:
  {
    data = m_JVS_reply_data[m_JVS_reply_offset - m_rx_count];
    if (m_rx_count > 0)
      m_rx_count--;

    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: RX_RXD(R): {:02x}", data);
  }
  break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: Unhandled address read: {:08x}", (u32)address);
    return 0;
  }

  return data;
}

void CEXIJVS::Write(RVAMemoryMap address, u32 value)
{
  u32 value_swap = Common::swap32(value);

  if ((u32)address == (value >> 6))
  {
    return;
  }

  switch (address)
  {
  case RVAMemoryMap::JVS_SWITCHES:
    g_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & JVS_IO_CSR_MASK;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_TXD:
  {
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: TX_TXD(W): {:02x}", value_swap);

    m_tx_data = value_swap;
    m_JVS_data[m_JVS_offset++] = value_swap;

    if (m_JVS_offset > 2)
    {
      u8 size = m_JVS_data[2] + 3;
      if (m_JVS_offset == size)
      {
        JVSIOMessage msg;

        u8 node = m_JVS_data[1];

        msg.start(node);
        msg.addData(1);

        u8* jvs_io = m_JVS_data + 3;
        m_JVS_offset--;  // checksum

        GCPadStatus pad_status;

        while (jvs_io < (m_JVS_data + m_JVS_offset))
        {
          int cmd = *jvs_io++;
          DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO:node={}, command={:02x}", node, cmd);
          switch (JVSIOCommands(cmd))
          {
          case JVSIOCommands::IOID:
            msg.addData(1);
            msg.addData("NO BRAND;NAOMI CONVERTER98701;VER2.0;");
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 10, IOID");
            msg.addData((u32)0);
            break;
          case JVSIOCommands::CommandRevision:
            msg.addData(1);
            msg.addData(0x11);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 11, CommandRevision");
            break;
          case JVSIOCommands::JVSRevision:
            msg.addData(1);
            msg.addData(0x20);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 12, JVSRevision");
            break;
          case JVSIOCommands::CommunicationVersion:
            msg.addData(1);
            msg.addData(0x10);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 13, CommunicationVersion");
            break;
          case JVSIOCommands::CheckFunctionality:
            msg.addData(1);
            // 2 Player (12bit), 2 Coin slot, 8 Analog-in
            msg.addData((void*)"\x01\x02\x0C\x00", 4);
            msg.addData((void*)"\x02\x02\x00\x00", 4);
            msg.addData((void*)"\x03\x08\x00\x00", 4);
            msg.addData((void*)"\x00\x00\x00\x00", 4);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 14, CheckFunctionality");
            break;
          case JVSIOCommands::SwitchesInput:
          {
            u8 player_count = *jvs_io++;
            u8 player_byte_count = *jvs_io++;

            INFO_LOG_FMT(EXPANSIONINTERFACE,
                         "RVA-JVS-IO: Command 20, SwitchesInput: Players:{} Bytes:{}", player_count,
                         player_byte_count);

            InterruptSet(RVA_IRQ_JVS_RS_RX);

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
            0x04:       Player 1 Button1 (Primary: Light Punch/A)
            0x08:       Player 2 Start
            0x10:       Player 2 Button1 (Primary: Light Punch/A)
            0x20:       Player 1 Button2 (Secondary: Medium Punch/B)
            0x40:       Player 1 Service
            0x80:       Player 2 Button2 (Secondary: Medium Punch/B)
            0x100:      Player 2 Service
            0x200:      Player 1 Button3 (Tertiary: Heavy Punch/X)
            0x400:      Player 1 Left
            0x800:      Player 1 Up
            0x1000:     Player 1 Right
            0x2000:     Player 1 Down
            0x4000:     Player 2 Button3 (Tertiary: Heavy Punch/X)
            0x8000:     Player 2 Left
            0x10000:    Player 2 Up
            0x20000:    Player 2 Right
            0x40000:    Player 2 Down
            0x80000:    Player 1 Button4 (Light Kick/Y)
            0x100000:   Player 1 Button5 (Medium Kick/L)
            0x200000:   Player 1 Button6 (Heavy Kick/R)
            0x400000:   Player 1 Button7 (Partner/Z)
            0x800000:   Player 2 Button4 (Light Kick/Y)
            0x1000000:  Player 2 Button5 (Medium Kick/L)
            0x2000000:  Player 2 Button6 (Heavy Kick/R)
            0x4000000:  Player 2 Button7 (Partner/Z)
            0x8000000:  Player 1 Button8 (Extra)
            0x10000000: Player 1 Button9 (Extra)
            0x20000000: Player 2 Button8 (Extra)
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
              u8 player_data[3] = {0, 0, 0};

              // Extract button states from shared memory based on player index
              bool start_pressed = false;
              bool service_pressed = false;
              bool button1_pressed = false;  // Light Punch/A
              bool button2_pressed = false;  // Medium Punch/B
              bool button3_pressed = false;  // Heavy Punch/X
              bool button4_pressed = false;  // Light Kick/Y
              bool button5_pressed = false;  // Medium Kick/L
              bool button6_pressed = false;  // Heavy Kick/R
              bool button7_pressed = false;  // Partner/Z
              bool left_pressed = false;
              bool up_pressed = false;
              bool right_pressed = false;
              bool down_pressed = false;

              if (i == 0)  // Player 1
              {
                start_pressed = (control & 0x02) != 0;
                service_pressed = (control & 0x40) != 0;
                button1_pressed = (control & 0x04) != 0;
                button2_pressed = (control & 0x20) != 0;
                button3_pressed = (control & 0x200) != 0;
                button4_pressed = (control & 0x80000) != 0;
                button5_pressed = (control & 0x100000) != 0;
                button6_pressed = (control & 0x200000) != 0;
                button7_pressed = (control & 0x400000) != 0;
                left_pressed = (control & 0x400) != 0;
                up_pressed = (control & 0x800) != 0;
                right_pressed = (control & 0x1000) != 0;
                down_pressed = (control & 0x2000) != 0;
              }
              else if (i == 1)  // Player 2
              {
                start_pressed = (control & 0x08) != 0;
                service_pressed = (control & 0x100) != 0;
                button1_pressed = (control & 0x10) != 0;
                button2_pressed = (control & 0x80) != 0;
                button3_pressed = (control & 0x4000) != 0;
                button4_pressed = (control & 0x800000) != 0;
                button5_pressed = (control & 0x1000000) != 0;
                button6_pressed = (control & 0x2000000) != 0;
                button7_pressed = (control & 0x4000000) != 0;
                left_pressed = (control & 0x8000) != 0;
                up_pressed = (control & 0x10000) != 0;
                right_pressed = (control & 0x20000) != 0;
                down_pressed = (control & 0x40000) != 0;
              }

              // Controller configuration for Tatsunoko vs Capcom (default for Wii)
              // Start
              if (start_pressed)
                player_data[0] |= 0x80;
              // Service button
              if (service_pressed)
                player_data[0] |= 0x40;
              // Light Punch (A)
              if (button1_pressed)
                player_data[0] |= 0x02;
              // Medium Punch (B)
              if (button2_pressed)
                player_data[0] |= 0x01;
              // Heavy Punch (X)
              if (button3_pressed)
                player_data[1] |= 0x80;
              // Light Kick (Y)
              if (button4_pressed)
                player_data[1] |= 0x40;
              // Medium Kick (L)
              if (button5_pressed)
                player_data[1] |= 0x20;
              // Heavy Kick (R)
              if (button6_pressed)
                player_data[1] |= 0x10;
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
              if (coin_pressed_now && !g_coin_pressed_prev_wii)
              {
                m_coin[i]++;
              }
              msg.addData((m_coin[i] >> 8) & 0x3f);
              msg.addData(m_coin[i] & 0xff);
            }

            // Update previous state for next frame
            g_coin_pressed_prev_wii = coin_pressed_now;

            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Command 21, CoinInput Slots:{}", slots);
            break;
          }
          case JVSIOCommands::CoinSubOutput:
          {
            u32 slot = *jvs_io++;
            m_coin[slot] -= (*jvs_io++ << 8) | *jvs_io++;
            msg.addData(1);
            break;
          }
          case JVSIOCommands::Reset:
            if (*jvs_io++ == 0xD9)
            {
              INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: Reset");
              m_dscr |= 0x80;
            }
            msg.addData(1);

            break;
          case JVSIOCommands::SetAddress:
            node = *jvs_io++;
            INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: SetAddress, node={}", node);
            msg.addData(node == 1);
            m_dscr &= ~0x80;
            break;
          default:
            ERROR_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS-IO: node={}, command={:02x}", node, cmd);
            break;
          }
        }

        msg.end();

        memset(m_JVS_reply_data, 0, sizeof(m_JVS_reply_data));

        m_JVS_reply_offset = msg.m_ptr;
        m_rx_count = msg.m_ptr;

        memcpy(m_JVS_reply_data, msg.m_msg, msg.m_ptr);

        memset(m_JVS_data, 0, sizeof(m_JVS_data));
        m_JVS_offset = 0;
        g_write_mode = 0;
        m_rx_len = m_rx_count;
      }
    }
  }
  break;
  case RVAMemoryMap::JVS_IO_TX_LEN:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: TX_LEN(W): {:02x}", value_swap);
    m_tx_len = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_RX_LEN:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: RX_LEN(W): {:02x}", value_swap);
    m_rx_len = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: DCSR(W): {:02x}", value_swap);
    m_dscr = value_swap;
    g_write_mode = 0;
    break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: Unhandled address write: {:08x} {:02x}",
                  (u32)address, value);
    break;
  }
}

CEXIJVS::~CEXIJVS()
{
}

CEXISI::CEXISI()
{
  m_csr = 0;
  m_tx_data = 0;
  m_tx_count = 0;
  m_rx_data = 0;
  m_rx_count = 0;
  m_tx_rate = 0;
  m_rx_rate = 0;
  m_status = 0;

  m_SI_offset = 0;
  memset(m_SI_buf, 0, sizeof(m_SI_buf));
  m_rx_offset = 0;
  m_IRQc = 0;
}

CEXISI::~CEXISI() = default;

void CEXISI::SetReply(SerialReplies reply, u32 size)
{
  m_rx_count = size;
  m_rx_type = reply;
  m_rx_offset = 0;
}

u32 CEXISI::Read(RVAMemoryMap address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  auto& ppc_state = system.GetPPCState();
  auto& jit_interface = system.GetJitInterface();

  u32 data = 0;

  switch (address)
  {
  case RVAMemoryMap::SI_0_CSR:
    data = m_csr | 0x40;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: CSR(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_0_TX_CNT:
    data = m_tx_count;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TX-CNT(R):{:08x}:{:02X}", size, data);
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RXD:
    switch (SerialReplies(m_rx_type))
    {
    case SerialReplies::SerialVersion:
      data = SI0_reply_version[m_rx_offset++];
      if (m_rx_offset > sizeof(SI0_reply_version))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::Serial_6:
      data = SI0_reply_6[m_rx_offset++];
      if (m_rx_offset > sizeof(SI0_reply_6))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::AMOServices:
      data = AMO_reply_se[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_se))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::AMOVersionServer:
      data = AMO_reply_version[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_version))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::AMOVersionClient:
      data = AMO_reply_client_version[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_client_version))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::MPSIVersion:
      data = version_check_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(version_check_reply))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::MPSIStatus:
      data = board_status_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(board_status_reply))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::MPSIUpdate:
      data = update_info_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(update_info_reply))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::SS:
      data = SSReply[m_rx_offset++];
      if (m_rx_offset > sizeof(SSReply))
      {
        m_rx_offset = 0;
      }
      break;
    case SerialReplies::MPSIConfig:
      data = ConfigReply[m_rx_offset++];
      if (m_rx_offset > sizeof(ConfigReply))
      {
        m_rx_offset = 0;
      }
      break;
    default:
      data = m_rx_data;
      break;
    }

    if (m_rx_count > 0)
      m_rx_count--;

    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RXD(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_0_RX_CNT:
    data = m_rx_count;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-CNT(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_CSR:
    data = m_csr | 0x40;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_TX_CNT:
    data = m_tx_count;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TX-CNT(R):{:08x}:{:02X}", size, data);
    m_status = 0;
    break;
  case RVAMemoryMap::SI_1_RXD:
    switch (SerialReplies(m_rx_type))
    {
    case SerialReplies::MDCDisplayID:
    {
      data = matrix_display_ID_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(matrix_display_ID_reply))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::AMOServices:
    {
      data = AMO_reply_se[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_se))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionServer:
    {
      data = AMO_reply_version[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_version))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionClient:
    {
      data = AMO_reply_client_version[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_client_version))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::AMOInfo:
    {
      data = AMO_reply_ie[m_rx_offset++];
      if (m_rx_offset > sizeof(AMO_reply_ie))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::MPSIVersion:
    {
      data = version_check_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(version_check_reply))
      {
        m_rx_offset = 0;
      }
    }
    break;
    case SerialReplies::MPSIStatus:
    {
      data = board_status_reply[m_rx_offset++];
      if (m_rx_offset > sizeof(board_status_reply))
      {
        m_rx_offset = 0;
      }
    }
    break;
    default:
    {
      data = m_rx_data;
    }
    break;
    }
    if (m_rx_count > 0)
      m_rx_count--;

    if (m_rx_count == 0)
    {
      if (memory.Read_U32(0x800FAE90) == 0x60000000)
      {
        memory.Write_U32(0x48000011, 0x800FAE90);
        ppc_state.iCache.Invalidate(memory, jit_interface, 0x800FAE90);
      }
    }

    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_RX_CNT:
    data = m_rx_count;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_RX_RAT:
    data = m_rx_rate;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(R):{:08x}", size);
    break;
  default:
    WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: Unhandled Memory Address Read {:08x}:{:08x}",
                 (u32)address, size);
    return 0x00;
  }

  return data;
}

void CEXISI::Write(RVAMemoryMap address, u32 value)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  auto& ppc_state = system.GetPPCState();
  auto& jit_interface = system.GetJitInterface();

  if ((u32)address == (value >> 6))
  {
    return;
  }

  u32 value_swap = Common::swap32(value);
  wchar_t mtext[128];

  switch (address)
  {
  case RVAMemoryMap::SI_0_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_TXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_SI_buf[m_SI_offset++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_1_TXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_SI_buf[m_SI_offset++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_0_RXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RX_CNT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-CNT(W): {:02x}", value_swap);
    m_rx_count = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RX_CNT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(W): {:02x}", value_swap);
    m_rx_count = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_TX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TX-RAT(W): {:02x}", value_swap);
    m_tx_rate = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_TX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TX-RAT(W): {:02x}", value_swap);
    m_tx_rate = value_swap;
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-RAT(W): {:02x}", value_swap);
    m_rx_rate = value_swap;

    if (m_rx_rate == 0x19 && m_csr == 0x03)
    {
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0:IRQ Request? {:02x}", m_IRQc);
      m_IRQc++;
    }

    if (m_IRQc == 4)
    {
      InterruptSet(RVA_IRQ_S0_RX);
      SetReply(SerialReplies::Serial_6, sizeof(SI0_reply_6));
    }
    g_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(W): {:02x}", value_swap);
    m_rx_rate = value_swap;
    g_write_mode = 0;
    break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI(?): Unhandled address write: {:08x} {:08x}",
                  (u32)address, value_swap);
    break;
  }

  if (m_SI_offset)
  {
    // Only allow valid commands
    if (m_SI_buf[0] != 2 && m_SI_buf[0] != 0x2F)
    {
      m_SI_offset = 0;
      return;
    }
  }

  if (m_SI_offset > 2)
  {
    // check for matrix display command
    if (m_SI_buf[0] == 2 && m_SI_buf[1] == 0x10)
    {
      // size is in byte 4
      if (m_SI_offset >= 4)
      {
        // Check for full packet
        if (m_SI_offset >= m_SI_buf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: {:02x}", m_SI_buf[4]);

          switch (MatrixDisplayCommand(m_SI_buf[4]))
          {
          case MatrixDisplayCommand::Reset:
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Reset");
            break;
          case MatrixDisplayCommand::Clear:
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Clear");
            break;
          case MatrixDisplayCommand::Scroll:
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Scroll");
            break;
          case MatrixDisplayCommand::DisplayPos:
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: DisplayPos");
            // BUG: it misses to send the checksum?
            if (m_SI_buf[6] == 0x02)
            {
              memset(m_SI_buf, 0, sizeof(m_SI_buf));
              m_SI_buf[0] = 0x02;
              m_SI_offset = 1;
              return;
            }
            break;
          case MatrixDisplayCommand::DisplayID:
          {
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Get Display ID");
            InterruptSet(RVA_IRQ_S1_RX);
            SetReply(SerialReplies::MDCDisplayID, sizeof(matrix_display_ID_reply));

            /*
              BUG: The recv loop calles OSSuspendThread and then never returns
            */
            if (memory.Read_U32(0x800FAE90) == 0x48000011)
            {
              memory.Write_U32(0x60000000, 0x800FAE90);
              ppc_state.iCache.Invalidate(memory, jit_interface, 0x800FAE90);
            }
          }
          break;
          // No command just prints the text
          default:
            /*
              Remap text
            */
            memset(mtext, 0, sizeof(mtext));

            for (u32 i = 0; i < strlen((char*)(m_SI_buf + 4)) - 1; ++i)
            {
              mtext[i] = character_display_code[m_SI_buf[i + 4]];
            }
#ifdef _WIN32
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Text: {}", TStrToUTF8(mtext));
#endif
            // WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Unhandled Command {:02x}",
            // m_SI_buf[4]);
            break;
          }

          g_write_size = 0;
          g_write_mode = 0;
          m_tx_data = 0;
          m_SI_offset = 0;
          memset(m_SI_buf, 0, sizeof(m_SI_buf));
        }
      }
      return;
    }

    // Device on SI0
    if (m_SI_buf[0] == 2 && m_SI_buf[1] == 3)
    {
      // Size is in byte 4
      if (m_SI_offset >= 4)
      {
        // Check for full packet
        if (m_SI_offset >= m_SI_buf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK3: {:02x}", m_SI_buf[4]);

          g_write_size = 0;
          g_write_mode = 0;
          m_tx_data = 0;
          m_SI_offset = 0;
          memset(m_SI_buf, 0, sizeof(m_SI_buf));
        }
      }
      return;
    }

    if (m_SI_buf[0] == 2 && m_SI_buf[1] == 4)
    {
      if (m_SI_offset >= 5)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK4: {:02x}", m_SI_buf[4]);

        InterruptSet(RVA_IRQ_S0_RX);
        SetReply(SerialReplies::SerialVersion, sizeof(SI0_reply_version));

        g_write_size = 0;
        g_write_mode = 0;
        m_tx_data = 0;
        m_SI_offset = 0;
        memset(m_SI_buf, 0, sizeof(m_SI_buf));
      }
      return;
    }

    // ?
    if (m_SI_buf[0] == 2 && m_SI_buf[1] == 6)
    {
      // Size is in byte 4
      if (m_SI_offset >= 4)
      {
        // Check for full packet
        if (m_SI_offset == m_SI_buf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK6: {:02x}", m_SI_buf[2]);

          InterruptSet(RVA_IRQ_S0_RX);
          SetReply(SerialReplies::Serial_6, sizeof(SI0_reply_6));

          g_write_size = 0;
          g_write_mode = 0;
          m_tx_data = 0;
          m_SI_offset = 0;
          memset(m_SI_buf, 0, sizeof(m_SI_buf));
        }
      }
      return;
    }

    // AMO board CMD:
    if (m_SI_buf[0] == 0x2F && m_SI_offset == 7)
    {
      // replace new lines for nicer print
      for (u32 i = 0; m_SI_buf[i] != 3; ++i)
      {
        if (m_SI_buf[i] == '\n')
          m_SI_buf[i] = ' ';
      }

      m_SI_buf[6] = 0;

      NOTICE_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: AMO: {}", (char*)m_SI_buf);

      switch (m_SI_buf[1])
      {
      case 's':
        SetReply(SerialReplies::AMOServices, sizeof(AMO_reply_se));
        break;
      case 'v':
        switch (g_game_type)
        {
        default:
        case GameType::MarioPartyFKC2Server:
          SetReply(SerialReplies::AMOVersionServer, sizeof(AMO_reply_version));
          break;
        case GameType::MarioPartyFKC2Client:
          SetReply(SerialReplies::AMOVersionClient, sizeof(AMO_reply_client_version));
          break;
        }

        break;
      case 'i':
        SetReply(SerialReplies::AMOInfo, sizeof(AMO_reply_ie));
        break;
      default:
        WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: AMO: Unhandled Command {:02x}", m_SI_buf[1]);
        break;
      }

      if (m_rx_count)
      {
        if (address == RVAMemoryMap::SI_0_TXD)
        {
          InterruptSet(RVA_IRQ_S0_RX);
        }
        else
        {
          InterruptSet(RVA_IRQ_S1_RX);
        }
      }

      // Hack: Fixes OSSleepThread bug in Mario Party F.K.C 2
      if (memory.Read_U32(0x800FAE90) == 0x48000011)
      {
        memory.Write_U32(0x60000000, 0x800FAE90);
        ppc_state.iCache.Invalidate(memory, jit_interface, 0x800FAE90);
      }

      g_write_mode = 0;
      m_tx_data = 0;
      m_SI_offset = 0;
      memset(m_SI_buf, 0, sizeof(m_SI_buf));
      return;
    }

    // Device on SI0/1
    // First part of the command has the size
    if (m_SI_buf[0] == 2 && m_SI_offset == 5)
    {
      // Size is in bytes 2 and 3 as ASCII
      sscanf((char*)m_SI_buf + 1, "%02X", &g_write_size);
      g_write_mode = 0;
      m_rx_count = 0;
      return;
    }

    // If size is larger than 8 bytes it is split in three parts
    if (g_write_size > 8)
    {
      if (m_SI_offset == g_write_size - 3)
      {
        g_write_mode = 0;
        return;
      }
    }

    // 2nd part completed
    if (m_SI_buf[0] == 2 && m_SI_offset == g_write_size)
      if (m_SI_buf[g_write_size - 1] == 3)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: MPSI: {}", (char*)(m_SI_buf + 1));

        switch (MarioPartySerialCommand(*(u16*)(m_SI_buf + 3)))
        {
        // Send a fake reply to test if a reply is read
        default:
          WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: MPSI: Unhandled Command {:02x}{:02x}",
                       m_SI_buf[3], m_SI_buf[4]);
        case MarioPartySerialCommand::Version:
        case MarioPartySerialCommand::Reset:

          version_check_reply[3] = m_SI_buf[3];
          version_check_reply[4] = m_SI_buf[4];

          m_rx_count = sizeof(version_check_reply);
          m_rx_type = SerialReplies::MPSIVersion;

          // Checksum is ASCII
          version_check_reply[9] = 0;
          version_check_reply[10] = 0;
          sprintf((char*)version_check_reply + 9, "%02X",
                  CheckSum(version_check_reply, sizeof(version_check_reply)));

          // End byte
          version_check_reply[11] = 3;

          break;
        case MarioPartySerialCommand::Status:
          SetReply(SerialReplies::MPSIStatus, sizeof(board_status_reply));
          break;
        case MarioPartySerialCommand::Update:
          SetReply(SerialReplies::MPSIUpdate, sizeof(update_info_reply));
          break;
        case MarioPartySerialCommand::Config:
          SetReply(SerialReplies::MPSIConfig, sizeof(ConfigReply));
          break;
        case MarioPartySerialCommand::SS:
          SetReply(SerialReplies::SS, sizeof(SSReply));
          break;
        // No reply
        case MarioPartySerialCommand::Error:
        case MarioPartySerialCommand::MI:
        case MarioPartySerialCommand::MO:
        case MarioPartySerialCommand::PM:
        case MarioPartySerialCommand::BI:
        case MarioPartySerialCommand::BO:
        case MarioPartySerialCommand::OT:
        case MarioPartySerialCommand::CI:
        case MarioPartySerialCommand::WM:
        case MarioPartySerialCommand::BJ:
        case MarioPartySerialCommand::CU:
        case MarioPartySerialCommand::SU:
        case MarioPartySerialCommand::HP:
        case MarioPartySerialCommand::PU:
        case MarioPartySerialCommand::MT:
        case MarioPartySerialCommand::LR:
          m_rx_count = 0;
          break;
        }

        if (m_rx_count)
        {
          if (address == RVAMemoryMap::SI_0_TXD)
          {
            InterruptSet(RVA_IRQ_S0_RX);
          }
          else
          {
            InterruptSet(RVA_IRQ_S1_RX);
          }
          m_rx_offset = 0;
        }

        g_write_size = 0;
        g_write_mode = 0;

        m_SI_offset = 0;
        memset(m_SI_buf, 0, sizeof(m_SI_buf));
      }
  }
}

CEXIRVA::CEXIRVA(Core::System& system) : IEXIDevice(system), m_JVS(), m_SI0(), m_SI1()
{
  g_write_mode = 0;
  g_write_size = 0;

  g_IRQ_delay = 0;
  g_IRQ_type = RVAIRQMasks::RVA_IRQ_MASK;
  g_have_IRQ = false;

  g_game_type = GameType::Unknown;

  m_watch_dog_timer = 0;
  m_SRAM_offset = 0;

  /*
    Mario Party F.K.C 2's server and client have the same title ID.
    They expect different version replies and the SRAM size is different.
  */

  IOS::HLE::Kernel ios;
  const IOS::ES::TMDReader tmd_mp8 = ios.GetESCore().FindInstalledTMD(0x00015000344D504ALL);
  if (tmd_mp8.IsValid())
  {
    if (memcmp(tmd_mp8.GetSha1().data(), mario_party_FKC_2_server_1302141211, 16) == 0)
    {
      g_game_type = GameType::MarioPartyFKC2Server;
    }
    else if (memcmp(tmd_mp8.GetSha1().data(), mario_party_FKC_2_client_1301080914, 16) == 0)
    {
      g_game_type = GameType::MarioPartyFKC2Client;
    }
    else if (memcmp(tmd_mp8.GetSha1().data(), mario_party_FKC_2_client_1302141156, 16) == 0)
    {
      g_game_type = GameType::MarioPartyFKC2Client;
    }
    else  // Fall back to client for unkown MP8 versions
    {
      g_game_type = GameType::MarioPartyFKC2Client;
    }
  }
  else
  {
    // Tatsunoko VS Capcom is using the title ID "RAV0"
    const IOS::ES::TMDReader tmd_tvsc = ios.GetESCore().FindInstalledTMD(0x0001500052564130LL);
    if (tmd_tvsc.IsValid())
    {
      if (memcmp(tmd_tvsc.GetSha1().data(), tatsunoko_VS_capcom_0811051625, 16) == 0)
      {
        g_game_type = GameType::TatsunokoVSCapcom;
      }
    }
  }

  if (g_game_type == GameType::Unknown)
  {
    PanicAlertFmt("Failed to detected game!");
  }

  // RVA has an extra dedicated SRAM which can be accessed through the EXI interface.

  std::string SRAM_filename(File::GetUserPath(D_RVAUSER_IDX) + "SRAM_");

  switch (g_game_type)
  {
  default:
  case GameType::MarioPartyFKC2Server:
    m_SRAM_size = 0x1400;
    m_SRAM_check_off = 0x13F8;
    SRAM_filename += "MarioPartyFKC2Server.bin";
    break;
  case GameType::MarioPartyFKC2Client:
    m_SRAM_size = 0x1A40;
    m_SRAM_check_off = 0x1A28;
    SRAM_filename += "MarioPartyFKC2Client.bin";
    break;
  case GameType::TatsunokoVSCapcom:
    m_SRAM_size = 0x8000;
    m_SRAM_check_off = 0;
    SRAM_filename += "TatsunokoVSCapcom.bin";
    break;
  }

  if (File::Exists(File::GetUserPath(D_RVAUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_RVAUSER_IDX));
  }

  if (File::IsFile(SRAM_filename))
  {
    m_SRAM = new File::IOFile(SRAM_filename, "rb+");
  }
  else
  {
    m_SRAM = new File::IOFile(SRAM_filename, "wb+");
  }

  // The RVA device will be opened twice
  if (!m_SRAM->IsGood())
  {
    // PanicAlertFmt("Failed to open SRAM\nFile might be in use.");
    return;
  }

  // Game doesn't need a file prepared
  if (g_game_type == GameType::TatsunokoVSCapcom)
  {
    return;
  }

  // Setup new file for Mario Party games
  if (m_SRAM->GetSize() == 0)
  {
    u8* data = new u8[m_SRAM_size];

    memset(data, 0, m_SRAM_size);

    strcpy((char*)data, "IDX");

    for (u32 i = 1; i < 5; ++i)
    {
      *(u64*)(data + 8) = i;
      *(u32*)(data + m_SRAM_check_off) =
          Common::swap32(Common::ComputeCRC32(data + 0x10, m_SRAM_check_off - 0x10));
      m_SRAM->WriteBytes(data, m_SRAM_size);
    }

    delete[] data;

    m_SRAM->Flush();
  }
}

CEXIRVA::~CEXIRVA()
{
  if (!m_SRAM)
  {
    m_SRAM->Close();
    delete m_SRAM;
  }
}

bool CEXIRVA::IsPresent() const
{
  return true;
}

void CEXIRVA::ImmWrite(u32 data, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  auto& ppc_state = system.GetPPCState();
  auto& jit_interface = system.GetJitInterface();

  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmWrite: {:08x} {}", data, size);

  if (RVAMemoryMap(data & 0x7F000000) == RVAMemoryMap::SRAM_SET_OFFSET)
  {
    m_SRAM_offset = (data >> 6) & 0xFFFF;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: SRAM Set Offset: {:08x}", m_SRAM_offset);
    return;
  }

  // IRQ handler
  if (data == 0x2C000700)
  {
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ(W)");
    m_address = RVAMemoryMap::JVS_IO_SI_0_1_CSR;
    return;
  }

  if (!g_write_mode)
  {
    m_address = RVAMemoryMap((data & ~0x80000000) >> 6);

    if (data & 0x80000000)
    {
      g_write_mode = 1;
      return;
    }
  }

  if (m_address == RVAMemoryMap::JVS_ID)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: Get ID");

    // HACK: MP FKC 2 Client: Patch out error 9906/8001
    if (memory.Read_U32(0x8010A9FC) == 0x480E27CD)
    {
      memory.Write_U32(0x60000000, 0x8010A9FC);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010A9FC);

      memory.Write_U32(0x48000030, 0x8010AC10);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010AC10);

      memory.Write_U32(0x60000000, 0x8010AC4C);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010AC4C);

      // Check config reply
      memory.Write_U32(0x60000000, 0x801FCAF8);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x801FCABC);

      // Check status reply
      memory.Write_U32(0x4800004C, 0x8010B7BC);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010B7BC);
      memory.Write_U32(0x60000000, 0x8010B818);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010B818);
      memory.Write_U32(0x60000000, 0x801FCBFC);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010B818);

      // Patch checks
      memory.Write_U32(0x4800004C, 0x8010C248);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010C248);

      memory.Write_U32(0x4800004C, 0x8010C358);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010C358);

      memory.Write_U32(0x4800004C, 0x8010C444);
      ppc_state.iCache.Invalidate(memory, jit_interface, 0x8010C358);

      // check error
      // memory.Write_U32(0x4E800020, 0x801ED1C8);
      // ppc_state.iCache.Invalidate(memory, jit_interface, 0x801ED1C8);
    }
    return;
  }

  switch (m_address)
  {
  case RVAMemoryMap::WATCH_DOG:
    m_watch_dog_timer = Common::swap32(data);
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: Watchdog Timer: {:02x}", m_watch_dog_timer);
    g_write_mode = 0;
    return;

  case RVAMemoryMap::JVS_IO_SI_0_1_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-IRQ(W): {:02x}", data);
    g_write_mode = 0;
    return;

  case RVAMemoryMap::JVS_SWITCHES:
    m_JVS.Write(m_address, data);
    return;

  case RVAMemoryMap::JVS_IO_CSR:
  case RVAMemoryMap::JVS_IO_TXD:
  case RVAMemoryMap::JVS_IO_RXD:
  case RVAMemoryMap::JVS_IO_RX_CNT:
  case RVAMemoryMap::JVS_IO_TX_LEN:
  case RVAMemoryMap::JVS_IO_RX_LEN:
  case RVAMemoryMap::JVS_IO_DCSR:
    m_JVS.Write(m_address, data);
    return;

  case RVAMemoryMap::SI_0_CSR:
  case RVAMemoryMap::SI_0_TXD:
  case RVAMemoryMap::SI_0_RXD:
  case RVAMemoryMap::SI_0_RX_CNT:
  case RVAMemoryMap::SI_0_TX_RAT:
  case RVAMemoryMap::SI_0_RX_RAT:
    m_SI0.Write(m_address, data);
    return;

  case RVAMemoryMap::SI_1_CSR:
  case RVAMemoryMap::SI_1_TXD:
  case RVAMemoryMap::SI_1_RXD:
  case RVAMemoryMap::SI_1_RX_CNT:
  case RVAMemoryMap::SI_1_TX_RAT:
  case RVAMemoryMap::SI_1_RX_RAT:
    m_SI1.Write(m_address, data);
    return;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "RVA: Unhandled Address: {:08x} ({:08x}) {}", data,
                  (u32)m_address, size);
    break;
  }
}

u32 CEXIRVA::ImmRead(u32 size)
{
  u32 data = 0;

  switch (m_address)
  {
  case RVAMemoryMap::JVS_ID:
    data = Common::swap32(EXI_DEVTYPE_RVA);
    break;

  case RVAMemoryMap::WATCH_DOG:
    data = Common::swap32(m_watch_dog_timer);
    break;

  case RVAMemoryMap::JVS_IO_SI_0_1_CSR:
    data = (u32)g_IRQ_type;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ Status:{:08x}", data);
    data = Common::swap32(data);
    break;

  case RVAMemoryMap::JVS_SWITCHES:
    data = m_JVS.Read(m_address, size);
    break;

  case RVAMemoryMap::JVS_IO_CSR:
  case RVAMemoryMap::JVS_IO_TXD:
  case RVAMemoryMap::JVS_IO_RXD:
  case RVAMemoryMap::JVS_IO_RX_CNT:
  case RVAMemoryMap::JVS_IO_TX_LEN:
  case RVAMemoryMap::JVS_IO_RX_LEN:
  case RVAMemoryMap::JVS_IO_DCSR:
    data = Common::swap32(m_JVS.Read(m_address, size));
    break;

  case RVAMemoryMap::SI_0_CSR:
  case RVAMemoryMap::SI_0_TXD:
  case RVAMemoryMap::SI_0_RXD:
  case RVAMemoryMap::SI_0_RX_CNT:
  case RVAMemoryMap::SI_0_TX_RAT:
  case RVAMemoryMap::SI_0_RX_RAT:
    data = Common::swap32(m_SI0.Read(m_address, size));
    break;

  case RVAMemoryMap::SI_1_CSR:
  case RVAMemoryMap::SI_1_TXD:
  case RVAMemoryMap::SI_1_RXD:
  case RVAMemoryMap::SI_1_RX_CNT:
  case RVAMemoryMap::SI_1_TX_RAT:
  case RVAMemoryMap::SI_1_RX_RAT:
    data = Common::swap32(m_SI1.Read(m_address, size));
    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "RVA: Unhandled Address: {:08x} ({:08x}) {}", data,
                  (u32)m_address, size);
    break;
  }

  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmRead {:08x} {:08x}  {}", (u32)m_address, data, size);

  return data;
}

void CEXIRVA::DMAWrite(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMAWrite: {:08x} {:08x} {:x}", address, m_SRAM_offset,
                size);

  if (m_SRAM)
  {
    m_SRAM->Seek(m_SRAM_offset, File::SeekOrigin::Begin);

    m_SRAM->WriteBytes(memory.GetPointer(address), size);

    m_SRAM->Flush();
  }
}

void CEXIRVA::DMARead(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMARead: {:08x} {:08x} {:x}", address, m_SRAM_offset,
                size);

  if (m_SRAM)
  {
    m_SRAM->Seek(m_SRAM_offset, File::SeekOrigin::Begin);

    m_SRAM->Flush();

    m_SRAM->ReadBytes(memory.GetPointer(address), size);
  }
}

bool CEXIRVA::IsInterruptSet()
{
  if (g_have_IRQ)
  {
    if (g_IRQ_delay-- == 0)
    {
      g_have_IRQ = false;
    }
    return 1;
  }
  return 0;
}

}  // namespace ExpansionInterface
