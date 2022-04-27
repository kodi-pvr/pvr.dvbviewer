/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "DvbData.h"
#include "StreamReader.h"
#include "TimeshiftBuffer.h"
#include "client.h"
#include "utilities/XMLUtils.h"

#include <kodi/General.h>
#include <kodi/Network.h>
#include <kodi/addon-instance/inputstream/TimingConstants.h>
#include <kodi/tools/StringUtils.h>

#include <tinyxml.h>
#include <inttypes.h>
#include <set>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <memory>
#include <ctime>

#if defined(TARGET_WINDOWS)
#include <windows.h>
#endif

using namespace dvbviewer;
using namespace dvbviewer::utilities;
using namespace kodi::tools;

template<typename T> void SafeDelete(T*& p)
{
  if (p)
  {
    delete p;
    p = nullptr;
  }
}

/* Copied from xbmc/URL.cpp */
std::string dvbviewer::URLEncode(const std::string& data)
{
  std::string result;

  /* wonder what a good value is here is, depends on how often it occurs */
  result.reserve(data.length() * 2);

  for (size_t i = 0; i < data.size(); ++i)
  {
    const char kar = data[i];

    // Don't URL encode "-_.!()" according to RFC1738
    // TODO: Update it to "-_.~" after Gotham according to RFC3986
    if (StringUtils::IsAsciiAlphaNum(kar) || kar == '-' || kar == '.'
        || kar == '_' || kar == '!' || kar == '(' || kar == ')')
      result.push_back(kar);
    else
      result += StringUtils::Format("%%%2.2X", (unsigned int)((unsigned char)kar));
  }

  return result;
}

std::time_t dvbviewer::ParseDateTime(const std::string& date, bool iso8601 = true)
{
  std::tm timeinfo;

  memset(&timeinfo, 0, sizeof(tm));
  if (iso8601)
    std::sscanf(date.c_str(), "%04d%02d%02d%02d%02d%02d", &timeinfo.tm_year,
        &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour,
        &timeinfo.tm_min, &timeinfo.tm_sec);
  else
    std::sscanf(date.c_str(), "%02d.%02d.%04d%02d:%02d:%02d", &timeinfo.tm_mday,
        &timeinfo.tm_mon, &timeinfo.tm_year, &timeinfo.tm_hour,
        &timeinfo.tm_min, &timeinfo.tm_sec);
  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  return std::mktime(&timeinfo);
}

std::tm dvbviewer::localtime(std::time_t tt)
{
  std::tm timeinfo;
#ifdef TARGET_POSIX
  localtime_r(&tt, &timeinfo);
#else
  localtime_s(&timeinfo, &tt);
#endif
  return timeinfo;
}

// XXX: not thread safe
long dvbviewer::UTCOffset()
{
  static long offset;
  static bool initialized = false;
  if (!initialized) {
#ifdef TARGET_POSIX
    std::tm t;
    tzset();
    std::time_t tt = std::time(nullptr);
    if (localtime_r(&tt, &t))
      offset = t.tm_gmtoff;
#else
    TIME_ZONE_INFORMATION tz;
    switch(GetTimeZoneInformation(&tz))
    {
      case TIME_ZONE_ID_DAYLIGHT:
        offset = (tz.Bias + tz.DaylightBias) * -60;
        break;
      case TIME_ZONE_ID_STANDARD:
        offset = (tz.Bias + tz.StandardBias) * -60;
        break;
      case TIME_ZONE_ID_UNKNOWN:
        offset = tz.Bias * -60;
        break;
    }
#endif
  }
  return offset;
}

void dvbviewer::RemoveNullChars(std::string& str)
{
  /* favourites.xml and timers.xml sometimes have null chars that screw the xml */
  str.erase(std::remove(str.begin(), str.end(), '\0'), str.end());
}

std::string dvbviewer::ConvertToUtf8(const std::string& src)
{
  std::string dest;
  kodi::UnknownToUTF8(src, dest);
  return dest;
}

Dvb::Dvb(const kodi::addon::IInstanceInfo& instance, const Settings &settings)
  : kodi::addon::CInstancePVRClient(instance), m_kvstore(*this), m_settings(settings)
{
  TiXmlBase::SetCondenseWhiteSpace(false);

  m_kvstore.OnError([this](const KVStore::Error err)
    {
      /* kvstore isn't mandatory so a queue error should be enough for now */
      if (err == KVStore::Error::RESPONSE_ERROR)
        kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30515));
      else if (err == KVStore::Error::GENERIC_PARSE_ERROR)
        kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30516));
    });

  m_running = true;
  m_thread = std::thread([&] { Process(); });
}

Dvb::~Dvb()
{
  m_running = false;
  if (m_thread.joinable())
    m_thread.join();

  for (auto channel : m_channels)
    delete channel;
}

bool Dvb::IsConnected()
{
  return m_state == PVR_CONNECTION_STATE_CONNECTED;
}

