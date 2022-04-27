/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/Filesystem.h>

#include <ctime>

namespace dvbviewer ATTR_DLL_LOCAL
{

class IStreamReader
{
public:
  virtual ~IStreamReader(void) = default;
  virtual bool Start() = 0;
  virtual ssize_t ReadData(unsigned char *buffer, unsigned int size) = 0;
  virtual int64_t Seek(long long position, int whence) = 0;
  virtual int64_t Position() = 0;
  virtual int64_t Length() = 0;
  virtual std::time_t TimeStart() = 0;
  virtual std::time_t TimeEnd() = 0;
  virtual bool IsRealTime() = 0;
  virtual bool IsTimeshifting() = 0;
};

} //namespace dvbviewer
