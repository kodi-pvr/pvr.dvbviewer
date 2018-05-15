#include "Timers.h"
#include "DvbData.h"
#include "client.h"

#include <algorithm>
#include <ctime>

#include "inttypes.h"
#include "util/XMLUtils.h"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"

using namespace dvbviewer;
using namespace ADDON;

#define TIMER_UPDATE_MEMBER(member) \
  if (member != source.member) \
  { \
    member = source.member; \
    updated = true; \
  }
bool Timer::updateFrom(const Timer &source)
{
  bool updated = false;
  TIMER_UPDATE_MEMBER(type);
  TIMER_UPDATE_MEMBER(channel);
  TIMER_UPDATE_MEMBER(priority);
  TIMER_UPDATE_MEMBER(title);
  TIMER_UPDATE_MEMBER(recfolder);
  TIMER_UPDATE_MEMBER(start);
  TIMER_UPDATE_MEMBER(end);
  TIMER_UPDATE_MEMBER(marginStart);
  TIMER_UPDATE_MEMBER(marginEnd);
  TIMER_UPDATE_MEMBER(weekdays);
  TIMER_UPDATE_MEMBER(state);
  return updated;
}

bool Timer::isScheduled() const
{
  return state == PVR_TIMER_STATE_SCHEDULED
      || state == PVR_TIMER_STATE_RECORDING;
}

bool Timer::isRunning(time_t *now, std::string *channelName) const
{
  if (!isScheduled())
    return false;
  if (now && !(start <= *now && *now <= end))
    return false;
  if (channelName && channel->name != *channelName)
    return false;
  return true;
}

void Timers::GetTimerTypes(std::vector<PVR_TIMER_TYPE> &types)
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
  for (auto &recf : m_cli.GetRecordingFolders())
    groupValues.emplace_back(groupValues.size(), recf);

  /* One-shot manual (time and channel based) */
  types.emplace_back(TimerType(
      Timer::Type::MANUAL_ONCE,
      PVR_TIMER_TYPE_IS_MANUAL                 |
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP,
      "", /* Let Kodi generate the description */
      priorityValues, groupValues));

   /* Repeating manual (time and channel based) */
  types.emplace_back(TimerType(
      Timer::Type::MANUAL_REPEATING,
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
      priorityValues, groupValues));

   /* One-shot epg based */
  types.emplace_back(TimerType(
      Timer::Type::EPG_ONCE,
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE,
      "", /* Let Kodi generate the description */
      priorityValues));
}

Timer *Timers::GetTimer(std::function<bool (const Timer&)> func)
{
  for (auto &pair : m_timers)
  {
    if (func(pair.second))
      return &pair.second;
  }
  return nullptr;
}

unsigned int Timers::GetTimerCount()
{
  return m_timers.size();
}

void Timers::GetTimers(std::vector<PVR_TIMER> &timers)
{
  for (auto &pair : m_timers)
  {
    const Timer &timer = pair.second;
    PVR_TIMER tmr = { 0 };

    PVR_STRCPY(tmr.strTitle, timer.title.c_str());
    tmr.iClientIndex      = timer.id;
    tmr.iClientChannelUid = timer.channel->id;
    // Kodi requires starttime/endtime to exclude the margins
    tmr.startTime         = timer.start + timer.marginStart * 60;
    tmr.endTime           = timer.end   - timer.marginEnd   * 60;
    tmr.iMarginStart      = timer.marginStart;
    tmr.iMarginEnd        = timer.marginEnd;
    tmr.state             = timer.state;
    tmr.iTimerType        = timer.type;
    tmr.iPriority         = timer.priority;
    tmr.iRecordingGroup   = timer.recfolder + 1; /* first entry is automatic */
    tmr.firstDay          = (timer.weekdays != 0) ? tmr.startTime : 0;
    tmr.iWeekdays         = timer.weekdays;

    timers.emplace_back(tmr);
  }
}

Timers::Error Timers::DeleteTimer(const PVR_TIMER &timer)
{
  auto it = m_timers.find(timer.iClientIndex);
  if (it == m_timers.end())
    return TIMER_UNKNOWN;

  const Dvb::httpResponse &res = m_cli.GetFromAPI(
      "api/timerdelete.html?id=%u", it->second.backendId);
  if (!res.error)
    m_timers.erase(it);
  return (res.error) ? RESPONSE_ERROR : SUCCESS;
}

