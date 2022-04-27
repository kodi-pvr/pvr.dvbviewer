/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "IStreamReader.h"

#include <kodi/Filesystem.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace dvbviewer ATTR_DLL_LOCAL
{

/* forward declaration */
class Settings;

class TimeshiftBuffer
  : public IStreamReader
{
public:
  TimeshiftBuffer(IStreamReader *strReader,
      const Settings &settings);
  ~TimeshiftBuffer(void);
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
  void DoReadWrite();

  std::string m_bufferPath;
  IStreamReader *m_strReader;
  kodi::vfs::CFile m_filebufferReadHandle;
  kodi::vfs::CFile m_filebufferWriteHandle;
  int m_readTimeout;
  std::time_t m_start = 0;
  std::atomic<uint64_t> m_writePos = { 0 };

  std::atomic<bool> m_running = { false };
  std::thread m_inputThread;
  std::condition_variable m_condition;
  std::mutex m_mutex;
};

} //namespace dvbviewer
