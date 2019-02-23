#include "DvbData.h"
#include "client.h"
#include "LocalizedString.h"

#include "util/XMLUtils.h"
#include "p8-platform/util/StringUtils.h"

#include <tinyxml.h>
#include <inttypes.h>
#include <set>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <memory>
#include <ctime>

using namespace dvbviewer;
using namespace ADDON;
using namespace P8PLATFORM;

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
    if (StringUtils::isasciialphanum(kar) || kar == '-' || kar == '.'
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
  char *tmp = XBMC->UnknownToUTF8(src.c_str());
  std::string dest(tmp);
  XBMC->FreeString(tmp);
  return dest;
}

Dvb::Dvb(const Settings &settings)
  : m_kvstore(*this), m_settings(settings)
{
  TiXmlBase::SetCondenseWhiteSpace(false);

  m_kvstore.OnError([this](const KVStore::Error err)
    {
      /* kvstore isn't mandatory so a queue error should be enough for now */
      if (err == KVStore::Error::RESPONSE_ERROR)
        XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30515).c_str());
      else if (err == KVStore::Error::GENERIC_PARSE_ERROR)
        XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30516).c_str());
    });

  CreateThread();
}

Dvb::~Dvb()
{
  StopThread();

  for (auto channel : m_channels)
    delete channel;
}

bool Dvb::IsConnected()
{
  return m_state == PVR_CONNECTION_STATE_CONNECTED;
}

std::string Dvb::GetBackendName()
{
  return m_backendName;
}

unsigned int Dvb::GetBackendVersion()
{
  return m_backendVersion;
}

bool Dvb::GetDriveSpace(long long *total, long long *used)
{
  CLockObject lock(m_mutex);
  if (!UpdateBackendStatus())
    return false;
  *total = m_diskspace.total;
  *used  = m_diskspace.used;
  return true;
}

/***************************************************************************
 * Channels
 **************************************************************************/
unsigned int Dvb::GetCurrentClientChannel(void)
{
  CLockObject lock(m_mutex);
  return m_currentChannel;
}

bool Dvb::GetChannels(ADDON_HANDLE handle, bool radio)
{
  for (auto channel : m_channels)
  {
    if (channel->hidden)
      continue;
    if (channel->radio != radio)
      continue;

    PVR_CHANNEL xbmcChannel;
    memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));
    xbmcChannel.iUniqueId         = channel->id;
    xbmcChannel.bIsRadio          = channel->radio;
    xbmcChannel.iChannelNumber    = channel->frontendNr;
    xbmcChannel.iEncryptionSystem = channel->encrypted;
    xbmcChannel.bIsHidden         = false;
    PVR_STRCPY(xbmcChannel.strChannelName, channel->name.c_str());
    PVR_STRCPY(xbmcChannel.strIconPath,    channel->logo.c_str());

    PVR->TransferChannelEntry(handle, &xbmcChannel);
  }
  return true;
}

bool Dvb::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channelinfo,
    std::time_t start, std::time_t end)
{
  DvbChannel *channel = GetChannel(channelinfo.iUniqueId);

  const httpResponse &res = GetFromAPI("api/epg.html?lvl=2&channel=%" PRIu64
      "&start=%f&end=%f", channel->epgId, start/86400.0 + DELPHI_DATE,
      end/86400.0 + DELPHI_DATE);
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse EPG. Error: %s",
        doc.ErrorDesc());
    return false;
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

    EPG_TAG broadcast;
    memset(&broadcast, 0, sizeof(EPG_TAG));
    broadcast.iUniqueBroadcastId  = entry.id;
    broadcast.strTitle            = entry.title.c_str();
    broadcast.iUniqueChannelId    = channelinfo.iUniqueId;
    broadcast.startTime           = entry.start;
    broadcast.endTime             = entry.end;
    broadcast.strPlotOutline      = entry.plotOutline.c_str();
    broadcast.strPlot             = entry.plot.c_str();
    broadcast.iGenreType          = entry.genre & 0xF0;
    broadcast.iGenreSubType       = entry.genre & 0x0F;
    broadcast.iFlags              = EPG_TAG_FLAG_UNDEFINED;

    PVR->TransferEpgEntry(handle, &broadcast);
    ++numEPG;

    XBMC->Log(LOG_DEBUG, "%s: Loaded EPG entry '%u:%s': start=%u, end=%u",
        __FUNCTION__, entry.id, entry.title.c_str(),
        entry.start, entry.end);
  }

  XBMC->Log(LOG_INFO, "Loaded %u EPG entries for channel '%s'",
      numEPG, channel->name.c_str());
  return true;
}

