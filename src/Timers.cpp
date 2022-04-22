/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Timers.h"
#include "client.h"
#include "DvbData.h"
#include "utilities/XMLUtils.h"

#include <algorithm>
#include <ctime>

#include <inttypes.h>
#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>

using namespace dvbviewer;
using namespace dvbviewer::utilities;
using namespace kodi::tools;

#define TIMER_UPDATE_MEMBER(member) \
  if (member != other.member) \
  { \
    member = other.member; \
    updated = true; \
  }
bool Timer::updateFrom(const Timer &other)
{
  bool updated = false;
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

bool Timer::isRunning(const std::time_t *now, const std::string *channelName) const
{
  if (!isScheduled())
    return false;
  if (now && !(start <= *now && *now <= end))
    return false;
  if (channelName && channel->name != *channelName)
    return false;
  return true;
}

void Timers::GetTimerTypes(std::vector< std::unique_ptr<kodi::addon::PVRTimerType> > &types)
{
  struct TimerType
    : kodi::addon::PVRTimerType
  {
    TimerType(unsigned int id, unsigned int attributes,
      const std::string &description = std::string(),
      const std::vector<kodi::addon::PVRTypeIntValue> &priorityValues
        = std::vector<kodi::addon::PVRTypeIntValue>(),
      const std::vector<kodi::addon::PVRTypeIntValue> &groupValues
        = std::vector<kodi::addon::PVRTypeIntValue>(),
      const std::vector<kodi::addon::PVRTypeIntValue> &deDupValues
        = std::vector<kodi::addon::PVRTypeIntValue>())
    {
      SetId(id);
      SetAttributes(attributes);
      SetDescription(description);

      if (!priorityValues.empty())
        SetPriorities(priorityValues, priorityValues[0].GetValue());
      if (!groupValues.empty())
        SetRecordingGroups(groupValues, groupValues[0].GetValue());
      if (!deDupValues.empty())
        SetPreventDuplicateEpisodes(deDupValues, deDupValues[0].GetValue());
    }
  };

  /* PVR_Timer.iPriority values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> priorityValues = {
    { -1,  kodi::addon::GetLocalizedString(30400) }, //default
    { 0,   kodi::addon::GetLocalizedString(30401) },
    { 25,  kodi::addon::GetLocalizedString(30402) },
    { 50,  kodi::addon::GetLocalizedString(30403) },
    { 75,  kodi::addon::GetLocalizedString(30404) },
    { 100, kodi::addon::GetLocalizedString(30405) },
  };

  /* PVR_Timer.iRecordingGroup values and presentation.*/
  std::vector<kodi::addon::PVRTypeIntValue> groupValues = {
    { 0, kodi::addon::GetLocalizedString(30410) }, //automatic
  };
  for (auto &recf : m_cli.GetRecordingFolders())
    groupValues.emplace_back(groupValues.size(), recf);

  /* One-shot manual (time and channel based) */
  types.emplace_back(std::unique_ptr<TimerType>(new TimerType(
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
      priorityValues, groupValues)));

   /* Repeating manual (time and channel based) */
  types.emplace_back(std::unique_ptr<TimerType>(new TimerType(
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
      priorityValues, groupValues)));

   /* One-shot epg based */
  types.emplace_back(std::unique_ptr<TimerType>(new TimerType(
      Timer::Type::EPG_ONCE,
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE,
      "", /* Let Kodi generate the description */
      priorityValues)));

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> deDupValues =
  {
    { AutoTimer::DeDup::DISABLED,             kodi::addon::GetLocalizedString(30430) },
    { AutoTimer::DeDup::CHECK_TITLE,          kodi::addon::GetLocalizedString(30431) },
    { AutoTimer::DeDup::CHECK_SUBTITLE,       kodi::addon::GetLocalizedString(30432) },
    { AutoTimer::DeDup::CHECK_TITLE_SUBTITLE, kodi::addon::GetLocalizedString(30433) },
  };

   /* epg auto search */
  types.emplace_back(std::unique_ptr<TimerType>(new TimerType(
      Timer::Type::EPG_AUTO_SEARCH,
      PVR_TIMER_TYPE_IS_REPEATING                |
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE     |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS           |
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL        |
      PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY          |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME           |
      PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME      |
      PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME        |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS           |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN   |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY           |
      PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH    |
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP    |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES,
      "", /* Let Kodi generate the description */
      priorityValues, groupValues, deDupValues)));
  types.back()->SetPreventDuplicateEpisodesDefault(
      AutoTimer::DeDup::CHECK_TITLE_SUBTITLE);

  /* One-shot created by epg auto search */
  types.emplace_back(std::unique_ptr<TimerType>(new TimerType(
      Timer::Type::EPG_AUTO_ONCE,
      PVR_TIMER_TYPE_IS_MANUAL                 |
      PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES     |
      PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE   |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS         |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME       |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME         |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY         |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP,
      kodi::addon::GetLocalizedString(30420),
      priorityValues, groupValues)));
}

Timers::Error Timers::RefreshAllTimers(bool &changes)
{
  bool changesAutotimers, changesTimers;
  /* refresh epg auto search first */
  Timers::Error err = Timers::SUCCESS;
  if (err == Timers::SUCCESS)
    err = RefreshAutoTimers(changesAutotimers);
  if (err == Timers::SUCCESS)
    err = RefreshTimers(changesTimers);
  if (err == Timers::SUCCESS)
    changes = changesAutotimers || changesTimers;
  return err;
}

template <typename T>
Timers::Error Timers::RefreshTimers(const char *name, const char *endpoint,
  const char *xmltag, std::map<unsigned int, T> &timerlist, bool &changes)
{
  std::unique_ptr<Dvb::httpResponse> res = m_cli.GetFromAPI(endpoint);
  if (res->error)
    return RESPONSE_ERROR;

  TiXmlDocument doc;
  RemoveNullChars(res->content);
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse %s list. Error: %s",
        name, doc.ErrorDesc());
    return GENERIC_PARSE_ERROR;
  }

  for (auto &timer : timerlist)
    timer.second.syncState = T::SyncState::NONE;

  std::vector<T> newTimers;
  std::size_t updated = 0, unchanged = 0;
  unsigned int pos = 0;
  for (const TiXmlElement *xTimer = doc.RootElement()->FirstChildElement(xmltag);
    xTimer; xTimer = xTimer->NextSiblingElement(xmltag), ++pos)
  {
    T newTimer;
    if (ParseTimerFrom(xTimer, pos, newTimer) != SUCCESS)
      continue;

    for (auto &entry : timerlist)
    {
      T &timer = entry.second;
      if (timer != newTimer)
        continue;

      if (timer.updateFrom(newTimer))
      {
        timer.syncState = newTimer.syncState = T::SyncState::UPDATED;
        ++updated;
        kodi::Log(ADDON_LOG_DEBUG, "timer %s updated", timer.title.c_str());
      }
      else
      {
        timer.syncState = newTimer.syncState = T::SyncState::FOUND;
        ++unchanged;
      }
      break;
    }

    if (newTimer.syncState == T::SyncState::NEW)
      newTimers.push_back(newTimer);
  }

  std::size_t removed = 0;
  for (auto it = timerlist.begin(); it != timerlist.end();)
  {
    const T &timer = it->second;
    if (timer.syncState == T::SyncState::NONE)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Removed %s '%s': id=%u",
          name, timer.title.c_str(), timer.id);
      it = timerlist.erase(it);
      ++removed;
    }
    else
      ++it;
  }

  std::size_t added = newTimers.size();
  for (auto &newTimer : newTimers)
  {
    newTimer.id = m_nextTimerId++;
    kodi::Log(ADDON_LOG_DEBUG, "New %s '%s': id=%u",
        name, newTimer.title.c_str(), newTimer.id);
    timerlist[newTimer.id] = newTimer;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s list update: removed=%lu, unchanged=%lu, updated=%lu"
      ", added=%lu", name, removed, unchanged, updated, added);
  changes = (removed || updated || added);
  return SUCCESS;
}

