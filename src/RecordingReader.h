#pragma once

#ifndef PVR_DVBVIEWER_RECORDINGREADER_H
#define PVR_DVBVIEWER_RECORDINGREADER_H

#include "libXBMC_addon.h"

class RecordingReader
{
public:
  RecordingReader(const std::string &streamURL, time_t end);
  ~RecordingReader(void);
  bool Start();
  ssize_t ReadData(unsigned char *buffer, unsigned int size);
  int64_t Seek(long long position, int whence);
  int64_t Position();
  int64_t Length();
  void OnPlay() { m_playback = true; }

private:
  std::string m_streamURL;
  void *m_readHandle;

  /*!< @brief end time of the recording in case this an ongoing recording */
  time_t m_end;
  time_t m_nextReopen;
  bool m_fastReopen;

  /*!< @brief indicates if ffmpeg playback has started
   * In case we reach the end we need to sleep until the next reopen. However
   * during start/seek/jump/resume ffmpeg seeks and reads till the end.
   */
  bool m_playback;
  uint64_t m_pos;
  uint64_t m_len;
};

#endif