PVR_ERROR Dvb::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(!m_isguest);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetSupportsChannelSettings(false);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);
  capabilities.SetSupportsRecordingPlayCount(true);
  capabilities.SetSupportsLastPlayedPosition(true);
  capabilities.SetSupportsRecordingEdl(true);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);

  if (IsConnected())
  {
    if (IsGuest())
      capabilities.SetSupportsTimers(false);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetBackendName(std::string& name)
{
  name = (!IsConnected()) ? "not connected" : m_backendName;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetBackendVersion(std::string& version)
{
  version = StringUtils::Format("%u.%u.%u.%u", m_backendVersion >> 24 & 0xFF,
      m_backendVersion >> 16 & 0xFF, m_backendVersion >> 8  & 0xFF, m_backendVersion & 0xFF);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetBackendHostname(std::string& hostname)
{
  hostname = StringUtils::Format("%s:%u", m_settings.m_hostname.c_str(),
      m_settings.m_webPort);

  if (!IsConnected())
    hostname += " (Not connected!)";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetConnectionString(std::string& connection)
{
  connection = m_settings.m_hostname;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!UpdateBackendStatus())
    return PVR_ERROR_SERVER_ERROR;
  total = m_diskspace.total;
  used  = m_diskspace.used;
  return PVR_ERROR_NO_ERROR;
}

unsigned int Dvb::GetBackendVersion()
{
  return m_backendVersion;
}

/***************************************************************************
 * Channels
 **************************************************************************/
unsigned int Dvb::GetCurrentClientChannel(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_currentChannel;
}

PVR_ERROR Dvb::GetChannels(bool radio,
    kodi::addon::PVRChannelsResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  for (auto channel : m_channels)
  {
    if (channel->hidden)
      continue;
    if (channel->radio != radio)
      continue;

    kodi::addon::PVRChannel xbmcChannel;
    xbmcChannel.SetUniqueId(channel->id);
    xbmcChannel.SetIsRadio(channel->radio);
    xbmcChannel.SetChannelNumber(channel->frontendNr);
    xbmcChannel.SetEncryptionSystem(channel->encrypted);
    xbmcChannel.SetIsHidden(false);
    xbmcChannel.SetChannelName(channel->name);
    xbmcChannel.SetIconPath(channel->logo);

    results.Add(xbmcChannel);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetEPGForChannel(int channelUid, time_t start,
    time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  DvbChannel *channel = GetChannel(channelUid);

  std::unique_ptr<const httpResponse> res = GetFromAPI("api/epg.html?lvl=2&channel=%" PRIu64
      "&start=%f&end=%f", channel->epgId, start/86400.0 + DELPHI_DATE,
      end/86400.0 + DELPHI_DATE);
  if (res->error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return PVR_ERROR_SERVER_ERROR;
  }

  TiXmlDocument doc;
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse EPG. Error: %s",
        doc.ErrorDesc());
    return PVR_ERROR_FAILED;
  }

  unsigned int numEPG = 0;
  for (TiXmlElement *xEntry = doc.RootElement()->FirstChildElement("programme");
      xEntry; xEntry = xEntry->NextSiblingElement("programme"))
  {
    DvbEPGEntry entry;
    entry.channel = channel;
    entry.start   = ParseDateTime(xEntry->Attribute("start"));
    entry.end     = ParseDateTime(xEntry->Attribute("stop"));

    if (end > 1 && end < entry.end)
       continue;

    if (!XMLUtils::GetUInt(xEntry, "eventid", entry.id))
      continue;

    // since RS 1.26.0 the correct language is already merged into the elements
    TiXmlElement *xTitles = xEntry->FirstChildElement("titles");
    if (!xTitles || !XMLUtils::GetString(xTitles, "title", entry.title))
      continue;

    if (TiXmlElement *xDescriptions = xEntry->FirstChildElement("descriptions"))
      XMLUtils::GetString(xDescriptions, "description", entry.plot);

    if (TiXmlElement *xEvents = xEntry->FirstChildElement("events"))
    {
      XMLUtils::GetString(xEvents, "event", entry.plotOutline);
      if (entry.plot.empty())
      {
        entry.plot = entry.plotOutline;
        entry.plotOutline.clear();
      }
      else if (m_settings.m_prependOutline == PrependOutline::IN_EPG
          || m_settings.m_prependOutline == PrependOutline::ALWAYS)
      {
        entry.plot.insert(0, entry.plotOutline + "\n");
        entry.plotOutline.clear();
      }
    }

    XMLUtils::GetUInt(xEntry, "content", entry.genre);

    kodi::addon::PVREPGTag broadcast;
    broadcast.SetUniqueBroadcastId(entry.id);
    broadcast.SetTitle(entry.title);
    broadcast.SetUniqueChannelId(channelUid);
    broadcast.SetStartTime(entry.start);
    broadcast.SetEndTime(entry.end);
    broadcast.SetPlotOutline(entry.plotOutline);
    broadcast.SetPlot(entry.plot);
    broadcast.SetGenreType(entry.genre & 0xF0);
    broadcast.SetGenreSubType(entry.genre & 0x0F);
    broadcast.SetFlags(EPG_TAG_FLAG_UNDEFINED);
    broadcast.SetSeriesNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    broadcast.SetEpisodeNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    broadcast.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);

    results.Add(broadcast);
    ++numEPG;

    kodi::Log(ADDON_LOG_DEBUG, "%s: Loaded EPG entry '%u:%s': start=%u, end=%u",
        __FUNCTION__, entry.id, entry.title.c_str(),
        entry.start, entry.end);
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %u EPG entries for channel '%s'",
      numEPG, channel->name.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetChannelsAmount(int& amount)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  amount = m_channelAmount;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetSignalStatus(int channelUid,
    kodi::addon::PVRSignalStatus& signalStatus)
{
  // the RS api doesn't provide information about signal quality (yet)
  signalStatus.SetAdapterName("DVBViewer Media Server");
  signalStatus.SetAdapterStatus("OK");
  return PVR_ERROR_NO_ERROR;
}

/***************************************************************************
 * Channel groups
 **************************************************************************/
PVR_ERROR Dvb::GetChannelGroups(bool radio,
    kodi::addon::PVRChannelGroupsResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  for (auto &group : m_groups)
  {
    if (group.hidden)
      continue;
    if (group.radio != radio)
      continue;

    kodi::addon::PVRChannelGroup tag;
    tag.SetIsRadio(group.radio);
    tag.SetGroupName(group.name);

    results.Add(tag);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& pvrGroup,
    kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  unsigned int channelNumberInGroup = 1;

  for (auto &group : m_groups)
  {
    if (group.name != pvrGroup.GetGroupName())
      continue;

    for (auto channel : group.channels)
    {
      kodi::addon::PVRChannelGroupMember tag;

      tag.SetGroupName(pvrGroup.GetGroupName());
      tag.SetChannelUniqueId(channel->id);
      tag.SetChannelNumber(channelNumberInGroup++);

      results.Add(tag);

      kodi::Log(ADDON_LOG_DEBUG, "%s: Add channel '%s' (backendid=%" PRIu64 ") to group '%s'",
          __FUNCTION__, channel->name.c_str(), channel->backendIds.front(),
          group.name.c_str());
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetChannelGroupsAmount(int& amount)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  amount = m_groupAmount;

  return PVR_ERROR_NO_ERROR;
}

/***************************************************************************
 * Timers
 **************************************************************************/
PVR_ERROR Dvb::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  if (!IsConnected())
    return PVR_ERROR_NO_ERROR;

  std::vector< std::unique_ptr<kodi::addon::PVRTimerType> > timerTypes;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.GetTimerTypes(timerTypes);
  }

  for (auto &timer : timerTypes)
    types.push_back(*timer);

  kodi::Log(ADDON_LOG_DEBUG, "GetTimerTypes: transferred %u types", timerTypes.size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::vector<kodi::addon::PVRTimer> timers;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.GetAutoTimers(timers);
    m_timers.GetTimers(timers);
  }

  for (auto &timer : timers)
    results.Add(timer);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::AddTimer(const kodi::addon::PVRTimer& timer)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  kodi::Log(ADDON_LOG_DEBUG, "AddTimer: channel=%u, title='%s'",
      timer.GetClientChannelUid(), timer.GetTitle().c_str());
  std::lock_guard<std::mutex> lock(m_mutex);

  Timers::Error err = m_timers.AddUpdateTimer(timer, false);
  if (err != Timers::SUCCESS)
  {
    if (err == Timers::TIMESPAN_OVERFLOW)
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30510));
    else if (err == Timers::EMPTY_SEARCH_PHRASE)
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30513));
    else if (err == Timers::TIMER_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Timer %u is unknown", timer.GetClientIndex());
    else if (err == Timers::CHANNEL_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Channel is unknown");
    else if (err == Timers::RECFOLDER_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Recording folder is unknown");
    else
      kodi::Log(ADDON_LOG_ERROR, "Unexpected error while add/edit timer");
    return PVR_ERROR_FAILED;
  }
  // full timer sync here to get the backend specific properties
  m_updateTimers = true;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  kodi::Log(ADDON_LOG_DEBUG, "UpdateTimer: channel=%u, title='%s'",
      timer.GetClientChannelUid(), timer.GetTitle().c_str());
  std::lock_guard<std::mutex> lock(m_mutex);

  Timers::Error err = m_timers.AddUpdateTimer(timer, true);
  if (err != Timers::SUCCESS)
  {
    if (err == Timers::TIMESPAN_OVERFLOW)
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30510));
    else if (err == Timers::EMPTY_SEARCH_PHRASE)
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30513));
    else if (err == Timers::TIMER_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Timer %u is unknown", timer.GetClientIndex());
    else if (err == Timers::CHANNEL_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Channel is unknown");
    else if (err == Timers::RECFOLDER_UNKNOWN)
      kodi::Log(ADDON_LOG_ERROR, "Recording folder is unknown");
    else
      kodi::Log(ADDON_LOG_ERROR, "Unexpected error while add/edit timer");
    return PVR_ERROR_FAILED;
  }
  // full timer sync here to get the backend specific properties
  m_updateTimers = true;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::lock_guard<std::mutex> lock(m_mutex);
  Timers::Error err = m_timers.DeleteTimer(timer);
  if (err != Timers::SUCCESS)
    return PVR_ERROR_FAILED;

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetTimersAmount(int& amount)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::lock_guard<std::mutex> lock(m_mutex);
  amount = static_cast<int>(m_timers.GetTimerCount());
  return PVR_ERROR_NO_ERROR;
}

/***************************************************************************
 * Recordings
 **************************************************************************/
PVR_ERROR Dvb::GetRecordings(bool deleted,
      kodi::addon::PVRRecordingsResultSet& results)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::lock_guard<std::mutex> lock(m_mutex);
  std::unique_ptr<httpResponse> res = GetFromAPI("api/recordings.html?utf8=1&images=1");
  if (res->error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return PVR_ERROR_SERVER_ERROR;
  }

  TiXmlDocument doc;
  RemoveNullChars(res->content);
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse recordings. Error: %s",
        doc.ErrorDesc());
    return PVR_ERROR_FAILED;
  }

  TiXmlElement *root = doc.RootElement();

  // there's no need to merge new recordings in older ones as Kodi does this
  // already for us (using strRecordingId). so just parse all recordings again
  std::vector<DvbRecording> recordings;
  m_recordingAmount = 0;

  // group name and its size/amount of recordings
  std::map<std::string, unsigned int> groups;

  // insert recordings in reverse order
  for (TiXmlNode *xNode = root->LastChild("recording");
      xNode; xNode = xNode->PreviousSibling("recording"))
  {
    TiXmlElement *xRecording = xNode->ToElement();
    if (!xRecording)
      continue;

    DvbRecording recording;
    recording.id = xRecording->Attribute("id");
    xRecording->QueryUnsignedAttribute("content", &recording.genre);
    XMLUtils::GetString(xRecording, "title", recording.title);
    XMLUtils::GetString(xRecording, "info",  recording.plotOutline);
    XMLUtils::GetString(xRecording, "desc",  recording.plot);
    if (recording.plot.empty())
    {
      recording.plot = recording.plotOutline;
      recording.plotOutline.clear();
    }
    else if (m_settings.m_prependOutline == PrependOutline::IN_RECORDINGS
        || m_settings.m_prependOutline == PrependOutline::ALWAYS)
    {
      recording.plot.insert(0, recording.plotOutline + "\n");
      recording.plotOutline.clear();
    }

    /* fetch and search channel */
    XMLUtils::GetString(xRecording, "channel", recording.channelName);
    recording.channel = GetChannel([&](const DvbChannel *channel)
        {
          return (channel->backendName == recording.channelName);
        });
    if (recording.channel)
      recording.channelName = recording.channel->name;

    std::string thumbnail;
    if (!m_settings.m_lowPerformance
        && XMLUtils::GetString(xRecording, "image", thumbnail))
      recording.thumbnail = BuildURL("upnp/thumbnails/video/%s",
          thumbnail.c_str());

    std::string startTime = xRecording->Attribute("start");
    recording.start = ParseDateTime(startTime);

    int hours, mins, secs;
    std::sscanf(xRecording->Attribute("duration"), "%02d%02d%02d", &hours, &mins, &secs);
    recording.duration = hours*60*60 + mins*60 + secs;

    std::string group("Unknown");
    switch(m_settings.m_groupRecordings)
    {
      case RecordGrouping::BY_DIRECTORY:
        {
          std::string file;
          if (!XMLUtils::GetString(xRecording, "file", file))
            break;
          for (auto &recf : m_recfolders)
          {
            if (!StringUtils::StartsWithNoCase(file, recf))
              continue;
            group = file.substr(recf.length(), file.rfind('\\') - recf.length());
            StringUtils::Replace(group, '\\', '/');
            StringUtils::TrimLeft(group, "/");
            break;
          }
        }
        break;
      case RecordGrouping::BY_DATE:
        group = StringUtils::Format("%s/%s", startTime.substr(0, 4).c_str(),
            startTime.substr(4, 2).c_str());
        break;
      case RecordGrouping::BY_FIRST_LETTER:
        group = ::toupper(recording.title[0]);
        break;
      case RecordGrouping::BY_TV_CHANNEL:
        group = recording.channelName;
        break;
      case RecordGrouping::BY_SERIES:
        XMLUtils::GetString(xRecording, "series", group);
        break;
      case RecordGrouping::BY_TITLE:
        group = recording.title;
        break;
      default:
        group = "";
        break;
    }
    recording.group = groups.emplace(group, 0).first;
    ++recording.group->second;

    m_kvstore.Get<int>("recplaycount_" + recording.id,
      recording.playCount, KVStore::Hint::FETCH_ALL);
    m_kvstore.Get<int>("recplaypos_" + recording.id,
      recording.lastPlayPosition, KVStore::Hint::FETCH_ALL);

    recordings.push_back(recording);
  }

  for (auto &recording : recordings)
  {
    kodi::addon::PVRRecording recinfo;

    recinfo.SetRecordingId(recording.id);
    recinfo.SetTitle(recording.title);
    recinfo.SetPlotOutline(recording.plotOutline);
    recinfo.SetPlot(recording.plot);
    recinfo.SetChannelName(recording.channelName);
    recinfo.SetThumbnailPath(recording.thumbnail);
    recinfo.SetRecordingTime(recording.start);
    recinfo.SetDuration(recording.duration);
    recinfo.SetGenreType(recording.genre & 0xF0);
    recinfo.SetGenreSubType(recording.genre & 0x0F);
    recinfo.SetPlayCount(recording.playCount);
    recinfo.SetLastPlayedPosition(recording.lastPlayPosition);
    recinfo.SetChannelUid(PVR_CHANNEL_INVALID_UID);
    recinfo.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_UNKNOWN);
    recinfo.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
    recinfo.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);

    if (recording.channel)
    {
      recinfo.SetChannelUid(recording.channel->id);
      recinfo.SetChannelType(recording.channel->radio
          ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV);
    }

    // no grouping for single entry groups if by_title
    if (m_settings.m_groupRecordings != RecordGrouping::BY_TITLE
        || recording.group->second > 1)
      recinfo.SetDirectory(recording.group->first);

    results.Add(recinfo);
    ++m_recordingAmount;

    kodi::Log(ADDON_LOG_DEBUG, "%s: Loaded recording entry '%s': start=%u, length=%u",
        __FUNCTION__, recording.title.c_str(), recording.start,
        recording.duration);
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded %u recording entries", m_recordingAmount);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::unique_ptr<const httpResponse> res = GetFromAPI("api/recdelete.html?recid=%s&delfile=1",
      recording.GetRecordingId().c_str());
  if (res->error)
    return PVR_ERROR_FAILED;
  kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetRecordingsAmount(bool deleted, int& amount)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::lock_guard<std::mutex> lock(m_mutex);
  amount = m_recordingAmount;
  return PVR_ERROR_NO_ERROR;
}