unsigned int Dvb::GetChannelsAmount()
{
  return m_channelAmount;
}

/***************************************************************************
 * Channel groups
 **************************************************************************/
bool Dvb::GetChannelGroups(ADDON_HANDLE handle, bool radio)
{
  for (auto &group : m_groups)
  {
    if (group.hidden)
      continue;
    if (group.radio != radio)
      continue;

    PVR_CHANNEL_GROUP tag;
    memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP));
    tag.bIsRadio = group.radio;
    PVR_STRCPY(tag.strGroupName, group.name.c_str());

    PVR->TransferChannelGroup(handle, &tag);
  }
  return true;
}

bool Dvb::GetChannelGroupMembers(ADDON_HANDLE handle,
    const PVR_CHANNEL_GROUP &pvrGroup)
{
  unsigned int channelNumberInGroup = 1;

  for (auto &group : m_groups)
  {
    if (group.name != pvrGroup.strGroupName)
      continue;

    for (auto channel : group.channels)
    {
      PVR_CHANNEL_GROUP_MEMBER tag;
      memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));
      PVR_STRCPY(tag.strGroupName, pvrGroup.strGroupName);
      tag.iChannelUniqueId = channel->id;
      tag.iChannelNumber   = channelNumberInGroup++;

      PVR->TransferChannelGroupMember(handle, &tag);

      XBMC->Log(LOG_DEBUG, "%s: Add channel '%s' (backendid=%" PRIu64 ") to group '%s'",
          __FUNCTION__, channel->name.c_str(), channel->backendIds.front(),
          group.name.c_str());
    }
  }
  return true;
}

unsigned int Dvb::GetChannelGroupsAmount()
{
  return m_groupAmount;
}

/***************************************************************************
 * Timers
 **************************************************************************/
void Dvb::GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  std::vector< std::unique_ptr<PVR_TIMER_TYPE> > timerTypes;
  {
    CLockObject lock(m_mutex);
    m_timers.GetTimerTypes(timerTypes);
  }

  int i = 0;
  for (auto &timer : timerTypes)
    types[i++] = *timer;
  *size = static_cast<int>(timerTypes.size());
  XBMC->Log(LOG_DEBUG, "transfered %u timers", *size);
}

bool Dvb::GetTimers(ADDON_HANDLE handle)
{
  std::vector<PVR_TIMER> timers;
  {
    CLockObject lock(m_mutex);
    m_timers.GetAutoTimers(timers);
    m_timers.GetTimers(timers);
  }

  for (auto &timer : timers)
    PVR->TransferTimerEntry(handle, &timer);
  return true;
}

bool Dvb::AddTimer(const PVR_TIMER &timer, bool update)
{
  XBMC->Log(LOG_DEBUG, "%sTimer: channel=%u, title='%s'",
      (update) ? "Edit" : "Add", timer.iClientChannelUid, timer.strTitle);
  CLockObject lock(m_mutex);

  Timers::Error err = m_timers.AddUpdateTimer(timer, update);
  if (err != Timers::SUCCESS)
  {
    if (err == Timers::TIMESPAN_OVERFLOW)
      XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30510).c_str());
    else if (err == Timers::EMPTY_SEARCH_PHRASE)
      XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30513).c_str());
    else if (err == Timers::TIMER_UNKNOWN)
      XBMC->Log(LOG_ERROR, "Timer %u is unknown", timer.iClientIndex);
    else if (err == Timers::CHANNEL_UNKNOWN)
      XBMC->Log(LOG_ERROR, "Channel is unknown");
    else if (err == Timers::RECFOLDER_UNKNOWN)
      XBMC->Log(LOG_ERROR, "Recording folder is unknown");
    else
      XBMC->Log(LOG_ERROR, "Unexpected error while add/edit timer");
    return false;
  }
  // full timer sync here to get the backend specific properties
  m_updateTimers = true;
  return true;
}

