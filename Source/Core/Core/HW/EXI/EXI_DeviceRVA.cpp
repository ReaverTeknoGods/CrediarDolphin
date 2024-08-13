// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceRVA.h"

#include <string>
#include <locale> 

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/IOFile.h"
#include "Common/Hash.h"

#include "Core/Core.h"
#include "Core/HLE/HLE.h" 
#include "Core/HW/EXI/EXI.h" 
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI_DeviceGCController.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCSymbolDB.h" 
#include "Core/System.h" 

namespace ExpansionInterface
{
  
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

static const u8 MatrixDisplayIDReply[] = { "\x2M202MD12BA\x3" };

static const u8 AMO_reply_se[] = { 3, '/', 'S', '\n', '/', 'S', 'V', ':', '\n', '/', 'E', '\n' };

static const u8 AMO_reply_version[] = { 3, '/', 'S', '\n',  'V', ':', 'M', 'P', '8', '2', 0, '\n', '/', 'E', '\n' };

static const u8 SI0_reply_version[] = { "\x02\x05\x00\x06MP8\x00\x01\x5F\x42" };

static const u8 SI0_reply_6[] = { "\x02\x01\x00\x06TEST12\xAC" }; 

bool g_have_irq;
u32  g_irq_delay;
u32  g_irq_type;

