// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "Common/CommonTypes.h"
#include "Common/IOFile.h"
#include "Core/HW/EXI/EXI_Device.h"

namespace Core
{
class System;
}

namespace ExpansionInterface
{

enum
{
  EXI_DEVTYPE_RVA = 0xFF800000
};


 // All commands are left shifted by 6

enum class RVAMemoryMap : u32
{
  JVS_ID = 0x00000000,

  WATCH_DOG = 0x00800000,  // 0x20000000

  JVS_SWITCHES = 0x00900000,  // 0x24000000

  SRAM_SET_OFFSET = 0x28000000,

  // JVS I/O
  JVS_IO_CSR = 0x00B00000,     // 0x2C000000
  JVS_IO_TXD = 0x00B00004,     // 0x2C000100
  JVS_IO_TX_CNT = 0x00B00004,  // 0x2C000100
  JVS_IO_RXD = 0x00B00008,     // 0x2C000200
  JVS_IO_RX_CNT = 0x00B0000C,  // 0x2C000300
  JVS_IO_TX_LEN = 0x00B00010,  // 0x2C000400
  JVS_IO_RX_LEN = 0x00B00014,  // 0x2C000500
  JVS_IO_DCSR = 0x00B00018,    // 0x2C000600

  // IRQ Status
  JVS_IO_SI_0_1_CSR = 0x00B0001C,  // 0x2C000700

  // UART0
  SI_0_CSR = 0x00C00000,     // 0x30000000
  SI_0_TXD = 0x00C00004,     // 0x30000100
  SI_0_TX_CNT = 0x00C00004,  // 0x30000100
  SI_0_RXD = 0x00C00008,     // 0x30000200
  SI_0_RX_CNT = 0x00C0000C,  // 0x30000300
  SI_0_TX_RAT = 0x00C00010,  // 0x30000400
  SI_0_RX_RAT = 0x00C00014,  // 0x30000500

  // UART1
  SI_1_CSR = 0x00D00000,     // 0x34000000
  SI_1_TXD = 0x00D00004,     // 0x34000100
  SI_1_TX_CNT = 0x00D00004,  // 0x34000100
  SI_1_RXD = 0x00D00008,     // 0x34000200
  SI_1_RX_CNT = 0x00D0000C,  // 0x34000300
  SI_1_TX_RAT = 0x00D00010,  // 0x34000400
  SI_1_RX_RAT = 0x00D00014,  // 0x34000500
};

enum JVSIOCSR : u32
{
  JVS_IO_CSR_RX_INT = 0x80,
  JVS_IO_CSR_TX_INT = 0x40,

  JVS_IO_CSR_RX_EN_INT = 0x20,
  JVS_IO_CSR_TX_EN_INT = 0x10,

  JVS_IO_CSR_RESET = 0x01,

  JVS_IO_CSR_MASK = 0x3F,
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
  Dimming = 0x04,
  // 5 - 7 unused
  BackSpace = 0x08,
  HTab = 0x09,
  Blinking = 0x0A,
  Scroll = 0x0B,
  Calendar = 0x0C,
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
  Reset = 0x1F,
};

enum JVSIOCommands
{
  IOID = 0x10,
  CommandRevision = 0x11,
  JVSRevision = 0x12,
  CommunicationVersion = 0x13,
  CheckFunctionality = 0x14,
  MainID = 0x15,

  SwitchesInput = 0x20,
  CoinInput = 0x21,
  AnalogInput = 0x22,
  RotaryInput = 0x23,
  KeyCodeInput = 0x24,
  PositionInput = 0x25,
  GeneralSwitchInput = 0x26,

  PayoutRemain = 0x2E,
  Retrans = 0x2F,
  CoinSubOutput = 0x30,
  PayoutAddOutput = 0x31,
  GeneralDriverOutput = 0x32,
  AnalogOutput = 0x33,
  CharacterOutput = 0x34,
  CoinAddOutput = 0x35,
  PayoutSubOutput = 0x36,
  GeneralDriverOutput2 = 0x37,
  GeneralDriverOutput3 = 0x38,

