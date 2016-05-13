#pragma once

#ifndef PVR_DVBVIEWER_ISTREAMREADER_H
#define PVR_DVBVIEWER_ISTREAMREADER_H

#include "libXBMC_addon.h"

class IStreamReader
{
public:
  virtual ~IStreamReader(void) {};
  virtual bool IsValid() = 0;
  virtual ssize_t ReadData(unsigned char *buffer, unsigned int size) = 0;
  virtual int64_t Seek(long long position, int whence) = 0;
  virtual int64_t Position() = 0;
  virtual int64_t Length() = 0;
  virtual time_t TimeStart() = 0;
  virtual time_t TimeEnd() = 0;
  virtual bool NearEnd() = 0;
};

#endif
