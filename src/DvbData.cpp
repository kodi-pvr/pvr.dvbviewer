#include "DvbData.h"
#include "client.h"
#include "util/XMLUtils.h"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"
#include <tinyxml.h>
#include <inttypes.h>
#include <map>
#include <set>
#include <iterator>
#include <sstream>
#include <algorithm>

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
  // simply add user@pass in front of the URL if username/password is set
  std::string auth("");
  if (!g_username.empty() && !g_password.empty())
    auth = StringUtils::Format("%s:%s@", URLEncode(g_username).c_str(),
        URLEncode(g_password).c_str());
  m_url = StringUtils::Format("http://%s%s:%u/", auth.c_str(), g_hostname.c_str(),
      g_webPort);

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
    PVR_STRCPY(xbmcChannel.strIconPath,    channel->logoURL.c_str());

    PVR->TransferChannelEntry(handle, &xbmcChannel);
  }
  return true;
}

bool Dvb::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channelinfo,
    time_t start, time_t end)
{
  DvbChannel *channel = m_channels[channelinfo.iUniqueId - 1];

  const std::string &url = BuildURL("api/epg.html?lvl=2&channel=%" PRIu64
      "&start=%f&end=%f", channel->epgId, start/86400.0 + DELPHI_DATE,
      end/86400.0 + DELPHI_DATE);
  const httpResponse &res = GetHttpXML(url);
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
    broadcast.iChannelNumber      = channelinfo.iChannelNumber;
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

      XBMC->Log(LOG_DEBUG, "%s: Add channel '%s' (%u) to group '%s'",
          __FUNCTION__, channel->name.c_str(), channel->backendNr,
          group.name.c_str());
    }
  }
  return true;
}