  CEXISI::CEXISI()
{
  m_csr    = 0;
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

u32 CEXISI::Read(RVAMemoryMap address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();   
  u32 data = 0;

  switch (address)
  {
    case RVAMemoryMap::SI_0_CSR:
      data = m_csr | 0x40;
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: CSR(R):{:08x}:{:02X}", size, data);
      break;
    case RVAMemoryMap::SI_0_TXD:
      data = m_tx_data;
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TXD(R):{:08x}:{:02X}", size, data);
      break;
    case RVAMemoryMap::SI_0_RXD:
      data = m_rx_data;

      if (m_rx_type == 1)
      {
        data = SI0_reply_version[m_rxoff++];
        if (m_rxoff > sizeof(SI0_reply_version))
        {
          m_rxoff = 0; 
          break;
        }
      }
      else if (m_rx_type == 2)
      {
        data = SI0_reply_6[m_rxoff++];
        if (m_rxoff > sizeof(SI0_reply_6))
        {
          m_rxoff = 0;
          break;
        }
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
      // Reply for the display ID request
      if (m_rx_type == 1)
      {
        data = MatrixDisplayIDReply[m_rxoff++];
        if (m_rxoff > sizeof(MatrixDisplayIDReply))
        {
          m_rxoff = 0;
        }
      }
      else if (m_rx_type == 2)
      {
        data = AMO_reply_se[m_rxoff++];
        if (m_rxoff > sizeof(AMO_reply_se))
        {
          m_rxoff = 0;
        }
      }
      else if (m_rx_type == 3)
      {
        data = AMO_reply_version[m_rxoff++];
        if (m_rxoff > sizeof(AMO_reply_version))
        {
          m_rxoff = 0;
        }
      }
      else
      {
        data = m_rx_data;
      }

      if (m_rx_cnt > 0)
          m_rx_cnt--;

      if (m_rx_cnt == 0)
      {
        if (memory.Read_U32(0x800FAE90) == 0x60000000)
        {
          memory.Write_U32(0x48000011, 0x800FAE90);
          PowerPC::ppcState.iCache.Invalidate(0x800FAE90); 
        }
      }

      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(R):{:08x}:{:02X}", size, data);
      break;
    case RVAMemoryMap::SI_1_TXD:
      data = m_status;
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TXD(R):{:08x}:{:02X}", size, data);
      m_status = 0;
    break;
    case RVAMemoryMap::SI_1_RX_RAT:
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(R):{:08x}", size );
      break;
    case RVAMemoryMap::SI_1_CSR:
      data = m_csr | 0x40;
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(R):{:08x}:{:02X}",size, data);
      break;
    case RVAMemoryMap::SI_1_RX_CNT:
      data = m_rx_cnt;
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(R):{:08x}:{:02X}", size, data ); 
      break;
    default:
      WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI: Unhandled Memory Address Read {:08x}:{:08x}", (u32)address, size);
      return 0x00;
  }

  return data;
}

void CEXISI::Write(RVAMemoryMap address, u32 value)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();   
 
  if ((u32)address == (value >> 6))
  {
    return;
  }

  u32 value_swap = Common::swap32(value);
  wchar_t mtext[128];
  
  switch (address)
  {
  case RVAMemoryMap::SI_0_CSR:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F; 
    break;
  case RVAMemoryMap::SI_1_CSR:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    break;
  case RVAMemoryMap::SI_0_TXD:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_sibuf[m_siboff++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_1_TXD:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TXD(W): {:02x}", value_swap);
    m_tx_data = value_swap;
    m_sibuf[m_siboff++] = value_swap;
    m_status = 0x800;
    break;
  case RVAMemoryMap::SI_0_RXD:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    break;
  case RVAMemoryMap::SI_1_RXD:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RXD(W): {:02x}", value_swap);
    m_rx_data = value_swap;
    break;
  case RVAMemoryMap::SI_0_RX_CNT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-CNT(W): {:02x}", value_swap);
    m_rx_cnt = value_swap;
    break;
  case RVAMemoryMap::SI_1_RX_CNT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-CNT(W): {:02x}", value_swap);
    m_rx_cnt = value_swap;
    break;
  case RVAMemoryMap::SI_0_TX_RAT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: TX-RAT(W): {:02x}", value_swap);
    m_tx_rat = value_swap;
    break;
  case RVAMemoryMap::SI_1_TX_RAT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: TX-RAT(W): {:02x}", value_swap);
    m_tx_rat = value_swap;
    break;
  case RVAMemoryMap::SI_0_RX_RAT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: RX-RAT(W): {:02x}", value_swap);
    m_rx_rat = value_swap;

    if (m_rx_rat == 0x19 && m_csr == 0x03)
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0:IRQ Request? {:02x}", m_irqc);
      m_irqc++;
    }

    if (m_irqc == 4)
    {
      g_have_irq = true;
      g_irq_type = RVA_IRQ_MASK & ~RVA_IRQ_S0_RX;
      g_irq_delay = 10;
      m_rx_cnt = sizeof(SI0_reply_6);
      m_rx_type = 2;
      m_rxoff = 0;
    }
    break;
  case RVAMemoryMap::SI_1_RX_RAT:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: RX-RAT(W): {:02x}", value_swap);
    m_rx_rat = value_swap;
    break;
  default:
    WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI(?): Unhandled address write: {:08x} {:08x}", (u32)address, value_swap);
    break;
  }

  if(m_siboff)
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
    if (m_sibuf[0] == 2 && m_sibuf[1] == 0x10 )
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
              m_siboff   = 1;
              return;
            }
            break;
          case MatrixDisplayCommand::DisplayID:
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Get Display ID");
            g_have_irq  = true;
            g_irq_type  = RVA_IRQ_MASK & ~RVA_IRQ_S1_RX;
            g_irq_delay = 10;
            m_rx_cnt    = sizeof(MatrixDisplayIDReply);
            m_rx_type   = 1;
            m_rxoff     = 0;
            /*
              BUG: The recv loop calles OSSuspendThread and then never returns
            */
            if (memory.Read_U32(0x800FAE90) == 0x48000011)
            {
              memory.Write_U32(0x60000000, 0x800FAE90);
              PowerPC::ppcState.iCache.Invalidate(0x800FAE90);
            }
            break;
          // No command just prints the text
          default:
            /*
              Remap text
            */
            memset(mtext, 0, sizeof(mtext)); 

            for (u32 i = 0; i < strlen((char*)(m_sibuf + 4))-1; ++i)
            {
              mtext[i] = character_display_code[m_sibuf[i + 4]];
            }
#ifdef _WIN32
            INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Text: {}", TStrToUTF8(mtext));
#endif
            //WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: MDC: Unhandled Command {:02x}", m_sibuf[4]);
            break;
          }

          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
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

          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
    }

    if (m_sibuf[0] == 2 && m_sibuf[1] == 4 )
    {
      if (m_siboff >= 5)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK4: {:02x}", m_sibuf[4]);

        g_have_irq  = true;
        g_irq_type  = RVA_IRQ_MASK & ~RVA_IRQ_S0_RX;
        g_irq_delay = 10;
        m_rx_cnt = sizeof(SI0_reply_version);
        m_rx_type   = 1;
        m_rxoff     = 0;

        m_tx_data = 0;
        m_siboff = 0;
        memset(m_sibuf, 0, sizeof(m_sibuf));
      }
    }

    // ?
    if (m_sibuf[0] == 2 && m_sibuf[1] == 6 )
    {
      // Size is in byte 4
      if (m_siboff >= 4)
      {
        // Check for full packet
        if (m_siboff == m_sibuf[3] + 5)
        {
          INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI0: UNK6: {:02x}", m_sibuf[2]);

          g_have_irq = true;
          g_irq_type = RVA_IRQ_MASK & ~RVA_IRQ_S0_RX;
          g_irq_delay = 10;
          m_rx_cnt = sizeof(SI0_reply_6);
          m_rx_type = 2;
          m_rxoff = 0;

          m_tx_data = 0;
          m_siboff = 0;
          memset(m_sibuf, 0, sizeof(m_sibuf));
        }
      }
    }

    // AMO board CMD:
    if (m_sibuf[0] == 0x2F && m_siboff == 7 )
    {
      // replace new lines for nicer print
      for (u32 i = 0; m_sibuf[i] != 3; ++i)
      {
        if (m_sibuf[i] == '\n')
            m_sibuf[i] = ' ';
      }

      m_sibuf[6] = 0;

      INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: AMO: {}", (char*)m_sibuf);

      switch (m_sibuf[1])
      {
      case 's':
        m_rx_type = 2;
        m_rx_cnt = sizeof(AMO_reply_se); 
        break;
      case 'v':
        m_rx_type = 3;
        m_rx_cnt = sizeof(AMO_reply_version); 
        break;
      //case 'i':
      //  m_rx_type = 4;
      //break;
      default:
        WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI-SI1: AMO: Unhandled Command {:02x}", m_sibuf[1]);
        break;
      }

      g_have_irq  = true;
      g_irq_type  = RVA_IRQ_MASK & ~RVA_IRQ_S1_RX;
      g_irq_delay = 10;
      m_rxoff     = 0;

      // Hack: Fixes OSSleepThread bug in Mario Party F.K.C 2
      if (memory.Read_U32(0x800FAE90) == 0x48000011)
      {
        memory.Write_U32(0x60000000, 0x800FAE90);
        PowerPC::ppcState.iCache.Invalidate(0x800FAE90);
      }

      m_tx_data = 0;
      m_siboff  = 0;
      memset(m_sibuf, 0, sizeof(m_sibuf));
    }
  }
}

  CEXIJVS::CEXIJVS()
{
  m_csr = 0;
  m_tx_data_cnt = 0;
  m_rx_data = 0;
  m_tx_len = 0;
  m_rx_len = 0;
  m_dscr = 0;

  jvsboff = 0;
  memset(jvsbuf, 0, sizeof(jvsbuf));
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

    if (pad_status.button & PAD_BUTTON_Y)
      switch_status &= ~0x10000000;
    else
      switch_status |= 0x10000000;

    if (pad_status.button & PAD_BUTTON_X)
      switch_status &= ~0x20000000;
    else
      switch_status |= 0x20000000;

    data = switch_status;
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    data = m_csr | 0x40;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: CSR(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_TX_CNT:
    data = m_tx_data_cnt;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: CNT/TXD(R): {:02x}", data);
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    data = m_dscr | 1;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: DCSR(R): {:02x}", data);
    break;
  default:
    WARN_LOG_FMT(EXPANSIONINTERFACE, "JVS: Unhandled address read: {:08x}", (u32)address);
    return 0;
  }

  return data;
}

