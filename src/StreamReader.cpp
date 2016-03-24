#include "StreamReader.h"
#include "client.h"

using namespace ADDON;

StreamReader::StreamReader(const std::string &streamURL)
  : m_start(time(NULL))
{
  m_streamHandle = XBMC->OpenFile(streamURL.c_str(), 0);
  XBMC->Log(LOG_DEBUG, "StreamReader: Started; url=%s", streamURL.c_str());
}

StreamReader::~StreamReader(void)
{
  if (m_streamHandle)
    XBMC->CloseFile(m_streamHandle);
  XBMC->Log(LOG_DEBUG, "StreamReader: Stopped");
}

bool StreamReader::IsValid()
{
  return (m_streamHandle != nullptr);
}

ssize_t StreamReader::ReadData(unsigned char *buffer, unsigned int size)
{
  return XBMC->ReadFile(m_streamHandle, buffer, size);
}

int64_t StreamReader::Seek(long long position, int whence)
{
  return XBMC->SeekFile(m_streamHandle, position, whence);
}

int64_t StreamReader::Position()
{
  return XBMC->GetFilePosition(m_streamHandle);
}

int64_t StreamReader::Length()
{
  return XBMC->GetFileLength(m_streamHandle);
}

time_t StreamReader::TimeStart()
{
  return m_start;
}

time_t StreamReader::TimeEnd()
{
  return time(NULL);
}

bool StreamReader::NearEnd()
{
  return true;
}
