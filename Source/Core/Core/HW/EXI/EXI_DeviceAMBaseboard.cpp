// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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
#include "Core/HW/DVD/AMMediaboard.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/WiiRoot.h"
#include "DiscIO/Enums.h"

bool g_interrupt_set = false;
u32 g_irq_timer = 0;
u32 g_irq_status = 0;

static u16 CheckSum(u8* Data, u32 Length)
{
  u16 check = 0;

  for (u32 i = 0; i < Length; i++)
  {
    check += Data[i];
  }

  return check;
}

namespace ExpansionInterface
{

void GenerateInterrupt(int flag)
{
  auto& system = Core::System::GetInstance();

  g_interrupt_set = true;
  g_irq_timer = 0;
  g_irq_status = flag;

  system.GetExpansionInterface().UpdateInterrupts();
}

CEXIAMBaseboard::CEXIAMBaseboard(Core::System& system) : IEXIDevice(system), m_position(0)
{
  std::string backup_Filename(File::GetUserPath(D_TRIUSER_IDX) + "tribackup_" +
                              SConfig::GetInstance().GetGameID().c_str() + ".bin");

  if (File::Exists(backup_Filename))
  {
    m_backup = new File::IOFile(backup_Filename, "rb+");
  }
  else
  {
    m_backup = new File::IOFile(backup_Filename, "wb+");
  }

  // Some games share the same ID Client/Server
  if (!m_backup->IsGood())
  {
    PanicAlertFmt("Failed to open tribackup\nFile might be in use.");

    backup_Filename = File::GetUserPath(D_TRIUSER_IDX) + "tribackup_tmp_" +
                      SConfig::GetInstance().GetGameID().c_str() + ".bin";

    m_backup = new File::IOFile(backup_Filename, "wb+");
  }

  // Virtua Striker 4 and Gekitou Pro Yakyuu need a higher FIRM version
  // Which is read from the backup data?!
  if (AMMediaboard::GetGameType() == VirtuaStriker4 ||
      AMMediaboard::GetGameType() == GekitouProYakyuu)
  {
    if (m_backup->GetSize() != 0)
    {
      u8* data = new u8[m_backup->GetSize()];

      m_backup->ReadBytes(data, m_backup->GetSize());

      // Set FIRM version
      *(u16*)(data + 0x12) = 0x1703;
      *(u16*)(data + 0x212) = 0x1703;

      // Update checksum
      *(u16*)(data + 0x0A) = Common::swap16(CheckSum(data + 0xC, 0x1F4));
      *(u16*)(data + 0x20A) = Common::swap16(CheckSum(data + 0x20C, 0x1F4));

      m_backup->Seek(0, File::SeekOrigin::Begin);
      m_backup->WriteBytes(data, m_backup->GetSize());
      m_backup->Flush();

      delete[] data;
    }
  }
}
CEXIAMBaseboard::~CEXIAMBaseboard()
{
  m_backup->Close();
  delete m_backup;
}

void CEXIAMBaseboard::SetCS(int cs)
{
  DEBUG_LOG_FMT(SP1, "AM-BB ChipSelect={}", cs);
  if (cs)
    m_position = 0;
}

bool CEXIAMBaseboard::IsPresent() const
{
  return true;
}

bool CEXIAMBaseboard::IsInterruptSet()
{
  if (g_interrupt_set)
  {
    DEBUG_LOG_FMT(SP1, "AM-BB IRQ");
    if (++g_irq_timer > 12)
      g_interrupt_set = false;
    return 1;
  }
  else
  {
    return 0;
  }
}

void CEXIAMBaseboard::DMAWrite(u32 addr, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: Backup DMA Write: {:08x} {:x}", addr, size);

  m_backup->Seek(m_backoffset, File::SeekOrigin::Begin);

  m_backup->WriteBytes(memory.GetPointer(addr), size);

  m_backup->Flush();
}

void CEXIAMBaseboard::DMARead(u32 addr, u32 size)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

  NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: Backup DMA Read: {:08x} {:x}", addr, size);

  m_backup->Seek(m_backoffset, File::SeekOrigin::Begin);

  m_backup->Flush();

  m_backup->ReadBytes(memory.GetPointer(addr), size);
}

