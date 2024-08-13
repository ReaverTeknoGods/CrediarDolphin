// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Common/IOFile.h"

namespace Core
{
class System;
}

namespace ExpansionInterface
{

/*
  All commands are left shifted by 6 
*/
  enum class RVAMemoryMap : u32
  {
    JVS_ID = 0x00000000, 

    WATCH_DOG = 0x00800000,     // 0x20000000

    JVS_Switches = 0x00900000,  // 0x24000000

    BACKUP_SET_OFFSET = 0x28000000,

    // JVS I/O
    JVS_IO_CSR = 0x00B00000,    // 0x2C000000
    JVS_IO_TXD = 0x00B00004,    // 0x2C000100
    JVS_IO_TX_CNT = 0x00B00004, // 0x2C000100
    JVS_IO_RXD = 0x00B00008,    // 0x2C000200
    JVS_IO_RX_CNT = 0x00B0000C, // 0x2C000300
    JVS_IO_TX_LEN = 0x00B00010, // 0x2C000400
    JVS_IO_RX_LEN = 0x00B00014, // 0x2C000500
    JVS_IO_DCSR = 0x00B00018,   // 0x2C000600

    // IRQ Flags
    JVS_IO_SI_0_1_CSR = 0x00B0001C, // 0x2C000700

    // UART0
    SI_0_CSR = 0x00C00000,    // 0x30000000
    SI_0_TXD = 0x00C00004,    // 0x30000100
    SI_0_TX_CNT = 0x00C00004, // 0x30000100
    SI_0_RXD = 0x00C00008,    // 0x30000200
    SI_0_RX_CNT = 0x00C0000C, // 0x30000300
    SI_0_TX_RAT = 0x00C00010, // 0x30000400
    SI_0_RX_RAT = 0x00C00014, // 0x30000500

    // UART1
    SI_1_CSR = 0x00D00000,    // 0x34000000
    SI_1_TXD = 0x00D00004,    // 0x34000100
    SI_1_TX_CNT = 0x00D00004, // 0x34000100
    SI_1_RXD = 0x00D00008,    // 0x34000200
    SI_1_RX_CNT = 0x00D0000C, // 0x34000300
    SI_1_TX_RAT = 0x00D00010, // 0x34000400
    SI_1_RX_RAT = 0x00D00014, // 0x34000500
  };

  enum RVAIRQMasks : u32
  {
    RVA_IRQ_JVS_RS_RX = 0x80,  // 0
    RVA_IRQ_JVS_RS_TX = 0x40,  // 3

    RVA_IRQ_S0_RX = 0x20,  // 1
    RVA_IRQ_S0_TX = 0x10,  // 4

    RVA_IRQ_S1_RX = 0x08,  // 2
    RVA_IRQ_S1_TX = 0x04,  // 5

    RVA_IRQ_MASK = 0xFC,
  };

  enum class MatrixDisplayCommand
  {
    // 0 - 3 unused
    Dimming   = 0x04,
    // 5 - 7 unused
    BackSpace = 0x08,
    HTab      = 0x09,
    Blinking = 0x0A,
    Scroll = 0x0B,
    Calendar= 0x0C,
    Clear = 0x0D,
    // 0x0E unused
    AllDisplay = 0x0F,
    DisplayPos = 0x10,
    // 0x11 - 0x16 unused
    CursorMode = 0x17,
    TriangleMarkOn = 0x18,
    TriangleMarkOff = 0x19,
    TriangleMarkAllOff = 0x1A,
    DisplayID = 0x1B,  // followed by 0x5B 0x63
    TriangleMarkBlink = 0x1E,
    Reset     = 0x1F,
  };


  class CEXIJVS
  {
  public:
    explicit CEXIJVS();
    virtual ~CEXIJVS();

    u32 Read(RVAMemoryMap address, u32 length);
    void Write(RVAMemoryMap address, u32 value);


  private:
    u32 m_csr;
    u32 m_tx_data_cnt;
    u32 m_rx_data;
    u32 m_rx_cnt;
    u32 m_tx_len;
    u32 m_rx_len;
    u32 m_dscr;

    u8 jvsbuf[255];
    u8 jvsboff;
  };

  class CEXISI
  {
  public:
    explicit CEXISI();
    virtual ~CEXISI();

    u32 Read(RVAMemoryMap address, u32 length);
    void Write(RVAMemoryMap address, u32 value );


  private:
    u32 m_csr;
    u32 m_tx_data;
    u32 m_rx_data;
    u32 m_rx_cnt;
    u32 m_tx_rat;
    u32 m_rx_rat;
    u32 m_status;

    RVAMemoryMap m_address;

    u8 m_sibuf[255];
    u8 m_siboff;

    u8 m_rxoff;
    u32 m_rx_type;

    u32 m_irqc;
  };

  
  class CEXIRVA : public IEXIDevice
  {
  public:
    explicit CEXIRVA(Core::System& system);
    virtual ~CEXIRVA();

    bool IsPresent() const override;
    void DMAWrite(u32 address, u32 size) override;
    void DMARead(u32 address, u32 size) override;
    bool IsInterruptSet() override;

  private:
    void ImmWrite(u32 data, u32 size) override;
    u32 ImmRead(u32 size) override;

    RVAMemoryMap m_address;

    u8 m_watch_dog_timer;
    u32 m_exi_write_mode;

    u16 m_backup_offset;
    File::IOFile* m_backup;

    CEXIJVS m_jvs;
    CEXISI  m_si0;
    CEXISI  m_si1;
  };

}  // namespace ExpansionInterface
