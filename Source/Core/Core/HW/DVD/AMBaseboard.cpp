// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma warning(disable : 4189)

#include "Core/BootManager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/IOFile.h"
#include "DiscIO/DirectoryBlob.h"

#include "Core/Boot/Boot.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigLoaders/NetPlayConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/Sram.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/DVD/DVDThread.h"
#include "Core/HW/DVD/AMBaseboard.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/WiiRoot.h"
#include "Core/HLE/HLE.h"
#include "Core/PowerPC/PPCSymbolDB.h" 

#include "DiscIO/Enums.h"
#include "DiscIO/VolumeDisc.h"

#ifdef WIN32
#include <winsock2.h> 
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

unsigned char JPEG[2712] =
{
    0x48, 0x54, 0x54, 0x50, 0x2F, 0x31, 0x2E, 0x31, 0x20, 0x32, 0x30, 0x30, 0x20, 0x4F, 0x4B, 0x0D, 0x0A, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3A, 0x20, 0x6E, 0x67, 0x69, 0x6E, 0x78, 0x0D, 0x0A, 
    0x44, 0x61, 0x74, 0x65, 0x3A, 0x20, 0x46, 0x72, 0x69, 0x2C, 0x20, 0x32, 0x33, 0x20, 0x4A, 0x75, 0x6C, 0x20, 0x32, 0x30, 0x32, 0x31, 0x20, 0x32, 0x31, 0x3A, 0x33, 0x32, 0x3A, 0x32, 0x30, 0x20, 
    0x47, 0x4D, 0x54, 0x0D, 0x0A, 0x43, 0x6F, 0x6E, 0x74, 0x65, 0x6E, 0x74, 0x2D, 0x54, 0x79, 0x70, 0x65, 0x3A, 0x20, 0x69, 0x6D, 0x61, 0x67, 0x65, 0x2F, 0x6A, 0x70, 0x65, 0x67, 0x0D, 0x0A, 0x43, 
    0x6F, 0x6E, 0x74, 0x65, 0x6E, 0x74, 0x2D, 0x4C, 0x65, 0x6E, 0x67, 0x74, 0x68, 0x3A, 0x20, 0x32, 0x33, 0x35, 0x31, 0x0D, 0x0A, 0x43, 0x6F, 0x6E, 0x6E, 0x65, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x3A, 
    0x20, 0x6B, 0x65, 0x65, 0x70, 0x2D, 0x61, 0x6C, 0x69, 0x76, 0x65, 0x0D, 0x0A, 0x58, 0x2D, 0x46, 0x72, 0x61, 0x6D, 0x65, 0x2D, 0x4F, 0x70, 0x74, 0x69, 0x6F, 0x6E, 0x73, 0x3A, 0x20, 0x53, 0x41, 
    0x4D, 0x45, 0x4F, 0x52, 0x49, 0x47, 0x49, 0x4E, 0x0D, 0x0A, 0x58, 0x2D, 0x43, 0x6F, 0x6E, 0x74, 0x65, 0x6E, 0x74, 0x2D, 0x54, 0x79, 0x70, 0x65, 0x2D, 0x4F, 0x70, 0x74, 0x69, 0x6F, 0x6E, 0x73, 
    0x3A, 0x20, 0x6E, 0x6F, 0x73, 0x6E, 0x69, 0x66, 0x66, 0x0D, 0x0A, 0x4C, 0x61, 0x73, 0x74, 0x2D, 0x4D, 0x6F, 0x64, 0x69, 0x66, 0x69, 0x65, 0x64, 0x3A, 0x20, 0x46, 0x72, 0x69, 0x2C, 0x20, 0x32, 
    0x33, 0x20, 0x4A, 0x75, 0x6C, 0x20, 0x32, 0x30, 0x32, 0x31, 0x20, 0x32, 0x31, 0x3A, 0x33, 0x31, 0x3A, 0x32, 0x35, 0x20, 0x47, 0x4D, 0x54, 0x0D, 0x0A, 0x45, 0x78, 0x70, 0x69, 0x72, 0x65, 0x73, 
    0x3A, 0x20, 0x54, 0x75, 0x65, 0x2C, 0x20, 0x32, 0x31, 0x20, 0x53, 0x65, 0x70, 0x20, 0x32, 0x30, 0x32, 0x31, 0x20, 0x32, 0x31, 0x3A, 0x33, 0x32, 0x3A, 0x32, 0x30, 0x20, 0x47, 0x4D, 0x54, 0x0D, 
    0x0A, 0x43, 0x61, 0x63, 0x68, 0x65, 0x2D, 0x43, 0x6F, 0x6E, 0x74, 0x72, 0x6F, 0x6C, 0x3A, 0x20, 0x6D, 0x61, 0x78, 0x2D, 0x61, 0x67, 0x65, 0x3D, 0x35, 0x31, 0x38, 0x34, 0x30, 0x30, 0x30, 0x0D, 
    0x0A, 0x50, 0x72, 0x61, 0x67, 0x6D, 0x61, 0x3A, 0x20, 0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x0D, 0x0A, 0x41, 0x63, 0x63, 0x65, 0x70, 0x74, 0x2D, 0x52, 0x61, 0x6E, 0x67, 0x65, 0x73, 0x3A, 0x20, 
    0x62, 0x79, 0x74, 0x65, 0x73, 0x0D, 0x0A, 0x0D, 0x0A, 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0xFF, 0xDB, 0x00, 
    0x43, 0x00, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x06, 0x04, 0x04, 0x03, 0x05, 0x07, 0x06, 0x07, 0x07, 
    0x07, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0B, 0x09, 0x08, 0x08, 0x0A, 0x08, 0x07, 0x07, 0x0A, 0x0D, 0x0A, 0x0A, 0x0B, 0x0C, 0x0C, 0x0C, 0x0C, 0x07, 0x09, 0x0E, 0x0F, 0x0D, 0x0C, 0x0E, 0x0B, 0x0C, 
    0x0C, 0x0C, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x06, 0x03, 0x03, 0x06, 0x0C, 0x08, 0x07, 0x08, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 
    0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 
    0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x54, 0x00, 0x55, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 
    0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 
    0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 
    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0xFD, 0xFC, 0xA2, 0x8A, 0x28, 0x00, 0xA2, 0xB9, 
    0x3F, 0x88, 0x7F, 0x1C, 0xFC, 0x23, 0xF0, 0x9F, 0xC4, 0x9E, 0x19, 0xD1, 0xFC, 0x47, 0xAF, 0x58, 0x69, 0x1A, 0x9F, 0x8C, 0xAF, 0x7F, 0xB3, 0xB4, 0x5B, 0x69, 0xD8, 0x87, 0xD4, 0x2E, 0x3E, 0x51, 
    0xB1, 0x30, 0x3A, 0xE5, 0xD0, 0x64, 0xE0, 0x65, 0xD4, 0x67, 0x2C, 0x01, 0xEB, 0x2A, 0x54, 0xE2, 0xDB, 0x49, 0xEA, 0x8D, 0xEA, 0x61, 0xAB, 0x53, 0xA7, 0x0A, 0xB5, 0x20, 0xD4, 0x67, 0x77, 0x16, 
    0xD3, 0x4A, 0x49, 0x3B, 0x3B, 0x3E, 0xB6, 0x69, 0xA7, 0x6E, 0xBA, 0x05, 0x14, 0x51, 0x54, 0x60, 0x14, 0x51, 0x45, 0x00, 0x14, 0x51, 0x45, 0x00, 0x14, 0x51, 0x45, 0x00, 0x14, 0x51, 0x45, 0x00, 
    0x7E, 0x7E, 0x7F, 0xC1, 0x65, 0x7C, 0x47, 0x63, 0xE0, 0xDF, 0xDA, 0xC7, 0xF6, 0x47, 0xD6, 0x35, 0x4B, 0xA8, 0xAC, 0x74, 0xBD, 0x27, 0xC5, 0xD3, 0x5E, 0x5E, 0x5D, 0x4C, 0x76, 0xC7, 0x6F, 0x0C, 
    0x77, 0x9A, 0x4B, 0xBB, 0xB1, 0xEC, 0x15, 0x54, 0x93, 0xEC, 0x0D, 0x7D, 0xFF, 0x00, 0x69, 0x77, 0x15, 0xFD, 0xAC, 0x73, 0xC1, 0x24, 0x73, 0x43, 0x32, 0x09, 0x23, 0x92, 0x36, 0x0C, 0xB2, 0x29, 
    0x19, 0x04, 0x11, 0xC1, 0x04, 0x77, 0xAE, 0x1B, 0xF6, 0x83, 0xFD, 0x98, 0x7C, 0x07, 0xFB, 0x55, 0x78, 0x42, 0x1D, 0x0B, 0xC7, 0xFE, 0x1C, 0xB5, 0xF1, 0x0E, 0x9B, 0x6D, 0x38, 0xB9, 0x81, 0x24, 
    0x96, 0x58, 0x24, 0x82, 0x41, 0xC6, 0xE4, 0x96, 0x26, 0x49, 0x13, 0x23, 0x83, 0xB5, 0x80, 0x23, 0x83, 0x9A, 0xEB, 0x7C, 0x27, 0xE1, 0x6D, 0x3F, 0xC0, 0xBE, 0x16, 0xD3, 0x74, 0x4D, 0x26, 0xD6, 
    0x3B, 0x1D, 0x2B, 0x47, 0xB4, 0x8A, 0xC6, 0xCA, 0xDA, 0x3C, 0xED, 0xB7, 0x86, 0x24, 0x09, 0x1A, 0x0C, 0xE4, 0xE1, 0x55, 0x40, 0xE7, 0xD2, 0xBC, 0xFC, 0x3E, 0x1A, 0xA5, 0x3C, 0x55, 0x5A, 0xAE, 
    0xDC, 0xB3, 0xB3, 0x5D, 0xEE, 0x95, 0xBF, 0x43, 0xED, 0xB3, 0xCE, 0x21, 0xC1, 0x63, 0xB8, 0x7F, 0x2D, 0xCB, 0xA1, 0x19, 0x2A, 0xD8, 0x55, 0x56, 0x32, 0xBD, 0xB9, 0x25, 0x19, 0xD4, 0x75, 0x22, 
    0xD3, 0xBF, 0x35, 0xD7, 0x33, 0x4D, 0x35, 0x6D, 0x13, 0x4F, 0xA1, 0xA1, 0x45, 0x14, 0x57, 0xA0, 0x7C, 0x49, 0xF3, 0xCF, 0x89, 0x7F, 0x6F, 0xFB, 0x4F, 0x08, 0xFF, 0x00, 0xC1, 0x40, 0xB4, 0x6F, 
    0x81, 0x1A, 0x8F, 0x85, 0x35, 0x0B, 0x37, 0xD7, 0xF4, 0xFF, 0x00, 0xB6, 0xD8, 0xEB, 0xF3, 0x5E, 0x22, 0xC1, 0x72, 0xDE, 0x4C, 0xB2, 0x85, 0x58, 0xB1, 0x92, 0x84, 0xC3, 0x24, 0x41, 0xB7, 0x64, 
    0xC8, 0xA5, 0x76, 0xE3, 0xE6, 0xAF, 0xA1, 0xAB, 0xC0, 0x3F, 0x6A, 0x1F, 0xD8, 0x07, 0x43, 0xFD, 0xA5, 0xFF, 0x00, 0x68, 0x7F, 0x85, 0x9F, 0x12, 0x1F, 0x55, 0x93, 0x44, 0xD6, 0xFE, 0x1A, 0xEA, 
    0x50, 0xDE, 0x39, 0x8A, 0xD4, 0x4A, 0x75, 0x88, 0x21, 0x9D, 0x2E, 0x22, 0xB7, 0x76, 0x2C, 0x36, 0x05, 0x95, 0x58, 0x86, 0x01, 0xB8, 0x96, 0x41, 0x8E, 0x41, 0x1E, 0xFF, 0x00, 0x5C, 0x78, 0x5F, 
    0xAC, 0x29, 0xD4, 0x55, 0xB5, 0x57, 0xF7, 0x5E, 0x9B, 0x5B, 0x6F, 0x93, 0xEE, 0x7D, 0x4F, 0x10, 0xBC, 0x92, 0x78, 0x5C, 0x0D, 0x4C, 0xA5, 0x38, 0xD4, 0xF6, 0x56, 0xAF, 0x17, 0xCD, 0xFC, 0x58, 
    0xCA, 0x4B, 0x99, 0x39, 0x36, 0xBD, 0xF8, 0xF2, 0xBB, 0x47, 0x45, 0xE4, 0xEE, 0x91, 0x45, 0x14, 0x57, 0x61, 0xF2, 0xC1, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x07, 
    0x8B, 0x7E, 0xDD, 0x9F, 0xB6, 0xA6, 0x93, 0xFB, 0x09, 0x7C, 0x1F, 0xB0, 0xF1, 0x7E, 0xB1, 0xA3, 0xDF, 0xEB, 0x90, 0x6A, 0x1A, 0xCC, 0x1A, 0x3A, 0x5B, 0xDA, 0x48, 0xB1, 0xBA, 0xB4, 0x91, 0xCB, 
    0x29, 0x72, 0x5B, 0x8C, 0x04, 0x85, 0xF8, 0xEE, 0x76, 0x8E, 0x33, 0x91, 0xED, 0x35, 0xF0, 0x4F, 0xFC, 0x1C, 0x4B, 0xFF, 0x00, 0x26, 0x53, 0xE1, 0x7F, 0xFB, 0x1D, 0xED, 0x3F, 0xF4, 0x83, 0x50, 
    0xAF, 0xBD, 0xAB, 0xCF, 0xA1, 0x88, 0x9C, 0xB1, 0x95, 0x68, 0xBD, 0xA2, 0xA3, 0x6F, 0x9D, 0xEF, 0xF9, 0x1F, 0x71, 0x9C, 0x64, 0x98, 0x4C, 0x3F, 0x0C, 0x65, 0xB9, 0x9D, 0x28, 0xFE, 0xF6, 0xBC, 
    0xF1, 0x0A, 0x6E, 0xEF, 0x55, 0x4D, 0xD2, 0xE5, 0xD3, 0x65, 0x6E, 0x69, 0x6D, 0xBD, 0xF5, 0xE8, 0x14, 0x51, 0x45, 0x7A, 0x07, 0xC3, 0x85, 0x14, 0x51, 0x40, 0x05, 0x14, 0x51, 0x40, 0x05, 0x79, 
    0x07, 0xED, 0xED, 0xF0, 0x7B, 0xC4, 0xDF, 0x1F, 0x3F, 0x64, 0x3F, 0x1B, 0xF8, 0x4B, 0xC1, 0xD7, 0xC7, 0x4F, 0xF1, 0x26, 0xAD, 0x67, 0x18, 0xB2, 0x90, 0x4E, 0x61, 0xF3, 0x4C, 0x73, 0x47, 0x2B, 
    0x43, 0xBC, 0x11, 0xB7, 0xCD, 0x44, 0x68, 0xF2, 0x4E, 0x3F, 0x79, 0xCF, 0x19, 0xAF, 0x5F, 0xA2, 0xB3, 0xAD, 0x49, 0x55, 0xA7, 0x2A, 0x72, 0xD9, 0xA6, 0xBE, 0xF3, 0xBF, 0x2A, 0xCC, 0x6A, 0xE5, 
    0xF8, 0xDA, 0x38, 0xFA, 0x16, 0xE7, 0xA5, 0x28, 0xCD, 0x5D, 0x5D, 0x5E, 0x2D, 0x49, 0x5D, 0x75, 0x57, 0x5A, 0xA3, 0xE5, 0x2F, 0xF8, 0x25, 0x27, 0xED, 0x8E, 0x3F, 0x68, 0x1F, 0x83, 0x3F, 0xF0, 
    0x84, 0x78, 0x82, 0xDE, 0xE7, 0x49, 0xF8, 0x8B, 0xF0, 0xBA, 0x08, 0x74, 0x3D, 0x6E, 0xC6, 0xF2, 0x46, 0x6B, 0x9B, 0x81, 0x0A, 0x88, 0x56, 0xE8, 0xEE, 0xF9, 0x8B, 0x33, 0x21, 0x12, 0x03, 0x92, 
    0xB2, 0x03, 0x9E, 0x19, 0x73, 0xF5, 0x6D, 0x7C, 0x03, 0xFF, 0x00, 0x05, 0x20, 0xF8, 0x31, 0xE2, 0x2F, 0xD9, 0x3B, 0xF6, 0x90, 0xF0, 0xDF, 0xED, 0x3B, 0xF0, 0xC7, 0x4B, 0xBD, 0xBC, 0x9A, 0xDE, 
    0x65, 0xB2, 0xF1, 0xB6, 0x97, 0x65, 0x11, 0x71, 0x7D, 0x68, 0x54, 0x06, 0x9D, 0xD5, 0x41, 0xC2, 0xB4, 0x6B, 0xB1, 0xD8, 0x8C, 0x2B, 0xAC, 0x2F, 0x8C, 0x82, 0x6B, 0xEA, 0x0F, 0xD9, 0x0B, 0xF6, 
    0xDF, 0xF0, 0x07, 0xED, 0xB7, 0xE0, 0xFB, 0xAD, 0x57, 0xC1, 0x3A, 0x85, 0xC3, 0xCF, 0xA6, 0x98, 0xD7, 0x52, 0xD3, 0x6F, 0x21, 0xF2, 0x6F, 0x34, 0xE6, 0x70, 0xC5, 0x04, 0x8B, 0x92, 0xA4, 0x36, 
    0xD6, 0xC3, 0x23, 0x32, 0x9D, 0xA4, 0x67, 0x20, 0x81, 0xE5, 0x65, 0xD8, 0xA7, 0x09, 0x7D, 0x4B, 0x10, 0xFD, 0xF8, 0xED, 0xFD, 0xE8, 0xF4, 0x6B, 0xCE, 0xDB, 0x9F, 0xA1, 0xF1, 0xC7, 0x0E, 0x53, 
    0xC5, 0x50, 0x8F, 0x16, 0xE4, 0x74, 0xDF, 0xD4, 0xEB, 0xEB, 0x34, 0xB5, 0xF6, 0x15, 0x5B, 0xB4, 0xE9, 0xCB, 0xB4, 0x5C, 0xB5, 0xA6, 0xDD, 0xAF, 0x19, 0x25, 0xB9, 0x73, 0xF6, 0xB9, 0xFD, 0x90, 
    0x7C, 0x25, 0xFB, 0x6A, 0xFC, 0x33, 0xB3, 0xF0, 0xA7, 0x8C, 0x8E, 0xA8, 0xBA, 0x65, 0x8E, 0xA7, 0x0E, 0xAD, 0x13, 0x69, 0xF7, 0x02, 0x09, 0x44, 0xD1, 0xAC, 0x88, 0x01, 0x62, 0xAC, 0x36, 0xB2, 
    0x4B, 0x22, 0x91, 0x8C, 0xE1, 0xB8, 0x20, 0x80, 0x47, 0xA9, 0x51, 0x45, 0x7A, 0xAA, 0x94, 0x14, 0xDD, 0x44, 0xB5, 0x76, 0xBB, 0xF4, 0xD8, 0xFC, 0xEA, 0xAE, 0x63, 0x8A, 0xAB, 0x86, 0xA7, 0x83, 
    0xA9, 0x36, 0xE9, 0xD3, 0x72, 0x71, 0x8F, 0x48, 0xB9, 0xDB, 0x99, 0xAF, 0x5E, 0x55, 0x7F, 0x40, 0xAF, 0x9B, 0x3F, 0xE0, 0xAA, 0xDF, 0xB6, 0x0E, 0xAB, 0xFB, 0x16, 0xFE, 0xCA, 0x73, 0xF8, 0x8B, 
    0xC3, 0xCB, 0x17, 0xFC, 0x24, 0x9A, 0xC6, 0xA5, 0x06, 0x8D, 0xA5, 0xCD, 0x2C, 0x6B, 0x2C, 0x76, 0x92, 0x3A, 0xC9, 0x2B, 0xCA, 0xC8, 0xDC, 0x36, 0x22, 0x86, 0x40, 0x07, 0x4D, 0xCC, 0x99, 0x04, 
    0x64, 0x57, 0x75, 0xFB, 0x5B, 0xFE, 0xD9, 0xFE, 0x06, 0xFD, 0x8C, 0xBE, 0x1D, 0xDC, 0x6B, 0x9E, 0x2D, 0xD4, 0xE1, 0x17, 0x7E, 0x59, 0x6B, 0x0D, 0x22, 0x19, 0x50, 0xDF, 0xEA, 0x8F, 0xD0, 0x2C, 
    0x51, 0x93, 0x9D, 0xB9, 0xEA, 0xE7, 0xE5, 0x5E, 0xE7, 0xA0, 0x3F, 0x1A, 0xFC, 0x1C, 0xFD, 0x9E, 0xBC, 0x79, 0xFF, 0x00, 0x05, 0x58, 0xFD, 0xA4, 0xF4, 0xDF, 0x8B, 0xFF, 0x00, 0x17, 0x74, 0x0B, 
    0xEF, 0x06, 0x7C, 0x33, 0xF0, 0xC8, 0x88, 0xF8, 0x6F, 0xC3, 0x17, 0x1B, 0xC3, 0xEA, 0x60, 0x37, 0x9A, 0xAC, 0xC1, 0xD5, 0x77, 0x44, 0xE4, 0x86, 0x79, 0x76, 0x2F, 0x98, 0x36, 0xA2, 0xFC, 0xA3, 
    0x2B, 0xE5, 0x66, 0x78, 0xC9, 0x34, 0xF0, 0x98, 0x57, 0x7A, 0xB2, 0xED, 0xF6, 0x57, 0xF3, 0x37, 0xD3, 0xCB, 0xAF, 0x63, 0xF4, 0x6F, 0x0F, 0xF8, 0x5A, 0x8A, 0x9C, 0x78, 0x97, 0x88, 0x22, 0xA3, 
    0x97, 0xD1, 0x77, 0x7C, 0xDF, 0xF2, 0xFA, 0x51, 0xDA, 0x94, 0x23, 0x74, 0xE6, 0xE4, 0xED, 0xCD, 0x6B, 0xC5, 0x2B, 0xF3, 0x33, 0xF4, 0x13, 0xE1, 0x4C, 0xDE, 0x20, 0xB8, 0xF8, 0x5D, 0xE1, 0xB7, 
    0xF1, 0x64, 0x76, 0xD0, 0xF8, 0xA5, 0xF4, 0xAB, 0x56, 0xD6, 0x63, 0xB6, 0xC7, 0x92, 0x97, 0xA6, 0x25, 0xF3, 0xC2, 0x60, 0x91, 0xB4, 0x49, 0xBF, 0x18, 0x3D, 0x31, 0x5B, 0xF4, 0x51, 0x5E, 0xC4, 
    0x55, 0x92, 0x47, 0xE6, 0x35, 0xEA, 0xFB, 0x4A, 0x92, 0xA9, 0x64, 0xAE, 0xDB, 0xB2, 0xD9, 0x5F, 0xA2, 0xF2, 0x5D, 0x02, 0x8A, 0x28, 0xA6, 0x64, 0x14, 0x51, 0x45, 0x00, 0x15, 0xF9, 0xDD, 0xFB, 
    0x58, 0x7C, 0x34, 0xF1, 0xF7, 0xFC, 0x13, 0x57, 0xF6, 0xA3, 0xF1, 0x0F, 0xED, 0x07, 0xF0, 0xD3, 0x42, 0xB1, 0xF1, 0x0F, 0xC3, 0xDF, 0x13, 0x5B, 0xA0, 0xF1, 0x76, 0x83, 0x19, 0x10, 0x7D, 0x88, 
    0x8D, 0x81, 0xA5, 0x18, 0x04, 0x85, 0x67, 0x1E, 0x60, 0x91, 0x55, 0xB6, 0x33, 0xC8, 0x19, 0x76, 0x9C, 0x9F, 0xD1, 0x1A, 0x8A, 0xF6, 0xCA, 0x1D, 0x4E, 0xCA, 0x5B, 0x7B, 0x88, 0xA2, 0xB8, 0xB7, 
    0xB8, 0x46, 0x8E, 0x58, 0xA4, 0x40, 0xC9, 0x22, 0x11, 0x82, 0xAC, 0x0F, 0x04, 0x10, 0x70, 0x41, 0xAE, 0x2C, 0x76, 0x0D, 0x62, 0x20, 0x92, 0x7C, 0xB2, 0x8B, 0xBC, 0x5A, 0xE8, 0xFF, 0x00, 0xCB, 
    0xBA, 0xEA, 0x7D, 0x67, 0x08, 0x71, 0x54, 0xF2, 0x3C, 0x54, 0xE7, 0x3A, 0x4A, 0xB5, 0x0A, 0xB1, 0xE4, 0xAB, 0x4A, 0x57, 0xE5, 0xA9, 0x06, 0xD3, 0x6A, 0xEB, 0x58, 0xC9, 0x34, 0x9C, 0x64, 0xB5, 
    0x8C, 0x95, 0xF6, 0xBA, 0x7F, 0x26, 0xF8, 0x37, 0xFE, 0x0B, 0x79, 0xFB, 0x3A, 0xF8, 0x83, 0xC2, 0x9A, 0x7D, 0xEE, 0xA5, 0xE3, 0x3B, 0x9D, 0x07, 0x50, 0xBA, 0x81, 0x24, 0xB9, 0xD3, 0xAE, 0x34, 
    0x4D, 0x42, 0x69, 0x2C, 0xA4, 0x23, 0xE6, 0x8C, 0xBC, 0x50, 0x34, 0x6D, 0x83, 0xDD, 0x58, 0x83, 0x5C, 0x1F, 0xC6, 0x0F, 0xF8, 0x2D, 0x7C, 0x1E, 0x2B, 0xD6, 0xEF, 0xBC, 0x3D, 0xF0, 0x03, 0xE1, 
    0xEF, 0x8A, 0x3E, 0x2F, 0xEA, 0x29, 0xA6, 0xBC, 0xCD, 0xA9, 0xD8, 0xD8, 0xDD, 0xA4, 0x3A, 0x74, 0x8C, 0x36, 0xA3, 0x9B, 0x6F, 0xB3, 0xB4, 0xD2, 0xA2, 0x39, 0x5D, 0xDB, 0x84, 0x40, 0xF4, 0x0D, 
    0xCE, 0x47, 0xD4, 0x3F, 0xF0, 0xC3, 0x9F, 0x05, 0x3F, 0xE8, 0x8F, 0xFC, 0x2D, 0xFF, 0x00, 0xC2, 0x52, 0xC3, 0xFF, 0x00, 0x8D, 0x57, 0x59, 0xF0, 0xDF, 0xE0, 0xB7, 0x83, 0xBE, 0x0D, 0xDB, 0xDD, 
    0x43, 0xE1, 0x0F, 0x09, 0xF8, 0x6B, 0xC2, 0xB1, 0x5F, 0x32, 0xBD, 0xCA, 0x68, 0xFA, 0x5C, 0x16, 0x2B, 0x70, 0xCA, 0x08, 0x52, 0xE2, 0x25, 0x50, 0xC4, 0x02, 0x71, 0x9E, 0x99, 0xAE, 0x3F, 0x61, 
    0x99, 0xCD, 0x72, 0x4E, 0xAC, 0x62, 0xBB, 0xC6, 0x2E, 0xFF, 0x00, 0x8B, 0x6B, 0xF0, 0x3E, 0xAA, 0x39, 0xC7, 0x00, 0x61, 0x6A, 0x4B, 0x13, 0x86, 0xCB, 0xEB, 0xD6, 0x97, 0xD9, 0x85, 0x5A, 0xD1, 
    0xF6, 0x5B, 0xF5, 0xF6, 0x70, 0x8C, 0xDD, 0x96, 0xCB, 0x9B, 0x5F, 0xB5, 0x73, 0xE2, 0x8F, 0xD8, 0xDF, 0xFE, 0x09, 0x8D, 0xE2, 0x1F, 0x8B, 0x5F, 0x10, 0xA0, 0xF8, 0xD5, 0xFB, 0x4A, 0xDE, 0x0F, 
    0x16, 0xF8, 0xC3, 0x53, 0xB7, 0x49, 0x6C, 0xFC, 0x3B, 0x7B, 0x6F, 0xB2, 0x3D, 0x20, 0xAB, 0xAB, 0x42, 0xF3, 0x04, 0x65, 0x8C, 0xB2, 0xA8, 0x61, 0xF6, 0x7F, 0x2F, 0xCB, 0x5D, 0xE4, 0xB6, 0x5B, 
    0x85, 0xFB, 0xF0, 0x0C, 0x0A, 0x5A, 0x2B, 0xB7, 0x07, 0x81, 0xA5, 0x85, 0x87, 0x2D, 0x3D, 0xDE, 0xED, 0xEA, 0xDB, 0xEE, 0xD9, 0xF2, 0x3C, 0x57, 0xC5, 0xF9, 0x87, 0x10, 0x62, 0x96, 0x23, 0x1C, 
    0xD2, 0x8C, 0x57, 0x2C, 0x29, 0xC5, 0x72, 0xD3, 0xA7, 0x1E, 0x90, 0x84, 0x56, 0x91, 0x4B, 0x4F, 0x37, 0x6D, 0x5B, 0x61, 0x45, 0x14, 0x57, 0x61, 0xF2, 0xE1, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 
    0x50, 0x01, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x01, 0x45, 0x14, 0x50, 0x07, 0xFF, 0xD9, 
} ;


