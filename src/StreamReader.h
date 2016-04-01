#pragma once

#ifndef PVR_DVBVIEWER_STREAMREADER_H
#define PVR_DVBVIEWER_STREAMREADER_H

#include "IStreamReader.h"

class StreamReader
  : public IStreamReader
{
public:
  StreamReader(const std::string &streamURL);
  ~StreamReader(void);
  bool IsValid();
  ssize_t ReadData(unsigned char *buffer, unsigned int size);
  int64_t Seek(long long position, int whence);
  int64_t Position();
  int64_t Length();
  time_t TimeStart();
  time_t TimeEnd();
  bool NearEnd();

private:
  void *m_streamHandle;
  time_t m_start;
};

#endif
