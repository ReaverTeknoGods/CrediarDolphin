#pragma once


#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h" 

class PointerWrap;

namespace Core
{
class System;
}

namespace File
{
class IOFile;
}

namespace DVDInterface
{
class DVDInterfaceState;
struct Data;
}  // namespace DVDInterface

enum GameType
{
  FZeroAX = 1,
  MarioKartGP,
  MarioKartGP2,
  VirtuaStriker3,
  VirtuaStriker4,
  GekitouProYakyuu,
  KeyOfAvalon
};
enum MediaType
{
  GDROM = 1,
  NAND, 
};

namespace AMBaseboard
{
  void  Init(void);
  void  FirmwareMap(bool on);
  u8*   InitDIMM(void);
  void  InitKeys(u32 KeyA, u32 KeyB, u32 KeyC);
  u32   ExecuteCommand( u32 *DICMDBUF, u32 Address, u32 Length );
	u32		GetGameType( void );
  u32   GetMediaType( void );
	void	Shutdown( void );
};