template <typename T>
T *Timers::GetTimer(std::function<bool (const T&)> func,
  std::map<unsigned int, T> &timerlist)
{
  for (auto &pair : timerlist)
  {
    if (func(pair.second))
      return &pair.second;
  }
  return nullptr;
}

/***************************************************************************
 * One-shot and repeating timers
 **************************************************************************/
Timer *Timers::GetTimer(std::function<bool (const Timer&)> func)
{
  return GetTimer<Timer>(func, m_timers);
}

std::size_t Timers::GetTimerCount()
{
  return m_timers.size();
}

void Timers::GetTimers(std::vector<kodi::addon::PVRTimer> &timers)
{
  for (auto &pair : m_timers)
  {
    const Timer &timer = pair.second;
    kodi::addon::PVRTimer tmr;

    tmr.SetTitle(timer.title);
    tmr.SetClientIndex(timer.id);
    tmr.SetClientChannelUid(timer.channel->id);
    // Kodi requires starttime/endtime to exclude the margins
    tmr.SetStartTime(timer.start + timer.marginStart * 60);
    tmr.SetEndTime(timer.end   - timer.marginEnd   * 60);
    tmr.SetMarginStart(timer.marginStart);
    tmr.SetMarginEnd(timer.marginEnd);
    tmr.SetState(timer.state);
    tmr.SetTimerType(timer.type);
    tmr.SetPriority(timer.priority);
    tmr.SetRecordingGroup(timer.recfolder + 1); /* first entry is automatic */
    tmr.SetFirstDay((timer.weekdays != 0) ? tmr.GetStartTime() : 0);
    tmr.SetWeekdays(timer.weekdays);

    if (timer.type == Timer::Type::MANUAL_ONCE && !timer.source.empty())
    {
      auto autotimer = GetAutoTimer([&](const AutoTimer &autotimer)
          { return autotimer.title == timer.source; });
      if (autotimer)
      {
        tmr.SetParentClientIndex(autotimer->id);
        tmr.SetTimerType(Timer::Type::EPG_AUTO_ONCE);
      }
    }

    timers.emplace_back(tmr);
  }
}

