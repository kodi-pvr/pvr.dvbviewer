/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/Filesystem.h>

#include <atomic>
#include <chrono>
#include <ctime>

namespace dvbviewer ATTR_DLL_LOCAL
{

class RecordingReader
{
public:
  RecordingReader(const std::string &streamURL,
      const std::pair<std::time_t, std::time_t> &startEnd);
  ~RecordingReader(void);
  bool Start();
  ssize_t ReadData(unsigned char *buffer, unsigned int size);
  int64_t Seek(long long position, int whence);
  int64_t Position();
  int64_t Length();

  /* methods if the recording hasn't finished */
  std::time_t TimeStart();
  std::time_t TimeRecorded();

private:
  std::string m_streamURL;
  kodi::vfs::CFile m_readHandle;

  std::time_t m_timeStart; /* start time. 0 if already finished on playback */
  std::time_t m_timeEnd;   /* end time. 0 as soon as recording is finished */
  std::atomic<std::time_t> m_timeRecorded; /* maximum time recorded. changes every reopen */
  std::chrono::steady_clock::time_point m_nextReopen;
  uint64_t m_pos = { 0 };
  uint64_t m_len;
};

} //namespace dvbviewer