Timers::Error Timers::AddUpdateTimer(const PVR_TIMER &tmr, bool update)
{
  Timer timer;
  Error err = ParseTimerFrom(tmr, timer);
  if (err != SUCCESS)
    return err;

  unsigned int date = ((timer.start + UTCOffset()) / DAY_SECS) + DELPHI_DATE;
  struct tm *timeinfo;
  timeinfo = localtime(&timer.start);
  unsigned int start = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  timeinfo = localtime(&timer.end);
  unsigned int stop = timeinfo->tm_hour * 60 + timeinfo->tm_min;

  char repeat[8] = "-------";
  for (int i = 0; i < 7; ++i)
  {
    if (timer.weekdays & (1 << i))
      repeat[i] = 'T';
  }

  uint64_t channel = timer.channel->backendIds.front();
  const std::string &recfolder = (timer.recfolder == -1) ? "Auto"
      : m_cli.GetRecordingFolders().at(timer.recfolder);
  std::string params = StringUtils::Format("encoding=255&ch=%" PRIu64 "&dor=%u"
      "&start=%u&stop=%u&pre=%u&post=%u&prio=%d&days=%s&enable=%d",
      channel, date, start, stop, timer.marginStart, timer.marginEnd,
      timer.priority, repeat, (timer.state != PVR_TIMER_STATE_DISABLED));
  params += "&title="  + URLEncode(timer.title)
         +  "&folder=" + URLEncode(recfolder);
  if (update)
    params += StringUtils::Format("&id=%d", timer.backendId);

  const Dvb::httpResponse &res = m_cli.GetFromAPI("api/timer%s.html?%s",
      (update) ? "edit" : "add", params.c_str());
  return (res.error) ? RESPONSE_ERROR : SUCCESS;
}

Timers::Error Timers::ParseTimerFrom(const PVR_TIMER &tmr, Timer &timer)
{
  timer.start       = (tmr.startTime) ? tmr.startTime : time(nullptr);
  timer.end         = tmr.endTime;
  timer.marginStart = tmr.iMarginStart;
  timer.marginEnd   = tmr.iMarginEnd;
  timer.weekdays    = tmr.iWeekdays;
  timer.title       = tmr.strTitle;
  timer.priority    = tmr.iPriority;
  timer.state       = tmr.state;
  timer.type        = static_cast<Timer::Type>(tmr.iTimerType);

  // DMS API requires starttime/endtime to include the margins
  timer.start -= timer.marginStart * 60;
  timer.end   += timer.marginEnd   * 60;
  if (timer.start >= timer.end || timer.start - timer.end >= DAY_SECS)
    return TIMESPAN_OVERFLOW;

  if (tmr.iClientIndex != PVR_TIMER_NO_CLIENT_INDEX)
  {
    auto it = m_timers.find(tmr.iClientIndex);
    if (it == m_timers.end())
      return TIMER_UNKNOWN;
    timer.backendId = it->second.backendId;
  }

  // timers require an assigned channel
  timer.channel = m_cli.GetChannel(tmr.iClientChannelUid);
  if (!timer.channel)
    return CHANNEL_UNKNOWN;

  if (timer.type != Timer::Type::EPG_ONCE && tmr.iRecordingGroup > 0)
  {
    if (tmr.iRecordingGroup > m_cli.GetRecordingFolders().size())
      return RECFOLDER_UNKNOWN;
    timer.recfolder = tmr.iRecordingGroup - 1;
  }

  return SUCCESS;
}

