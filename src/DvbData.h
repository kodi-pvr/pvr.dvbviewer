#pragma once

#ifndef PVR_DVBVIEWER_DVBDATA_H
#define PVR_DVBVIEWER_DVBDATA_H

#include "RecordingReader.h"
#include "libXBMC_pvr.h"
#include "p8-platform/threads/threads.h"
#include <list>
#include <functional>

#define CHANNELDAT_HEADER_SIZE       (7)
#define ENCRYPTED_FLAG               (1 << 0)
#define RDS_DATA_FLAG                (1 << 2)
#define VIDEO_FLAG                   (1 << 3)
#define AUDIO_FLAG                   (1 << 4)
#define ADDITIONAL_AUDIO_TRACK_FLAG  (1 << 7)
#define DAY_SECS                     (24 * 60 * 60)
#define DELPHI_DATE                  (25569)

// minimum version required
#define RS_VERSION_MAJOR   1
#define RS_VERSION_MINOR   30
#define RS_VERSION_PATCH1  1
#define RS_VERSION_PATCH2  0
#define RS_VERSION_NUM  (RS_VERSION_MAJOR << 24 | RS_VERSION_MINOR << 16 | \
                          RS_VERSION_PATCH1 << 8 | RS_VERSION_PATCH2)
#define RS_VERSION_STR  XSTR(RS_VERSION_MAJOR) "." XSTR(RS_VERSION_MINOR) \
                          "." XSTR(RS_VERSION_PATCH1) "." XSTR(RS_VERSION_PATCH2)

/* forward declaration */
class DvbGroup;

class DvbChannel
{
public:
  DvbChannel()
    : backendNr(0), epgId(0)
  {}

public:
  /*!< @brief unique id passed to kodi's database.
   * starts at 1 and increases by each channel regardless of hidden state.
   * see FIXME for more details
   */
  unsigned int id;
  /*!< @brief backend number for generating the stream url */
  unsigned int backendNr;
  /*!< @brief channel number on the frontend */
  unsigned int frontendNr;
  /*!< @brief list of backend ids (e.g AC3, other languages, ...) */
  std::list<uint64_t> backendIds;
  uint64_t epgId;
  std::string name;
  /*!< @brief name of the channel on the backend */
  std::string backendName;
  std::string streamURL;
  std::string logoURL;
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
  time_t start;
  time_t end;
  unsigned int genre;
  std::string plotOutline;
  std::string plot;
};

class DvbTimer
{
public:
  enum class State
    : uint8_t
  {
    NONE,
    NEW,
    FOUND,
    UPDATED
  };

  DvbTimer()
    : updateState(State::NEW)
  {}

#define TIMER_UPDATE_MEMBER(member) \
  if (member != source.member) \
  { \
    member = source.member; \
    updated = true; \
   }

  bool updateFrom(const DvbTimer &source)
  {
    bool updated = false;
    TIMER_UPDATE_MEMBER(channel);
    TIMER_UPDATE_MEMBER(title);
    TIMER_UPDATE_MEMBER(start);
    TIMER_UPDATE_MEMBER(end);
    TIMER_UPDATE_MEMBER(priority);
    TIMER_UPDATE_MEMBER(weekdays);
    TIMER_UPDATE_MEMBER(state);
    return updated;
  }

public:
  /*!< @brief unique id passed to kodi's database
   * starts at 1 and increases by each new timer. never decreases.
   */
  unsigned int id;
  /*!< @brief unique guid provided by backend. unique every time */
  std::string guid;
  /*!< @brief timer id on backend. unique at a time */
  unsigned int backendId;

  DvbChannel *channel;
  std::string title;
  uint64_t channelId;
  time_t start;
  time_t end;
  int priority;
  unsigned int weekdays;
  PVR_TIMER_STATE state;
  State updateState;
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
    BY_SERIES
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
  std::string plot;
  std::string plotOutline;
  std::string thumbnailPath;
  /*!< @brief channel name provided by the backend */
  std::string channelName;
  /*!< @brief channel in case our search was successful */
  DvbChannel *channel;
};

typedef std::vector<DvbChannel *> DvbChannels_t;
typedef std::vector<DvbGroup> DvbGroups_t;
typedef std::vector<DvbTimer> DvbTimers_t;

class Dvb
  : public P8PLATFORM::CThread
{
public:
  Dvb(void);
  ~Dvb();

  bool Open();
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

  bool GetTimers(ADDON_HANDLE handle);
  bool AddTimer(const PVR_TIMER &timer, bool update = false);
  bool DeleteTimer(const PVR_TIMER &timer);
  unsigned int GetTimersAmount(void);

  bool GetRecordings(ADDON_HANDLE handle);
  bool DeleteRecording(const PVR_RECORDING &recinfo);
  unsigned int GetRecordingsAmount();
  RecordingReader *OpenRecordedStream(const PVR_RECORDING &recinfo);

  bool OpenLiveStream(const PVR_CHANNEL &channelinfo);
  void CloseLiveStream();
  const std::string &GetLiveStreamURL(const PVR_CHANNEL &channelinfo);

protected:
  virtual void *Process(void);

private:
  // functions
  std::string GetHttpXML(const std::string& url);
  std::string URLEncode(const std::string& data);
  bool LoadChannels();
  DvbTimers_t LoadTimers();
  void TimerUpdates();
  DvbChannel *GetChannel(std::function<bool (const DvbChannel*)> func);
  DvbTimer *GetTimer(std::function<bool (const DvbTimer&)> func);

  // helper functions
  void RemoveNullChars(std::string& str);
  bool CheckBackendVersion();
  bool UpdateBackendStatus(bool updateSettings = false);
  time_t ParseDateTime(const std::string& strDate, bool iso8601 = true);
  std::string BuildURL(const char* path, ...);
  std::string BuildExtURL(const std::string& baseURL, const char* path, ...);
  std::string ConvertToUtf8(const std::string& src);
  long GetGMTOffset();

private:
  bool m_connected;
  unsigned int m_backendVersion;
  std::string m_url;
  std::string m_recordingURL;

  long m_timezone;
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

  DvbTimers_t m_timers;
  unsigned int m_nextTimerId;

  P8PLATFORM::CMutex m_mutex;
  P8PLATFORM::CCondition<bool> m_started;
};

#endif