namespace AMBaseboard
{

static u32 m_game_type = 0;
static u32 FIRMWAREMAP = 0;
static u32 m_segaboot = 0;
static u32 m_v2_dma_adr = 0;
static u32 Timeouts[3] = {20000, 20000, 20000};

static u32 GCAMKeyA;
static u32 GCAMKeyB;
static u32 GCAMKeyC;

static File::IOFile* m_netcfg = nullptr;
static File::IOFile* m_netctrl = nullptr;
static File::IOFile* m_extra = nullptr;
static File::IOFile* m_backup = nullptr;
static File::IOFile* m_dimm = nullptr;

unsigned char* m_dimm_disc = nullptr;

static unsigned char firmware[2*1024*1024];
static unsigned char media_buffer[0x300];
static unsigned char network_command_buffer[0x4FFE00];
static unsigned char network_buffer[64*1024];

enum BaseBoardAddress : u32
{
  NetworkCommandAddress = 0x1F800200,
  NetworkBufferAddress1 = 0x1FA00000,
  NetworkBufferAddress2 = 0x1FD00000,
};

static inline void PrintMBBuffer( u32 Address, u32 Length )
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();

	for( u32 i=0; i < Length; i+=0x10 )
	{
    NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}", memory.Read_U32(Address + i),
																                            memory.Read_U32(Address+i+4),
																                            memory.Read_U32(Address+i+8),
																                            memory.Read_U32(Address+i+12) );
	}
}
void FirmwareMap(bool on)
{
  if (on)
    FIRMWAREMAP = 1;
  else
    FIRMWAREMAP = 0;
}

