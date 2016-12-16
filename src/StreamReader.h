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
  bool IsValid() override;
  ssize_t ReadData(unsigned char *buffer, unsigned int size) override;
  int64_t Seek(long long position, int whence) override;
  int64_t Position() override;
  int64_t Length() override;
  time_t TimeStart() override;
  time_t TimeEnd() override;
  bool NearEnd() override;
  bool IsTimeshifting() override;

private:
  void *m_streamHandle;
  time_t m_start;
};

#endif
