#include "DvbData.h"
#include "client.h"
#include "util/XMLUtils.h"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"

#include <tinyxml.h>
#include <inttypes.h>
#include <set>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <memory>

using namespace ADDON;
using namespace P8PLATFORM;

/* private copy until https://github.com/xbmc/kodi-platform/pull/2 get merged */
static bool XMLUtils_GetString(const TiXmlNode* pRootNode, const char* strTag,
    std::string& strStringValue)
{
  const TiXmlElement* pElement = pRootNode->FirstChildElement(strTag);
  if (!pElement)
    return false;
  const TiXmlNode* pNode = pElement->FirstChild();
  if (pNode != NULL)
  {
    strStringValue = pNode->ValueStr();
    return true;
  }
  strStringValue.clear();
  return true;
}

Dvb::Dvb()
  : m_state(PVR_CONNECTION_STATE_UNKNOWN), m_backendVersion(0), m_currentChannel(0),
  m_nextTimerId(1)
{
  TiXmlBase::SetCondenseWhiteSpace(false);

  m_updateTimers = false;
  m_updateEPG    = false;
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
  // RS api doesn't provide a reliable way to extract the server name
  return "DVBViewer";
}

std::string Dvb::GetBackendVersion()
{
  std::string version = StringUtils::Format("%u.%u.%u.%u",
      m_backendVersion >> 24 & 0xFF,
      m_backendVersion >> 16 & 0xFF,
      m_backendVersion >> 8 & 0xFF,
      m_backendVersion & 0xFF);
  return version;
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
    time_t start, time_t end)
{
  DvbChannel *channel = m_channels[channelinfo.iUniqueId - 1];

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
    TiXmlNode *xTitles = xEntry->FirstChild("titles");
    if (!xTitles || !XMLUtils_GetString(xTitles, "title", entry.title))
      continue;

    TiXmlNode *xDescriptions = xEntry->FirstChild("descriptions");
    if (xDescriptions)
      XMLUtils_GetString(xDescriptions, "description", entry.plot);

    TiXmlNode *xEvents = xEntry->FirstChild("events");
    if (xEvents)
    {
      XMLUtils_GetString(xEvents, "event", entry.plotOutline);
      if (entry.plot.empty())
      {
        entry.plot = entry.plotOutline;
        entry.plotOutline.clear();
      }
      else if (g_prependOutline == PrependOutline::IN_EPG
          || g_prependOutline == PrependOutline::ALWAYS)
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
bool Dvb::GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  struct TimerType
    : PVR_TIMER_TYPE
  {
    TimerType(unsigned int id, unsigned int attributes,
      const std::string &description = std::string(),
      const std::vector< std::pair<int, std::string> > &priorityValues
        = std::vector< std::pair<int, std::string> >(),
      const std::vector< std::pair<int, std::string> > &groupValues
        = std::vector< std::pair<int, std::string> >())
    {
      int i;
      memset(this, 0, sizeof(PVR_TIMER_TYPE));

      iId         = id;
      iAttributes = attributes;
      PVR_STRCPY(strDescription, description.c_str());
      //TODO: add support for deDup + CheckRecTitle, CheckRecSubtitle

      if ((iPrioritiesSize = priorityValues.size()))
        iPrioritiesDefault = priorityValues[0].first;
      i = 0;
      for (auto &priority : priorityValues)
      {
        priorities[i].iValue = priority.first;
        PVR_STRCPY(priorities[i].strDescription, priority.second.c_str());
        ++i;
      }

      if ((iRecordingGroupSize = groupValues.size()))
        iRecordingGroupDefault = groupValues[0].first;
      i = 0;
      for (auto &group : groupValues)
      {
        recordingGroup[i].iValue = group.first;
        PVR_STRCPY(recordingGroup[i].strDescription, group.second.c_str());
        ++i;
      }
    }
  };

  /* PVR_Timer.iPriority values and presentation.*/
  static std::vector< std::pair<int, std::string> > priorityValues = {
    { -1,  XBMC->GetLocalizedString(30400) }, //default
    { 0,   XBMC->GetLocalizedString(30401) },
    { 25,  XBMC->GetLocalizedString(30402) },
    { 50,  XBMC->GetLocalizedString(30403) },
    { 75,  XBMC->GetLocalizedString(30404) },
    { 100, XBMC->GetLocalizedString(30405) },
  };

  /* PVR_Timer.iRecordingGroup values and presentation.*/
  std::vector< std::pair<int, std::string> > groupValues = {
    { 0, XBMC->GetLocalizedString(30410) }, //automatic
  };
  for (auto &recf : m_recfolders)
    groupValues.emplace_back(groupValues.size(), recf);

  std::vector< std::unique_ptr<TimerType> > timerTypes;

  //TODO: use std::make_unique with C++14
  timerTypes.emplace_back(
    /* One-shot manual (time and channel based) */
    std::unique_ptr<TimerType>(new TimerType(
      DvbTimer::Type::MANUAL_ONCE,
      PVR_TIMER_TYPE_IS_MANUAL                 |
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP,
      "", /* Let Kodi generate the description */
      priorityValues, groupValues)));

  timerTypes.emplace_back(
    /* Repeating manual (time and channel based) */
    std::unique_ptr<TimerType>(new TimerType(
      DvbTimer::Type::MANUAL_REPEATING,
      PVR_TIMER_TYPE_IS_MANUAL                 |
      PVR_TIMER_TYPE_IS_REPEATING              |
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP,
      "", /* Let Kodi generate the description */
      priorityValues, groupValues)));


  int i = 0;
  for (auto &timer : timerTypes)
    types[i++] = *timer;
  *size = timerTypes.size();
  XBMC->Log(LOG_DEBUG, "transfered %u timers", *size);
  return true;
}

bool Dvb::GetTimers(ADDON_HANDLE handle)
{
  CLockObject lock(m_mutex);
  for (auto &timer : m_timers)
  {
    PVR_TIMER tag;
    memset(&tag, 0, sizeof(PVR_TIMER));

    PVR_STRCPY(tag.strTitle, timer.title.c_str());
    tag.iClientIndex      = timer.id;
    tag.iClientChannelUid = timer.channel->id;
    tag.startTime         = timer.start;
    tag.endTime           = timer.end;
    tag.iMarginStart      = timer.pre;
    tag.iMarginEnd        = timer.post;
    tag.state             = timer.state;
    tag.iTimerType        = timer.type;
    tag.iPriority         = timer.priority;
    tag.iRecordingGroup   = timer.recfolder + 1; /* first entry is automatic */
    tag.firstDay          = (timer.weekdays != 0) ? timer.start : 0;
    tag.iWeekdays         = timer.weekdays;

    PVR->TransferTimerEntry(handle, &tag);
  }
  return true;
}

bool Dvb::AddTimer(const PVR_TIMER &timer, bool update)
{
  XBMC->Log(LOG_DEBUG, "%s: channel=%u, title='%s'",
      __FUNCTION__, timer.iClientChannelUid, timer.strTitle);
  CLockObject lock(m_mutex);

  // DMS API requires starttime/endtime to include the margins
  unsigned int pre = timer.iMarginStart, post = timer.iMarginEnd;
  time_t startTime = (timer.startTime) ? timer.startTime - pre*60 : time(nullptr);
  time_t endTime   = timer.endTime + post*60;
  if (endTime - startTime >= DAY_SECS)
  {
    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30510));
    return false;
  }

  unsigned int date = ((startTime + m_timezone) / DAY_SECS) + DELPHI_DATE;
  struct tm *timeinfo;
  timeinfo = localtime(&startTime);
  unsigned int start = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  timeinfo = localtime(&endTime);
  unsigned int stop = timeinfo->tm_hour * 60 + timeinfo->tm_min;

  char repeat[8] = "-------";
  for (int i = 0; i < 7; ++i)
  {
    if (timer.iWeekdays & (1 << i))
      repeat[i] = 'T';
  }

  uint64_t backendId = m_channels[timer.iClientChannelUid - 1]->backendIds.front();
  std::string params = StringUtils::Format("encoding=255&ch=%" PRIu64 "&dor=%u"
      "&start=%u&stop=%u&pre=%u&post=%u&prio=%d&days=%s&enable=%d",
      backendId, date, start, stop, pre, post, timer.iPriority, repeat,
      (timer.state != PVR_TIMER_STATE_DISABLED));
  params += "&title="  + URLEncode(timer.strTitle);
  params += "&folder=" + URLEncode((timer.iRecordingGroup == 0) ? "Auto"
      : m_recfolders[timer.iRecordingGroup - 1]);
  if (update) {
    auto t = GetTimer([&] (const DvbTimer &t)
        {
          return (t.id == timer.iClientIndex);
        });
    if (!t)
    {
      XBMC->Log(LOG_ERROR, "Timer %u is unknown", timer.iClientIndex);
      return false;
    }
    params += StringUtils::Format("&id=%d", t->backendId);
  }

  const httpResponse &res = GetFromAPI("api/timer%s.html?%s",
      (update) ? "edit" : "add", params.c_str());
  if (res.error)
  {
    XBMC->Log(LOG_ERROR, "Unable to add/edit timer");
    return false;
  }

  //TODO: instead of syncing all timers, we could only sync the new/modified
  // in case the timer has already started we should check the recordings too
  m_updateTimers = true;
  return true;
}

