#pragma once

#include "RecordingReader.h"
#include "Settings.h"
#include "Timers.h"

#include "libXBMC_pvr.h"
#include "p8-platform/threads/threads.h"

#include <list>
#include <map>
#include <functional>

// minimum version required
#define DMS_MIN_VERSION 1, 33, 2, 0

#define DMS_MIN_VERSION_NUM DMS_VERSION_NUM2(DMS_MIN_VERSION)
#define DMS_MIN_VERSION_STR DMS_VERSION_STR2(DMS_MIN_VERSION)

#define MSVC_EXPAND(x) x
#define DMS_VERSION_NUM2(...) MSVC_EXPAND(DMS_VERSION_NUM(__VA_ARGS__))
#define DMS_VERSION_STR2(...) MSVC_EXPAND(DMS_VERSION_STR(__VA_ARGS__))
#define DMS_VERSION_NUM(a, b, c, d) (a << 24 | b << 16 | c << 8 | d)
#define DMS_VERSION_STR(a, b, c, d) STR(a) "." STR(b) "." STR(c) "." STR(d)

#define ENCRYPTED_FLAG               (1 << 0)
#define RDS_DATA_FLAG                (1 << 2)
#define VIDEO_FLAG                   (1 << 3)
#define AUDIO_FLAG                   (1 << 4)
#define ADDITIONAL_AUDIO_TRACK_FLAG  (1 << 7)
#define DAY_SECS                     (24 * 60 * 60)
#define DELPHI_DATE                  (25569)

#define DMS_GUID_KVSTORE            "b3c542c3-34fa-482f-919e-251a3f4cb23b"

namespace dvbviewer
{
  std::string URLEncode(const std::string& data);
  std::time_t ParseDateTime(const std::string& date, bool iso8601);
  std::tm localtime(std::time_t tt = std::time(nullptr));
  long UTCOffset();
  void RemoveNullChars(std::string& str);
  std::string ConvertToUtf8(const std::string& src);
}; // namespace dvbviewer

/* forward declaration */
class DvbGroup;

class DvbChannel
{
public:
  DvbChannel() = default;

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
  uint64_t epgId = 0;
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
  DvbEPGEntry() = default;

public:
  unsigned int id;
  DvbChannel *channel;
  std::string title;
  std::time_t start, end;
  unsigned int genre = 0;
  std::string plot, plotOutline;
};

class DvbRecording
{
public:
  DvbRecording() = default;

public:
  std::string id;
  std::time_t start;
  int duration;
  unsigned int genre = 0;
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
  Dvb(const dvbviewer::Settings &settings);
  ~Dvb();

  bool IsConnected();

  std::string GetBackendName();
  unsigned int GetBackendVersion();
  bool GetDriveSpace(long long *total, long long *used);
  bool IsGuest()
  { return m_isguest; }
  dvbviewer::Settings &GetSettings()
  { return m_settings; };

  unsigned int GetCurrentClientChannel(void);
  bool GetChannels(ADDON_HANDLE handle, bool radio);
  bool GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channelinfo,
      std::time_t start, std::time_t end);
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
  unsigned int GetRecordingsAmount();
  RecordingReader *OpenRecordedStream(const PVR_RECORDING &recinfo);
  bool GetRecordingEdl(const PVR_RECORDING &recinfo, PVR_EDL_ENTRY edl[],
      int *size);
  bool SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition);
  int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording);

  bool OpenLiveStream(const PVR_CHANNEL &channelinfo);
  void CloseLiveStream();
  const std::string GetLiveStreamURL(const PVR_CHANNEL &channelinfo);

  DvbChannel *GetChannel(unsigned int id)
  { return (--id < m_channels.size()) ? m_channels[id] : nullptr; };
  DvbChannel *GetChannel(std::function<bool (const DvbChannel*)> func);
  const std::vector<std::string>& GetRecordingFolders()
  { return m_recfolders; };

  struct httpResponse {
      void *file;
      bool error;
      unsigned short code;
      std::string content;
  };
  httpResponse OpenFromAPI(const char* format, va_list args);
  httpResponse OpenFromAPI(const char* format, ...);
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
  PVR_CONNECTION_STATE m_state = PVR_CONNECTION_STATE_UNKNOWN;
  std::string m_backendName = "";
  unsigned int m_backendVersion = 0;
  bool m_isguest = false;

  struct { long long total, used; } m_diskspace;
  std::vector<std::string> m_recfolders;

  /* channels */
  DvbChannels_t m_channels;
  /* active (not hidden) channels */
  unsigned int m_channelAmount = 0;
  unsigned int m_currentChannel = 0;

  /* channel groups */
  DvbGroups_t m_groups;
  /* active (not hidden) groups */
  unsigned int m_groupAmount = 0;

  bool m_updateTimers = false;
  bool m_updateEPG = false;
  unsigned int m_recordingAmount = 0;

  dvbviewer::Timers m_timers = dvbviewer::Timers(*this);
  dvbviewer::Settings m_settings;

  P8PLATFORM::CMutex m_mutex;
};