Timers::Error Timers::DeleteTimer(const kodi::addon::PVRTimer &timer)
{
  if (IsAutoTimer(timer))
    return DeleteAutoTimer(timer);

  auto it = m_timers.find(timer.GetClientIndex());
  if (it == m_timers.end())
    return TIMER_UNKNOWN;

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI(
      "api/timerdelete.html?id=%u", it->second.backendId);
  if (!res->error)
    m_timers.erase(it);
  return (res->error) ? RESPONSE_ERROR : SUCCESS;
}

Timers::Error Timers::AddUpdateTimer(const kodi::addon::PVRTimer &tmr, bool update)
{
  if (update && tmr.GetClientIndex() == PVR_TIMER_NO_CLIENT_INDEX)
    return TIMER_UNKNOWN;

  if (IsAutoTimer(tmr))
    return AddUpdateAutoTimer(tmr, update);

  Timer timer;
  Error err = ParseTimerFrom(tmr, timer);
  if (err != SUCCESS)
    return err;

  unsigned int date = ((timer.start + UTCOffset()) / DAY_SECS) + DELPHI_DATE;
  const std::tm &tm1 = localtime(timer.start);
  unsigned int start = tm1.tm_hour * 60 + tm1.tm_min;
  const std::tm &tm2 = localtime(timer.end);
  unsigned int stop  = tm2.tm_hour * 60 + tm2.tm_min;

  char repeat[8] = "-------";
  for (int i = 0; i < 7; ++i)
  {
    if (timer.weekdays & (1 << i))
      repeat[i] = 'T';
  }

  uint64_t channel = timer.channel->backendIds.front();
  const std::string &recfolder = (timer.recfolder == -1) ? "Auto"
      : m_cli.GetRecordingFolders().at(timer.recfolder);
  std::string params = StringUtils::Format("encoding=255&ch=%" PRIu64
      "&dor=%u&start=%u&stop=%u&pre=%u&post=%u&days=%s&enable=%d",
      channel, date, start, stop, timer.marginStart, timer.marginEnd,
      repeat, (timer.state != PVR_TIMER_STATE_DISABLED));
  params += "&title="  + URLEncode(timer.title)
         +  "&folder=" + URLEncode(recfolder);

  if (timer.priority >= 0 || update)
  {
    int priority = (timer.priority >= 0) ? timer.priority
        : m_cli.GetSettings().m_priority;
    params += "&prio=" + std::to_string(priority);
  }

  if (update)
    params += "&id=" + std::to_string(timer.backendId);

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI("api/timer%s.html?%s",
      (update) ? "edit" : "add", params.c_str());
  return (res->error) ? RESPONSE_ERROR : SUCCESS;
}

