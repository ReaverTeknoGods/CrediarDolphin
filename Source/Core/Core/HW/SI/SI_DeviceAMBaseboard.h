
#pragma once

#include <SFML/Network.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "Common/CommonTypes.h"
#include "Common/Flag.h"
#include "Core/HW/SI/SI_Device.h"

namespace SerialInterface
{
// triforce (GC-AM) baseboard
class CSIDevice_AMBaseboard : public ISIDevice
{

  
typedef struct
  {
    unsigned short button;       // Or-ed PAD_BUTTON_* and PAD_TRIGGER_* bits
    unsigned char stickX;        // 0 <= stickX       <= 255
    unsigned char stickY;        // 0 <= stickY       <= 255
    unsigned char substickX;     // 0 <= substickX    <= 255
    unsigned char substickY;     // 0 <= substickY    <= 255
    unsigned char triggerLeft;   // 0 <= triggerLeft  <= 255
    unsigned char triggerRight;  // 0 <= triggerRight <= 255
    unsigned char analogA;       // 0 <= analogA      <= 255
    unsigned char analogB;       // 0 <= analogB      <= 255
    signed char err;             // one of PAD_ERR_* number
  } SPADStatus;

private:
  enum EBufferCommands
  {
    CMD_RESET = 0x00,
    CMD_GCAM = 0x70,
  };
  enum CARDCommands
  {
    CARD_INIT = 0x10,
    CARD_GET_CARD_STATE = 0x20,
    CARD_READ = 0x33,
    CARD_IS_PRESENT = 0x40,
    CARD_WRITE = 0x53,
    CARD_SET_PRINT_PARAM = 0x78,
    CARD_REGISTER_FONT = 0x7A,
    CARD_WRITE_INFO = 0x7C,
    CARD_ERASE = 0x7D,
    CARD_EJECT = 0x80,
    CARD_CLEAN_CARD = 0xA0,
    CARD_LOAD_CARD = 0xB0,
    CARD_SET_SHUTTER = 0xD0,
  };

  unsigned short m_coin[2];
  int m_coin_pressed[2];

  u8 m_card_memory[0xD0];
  u8 m_card_read_packet[0xDB];
  u8 m_card_buffer[0x100];
  u32 m_card_memory_size;
  u32 m_card_is_inserted;
  u32 m_card_command;
  u32 m_card_clean;
  u32 m_card_write_length;
  u32 m_card_wrote;
  u32 m_card_read_length;
  u32 m_card_read;
  u32 m_card_bit;
  u32 m_card_state_call_count;
  u8  m_card_offset;

  u32 m_wheelinit;

  u32 m_motorinit;
  u8 m_motorreply[64];
  s16 m_motorforce_x;

public:
  // constructor
  CSIDevice_AMBaseboard(SIDevices device, int _iDeviceNumber);

  // run the SI Buffer
  int RunBuffer(u8* _pBuffer, int request_length) override;

  // return true on new data
  bool GetData(u32& _Hi, u32& _Low) override;

  // send a command directly
  void SendCommand(u32 _Cmd, u8 _Poll) override;
};

}  // namespace SerialInterface