void CEXIJVS::Write(RVAMemoryMap address, u32 value)
{
  u32 value_swap = Common::swap32(value);

  switch (address)
  {
  case RVAMemoryMap::JVS_Switches:
    break;
  case RVAMemoryMap::JVS_IO_CSR:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: CSR(W): {:02x}", value_swap);
    m_csr = value_swap & 0x3F;
    break;
  case RVAMemoryMap::JVS_IO_TXD:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: TX_CNT/TXD(W): {:02x}", value_swap);
    m_tx_data_cnt = value_swap;
    jvsbuf[jvsboff++] = value_swap;
    break;
  case RVAMemoryMap::JVS_IO_TX_LEN:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: TX_LEN(W): {:02x}", value_swap);
    m_tx_len = value_swap;
    break;
  case RVAMemoryMap::JVS_IO_RX_LEN:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: RX_LEN(W): {:02x}", value_swap);
    m_rx_len = value_swap;
    break;
  case RVAMemoryMap::JVS_IO_DCSR:
    INFO_LOG_FMT(EXPANSIONINTERFACE, "JVS: DCSR(W): {:02x}", value_swap);
    m_dscr = value_swap;
  break;
  default:
    WARN_LOG_FMT(EXPANSIONINTERFACE, "JVS: Unhandled address write: {:08x} {:02x}", (u32)address, value);
    break;
  }
}

CEXIJVS::~CEXIJVS() {}

CEXIRVA::CEXIRVA() : m_jvs(), m_si0(), m_si1()
{
  m_exi_write_mode = 0;
  m_watch_dog_timer = 0;
  m_backup_offset = 0;
  g_have_irq = false;
  g_irq_delay = 0;
  g_irq_type = 0;

  if (File::Exists(File::GetUserPath(D_RVAUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_RVAUSER_IDX));
  }

  std::string backup_filename(File::GetUserPath(D_RVAUSER_IDX) + "rvabackup_" +
                              SConfig::GetInstance().GetGameID().c_str() + ".bin");

  if (File::IsFile(backup_filename))
  {
    m_backup = new File::IOFile(backup_filename, "rb+");
  }
  else
  {
    m_backup = new File::IOFile(backup_filename, "wb+");
  }

  if (!m_backup->IsGood())
  {
    //PanicAlertFmt("Failed to open rvabackup\nFile might be in use.");
    return;
  }

  // Setup new backup file
  if (m_backup->GetSize() == 0)
  {
    u8 data[0x1400];

    memset(data, 0, sizeof(data));

    strcpy((char*)data, "IDX");

    for (u32 i = 1; i < 5; ++i)
    {
        *(u64*)(data + 8) = i;
        *(u32*)(data + 0x13F8) = Common::swap32(Common::ComputeCRC32(data + 0x10, 0x13E8));
        m_backup->WriteBytes(data, 0x1400);
    }

    m_backup->Flush();
  }
}