Timers::Error Timers::ParseTimerFrom(const kodi::addon::PVRTimer &tmr, Timer &timer)
{
  timer.start       = (tmr.GetStartTime()) ? tmr.GetStartTime() : std::time(nullptr);
  timer.end         = tmr.GetEndTime();
  timer.marginStart = tmr.GetMarginStart();
  timer.marginEnd   = tmr.GetMarginEnd();
  timer.weekdays    = tmr.GetWeekdays();
  timer.title       = tmr.GetTitle();
  timer.priority    = tmr.GetPriority();
  timer.state       = tmr.GetState();
  timer.type        = static_cast<Timer::Type>(tmr.GetTimerType());

  // DMS API requires starttime/endtime to include the margins
  timer.start -= timer.marginStart * 60;
  timer.end   += timer.marginEnd   * 60;
  if (timer.start >= timer.end || timer.start - timer.end >= DAY_SECS)
    return TIMESPAN_OVERFLOW;

  if (tmr.GetClientIndex() != PVR_TIMER_NO_CLIENT_INDEX)
  {
    auto it = m_timers.find(tmr.GetClientIndex());
    if (it == m_timers.end())
      return TIMER_UNKNOWN;
    timer.backendId = it->second.backendId;
  }

  // timers require an assigned channel
  timer.channel = m_cli.GetChannel(tmr.GetClientChannelUid());
  if (!timer.channel)
    return CHANNEL_UNKNOWN;

  if (timer.type != Timer::Type::EPG_ONCE && tmr.GetRecordingGroup() > 0)
  {
    if (tmr.GetRecordingGroup() > m_cli.GetRecordingFolders().size())
      return RECFOLDER_UNKNOWN;
    timer.recfolder = tmr.GetRecordingGroup() - 1;
  }

  return SUCCESS;
}

Timers::Error Timers::RefreshTimers(bool &changes)
{
  // utf8=2 is correct here
  return RefreshTimers<Timer>("timers", "api/timerlist.html?utf8=2", "Timer",
      m_timers, changes);
}

Timers::Error Timers::ParseTimerFrom(const TiXmlElement *xml, unsigned int pos,
    Timer &timer)
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

  timer.channel = m_cli.GetChannel([&](const DvbChannel *channel)
      {
        return (std::find(channel->backendIds.begin(),
              channel->backendIds.end(), backendId)
            != channel->backendIds.end());
      });
  if (!timer.channel)
  {
    kodi::Log(ADDON_LOG_INFO, "Found timer for unknown channel (backendid=%"
      PRIu64 "). Ignoring.", backendId);
    return CHANNEL_UNKNOWN;
  }

  std::string startDate = xml->Attribute("Date");
  startDate += xml->Attribute("Start");
  timer.start = ParseDateTime(startDate, false);
  timer.end   = timer.start + atoi(xml->Attribute("Dur")) * 60;

  if (const auto *stat = xml->FirstChildElement("Recordstat"))
  {
    startDate = stat->Attribute("StartTime");
    timer.realStart = ParseDateTime(startDate, false);
  }

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
    if (pos >= 0 && pos < static_cast<std::ptrdiff_t>(recfolders.size()))
      timer.recfolder = pos;
  }

  std::string source;
  if (XMLUtils::GetString(xml, "Source", source)
      && StringUtils::StartsWith(source, "Search:"))
    timer.source = source.substr(strlen("Search:"));

  return SUCCESS;
}

/***************************************************************************
 * EPG auto timers
 **************************************************************************/
bool AutoTimer::updateFrom(const AutoTimer &other)
{
  bool updated = Timer::updateFrom(other);
  TIMER_UPDATE_MEMBER(searchPhrase);
  TIMER_UPDATE_MEMBER(searchFulltext);
  TIMER_UPDATE_MEMBER(deDup);
  // all three values are based on other values we already checked. so just copy
  startAnyTime = other.startAnyTime;
  endAnyTime   = other.endAnyTime;
  firstDay     = other.firstDay;
  backendId    = other.backendId; // always update the backendid
  return updated;
}

