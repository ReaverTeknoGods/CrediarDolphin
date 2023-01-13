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
 
namespace AMBaseboard
{
	void	Init( void );
  u32   ExecuteCommand(u32 Command, u32 Length, u32 Address, u32 Offset, u32 reply_type);
	u32		GetControllerType( void );
	void	Shutdown( void );
};