bool Dvb::OpenRecordedStream(const kodi::addon::PVRRecording& recinfo)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_recReader)
    SafeDelete(m_recReader);

  std::string url;
  switch(m_settings.m_recordingTranscoding)
  {
    case Transcoding::TS:
      url = BuildURL("flashstream/stream.ts?recid=%s&%s",
        recinfo.GetRecordingId().c_str(), m_settings.m_recordingTranscodingParams.c_str());
      break;
    case Transcoding::WEBM:
      url = BuildURL("flashstream/stream.webm?recid=%s&%s",
        recinfo.GetRecordingId().c_str(), m_settings.m_recordingTranscodingParams.c_str());
      break;
    case Transcoding::FLV:
      url = BuildURL("flashstream/stream.flv?recid=%s&%s",
        recinfo.GetRecordingId().c_str(), m_settings.m_recordingTranscodingParams.c_str());
      break;
    default:
      url = BuildURL("upnp/recordings/%s.ts", recinfo.GetRecordingId().c_str());
      break;
  }

  std::pair<std::time_t, std::time_t> startEndTimes(0, 0);
  /* recording reopen only works in non-transcoding case */
  if (m_settings.m_recordingTranscoding == Transcoding::OFF)
  {
    std::time_t now = std::time(nullptr);
    const std::string channelName = recinfo.GetChannelName();
    auto timer = m_timers.GetTimer([&](const Timer &timer)
        {
          return timer.isRunning(&now, &channelName);
        });
    if (timer)
      startEndTimes = std::make_pair(timer->realStart, timer->end);
  }

  m_recReader = new RecordingReader(url, startEndTimes);
  return m_recReader->Start();
}

