
#pragma once

#include <SFML/Network.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "Common/CommonTypes.h"
#include "Common/Flag.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Common/IOFile.h"

namespace Core
{
class System;
}


namespace ExpansionInterface
{
void GenerateInterrupt(int flag);

class CEXIAMBaseboard : public IEXIDevice
{
public:
  explicit CEXIAMBaseboard(Core::System& system);
  virtual ~CEXIAMBaseboard();

  void SetCS(int _iCS) override;
  bool IsInterruptSet() override;
  bool IsPresent() const override;
  void DoState(PointerWrap& p) override;
  void DMAWrite(u32 addr, u32 size) override;
  void DMARead(u32 addr, u32 size) override;


private:

enum
{
    AMBB_OFFSET_SET   = 0x01,
    AMBB_BACKUP_WRITE = 0x02,
    AMBB_BACKUP_READ = 0x03,

    AMBB_DMA_OFFSET_LENGTH_SET = 0x05,

    AMBB_ISR_READ = 0x82,
    AMBB_ISR_WRITE = 0x83,     
    AMBB_IMR_READ = 0x86, 
    AMBB_IMR_WRITE = 0x87, 
    AMBB_LANCNT_WRITE = 0xFF, 
};

	int m_position;
  u32 m_backup_dma_off;
  u32 m_backup_dma_len;
	unsigned char m_command[4];
	unsigned short m_backoffset;
  File::IOFile* m_backup;

protected:
  void TransferByte(u8& _uByte) override;
};
}  // namespace ExpansionInterface
