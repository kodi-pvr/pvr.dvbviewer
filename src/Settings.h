/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

#include <string>

#define DEFAULT_HOST         "127.0.0.1"
#define DEFAULT_WEB_PORT     8089
#define DEFAULT_TSBUFFERPATH "special://userdata/addon_data/pvr.dvbviewer"

namespace dvbviewer ATTR_DLL_LOCAL
{

/* forward declaration */
class Dvb;

enum class Timeshift
  : int // same type as addon settings
{
  OFF = 0,
  ON_PLAYBACK,
  ON_PAUSE
};

enum class RecordGrouping
  : int // same type as addon settings
{
  DISABLED = 0,
  BY_DIRECTORY,
  BY_DATE,
  BY_FIRST_LETTER,
  BY_TV_CHANNEL,
  BY_SERIES,
  BY_TITLE
};

enum class PrependOutline
  : int // same type as addon settings
{
  NEVER = 0,
  IN_EPG,
  IN_RECORDINGS,
  ALWAYS
};

enum class Transcoding
  : int // same type as addon settings
{
  OFF = 0,
  TS,
  WEBM,
  FLV,
};

class Settings
{
  struct EdlSettings
  {
    bool enabled;
    int padding_start, padding_stop;
  };

public:
  Settings();

  void ReadFromKodi();
  bool ReadFromBackend(Dvb &cli);
  ADDON_STATUS SetValue(const std::string name, const kodi::addon::CSettingValue& value);

  bool IsTimeshiftBufferPathValid() const;
  std::string BaseURL(bool credentials = true) const;

private:
  void ResetBackendSettings();

public:
  /* PVR settings */
  std::string    m_hostname = DEFAULT_HOST;
  int            m_webPort  = DEFAULT_WEB_PORT;
  std::string    m_username;
  std::string    m_password;
  int            m_profileId = 0;
  bool           m_useWoL = false;
  std::string    m_mac;
  bool           m_useFavourites     = false;
  bool           m_useFavouritesFile = false;
  std::string    m_favouritesFile;
  Timeshift      m_timeshift            = Timeshift::OFF;
  std::string    m_timeshiftBufferPath  = DEFAULT_TSBUFFERPATH;
  RecordGrouping m_groupRecordings      = RecordGrouping::DISABLED;
  EdlSettings    m_edl                  = { false, 0, 0 };
  PrependOutline m_prependOutline       = PrependOutline::IN_EPG;
  bool           m_lowPerformance       = false;
  int            m_readTimeout          = 0;
  int            m_streamReadChunkSize  = 0;
  Transcoding    m_transcoding          = Transcoding::OFF;
  Transcoding    m_recordingTranscoding = Transcoding::OFF;
  std::string    m_transcodingParams;
  std::string    m_recordingTranscodingParams;

  /* settings fetched from backend */
  int m_priority;
  std::string m_recordingTask;
};

} //namespace dvbviewer