void Dvb::CloseRecordedStream()
{
  if (m_recReader)
    SafeDelete(m_recReader);
}

int Dvb::ReadRecordedStream(unsigned char* buffer, unsigned int size)
{
  if (!m_recReader)
    return 0;

  return static_cast<int>(m_recReader->ReadData(buffer, size));
}

int64_t Dvb::SeekRecordedStream(int64_t position, int whence)
{
  if (!m_recReader)
    return 0;

  return m_recReader->Seek(position, whence);
}

int64_t Dvb::LengthRecordedStream()
{
  if (!m_recReader)
    return -1;

  return m_recReader->Length();
}

PVR_ERROR Dvb::GetRecordingEdl(const kodi::addon::PVRRecording& recinfo,
    std::vector<kodi::addon::PVREDLEntry>& edl)
{
  if (!m_settings.m_edl.enabled)
    return PVR_ERROR_NO_ERROR;

  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  std::unique_ptr<httpResponse> res = OpenFromAPI("api/sideload.html?rec=1&file=.edl"
    "&fileid=%s", recinfo.GetRecordingId().c_str());
  if (res->error)
    return PVR_ERROR_NO_ERROR; // no EDL file found

  size_t lineNumber = 0;
  std::string buffer;
  while(res->file.ReadLine(buffer))
  {
    float start = 0.0f, stop = 0.0f;
    unsigned int type = PVR_EDL_TYPE_CUT;
    ++lineNumber;
    if (std::sscanf(buffer.c_str(), "%f %f %u", &start, &stop, &type) < 2
      || type > PVR_EDL_TYPE_COMBREAK)
    {
      kodi::Log(ADDON_LOG_INFO, "Unable to parse EDL entry at line %zu. Skipping.",
          lineNumber);
      continue;
    }

    start += m_settings.m_edl.padding_start / 1000.0f;
    stop  += m_settings.m_edl.padding_stop  / 1000.0f;

    start = std::max(start, 0.0f);
    stop  = std::max(stop,  0.0f);
    start = std::min(start, stop);
    stop  = std::max(start, stop);

    kodi::Log(ADDON_LOG_DEBUG, "edl line=%zu start=%f stop=%f type=%u", lineNumber,
        start, stop, type);

    kodi::addon::PVREDLEntry entry;
    entry.SetStart(static_cast<int64_t>(start * 1000.0f));
    entry.SetEnd(static_cast<int64_t>(stop  * 1000.0f));
    entry.SetType(static_cast<PVR_EDL_TYPE>(type));
    edl.emplace_back(entry);
  }

  res->file.Close();
  return PVR_ERROR_NO_ERROR;
}