bool Dvb::DeleteTimer(const PVR_TIMER &timer)
{
  CLockObject lock(m_mutex);
  auto t = GetTimer([&] (const DvbTimer &t)
      {
        return (t.id == timer.iClientIndex);
      });
  if (!t)
    return false;

  GetFromAPI("api/timerdelete.html?id=%u", t->backendId);

  //TODO: instead of syncing all timers, we could only sync the new/modified
  m_updateTimers = true;
  return true;
}

unsigned int Dvb::GetTimersAmount()
{
  CLockObject lock(m_mutex);
  return m_timers.size();
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

  // there's no need to merge new recordings in older ones as XBMC does this
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
    XMLUtils_GetString(xRecording, "title", recording.title);
    XMLUtils_GetString(xRecording, "info",  recording.plotOutline);
    XMLUtils_GetString(xRecording, "desc",  recording.plot);
    if (recording.plot.empty())
    {
      recording.plot = recording.plotOutline;
      recording.plotOutline.clear();
    }
    else if (g_prependOutline == PrependOutline::IN_RECORDINGS
        || g_prependOutline == PrependOutline::ALWAYS)
    {
      recording.plot.insert(0, recording.plotOutline + "\n");
      recording.plotOutline.clear();
    }

    /* fetch and search channel */
    XMLUtils_GetString(xRecording, "channel", recording.channelName);
    recording.channel = GetChannel([&] (const DvbChannel *channel)
        {
          return (channel->backendName == recording.channelName);
        });
    if (recording.channel)
      recording.channelName = recording.channel->name;

    std::string thumbnail;
    if (!g_lowPerformance && XMLUtils_GetString(xRecording, "image", thumbnail))
      recording.thumbnail = BuildURL("upnp/thumbnails/video/%s",
          thumbnail.c_str());

    std::string startTime = xRecording->Attribute("start");
    recording.start = ParseDateTime(startTime);

    int hours, mins, secs;
    sscanf(xRecording->Attribute("duration"), "%02d%02d%02d", &hours, &mins, &secs);
    recording.duration = hours*60*60 + mins*60 + secs;

    std::string group("Unknown");
    switch(g_groupRecordings)
    {
      case DvbRecording::Grouping::BY_DIRECTORY:
        {
          std::string file;
          if (!XMLUtils_GetString(xRecording, "file", file))
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
      case DvbRecording::Grouping::BY_DATE:
        group = StringUtils::Format("%s/%s", startTime.substr(0, 4).c_str(),
            startTime.substr(4, 2).c_str());
        break;
      case DvbRecording::Grouping::BY_FIRST_LETTER:
        group = ::toupper(recording.title[0]);
        break;
      case DvbRecording::Grouping::BY_TV_CHANNEL:
        group = recording.channelName;
        break;
      case DvbRecording::Grouping::BY_SERIES:
        XMLUtils_GetString(xRecording, "series", group);
        break;
      case DvbRecording::Grouping::BY_TITLE:
        group = recording.title;
        break;
      default:
        group = "";
        break;
    }
    recording.group = groups.emplace(group, 0).first;
    ++recording.group->second;

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
    recinfo.recordingTime = recording.start;
    recinfo.iDuration     = recording.duration;
    recinfo.iGenreType    = recording.genre & 0xF0;
    recinfo.iGenreSubType = recording.genre & 0x0F;
    recinfo.iChannelUid   = PVR_CHANNEL_INVALID_UID;
    recinfo.channelType   = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

    if (recording.channel)
    {
      recinfo.iChannelUid = recording.channel->id;
      recinfo.channelType = (recording.channel->radio)
          ? PVR_RECORDING_CHANNEL_TYPE_RADIO : PVR_RECORDING_CHANNEL_TYPE_TV;
    }

    // no grouping for single entry groups if by_title
    if (g_groupRecordings != DvbRecording::Grouping::BY_TITLE
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
  const httpResponse &res = GetFromAPI("api/recdelete.html?recid=%s&delfile=1",
      recinfo.strRecordingId);
  if (res.error)
    return false;
  PVR->TriggerRecordingUpdate();
  return true;
}

// edltype_to_string
//
// Converts a PVR_EDL_TYPE enumeration value into a string
static char const* const edltype_to_string(PVR_EDL_TYPE const& type)
{
  switch (type) {

  case PVR_EDL_TYPE::PVR_EDL_TYPE_CUT: return "CUT";
  case PVR_EDL_TYPE::PVR_EDL_TYPE_MUTE: return "MUTE";
  case PVR_EDL_TYPE::PVR_EDL_TYPE_SCENE: return "SCENE";
  case PVR_EDL_TYPE::PVR_EDL_TYPE_COMBREAK: return "COMBREAK";
  }

  return "<UNKNOWN>";
}

//---------------------------------------------------------------------------
// GetRecordingEdl
//
// Retrieve the edit decision list (EDL) of a recording on the backend
//
// Arguments:
//
//	recording	- The recording
//	edl			- The function has to write the EDL list into this array
//	count		- in: The maximum size of the EDL, out: the actual size of the EDL

bool Dvb::GetRecordingEdl(PVR_RECORDING const& recording, PVR_EDL_ENTRY edl[], int* count)
{
  CLockObject lock(m_mutex);
  std::vector<PVR_EDL_ENTRY>    entries;                // vector<> of PVR_EDL_ENTRYs

  if (count == nullptr) return false;
  if ((*count) && (edl == nullptr)) return false;

  memset(edl, 0, sizeof(PVR_EDL_ENTRY) * (*count));     // Initialize [out] array

  XBMC->Log(LOG_DEBUG, "%s: kodi requests edl for recording '%s': id='%s'",
  __FUNCTION__, recording.strTitle, recording.strRecordingId);

  // check if EDL is enabled
  if (!g_enable_recording_edl) return false;

  // Verify that the specified directory for the EDL files exists
  if (!XBMC->DirectoryExists(g_recordingEdlFolder.c_str()))
  {
    XBMC->Log(LOG_INFO, "%s: specified edit decision list file directory '%s' cannot be accessed",
      __FUNCTION__, g_recordingEdlFolder.c_str());
    return false;
  }

  // Request recordings to get filename of recording.strRecordingId
  httpResponse &&res = GetHttpXML(BuildURL("api/recordings.html?utf8=1"
    "&images=1"));
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

  // parse recordings and try to find our requested recording id
  for (TiXmlNode *xNode = root->LastChild("recording");
    xNode; xNode = xNode->PreviousSibling("recording"))
  {
    TiXmlElement *xRecording = xNode->ToElement();
    if (!xRecording)
      continue;

    // parse recording id
    std::string basename;
    std::string recording_id;
    recording_id = xRecording->Attribute("id");
    XMLUtils_GetString(xRecording, "file", basename);

    // is this our recording id?
    if (recording_id.compare(recording.strRecordingId) == 0)
    {
      //we have found our recording -> Generate the full name of the .EDL file and if it exists, attempt to process it
      std::string filename = g_recordingEdlFolder;
      filename.append(basename.substr(basename.find_last_of("\\/") + 1, basename.size() - 4 - basename.find_last_of("\\/"))).append(".edl");

      XBMC->Log(LOG_DEBUG, "%s: TFILE: '%s'",
        __FUNCTION__, filename.c_str());

      //check if edl file exists
      if (XBMC->FileExists(filename.c_str(), false)) {

        // 2 KiB should be more than sufficient to hold a single line from the .edl file
        std::unique_ptr<char[]> line(new char[2048]);

        // Attempt to open the input edit decision list file
        void* handle = XBMC->OpenFile(filename.c_str(), 0);
        if (handle != nullptr) {

          size_t linenumber = 0;
          XBMC->Log(LOG_INFO, "%s: processing edit decision list file: '%s'",
            __FUNCTION__, filename.c_str());

          // Process each line of the file individually
          while (XBMC->ReadFileString(handle, &line[0], 2048)) {

            // The only currently supported format for EDL is the {float|float|[int]} format, as the
            // frame rate of the recording would be required to process the {#frame|#frame|[int]} format

            // Increment the line number
            ++linenumber;

            // Starting point, in milliseconds
            float  start = 0.0F;

            // Ending point, in milliseconds
            float  end = 0.0F;

            // Type of edit to be made
            int    type = PVR_EDL_TYPE_CUT;

            if (sscanf(&line[0], "%f %f %i", &start, &end, &type) >= 2) {

              // Apply any user-specified adjustments to the start and end times accordingly
              start += (static_cast<float>(g_recording_edl_start_padding) / 1000.0F);
              end -= (static_cast<float>(g_recording_edl_end_padding) / 1000.0F);

              // Ensure the start and end times are positive and do not overlap
              start = std::min(std::max(start, 0.0F), std::max(end, 0.0F));
              end = std::max(std::max(end, 0.0F), std::max(start, 0.0F));

              // Log the adjusted values for the entry and add a PVR_EDL_ENTRY to the vector<>
              XBMC->Log(LOG_INFO, "%s: adding edit decision list entry (start=%f ms, end=%f ms, type=%s)", __FUNCTION__,start,end, edltype_to_string(static_cast<PVR_EDL_TYPE>(type)));
              entries.emplace_back(PVR_EDL_ENTRY{ static_cast<int64_t>(start * 1000.0F), static_cast<int64_t>(end * 1000.0F), static_cast<PVR_EDL_TYPE>(type) });
            }

            else XBMC->Log(LOG_ERROR, "%s: invalid edit decision list entry detected at line #%i", __FUNCTION__, linenumber);
          }

          XBMC->CloseFile(handle);
        }

        else XBMC->Log(LOG_ERROR, "%s: unable to open edit decision list file: %s", __FUNCTION__, filename.c_str());
      }

      // Copy the parsed entries, if any, from the vector<> into the output array
      *count = static_cast<int>(std::min(entries.size(), static_cast<size_t>(*count)));
      memcpy(edl, entries.data(), (*count * sizeof(PVR_EDL_ENTRY)));

      return true;
    }
  }

  XBMC->Log(LOG_DEBUG, "%s: recording id='%s' not found! do not return any edl information to kodi!",
    __FUNCTION__, recording.strRecordingId);

  return false;
}

unsigned int Dvb::GetRecordingsAmount()
{
  CLockObject lock(m_mutex);
  return m_recordingAmount;
}

RecordingReader *Dvb::OpenRecordedStream(const PVR_RECORDING &recinfo)
{
  CLockObject lock(m_mutex);
  time_t now = time(nullptr), end = 0;
  std::string channelName = recinfo.strChannelName;
  auto timer = GetTimer([&] (const DvbTimer &timer)
      {
        return (timer.start <= now && now <= timer.end
            && timer.state != PVR_TIMER_STATE_DISABLED
            && timer.channel->name == channelName);
      });
  if (timer)
    end = timer->end;

  return new RecordingReader(BuildURL("upnp/recordings/%s.ts",
        recinfo.strRecordingId), end);
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
    if (!g_lowPerformance)
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
  DvbChannel *channel = m_channels[channelinfo.iUniqueId - 1];
  uint64_t backendId = channel->backendIds.front();
  switch(g_transcoding)
  {
    case Transcoding::TS:
      return BuildURL("flashstream/stream.ts?chid=%" PRIu64 "&%s",
        backendId, g_transcodingParams.c_str());
      break;
    case Transcoding::WEBM:
      return BuildURL("flashstream/stream.webm?chid=%" PRIu64 "&%s",
        backendId, g_transcodingParams.c_str());
      break;
    case Transcoding::FLV:
      return BuildURL("flashstream/stream.flv?chid=%" PRIu64 "&%s",
        backendId, g_transcodingParams.c_str());
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
  int interval = (!g_lowPerformance) ? 60 : 300;

  // set PVR_CONNECTION_STATE_CONNECTING only once!
  SetConnectionState(PVR_CONNECTION_STATE_CONNECTING);

  while (!IsStopped())
  {
    if (!IsConnected())
    {
      if (g_useWoL)
      {
        if (!XBMC->WakeOnLan(g_mac.c_str()))
          XBMC->Log(LOG_ERROR, "Error sending WoL packet to %s", g_mac.c_str());
      }

      XBMC->Log(LOG_INFO, "Trying to connect to the backend service...");

      if (CheckBackendVersion() && UpdateBackendStatus(true) && LoadChannels())
      {
        XBMC->Log(LOG_INFO, "Connection to the backend service successful.");
        SetConnectionState(PVR_CONNECTION_STATE_CONNECTED);

        TimerUpdates();
        // force recording sync as Kodi won't update recordings on PVR restart
        PVR->TriggerRecordingUpdate();
      }
      else
      {
        XBMC->Log(LOG_INFO, "Connection to the backend service failed."
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
        XBMC->Log(LOG_INFO, "Performing forced EPG update!");
        PVR->TriggerEpgUpdate(m_currentChannel);
      }

      if (m_updateTimers)
      {
        m_updateTimers = false;
        m_mutex.Unlock();
        Sleep(1000);
        m_mutex.Lock();
        XBMC->Log(LOG_INFO, "Performing forced timer updates!");
        TimerUpdates();
        update = 0;
      }

      if (update >= interval)
      {
        update = 0;
        XBMC->Log(LOG_INFO, "Performing timer/recording updates!");
        TimerUpdates();
        PVR->TriggerRecordingUpdate();
      }
    }
  }

  return nullptr;
}

Dvb::httpResponse Dvb::GetFromAPI(const char* format, ...)
{
  static const std::string baseUrl = StringUtils::Format("http://%s:%u/",
      g_hostname.c_str(), g_webPort);
  va_list argList;
  va_start(argList, format);
  std::string url = baseUrl + StringUtils::FormatV(format, argList);
  va_end(argList);

  httpResponse res = { true, 0, "" };
  void *file = XBMC->CURLCreate(url.c_str());
  if (!file)
  {
    XBMC->Log(LOG_ERROR, "Unable to create curl handle for %s", url.c_str());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return res;
  }

  XBMC->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "user-agent", "Kodi PVR");
  XBMC->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, "Accept", "text/xml");
  if (!g_username.empty() && !g_password.empty())
    XBMC->CURLAddOption(file, XFILE::CURL_OPTION_CREDENTIALS,
        g_username.c_str(), g_password.c_str());

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

  res.error = (res.code >= 300);
  char buffer[1024];
  while (int bytesRead = XBMC->ReadFile(file, buffer, 1024))
    res.content.append(buffer, bytesRead);
  XBMC->CloseFile(file);
  return res;
}

/* Copied from xbmc/URL.cpp */
std::string Dvb::URLEncode(const std::string& data)
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
        XBMC->GetLocalizedString(30502));
    return false;
  }

  m_channels.clear();
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
  if (g_useFavourites && !g_useFavouritesFile && !hasFavourites)
  {
    XBMC->Log(LOG_NOTICE, "Favourites enabled but non defined");
    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30509));
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
      group->hidden   = g_useFavourites;
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
        channel->hidden     = g_useFavourites;
        channel->frontendNr = (!channel->hidden) ? m_channels.size() + 1 : 0;
        xChannel->QueryValueAttribute<uint64_t>("EPGID", &channel->epgId);

        uint64_t backendId = 0;
        xChannel->QueryValueAttribute<uint64_t>("ID", &backendId);
        channel->backendIds.push_back(backendId);

        std::string logo;
        if (!g_lowPerformance && XMLUtils_GetString(xChannel, "logo", logo))
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

  if (g_useFavourites && !g_useFavouritesFile)
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
        DvbChannel *channel = GetChannel([&] (const DvbChannel *channel)
            {
              return (std::find(channel->backendIds.begin(),
                    channel->backendIds.end(), backendId)
                  != channel->backendIds.end());
            });
        if (!channel)
        {
          XBMC->Log(LOG_NOTICE, "Favourites contains unresolvable channel: %s."
              " Ignoring.", xChannel->Attribute("name"));
          XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30508),
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
  else if (g_useFavourites && g_useFavouritesFile)
  {
    void *fileHandle = XBMC->OpenFile(g_favouritesFile.c_str(), 0);
    if (!fileHandle)
    {
      XBMC->Log(LOG_ERROR, "Unable to open local favourites.xml");
      SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
          XBMC->GetLocalizedString(30504));
      return false;
    }

    std::string content;
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
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
          XBMC->GetLocalizedString(30505));
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

        DvbChannel *channel = GetChannel([&] (const DvbChannel *channel)
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
          XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30508),
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

  XBMC->Log(LOG_INFO, "Loaded (%u/%u) channels in (%u/%u) groups",
      m_channelAmount, m_channels.size(), m_groupAmount, m_groups.size());
  // force channel sync as stream urls may have changed (e.g. rstp on/off)
  PVR->TriggerChannelUpdate();
  return true;
}