CEXIRVA::~CEXIRVA()
{
  m_backup->Close();
  delete m_backup;
}

bool CEXIRVA::IsPresent() const
{
  return true;
}

void CEXIRVA::ImmWrite(u32 data, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmWrite: {:08x} {}", data, size);

  // IRQ handler
  if (data == 0x2C000700)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ(W)");
    m_address = RVAMemoryMap::JVS_IO_SI_0_1_CSR;
    return;
  }

  if (!m_exi_write_mode)
  {
    m_address = RVAMemoryMap((data & ~0x80000000) >> 6);

    if (data & 0x80000000)
    {
      m_exi_write_mode = 1;
      return;
    }
  }
  else
  {
    m_exi_write_mode = 0;

    if (m_address == RVAMemoryMap::JVS_IO_TXD)
    {
      if (data != 0x2C000100)
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
      return;
      }

      if (data != 0x24000000)
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
      }
    }

    if (m_address == RVAMemoryMap::SI_0_TXD )
    {
      if (data != 0x30000000)
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
        return;
      }

      if (data != 0x24000000)
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
      }
    }

    if (m_address == RVAMemoryMap::SI_1_TXD)
    {
      if (data != 0x34000000 )
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
        return;
      }

      if (data != 0x24000000)
      {
        m_exi_write_mode = 1;
      }
      else
      {
        m_address = RVAMemoryMap((data & ~0x80000000) >> 6);
      }
    }      
  }

  if (m_address == RVAMemoryMap::JVS_ID)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-JVS: Get ID");
    return;
  }

  if (m_address == RVAMemoryMap::WATCH_DOG)
  {
    m_watch_dog_timer = Common::swap32(data);
    DEBUG_LOG_FMT(EXPANSIONINTERFACE, "RVA: Watchdog Timer: {:02x}", m_watch_dog_timer );
    return;
  }

  if (m_address == RVAMemoryMap::JVS_Switches )
  {
    m_jvs.Write(m_address, data);
    return;
  }

  if (m_address == RVAMemoryMap::JVS_IO_SI_0_1_CSR)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA-IRQ(W): {:02x}", data );
    return;
  }

  if (m_address >= RVAMemoryMap::JVS_IO_CSR && m_address <= RVAMemoryMap::JVS_IO_DCSR)
  {
    m_jvs.Write(m_address, data);
    return;
  }

  if (m_address >= RVAMemoryMap::SI_0_CSR && m_address <= RVAMemoryMap::SI_0_RX_RAT)
  {
    m_si0.Write(m_address, data);
    return;
  }

  if (m_address >= RVAMemoryMap::SI_1_CSR && m_address <= RVAMemoryMap::SI_1_RX_RAT)
  { 
    m_si1.Write(m_address, data);
    return;
  }

  if (RVAMemoryMap(data & 0x7F000000) == RVAMemoryMap::BACKUP_SET_OFFSET)
  {
    m_backup_offset = (data >> 6) & 0xFFFF;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: Backup Set Offset: {:08x}", m_backup_offset);
    return;
  }

  WARN_LOG_FMT(EXPANSIONINTERFACE, "RVA: Unhandled Address: {:08x} ({:08x}) {}", data,  (u32)m_address, size);
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
    INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: IRQ Status:{:08x}", data);
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
  
  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: ImmRead {:08x} {:08x}  {}", (u32)m_address, data, size);

  return data;
}

void CEXIRVA::DMAWrite(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMAWrite: {:08x} {:08x} {:x}", address, m_backup_offset, size);

  m_backup->Seek(m_backup_offset, File::SeekOrigin::Begin);

  m_backup->WriteBytes(memory.GetPointer(address), size);

  m_backup->Flush();
}

void CEXIRVA::DMARead(u32 address, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  INFO_LOG_FMT(EXPANSIONINTERFACE, "RVA: DMARead: {:08x} {:08x} {:x}", address, m_backup_offset, size);

  m_backup->Seek(m_backup_offset, File::SeekOrigin::Begin);

  m_backup->Flush();

  m_backup->ReadBytes(memory.GetPointer(address), size);
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