bool Dvb::DeleteTimer(const PVR_TIMER &timer)
{
  CLockObject lock(m_mutex);
  Timers::Error err = m_timers.DeleteTimer(timer);
  if (err != Timers::SUCCESS)
    return false;

  PVR->TriggerTimerUpdate();
  return true;
}

unsigned int Dvb::GetTimersAmount()
{
  CLockObject lock(m_mutex);
  return static_cast<int>(m_timers.GetTimerCount());
}

/***************************************************************************
 * Recordings
 **************************************************************************/
bool Dvb::GetRecordings(ADDON_HANDLE handle)
{
  CLockObject lock(m_mutex);
  httpResponse &&res = GetFromAPI("api/recordings.html?utf8=1&images=1");
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  RemoveNullChars(res.content);
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse recordings. Error: %s",
        doc.ErrorDesc());
    return false;
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

    if (m_kvstore.IsSupported())
    {
      m_kvstore.Get<int>("recplaycount_" + recording.id,
        recording.playCount, KVStore::Hint::FETCH_ALL);
      m_kvstore.Get<int>("recplaypos_" + recording.id,
        recording.lastPlayPosition, KVStore::Hint::FETCH_ALL);
    }

    recordings.push_back(recording);
  }

  for (auto &recording : recordings)
  {
    PVR_RECORDING recinfo;
    memset(&recinfo, 0, sizeof(PVR_RECORDING));
    PVR_STRCPY(recinfo.strRecordingId,   recording.id.c_str());
    PVR_STRCPY(recinfo.strTitle,         recording.title.c_str());
    PVR_STRCPY(recinfo.strPlotOutline,   recording.plotOutline.c_str());
    PVR_STRCPY(recinfo.strPlot,          recording.plot.c_str());
    PVR_STRCPY(recinfo.strChannelName,   recording.channelName.c_str());
    PVR_STRCPY(recinfo.strThumbnailPath, recording.thumbnail.c_str());
    recinfo.recordingTime       = recording.start;
    recinfo.iDuration           = recording.duration;
    recinfo.iGenreType          = recording.genre & 0xF0;
    recinfo.iGenreSubType       = recording.genre & 0x0F;
    recinfo.iPlayCount          = recording.playCount;
    recinfo.iLastPlayedPosition = recording.lastPlayPosition;
    recinfo.iChannelUid         = PVR_CHANNEL_INVALID_UID;
    recinfo.channelType         = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

    if (recording.channel)
    {
      recinfo.iChannelUid = recording.channel->id;
      recinfo.channelType = (recording.channel->radio)
          ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV;
    }

    // no grouping for single entry groups if by_title
    if (m_settings.m_groupRecordings != RecordGrouping::BY_TITLE
        || recording.group->second > 1)
      PVR_STRCPY(recinfo.strDirectory, recording.group->first.c_str());

    PVR->TransferRecordingEntry(handle, &recinfo);
    ++m_recordingAmount;

    XBMC->Log(LOG_DEBUG, "%s: Loaded recording entry '%s': start=%u, length=%u",
        __FUNCTION__, recording.title.c_str(), recording.start,
        recording.duration);
  }

  XBMC->Log(LOG_INFO, "Loaded %u recording entries", m_recordingAmount);
  return true;
}

bool Dvb::DeleteRecording(const PVR_RECORDING &recinfo)
{
  if (m_isguest)
  {
    XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30512).c_str());
    return false;
  }

  const httpResponse &res = GetFromAPI("api/recdelete.html?recid=%s&delfile=1",
      recinfo.strRecordingId);
  if (res.error)
    return false;
  PVR->TriggerRecordingUpdate();
  return true;
}

unsigned int Dvb::GetRecordingsAmount()
{
  CLockObject lock(m_mutex);
  return m_recordingAmount;
}

