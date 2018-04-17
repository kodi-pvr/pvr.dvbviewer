#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>

#include "libXBMC_pvr.h"
#include "tinyxml.h"

class Dvb;
class DvbChannel;

namespace dvbviewer
{

class Timer
{
public:
  enum Type
    : unsigned int // same type as PVR_TIMER_TYPE.iId
  {
    MANUAL_ONCE      = PVR_TIMER_TYPE_NONE + 1,
    MANUAL_REPEATING = PVR_TIMER_TYPE_NONE + 2,
    EPG_ONCE         = PVR_TIMER_TYPE_NONE + 3,
  };

  enum class SyncState
    : uint8_t
  {
    NONE,
    NEW,
    FOUND,
    UPDATED
  };

  Timer();
  bool updateFrom(const Timer &source);
  bool isScheduled() const;
  bool isRunning(time_t *now, std::string *channelName = nullptr) const;

public:
  /*!< @brief Unique id passed to Kodi as PVR_TIMER.iClientIndex.
   * Starts at 1 and increases by each new timer. Never decreases.
   */
  unsigned int id;
  /*!< @brief Unique guid provided by backend. Unique every time */
  std::string guid;
  /*!< @brief Timer id on backend. Unique at a time */
  unsigned int backendId;

  Type        type;
  DvbChannel  *channel; //TODO: convert to shared_ptr

  int         priority;
  std::string title;
  // index to recfolders or -1 for automatic
  int         recfolder; //TODO add method for resyncing

  // start/end include the margins
  time_t       start;
  time_t       end;
  unsigned int marginStart;
  unsigned int marginEnd;
  unsigned int weekdays;

  PVR_TIMER_STATE state;
  SyncState syncState;
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
    RESPONSE_ERROR,
    NO_TIMER_CHANGES,
  };

  Timers(Dvb &cli)
    : m_cli(cli), m_nextTimerId(1)
  {};
  ~Timers()
  {};

  void GetTimerTypes(std::vector<PVR_TIMER_TYPE> &types);

  Timer *GetTimer(std::function<bool (const Timer&)> func);
  unsigned int GetTimerCount();
  void GetTimers(std::vector<PVR_TIMER> &timers);

  Error AddUpdateTimer(const PVR_TIMER &timer, bool update);
  Error DeleteTimer(const PVR_TIMER &timer);

  Error RefreshTimers();

private:
  Error ParseTimerFrom(const PVR_TIMER &tmr, Timer &timer);
  Error ParseTimerFrom(const TiXmlElement *xml, Timer &timer);

  Dvb &m_cli;
  /*!< @brief map of [timer.id, timer] pairs */
  std::map<unsigned int, Timer> m_timers;
  unsigned int m_nextTimerId;
};

} //namespace dvbviewer
