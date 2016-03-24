#pragma once

#ifndef PVR_DVBVIEWER_TIMESHIFTBUFFER_H
#define PVR_DVBVIEWER_TIMESHIFTBUFFER_H

#include "IStreamReader.h"
#include "p8-platform/threads/threads.h"

class TimeshiftBuffer
  : public IStreamReader, public P8PLATFORM::CThread
{
public:
  TimeshiftBuffer(const std::string &streamURL, const std::string &bufferPath);
  ~TimeshiftBuffer(void);
  bool IsValid();
  ssize_t ReadData(unsigned char *buffer, unsigned int size);
  int64_t Seek(long long position, int whence);
  int64_t Position();
  int64_t Length();
  time_t TimeStart();
  time_t TimeEnd();
  bool NearEnd();

private:
  virtual void *Process(void);

  std::string m_bufferPath;
  void *m_streamHandle;
  void *m_filebufferReadHandle;
  void *m_filebufferWriteHandle;
  time_t m_start;
#ifndef TARGET_POSIX
  P8PLATFORM::CMutex m_mutex;
  uint64_t m_writePos;
#endif
};

#endif