void AutoTimer::CalcGUID()
{
  guid = title + "/" + searchPhrase;
}

bool Timers::IsAutoTimer(const kodi::addon::PVRTimer &timer)
{
  return timer.GetTimerType() == Timer::Type::EPG_AUTO_SEARCH;
}

AutoTimer *Timers::GetAutoTimer(std::function<bool (const AutoTimer&)> func)
{
  return GetTimer<AutoTimer>(func, m_autotimers);
}

std::size_t Timers::GetAutoTimerCount()
{
  return m_autotimers.size();
}

void Timers::GetAutoTimers(std::vector<kodi::addon::PVRTimer> &timers)
{
  for (auto &pair : m_autotimers)
  {
    const AutoTimer &timer = pair.second;
    kodi::addon::PVRTimer tmr;

    tmr.SetTitle(timer.title);
    tmr.SetClientIndex(timer.id);
    tmr.SetClientChannelUid(timer.channel ? timer.channel->id : PVR_TIMER_ANY_CHANNEL);
    tmr.SetStartTime(timer.start);
    tmr.SetEndTime(timer.end);
    tmr.SetStartAnyTime(timer.startAnyTime);
    tmr.SetEndAnyTime(timer.endAnyTime);
    tmr.SetMarginStart(timer.marginStart);
    tmr.SetMarginEnd(timer.marginEnd);
    tmr.SetState(timer.state);
    tmr.SetTimerType(timer.type);
    tmr.SetPriority(timer.priority);
    tmr.SetRecordingGroup(timer.recfolder + 1); /* first entry is automatic */
    tmr.SetFirstDay(timer.firstDay);
    tmr.SetWeekdays(timer.weekdays);

    tmr.SetEPGSearchString(timer.searchPhrase);
    tmr.SetFullTextEpgSearch(timer.searchFulltext);
    tmr.SetPreventDuplicateEpisodes(timer.deDup);

    timers.emplace_back(tmr);
  }
}

Timers::Error Timers::DeleteAutoTimer(const kodi::addon::PVRTimer &timer)
{
  auto it = m_autotimers.find(timer.GetClientIndex());
  if (it == m_autotimers.end())
    return TIMER_UNKNOWN;

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI(
      "api/searchdelete.html?name=%s", URLEncode(it->second.title).c_str());
  if (!res->error)
    m_autotimers.erase(it);
  return (res->error) ? RESPONSE_ERROR : SUCCESS;
}

Timers::Error Timers::AddUpdateAutoTimer(const kodi::addon::PVRTimer &tmr, bool update)
{
  AutoTimer timer;
  Error err = ParseTimerFrom(tmr, timer);
  if (err != SUCCESS)
    return err;

  const std::string &recfolder = (timer.recfolder == -1) ? "Auto"
      : m_cli.GetRecordingFolders().at(timer.recfolder);
  std::string params = StringUtils::Format(
      "EPGBefore=%u&EPGAfter=%u&Days=%u"
      "&SearchFields=%d&AutoRecording=%d&CheckRecTitle=%d&CheckRecSubtitle=%d",
      timer.marginStart, timer.marginEnd, timer.weekdays,
      timer.searchFulltext ? 7 : 3, (timer.state != PVR_TIMER_STATE_DISABLED),
      timer.deDup & AutoTimer::DeDup::CHECK_TITLE,
      timer.deDup & AutoTimer::DeDup::CHECK_SUBTITLE);
  params += "&SearchPhrase="    + URLEncode(timer.searchPhrase)
         +  "&Name="            + URLEncode(timer.title)
         +  "&Series="          + URLEncode(timer.title)
         +  "&RecordingFolder=" + URLEncode(recfolder);

  if (timer.priority >= 0 || update)
  {
    int priority = (timer.priority >= 0) ? timer.priority
        : m_cli.GetSettings().m_priority;
    params += "&Priority=" + std::to_string(priority);
  }

  if (!update)
  {
    params += "&CheckTimer=1"; // we always enable "check against existing timers"
    params += "&AfterProcessAction=" + URLEncode(m_cli.GetSettings().m_recordingTask);
  }

  params += "&Channels=";
  if (timer.channel)
    params += std::to_string(timer.channel->epgId);

  //TODO: use std::put_time, not available in gcc < 5
  params += "&StartTime=";
  if (!timer.startAnyTime)
  {
    const std::tm &tm = localtime(timer.start);
    params += StringUtils::Format("%02d:%02d", tm.tm_hour, tm.tm_min);
  }

  params += "&EndTime=";
  if (!timer.endAnyTime)
  {
    const std::tm &tm = localtime(timer.end);
    params += StringUtils::Format("%02d:%02d", tm.tm_hour, tm.tm_min);
  }

  params += "&StartDate=";
  if (timer.firstDay)
  {
    const std::tm &tm = localtime(timer.firstDay);
    params += StringUtils::Format("%02d.%02d.%04d", tm.tm_mday,
        tm.tm_mon + 1, tm.tm_year + 1900);
  }

  /* updating the name only works by using the index */
  AutoTimer *oldTimer = (update) ? &m_autotimers.at(tmr.GetClientIndex()) : nullptr;
  if (update && timer.title != oldTimer->title)
    params += "&id=" + std::to_string(timer.backendId);

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI("api/search%s.html?%s",
      (update) ? "edit" : "add", params.c_str());
  if (res->error)
    return RESPONSE_ERROR;

  /* make sure we can recognize the timer during sync */
  if (update && timer != *oldTimer)
    oldTimer->guid = timer.guid;

  std::unique_ptr<const Dvb::httpResponse> res2 = m_cli.GetFromAPI(
    "api/tasks.html?action=AutoTimer");
  if (res2->error)
    return RESPONSE_ERROR;

  return SUCCESS;
}