DvbTimers_t Dvb::LoadTimers()
{
  DvbTimers_t timers;

  // utf8=2 is correct here
  httpResponse &&res = GetFromAPI("api/timerlist.html?utf8=2");
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return timers;
  }

  TiXmlDocument doc;
  RemoveNullChars(res.content);
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse timers. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
        XBMC->GetLocalizedString(30506));
    return timers;
  }

  for (TiXmlElement *xTimer = doc.RootElement()->FirstChildElement("Timer");
      xTimer; xTimer = xTimer->NextSiblingElement("Timer"))
  {
    DvbTimer timer;

    if (!XMLUtils_GetString(xTimer, "GUID", timer.guid))
      continue;
    XMLUtils::GetUInt(xTimer, "ID", timer.backendId);
    XMLUtils_GetString(xTimer, "Descr", timer.title);

    //TODO: DMS 2.0.4.17 adds the EPGID. do the search with that
    uint64_t backendId = 0;
    std::istringstream ss(xTimer->FirstChildElement("Channel")->Attribute("ID"));
    ss >> backendId;
    if (!backendId)
      continue;

    timer.channel = GetChannel([&] (const DvbChannel *channel)
        {
          return (std::find(channel->backendIds.begin(),
                channel->backendIds.end(), backendId)
              != channel->backendIds.end());
        });
    if (!timer.channel)
    {
      XBMC->Log(LOG_NOTICE, "Found timer for unknown channel (backendid=%"
        PRIu64 "). Ignoring.", backendId);
      continue;
    }

    std::string startDate = xTimer->Attribute("Date");
    startDate += xTimer->Attribute("Start");
    timer.start = ParseDateTime(startDate, false);
    timer.end   = timer.start + atoi(xTimer->Attribute("Dur")) * 60;

    timer.pre = timer.post = 0;
    xTimer->QueryUnsignedAttribute("PreEPG",  &timer.pre);
    xTimer->QueryUnsignedAttribute("PostEPG", &timer.post);
    // Kodi requires starttime/endtime to exclude the margins
    timer.start += timer.pre * 60;
    timer.end   -= timer.post * 60;

    timer.weekdays = PVR_WEEKDAY_NONE;
    const char *weekdays = xTimer->Attribute("Days");
    for (unsigned int j = 0; weekdays && weekdays[j] != '\0'; ++j)
    {
      if (weekdays[j] != '-')
        timer.weekdays += (1 << j);
    }
    if (timer.weekdays != PVR_WEEKDAY_NONE)
      timer.type = DvbTimer::Type::MANUAL_REPEATING;

    xTimer->QueryIntAttribute("Priority", &timer.priority);
    timer.updateState = DvbTimer::State::NEW;
    timer.state       = PVR_TIMER_STATE_SCHEDULED;
    if (xTimer->Attribute("Enabled")[0] == '0')
      timer.state = PVR_TIMER_STATE_DISABLED;

    int tmp;
    XMLUtils::GetInt(xTimer, "Recording", tmp);
    if (tmp == -1)
      timer.state = PVR_TIMER_STATE_RECORDING;

    timer.recfolder = -1;
    std::string recfolder;
    if (XMLUtils_GetString(xTimer, "Folder", recfolder))
    {
      auto pos = std::distance(m_recfolders.begin(),
          std::find(m_recfolders.begin(), m_recfolders.end(), recfolder));
      if (pos < m_recfolders.size())
        timer.recfolder = pos;
    }

    timers.emplace_back(timer);
    XBMC->Log(LOG_DEBUG, "%s: Loaded timer entry '%s': type=%u, start=%u, end=%u",
        __FUNCTION__, timer.title.c_str(), timer.type, timer.start, timer.end);
  }

  XBMC->Log(LOG_INFO, "Loaded %u timer entries", timers.size());
  return timers;
}

