/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/addon-instance/pvr/Timers.h>
#include <tinyxml.h>

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <ctime>
#include <type_traits>

namespace dvbviewer ATTR_DLL_LOCAL
{

/* forward declaration */
class Dvb;
class DvbChannel;

class Timer
{
public:
  enum Type
    : unsigned int // same type as PVR_TIMER_TYPE.iId
  {
    MANUAL_ONCE      = PVR_TIMER_TYPE_NONE + 1,
    MANUAL_REPEATING = PVR_TIMER_TYPE_NONE + 2,
    EPG_ONCE         = PVR_TIMER_TYPE_NONE + 3,
    EPG_AUTO_SEARCH  = PVR_TIMER_TYPE_NONE + 4,
    EPG_AUTO_ONCE    = PVR_TIMER_TYPE_NONE + 5,
  };

  enum class SyncState
    : uint8_t
  {
    NONE,
    NEW,
    FOUND,
    UPDATED,
  };

  Timer() = default;
  bool updateFrom(const Timer &other);
  bool isScheduled() const;
  bool isRunning(const std::time_t *now, const std::string *channelName = nullptr) const;

  bool operator!=(const Timer &other) const
  {
    return guid != other.guid;
  };

public:
  /*!< @brief Unique id passed to Kodi as PVR_TIMER.iClientIndex.
   * Starts at 1 and increases by each new timer. Never decreases.
   */
  unsigned int id;
  /*!< @brief Unique guid provided by backend. Unique every time */
  std::string guid;
  /*!< @brief Timer id on backend. Unique at a time */
  unsigned int backendId;

  Type        type = Type::MANUAL_ONCE;
  DvbChannel  *channel = nullptr; //TODO: convert to shared_ptr

  int         priority = 0;
  std::string title;
  // index to recfolders or -1 for automatic
  int         recfolder = -1; //TODO add method for resyncing

  // start/end include the margins
  std::time_t  start;
  std::time_t  end;
  unsigned int marginStart = 0;
  unsigned int marginEnd = 0;
  unsigned int weekdays;
  std::time_t  realStart = 0; // real start time. only available if timer is running.
                              // might differ from start if added in the middle of a show

  std::string source; // holds autotimer.title if created by an autotimer

  PVR_TIMER_STATE state;
  SyncState syncState = SyncState::NEW;
};

class AutoTimer
  : public Timer
{
public:
  enum DeDup
    : unsigned int  // same type as PVR_TIMER_TYPE.iPreventDuplicateEpisodes
  {
    DISABLED             = 0,
    CHECK_TITLE          = 1,
    CHECK_SUBTITLE       = 2,
    CHECK_TITLE_SUBTITLE = CHECK_TITLE | CHECK_SUBTITLE,
  };

public:
  AutoTimer() = default;
  bool updateFrom(const AutoTimer &other);
  void CalcGUID();

public:
  std::time_t firstDay = 0;
  std::string searchPhrase;
  bool searchFulltext = false;
  bool startAnyTime = false;
  bool endAnyTime   = false;
  std::underlying_type<DeDup>::type deDup = DeDup::DISABLED;
};

class Timers
{
public:
  enum Error
  {
    SUCCESS,
    GENERIC_PARSE_ERROR,
    TIMESPAN_OVERFLOW,
    TIMER_UNKNOWN,
    CHANNEL_UNKNOWN,
    RECFOLDER_UNKNOWN,
    EMPTY_SEARCH_PHRASE,
    RESPONSE_ERROR,
  };

  Timers(Dvb &cli)
    : m_cli(cli)
  {};

  void GetTimerTypes(std::vector< std::unique_ptr<kodi::addon::PVRTimerType> > &types);
  Error RefreshAllTimers(bool &changes);

  Timer *GetTimer(std::function<bool (const Timer&)> func);
  AutoTimer *GetAutoTimer(std::function<bool (const AutoTimer&)> func);

  std::size_t GetTimerCount();
  std::size_t GetAutoTimerCount();

  void GetTimers(std::vector<kodi::addon::PVRTimer> &timers);
  void GetAutoTimers(std::vector<kodi::addon::PVRTimer> &timers);

  Error AddUpdateTimer(const kodi::addon::PVRTimer &timer, bool update);
  Error AddUpdateAutoTimer(const kodi::addon::PVRTimer &timer, bool update);

  Error DeleteTimer(const kodi::addon::PVRTimer &timer);
  Error DeleteAutoTimer(const kodi::addon::PVRTimer &timer);

private:
  template <typename T>
  T *GetTimer(std::function<bool (const T&)> func,
      std::map<unsigned int, T> &timerlist);

  template <typename T>
  Timers::Error RefreshTimers(const char *name, const char *endpoint,
      const char *xmltag, std::map<unsigned int, T> &timerlist, bool &changes);
  Error RefreshTimers(bool &changes);
  Error RefreshAutoTimers(bool &changes);
  bool IsAutoTimer(const kodi::addon::PVRTimer &timer);

  Error ParseTimerFrom(const kodi::addon::PVRTimer &tmr, Timer &timer);
  Error ParseTimerFrom(const TiXmlElement *xml, unsigned int pos, Timer &timer);
  Error ParseTimerFrom(const kodi::addon::PVRTimer &tmr, AutoTimer &timer);
  Error ParseTimerFrom(const TiXmlElement *xml, unsigned int pos, AutoTimer &timer);

  void ParseDate(const std::string &date, std::tm &timeinfo);
  void ParseTime(const std::string &time, std::tm &timeinfo);

private:
  Dvb &m_cli;
  /*!< @brief map of [timer.id, timer] pairs */
  std::map<unsigned int, Timer> m_timers;
  std::map<unsigned int, AutoTimer> m_autotimers;
  unsigned int m_nextTimerId = 1;
};

} //namespace dvbviewer