PVR_ERROR Dvb::SetRecordingPlayCount(const kodi::addon::PVRRecording& recinfo,
    int count)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  const std::string value = std::string("recplaycount_") + recinfo.GetRecordingId();
  return m_kvstore.Set(value, count)
     ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Dvb::SetRecordingLastPlayedPosition(
    const kodi::addon::PVRRecording& recinfo, int lastplayedposition)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  const std::string value = std::string("recplaypos_") + recinfo.GetRecordingId();
  return m_kvstore.Set<int>(value, lastplayedposition)
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Dvb::GetRecordingLastPlayedPosition(
    const kodi::addon::PVRRecording& recinfo, int& position)
{
  if (!IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  const std::string value = std::string("recplaypos_") + recinfo.GetRecordingId();
  return m_kvstore.Get<int>(value, position)
   ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

/***************************************************************************
 * Livestream
 **************************************************************************/
bool Dvb::OpenLiveStream(const kodi::addon::PVRChannel& channelinfo)
{
  if (!IsConnected())
    return false;

  kodi::Log(ADDON_LOG_DEBUG, "%s: channel=%u", __FUNCTION__,
      channelinfo.GetUniqueId());
  std::lock_guard<std::mutex> lock(m_mutex);

  if (channelinfo.GetUniqueId() != m_currentChannel)
  {
    m_currentChannel = channelinfo.GetUniqueId();
    if (!m_settings.m_lowPerformance)
      m_updateEPG = true;
  }

  /* queue a warning if the timeshift buffer path does not exist */
  if (m_settings.m_timeshift != Timeshift::OFF
      && !m_settings.IsTimeshiftBufferPathValid())
    kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30514));

  std::string streamURL = GetLiveStreamURL(channelinfo);
  m_strReader = new dvbviewer::StreamReader(streamURL, m_settings);
  if (m_settings.m_timeshift == Timeshift::ON_PLAYBACK)
    m_strReader = new dvbviewer::TimeshiftBuffer(m_strReader, m_settings);
  return m_strReader->Start();
}

void Dvb::CloseLiveStream()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_currentChannel = 0;
  SafeDelete(m_strReader);
}

bool Dvb::IsRealTimeStream()
{
  return (m_strReader) ? m_strReader->IsRealTime() : false;
}

bool Dvb::CanPauseStream()
{
  if (m_settings.m_timeshift != Timeshift::OFF && m_strReader)
    return (m_strReader->IsTimeshifting() || m_settings.IsTimeshiftBufferPathValid());
  return false;
}

void Dvb::PauseStream(bool paused)
{
  /* start timeshift on pause */
  if (paused && m_settings.m_timeshift == Timeshift::ON_PAUSE
      && m_strReader && !m_strReader->IsTimeshifting()
      && m_settings.IsTimeshiftBufferPathValid())
  {
    m_strReader = new TimeshiftBuffer(m_strReader, m_settings);
    (void)m_strReader->Start();
  }
}

bool Dvb::CanSeekStream()
{
  // pause button seems to check CanSeekStream() too
  //return (m_strReader && m_strReader->IsTimeshifting());
  return (m_settings.m_timeshift != Timeshift::OFF);
}

int Dvb::ReadLiveStream(unsigned char* buffer, unsigned int size)
{
  return (m_strReader) ? static_cast<int>(m_strReader->ReadData(buffer, size)) : 0;
}

int64_t Dvb::SeekLiveStream(int64_t position, int whence)
{
  return (m_strReader) ? m_strReader->Seek(position, whence) : -1;
}

int64_t Dvb::LengthLiveStream()
{
  return (m_strReader) ? m_strReader->Length() : -1;
}

const std::string Dvb::GetLiveStreamURL(
    const kodi::addon::PVRChannel& channelinfo)
{
  DvbChannel *channel = GetChannel(channelinfo.GetUniqueId());
  uint64_t backendId = channel->backendIds.front();
  switch(m_settings.m_transcoding)
  {
    case Transcoding::TS:
      return BuildURL("flashstream/stream.ts?chid=%" PRIu64 "&%s",
        backendId, m_settings.m_transcodingParams.c_str());
      break;
    case Transcoding::WEBM:
      return BuildURL("flashstream/stream.webm?chid=%" PRIu64 "&%s",
        backendId, m_settings.m_transcodingParams.c_str());
      break;
    case Transcoding::FLV:
      return BuildURL("flashstream/stream.flv?chid=%" PRIu64 "&%s",
        backendId, m_settings.m_transcodingParams.c_str());
      break;
    default:
      break;
  }
  return BuildURL("upnp/channelstream/%" PRIu64 ".ts", backendId);
}

