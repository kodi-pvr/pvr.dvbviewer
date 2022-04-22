/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "IStreamReader.h"
#include "KVStore.h"
#include "RecordingReader.h"
#include "Settings.h"
#include "Timers.h"

#include <kodi/addon-instance/PVR.h>

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// minimum version required
#define DMS_MIN_VERSION 3, 0, 0, 0

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

namespace dvbviewer ATTR_DLL_LOCAL
{
  std::string URLEncode(const std::string& data);
  std::time_t ParseDateTime(const std::string& date, bool iso8601);
  std::tm localtime(std::time_t tt = std::time(nullptr));
  long UTCOffset();
  void RemoveNullChars(std::string& str);
  std::string ConvertToUtf8(const std::string& src);
}

namespace dvbviewer ATTR_DLL_LOCAL
{
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
  int playCount = 0;
  int lastPlayPosition = 0;
};

typedef std::vector<DvbChannel *> DvbChannels_t;
typedef std::vector<DvbGroup> DvbGroups_t;

class Dvb
  : public kodi::addon::CInstancePVRClient
{
public:
  Dvb(const kodi::addon::IInstanceInfo& instance,
      const dvbviewer::Settings &settings);
  ~Dvb();

  bool IsConnected();

  PVR_ERROR GetCapabilities(
      kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;

  unsigned int GetBackendVersion();
  bool IsGuest()
  { return m_isguest; }
  dvbviewer::Settings &GetSettings()
  { return m_settings; };

  unsigned int GetCurrentClientChannel(void);
  PVR_ERROR GetChannels(bool radio,
      kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end,
      kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetSignalStatus(int channelUid,
      kodi::addon::PVRSignalStatus& signalStatus) override;

  PVR_ERROR GetChannelGroups(bool radio,
      kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
      kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;

  PVR_ERROR GetTimerTypes(
      std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer,
      bool forceDelete) override;
  PVR_ERROR GetTimersAmount(int& amount) override;

  PVR_ERROR GetRecordings(bool deleted,
      kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(
      const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recinfo) override;
  void CloseRecordedStream() override;
  int ReadRecordedStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekRecordedStream(int64_t position, int whence) override;
  int64_t LengthRecordedStream() override;
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recinfo,
        std::vector<kodi::addon::PVREDLEntry>& edl) override;
  PVR_ERROR SetRecordingPlayCount(const kodi::addon::PVRRecording& recinfo,
        int count) override;
  PVR_ERROR SetRecordingLastPlayedPosition(
        const kodi::addon::PVRRecording& recinfo,
        int lastplayedposition) override;
  PVR_ERROR GetRecordingLastPlayedPosition(
        const kodi::addon::PVRRecording& recinfo,
        int& position) override;

  bool OpenLiveStream(const kodi::addon::PVRChannel& channelinfo) override;
  void CloseLiveStream() override;
  bool IsRealTimeStream() override;
  bool CanPauseStream() override;
  bool CanSeekStream() override;
  void PauseStream(bool paused) override;
  int ReadLiveStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekLiveStream(int64_t position, int whence) override;
  int64_t LengthLiveStream() override;
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;


  PVR_ERROR GetStreamReadChunkSize(int& chunksize) override;

  DvbChannel *GetChannel(unsigned int id)
  { return (--id < m_channels.size()) ? m_channels[id] : nullptr; };
  DvbChannel *GetChannel(std::function<bool (const DvbChannel*)> func);
  const std::vector<std::string>& GetRecordingFolders()
  { return m_recfolders; };

  struct httpResponse {
      kodi::vfs::CFile file;
      bool error = true;
      unsigned short code = 0;
      std::string content;
  };
  std::unique_ptr<httpResponse> OpenFromAPI(const char* format, va_list args);
  std::unique_ptr<httpResponse> OpenFromAPI(const char* format, ...);
  std::unique_ptr<httpResponse> GetFromAPI(const char* format, ...);

protected:
  void Process();

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
  const std::string GetLiveStreamURL(
      const kodi::addon::PVRChannel& channelinfo);
  void SleepMs(uint32_t ms);

private:
  std::atomic<PVR_CONNECTION_STATE> m_state = { PVR_CONNECTION_STATE_UNKNOWN };
  std::string m_backendName = "";
  uint32_t m_backendVersion = 0;
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

  IStreamReader* m_strReader = nullptr;
  RecordingReader* m_recReader = nullptr;

  Timers m_timers = Timers(*this);
  KVStore m_kvstore;
  Settings m_settings;

  std::atomic<bool> m_running = {false};
  std::thread m_thread;
  std::mutex m_mutex;
};

} // namespace dvbviewer