unsigned int Dvb::GetChannelGroupsAmount()
{
  return m_groupAmount;
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
    tag.state             = timer.state;
    /* TODO: Implement own timer types to get support for the timer features introduced with PVR API 1.9.7 */
    tag.iTimerType        = PVR_TIMER_TYPE_NONE;
    tag.iPriority         = timer.priority;
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

  time_t startTime = timer.startTime - timer.iMarginStart * 60;
  time_t endTime   = timer.endTime   + timer.iMarginEnd * 60;
  if (!timer.startTime)
    startTime = time(NULL);

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
  if (!update)
    GetHttpXML(BuildURL("api/timeradd.html?ch=%" PRIu64 "&dor=%u&enable=1"
        "&start=%u&stop=%u&prio=%d&days=%s&title=%s&encoding=255",
        backendId, date, start, stop, timer.iPriority, repeat,
        URLEncode(timer.strTitle).c_str()));
  else
  {
    auto t = GetTimer([&] (const DvbTimer &t)
        {
          return (t.id == timer.iClientIndex);
        });
    if (!t)
      return false;

    short enabled = (timer.state == PVR_TIMER_STATE_CANCELLED) ? 0 : 1;
    GetHttpXML(BuildURL("api/timeredit.html?id=%d&ch=%" PRIu64 "&dor=%u"
        "&enable=%d&start=%u&stop=%u&prio=%d&days=%s&title=%s&encoding=255",
        t->backendId, backendId, date, enabled, start, stop, timer.iPriority,
        repeat, URLEncode(timer.strTitle).c_str()));
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

  GetHttpXML(BuildURL("api/timerdelete.html?id=%u", t->backendId));

  //TODO: instead of syncing all timers, we could only sync the new/modified
  m_updateTimers = true;
  return true;
}

unsigned int Dvb::GetTimersAmount()
{
  CLockObject lock(m_mutex);
  return m_timers.size();
}


bool Dvb::GetRecordings(ADDON_HANDLE handle)
{
  CLockObject lock(m_mutex);

  std::string apiResource = "api/recordings.html?utf8=1&images=1";
  // suppress file names of recording, if not required for grouping
  if (g_groupRecordings != DvbRecording::Grouping::BY_DIRECTORY)
  {
    apiResource.append("&nofilename=1");
  }

  httpResponse &&res = GetHttpXML(BuildURL(apiResource.c_str()));
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

  std::string imageURL;
  TiXmlElement *root = doc.RootElement();
  // refresh in case this has changed
  XMLUtils_GetString(root, "serverURL", m_recordingURL);
  XMLUtils_GetString(root, "imageURL",  imageURL);

  // there's no need to merge new recordings in older ones as XBMC does this
  // already for us (using strRecordingId). so just parse all recordings again
  m_recordingAmount = 0;

  // count number of recordings per group
  std::map<std::string, unsigned int> recordingsPerGroup;

  // list of recordings
  std::vector<PVR_RECORDING> listOfRecordings;

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
    XMLUtils_GetString(xRecording, "title",   recording.title);
    XMLUtils_GetString(xRecording, "info",    recording.plotOutline);
    XMLUtils_GetString(xRecording, "desc",    recording.plot);
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
      recording.thumbnailPath = BuildExtURL(imageURL, "%s", thumbnail.c_str());

    std::string startTime = xRecording->Attribute("start");
    recording.start = ParseDateTime(startTime);

    int hours, mins, secs;
    sscanf(xRecording->Attribute("duration"), "%02d%02d%02d", &hours, &mins, &secs);
    recording.duration = hours*60*60 + mins*60 + secs;

    PVR_RECORDING recinfo;
    memset(&recinfo, 0, sizeof(PVR_RECORDING));
    PVR_STRCPY(recinfo.strRecordingId,   recording.id.c_str());
    PVR_STRCPY(recinfo.strTitle,         recording.title.c_str());
    PVR_STRCPY(recinfo.strPlotOutline,   recording.plotOutline.c_str());
    PVR_STRCPY(recinfo.strPlot,          recording.plot.c_str());
    PVR_STRCPY(recinfo.strChannelName,   recording.channelName.c_str());
    PVR_STRCPY(recinfo.strThumbnailPath, recording.thumbnailPath.c_str());
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

    std::string tmp;
    switch(g_groupRecordings)
    {
      case DvbRecording::Grouping::BY_DIRECTORY:
        XMLUtils_GetString(xRecording, "file", tmp);
        StringUtils::ToLower(tmp);
        for (auto &recf : m_recfolders)
        {
          if (!StringUtils::StartsWith(tmp, recf))
            continue;
          tmp = tmp.substr(recf.length(), tmp.rfind('\\') - recf.length());
          StringUtils::Replace(tmp, '\\', '/');
          // strip leading /, if present
          std::string::size_type offset = 0;
          if (!tmp.empty() && (tmp[0] == '/'))
          {
            offset++;
          }

          PVR_STRCPY(recinfo.strDirectory, tmp.c_str() + offset);
          break;
        }
        break;
      case DvbRecording::Grouping::BY_DATE:
        tmp = StringUtils::Format("%s/%s", startTime.substr(0, 4).c_str(),
            startTime.substr(4, 2).c_str());
        PVR_STRCPY(recinfo.strDirectory, tmp.c_str());
        break;
      case DvbRecording::Grouping::BY_FIRST_LETTER:
        recinfo.strDirectory[0] = recording.title[0];
        recinfo.strDirectory[1] = '\0';
        break;
      case DvbRecording::Grouping::BY_TV_CHANNEL:
        PVR_STRCPY(recinfo.strDirectory, recording.channelName.c_str());
        break;
      case DvbRecording::Grouping::BY_SERIES:
        tmp = "Unknown";
        XMLUtils_GetString(xRecording, "series", tmp);
        PVR_STRCPY(recinfo.strDirectory, tmp.c_str());
        break;
      case DvbRecording::Grouping::BY_TITLE:
        PVR_STRCPY(recinfo.strDirectory, recinfo.strTitle);
        break;
      default:
        break;
    }

    recordingsPerGroup[recinfo.strDirectory]++;

    listOfRecordings.push_back(recinfo);    

    XBMC->Log(LOG_DEBUG, "%s: Loaded recording entry '%s': start=%u, length=%u",
        __FUNCTION__, recording.title.c_str(), recording.start,
        recording.duration);
  }

  // remove groups having only one member
  if (g_useGroupsOnDemand)
  {
    for (auto &recinfo : listOfRecordings)
    {
      if (recordingsPerGroup[recinfo.strDirectory] == 1)
      {
        recinfo.strDirectory[0] = '\0';
      }
    }
  }

  // transfer all recordings to PVR manager
  for (auto &recinfo : listOfRecordings)
  {
    PVR->TransferRecordingEntry(handle, &recinfo);
    ++m_recordingAmount;
  }

  XBMC->Log(LOG_INFO, "Loaded %u recording entries", m_recordingAmount);
  return true;
}

bool Dvb::DeleteRecording(const PVR_RECORDING &recinfo)
{
  // RS api doesn't return a result
  // TODO: check for http 200 / http 423
  // but kodi curl wrapper doesn't expose m_httpresponse
  GetHttpXML(BuildURL("api/recdelete.html?recid=%s&delfile=1",
      recinfo.strRecordingId));

  PVR->TriggerRecordingUpdate();
  return true;
}

unsigned int Dvb::GetRecordingsAmount()
{
  CLockObject lock(m_mutex);
  return m_recordingAmount;
}