PVR_ERROR Dvb::GetStreamTimes(kodi::addon::PVRStreamTimes& times)
{
  int64_t timeStart, timeEnd;
  if (m_strReader)
  {
    timeStart = timeEnd = 0;
    if (m_strReader->IsTimeshifting())
    {
      timeStart = m_strReader->TimeStart();
      timeEnd   = m_strReader->TimeEnd();
    }
  }
  else if (m_recReader && m_recReader->TimeStart() > 0)
  {
    timeStart = m_recReader->TimeStart();
    timeEnd   = m_recReader->TimeRecorded();
  }
  else
    return PVR_ERROR_NOT_IMPLEMENTED;

  times.SetStartTime(timeStart);
  times.SetPTSStart(0);
  times.SetPTSBegin(0);
  times.SetPTSEnd((timeEnd - timeStart) * STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Dvb::GetStreamReadChunkSize(int& chunksize)
{
  int size = m_settings.m_streamReadChunkSize;
  if (!size)
    return PVR_ERROR_NOT_IMPLEMENTED;
  chunksize = m_settings.m_streamReadChunkSize * 1024;
  return PVR_ERROR_NO_ERROR;
}

/***************************************************************************
 * Internal
 **************************************************************************/
void Dvb::SleepMs(uint32_t ms)
{
  while (ms >= 100)
  {
    ms -= 100;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!m_running)
      break;
  }

  if (m_running && ms > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void Dvb::Process()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s: Running...", __FUNCTION__);
  int update = 0;
  int interval = (!m_settings.m_lowPerformance) ? 60 : 300;

  // set PVR_CONNECTION_STATE_CONNECTING only once!
  SetConnectionState(PVR_CONNECTION_STATE_CONNECTING);

  while (m_running)
  {
    if (!IsConnected())
    {
      if (m_settings.m_useWoL)
      {
        if (!kodi::network::WakeOnLan(m_settings.m_mac))
          kodi::Log(ADDON_LOG_ERROR, "Error sending WoL packet to %s",
              m_settings.m_mac.c_str());
      }

      kodi::Log(ADDON_LOG_INFO, "Trying to connect to the backend server...");

      if (CheckBackendVersion() && UpdateBackendStatus(true) && LoadChannels())
      {
        m_kvstore.Reset();

        kodi::Log(ADDON_LOG_INFO, "Connection to the backend server successful.");
        SetConnectionState(PVR_CONNECTION_STATE_CONNECTED);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          TimerUpdates();
        }
        // force recording sync as Kodi won't update recordings on PVR restart
        kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
      }
      else
      {
        kodi::Log(ADDON_LOG_INFO, "Connection to the backend server failed."
          " Retrying in 10 seconds...");
        SleepMs(10000);
      }
    }
    else
    {
      SleepMs(1000);
      ++update;

      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_updateEPG)
      {
        m_updateEPG = false;
        lock.unlock();
        SleepMs(8000); /* Sleep enough time to let the media server grab the EPG data */
        lock.lock();
        kodi::Log(ADDON_LOG_INFO, "Triggering EPG update on current channel!");
        kodi::addon::CInstancePVRClient::TriggerEpgUpdate(m_currentChannel);
      }

      if (m_updateTimers)
      {
        m_updateTimers = false;
        lock.unlock();
        SleepMs(1000);
        lock.lock();
        kodi::Log(ADDON_LOG_INFO, "Running forced timer updates!");
        TimerUpdates();
        update = 0;
      }

      if (update >= interval)
      {
        update = 0;
        kodi::Log(ADDON_LOG_INFO, "Running timer + recording updates!");
        TimerUpdates();
        kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();

        /* actually the DMS should do this itself... */
        m_kvstore.Save();
      }
    }
  }
}

