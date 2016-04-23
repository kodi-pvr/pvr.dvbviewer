#include "RecordingReader.h"
#include "client.h"
#include "p8-platform/util/util.h"
#include "p8-platform/threads/mutex.h"
#include <algorithm>

#define REOPEN_INTERVAL      30
#define REOPEN_INTERVAL_FAST 10

using namespace ADDON;

RecordingReader::RecordingReader(const std::string &streamURL, time_t end)
  : m_streamURL(streamURL), m_end(end), m_fastReopen(false), m_playback(false)
{
  m_readHandle = XBMC->OpenFile(m_streamURL.c_str(), 0);
  m_len = XBMC->GetFileLength(m_readHandle);
  m_pos = 0;
  m_nextReopen = time(NULL) + REOPEN_INTERVAL;
  XBMC->Log(LOG_DEBUG, "RecordingReader: Started; url=%s, end=%u",
      m_streamURL.c_str(), m_end);
}

RecordingReader::~RecordingReader(void)
{
  if (m_readHandle)
    XBMC->CloseFile(m_readHandle);
  XBMC->Log(LOG_DEBUG, "RecordingReader: Stopped");
}

bool RecordingReader::IsValid()
{
  return (m_readHandle != nullptr);
}

ssize_t RecordingReader::ReadData(unsigned char *buffer, unsigned int size)
{
  /* check for playback of ongoing recording */
  if (m_playback && m_end)
  {
    time_t now = time(NULL);
    if (now > m_nextReopen)
    {
FORCE_REOPEN:
      /* reopen stream */
      XBMC->Log(LOG_DEBUG, "RecordingReader: Reopening stream...");
      XBMC->CloseFile(m_readHandle);
      m_readHandle = XBMC->OpenFile(m_streamURL.c_str(), 0);
      m_len = XBMC->GetFileLength(m_readHandle);
      XBMC->SeekFile(m_readHandle, m_pos, SEEK_SET);

      m_nextReopen = now + ((m_fastReopen) ? REOPEN_INTERVAL_FAST : REOPEN_INTERVAL);

      /* recording has finished */
      if (now > m_end)
        m_end = 0;
    }
    else if (m_pos == m_len)
    {
      /* in case we reached the end we need to wait a little */
      int sleep = REOPEN_INTERVAL_FAST + 5;
      if (!m_fastReopen)
        sleep = std::min(sleep, static_cast<int>(m_nextReopen - now + 1));
      XBMC->Log(LOG_DEBUG, "RecordingReader: End reached. Sleeping %d secs",
          sleep);
      P8PLATFORM::CEvent::Sleep(sleep * 1000);
      now += sleep;
      m_fastReopen = true;
      goto FORCE_REOPEN;
    }
  }

  ssize_t read = XBMC->ReadFile(m_readHandle, buffer, size);
  m_pos += read;
  return read;
}

int64_t RecordingReader::Seek(long long position, int whence)
{
  int64_t ret = XBMC->SeekFile(m_readHandle, position, whence);
  // for unknown reason seek sometimes doesn't return the correct position
  // so let's sync with the underlaying implementation
  m_pos = XBMC->GetFilePosition(m_readHandle);
  m_len = XBMC->GetFileLength(m_readHandle);
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