Timers::Error Timers::ParseTimerFrom(const kodi::addon::PVRTimer &tmr, AutoTimer &timer)
{
  timer.start          = (tmr.GetStartAnyTime()) ? 0 : tmr.GetStartTime();
  timer.end            = (tmr.GetEndAnyTime())   ? 0 : tmr.GetEndTime();
  timer.marginStart    = tmr.GetMarginStart();
  timer.marginEnd      = tmr.GetMarginEnd();
  timer.firstDay       = tmr.GetFirstDay();
  timer.weekdays       = tmr.GetWeekdays();
  timer.title          = tmr.GetTitle();
  timer.priority       = tmr.GetPriority();
  timer.state          = tmr.GetState();
  timer.type           = static_cast<Timer::Type>(tmr.GetTimerType());
  timer.searchPhrase   = tmr.GetEPGSearchString();
  timer.searchFulltext = tmr.GetFullTextEpgSearch();
  timer.startAnyTime   = tmr.GetStartAnyTime();
  timer.endAnyTime     = tmr.GetEndAnyTime();
  timer.deDup          = static_cast<AutoTimer::DeDup>(tmr.GetPreventDuplicateEpisodes());

  if (timer.searchPhrase.empty())
    return EMPTY_SEARCH_PHRASE;

  if (tmr.GetClientIndex() != PVR_TIMER_NO_CLIENT_INDEX)
  {
    auto it = m_autotimers.find(tmr.GetClientIndex());
    if (it == m_autotimers.end())
      return TIMER_UNKNOWN;
    timer.backendId = it->second.backendId;
  }

  if (tmr.GetClientChannelUid() != PVR_TIMER_ANY_CHANNEL)
  {
    timer.channel = m_cli.GetChannel(tmr.GetClientChannelUid());
    if (!timer.channel)
      return CHANNEL_UNKNOWN;
  }

  if (tmr.GetRecordingGroup() > 0)
  {
    if (tmr.GetRecordingGroup() > m_cli.GetRecordingFolders().size())
      return RECFOLDER_UNKNOWN;
    timer.recfolder = tmr.GetRecordingGroup() - 1;
  }

  timer.CalcGUID();
  return SUCCESS;
}

Timers::Error Timers::RefreshAutoTimers(bool &changes)
{
  return RefreshTimers<AutoTimer>("EPG auto timers",
      "api/searchlist.html", "Search", m_autotimers, changes);
}

