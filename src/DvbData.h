#pragma once

#ifndef PVR_DVBVIEWER_DVBDATA_H
#define PVR_DVBVIEWER_DVBDATA_H

#include "RecordingReader.h"
#include "Timers.h"

#include "libXBMC_pvr.h"
#include "p8-platform/threads/threads.h"
#include <list>
#include <map>
#include <functional>

// minimum version required
#define DMS_MIN_VERSION_MAJOR   1
#define DMS_MIN_VERSION_MINOR   33
#define DMS_MIN_VERSION_PATCH1  2
#define DMS_MIN_VERSION_PATCH2  0

#define ENCRYPTED_FLAG               (1 << 0)
#define RDS_DATA_FLAG                (1 << 2)
#define VIDEO_FLAG                   (1 << 3)
#define AUDIO_FLAG                   (1 << 4)
#define ADDITIONAL_AUDIO_TRACK_FLAG  (1 << 7)
#define DAY_SECS                     (24 * 60 * 60)
#define DELPHI_DATE                  (25569)

#define DMS_VERSION_NUM(a, b, c, d) (a << 24 | b << 16 | c << 8 | d)
#define DMS_MIN_VERSION_NUM  DMS_VERSION_NUM(DMS_MIN_VERSION_MAJOR,  \
                                             DMS_MIN_VERSION_MINOR,  \
                                             DMS_MIN_VERSION_PATCH1, \
                                             DMS_MIN_VERSION_PATCH2)
#define DMS_MIN_VERSION_STR  STR(DMS_MIN_VERSION_MAJOR)  "." \
                             STR(DMS_MIN_VERSION_MINOR)  "." \
                             STR(DMS_MIN_VERSION_PATCH1) "." \
                             STR(DMS_MIN_VERSION_PATCH2)

namespace dvbviewer
{
  std::string URLEncode(const std::string& data);
  time_t ParseDateTime(const std::string& date, bool iso8601);
  long UTCOffset();
  void RemoveNullChars(std::string& str);
  std::string ConvertToUtf8(const std::string& src);
}; // namespace dvbviewer

/* forward declaration */
class DvbGroup;

class DvbChannel
{
public:
  DvbChannel()
    : epgId(0)
  {}

public:
  /*!< @brief unique id passed to Kodi as PVR_CHANNEL.iUniqueId.
   * starts at 1 and increases by each channel regardless of hidden state.
   * see FIXME for more details
   */
  unsigned int id;
  /*!< @brief channel number on the frontend */
  unsigned int frontendNr;
  /*!< @brief list of backend ids (e.g AC3, other languages, ...).
   * the first entry is used for generating the stream url
   */
  std::list<uint64_t> backendIds;
  uint64_t epgId;
  std::string name;
  /*!< @brief name of the channel on the backend */
  std::string backendName;
  std::string logo;
  bool radio;
  bool hidden;
  bool encrypted;
};

class DvbGroup
{
public:
  std::string name;
  /*!< @brief name of the channel on the backend */
  std::string backendName;
  std::list<DvbChannel *> channels;
  bool radio;
  bool hidden;
};

class DvbEPGEntry
{
public:
  DvbEPGEntry()
    : genre(0)
  {}

public:
  unsigned int id;
  DvbChannel *channel;
  std::string title;
  time_t start, end;
  unsigned int genre;
  std::string plot, plotOutline;
};

class DvbRecording
{
public:
  enum class Grouping
    : int // same type as addon settings
  {
    DISABLED = 0,
    BY_DIRECTORY,
    BY_DATE,
    BY_FIRST_LETTER,
    BY_TV_CHANNEL,
    BY_SERIES,
    BY_TITLE
  };

public:
  DvbRecording()
    : genre(0)
  {}

public:
  std::string id;
  time_t start;
  int duration;
  unsigned int genre;
  std::string title;
  std::string plot, plotOutline;
  std::string thumbnail;
  /*!< @brief channel name provided by the backend */
  std::string channelName;
  /*!< @brief channel in case our search was successful */
  DvbChannel *channel;
  /*!< @brief group name and its size/amount of recordings */
  std::map<std::string, unsigned int>::iterator group;
};

typedef std::vector<DvbChannel *> DvbChannels_t;
typedef std::vector<DvbGroup> DvbGroups_t;

class Dvb
  : public P8PLATFORM::CThread
{
public:
  Dvb(void);
  ~Dvb();

  bool IsConnected();

  std::string GetBackendName();
  std::string GetBackendVersion();
  bool GetDriveSpace(long long *total, long long *used);

  unsigned int GetCurrentClientChannel(void);
  bool GetChannels(ADDON_HANDLE handle, bool radio);
  bool GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channelinfo,
      time_t start, time_t end);
  unsigned int GetChannelsAmount(void);

  bool GetChannelGroups(ADDON_HANDLE handle, bool radio);
  bool GetChannelGroupMembers(ADDON_HANDLE handle,
      const PVR_CHANNEL_GROUP &group);
  unsigned int GetChannelGroupsAmount(void);

  void GetTimerTypes(PVR_TIMER_TYPE types[], int *size);
  bool GetTimers(ADDON_HANDLE handle);
  bool AddTimer(const PVR_TIMER &timer, bool update = false);
  bool DeleteTimer(const PVR_TIMER &timer);
  unsigned int GetTimersAmount(void);

  bool GetRecordings(ADDON_HANDLE handle);
  bool DeleteRecording(const PVR_RECORDING &recinfo);
  bool GetRecordingEdl(PVR_RECORDING const& recording, PVR_EDL_ENTRY edl[], int* count);
  unsigned int GetRecordingsAmount();
  RecordingReader *OpenRecordedStream(const PVR_RECORDING &recinfo);

  bool OpenLiveStream(const PVR_CHANNEL &channelinfo);
  void CloseLiveStream();
  const std::string GetLiveStreamURL(const PVR_CHANNEL &channelinfo);

  DvbChannel *GetChannel(unsigned int id)
  { return (--id < m_channels.size()) ? m_channels[id] : nullptr; };
  DvbChannel *GetChannel(std::function<bool (const DvbChannel*)> func);
  const std::vector<std::string>& GetRecordingFolders()
  { return m_recfolders; };

  struct httpResponse { bool error; unsigned short code; std::string content; };
  httpResponse GetFromAPI(const char* format, ...);

protected:
  virtual void *Process(void) override;

private:
  // functions
  bool LoadChannels();
  void TimerUpdates();

  // helper functions
  bool CheckBackendVersion();
  bool UpdateBackendStatus(bool updateSettings = false);
  void SetConnectionState(PVR_CONNECTION_STATE state,
      const char *message = nullptr, ...);
  std::string BuildURL(const char* path, ...);

private:
  PVR_CONNECTION_STATE m_state;
  unsigned int m_backendVersion;

  struct { long long total, used; } m_diskspace;
  std::vector<std::string> m_recfolders;

  /* channels */
  DvbChannels_t m_channels;
  /* active (not hidden) channels */
  unsigned int m_channelAmount;
  unsigned int m_currentChannel;

  /* channel groups */
  DvbGroups_t m_groups;
  /* active (not hidden) groups */
  unsigned int m_groupAmount;

  bool m_updateTimers;
  bool m_updateEPG;
  unsigned int m_recordingAmount;

  dvbviewer::Timers m_timers;

  P8PLATFORM::CMutex m_mutex;
};

#endif