std::unique_ptr<Dvb::httpResponse> Dvb::OpenFromAPI(const char* format, va_list args)
{
  static const std::string baseUrl = m_settings.BaseURL(false);
  std::string url = baseUrl + StringUtils::FormatV(format, args);

  std::unique_ptr<httpResponse> res = std::make_unique<httpResponse>();
  if (!res->file.CURLCreate(url))
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to create curl handle for %s", url.c_str());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  res->file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "user-agent", "Kodi PVR");
  res->file.CURLAddOption(ADDON_CURL_OPTION_HEADER, "Accept", "text/xml");
  if (!m_settings.m_username.empty() && !m_settings.m_password.empty())
    res->file.CURLAddOption(ADDON_CURL_OPTION_CREDENTIALS,
        m_settings.m_username, m_settings.m_password);

  /*
   * FIXME
   * CURLOpen fails on http!=2xy responses and the underlaying handle gets
   * deleted. So we can't parse the status line anymore.
   */
  if (!res->file.CURLOpen(ADDON_READ_NO_CACHE))
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to open url: %s", url.c_str());
    res->file.Close();
    return res;
  }

  std::string status = res->file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  if (status.empty())
  {
    kodi::Log(ADDON_LOG_ERROR, "Endpoint %s didn't return a status line.", url.c_str());
    res->file.Close();
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  std::istringstream ss(status);
  ss.ignore(10, ' ');
  ss >> res->code;
  if (!ss.good())
  {
    kodi::Log(ADDON_LOG_ERROR, "Endpoint %s returned an invalid status line: %s",
        url.c_str(), status.c_str());
    res->file.Close();
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  // everything non 2xx is an error
  // NOTE: this doesn't work for now. see above
  if (res->code >= 300)
  {
    kodi::Log(ADDON_LOG_INFO, "Endpoint %s returned non-successful status code %hu",
        url.c_str(), res->code);
    res->file.Close();
    return res;
  }

  res->error = false;
  return res;
}

std::unique_ptr<Dvb::httpResponse> Dvb::OpenFromAPI(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  std::unique_ptr<httpResponse> res = OpenFromAPI(format, args);
  va_end(args);
  return res;
}

std::unique_ptr<Dvb::httpResponse> Dvb::GetFromAPI(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  std::unique_ptr<httpResponse> res = OpenFromAPI(format, args);
  va_end(args);

  if (!res->error)
  {
    char buffer[1024];
    ssize_t bytesRead;
    while ((bytesRead = res->file.Read(buffer, sizeof(buffer))) > 0)
      res->content.append(buffer, bytesRead);
    res->file.Close();
  }
  return res;
}

bool Dvb::LoadChannels()
{
  std::unique_ptr<const httpResponse> res = GetFromAPI("api/getchannelsxml.html"
      "?fav=1&subchannels=1&logo=1");
  if (res->error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse channels. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
        kodi::addon::GetLocalizedString(30502).c_str());
    return false;
  }

  m_channels.clear(); //TODO: this leaks all channels
  m_channelAmount = 0;
  m_groups.clear();
  m_groupAmount = 0;

  TiXmlElement *root = doc.RootElement();
  if (!root->FirstChildElement("root"))
  {
    kodi::Log(ADDON_LOG_INFO, "Channel list is empty");
    return true; // empty channel is fine
  }

  // check if the first group contains favourites.
  // favourites have negative channel numbers
  bool hasFavourites = false;
  if (TiXmlElement *tmp = root->FirstChildElement("root"))
  {
    int channelNr = 0;
    hasFavourites = ((tmp = tmp->FirstChildElement("group"))
        && (tmp = tmp->FirstChildElement("channel"))
        && tmp->QueryIntAttribute("nr", &channelNr) == TIXML_SUCCESS
        && channelNr < 0);
  }

  // user wants to use remote favourites but doesn't have any defined
  if (m_settings.m_useFavourites && !m_settings.m_useFavouritesFile && !hasFavourites)
  {
    kodi::Log(ADDON_LOG_INFO, "Favourites enabled but non defined");
    kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30509));
    return false; // empty favourites is an error
  }

  TiXmlElement *xRoot = root->FirstChildElement("root");
  if (xRoot && hasFavourites) // skip favourites
    xRoot = xRoot->NextSiblingElement("root");
  for (; xRoot; xRoot = xRoot->NextSiblingElement("root"))
  {
    for (TiXmlElement *xGroup = xRoot->FirstChildElement("group");
        xGroup; xGroup = xGroup->NextSiblingElement("group"))
    {
      m_groups.push_back(DvbGroup());
      DvbGroup *group = &m_groups.back();
      group->name     = group->backendName = xGroup->Attribute("name");
      group->hidden   = m_settings.m_useFavourites;
      group->radio    = true;
      if (!group->hidden)
        ++m_groupAmount;

      for (TiXmlElement *xChannel = xGroup->FirstChildElement("channel");
          xChannel; xChannel = xChannel->NextSiblingElement("channel"))
      {
        DvbChannel *channel = new DvbChannel();
        unsigned int flags = 0;
        xChannel->QueryUnsignedAttribute("flags", &flags);
        channel->radio      = !(flags & VIDEO_FLAG);
        channel->encrypted  = (flags & ENCRYPTED_FLAG);
        channel->name       = channel->backendName = xChannel->Attribute("name");
        channel->hidden     = m_settings.m_useFavourites;
        channel->frontendNr = (!channel->hidden) ? m_channels.size() + 1 : 0;
        xChannel->QueryValueAttribute<uint64_t>("EPGID", &channel->epgId);

        uint64_t backendId = 0;
        xChannel->QueryValueAttribute<uint64_t>("ID", &backendId);
        channel->backendIds.push_back(backendId);

        std::string logo;
        if (!m_settings.m_lowPerformance
            && XMLUtils::GetString(xChannel, "logo", logo))
          channel->logo = BuildURL("%s", logo.c_str());

        for (TiXmlElement* xSubChannel = xChannel->FirstChildElement("subchannel");
            xSubChannel; xSubChannel = xSubChannel->NextSiblingElement("subchannel"))
        {
          uint64_t backendId = 0;
          xSubChannel->QueryValueAttribute<uint64_t>("ID", &backendId);
          channel->backendIds.push_back(backendId);
        }

        //FIXME: PVR_CHANNEL.UniqueId is uint32 but DVBViewer ids are uint64
        // so generate our own unique ids, at least for this session
        channel->id = m_channels.size() + 1;
        m_channels.push_back(channel);
        group->channels.push_back(channel);
        if (!channel->hidden)
          ++m_channelAmount;

        if (!channel->radio)
          group->radio = false;
      }
    }
  }

  auto searchChannelByBackendId = [&](uint64_t backendId) -> DvbChannel* {
    for (auto channel : m_channels)
    {
      bool found = (std::find(channel->backendIds.begin(),
            channel->backendIds.end(), backendId)
          != channel->backendIds.end());
      if (found)
        return channel;
    }
    return nullptr;
  };

  if (m_settings.m_useFavourites && !m_settings.m_useFavouritesFile)
  {
    m_groups.clear();
    m_groupAmount = 0;

    // the first group contains the favourites
    TiXmlElement *xRoot = root->FirstChildElement("root");
    for (TiXmlElement *xGroup = xRoot->FirstChildElement("group");
        xGroup; xGroup = xGroup->NextSiblingElement("group"))
    {
      m_groups.push_back(DvbGroup());
      DvbGroup *group = &m_groups.back();
      group->name     = group->backendName = xGroup->Attribute("name");
      group->hidden   = false;
      group->radio    = true;
      ++m_groupAmount;

      for (TiXmlElement *xChannel = xGroup->FirstChildElement("channel");
          xChannel; xChannel = xChannel->NextSiblingElement("channel"))
      {
        uint64_t backendId = 0;
        xChannel->QueryValueAttribute<uint64_t>("ID", &backendId);
        DvbChannel *channel = searchChannelByBackendId(backendId);
        if (!channel)
        {
          kodi::Log(ADDON_LOG_INFO, "Favourites contains unresolvable channel: %s."
              " Ignoring.", xChannel->Attribute("name"));
          kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30508),
              xChannel->Attribute("name"));
          continue;
        }

        channel->name = xChannel->Attribute("name");
        channel->hidden = false;
        channel->frontendNr = ++m_channelAmount;
        group->channels.push_back(channel);
        if (!channel->radio)
          group->radio = false;
      }
    }
  }
  else if (m_settings.m_useFavourites && m_settings.m_useFavouritesFile)
  {
    kodi::vfs::CFile fileHandle;
    if (!fileHandle.OpenFile(m_settings.m_favouritesFile))
    {
      kodi::Log(ADDON_LOG_ERROR, "Unable to open local favourites.xml");
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          kodi::addon::GetLocalizedString(30504).c_str());
      return false;
    }

    std::string content;
    char buffer[1024];
    ssize_t bytesRead;
    while ((bytesRead = fileHandle.Read(buffer, sizeof(buffer))) > 0)
      content.append(buffer, bytesRead);
    fileHandle.Close();

    TiXmlDocument doc;
    RemoveNullChars(content);
    doc.Parse(content.c_str());
    if (doc.Error())
    {
      kodi::Log(ADDON_LOG_ERROR, "Unable to parse favourites.xml. Error: %s",
          doc.ErrorDesc());
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          kodi::addon::GetLocalizedString(30505).c_str());
      return false;
    }

    m_groups.clear();
    m_groupAmount = 0;

    /* example data:
     * <settings>
     *    <section name="0">
     *      <entry name="Header">Group 1</entry>
     *      <entry name="0">1234567890123456789|Channel 1</entry>
     *      <entry name="1">1234567890123456789|Channel 2</entry>
     *    </section>
     *   <section name="1">
     *     <entry name="Header">1234567890123456789|Channel 3</entry>
     *    </section>
     *    ...
     *  </settings>
     */
    for (TiXmlElement *xSection = doc.RootElement()->FirstChildElement("section");
        xSection; xSection = xSection->NextSiblingElement("section"))
    {
      DvbGroup *group = NULL;
      for (TiXmlElement *xEntry = xSection->FirstChildElement("entry");
          xEntry; xEntry = xEntry->NextSiblingElement("entry"))
      {
        // name="Header" doesn't indicate a group alone. we must have at least
        // one additional child. see example above
        if (!group && std::string(xEntry->Attribute("name")) == "Header"
            && xEntry->NextSiblingElement("entry"))
        {
          m_groups.push_back(DvbGroup());
          group = &m_groups.back();
          group->name   = ConvertToUtf8(xEntry->GetText());
          group->hidden = false;
          group->radio  = false;
          ++m_groupAmount;
          continue;
        }

        uint64_t backendId = 0;
        std::istringstream ss(xEntry->GetText());
        ss >> backendId;
        if (!backendId)
          continue;

        std::string channelName;
        if (!ss.eof())
        {
          ss.ignore(1);
          getline(ss, channelName);
          channelName = ConvertToUtf8(channelName);
        }

        DvbChannel *channel = searchChannelByBackendId(backendId);
        if (!channel)
        {
          const char *descr = (channelName.empty()) ? xEntry->GetText()
            : channelName.c_str();
          kodi::Log(ADDON_LOG_INFO, "Favourites contains unresolvable channel: %s."
              " Ignoring.", descr);
          kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30508),
              descr);
          continue;
        }

        channel->hidden = false;
        channel->frontendNr = ++m_channelAmount;
        if (!channelName.empty())
          channel->name = channelName;

        if (group)
        {
          group->channels.push_back(channel);
          if (!channel->radio)
            group->radio = false;
        }
      }
    }

    // assign channel number to remaining channels
    unsigned int channelNumber = m_channelAmount;
    for (auto channel : m_channels)
    {
      if (!channel->frontendNr)
        channel->frontendNr = ++channelNumber;
    }
  }

  kodi::Log(ADDON_LOG_INFO, "Loaded (%u/%lu) channels in (%u/%lu) groups",
      m_channelAmount, m_channels.size(), m_groupAmount, m_groups.size());
  // force channel sync as stream urls may have changed (e.g. rstp on/off)
  kodi::addon::CInstancePVRClient::TriggerChannelUpdate();
  return true;
}

