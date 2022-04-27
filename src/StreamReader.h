/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "IStreamReader.h"

namespace dvbviewer ATTR_DLL_LOCAL
{

/* forward declaration */
class Settings;

class StreamReader
  : public IStreamReader
{
public:
  StreamReader(const std::string &streamURL,
      const Settings &settings);
  ~StreamReader(void);
  bool Start() override;
  ssize_t ReadData(unsigned char *buffer, unsigned int size) override;
  int64_t Seek(long long position, int whence) override;
  int64_t Position() override;
  int64_t Length() override;
  std::time_t TimeStart() override;
  std::time_t TimeEnd() override;
  bool IsRealTime() override;
  bool IsTimeshifting() override;

private:
  kodi::vfs::CFile m_streamHandle;
  std::time_t m_start = time(nullptr);
};

} //namespace dvbviewer
