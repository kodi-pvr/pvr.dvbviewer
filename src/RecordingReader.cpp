/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RecordingReader.h"
#include "client.h"

#include <algorithm>
#include <ctime>

#define REOPEN_INTERVAL      30
#define REOPEN_INTERVAL_FAST 10

using namespace dvbviewer;

RecordingReader::RecordingReader(const std::string &streamURL,
    const std::pair<std::time_t, std::time_t> &startEnd)
  : m_streamURL(streamURL), m_timeStart(startEnd.first), m_timeEnd(startEnd.second)
{
  m_readHandle.CURLCreate(m_streamURL);
  m_readHandle.CURLOpen(ADDON_READ_NO_CACHE | ADDON_READ_AUDIO_VIDEO);
  m_len = m_readHandle.GetLength();
  m_nextReopen = std::chrono::steady_clock::now()
      + std::chrono::seconds(REOPEN_INTERVAL);
  m_timeRecorded = std::time(nullptr);
  kodi::Log(ADDON_LOG_DEBUG, "RecordingReader: Started; url=%s, start=%u, end=%u",
      m_streamURL.c_str(), m_timeStart, m_timeEnd);
}

RecordingReader::~RecordingReader(void)
{
  if (m_readHandle.IsOpen())
    m_readHandle.Close();
  kodi::Log(ADDON_LOG_DEBUG, "RecordingReader: Stopped");
}

bool RecordingReader::Start()
{
  return m_readHandle.IsOpen();
}

ssize_t RecordingReader::ReadData(unsigned char *buffer, unsigned int size)
{
  /* check for playback of ongoing recording */
  if (m_timeEnd)
  {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (m_pos == m_len || now > m_nextReopen)
    {
      /* reopen stream */
      kodi::Log(ADDON_LOG_DEBUG, "RecordingReader: Reopening stream...");
      m_readHandle.CURLOpen(ADDON_READ_REOPEN | ADDON_READ_NO_CACHE
          | ADDON_READ_AUDIO_VIDEO);
      m_len = m_readHandle.GetLength();
      m_timeRecorded = std::time(nullptr);
      m_readHandle.Seek(m_pos, SEEK_SET);

      // random value (10 MiB) we choose to switch to fast reopen interval
      bool nearEnd = (m_len - m_pos <= 10 * 1024 * 1024);
      m_nextReopen = now  + std::chrono::seconds(
          nearEnd ? REOPEN_INTERVAL_FAST : REOPEN_INTERVAL);

      /* recording has finished */
      if (m_timeRecorded > m_timeEnd)
      {
        m_timeRecorded = m_timeEnd;
        m_timeEnd = 0;
      }
    }
  }

  ssize_t read = m_readHandle.Read(buffer, size);
  m_pos += read;
  return read;
}

int64_t RecordingReader::Seek(long long position, int whence)
{
  int64_t ret = m_readHandle.Seek(position, whence);
  // for unknown reason seek sometimes doesn't return the correct position
  // so let's sync with the underlaying implementation
  m_pos = m_readHandle.GetPosition();
  m_len = m_readHandle.GetLength();
  return ret;
}

int64_t RecordingReader::Position()
{
  return m_pos;
}

int64_t RecordingReader::Length()
{
  return m_len;
}

std::time_t RecordingReader::TimeStart()
{
  return m_timeStart;
}

std::time_t RecordingReader::TimeRecorded()
{
  return m_timeRecorded;
}