RecordingReader *Dvb::OpenRecordedStream(const PVR_RECORDING &recinfo)
{
  CLockObject lock(m_mutex);
  time_t now = time(NULL), end = 0;
  std::string channelName = recinfo.strChannelName;
  auto timer = GetTimer([&] (const DvbTimer &timer)
      {
        return (timer.start <= now && now <= timer.end
            && timer.state != PVR_TIMER_STATE_CANCELLED
            && timer.channel->name == channelName);
      });
  if (timer)
    end = timer->end;

  return new RecordingReader(BuildExtURL(m_recordingURL, "%s.ts",
        recinfo.strRecordingId), end);
}


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
  //TODO: RS API doc says better use channel->backendId here.
  // however this might break default subchannel logic/overwrite
  if (g_transcoding != Transcoding::OFF)
  {
    switch(g_transcoding)
    {
      case Transcoding::TS:
        return BuildURL("flashstream/stream.ts?chid=%u&%s",
          channel->backendNr, g_transcodingParams.c_str());
        break;
      case Transcoding::WEBM:
        return BuildURL("flashstream/stream.webm?chid=%u&%s",
          channel->backendNr, g_transcodingParams.c_str());
        break;
      case Transcoding::FLV:
        return BuildURL("flashstream/stream.flv?chid=%u&%s",
          channel->backendNr, g_transcodingParams.c_str());
        break;
    }
  }
  return m_channels[channelinfo.iUniqueId - 1]->streamURL;
}

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
        Sleep(8000); /* Sleep enough time to let the recording service grab the EPG data */
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