  NAMCOCommand = 0x70,

  Reset = 0xF0,
  SetAddress = 0xF1,
  ChangeComm = 0xF2,
};

enum class MarioPartySerialCommand
{
  Version = 0x5256,
  Reset = 0x5452,
  Status = 0x5453,
  Update = 0x5055,
  Config = 0x4643,
  Error = 0x5245,
  SS = 0x5353,
  TR = 0x5452,

  MI = 0x494D,
  MO = 0x4F4D,
  PM = 0x4D50,
  BI = 0x4942,
  BO = 0x4F42,
  OT = 0x544f,
  CI = 0x4943,
  WM = 0x4D57,
  BJ = 0x504A,
  CU = 0x5543,
  SU = 0x5553,
  HP = 0x5048,
  PU = 0x5550,
  MT = 0x544d,
  LR = 0x4c52,

};

enum class SerialReplies
{
  SerialVersion,
  Serial_6,

  MDCDisplayID,

  AMOServices,
  AMOVersionServer,
  AMOVersionClient,
  AMOInfo,

  MPSIVersion,  // reply
  MPSIStatus,   // reply
  MPSIUpdate,   // reply
  MPSIConfig,   // reply
  MPSIError,    // reply
  SS,           // reply
  TR,           // no reply

  MI,  // no reply
  MO,  // no reply
  PM,  // no reply
  BI,  // no reply
  BO,  // no reply
  OT,  // no reply
  CI,  // no reply
  WM,  // no reply
  BJ,  // no reply
  CU,  // no reply
  SU,  // no reply
  HP,  // no reply
  PU,  // no reply
  MT,  // no reply
  LR,  // no reply
};

enum class GameType
{
  Unknown,
  MarioPartyFKC2Server = 1,
  MarioPartyFKC2Client,
  TatsunokoVSCapcom,
};

extern bool g_coin_pressed_prev_wii;

// "JAMMA Video Standard" I/O
class JVSIOMessage
{
public:
  u32 m_ptr, m_last_start, m_csum;
  u8 m_msg[0x80];

  JVSIOMessage();
  void start(int node);
  void addData(const u8* dst, size_t len, int sync);
  void addData(const void* data, size_t len);
  void addData(const char* data);
  void addData(u32 n);
  void end();
};  // end class JVSIOMessage

class CEXIJVS
{
public:
  explicit CEXIJVS();
  virtual ~CEXIJVS();

  u32 Read(RVAMemoryMap address, u32 size);
  void Write(RVAMemoryMap address, u32 value);

private:
  u32 m_csr;
  u32 m_tx_data;
  u32 m_tx_count;
  u32 m_rx_data;
  u32 m_rx_count;
  u32 m_tx_len;
  u32 m_rx_len;
  u32 m_dscr;

  u16 m_coin[2];
  u32 m_coin_pressed[2];

  u8 m_JVS_data[255];
  u8 m_JVS_offset;

  u8 m_JVS_reply_data[255];
  u8 m_JVS_reply_offset;
};

class CEXISI
{
public:
  explicit CEXISI();
  virtual ~CEXISI();

  void SetReply(SerialReplies reply, u32 size);
  u32 Read(RVAMemoryMap address, u32 size);
  void Write(RVAMemoryMap address, u32 value);

private:
  u32 m_csr;
  u32 m_tx_data;
  u32 m_tx_count;
  u32 m_rx_data;
  u32 m_rx_count;
  u32 m_tx_rate;
  u32 m_rx_rate;
  u32 m_status;

  RVAMemoryMap m_address;

  u8 m_SI_buf[255];
  u8 m_SI_offset;

  u8 m_rx_offset;
  SerialReplies m_rx_type;

  u32 m_IRQc;
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

  u16 m_SRAM_offset;
  File::IOFile* m_SRAM;
  u32 m_SRAM_size;
  u32 m_SRAM_check_off;

  CEXIJVS m_JVS;
  CEXISI m_SI0;
  CEXISI m_SI1;
};

}  // namespace ExpansionInterface