void CEXIAMBaseboard::TransferByte(u8& _byte)
{
  DEBUG_LOG_FMT(SP1, "AM-BB > {:02x}", _byte);
  if (m_position < 4)
  {
    m_command[m_position] = _byte;
    _byte = 0xFF;
  }

  if ((m_position >= 2) && (m_command[0] == 0 && m_command[1] == 0))
  {
    // Read serial ID
    _byte = "\x06\x04\x10\x00"[(m_position - 2) & 3];
  }
  else if (m_position == 3)
  {
    unsigned int checksum = (m_command[0] << 24) | (m_command[1] << 16) | (m_command[2] << 8);
    unsigned int bit = 0x80000000UL;
    unsigned int check = 0x8D800000UL;
    while (bit >= 0x100)
    {
      if (checksum & bit)
        checksum ^= check;
      check >>= 1;
      bit >>= 1;
    }

    if (m_command[3] != (checksum & 0xFF))
      DEBUG_LOG_FMT(SP1, "AM-BB cs: {:02x}, w: {:02x}", m_command[3], checksum & 0xFF);
  }
  else
  {
    if (m_position == 4)
    {
      switch (m_command[0])
      {
      case AMBB_OFFSET_SET:
        m_backoffset = (m_command[1] << 8) | m_command[2];
        DEBUG_LOG_FMT(SP1, "AM-BB COMMAND: Backup Offset:{:04x}", m_backoffset);
        m_backup->Seek(m_backoffset, File::SeekOrigin::Begin);
        _byte = 0x01;
        break;
      case AMBB_BACKUP_WRITE:
        DEBUG_LOG_FMT(SP1, "AM-BB COMMAND: Backup Write:{:04x}-{:02x}", m_backoffset, m_command[1]);
        m_backup->WriteBytes(&m_command[1], 1);
        m_backup->Flush();
        _byte = 0x01;
        break;
      case AMBB_BACKUP_READ:
        DEBUG_LOG_FMT(SP1, "AM-BB COMMAND: Backup Read :{:04x}", m_backoffset);
        _byte = 0x01;
        break;
      case AMBB_DMA_OFFSET_LENGTH_SET:
        m_backup_dma_off = (m_command[1] << 8) | m_command[2];
        m_backup_dma_len = m_command[3];
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: Backup DMA :{:04x} {:02x}", m_backup_dma_off,
                       m_backup_dma_len);
        _byte = 0x01;
        break;
      case AMBB_ISR_READ:
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: ISRRead  :{:02x} {:02x}:{:02x} {:02x}", m_command[1],
                       m_command[2], 4, g_irq_status);
        _byte = 0x04;
        break;
      case AMBB_ISR_WRITE:
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: ISRWrite :{:02x} {:02x}", m_command[1], m_command[2]);
        g_irq_status &= ~(m_command[2]);
        _byte = 0x04;
        break;
      // 2 byte out
      case AMBB_IMR_READ:
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: IMRRead  :{:02x} {:02x}", m_command[1], m_command[2]);
        _byte = 0x04;
        break;
      case AMBB_IMR_WRITE:
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: IMRWrite :{:02x} {:02x}", m_command[1], m_command[2]);
        _byte = 0x04;
        break;
      case AMBB_LANCNT_WRITE:
        NOTICE_LOG_FMT(SP1, "AM-BB COMMAND: LANCNTWrite :{:02x} {:02x}", m_command[1],
                       m_command[2]);
        if ((m_command[1] == 0) && (m_command[2] == 0))
        {
          g_interrupt_set = true;
          g_irq_timer = 0;
          g_irq_status = 0x02;
        }
        if ((m_command[1] == 2) && (m_command[2] == 1))
        {
          g_irq_status = 0;
        }
        _byte = 0x08;
        break;
      default:
        _byte = 4;
        ERROR_LOG_FMT(SP1, "AM-BB COMMAND: {:02x} {:02x} {:02x}", m_command[0], m_command[1],
                      m_command[2]);
        break;
      }
    }
    else if (m_position > 4)
    {
      switch (m_command[0])
      {
      // Read backup - 1 byte out
      case 0x03:
        m_backup->Flush();
        m_backup->ReadBytes(&_byte, 1);
        break;
      // DMA?
      case 0x05:
        _byte = 0x01;
        break;
      // 2 byte out
      case AMBB_ISR_READ:
        if (m_position == 6)
        {
          _byte = g_irq_status;
          g_interrupt_set = false;
        }
        else
        {
          _byte = 0x04;
        }
        break;
      // 2 byte out
      case AMBB_IMR_READ:
        if (m_position == 5)
          _byte = 0xFF;
        if (m_position == 6)
          _byte = 0x81;
        break;
      default:
        ERROR_LOG_FMT(SP1, "Unknown AM-BB command");
        break;
      }
    }
    else
    {
      _byte = 0xFF;
    }
  }
  DEBUG_LOG_FMT(SP1, "AM-BB < {:02x}", _byte);
  m_position++;
}

void CEXIAMBaseboard::DoState(PointerWrap& p)
{
  p.Do(m_position);
  p.Do(g_interrupt_set);
  p.Do(m_command);
}

}  // namespace ExpansionInterface