Dvb::httpResponse Dvb::GetHttpXML(const std::string& url)
{
  // TODO Kodi CURL api doesn't expose the http code. need to replace it
  // afterwards handle connection failures here
  // (PVR_CONNECTION_STATE_SERVER_UNREACHABLE)
  httpResponse res = { true, "" };
  void *fileHandle = XBMC->OpenFile(url.c_str(), READ_NO_CACHE);
  if (fileHandle)
  {
    res.error = false;
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
      res.content.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);
    return res;
  }
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
  const httpResponse &res = GetHttpXML(BuildURL("api/getchannelsxml.html"
      "?subchannels=1&upnp=1&logo=1"));
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

  TiXmlElement *root = doc.RootElement();
  std::string streamURL;
  XMLUtils_GetString(root, "upnpURL", streamURL);

  m_channels.clear();
  m_channelAmount = 0;
  m_groups.clear();
  m_groupAmount = 0;

  for (TiXmlElement *xRoot = root->FirstChildElement("root");
      xRoot; xRoot = xRoot->NextSiblingElement("root"))
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
        channel->frontendNr = (!g_useFavourites) ? m_channels.size() + 1 : 0;
        xChannel->QueryUnsignedAttribute("nr", &channel->backendNr);
        xChannel->QueryValueAttribute<uint64_t>("EPGID", &channel->epgId);

        uint64_t backendId = 0;
        xChannel->QueryValueAttribute<uint64_t>("ID", &backendId);
        channel->backendIds.push_back(backendId);

        std::string logoURL;
        if (!g_lowPerformance && XMLUtils_GetString(xChannel, "logo", logoURL))
          channel->logoURL = BuildURL("%s", logoURL.c_str());
        //TODO: maybe move this to GetLiveStreamURL
        channel->streamURL = BuildExtURL(streamURL, "%u.ts", channel->backendNr);
        //TODO: better use channel->backendId here? might break default subchannel logic

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

  if (g_useFavourites)
  {
    std::string &&url = BuildURL("api/getfavourites.html");
    if (g_useFavouritesFile)
    {
      if (!XBMC->FileExists(g_favouritesFile.c_str(), false))
      {
        XBMC->Log(LOG_ERROR, "Unable to open local favourites.xml");
        SetConnectionState(PVR_CONNECTION_STATE_SERVER_MISMATCH,
            XBMC->GetLocalizedString(30504));
        return false;
      }
      url = g_favouritesFile;
    }

    httpResponse &&res = GetHttpXML(url);
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

        for (auto channel : m_channels)
        {
          bool found = false;

          for (auto backendId2 : channel->backendIds)
          {
            /* legacy support for old 32bit channel ids */
            if (backendId <= 0xFFFFFFFF)
              backendId2 &= 0xFFFFFFFF;
            if (backendId == backendId2)
            {
              found = true;
              break;
            }
          }

          if (found)
          {
            channel->hidden = false;
            channel->frontendNr = ++m_channelAmount;
            if (!ss.eof())
            {
              ss.ignore(1);
              std::string channelName;
              getline(ss, channelName);
              channel->name = ConvertToUtf8(channelName);
            }

            if (group)
            {
              group->channels.push_back(channel);
              if (!channel->radio)
                group->radio = false;
            }
            break;
          }
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
  httpResponse &&res = GetHttpXML(BuildURL("api/timerlist.html?utf8=2"));
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
      continue;

    std::string startDate = xTimer->Attribute("Date");
    startDate += xTimer->Attribute("Start");
    timer.start = ParseDateTime(startDate, false);
    timer.end   = timer.start + atoi(xTimer->Attribute("Dur")) * 60;

    timer.weekdays = PVR_WEEKDAY_NONE;
    const char *weekdays = xTimer->Attribute("Days");
    for (unsigned int j = 0; weekdays && weekdays[j] != '\0'; ++j)
    {
      if (weekdays[j] != '-')
        timer.weekdays += (1 << j);
    }

    timer.priority    = atoi(xTimer->Attribute("Priority"));
    timer.updateState = DvbTimer::State::NEW;
    timer.state       = PVR_TIMER_STATE_SCHEDULED;
    if (xTimer->Attribute("Enabled")[0] == '0')
      timer.state = PVR_TIMER_STATE_CANCELLED;

    int tmp;
    XMLUtils::GetInt(xTimer, "Recording", tmp);
    if (tmp == -1)
      timer.state = PVR_TIMER_STATE_RECORDING;

    timers.push_back(timer);
    XBMC->Log(LOG_DEBUG, "%s: Loaded timer entry '%s': start=%u, end=%u",
        __FUNCTION__, timer.title.c_str(), timer.start, timer.end);
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
  const httpResponse &res = GetHttpXML(BuildURL("api/version.html"));
  if (res.error)
  {
    SetConnectionState(PVR_CONNECTION_STATE_SERVER_UNREACHABLE);
    return false;
  }

  // TODO check access denied (PVR_CONNECTION_STATE_ACCESS_DENIED) vs timeout
  // but kodi curl wrapper doesn't expose m_httpresponse

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

  if (m_backendVersion < RS_VERSION_NUM)
  {
    XBMC->Log(LOG_ERROR, "Recording Service version %s or higher required",
        RS_VERSION_STR);
    SetConnectionState(PVR_CONNECTION_STATE_VERSION_MISMATCH,
      XBMC->GetLocalizedString(30501), RS_VERSION_STR);
    return false;
  }

  return true;
}

bool Dvb::UpdateBackendStatus(bool updateSettings)
{
  const httpResponse &res = GetHttpXML(BuildURL("api/status2.html"));
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
  typedef std::pair<long long, long long> Recfolder_t;
  std::set<Recfolder_t> folders;
  m_diskspace.total = m_diskspace.used = 0;
  for (TiXmlElement *xFolder = root->FirstChild("recfolders")->FirstChildElement("folder");
      xFolder; xFolder = xFolder->NextSiblingElement("folder"))
  {
    long long size = 0, free = 0;
    xFolder->QueryValueAttribute<long long>("size", &size);
    xFolder->QueryValueAttribute<long long>("free", &free);

    if (folders.insert(std::make_pair(size, free)).second)
    {
      m_diskspace.total += size / 1024;
      m_diskspace.used += (size - free) / 1024;
    }

    if (updateSettings && g_groupRecordings == DvbRecording::Grouping::BY_DIRECTORY)
    {
      std::string recf = xFolder->GetText();
      StringUtils::ToLower(recf);
      m_recfolders.push_back(recf);
    }
  }

  if (updateSettings && g_groupRecordings == DvbRecording::Grouping::BY_DIRECTORY)
    std::sort(m_recfolders.begin(), m_recfolders.end(),
        [](const std::string& a, const std::string& b)
        {
          return (a.length() < b.length());
        });

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
  std::string url(m_url);
  va_list argList;
  va_start(argList, path);
  url += StringUtils::FormatV(path, argList);
  va_end(argList);
  return url;
}

std::string Dvb::BuildExtURL(const std::string& baseURL, const char* path, ...)
{
  std::string url(baseURL);
  // simply add user@pass in front of the URL if username/password is set
  //TODO: maybe use special authorization option (see CurlFile.cpp)?
  if (!g_username.empty() && !g_password.empty())
  {
    std::string auth = StringUtils::Format("%s:%s@",
        URLEncode(g_username).c_str(), URLEncode(g_password).c_str());
    std::string::size_type pos = url.find("://");
    if (pos != std::string::npos)
      url.insert(pos + strlen("://"), auth);
  }
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
  time_t tt = time(NULL);
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