dvbviewer::RecordingReader *Dvb::OpenRecordedStream(const PVR_RECORDING &recinfo)
{
  CLockObject lock(m_mutex);

  std::string url;
  switch(m_settings.m_recordingTranscoding)
  {
    case Transcoding::TS:
      url = BuildURL("flashstream/stream.ts?recid=%s&%s",
        recinfo.strRecordingId, m_settings.m_recordingTranscodingParams.c_str());
      break;
    case Transcoding::WEBM:
      url = BuildURL("flashstream/stream.webm?recid=%s&%s",
        recinfo.strRecordingId, m_settings.m_recordingTranscodingParams.c_str());
      break;
    case Transcoding::FLV:
      url = BuildURL("flashstream/stream.flv?recid=%s&%s",
        recinfo.strRecordingId, m_settings.m_recordingTranscodingParams.c_str());
      break;
    default:
      url = BuildURL("upnp/recordings/%s.ts", recinfo.strRecordingId);
      break;
  }

  std::pair<std::time_t, std::time_t> startEndTimes(0, 0);
  /* recording reopen only works in non-transcoding case */
  if (m_settings.m_recordingTranscoding == Transcoding::OFF)
  {
    std::time_t now = std::time(nullptr);
    const std::string channelName = recinfo.strChannelName;
    auto timer = m_timers.GetTimer([&](const Timer &timer)
        {
          return timer.isRunning(&now, &channelName);
        });
    if (timer)
      startEndTimes = std::make_pair(timer->realStart, timer->end);
  }

  return new RecordingReader(url, startEndTimes);
}

bool Dvb::GetRecordingEdl(const PVR_RECORDING &recinfo, PVR_EDL_ENTRY edl[],
    int *size)
{
  int maxSize = *size;
  *size = 0;

  if (m_backendVersion < DMS_VERSION_NUM(2, 1, 0, 0))
  {
    XBMC->Log(LOG_ERROR, "Backend server is too old. Disabling EDL support.");
    XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30511).c_str(),
      DMS_VERSION_STR(2, 1, 0, 0));
    m_settings.m_edl.enabled = false;
    return false;
  }

  const httpResponse &res = OpenFromAPI("api/sideload.html?rec=1&file=.edl"
    "&fileid=%s", recinfo.strRecordingId);
  if (res.error)
    return true; // no EDL file found

  int idx = 0;
  size_t lineNumber = 0;
  char buffer[2048];
  while(XBMC->ReadFileString(res.file, buffer, 2048))
  {
    if (idx >= maxSize)
      break;

    float start = 0.0f, stop = 0.0f;
    unsigned int type = PVR_EDL_TYPE_CUT;
    ++lineNumber;
    if (std::sscanf(buffer, "%f %f %u", &start, &stop, &type) < 2
      || type > PVR_EDL_TYPE_COMBREAK)
    {
      XBMC->Log(LOG_NOTICE, "Unable to parse EDL entry at line %zu. Skipping.",
          lineNumber);
      continue;
    }

    start += m_settings.m_edl.padding_start / 1000.0f;
    stop  += m_settings.m_edl.padding_stop  / 1000.0f;

    start = std::max(start, 0.0f);
    stop  = std::max(stop,  0.0f);
    start = std::min(start, stop);
    stop  = std::max(start, stop);

    XBMC->Log(LOG_DEBUG, "edl line=%zu start=%f stop=%f type=%u", lineNumber,
        start, stop, type);

    edl[idx].start = static_cast<int64_t>(start * 1000.0f);
    edl[idx].end   = static_cast<int64_t>(stop  * 1000.0f);
    edl[idx].type  = static_cast<PVR_EDL_TYPE>(type);
    ++idx;
  }

  *size = idx;
  XBMC->CloseFile(res.file);
  return true;
}

bool Dvb::SetRecordingPlayCount(const PVR_RECORDING &recinfo, int count)
{
  const std::string value = std::string("recplaycount_") + recinfo.strRecordingId;
  return m_kvstore.Set(value, count);
}

