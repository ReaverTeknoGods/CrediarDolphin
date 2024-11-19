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

namespace ExpansionInterface
{

static const wchar_t character_display_code[] = {
    /*0x04                    0x08                       0x0C*/
    /* 0x00 */ L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ', /* 0x00 Display Commands */
    /* 0x10 */ L' ',
    L' ',
    ' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ',
    L' ', /* 0x10 Display Commands */
    /* 0x20 */ L'　',
    L'!',
    L'\"',
    L'#',
    L'$',
    L'%',
    L'&',
    L'\'',
    L'(',
    L')',
    L'*',
    L'+',
    L',',
    L'-',
    L'.',
    L'/', /* 0x20 */
    /* 0x30 */ L'0',
    L'1',
    L'2',
    L'3',
    L'4',
    L'5',
    L'6',
    L'7',
    L'8',
    L'9',
    L':',
    L';',
    L'<',
    L'=',
    L'>',
    L'?', /* 0x30 */
    /* 0x40 */ L'@',
    L'A',
    L'B',
    L'C',
    L'D',
    L'E',
    L'F',
    L'G',
    L'H',
    L'I',
    L'J',
    L'K',
    L'L',
    L'M',
    L'N',
    L'O', /* 0x40 */
    /* 0x50 */ L'P',
    L'Q',
    L'R',
    L'S',
    L'T',
    L'U',
    L'V',
    L'W',
    L'X',
    L'Y',
    L'Z',
    L'[',
    L'Y',
    L']',
    L'^',
    L'_', /* 0x50 */
    /* 0x60 */ L'`',
    L'a',
    L'b',
    L'c',
    L'd',
    L'e',
    L'f',
    L'g',
    L'h',
    L'i',
    L'j',
    L'k',
    L'L',
    L'm',
    L'n',
    L'o', /* 0x60 */
    /* 0x70 */ L'p',
    L'q',
    L'r',
    L's',
    L't',
    L'u',
    L'v',
    L'w',
    L'x',
    L'y',
    L'z',
    L'{',
    L'|',
    L'}',
    L'?',
    L'?', /* 0x70 */
    /* 0x80 */ L'α',
    L'β',
    L'γ',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'µ',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?', /* 0x80 */
    /* 0x90 */ L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'²',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?', /* 0x90 */
    /* 0xA0 */ L'！',
    L'。',
    L'┌',
    L'┘',
    L'、',
    L'・',
    L'ヲ',
    L'ァ',
    L'ィ',
    L'ゥ',
    L'ェ',
    L'ォ',
    L'ャ',
    L'ュ',
    L'ョ',
    L'ツ', /* 0xA0 */
    /* 0xB0 */ L'ー',
    L'ア',
    L'イ',
    L'ウ',
    L'エ',
    L'オ',
    L'カ',
    L'キ',
    L'ク',
    L'ケ',
    L'コ',
    L'サ',
    L'シ',
    L'ス',
    L'セ',
    L'ソ', /* 0xB0 */
    /* 0xC0 */ L'タ',
    L'チ',
    L'ツ',
    L'テ',
    L'ト',
    L'ナ',
    L'二',
    L'ヌ',
    L'ネ',
    L'ノ',
    L'ハ',
    L'ヒ',
    L'フ',
    L'へ',
    L'ホ',
    L'マ', /* 0xC0 */
    /* 0xD0 */ L'ミ',
    L'ム',
    L'メ',
    L'モ',
    L'ヤ',
    L'ユ',
    L'ヨ',
    L'ラ',
    L'リ',
    L'ル',
    L'レ',
    L'ロ',
    L'ワ',
    L'ン',
    L'゛',
    L'゜', /* 0xD0 */
    /* 0xE0 */ L'↑',
    L'↓',
    L'?',
    L'?',
    L'?',
    L'日',
    L'月',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?', /* 0xE0 */
    /* 0xF0 */ L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?',
    L'?', /* 0xF0 */
};

static const u8 MatrixDisplayIDReply[] = {"\x02M202MD12BA\x03"};

u8 VersionCheckReply[] = {2, '0', 'C', 'V', 'R', '0', '1', '0', '4', 'E', '5', 3};

static const u8 BoardStatusReply[] = {2, '0', 'C', 'S', 'T', '8', '0', '0', '0', 'E', '7', 3};

static const u8 UpdateInfoReply[] = {2, '0', 'A', 'U', 'P', '0', '0', '7', 'B', 3};

u8 MIReply[9] = {
    0x02, 0x30, 0x39, 0x4D, 0x49, 0x30, 0x33, 0x34, 0x03,
};

u8 MOReply[9] = {
    0x02, 0x30, 0x39, 0x4D, 0x4F, 0x30, 0x33, 0x41, 0x03,
};

u8 PMReply[19] = {
    0x02, 0x31, 0x33, 0x50, 0x4D, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x36, 0x03,
};

u8 BIReply[9] = {
    0x02, 0x30, 0x39, 0x42, 0x49, 0x30, 0x32, 0x39, 0x03,
};

u8 BOReply[9] = {
    0x02, 0x30, 0x39, 0x42, 0x4F, 0x30, 0x32, 0x46, 0x03,
};

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

static const u8 MarioPartyFKC2Server_1302141211[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 MarioPartyFKC2Client_1301080914[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 MarioPartyFKC2Client_1302141156[20] = {
    0x9F, 0x67, 0x2B, 0x38, 0x60, 0x30, 0xB4, 0x9F, 0x17, 0x53,
    0x80, 0xE2, 0x42, 0x42, 0xEC, 0x5E, 0x3F, 0x2F, 0x5E, 0xE6,
};

static const u8 TatsunokoVSCapcom_0811051625[20] = {
    0x90, 0x2D, 0xDE, 0x93, 0xAB, 0xBF, 0x20, 0xD2, 0xB9, 0x6A,
    0x2D, 0xAB, 0xC3, 0x9E, 0x06, 0x74, 0xB7, 0x1C, 0x60, 0x30,
};

bool g_have_irq;
u32 g_irq_delay;
u32 g_irq_type;

u32 g_exi_write_mode;
u32 g_exi_write_size;
u32 g_exi_write_count;

u32 g_game_type = Unknown;

static void InterruptSet(RVAIRQMasks Interrupt)
{
  g_have_irq = true;
  g_irq_type = RVA_IRQ_MASK & ~Interrupt;
  g_irq_delay = 10;
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

CEXISI::CEXISI()
{
  m_csr = 0;
  m_tx_data = 0;
  m_rx_data = 0;
  m_rx_cnt = 0;
  m_tx_rat = 0;
  m_rx_rat = 0;
  m_status = 0;

  g_irq_type = RVAIRQMasks::RVA_IRQ_MASK;

  m_siboff = 0;
  memset(m_sibuf, 0, sizeof(m_sibuf));
  m_rxoff = 0;
  m_irqc = 0;
}

CEXISI::~CEXISI() = default;

void CEXISI::SetReply(u32 length, SerialReplies reply)
{
  m_rx_cnt = length;
  m_rx_type = reply;
  m_rxoff = 0;
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
    data = m_siboff;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TX-CNT(R):{:08x}:{:02X}", size, data);
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RXD:
    data = m_rx_data;
    switch (SerialReplies(m_rx_type))
    {
    case SerialReplies::SerialVersion:
    {
      data = SI0_reply_version[m_rxoff++];
      if (m_rxoff > sizeof(SI0_reply_version))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::Serial_6:
    {
      data = SI0_reply_6[m_rxoff++];
      if (m_rxoff > sizeof(SI0_reply_6))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOServices:
    {
      data = AMO_reply_se[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_se))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionServer:
    {
      data = AMO_reply_version[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_version))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionClient:
    {
      data = AMO_reply_client_version[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_client_version))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIVersion:
    {
      data = VersionCheckReply[m_rxoff++];
      if (m_rxoff > sizeof(VersionCheckReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIStatus:
    {
      data = BoardStatusReply[m_rxoff++];
      if (m_rxoff > sizeof(BoardStatusReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIUpdate:
    {
      data = UpdateInfoReply[m_rxoff++];
      if (m_rxoff > sizeof(UpdateInfoReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::SS:
    {
      data = SSReply[m_rxoff++];
      if (m_rxoff > sizeof(SSReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIConfig:
    {
      data = ConfigReply[m_rxoff++];
      if (m_rxoff > sizeof(ConfigReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    }

    if (m_rx_cnt > 0)
      m_rx_cnt--;

    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RXD(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_0_RX_CNT:
    data = m_rx_cnt;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-CNT(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_RXD:
    switch (SerialReplies(m_rx_type))
    {
    case SerialReplies::MDCDisplayID:
    {
      data = MatrixDisplayIDReply[m_rxoff++];
      if (m_rxoff > sizeof(MatrixDisplayIDReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOServices:
    {
      data = AMO_reply_se[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_se))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionServer:
    {
      data = AMO_reply_version[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_version))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOVersionClient:
    {
      data = AMO_reply_client_version[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_client_version))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::AMOInfo:
    {
      data = AMO_reply_ie[m_rxoff++];
      if (m_rxoff > sizeof(AMO_reply_ie))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIVersion:
    {
      data = VersionCheckReply[m_rxoff++];
      if (m_rxoff > sizeof(VersionCheckReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    case SerialReplies::MPSIStatus:
    {
      data = BoardStatusReply[m_rxoff++];
      if (m_rxoff > sizeof(BoardStatusReply))
      {
        m_rxoff = 0;
      }
    }
    break;
    default:
    {
      data = m_rx_data;
    }
    break;
    }
    if (m_rx_cnt > 0)
      m_rx_cnt--;

    if (m_rx_cnt == 0)
    {
      if (memory.Read_U32(0x800FAE90) == 0x60000000)
      {
        memory.Write_U32(0x48000011, 0x800FAE90);
        ppc_state.iCache.Invalidate(memory, jit_interface, 0x800FAE90);
      }
    }

    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_TX_CNT:
    data = m_siboff;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TX-CNT(R):{:08x}:{:02X}", size, data);
    m_status = 0;
    break;
  case RVAMemoryMap::SI_1_RX_RAT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(R):{:08x}", size);
    break;
  case RVAMemoryMap::SI_1_CSR:
    data = m_csr | 0x40;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(R):{:08x}:{:02X}", size, data);
    break;
  case RVAMemoryMap::SI_1_RX_CNT:
    data = m_rx_cnt;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(R):{:08x}:{:02X}", size, data);
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
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_TXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_sibuf[m_siboff++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_1_TXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_sibuf[m_siboff++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_0_RXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RXD:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RX_CNT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-CNT(W): {:02x}", value_swap);
    m_rx_cnt = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RX_CNT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(W): {:02x}", value_swap);
    m_rx_cnt = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_TX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TX-RAT(W): {:02x}", value_swap);
    m_tx_rat = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_TX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TX-RAT(W): {:02x}", value_swap);
    m_tx_rat = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_0_RX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-RAT(W): {:02x}", value_swap);
    m_rx_rat = value_swap;

    if (m_rx_rat == 0x19 && m_csr == 0x03)
    {
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0:IRQ Request? {:02x}", m_irqc);
      m_irqc++;
    }

    if (m_irqc == 4)
    {
      InterruptSet(RVA_IRQ_S0_RX);
      SetReply(sizeof(SI0_reply_6), SerialReplies::Serial_6);
    }
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::SI_1_RX_RAT:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(W): {:02x}", value_swap);
    m_rx_rat = value_swap;
    g_exi_write_mode = 0;
    break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI(?): Unhandled address write: {:08x} {:08x}",
                  (u32)address, value_swap);
    break;
  }

  if (m_siboff)
  {
    // Only allow valid commands
    if (m_sibuf[0] != 2 && m_sibuf[0] != 0x2F)
    {
      m_siboff = 0;
      return;
    }
  }

  if (m_siboff > 2)
  {
    // check for matrix display command
    if (m_sibuf[0] == 2 && m_sibuf[1] == 0x10)
    {
      // size is in byte 4
      if (m_siboff >= 4)
      {
        // Check for full packet
        if (m_siboff >= m_sibuf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: {:02x}", m_sibuf[4]);

          switch (MatrixDisplayCommand(m_sibuf[4]))
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
            if (m_sibuf[6] == 0x02)
            {
              memset(m_sibuf, 0, sizeof(m_sibuf));
              m_sibuf[0] = 0x02;
              m_siboff = 1;
              return;
            }
            break;
          case MatrixDisplayCommand::DisplayID:
          {
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Get Display ID");
            InterruptSet(RVA_IRQ_S1_RX);
            SetReply(sizeof(MatrixDisplayIDReply), SerialReplies::MDCDisplayID);

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

            for (u32 i = 0; i < strlen((char*)(m_sibuf + 4)) - 1; ++i)
            {
              mtext[i] = character_display_code[m_sibuf[i + 4]];
            }
#ifdef _WIN32
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Text: {}", TStrToUTF8(mtext));
#endif
            // WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Unhandled Command {:02x}",
            // m_sibuf[4]);
            break;
          }

          g_exi_write_size = 0;
          g_exi_write_count = 0;
          g_exi_write_mode = 0;
          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
      return;
    }

    // Device on SI0
    if (m_sibuf[0] == 2 && m_sibuf[1] == 3)
    {
      // Size is in byte 4
      if (m_siboff >= 4)
      {
        // Check for full packet
        if (m_siboff >= m_sibuf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK3: {:02x}", m_sibuf[4]);

          g_exi_write_size = 0;
          g_exi_write_count = 0;
          g_exi_write_mode = 0;
          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
      return;
    }

    if (m_sibuf[0] == 2 && m_sibuf[1] == 4)
    {
      if (m_siboff >= 5)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK4: {:02x}", m_sibuf[4]);

        InterruptSet(RVA_IRQ_S0_RX);

        m_rx_cnt = sizeof(SI0_reply_version);
        m_rx_type = SerialReplies::SerialVersion;
        m_rxoff = 0;

        g_exi_write_size = 0;
        g_exi_write_count = 0;
        g_exi_write_mode = 0;
        m_tx_data = 0;
        m_siboff = 0;
        memset(m_sibuf, 0, sizeof(m_sibuf));
      }
      return;
    }

    // ?
    if (m_sibuf[0] == 2 && m_sibuf[1] == 6)
    {
      // Size is in byte 4
      if (m_siboff >= 4)
      {
        // Check for full packet
        if (m_siboff == m_sibuf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK6: {:02x}", m_sibuf[2]);

          InterruptSet(RVA_IRQ_S0_RX);

          m_rx_cnt = sizeof(SI0_reply_6);
          m_rx_type = SerialReplies::Serial_6;
          m_rxoff = 0;

          g_exi_write_size = 0;
          g_exi_write_count = 0;
          g_exi_write_mode = 0;
          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
      return;
    }

    // AMO board CMD:
    if (m_sibuf[0] == 0x2F && m_siboff == 7)
    {
      // replace new lines for nicer print
      for (u32 i = 0; m_sibuf[i] != 3; ++i)
      {
        if (m_sibuf[i] == '\n')
          m_sibuf[i] = ' ';
      }

      m_sibuf[6] = 0;

      NOTICE_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: AMO: {}", (char*)m_sibuf);

      switch (m_sibuf[1])
      {
      case 's':
        m_rx_type = SerialReplies::AMOServices;
        m_rx_cnt = sizeof(AMO_reply_se);
        break;
      case 'v':
        switch (g_game_type)
        {
        default:
        case MarioPartyFKC2Server:
          m_rx_type = SerialReplies::AMOVersionServer;
          m_rx_cnt = sizeof(AMO_reply_version);
          break;
        case MarioPartyFKC2Client:
          m_rx_type = SerialReplies::AMOVersionClient;
          m_rx_cnt = sizeof(AMO_reply_client_version);
          break;
        }

        break;
      case 'i':
        m_rx_type = SerialReplies::AMOInfo;
        m_rx_cnt = sizeof(AMO_reply_ie);
        break;
      default:
        WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: AMO: Unhandled Command {:02x}", m_sibuf[1]);
        break;
      }

      if (m_rx_cnt)
      {
        if (address == RVAMemoryMap::SI_0_TXD)
        {
          InterruptSet(RVA_IRQ_S0_RX);
        }
        else
        {
          InterruptSet(RVA_IRQ_S1_RX);
        }
        m_rxoff = 0;
      }

      // Hack: Fixes OSSleepThread bug in Mario Party F.K.C 2
      if (memory.Read_U32(0x800FAE90) == 0x48000011)
      {
        memory.Write_U32(0x60000000, 0x800FAE90);
        ppc_state.iCache.Invalidate(memory, jit_interface, 0x800FAE90);
      }

      g_exi_write_mode = 0;
      m_tx_data = 0;
      m_siboff = 0;
      memset(m_sibuf, 0, sizeof(m_sibuf));
      return;
    }

    // Device on SI0/1
    // First part of the command has the size
    if (m_sibuf[0] == 2 && m_siboff == 5)
    {
      // Size is in bytes 2 and 3 as ASCII
      sscanf((char*)m_sibuf + 1, "%02X", &g_exi_write_size);
      g_exi_write_mode = 0;
      m_rx_cnt = 0;
      return;
    }

    // If size is larger than 8 bytes it is split in three parts
    if (g_exi_write_size > 8)
    {
      if (m_siboff == g_exi_write_size - 3)
      {
        g_exi_write_mode = 0;
        return;
      }
    }

    // 2nd part completed
    if (m_sibuf[0] == 2 && m_siboff == g_exi_write_size)
      if (m_sibuf[g_exi_write_size - 1] == 3)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: MPSI: {}", (char*)(m_sibuf + 1));

        switch (MarioPartySerialCommand(*(u16*)(m_sibuf + 3)))
        {
        // Send a fake reply to test if a reply is read
        default:
          WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: MPSI: Unhandled Command {:02x}{:02x}",
                       m_sibuf[3], m_sibuf[4]);
        case MarioPartySerialCommand::Version:
        case MarioPartySerialCommand::Reset:

          VersionCheckReply[3] = m_sibuf[3];
          VersionCheckReply[4] = m_sibuf[4];

          m_rx_cnt = sizeof(VersionCheckReply);
          m_rx_type = SerialReplies::MPSIVersion;

          // Checksum is ASCII
          VersionCheckReply[9] = 0;
          VersionCheckReply[10] = 0;
          sprintf((char*)VersionCheckReply + 9, "%02X",
                  CheckSum(VersionCheckReply, sizeof(VersionCheckReply)));

          // End byte
          VersionCheckReply[11] = 3;

          break;
        case MarioPartySerialCommand::Status:
          m_rx_cnt = sizeof(BoardStatusReply);
          m_rx_type = SerialReplies::MPSIStatus;
          break;
        case MarioPartySerialCommand::Update:
          m_rx_cnt = sizeof(UpdateInfoReply);
          m_rx_type = SerialReplies::MPSIUpdate;
          break;
        case MarioPartySerialCommand::Config:
          m_rx_cnt = sizeof(ConfigReply);
          m_rx_type = SerialReplies::MPSIConfig;
          break;
        case MarioPartySerialCommand::SS:
          m_rx_cnt = sizeof(SSReply);
          m_rx_type = SerialReplies::SS;
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
          m_rx_cnt = 0;
          break;
        }

        if (m_rx_cnt)
        {
          if (address == RVAMemoryMap::SI_0_TXD)
          {
            InterruptSet(RVA_IRQ_S0_RX);
          }
          else
          {
            InterruptSet(RVA_IRQ_S1_RX);
          }
          m_rxoff = 0;
        }

        g_exi_write_size = 0;
        g_exi_write_mode = 0;

        m_siboff = 0;
        memset(m_sibuf, 0, sizeof(m_sibuf));
      }
  }
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
  m_tx_data_cnt = 0;
  m_rx_data = 0;
  m_tx_len = 0;
  m_rx_len = 0;
  m_dscr = 0;
  g_exi_write_size = 0;
  g_exi_write_count = 0;

  memset(m_coin, 0, sizeof(m_coin));
  memset(m_coin_pressed, 0, sizeof(m_coin_pressed));

  m_jvs_offset = 0;
  memset(m_jvs_data, 0, sizeof(m_jvs_data));
}

u32 CEXIJVS::Read(RVAMemoryMap address, u32 size)
{
  static u32 switch_status = 0xFFFFFFFF;
  u32 data = 0;
  GCPadStatus pad_status;

  switch (address)
  {
  case RVAMemoryMap::JVS_Switches:
    pad_status = Pad::GetStatus(0);

    if (pad_status.button & PAD_TRIGGER_Z)
      switch_status &= ~0x10000000;
    else
      switch_status |= 0x10000000;

    if (pad_status.button & PAD_TRIGGER_L)
      switch_status &= ~0x20000000;
    else
      switch_status |= 0x20000000;

    data = switch_status;
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    data = m_csr | 0x40;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: CSR(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_TXD:
    data = 0;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: TXD(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    data = m_dscr;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: DCSR(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_RX_CNT:
    data = m_rx_cnt;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: RX_CNT(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_RXD:
  {
    data = m_jvs_reply_data[m_jvs_reply_offset - m_rx_cnt];
    if (m_rx_cnt > 0)
      m_rx_cnt--;

    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: RX_RXD(R): {:02x}", data);
  }
  break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "JVS: Unhandled address read: {:08x}", (u32)address);
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
  case RVAMemoryMap::JVS_Switches:
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_TXD:
  {
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: TX_TXD(W): {:02x}", value_swap);

    m_tx_data = value_swap;
    m_jvs_data[m_jvs_offset++] = value_swap;

    if (m_jvs_offset > 2)
    {
      u8 size = m_jvs_data[2] + 3;
      if (m_jvs_offset == size)
      {
        JVSIOMessage msg;

        u8 node = m_jvs_data[1];

        msg.start(node);
        msg.addData(1);

        u8* jvs_io = m_jvs_data + 3;
        m_jvs_offset--;  // checksum

        while (jvs_io < (m_jvs_data + m_jvs_offset))
        {
          int cmd = *jvs_io++;
          DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:node={}, command={:02x}", node, cmd);
          switch (JVSIOCommands(cmd))
          {
          case JVSIOCommands::IOID:
            msg.addData(1);
            msg.addData("NO BRAND;NAOMI CONVERTER98701;VER2.0;");
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 10, BoardID");
            msg.addData((u32)0);
            break;
          case JVSIOCommands::CommandRevision:
            msg.addData(1);
            msg.addData(0x11);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 11, CMDFormatRevision");
            break;
          case JVSIOCommands::JVRevision:
            msg.addData(1);
            msg.addData(0x20);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 12, Revision");
            break;
          case JVSIOCommands::CommunicationVersion:
            msg.addData(1);
            msg.addData(0x10);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 13, COMVersion");
            break;
          case JVSIOCommands::CheckFunctionality:
            msg.addData(1);
            // 2 Player (12bit), 2 Coin slot, 8 Analog-in
            msg.addData((void*)"\x01\x02\x0C\x00", 4);
            msg.addData((void*)"\x02\x02\x00\x00", 4);
            msg.addData((void*)"\x03\x08\x00\x00", 4);
            msg.addData((void*)"\x00\x00\x00\x00", 4);
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 14, SlaveFeatures");
            break;
          case JVSIOCommands::SwitchesInput:
          {
            int player_count = *jvs_io++;
            int player_byte_count = *jvs_io++;

            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO:  Command 20, SwitchInputs: {} {}",
                         player_count, player_byte_count);

            InterruptSet(RVA_IRQ_JVS_RS_RX);

            msg.addData(1);

            GCPadStatus PadStatus;
            PadStatus = Pad::GetStatus(0);

            // Test button
            if (PadStatus.substickX > PadStatus.C_STICK_CENTER_X)
              msg.addData(0x80);
            else
              msg.addData((u32)0x00);

            for (int i = 0; i < player_count; ++i)
            {
              u8 player_data[3] = {0, 0, 0};

              PadStatus = Pad::GetStatus(i);
              // Start
              if (PadStatus.button & PAD_BUTTON_START)
                player_data[0] |= 0x80;
              // Service button
              if (PadStatus.substickY > PadStatus.C_STICK_CENTER_Y)
                player_data[0] |= 0x40;
              // Shot 1
              if (PadStatus.button & PAD_BUTTON_A)
                player_data[0] |= 0x02;
              // Shot 2
              if (PadStatus.button & PAD_BUTTON_B)
                player_data[0] |= 0x01;
              // Shot 3
              if (PadStatus.button & PAD_BUTTON_X)
                player_data[1] |= 0x80;
              // Shot 4
              if (PadStatus.button & PAD_BUTTON_Y)
                player_data[1] |= 0x40;
              // Shot 5
              if (PadStatus.button & PAD_TRIGGER_L)
                player_data[1] |= 0x20;
              // Shot 6
              if (PadStatus.button & PAD_TRIGGER_R)
                player_data[1] |= 0x10;
              // Left
              if (PadStatus.stickX < (PadStatus.MAIN_STICK_CENTER_X - 34))
                player_data[0] |= 0x08;
              // Up
              if (PadStatus.stickY > (PadStatus.MAIN_STICK_CENTER_Y + 34))
                player_data[0] |= 0x20;
              // Right
              if (PadStatus.stickX > (PadStatus.MAIN_STICK_CENTER_X + 34))
                player_data[0] |= 0x04;
              // Down
              if (PadStatus.stickY < (PadStatus.MAIN_STICK_CENTER_Y - 34))
                player_data[0] |= 0x10;

              for (int j = 0; j < player_byte_count; ++j)
                msg.addData(player_data[j]);
            }
          }
          break;
          case JVSIOCommands::CoinInput:
          {
            int slots = *jvs_io++;
            msg.addData(1);
            for (int i = 0; i < slots; i++)
            {
              GCPadStatus PadStatus;
              PadStatus = Pad::GetStatus(i);
              if ((PadStatus.button & PAD_TRIGGER_Z) && !m_coin_pressed[i])
              {
                m_coin[i]++;
              }
              m_coin_pressed[i] = PadStatus.button & PAD_TRIGGER_Z;
              msg.addData((m_coin[i] >> 8) & 0x3f);
              msg.addData(m_coin[i] & 0xff);
            }
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO: Command 21, Get Coins Slots:{}", slots);
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
              INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO: RESET");
              m_dscr |= 0x80;
            }
            msg.addData(1);

            break;
          case JVSIOCommands::SetAddress:
            node = *jvs_io++;
            INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO: SET ADDRESS, node={}", node);
            msg.addData(node == 1);
            m_dscr &= ~0x80;
            break;
          default:
            ERROR_LOG_FMT(EXPANSIONINTERFACE, "JVS-IO: node={}, command={:02x}", node, cmd);
            break;
          }
        }

        msg.end();

        memset(m_jvs_reply_data, 0, sizeof(m_jvs_reply_data));

        m_jvs_reply_offset = msg.m_ptr;
        m_rx_cnt = msg.m_ptr;

        memcpy(m_jvs_reply_data, msg.m_msg, msg.m_ptr);

        memset(m_jvs_data, 0, sizeof(m_jvs_data));
        m_jvs_offset = 0;
        g_exi_write_mode = 0;
      }
    }
  }
  break;
  case RVAMemoryMap::JVS_IO_TX_LEN:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: TX_LEN(W): {:02x}", value_swap);
    m_tx_len = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_RX_LEN:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: RX_LEN(W): {:02x}", value_swap);
    m_rx_len = value_swap;
    g_exi_write_mode = 0;
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "JVS: DCSR(W): {:02x}", value_swap);
    m_dscr = value_swap;
    g_exi_write_mode = 0;
    break;
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "JVS: Unhandled address write: {:08x} {:02x}", (u32)address,
                  value);
    break;
  }
}

CEXIJVS::~CEXIJVS()
{
}

CEXIRVA::CEXIRVA(Core::System& system) : IEXIDevice(system), m_jvs(), m_si0(), m_si1()
{
  auto& memory = system.GetMemory();

  g_exi_write_mode = 0;
  m_watch_dog_timer = 0;
  m_backup_offset = 0;
  g_have_irq = false;
  g_irq_delay = 0;
  g_irq_type = 0;

  // Do a few sanity checks

  memory.Init();

  if (memory.GetRamSize() != Memory::MEM1_SIZE_GDEV)
  {
    CriticalAlertFmt("Please set MEM1 size to 50MB!");
    return;
  }

  if (memory.GetExRamSize() != Memory::MEM2_SIZE_NDEV)
  {
    CriticalAlertFmt("Please set MEM2 size to 128MB!");
    return;
  }

  ExpansionInterface::EXIDeviceType Type = Config::Get(Config::MAIN_SLOT_A);
  if (Type != ExpansionInterface::EXIDeviceType::RVA)
  {
    CriticalAlertFmt("Please set both memcard slots\nto: Wii Arcadeboard");
    return;
  }

  Type = Config::Get(Config::MAIN_SLOT_B);
  if (Type != ExpansionInterface::EXIDeviceType::RVA)
  {
    CriticalAlertFmt("Please set both memcard slots\nto: Wii Arcadeboard");
    return;
  }

  /*
    Mario Party F.K.C 2's server and client have the same title ID.
    They expect different version replies and the backup size is different.
  */

  IOS::HLE::Kernel ios;
  const IOS::ES::TMDReader tmd_mp8 = ios.GetESCore().FindInstalledTMD(0x00015000344D504ALL);
  if (tmd_mp8.IsValid())
  {
    if (memcmp(tmd_mp8.GetSha1().data(), MarioPartyFKC2Server_1302141211, 16) == 0)
    {
      g_game_type = MarioPartyFKC2Server;
    }
    else if (memcmp(tmd_mp8.GetSha1().data(), MarioPartyFKC2Client_1301080914, 16) == 0)
    {
      g_game_type = MarioPartyFKC2Client;
    }
    else if (memcmp(tmd_mp8.GetSha1().data(), MarioPartyFKC2Client_1302141156, 16) == 0)
    {
      g_game_type = MarioPartyFKC2Client;
    }
    else  // Fall back to client for unkown MP8 versions
    {
      g_game_type = MarioPartyFKC2Client;
    }
  }
  else
  {
    // Tatsunoko VS Capcom is using the title ID "RAV0"
    const IOS::ES::TMDReader tmd_tvsc = ios.GetESCore().FindInstalledTMD(0x0001500052564130LL);
    if (tmd_tvsc.IsValid())
    {
      if (memcmp(tmd_tvsc.GetSha1().data(), TatsunokoVSCapcom_0811051625, 16) == 0)
      {
        g_game_type = TatsunokoVSCapcom;
      }
    }
  }

  if (g_game_type == Unknown)
  {
    PanicAlertFmt("Failed to detected game!");
  }

  std::string backup_filename(File::GetUserPath(D_RVAUSER_IDX) + "backup_");

  switch (g_game_type)
  {
  default:
  case MarioPartyFKC2Server:
    m_backup_size = 0x1400;
    m_backup_check_off = 0x13F8;
    backup_filename += "MarioPartyFKC2Server.bin";
    break;
  case MarioPartyFKC2Client:
    m_backup_size = 0x1A40;
    m_backup_check_off = 0x1A28;
    backup_filename += "MarioPartyFKC2Client.bin";
    break;
  case TatsunokoVSCapcom:
    m_backup_size = 0x1A40;
    m_backup_check_off = 0x1A28;
    backup_filename += "TatsunokoVSCapcom.bin";
    break;
  }

  if (File::Exists(File::GetUserPath(D_RVAUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_RVAUSER_IDX));
  }

  if (File::IsFile(backup_filename))
  {
    m_backup = new File::IOFile(backup_filename, "rb+");
  }
  else
  {
    m_backup = new File::IOFile(backup_filename, "wb+");
  }

  // The RVA device will be opened twice
  if (!m_backup->IsGood())
  {
    // PanicAlertFmt("Failed to open rvabackup\nFile might be in use.");
    return;
  }

  // Setup new backup file
  if (m_backup->GetSize() == 0)
  {
    u8* data = new u8[m_backup_size];

    memset(data, 0, m_backup_size);

    strcpy((char*)data, "IDX");

    for (u32 i = 1; i < 5; ++i)
    {
      *(u64*)(data + 8) = i;
      *(u32*)(data + m_backup_check_off) =
          Common::swap32(Common::ComputeCRC32(data + 0x10, m_backup_check_off - 0x10));
      m_backup->WriteBytes(data, m_backup_size);
    }

    delete[] data;

    m_backup->Flush();
  }
}

CEXIRVA::~CEXIRVA()
{
  if (!m_backup)
  {
    m_backup->Close();
    delete m_backup;
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

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmWrite: {:08x} {}", data, size);

  if (RVAMemoryMap(data & 0x7F000000) == RVAMemoryMap::BACKUP_SET_OFFSET)
  {
    m_backup_offset = (data >> 6) & 0xFFFF;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: Backup Set Offset: {:08x}", m_backup_offset);
    return;
  }

  // IRQ handler
  if (data == 0x2C000700)
  {
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ(W)");
    m_address = RVAMemoryMap::JVS_IO_SI_0_1_CSR;
    return;
  }

  if (!g_exi_write_mode)
  {
    m_address = RVAMemoryMap((data & ~0x80000000) >> 6);

    if (data & 0x80000000)
    {
      g_exi_write_mode = 1;
      return;
    }
  }

  if (m_address == RVAMemoryMap::JVS_ID)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: Get ID");

    // HACK: MP FKC Client: Patch out error 9906/8001
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
    g_exi_write_mode = 0;
    return;

  case RVAMemoryMap::JVS_IO_SI_0_1_CSR:
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA-IRQ(W): {:02x}", data);
    g_exi_write_mode = 0;
    return;

  case RVAMemoryMap::JVS_Switches:
    m_jvs.Write(m_address, data);
    return;

  case RVAMemoryMap::JVS_IO_CSR:
  case RVAMemoryMap::JVS_IO_TXD:
  case RVAMemoryMap::JVS_IO_RXD:
  case RVAMemoryMap::JVS_IO_RX_CNT:
  case RVAMemoryMap::JVS_IO_TX_LEN:
  case RVAMemoryMap::JVS_IO_RX_LEN:
  case RVAMemoryMap::JVS_IO_DCSR:
    m_jvs.Write(m_address, data);
    return;

  case RVAMemoryMap::SI_0_CSR:
  case RVAMemoryMap::SI_0_TXD:
  case RVAMemoryMap::SI_0_RXD:
  case RVAMemoryMap::SI_0_RX_CNT:
  case RVAMemoryMap::SI_0_TX_RAT:
  case RVAMemoryMap::SI_0_RX_RAT:
    m_si0.Write(m_address, data);
    return;

  case RVAMemoryMap::SI_1_CSR:
  case RVAMemoryMap::SI_1_TXD:
  case RVAMemoryMap::SI_1_RXD:
  case RVAMemoryMap::SI_1_RX_CNT:
  case RVAMemoryMap::SI_1_TX_RAT:
  case RVAMemoryMap::SI_1_RX_RAT:
    m_si1.Write(m_address, data);
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

  // ID
  if (m_address == RVAMemoryMap::JVS_ID)
  {
    data = 0;
  }

  // Watchdog
  if (m_address == RVAMemoryMap::WATCH_DOG)
  {
    data = Common::swap32(m_watch_dog_timer);
  }

  // Switches
  if (m_address == RVAMemoryMap::JVS_Switches)
  {
    data = m_jvs.Read(m_address, size);
  }

  // JVS(IO)
  if (m_address >= RVAMemoryMap::JVS_IO_CSR && m_address <= RVAMemoryMap::JVS_IO_DCSR)
  {
    data = Common::swap32(m_jvs.Read(m_address, size));
  }

  // IRQ handler
  if (m_address == RVAMemoryMap::JVS_IO_SI_0_1_CSR)
  {
    data = (u32)g_irq_type;
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ Status:{:08x}", data);
    data = Common::swap32(data);
  }

  // SI(0)
  if (m_address >= RVAMemoryMap::SI_0_CSR && m_address <= RVAMemoryMap::SI_0_RX_RAT)
  {
    data = Common::swap32(m_si0.Read(m_address, size));
  }

  // SI(1)
  if (m_address >= RVAMemoryMap::SI_1_CSR && m_address <= RVAMemoryMap::SI_1_RX_RAT)
  {
    data = Common::swap32(m_si1.Read(m_address, size));
  }

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmRead {:08x} {:08x}  {}", (u32)m_address, data, size);

  return data;
}

void CEXIRVA::DMAWrite(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMAWrite: {:08x} {:08x} {:x}", address, m_backup_offset,
                size);

  if (m_backup)
  {
    m_backup->Seek(m_backup_offset, File::SeekOrigin::Begin);

    m_backup->WriteBytes(memory.GetPointer(address), size);

    m_backup->Flush();
  }
}

void CEXIRVA::DMARead(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMARead: {:08x} {:08x} {:x}", address, m_backup_offset,
                size);

  if (m_backup)
  {
    m_backup->Seek(m_backup_offset, File::SeekOrigin::Begin);

    m_backup->Flush();

    m_backup->ReadBytes(memory.GetPointer(address), size);
  }
}

bool CEXIRVA::IsInterruptSet()
{
  if (g_have_irq)
  {
    if (g_irq_delay-- == 0)
    {
      g_have_irq = false;
    }
    return 1;
  }
  return 0;
}

}  // namespace ExpansionInterface
