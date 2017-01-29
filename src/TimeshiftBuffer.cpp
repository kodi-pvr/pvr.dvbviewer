#include "TimeshiftBuffer.h"
#include "StreamReader.h"
#include "client.h"
#include "p8-platform/util/util.h"

#define STREAM_READ_BUFFER_SIZE   32768
#define BUFFER_READ_TIMEOUT       10000
#define BUFFER_READ_WAITTIME      50

using namespace ADDON;

TimeshiftBuffer::TimeshiftBuffer(IStreamReader *strReader,
    const std::string &bufferPath)
  : m_strReader(strReader), m_bufferPath(bufferPath), m_start(0)
{
  m_bufferPath += "/tsbuffer.ts";
  m_filebufferWriteHandle = XBMC->OpenFileForWrite(m_bufferPath.c_str(), true);
#ifndef TARGET_POSIX
  m_writePos = 0;
#endif
  Sleep(100);
  m_filebufferReadHandle = XBMC->OpenFile(m_bufferPath.c_str(), READ_NO_CACHE);
}

TimeshiftBuffer::~TimeshiftBuffer(void)
{
  StopThread(0);

  if (m_filebufferWriteHandle)
  {
    // XBMC->TruncateFile doesn't work for unknown reasons
    XBMC->CloseFile(m_filebufferWriteHandle);
    void *tmp;
    if ((tmp = XBMC->OpenFileForWrite(m_bufferPath.c_str(), true)) != nullptr)
      XBMC->CloseFile(tmp);
  }
  if (m_filebufferReadHandle)
    XBMC->CloseFile(m_filebufferReadHandle);
  SAFE_DELETE(m_strReader);
  XBMC->Log(LOG_DEBUG, "Timeshift: Stopped");
}

bool TimeshiftBuffer::Start()
{
  if (m_strReader == nullptr
      || m_filebufferWriteHandle == nullptr
      || m_filebufferReadHandle == nullptr)
    return false;
  if (IsRunning())
    return true;
  XBMC->Log(LOG_INFO, "Timeshift: Started");
  m_start = time(NULL);
  CreateThread();
  return true;
}

void *TimeshiftBuffer::Process()
{
  XBMC->Log(LOG_DEBUG, "Timeshift: Thread started");
  uint8_t buffer[STREAM_READ_BUFFER_SIZE];

  m_strReader->Start();
  while (!IsStopped())
  {
    ssize_t read = m_strReader->ReadData(buffer, sizeof(buffer));
    XBMC->WriteFile(m_filebufferWriteHandle, buffer, read);

#ifndef TARGET_POSIX
    m_mutex.Lock();
    m_writePos += read;
    m_mutex.Unlock();
#endif
  }
  XBMC->Log(LOG_DEBUG, "Timeshift: Thread stopped");
  return NULL;
}

int64_t TimeshiftBuffer::Seek(long long position, int whence)
{
  return XBMC->SeekFile(m_filebufferReadHandle, position, whence);
}

int64_t TimeshiftBuffer::Position()
{
  return XBMC->GetFilePosition(m_filebufferReadHandle);
}

int64_t TimeshiftBuffer::Length()
{
  // We can't use GetFileLength here as it's value will be cached
  // by Kodi until we read or seek above it.
  // see xbm/xbmc/filesystem/HDFile.cpp CHDFile::GetLength()
  //return XBMC->GetFileLength(m_filebufferReadHandle);

  int64_t writePos = 0;
#ifdef TARGET_POSIX
  /* refresh write position */
  XBMC->SeekFile(m_filebufferWriteHandle, 0L, SEEK_CUR);
  writePos = XBMC->GetFilePosition(m_filebufferWriteHandle);
#else
  m_mutex.Lock();
  writePos = m_writePos;
  m_mutex.Unlock();
#endif
  return writePos;
}

ssize_t TimeshiftBuffer::ReadData(unsigned char *buffer, unsigned int size)
{
  /* make sure we never read above the current write position */
  int64_t readPos = XBMC->GetFilePosition(m_filebufferReadHandle);
  unsigned int timeWaited = 0;
  while (readPos + size > Length())
  {
    if (timeWaited > BUFFER_READ_TIMEOUT)
    {
      XBMC->Log(LOG_DEBUG, "Timeshift: Read timed out; waited %u", timeWaited);
      return -1;
    }
    Sleep(BUFFER_READ_WAITTIME);
    timeWaited += BUFFER_READ_WAITTIME;
  }

  return XBMC->ReadFile(m_filebufferReadHandle, buffer, size);
}

time_t TimeshiftBuffer::TimeStart()
{
  return m_start;
}

time_t TimeshiftBuffer::TimeEnd()
{
  return time(NULL);
}

bool TimeshiftBuffer::NearEnd()
{
  //FIXME as soon as we return false here the players current time value starts
  // flickering/jumping
  return true;

  // other PVRs use 10 seconds here, but we aren't doing any demuxing
  // we'll therefore just asume 1 secs needs about 1mb
  //return Length() - Position() <= 10 * 1048576;
}

bool TimeshiftBuffer::CanTimeshift()
{
  return true;
}