void InitKeys(u32 KeyA, u32 KeyB, u32 KeyC)
{
  GCAMKeyA = KeyA;
  GCAMKeyB = KeyB;
  GCAMKeyC = KeyC;
}
void Init(void)
{
  memset(media_buffer, 0, sizeof(media_buffer));
  memset(network_buffer, 0, sizeof(network_buffer));
  memset(network_command_buffer, 0, sizeof(network_command_buffer));
  memset(firmware, -1, sizeof(firmware));

  m_game_type = 0;
  m_segaboot = 0;
  FIRMWAREMAP = 0;

  GCAMKeyA = 0;
  GCAMKeyB = 0;
  GCAMKeyC = 0;

  if (File::Exists(File::GetUserPath(D_TRIUSER_IDX)) == false)
  {
    File::CreateFullPath(File::GetUserPath(D_TRIUSER_IDX));
  }

  std::string netcfg_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetcfg.bin");
  if (File::Exists(netcfg_Filename))
  {
    m_netcfg = new File::IOFile(netcfg_Filename, "rb+");
  }
  else
  {
    m_netcfg = new File::IOFile(netcfg_Filename, "wb+");
  }
  if (!m_netcfg)
  {
    PanicAlertFmt("Failed to open/create:{0}", netcfg_Filename);
  }

  std::string netctrl_Filename(File::GetUserPath(D_TRIUSER_IDX) + "trinetctrl.bin");
  if (File::Exists(netctrl_Filename))
  {
    m_netctrl = new File::IOFile(netctrl_Filename, "rb+");
  }
  else
  {
    m_netctrl = new File::IOFile(netctrl_Filename, "wb+");
  }

  std::string extra_Filename(File::GetUserPath(D_TRIUSER_IDX) + "triextra.bin");
  if (File::Exists(extra_Filename))
  {
    m_extra = new File::IOFile(extra_Filename, "rb+");
  }
  else
  {
    m_extra = new File::IOFile(extra_Filename, "wb+");
  }

  std::string dimm_Filename(File::GetUserPath(D_TRIUSER_IDX) + "tridimm_" + SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(dimm_Filename))
  {
    m_dimm = new File::IOFile(dimm_Filename, "rb+");
  }
  else
  {
    m_dimm = new File::IOFile(dimm_Filename, "wb+");
  }

  std::string backup_Filename(File::GetUserPath(D_TRIUSER_IDX) + "backup_" + SConfig::GetInstance().GetGameID().c_str() + ".bin");
  if (File::Exists(backup_Filename))
  {
    m_backup = new File::IOFile(backup_Filename, "rb+");
  }
  else
  {
    m_backup = new File::IOFile(backup_Filename, "wb+");
  }

  // This is the firmware for the Triforce
  std::string sega_boot_Filename(File::GetUserPath(D_TRIUSER_IDX) + "segaboot.gcm");
  if (File::Exists(sega_boot_Filename))
  {
    File::IOFile* sega_boot = new File::IOFile(sega_boot_Filename, "rb+");
    if (sega_boot)
    {
      u64 Length = sega_boot->GetSize();
      if (Length >= sizeof(firmware))
      {
        Length = sizeof(firmware);
      }
      sega_boot->ReadBytes(firmware, Length);
      sega_boot->Close();
    }
  }
}
u8 *InitDIMM( void )
{
  if (!m_dimm_disc)
  {
    m_dimm_disc = new u8[512 * 1024 * 1024];
  }
  FIRMWAREMAP = 0;
  return m_dimm_disc;
}
u32 ExecuteCommand(u32* DICMDBUF, u32 Address, u32 Length)
{
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();   

  /*
    The triforce IPL sends these commands first
    01010000 00000101 00000000
    01010000 00000000 0000ffff
  */
  if (GCAMKeyA == 0)
  {
    /*
      Since it is currently unknown how the seed is created
      we have to patch out the crypto.
    */
    if (memory.Read_U32(0x8131ecf4))
    {
      memory.Write_U32(0, 0x8131ecf4);
      memory.Write_U32(0, 0x8131ecf8);
      memory.Write_U32(0, 0x8131ecfC);
      memory.Write_U32(0, 0x8131ebe0);
      memory.Write_U32(0, 0x8131ed6c);
      memory.Write_U32(0, 0x8131ed70);
      memory.Write_U32(0, 0x8131ed74);

      memory.Write_U32(0x4E800020, 0x813025C8);
      memory.Write_U32(0x4E800020, 0x81302674);

      PowerPC::ppcState.iCache.Invalidate(0x813025C8);
      PowerPC::ppcState.iCache.Invalidate(0x81302674);

      HLE::Patch(0x813048B8, "OSReport");
      HLE::Patch(0x8130095C, "OSReport");  // Apploader
    }
  }

  DICMDBUF[0] ^= GCAMKeyA;
  DICMDBUF[1] ^= GCAMKeyB;
  //Length ^= GCAMKeyC; DMA Length is always plain

  u32 seed = DICMDBUF[0] >> 16;

  GCAMKeyA *= seed;
  GCAMKeyB *= seed;
  GCAMKeyC *= seed;

  DICMDBUF[0] <<= 24;
  DICMDBUF[1] <<= 2;
 
  // SegaBoot adds bits for some reason to offset/length
  // also adds 0x20 to offset
  if (DICMDBUF[1] == 0x00100440)
  {
    m_segaboot = 1;
  }

  u32 Command = DICMDBUF[0];
  u32 Offset  = DICMDBUF[1];
 
	INFO_LOG_FMT(DVDINTERFACE, "GCAM: {:08x} {:08x} DMA=addr:{:08x},len:{:08x} Keys: {:08x} {:08x} {:08x}",
                                Command, Offset, Address, Length, GCAMKeyA, GCAMKeyB, GCAMKeyC );

  // Test menu exit to sega boot
  //if (Offset == 0x0002440 && Address == 0x013103a0 )
  //{
  //  FIRMWAREMAP = 1;
  //}
  
	switch(Command>>24)
	{
		// Inquiry
    case 0x12:
      if(FIRMWAREMAP == 1)
      {
        FIRMWAREMAP = 0;
        m_segaboot = 0;
      }
      //Avalon excepts higher version
      if (GetGameType() == KeyOfAvalon )
      {
        return 0x29484100;
      }

			return 0x21000000;
			break;
		// Read
		case 0xA8:
      if ((Offset & 0x8FFF0000) == 0x80000000 )
			{
				switch(Offset)
				{
				// Media board status (1)
        case 0x80000000: 
					memory.Write_U16( Common::swap16( 0x0100 ), Address );
					break;
				// Media board status (2)
        case 0x80000020: 
					memset( memory.GetPointer(Address), 0, Length );
					break;
				// Media board status (3)
        case 0x80000040: 
					memset( memory.GetPointer(Address), 0xFF, Length );
					// DIMM size (512MB)
					memory.Write_U32( Common::swap32( 0x20000000 ), Address );
					// GCAM signature
					memory.Write_U32( 0x4743414D, Address+4 );
          break;
        // ?
        case 0x80000100:
          memory.Write_U32(Common::swap32( (u32)0x001F1F1F ), Address);
          break;
        // Firmware status (1)
        case 0x80000120:
          memory.Write_U32(Common::swap32( (u32)0x01FA ), Address);
          break;
				// Firmware status (2)
        case 0x80000140:
          memory.Write_U32(Common::swap32((u32)1), Address);
          break;
        case 0x80000160:
          memory.Write_U32( 0x00001E00, Address);
          break;
        case 0x80000180:
          memory.Write_U32( 0, Address);
          break;
        case 0x800001A0:
          memory.Write_U32( 0xFFFFFFFF , Address);
          break;
				default:
					PrintMBBuffer( Address, Length );
          PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", Offset);
					break;
				}
				return 0;
      }
			// Network configuration
			if( (Offset == 0x00000000) && (Length == 0x80) )
			{
        m_netcfg->Seek(0, File::SeekOrigin::Begin);
				m_netcfg->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
      // Extra Settings
      // media crc check on/off
      if ((Offset == 0x1FFEFFE0) && (Length == 0x20))
      {
        m_extra->Seek(0, File::SeekOrigin::Begin);
        m_extra->ReadBytes(memory.GetPointer(Address), Length);
        return 0;
      }      
			// DIMM memory (8MB)
			if( (Offset >= 0x1F000000) && (Offset <= 0x1F800000) )
			{
        u32 dimmoffset = Offset - 0x1F000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
				m_dimm->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
			// DIMM command (V1)
			if( (Offset >= 0x1F900000) && (Offset <= 0x1F90003F) )
			{
        u32 dimmoffset = Offset - 0x1F900000;
				memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );
				
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (1) ({:08x})", dimmoffset);
				PrintMBBuffer( Address, Length );
				return 0;
			}
			// Network command
			if( (Offset >= NetworkCommandAddress) && (Offset <= 0x1FCFFFFF) )
      {
        u32 dimmoffset = Offset - NetworkCommandAddress;
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK COMMAND BUFFER ({:08x},{})", Offset, Length );
        memcpy(memory.GetPointer(Address), network_command_buffer + dimmoffset, Length); 		 
				return 0;
      }

      // Network buffer 
      if ((Offset >= 0x1FA00000) && (Offset <= 0x1FA0FFFF))
      {
        u32 dimmoffset = Offset - 0x1FA00000;
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (1) ({:08x},{})", Offset, Length);
        memcpy(memory.GetPointer(Address), network_buffer + dimmoffset, Length);
        return 0;
      }

      // Network buffer
      if ((Offset >= 0x1FD00000) && (Offset <= 0x1FD0FFFF))
      {
        u32 dimmoffset = Offset - 0x1FD00000;
        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read NETWORK BUFFER (2) ({:08x},{})", Offset, Length);
        memcpy(memory.GetPointer(Address), network_buffer + dimmoffset, Length);
        return 0;
      }
      
			// DIMM command (V2)
			if( (Offset >= 0x84000000) && (Offset <= 0x8400005F) )
			{
				u32 dimmoffset = Offset - 0x84000000;
				memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );
				
				NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (2) ({:08x})", dimmoffset );
				PrintMBBuffer( Address, Length );
				return 0;
      }

      // DIMM command (V2)
      if( Offset == 0x88000000 )
      {
        u32 dimmoffset = Offset - 0x88000000;

        memset(media_buffer, 0, 0x20);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Execute command:{:03X}", *(u16*)(media_buffer + 0x202));

        switch( *(u16*)(media_buffer + 0x202) )
        {
          case 1:
            *(u32*)(media_buffer) = 0x1FFF8000;
            break;
          default:
            break;
        }

        memcpy( memory.GetPointer(Address), media_buffer + dimmoffset, Length );

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (?) ({:08x})", dimmoffset);
        PrintMBBuffer(Address, Length);
        return 0;
      }

      // DIMM command (V2)
      if ((Offset >= 0x89000000) && (Offset <= 0x89000200))
      {
        u32 dimmoffset = Offset - 0x89000000;
        memcpy(memory.GetPointer(Address), media_buffer + dimmoffset, Length);

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Read MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset);
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM memory (8MB)
			if( (Offset >= 0xFF000000) && (Offset <= 0xFF800000) )
			{
				u32 dimmoffset = Offset - 0xFF000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
				m_dimm->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}
			// Network control
			if( (Offset == 0xFFFF0000) && (Length == 0x20) )
			{
        m_netctrl->Seek(0, File::SeekOrigin::Begin);
				m_netctrl->ReadBytes( memory.GetPointer(Address), Length );
				return 0;
			}

			// Max GC disc offset
			if( Offset >= 0x57058000 )
			{
				PanicAlertFmtT("Unhandled Media Board Read:{0:08x}", Offset );
			}

      if (FIRMWAREMAP)
      {
        if (m_segaboot)
        {
          DICMDBUF[1] &= ~0x00100000;
          DICMDBUF[1] -= 0x20;
        }
        memcpy(memory.GetPointer(Address), firmware + Offset, Length);
        return 0;
      }

      if (m_dimm_disc)
      {
        memcpy(memory.GetPointer(Address), m_dimm_disc + Offset, Length);
        return 0;
      }

      return 1;			 
			break;
		// Write
		case 0xAA:
			if( (Offset == 0x00600000 ) && (Length == 0x20) )
			{
				FIRMWAREMAP = 1;
				return 0;
			}
			if( (Offset == 0x00700000 ) && (Length == 0x20) )
			{
				FIRMWAREMAP = 1;
				return 0;
			}
			if(FIRMWAREMAP)
			{
				// Firmware memory (2MB)
				if( (Offset >= 0x00400000) && (Offset <= 0x600000) )
				{
					u32 fwoffset = Offset - 0x00400000;
					memcpy( firmware + fwoffset, memory.GetPointer(Address), Length );
					return 0;
				}
			}
			// Network configuration
			if( (Offset == 0x00000000) && (Length == 0x80) )
			{
        m_netcfg->Seek(0, File::SeekOrigin::Begin);
				m_netcfg->WriteBytes( memory.GetPointer(Address), Length );
        m_netcfg->Flush();
				return 0;
      }
      // Extra Settings
      // media crc check on/off
      if ((Offset == 0x1FFEFFE0) && (Length == 0x20))
      {
        m_extra->Seek(0, File::SeekOrigin::Begin);
        m_extra->WriteBytes(memory.GetPointer(Address), Length);
        m_extra->Flush();
        return 0;
      }      
			// Backup memory (8MB)
			if( (Offset >= 0x000006A0) && (Offset <= 0x00800000) )
			{
        m_backup->Seek(Offset, File::SeekOrigin::Begin);
        m_backup->WriteBytes(memory.GetPointer(Address), Length);
        m_backup->Flush();
				return 0;
			}
			// DIMM memory (8MB)
			if( (Offset >= 0x1F000000) && (Offset <= 0x1F800000) )
			{
				u32 dimmoffset = Offset - 0x1F000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
        m_dimm->WriteBytes(memory.GetPointer(Address), Length);
        m_dimm->Flush();
				return 0;
			}
			// Network command
			if( (Offset >= NetworkCommandAddress) && (Offset <= 0x1F8003FF) )
			{
				u32 offset = Offset - NetworkCommandAddress;

				memcpy(network_command_buffer + offset, memory.GetPointer(Address), Length);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK COMMAND BUFFER ({:08x},{})", Offset, Length);
				PrintMBBuffer( Address, Length );
				return 0;
      }
      // Network buffer
      if ((Offset >= 0x1FA00000) && (Offset <= 0x1FA0FFFF))
      {
        u32 offset = Offset - 0x1FA00000;

        memcpy(network_buffer + offset, memory.GetPointer(Address), Length);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (1) ({:08x},{})", Offset, Length);
        PrintMBBuffer(Address, Length);
        return 0;
      }
      // Network buffer
      if ((Offset >= 0x1FD00000) && (Offset <= 0x1FD0FFFF))
      {
        u32 offset = Offset - 0x1FD00000;

        memcpy(network_buffer + offset, memory.GetPointer(Address), Length);

        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write NETWORK BUFFER (2) ({:08x},{})", Offset, Length);
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM command, used when inquiry returns 0x21000000
			if( (Offset >= 0x1F900000) && (Offset <= 0x1F90003F) )
			{
				u32 dimmoffset = Offset - 0x1F900000;
				memcpy( media_buffer + dimmoffset, memory.GetPointer(Address), Length );
				
				INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (1) ({:08x})", dimmoffset );
				PrintMBBuffer( Address, Length );
				return 0;
			}

			// DIMM command, used when inquiry returns 0x29000000
			if( (Offset >= 0x84000000) && (Offset <= 0x8400005F) )
      {
        u32 dimmoffset = Offset - 0x84000000;
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (2) ({:08x})", dimmoffset);
        PrintMBBuffer(Address, Length);

        u8 cmd_flag = memory.Read_U8(Address);

        if (dimmoffset == 0x20 && cmd_flag != 0 )
        {
          m_v2_dma_adr = Address;
        }

        //if (dimmoffset == 0x40 && cmd_flag == 0)
        //{
        //  INFO_LOG_FMT(DVDINTERFACE, "GCAM: PC:{:08x}", PC);
        //  PowerPC::breakpoints.Add( PC+8, false );
        //}

				if( dimmoffset == 0x40 && cmd_flag == 1 )
        { 
          // Recast for easier access
          u32* media_buffer_in_32 = (u32*)(media_buffer + 0x20);
          u16* media_buffer_in_16 = (u16*)(media_buffer + 0x20);
          u32* media_buffer_out_32 = (u32*)(media_buffer);
          u16* media_buffer_out_16 = (u16*)(media_buffer);

					INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Execute command:{:03X}", media_buffer_in_16[1] );
					
					memset(media_buffer, 0, 0x20);

          media_buffer_out_32[0] = media_buffer_in_32[0] | 0x80000000;  // Set command okay flag

          for (u32 i = 0; i < 0x20; i += 4)
          {
            *(u32*)(media_buffer + 0x40 + i) = *(u32*)(media_buffer);
          }

          //memory.Write_U8( 0xA9, m_v2_dma_adr + 0x20 );

					switch (media_buffer_in_16[1])
          {
          // ?
          case 0x000:
            media_buffer_out_32[1] = 1;
            break;
          // NAND size
          case 0x001:
            media_buffer_out_32[1] = 0x1FFF8000;
            break;
          // Loading Progress
					case 0x100:
						// Status
            media_buffer_out_32[1] = 5;
						// Progress in %
            media_buffer_out_32[2] = 100;
            break;
          // SegaBoot version: 3.09
          case 0x101:
            // Version
            media_buffer_out_16[2] = Common::swap16(0x0309);
            // Unknown
            media_buffer_out_16[3] = 2;
            media_buffer_out_32[2] = 0x4746; // "GF" 
            media_buffer_out_32[4] = 0xFF;
            break;
          // System flags
          case 0x102:
            // 1: GD-ROM
            media_buffer[4] = 0;
            media_buffer[5] = 1;
            // enable development mode (Sega Boot)
            // This also allows region free booting
            media_buffer[6] = 1;
            media_buffer_out_16[4] = 0;  // Access Count
            // Only used when inquiry 0x29
            /*
              0: NAND/MASK BOARD(HDD)
              1: NAND/MASK BOARD(MASK)
              2: NAND/MASK BOARD(NAND)
              3: NAND/MASK BOARD(NAND)
              4: DIMM BOARD (TYPE 3)
              5: DIMM BOARD (TYPE 3)
              6: DIMM BOARD (TYPE 3)
              7: N/A
              8: Unknown
            */
            media_buffer[7] = 1;
            break;
          // Media board serial
          case 0x103:
            memcpy(media_buffer + 4, "A85E-01A62204904", 16);
            break;
          case 0x104:
            media_buffer[4] = 1;
            break;
          }


          memset(media_buffer + 0x20, 0, 0x20 );
          return 0;
				}
        else
        {
          memcpy(media_buffer + dimmoffset, memory.GetPointer(Address), Length);
        }
        return 0;
			}

      // DIMM command, used when inquiry returns 0x29000000
      if ((Offset >= 0x89000000) && (Offset <= 0x89000200))
      {
        u32 dimmoffset = Offset - 0x89000000;
        INFO_LOG_FMT(DVDINTERFACE, "GC-AM: Write MEDIA BOARD COMM AREA (3) ({:08x})", dimmoffset );
        PrintMBBuffer(Address, Length);

        memcpy(media_buffer + dimmoffset, memory.GetPointer(Address), Length);

        return 0;
      }

      // Firmware Write
      if ((Offset >= 0x84800000) && (Offset <= 0x84818000))
      {
        u32 dimmoffset = Offset - 0x84800000;

        NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: Write Firmware ({:08x})", dimmoffset );
        PrintMBBuffer(Address, Length);
        return 0;
      }

			// DIMM memory (8MB)
			if( (Offset >= 0xFF000000) && (Offset <= 0xFF800000) )
			{
				u32 dimmoffset = Offset - 0xFF000000;
        m_dimm->Seek(dimmoffset, File::SeekOrigin::Begin);
        m_dimm->WriteBytes(memory.GetPointer(Address), Length);
        m_dimm->Flush();
				return 0;
			}
			// Network control
			if( (Offset == 0xFFFF0000) && (Length == 0x20) )
			{
				m_netctrl->Seek( 0, File::SeekOrigin::Begin );
        m_netctrl->WriteBytes(memory.GetPointer(Address), Length);
        m_netctrl->Flush();
				return 0;
			}
			// Max GC disc offset
			if( Offset >= 0x57058000 )
			{
				PrintMBBuffer( Address, Length );
				PanicAlertFmtT("Unhandled Media Board Write:{0:08x}", Offset );
			}
			break;
		// Execute
    case 0xAB:
			if( (Offset == 0) && (Length == 0) )
      {
        // Recast for easier access
        u32* media_buffer_in_32  = (u32*)(media_buffer + 0x20);
        u16* media_buffer_in_16  = (u16*)(media_buffer + 0x20);
        u32* media_buffer_out_32 = (u32*)(media_buffer);
        u16* media_buffer_out_16 = (u16*)(media_buffer);

				memset( media_buffer, 0, 0x20 );

				media_buffer_out_16[0] = media_buffer_in_16[0];

				// Command
        media_buffer_out_16[1] = media_buffer_in_16[1] | 0x8000;  // Set command okay flag
				
				if (media_buffer_in_16[1])
          NOTICE_LOG_FMT(DVDINTERFACE, "GCAM: Execute command:{:03X}", media_buffer_in_16[1]);

				switch (media_buffer_in_16[1])
				{
					// ?
					case 0x000:
            media_buffer_out_32[1] = 1;
						break;
					// DIMM size
          case 0x001:
            media_buffer_out_32[1] = 0x20000000;
						break;
					// Media board status
					/*
					0x00: "Initializing media board. Please wait.."
					0x01: "Checking network. Please wait..." 
					0x02: "Found a system disc. Insert a game disc"
					0x03: "Testing a game program. {:d}%%"
					0x04: "Loading a game program. {:d}%%"
					0x05: go
					0x06: error xx
					*/
					case 0x100:
          {
            // Fake loading the game to have a chance to enter test mode
            static u32 status   = 4;
            static u32 progress = 80;
            // Status
            media_buffer_out_32[1] = status;
            // Progress in %
            media_buffer_out_32[2] = progress;
            if (progress < 100)
            {
              progress++;
            }
            else
            {
              status = 5;
            }            
          }
          break;
					// SegaBoot version: 3.11
					case 0x101:
						// Version
            media_buffer_out_16[2] = Common::swap16(0x1103);
						// Unknown
            media_buffer_out_16[3] = 1;
            media_buffer_out_32[2] = 1;
            media_buffer_out_32[4] = 0xFF;
						break;
					// System flags
          case 0x102:
            // 1: GD-ROM
            media_buffer[4] = 1;
            media_buffer[5] = 1;
						// Enable development mode (Sega Boot)
            // This also allows region free booting
						media_buffer[6] = 1; 
            media_buffer_out_16[4] = 0;  // Access Count
						break;
					// Media board serial
					case 0x103:
						memcpy( media_buffer + 4, "A89E-27A50364511", 16 );
						break;
					case 0x104:
						media_buffer[4] = 1;
						break;
					// Hardware test
					case 0x301:
						// Test type
						/*
							0x01: Media board
							0x04: Network
						*/
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x301: ({:08x})", *(u32*)(media_buffer+0x24) );

						//Pointer to a memory address that is directly displayed on screen as a string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x28) );

						// On real system it shows the status about the DIMM/GD-ROM here
						// We just show "TEST OK"
            memory.Write_U32(0x54534554, media_buffer_in_32[4] );
            memory.Write_U32(0x004B4F20, media_buffer_in_32[4] + 4 );

						media_buffer_out_32[1] = media_buffer_in_32[1];
						break;
					case 0x401:
					{
            u32 fd        = media_buffer_in_32[2];
            u32 addr_off  = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 len_off   = media_buffer_in_32[4] - NetworkCommandAddress;

            struct sockaddr* addr = (struct sockaddr*)(network_command_buffer + addr_off);
            int* len              = (int*)(network_command_buffer + len_off);

            int ret = 0;
            int err = 0;

            u32 timeout = Timeouts[0] / 1000;
            while (timeout--)
            {
              ret = accept(fd, addr, len);
              if (ret == SOCKET_ERROR)
              {
                err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                {
                  Sleep(1);
                  continue;
                }
              }
              else
              {
                // Set newly created socket non-blocking
#ifdef WIN32
                u_long val = 1;
                ioctlsocket(fd, FIONBIO, &val);
#else
                int flags = cntl(fd, F_GETFL);
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
                break;
              }
            }

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: accept( {} ):{} ({})\n", fd, ret, err);
            media_buffer_out_32[1] = ret;
					} break;
					case 0x402:
					{
						struct sockaddr_in addr;

						u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3] - NetworkCommandAddress; 
            u32 len = media_buffer_in_32[4];

						memcpy( (void*)&addr, network_command_buffer + off, sizeof(struct sockaddr_in) );
						
						addr.sin_family					= Common::swap16(addr.sin_family);
						*(u32*)(&addr.sin_addr)	= Common::swap32(*(u32*)(&addr.sin_addr));

            /*
              Triforce games usually use hardcoded IPs
              This is replaced to listen to the ANY address instead
            */
            addr.sin_addr.s_addr = INADDR_ANY;

						int ret = bind( fd, (const sockaddr*)&addr, len );
						int err = WSAGetLastError();

            //if (ret < 0 )
            //  PanicAlertFmt("Socket Bind Failed with{0}", err);

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: bind( {}, ({},{:08x}:{}), {} ):{} ({})\n", fd,
                           addr.sin_family, addr.sin_addr.S_un.S_addr,
                           Common::swap16(addr.sin_port), len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
					case 0x403:
					{
            u32 fd = media_buffer_in_32[2];

            int ret = closesocket(fd);

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: closesocket( {} ):{}\n", fd, ret);

            media_buffer_out_32[1] = ret;
					} break;
					case 0x404:
					{
						struct sockaddr_in addr;

						u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 len = media_buffer_in_32[4];

            int ret = 0;
            int err = 0;

						memcpy( (void*)&addr, network_command_buffer + off , sizeof(struct sockaddr_in) );

            // CyCraft Connect IP, change to localhost
            if (addr.sin_addr.S_un.S_addr == 1863035072)
            {
              addr.sin_addr.S_un.S_addr = 0x7F000001;
            }

            // NAMCO Camera
            if (addr.sin_addr.S_un.S_addr == 0xc0a81d68)
            {
              addr.sin_addr.S_un.S_addr = 0x7F000001;
              addr.sin_family = htons(AF_INET); // fix family?
            }

            addr.sin_family = Common::swap16(addr.sin_family);
            *(u32*)(&addr.sin_addr) = Common::swap32(*(u32*)(&addr.sin_addr));
              
            u32 timeout = 10;
            while(timeout--)
            {
              ret = connect(fd, (const sockaddr*)&addr, len);
              if( ret == 0 )
                break;
              if ( ret == SOCKET_ERROR )
              {
                err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAEALREADY )
                {
                  Sleep(1);
                  continue;
                }
                if (err == WSAEISCONN)
                {
                  ret = 0;
                  break;
                }
              }
            } 

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: connect( {}, ({},{}:{}), {} ):{} ({})\n", fd, addr.sin_family, inet_ntoa(addr.sin_addr), Common::swap16(addr.sin_port), len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
					// getIPbyDNS
					case 0x405:
					{
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x405: ({:08x})", *(u32*)(media_buffer+0x24) );
						// Address of string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x28) );
						// Length of string
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", *(u32*)(media_buffer+0x2C) );

						u32 offset = media_buffer_in_32[2] - NetworkCommandAddress;
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: getIPbyDNS({})", (char*)(network_command_buffer + offset) );						
					} break;
					// inet_addr
					case 0x406:
					{
            char *IP			= (char*)(network_command_buffer + (media_buffer_in_32[2] - NetworkCommandAddress) );
            u32 IPLength = media_buffer_in_32[3];

						memcpy( media_buffer + 8, IP, IPLength );
							
						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: inet_addr({})\n", IP );
            media_buffer_out_32[1] = Common::swap32(inet_addr(IP));
					} break;
          /*
            0x407: ioctl
          */
					case 0x408:
					{
            u32 fd      = media_buffer_in_32[2];
            u32 backlog = media_buffer_in_32[3];

						int ret = listen( fd, backlog );

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: listen( {}, {} ):{:d}\n", fd, backlog, ret);

            media_buffer_out_32[1] = ret;
					} break;
					case 0x409:
					{
            u32 fd  = media_buffer_in_32[2];
            u32 off = media_buffer_in_32[3];
            u16 len = media_buffer_in_32[4];

            int ret = 0; 
            char* buffer = (char*)(network_buffer+off);

            if( off >= NetworkCommandAddress && off < 0x1FD00000 )
            {
              buffer = (char*)(network_command_buffer + off - NetworkCommandAddress);

              if( off + len > sizeof(network_command_buffer) )
              {
                PanicAlertFmt("RECV: Buffer overrun:{0} {1} ", off, len);
              }
            }
            else
            {
              if (off + len > sizeof(network_buffer))
              {
                PanicAlertFmt("RECV: Buffer overrun:{0} {1} ", off, len);
              }
            }

            u32 timeout = Timeouts[0] / 1000;
            int err = 0;
            while (timeout--)
            {
              ret = recv(fd, buffer, len, 0);
              if (ret >= 0)
                break;

              if (ret == SOCKET_ERROR)
              {
                err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                {
                  Sleep(1);
                  continue;
                }
                break;
              }
            }

            if( err == WSAEWOULDBLOCK )
            {
              ret = 0;
            }

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: recv( {}, 0x{:08x}, {} ):{} {}\n", fd, off, len, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
					// send
					case 0x40A:
					{
            u32 fd     = media_buffer_in_32[2];
            u32 offset = media_buffer_in_32[3];
            u32 len    = media_buffer_in_32[4];
            
            int ret = 0;

            if (offset >= NetworkBufferAddress1 && offset < 0x1FA01000)
            {
              offset -= NetworkBufferAddress1;
            }
            else if (offset >= NetworkBufferAddress2 && offset < 0x1FD01000)
            {
              offset -= NetworkBufferAddress2;
            }
            else 
            {
              ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: send(error) unhandled destination:{}\n", offset  );
            }

            ret = send(fd, (char*)(network_buffer + offset), len, 0);
            int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: send( {}, 0x{:08x}, {} ): {} {}\n", fd, offset, len, ret ,err );

						media_buffer_out_32[1] = ret;
					} break;
					// socket - Protocol is not sent
					case 0x40B:
					{
            u32 af   = media_buffer_in_32[2];
            u32 type = media_buffer_in_32[3];

						SOCKET fd = socket(af, type, IPPROTO_TCP);

            // Set socket non-blocking
#ifdef WIN32
            u_long val = 1;
            ioctlsocket(fd, FIONBIO, &val);
#else
            int flags = cntl( fd, F_GETFL );
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: socket( {}, {}, 6 ):{}\n", af, type, fd);
							
						media_buffer_out_32[1] = fd;
            media_buffer_out_32[2] = media_buffer_in_32[2];
            media_buffer_out_32[3] = media_buffer_in_32[3];
            media_buffer_out_32[4] = media_buffer_in_32[4];
					} break;
					// select
					case 0x40C:
					{
            u32 nfds     = media_buffer_in_32[2];
            u32 NOffsetA = media_buffer_in_32[3] - NetworkCommandAddress;
            u32 NOffsetB = media_buffer_in_32[6] - NetworkCommandAddress;

            fd_set readfds_{};
            fd_set writefds_{};

            fd_set* readfds  = &readfds_;     
            fd_set* writefds = &writefds_;

            // Either of these can be zero but select still expects two inputs
            if (media_buffer_in_32[3])
            {
              readfds = (fd_set*)(network_command_buffer + NOffsetA);
            }

            if (media_buffer_in_32[6])
            {
              writefds = (fd_set*)(network_command_buffer + NOffsetB);
            }

            FD_ZERO(writefds);
            FD_ZERO(readfds);

            FD_SET(nfds, readfds);
            FD_SET(nfds, writefds);

            timeval timeout;
            timeout.tv_sec = Timeouts[0] / 1000;
            timeout.tv_usec= 0;

            int ret = select( nfds, readfds, writefds, nullptr, &timeout );

            int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: select( 0x{:08x} 0x{:08x} 0x{:08x} ):{} {} \n", nfds, NOffsetA, NOffsetB, ret, err);
						//hexdump( NetworkCMDBuffer, 0x40 );

						media_buffer_out_32[1] = ret;
					} break;
          /*
            0x40D: shutdown
          */
					// setsockopt
					case 0x40E:
					{ 
						SOCKET s            = (SOCKET)(media_buffer_in_32[2]);
            int level           =    (int)(media_buffer_in_32[3]);
            int optname         =    (int)(media_buffer_in_32[4]);
            const char* optval  =  (char*)(network_command_buffer + media_buffer_in_32[5] - NetworkCommandAddress );
            int optlen          =    (int)(media_buffer_in_32[6]);

						int ret = setsockopt( s, level, optname, optval, optlen );

						int err = WSAGetLastError();

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: setsockopt( {:d}, {:04x}, {}, {:p}, {} ):{:d} ({})\n", s, level, optname, optval, optlen, ret, err);

            media_buffer_out_32[1] = ret;
					} break;
          /*
            0x40F: getsockopt
          */
					case 0x410:
					{
						u32 fd       = media_buffer_in_32[2];
            u32 timeoutA = media_buffer_in_32[3];
            u32 timeoutB = media_buffer_in_32[4];
            u32 timeoutC = media_buffer_in_32[5];

            Timeouts[0] = timeoutA;
            Timeouts[1] = timeoutB;
            Timeouts[2] = timeoutC;

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: SetTimeOuts( {}, {}, {}, {} )\n", fd, timeoutA,  timeoutB, timeoutC );
							
						media_buffer_out_32[1] = 0;
            media_buffer_out_32[3] = media_buffer_in_32[3];
            media_buffer_out_32[4] = media_buffer_in_32[4];
            media_buffer_out_32[5] = media_buffer_in_32[5];
					} break;
          /*
            This excepts 0x46 to be returned
          */
					case 0x411:
					{
            u32 fd = media_buffer_in_32[2];

            //int ret = closesocket(fd);

						NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: SetUnknownFlag( {} )\n", fd);
            
						media_buffer_out_32[1] = 0x46;
					} break;
          /*
            0x412: routeAdd
            0x413: routeDelete
            0x414: getParambyDHCPExec
          */
					// Set IP
					case 0x415:
					{
            char* IP = (char*)( network_command_buffer + media_buffer_in_32[2] - NetworkCommandAddress );
						//u32 IPLength	= (*(u32*)(media_buffer+0x2C));
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: modifyMyIPaddr({})\n", IP);
					} break;
          /*
*           0x416: recvfrom
            0x417: sendto
            0x418: recvDimmImage
            0x419: sendDimmImage
          */
          /*

            This group of commands is used to establish a link connection.
            Only used by F-Zero AX.
            Uses UDP for connections.
          */

					// Empty reply
					case 0x601: // Init Link ?
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x601");
						break;
					case 0x606: // Setup link?
					{
            struct sockaddr_in addra, addrb;
            addra.sin_addr.S_un.S_addr = media_buffer_in_32[4];
            addrb.sin_addr.S_un.S_addr = media_buffer_in_32[5];

            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x606:");
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:  Size: ({}) ",   media_buffer_in_16[2] );                 // size
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:  Port: ({})",    Common::swap16(media_buffer_in_16[3]) ); // port
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:LinkNum:({:02x})",media_buffer[0x28] );                    // linknum
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:02x})",media_buffer[0x29] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:04x})",media_buffer_in_16[5] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})",    inet_ntoa( addra.sin_addr ) );           // IP
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:   IP:  ({})",    inet_ntoa( addrb.sin_addr ) );           // IP
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",Common::swap32(media_buffer_in_32[6]) ); // some RAM address
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})",Common::swap32(media_buffer_in_32[7]) ); // some RAM address
            
						media_buffer_out_32[1] = 0;
					} break;
					case 0x607: // Search other device?
					{
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: 0x607: ({})", media_buffer_in_16[2] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({})", media_buffer_in_16[3] );
            NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM:        ({:08x})", media_buffer_in_32[2] );

						u8* Data = (u8*)(network_buffer + media_buffer_in_32[2] - 0x1FD00000 );
						
						for( u32 i=0; i < 0x20; i+=0x10 )
						{
							NOTICE_LOG_FMT(DVDINTERFACE, "GC-AM: {:08x} {:08x} {:08x} {:08x}",	*(u32*)(Data+i),
																																			            *(u32*)(Data+i+4),
																																			            *(u32*)(Data+i+8),
																																			            *(u32*)(Data+i+12) );
						}

						media_buffer[4] = 23;
					}	break;
					case 0x614:
					{
						//ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: 0x614");
					}	break;
					default:
						ERROR_LOG_FMT(DVDINTERFACE, "GC-AM: execute buffer UNKNOWN:{:03x}", *(u16*)(media_buffer+0x22) );
						break;
					}

				memset( media_buffer + 0x20, 0, 0x20 );
				return 0;
			}

			PanicAlertFmtT("Unhandled Media Board Execute:{0:08x}", *(u16*)(media_buffer + 0x22) );
			break;
	}

	return 0;
}