Timers::Error Timers::RefreshTimers()
{
  // utf8=2 is correct here
  Dvb::httpResponse &&res = m_cli.GetFromAPI("api/timerlist.html?utf8=2");
  if (res.error)
    return RESPONSE_ERROR;

  TiXmlDocument doc;
  RemoveNullChars(res.content);
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse timers. Error: %s", doc.ErrorDesc());
    return GENERIC_PARSE_ERROR;
  }

  for (auto &timer : m_timers)
    timer.second.syncState = Timer::SyncState::NONE;

  std::vector<Timer> newtimers;
  unsigned int updated = 0, unchanged = 0;
  for (const TiXmlElement *xTimer = doc.RootElement()->FirstChildElement("Timer");
    xTimer; xTimer = xTimer->NextSiblingElement("Timer"))
  {
    Timer newtimer;
    if (ParseTimerFrom(xTimer, newtimer) != SUCCESS)
      continue;

    for (auto &entry : m_timers)
    {
      Timer &timer = entry.second;
      if (timer.guid != newtimer.guid)
        continue;

      if (timer.updateFrom(newtimer))
      {
        timer.syncState = newtimer.syncState = Timer::SyncState::UPDATED;
        ++updated;
      }
      else
      {
        timer.syncState = newtimer.syncState = Timer::SyncState::FOUND;
        ++unchanged;
      }
      break;
    }

    if (newtimer.syncState == Timer::SyncState::NEW)
      newtimers.push_back(newtimer);
  }

  unsigned int removed = 0;
  for (auto it = m_timers.begin(); it != m_timers.end();)
  {
    const Timer &timer = it->second;
    if (timer.syncState == Timer::SyncState::NONE)
    {
      XBMC->Log(LOG_DEBUG, "Removed timer '%s': id=%u", timer.title.c_str(),
          timer.id);
      it = m_timers.erase(it);
      ++removed;
    }
    else
      ++it;
  }

  unsigned int added = newtimers.size();
  for (auto &newtimer : newtimers)
  {
    newtimer.id = m_nextTimerId++;
    XBMC->Log(LOG_DEBUG, "New timer '%s': id=%u", newtimer.title.c_str(),
        newtimer.id);
    m_timers[newtimer.id] = newtimer;
  }

  XBMC->Log(LOG_DEBUG, "Timers update: removed=%u, unchanged=%u, updated=%u"
      ", added=%u", removed, unchanged, updated, added);
  return (removed || updated || added) ? SUCCESS : NO_TIMER_CHANGES;
}

Timers::Error Timers::ParseTimerFrom(const TiXmlElement *xml, Timer &timer)
{
  if (!XMLUtils::GetString(xml, "GUID", timer.guid))
    return GENERIC_PARSE_ERROR;

  XMLUtils::GetUInt(xml,   "ID",    timer.backendId);
  XMLUtils::GetString(xml, "Descr", timer.title);

  //TODO: DMS 2.0.4.17 adds the EPGID. do the search with that
  uint64_t backendId = 0;
  std::istringstream ss(xml->FirstChildElement("Channel")->Attribute("ID"));
  ss >> backendId;
  if (!backendId)
    return GENERIC_PARSE_ERROR;

  timer.channel = m_cli.GetChannel([&] (const DvbChannel *channel)
      {
        return (std::find(channel->backendIds.begin(),
              channel->backendIds.end(), backendId)
            != channel->backendIds.end());
      });
  if (!timer.channel)
  {
    XBMC->Log(LOG_NOTICE, "Found timer for unknown channel (backendid=%"
      PRIu64 "). Ignoring.", backendId);
    return CHANNEL_UNKNOWN;
  }

  std::string startDate = xml->Attribute("Date");
  startDate += xml->Attribute("Start");
  timer.start = ParseDateTime(startDate, false);
  timer.end   = timer.start + atoi(xml->Attribute("Dur")) * 60;

  xml->QueryUnsignedAttribute("PreEPG",  &timer.marginStart);
  xml->QueryUnsignedAttribute("PostEPG", &timer.marginEnd);
  xml->QueryIntAttribute("Priority", &timer.priority);

  timer.weekdays = PVR_WEEKDAY_NONE;
  const char *weekdays = xml->Attribute("Days");
  for (unsigned int j = 0; weekdays && weekdays[j] != '\0'; ++j)
  {
    if (weekdays[j] != '-')
      timer.weekdays += (1 << j);
  }
  if (timer.weekdays != PVR_WEEKDAY_NONE)
    timer.type = Timer::Type::MANUAL_REPEATING;

  // determine timer state
  int tmp = 0;
  timer.state = PVR_TIMER_STATE_SCHEDULED;
  if (XMLUtils::GetInt(xml, "Recording", tmp) && tmp != 0)
    timer.state = PVR_TIMER_STATE_RECORDING;
  else if (xml->QueryIntAttribute("Enabled", &tmp) == TIXML_SUCCESS && tmp == 0)
    timer.state = PVR_TIMER_STATE_DISABLED;
  if (timer.state != PVR_TIMER_STATE_DISABLED
      && XMLUtils::GetInt(xml, "Executeable", tmp) && tmp == 0)
    timer.state = PVR_TIMER_STATE_ERROR;

  std::string recfolder;
  if (XMLUtils::GetString(xml, "Folder", recfolder))
  {
    auto recfolders = m_cli.GetRecordingFolders();
    auto pos = std::distance(recfolders.begin(),
        std::find(recfolders.begin(), recfolders.end(), recfolder));
    if (pos < recfolders.size())
      timer.recfolder = pos;
  }

  return SUCCESS;
}