void Dvb::TimerUpdates()
{
  for (auto &timer : m_timers)
    timer.updateState = DvbTimer::State::NONE;

  DvbTimers_t &&newtimers = LoadTimers();
  unsigned int updated = 0, unchanged = 0;
  for (auto &newtimer : newtimers)
  {
    for (auto &timer : m_timers)
    {
      if (timer.guid != newtimer.guid)
        continue;

      if (timer.updateFrom(newtimer))
      {
        timer.updateState = newtimer.updateState = DvbTimer::State::UPDATED;
        ++updated;
      }
      else
      {
        timer.updateState = newtimer.updateState = DvbTimer::State::FOUND;
        ++unchanged;
      }
      break;
    }
  }

  unsigned int removed = 0;
  for (auto it = m_timers.begin(); it != m_timers.end();)
  {
    if (it->updateState == DvbTimer::State::NONE)
    {
      XBMC->Log(LOG_DEBUG, "%s: Removed timer '%s': id=%u", __FUNCTION__,
          it->title.c_str(), it->id);
      it = m_timers.erase(it);
      ++removed;
    }
    else
      ++it;
  }

  unsigned int added = 0;
  for (auto &newtimer : newtimers)
  {
    if (newtimer.updateState == DvbTimer::State::NEW)
    {
      newtimer.id = m_nextTimerId;
      XBMC->Log(LOG_DEBUG, "%s: New timer '%s': id=%u", __FUNCTION__,
          newtimer.title.c_str(), newtimer.id);
      m_timers.push_back(newtimer);
      ++m_nextTimerId;
      ++added;
    }
  }

  XBMC->Log(LOG_DEBUG, "%s: Timers update: removed=%u, unchanged=%u, updated=%u, added=%u",
      __FUNCTION__, removed, unchanged, updated, added);

  if (removed || updated || added)
  {
    XBMC->Log(LOG_INFO, "Changes in timerlist detected, triggering an update!");
    PVR->TriggerTimerUpdate();
  }
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

DvbTimer *Dvb::GetTimer(std::function<bool (const DvbTimer&)> func)
{
  for (auto &timer : m_timers)
  {
    if (func(timer))
      return &timer;
  }
  return nullptr;
}


void Dvb::RemoveNullChars(std::string& str)
{
  /* favourites.xml and timers.xml sometimes have null chars that screw the xml */
  str.erase(std::remove(str.begin(), str.end(), '\0'), str.end());
}

bool Dvb::CheckBackendVersion()
{
  const httpResponse &res = GetFromAPI("api/version.html");
  if (res.code == 401)
    SetConnectionState(PVR_CONNECTION_STATE_ACCESS_DENIED);
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to connect to the backend service. Error: %s",
        doc.ErrorDesc());
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }

  XBMC->Log(LOG_NOTICE, "Checking backend version...");
  if (doc.RootElement()->QueryUnsignedAttribute("iver", &m_backendVersion)
      != TIXML_SUCCESS)
  {
    XBMC->Log(LOG_ERROR, "Unable to parse version");
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH);
    return false;
  }
  XBMC->Log(LOG_NOTICE, "Version: %u", m_backendVersion);

  if (m_backendVersion < DMS_MIN_VERSION_NUM)
  {
    XBMC->Log(LOG_ERROR, "DVBViewer Media Server version %s or higher required",
        DMS_MIN_VERSION_STR);
    SetConnectionState(PVR_CONNECTION_STATE_VERSION_MISMATCH,
      XBMC->GetLocalizedString(30501), DMS_MIN_VERSION_STR);
    return false;
  }

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
  {
    m_timezone = GetGMTOffset();
    m_recfolders.clear();
  }

  // compute disk space. duplicates are detected by their identical values
  TiXmlElement *root = doc.RootElement();
  std::set< std::pair<long long, long long> > folders;
  m_diskspace.total = m_diskspace.used = 0;
  for (TiXmlElement *xFolder = root->FirstChild("recfolders")->FirstChildElement("folder");
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
    PVR->ConnectionStateChange(g_hostname.c_str(), m_state, message);
  }
}

