/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "TimeshiftBuffer.h"
#include "Settings.h"
#include "StreamReader.h"
#include "client.h"

#define BUFFER_SIZE 32 * 1024
#define DEFAULT_READ_TIMEOUT 10
#define READ_WAITTIME 50

using namespace dvbviewer;

TimeshiftBuffer::TimeshiftBuffer(IStreamReader *strReader,
    const Settings &settings)
  : m_strReader(strReader)
{
  m_bufferPath = settings.m_timeshiftBufferPath + "/tsbuffer.ts";
  m_readTimeout = (settings.m_readTimeout) ? settings.m_readTimeout
      : DEFAULT_READ_TIMEOUT;

  m_filebufferWriteHandle.OpenFileForWrite(m_bufferPath, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  m_filebufferReadHandle.OpenFile(m_bufferPath, ADDON_READ_NO_CACHE);
}

TimeshiftBuffer::~TimeshiftBuffer(void)
{
  m_running = false;
  if (m_inputThread.joinable())
    m_inputThread.join();

  if (m_filebufferWriteHandle.IsOpen())
  {
    // XBMC->TruncateFile doesn't work for unknown reasons
    m_filebufferWriteHandle.Close();
    kodi::vfs::CFile tmp;
    if (tmp.OpenFileForWrite(m_bufferPath, true))
      tmp.Close();
  }
  if (m_filebufferReadHandle.IsOpen())
    m_filebufferReadHandle.Close();
  delete m_strReader;
  kodi::Log(ADDON_LOG_DEBUG, "Timeshift: Stopped");
}

bool TimeshiftBuffer::Start()
{
  if (m_strReader == nullptr
      || !m_filebufferWriteHandle.IsOpen()
      || !m_filebufferReadHandle.IsOpen())
    return false;
  if (m_running)
    return true;

  kodi::Log(ADDON_LOG_INFO, "Timeshift: Started");
  m_start = time(nullptr);
  m_running = true;
  m_inputThread = std::thread([&] { DoReadWrite(); });

  return true;
}

void TimeshiftBuffer::DoReadWrite()
{
  kodi::Log(ADDON_LOG_DEBUG, "Timeshift: Thread started");
  uint8_t buffer[BUFFER_SIZE];

  m_strReader->Start();
  while (m_running)
  {
    ssize_t read = m_strReader->ReadData(buffer, sizeof(buffer));

    // don't handle any errors here, assume write fully succeeds
    ssize_t write = m_filebufferWriteHandle.Write(buffer, read);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_writePos += write;

    m_condition.notify_one();
  }
  kodi::Log(ADDON_LOG_DEBUG, "Timeshift: Thread stopped");
  return;
}

int64_t TimeshiftBuffer::Seek(long long position, int whence)
{
  return m_filebufferReadHandle.Seek(position, whence);
}

int64_t TimeshiftBuffer::Position()
{
  return m_filebufferReadHandle.GetPosition();
}

int64_t TimeshiftBuffer::Length()
{
  return m_writePos;
}

ssize_t TimeshiftBuffer::ReadData(unsigned char *buffer, unsigned int size)
{
  int64_t requiredLength = Position() + size;

  /* make sure we never read above the current write position */
  std::unique_lock<std::mutex> lock(m_mutex);
  bool available = m_condition.wait_for(lock, std::chrono::seconds(m_readTimeout),
    [&] { return Length() >= requiredLength; });

  if (!available)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Timeshift: Read timed out; waited %d", m_readTimeout);
    return -1;
  }

  return m_filebufferReadHandle.Read(buffer, size);
}

std::time_t TimeshiftBuffer::TimeStart()
{
  return m_start;
}

std::time_t TimeshiftBuffer::TimeEnd()
{
  return std::time(nullptr);
}

bool TimeshiftBuffer::IsRealTime()
{
  // other PVRs use 10 seconds here, but we aren't doing any demuxing
  // we'll therefore just asume 1 secs needs about 1mb
  return Length() - Position() <= 10 * 1048576;
}

bool TimeshiftBuffer::IsTimeshifting()
{
  return true;
}