void Dvb::TimerUpdates()
{
  bool changes;
  // Locking handled by calling functions
  Timers::Error err = m_timers.RefreshAllTimers(changes);
  if (err != Timers::SUCCESS || !changes)
  {
    if (err == Timers::RESPONSE_ERROR)
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    else if (err == Timers::GENERIC_PARSE_ERROR)
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          kodi::addon::GetLocalizedString(30506).c_str());
    return;
  }
  kodi::Log(ADDON_LOG_INFO, "Changes in timerlist detected, triggering an update!");
  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
}

DvbChannel *Dvb::GetChannel(std::function<bool (const DvbChannel*)> func)
{
  for (auto channel : m_channels)
  {
    if (channel->hidden)
      continue;
    if (func(channel))
      return channel;
  }
  return nullptr;
}

bool Dvb::CheckBackendVersion()
{
  std::unique_ptr<const httpResponse> res = GetFromAPI("api/version.html");
  if (res->error || res->content.empty())
  {
    SetConnectionState((res->code == 401) ? PVR_CONNECTION_STATE_ACCESS_DENIED
      : PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to connect to the backend server. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }

  m_backendVersion = 0;
  kodi::Log(ADDON_LOG_INFO, "Checking backend version...");
  if (doc.RootElement()->QueryUnsignedAttribute("iver", &m_backendVersion)
      != TIXML_SUCCESS)
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse version");
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }
  kodi::Log(ADDON_LOG_INFO, "Version: %u / %u.%u.%u.%u", m_backendVersion,
    m_backendVersion >> 24 & 0xFF, m_backendVersion >> 16 & 0xFF,
    m_backendVersion >> 8  & 0xFF, m_backendVersion & 0xFF);

  if (m_backendVersion < DMS_MIN_VERSION_NUM)
  {
    kodi::Log(ADDON_LOG_ERROR, "DVBViewer Media Server version %s or higher required",
        DMS_MIN_VERSION_STR);
    SetConnectionState(PVR_CONNECTION_STATE_VERSION_MISMATCH,
      kodi::addon::GetLocalizedString(30501).c_str(), DMS_MIN_VERSION_STR);
    return false;
  }

  m_backendName = doc.RootElement()->GetText();
  return true;
}

bool Dvb::UpdateBackendStatus(bool updateSettings)
{
  std::unique_ptr<const httpResponse> res = GetFromAPI("api/status2.html");
  if (res->error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to get backend status. Error: %s",
        doc.ErrorDesc());
    return false;
  }

  if (updateSettings)
    m_recfolders.clear();

  // compute disk space. duplicates are detected by their identical values
  TiXmlElement *root = doc.RootElement();
  std::set< std::pair<long long, long long> > folders;
  m_diskspace.total = m_diskspace.used = 0;
  for (TiXmlElement *xFolder = root->FirstChildElement("recfolders")->FirstChildElement("folder");
      xFolder; xFolder = xFolder->NextSiblingElement("folder"))
  {
    long long size = 0, free = 0;
    xFolder->QueryValueAttribute<long long>("size", &size);
    xFolder->QueryValueAttribute<long long>("free", &free);

    if (folders.emplace(size, free).second)
    {
      m_diskspace.total += size / 1024;
      m_diskspace.used += (size - free) / 1024;
    }

    if (updateSettings)
    {
      std::string recf = xFolder->GetText();
      m_recfolders.emplace_back(recf);
    }
  }

  if (updateSettings)
  {
    std::string rights("");
    XMLUtils::GetString(root, "rights", rights);
    if ((m_isguest = (rights != "full")))
      kodi::Log(ADDON_LOG_INFO, "Only guest permissions available!");

    /* read some settings from backend */
    m_settings.ReadFromBackend(*this);
  }

  return true;
}

void Dvb::SetConnectionState(PVR_CONNECTION_STATE state,
    const char *message, ...)
{
  if (state != m_state)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Connection state change (%d -> %d)", m_state.load(), state);
    m_state = state;

    std::string tmp("");
    if (message)
    {
      va_list argList;
      va_start(argList, message);
      tmp = StringUtils::FormatV(message, argList);
      va_end(argList);
    }
    kodi::addon::CInstancePVRClient::ConnectionStateChange(m_settings.m_hostname, m_state, tmp);
  }
}

std::string Dvb::BuildURL(const char* path, ...)
{
  static const std::string baseUrl = m_settings.BaseURL();
  std::string url(baseUrl);
  va_list argList;
  va_start(argList, path);
  url += StringUtils::FormatV(path, argList);
  va_end(argList);
  return url;
}