u32 GetMediaType(void)
{
  switch (GetGameType())
  {
    default:
    case FZeroAX: // Monster Ride is on a NAND
    case VirtuaStriker3:
    case VirtuaStriker4:
    case GekitouProYakyuu:
    case KeyOfAvalon:
        return GDROM;
        break;

    case MarioKartGP:
    case MarioKartGP2:
        return NAND;
        break;
  }
// never reached
}
u32 GetGameType(void)
{
  u64 gameid = 0;
  // Convert game ID into hex
  sscanf(SConfig::GetInstance().GetGameID().c_str(), "%s", (char*)&gameid);

  // This is checking for the real game IDs (not those people made up) (See boot.id within the game)
  switch (Common::swap32((u32)gameid))
  {
  // SBHA/SBGG - F-ZERO AX
  case 0x53424841:
  case 0x53424747:
      m_game_type = FZeroAX;
      break;
  // SBKJ/SBKP - MARIOKART ARCADE GP
  case 0x53424B50:
  case 0x53424B5A:
      m_game_type = MarioKartGP;
      break;
  // SBNJ/SBNL - MARIOKART ARCADE GP2
  case 0x53424E4A:
  case 0x53424E4C:
      m_game_type = MarioKartGP2;
      break;
  // SBEJ/SBEY - Virtua Striker 2002
  case 0x5342454A:
  case 0x53424559:
      m_game_type = VirtuaStriker3;
      break;
  // SBLJ/SBLK/SBLL - VIRTUA STRIKER 4 Ver.2006
  case 0x53424C4A:
  case 0x53424C4B:
  case 0x53424C4C:
  // SBHJ/SBHN/SBHZ - VIRTUA STRIKER 4 VER.A
  case 0x5342484A:
  case 0x5342484E:
  case 0x5342485A:
  // SBJA/SBJJ  - VIRTUA STRIKER 4
  case 0x53424A41:
  case 0x53424A4A:
      m_game_type = VirtuaStriker4;
      break;
  // SBFX/SBJN - Key of Avalon
  case 0x53424658:
  case 0x53424A4E:
      m_game_type = KeyOfAvalon;
      break;
  // SBGX - Gekitou Pro Yakyuu (DIMM Uprade 3.17 shares the same ID)
  case 0x53424758:
      m_game_type = GekitouProYakyuu;
      break;      
  default:
      PanicAlertFmtT("Unknown game ID:{0:08x}, using default controls.", gameid);
  // GSBJ/G12U/RELS/RELJ - SegaBoot (does not have a boot.id)
  case 0x4753424A:
  case 0x47313255:
  case 0x52454C53:
  case 0x52454c4a:
      m_game_type = VirtuaStriker3;
      break;
  }
	return m_game_type;
}
void Shutdown( void )
{
  m_netcfg->Close();
  m_netctrl->Close();
  m_extra->Close();
  m_backup->Close();
	m_dimm->Close();

  if (m_dimm_disc)
  {
      delete[] m_dimm_disc;
      m_dimm_disc = nullptr;
  }
}

}