time_t Dvb::ParseDateTime(const std::string& date, bool iso8601)
{
  struct tm timeinfo;

  memset(&timeinfo, 0, sizeof(tm));
  if (iso8601)
    sscanf(date.c_str(), "%04d%02d%02d%02d%02d%02d", &timeinfo.tm_year,
        &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour,
        &timeinfo.tm_min, &timeinfo.tm_sec);
  else
    sscanf(date.c_str(), "%02d.%02d.%04d%02d:%02d:%02d", &timeinfo.tm_mday,
        &timeinfo.tm_mon, &timeinfo.tm_year, &timeinfo.tm_hour,
        &timeinfo.tm_min, &timeinfo.tm_sec);
  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  return mktime(&timeinfo);
}

std::string Dvb::BuildURL(const char* path, ...)
{
  static const std::string auth = (g_username.empty() || g_password.empty()) ? ""
      : StringUtils::Format("%s:%s@", URLEncode(g_username).c_str(),
          URLEncode(g_password).c_str());
  static const std::string baseUrl = StringUtils::Format("http://%s%s:%u/",
      auth.c_str(), g_hostname.c_str(), g_webPort);

  std::string url(baseUrl);
  va_list argList;
  va_start(argList, path);
  url += StringUtils::FormatV(path, argList);
  va_end(argList);
  return url;
}

std::string Dvb::ConvertToUtf8(const std::string& src)
{
  char *tmp = XBMC->UnknownToUTF8(src.c_str());
  std::string dest(tmp);
  XBMC->FreeString(tmp);
  return dest;
}

long Dvb::GetGMTOffset()
{
#ifdef TARGET_POSIX
  struct tm t;
  tzset();
  time_t tt = time(nullptr);
  if (localtime_r(&tt, &t))
    return t.tm_gmtoff;
#else
  TIME_ZONE_INFORMATION tz;
  switch(GetTimeZoneInformation(&tz))
  {
    case TIME_ZONE_ID_DAYLIGHT:
      return (tz.Bias + tz.DaylightBias) * -60;
      break;
    case TIME_ZONE_ID_STANDARD:
      return (tz.Bias + tz.StandardBias) * -60;
      break;
    case TIME_ZONE_ID_UNKNOWN:
      return tz.Bias * -60;
      break;
  }
#endif
  return 0;
}