Timers::Error Timers::ParseTimerFrom(const TiXmlElement *xml, unsigned int pos,
    AutoTimer &timer)
{
  if (xml->QueryStringAttribute("Name", &timer.title) != TIXML_SUCCESS)
    return GENERIC_PARSE_ERROR;

  timer.type = Timer::Type::EPG_AUTO_SEARCH;
  timer.backendId = pos;
  XMLUtils::GetUInt(xml, "EPGBefore", timer.marginStart);
  XMLUtils::GetUInt(xml, "EPGAfter",  timer.marginEnd);
  XMLUtils::GetUInt(xml, "Days",      timer.weekdays);
  XMLUtils::GetInt(xml,  "Priority",  timer.priority);

  int tmp = 0;
  timer.state = PVR_TIMER_STATE_SCHEDULED;
  if (xml->QueryIntAttribute("AutoRecording", &tmp) == TIXML_SUCCESS && tmp == 0)
    timer.state = PVR_TIMER_STATE_DISABLED;

  XMLUtils::GetString(xml, "SearchPhrase", timer.searchPhrase);
  if (XMLUtils::GetInt(xml, "SearchFields", tmp) && (tmp & 0x04))
    timer.searchFulltext = true;

  if (xml->QueryIntAttribute("CheckRecTitle", &tmp) == TIXML_SUCCESS && tmp != 0)
    timer.deDup |= AutoTimer::DeDup::CHECK_TITLE;
  if (xml->QueryIntAttribute("CheckRecSubTitle", &tmp) == TIXML_SUCCESS && tmp != 0)
    timer.deDup |= AutoTimer::DeDup::CHECK_SUBTITLE;

  // DMS supports specifying multiple channels whereas Kodi only a single
  // channel. So if multiple channels are defined we show ANY_CHANNEL in Kodi.
  if (const TiXmlElement *xChannels = xml->FirstChildElement("Channels"))
  {
    const TiXmlElement *xChannel1 = xChannels->FirstChildElement("Channel");
    const TiXmlElement *xChannel2 = xChannel1->NextSiblingElement("Channel");
    if (xChannel1 && !xChannel2)
    {
      uint64_t backendId = 0;
      std::istringstream ss(xChannel1->GetText());
      ss >> backendId;
      if (!backendId)
        return GENERIC_PARSE_ERROR;

      timer.channel = m_cli.GetChannel([&](const DvbChannel *channel)
          { return channel->epgId == backendId; });
      if (!timer.channel)
      {
        kodi::Log(ADDON_LOG_INFO, "Found timer for unknown channel (backendid=%"
          PRIu64 "). Ignoring.", backendId);
        return CHANNEL_UNKNOWN;
      }
    }
  }

  std::string tmpstr;
  std::tm timeinfo = std::tm();
  if (XMLUtils::GetString(xml, "StartDate", tmpstr))
    ParseDate(tmpstr, timeinfo);
  else
    timeinfo = localtime();

  XMLUtils::GetString(xml, "StartTime", tmpstr);
  ParseTime(tmpstr, timeinfo);
  timeinfo.tm_sec = 0;
  timer.firstDay = timer.start = std::mktime(&timeinfo);
  timer.startAnyTime = (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0);

  XMLUtils::GetString(xml, "EndTime", tmpstr);
  ParseTime(tmpstr, timeinfo);
  timeinfo.tm_sec = 0;
  timer.end = std::mktime(&timeinfo);
  timer.endAnyTime = (timeinfo.tm_hour == 23 && timeinfo.tm_min == 59);

  std::string recfolder;
  if (XMLUtils::GetString(xml, "RecordingFolder", recfolder))
  {
    auto recfolders = m_cli.GetRecordingFolders();
    auto pos = std::distance(recfolders.begin(),
        std::find(recfolders.begin(), recfolders.end(), recfolder));
    if (pos >= 0 && pos < static_cast<std::ptrdiff_t>(recfolders.size()))
      timer.recfolder = pos;
  }

  timer.CalcGUID();
  return SUCCESS;
}

/***************************************************************************
 * Helpers
 **************************************************************************/
void Timers::ParseDate(const std::string &date, std::tm &timeinfo)
{
  std::sscanf(date.c_str(), "%02d.%02d.%04d", &timeinfo.tm_mday,
      &timeinfo.tm_mon, &timeinfo.tm_year);
  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;
}

void Timers::ParseTime(const std::string &time, std::tm &timeinfo)
{
  std::sscanf(time.c_str(), "%02d:%02d:%02d", &timeinfo.tm_hour,
      &timeinfo.tm_min, &timeinfo.tm_sec);
}