int Dvb::GetRecordingLastPlayedPosition(const PVR_RECORDING &recinfo)
{
  const std::string value = std::string("recplaypos_") + recinfo.strRecordingId;
  int pos;
  return m_kvstore.Get<int>(value, pos) ? pos : -1;
}

bool Dvb::SetRecordingLastPlayedPosition(const PVR_RECORDING &recinfo, int pos)
{
  const std::string value = std::string("recplaypos_") + recinfo.strRecordingId;
  return m_kvstore.Set<int>(value, pos);
}

/***************************************************************************
 * Livestream
 **************************************************************************/
bool Dvb::OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  XBMC->Log(LOG_DEBUG, "%s: channel=%u", __FUNCTION__, channelinfo.iUniqueId);
  CLockObject lock(m_mutex);

  if (channelinfo.iUniqueId != m_currentChannel)
  {
    m_currentChannel = channelinfo.iUniqueId;
    if (!m_settings.m_lowPerformance)
      m_updateEPG = true;
  }
  return true;
}

void Dvb::CloseLiveStream(void)
{
  CLockObject lock(m_mutex);
  m_currentChannel = 0;
}

const std::string Dvb::GetLiveStreamURL(const PVR_CHANNEL &channelinfo)
{
  DvbChannel *channel = GetChannel(channelinfo.iUniqueId);
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

/***************************************************************************
 * Internal
 **************************************************************************/
void *Dvb::Process()
{
  XBMC->Log(LOG_DEBUG, "%s: Running...", __FUNCTION__);
  int update = 0;
  int interval = (!m_settings.m_lowPerformance) ? 60 : 300;

  // set PVR_CONNECTION_STATE_CONNECTING only once!
  SetConnectionState(PVR_CONNECTION_STATE_CONNECTING);

  while (!IsStopped())
  {
    if (!IsConnected())
    {
      if (m_settings.m_useWoL)
      {
        if (!XBMC->WakeOnLan(m_settings.m_mac.c_str()))
          XBMC->Log(LOG_ERROR, "Error sending WoL packet to %s",
              m_settings.m_mac.c_str());
      }

      XBMC->Log(LOG_INFO, "Trying to connect to the backend server...");

      if (CheckBackendVersion() && UpdateBackendStatus(true) && LoadChannels())
      {
        m_kvstore.Reset();

        XBMC->Log(LOG_INFO, "Connection to the backend server successful.");
        SetConnectionState(PVR_CONNECTION_STATE_CONNECTED);

        TimerUpdates();
        // force recording sync as Kodi won't update recordings on PVR restart
        PVR->TriggerRecordingUpdate();
      }
      else
      {
        XBMC->Log(LOG_INFO, "Connection to the backend server failed."
          " Retrying in 10 seconds...");
        Sleep(10000);
      }
    }
    else
    {
      Sleep(1000);
      ++update;

      CLockObject lock(m_mutex);
      if (m_updateEPG)
      {
        m_updateEPG = false;
        m_mutex.Unlock();
        Sleep(8000); /* Sleep enough time to let the media server grab the EPG data */
        m_mutex.Lock();
        XBMC->Log(LOG_INFO, "Triggering EPG update on current channel!");
        PVR->TriggerEpgUpdate(m_currentChannel);
      }

      if (m_updateTimers)
      {
        m_updateTimers = false;
        m_mutex.Unlock();
        Sleep(1000);
        m_mutex.Lock();
        XBMC->Log(LOG_INFO, "Running forced timer updates!");
        TimerUpdates();
        update = 0;
      }

      if (update >= interval)
      {
        update = 0;
        XBMC->Log(LOG_INFO, "Running timer + recording updates!");
        TimerUpdates();
        PVR->TriggerRecordingUpdate();

        /* actually the DMS should do this itself... */
        if (m_kvstore.IsSupported())
          m_kvstore.Save();
      }
    }
  }

  return nullptr;
}

Dvb::httpResponse Dvb::OpenFromAPI(const char* format, va_list args)
{
  static const std::string baseUrl = m_settings.BaseURL(false);
  std::string url = baseUrl + StringUtils::FormatV(format, args);

  httpResponse res = { nullptr, true, 0, "" };
  void *file = XBMC->CURLCreate(url.c_str());
  if (!file)
  {
    XBMC->Log(LOG_ERROR, "Unable to create curl handle for %s", url.c_str());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  XBMC->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "user-agent", "Kodi PVR");
  XBMC->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, "Accept", "text/xml");
  if (!m_settings.m_username.empty() && !m_settings.m_password.empty())
    XBMC->CURLAddOption(file, XFILE::CURL_OPTION_CREDENTIALS,
        m_settings.m_username.c_str(), m_settings.m_password.c_str());

  /*
   * FIXME
   * CURLOpen fails on http!=2xy responses and the underlaying handle gets
   * deleted. So we can't parse the status line anymore.
   */
  if (!XBMC->CURLOpen(file, XFILE::READ_NO_CACHE))
  {
    XBMC->Log(LOG_ERROR, "Unable to open url: %s", url.c_str());
    XBMC->CloseFile(file);
    return res;
  }

  char *status = XBMC->GetFilePropertyValue(file,
    XFILE::FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  if (!status)
  {
    XBMC->Log(LOG_ERROR, "Endpoint %s didn't return a status line.", url.c_str());
    XBMC->CloseFile(file);
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  std::istringstream ss(status);
  ss.ignore(10, ' ');
  ss >> res.code;
  if (!ss.good())
  {
    XBMC->Log(LOG_ERROR, "Endpoint %s returned an invalid status line: ",
        url.c_str(), status);
    XBMC->CloseFile(file);
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  // everything non 2xx is an error
  // NOTE: this doesn't work for now. see above
  if (res.code >= 300)
  {
    XBMC->Log(LOG_NOTICE, "Endpoint %s returned non-successful status code %hu",
        url.c_str(), res.code);
    XBMC->CloseFile(file);
    return res;
  }

  res.file  = file;
  res.error = false;
  return res;
}

Dvb::httpResponse Dvb::OpenFromAPI(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  httpResponse &&res = OpenFromAPI(format, args);
  va_end(args);
  return res;
}

Dvb::httpResponse Dvb::GetFromAPI(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  httpResponse &&res = OpenFromAPI(format, args);
  va_end(args);

  if (res.file)
  {
    char buffer[1024];
    while (ssize_t bytesRead = XBMC->ReadFile(res.file, buffer, 1024))
      res.content.append(buffer, bytesRead);
    XBMC->CloseFile(res.file);
    res.file = nullptr;
  }
  return res;
}

bool Dvb::LoadChannels()
{
  const httpResponse &res = GetFromAPI("api/getchannelsxml.html"
      "?fav=1&subchannels=1&logo=1");
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse channels. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
        LocalizedString(30502).c_str());
    return false;
  }

  m_channels.clear(); //TODO: this leaks all channels
  m_channelAmount = 0;
  m_groups.clear();
  m_groupAmount = 0;

  TiXmlElement *root = doc.RootElement();
  if (!root->FirstChildElement("root"))
  {
    XBMC->Log(LOG_NOTICE, "Channel list is empty");
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
    XBMC->Log(LOG_NOTICE, "Favourites enabled but non defined");
    XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30509).c_str());
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
        DvbChannel *channel = GetChannel([&](const DvbChannel *channel)
            {
              return (std::find(channel->backendIds.begin(),
                    channel->backendIds.end(), backendId)
                  != channel->backendIds.end());
            });
        if (!channel)
        {
          XBMC->Log(LOG_NOTICE, "Favourites contains unresolvable channel: %s."
              " Ignoring.", xChannel->Attribute("name"));
          XBMC->QueueNotification(QUEUE_WARNING, LocalizedString(30508).c_str(),
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
    void *fileHandle = XBMC->OpenFile(m_settings.m_favouritesFile.c_str(), 0);
    if (!fileHandle)
    {
      XBMC->Log(LOG_ERROR, "Unable to open local favourites.xml");
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          LocalizedString(30504).c_str());
      return false;
    }

    std::string content;
    char buffer[1024];
    while (ssize_t bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
      content.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);

    TiXmlDocument doc;
    RemoveNullChars(content);
    doc.Parse(content.c_str());
    if (doc.Error())
    {
      XBMC->Log(LOG_ERROR, "Unable to parse favourites.xml. Error: %s",
          doc.ErrorDesc());
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          LocalizedString(30505).c_str());
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

        DvbChannel *channel = GetChannel([&](const DvbChannel *channel)
            {
              return (std::find(channel->backendIds.begin(),
                    channel->backendIds.end(), backendId)
                  != channel->backendIds.end());
            });
        if (!channel)
        {
          const char *descr = (channelName.empty()) ? xEntry->GetText()
            : channelName.c_str();
          XBMC->Log(LOG_NOTICE, "Favourites contains unresolvable channel: %s."
              " Ignoring.", descr);
          XBMC->QueueNotification(QUEUE_WARNING, LocalizedString(30508).c_str(),
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

  XBMC->Log(LOG_INFO, "Loaded (%u/%lu) channels in (%u/%lu) groups",
      m_channelAmount, m_channels.size(), m_groupAmount, m_groups.size());
  // force channel sync as stream urls may have changed (e.g. rstp on/off)
  PVR->TriggerChannelUpdate();
  return true;
}

void Dvb::TimerUpdates()
{
  bool changes;
  CLockObject lock(m_mutex);
  Timers::Error err = m_timers.RefreshAllTimers(changes);
  if (err != Timers::SUCCESS || !changes)
  {
    if (err == Timers::RESPONSE_ERROR)
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    else if (err == Timers::GENERIC_PARSE_ERROR)
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          LocalizedString(30506).c_str());
    return;
  }
  XBMC->Log(LOG_INFO, "Changes in timerlist detected, triggering an update!");
  PVR->TriggerTimerUpdate();
}

DvbChannel *Dvb::GetChannel(std::function<bool (const DvbChannel*)> func)
{
  for (auto channel : m_channels)
  {
    if (func(channel))
      return channel;
  }
  return nullptr;
}

bool Dvb::CheckBackendVersion()
{
  const httpResponse &res = GetFromAPI("api/version.html");
  if (res.error)
  {
    SetConnectionState((res.code == 401) ? PVR_CONNECTION_STATE_ACCESS_DENIED
      : PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to connect to the backend server. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }

  m_backendVersion = 0;
  XBMC->Log(LOG_NOTICE, "Checking backend version...");
  if (doc.RootElement()->QueryUnsignedAttribute("iver", &m_backendVersion)
      != TIXML_SUCCESS)
  {
    XBMC->Log(LOG_ERROR, "Unable to parse version");
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }
  XBMC->Log(LOG_NOTICE, "Version: %u / %u.%u.%u.%u", m_backendVersion,
    m_backendVersion >> 24 & 0xFF, m_backendVersion >> 16 & 0xFF,
    m_backendVersion >> 8  & 0xFF, m_backendVersion & 0xFF);

  if (m_backendVersion < DMS_MIN_VERSION_NUM)
  {
    XBMC->Log(LOG_ERROR, "DVBViewer Media Server version %s or higher required",
        DMS_MIN_VERSION_STR);
    SetConnectionState(PVR_CONNECTION_STATE_VERSION_MISMATCH,
      LocalizedString(30501).c_str(), DMS_MIN_VERSION_STR);
    return false;
  }

  m_backendName = doc.RootElement()->GetText();
  return true;
}

bool Dvb::UpdateBackendStatus(bool updateSettings)
{
  const httpResponse &res = GetFromAPI("api/status2.html");
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to get backend status. Error: %s",
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
      XBMC->Log(LOG_NOTICE, "Only guest permissions available!");

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
    XBMC->Log(LOG_DEBUG, "Connection state change (%d -> %d)", m_state, state);
    m_state = state;

    std::string tmp;
    if (message)
    {
      va_list argList;
      va_start(argList, message);
      tmp = StringUtils::FormatV(message, argList);
      message = tmp.c_str();
      va_end(argList);
    }
    PVR->ConnectionStateChange(m_settings.m_hostname.c_str(), m_state, message);
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
