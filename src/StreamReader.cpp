/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "StreamReader.h"
#include "client.h"
#include "Settings.h"

using namespace dvbviewer;

StreamReader::StreamReader(const std::string &streamURL,
  const Settings &settings)
{
  m_streamHandle.CURLCreate(streamURL);
  if (settings.m_readTimeout > 0)
    m_streamHandle.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL,
      "connection-timeout", std::to_string(settings.m_readTimeout));

  kodi::Log(ADDON_LOG_DEBUG, "StreamReader: Started; url=%s", streamURL.c_str());
}

StreamReader::~StreamReader(void)
{
  if (m_streamHandle.IsOpen())
    m_streamHandle.Close();
  kodi::Log(ADDON_LOG_DEBUG, "StreamReader: Stopped");
}

bool StreamReader::Start()
{
  return m_streamHandle.CURLOpen(ADDON_READ_TRUNCATED | ADDON_READ_CHUNKED
      | ADDON_READ_NO_CACHE);
}

ssize_t StreamReader::ReadData(unsigned char *buffer, unsigned int size)
{
  return m_streamHandle.Read(buffer, size);
}

int64_t StreamReader::Seek(long long position, int whence)
{
  return m_streamHandle.Seek(position, whence);
}

int64_t StreamReader::Position()
{
  return m_streamHandle.GetPosition();
}

int64_t StreamReader::Length()
{
  return m_streamHandle.GetLength();
}

std::time_t StreamReader::TimeStart()
{
  return m_start;
}

std::time_t StreamReader::TimeEnd()
{
  return std::time(nullptr);
}

bool StreamReader::IsRealTime()
{
  return true;
}

bool StreamReader::IsTimeshifting()
{
  return false;
}
