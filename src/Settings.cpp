/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Settings.h"
#include "client.h"
#include "DvbData.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>
#include <tinyxml.h>

using namespace dvbviewer;
using namespace kodi::tools;

Settings::Settings()
{
  ResetBackendSettings();
}

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromKodi()
{
  if (!kodi::addon::CheckSettingString("host", m_hostname))
    m_hostname = DEFAULT_HOST;

  if (!kodi::addon::CheckSettingInt("webport", m_webPort))
    m_webPort = DEFAULT_WEB_PORT;

  m_username = kodi::addon::GetSettingString("user");
  m_password = kodi::addon::GetSettingString("pass");
  m_profileId = kodi::addon::GetSettingInt("profileid");
  m_useWoL = kodi::addon::GetSettingBoolean("usewol");
  m_mac = kodi::addon::GetSettingString("mac");
  m_useFavourites = kodi::addon::GetSettingBoolean("usefavourites");
  m_useFavouritesFile = kodi::addon::GetSettingBoolean("usefavouritesfile");
  m_favouritesFile = kodi::addon::GetSettingString("favouritesfile");
  m_groupRecordings = kodi::addon::GetSettingEnum<RecordGrouping>("grouprecordings");
  m_timeshift = kodi::addon::GetSettingEnum<Timeshift>("timeshift");
  m_timeshiftBufferPath = kodi::addon::GetSettingString("timeshiftpath");

  m_edl.enabled = kodi::addon::GetSettingBoolean("edl");
  m_edl.padding_start = kodi::addon::GetSettingInt("edl_padding_start");
  m_edl.padding_stop = kodi::addon::GetSettingInt("edl_padding_stop");

  m_prependOutline = kodi::addon::GetSettingEnum<PrependOutline>("prependoutline");
  m_lowPerformance = kodi::addon::GetSettingBoolean("lowperformance");
  m_readTimeout = kodi::addon::GetSettingInt("readtimeout");
  m_streamReadChunkSize = kodi::addon::GetSettingInt("stream_readchunksize");
  m_transcoding = kodi::addon::GetSettingEnum<Transcoding>("transcoding");
  m_recordingTranscoding = kodi::addon::GetSettingEnum<Transcoding>("recording_transcoding");

  if (kodi::addon::CheckSettingString("transcodingparams", m_transcodingParams))
    StringUtils::Replace(m_transcodingParams, " ", "+");

  if (kodi::addon::CheckSettingString("recording_transcodingparams", m_recordingTranscodingParams))
    StringUtils::Replace(m_recordingTranscodingParams, " ", "+");

  /* Log the current settings for debugging purposes */
  /* general tab */
  kodi::Log(ADDON_LOG_DEBUG, "DVBViewer Addon Configuration options");
  kodi::Log(ADDON_LOG_DEBUG, "Backend: http://%s:%d/", m_hostname.c_str(), m_webPort);
  if (!m_username.empty() && !m_password.empty())
    kodi::Log(ADDON_LOG_DEBUG, "Login credentials: %s/PASSWORD", m_username.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "Profile ID: %d", m_profileId);
  if (m_useWoL)
    kodi::Log(ADDON_LOG_DEBUG, "WoL MAC: %s", m_mac.c_str());

  /* livetv tab */
  kodi::Log(ADDON_LOG_DEBUG, "Use favourites: %s", (m_useFavourites) ? "yes" : "no");
  if (m_useFavouritesFile)
    kodi::Log(ADDON_LOG_DEBUG, "Favourites file: %s", m_favouritesFile.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "Timeshift mode: %d", m_timeshift);
  if (m_timeshift != Timeshift::OFF)
    kodi::Log(ADDON_LOG_DEBUG, "Timeshift buffer path: %s", m_timeshiftBufferPath.c_str());
  if (m_transcoding != Transcoding::OFF)
    kodi::Log(ADDON_LOG_DEBUG, "Transcoding: format=%d params=%s",
        m_transcoding, m_transcodingParams.c_str());

  /* recordings tab */
  if (m_groupRecordings != RecordGrouping::DISABLED)
    kodi::Log(ADDON_LOG_DEBUG, "Group recordings: %d", m_groupRecordings);
  if (m_edl.enabled)
    kodi::Log(ADDON_LOG_DEBUG, "EDL enabled. Padding: start=%d stop=%d",
      m_edl.padding_start, m_edl.padding_stop);
  if (m_recordingTranscoding != Transcoding::OFF)
    kodi::Log(ADDON_LOG_DEBUG, "Recording transcoding: format=%d, params=%s",
      m_recordingTranscoding, m_recordingTranscodingParams.c_str());

  /* advanced tab */
  if (m_prependOutline != PrependOutline::NEVER)
    kodi::Log(ADDON_LOG_DEBUG, "Prepend outline: %d", m_prependOutline);
  kodi::Log(ADDON_LOG_DEBUG, "Low performance mode: %s", (m_lowPerformance) ? "yes" : "no");
  if (m_readTimeout)
    kodi::Log(ADDON_LOG_DEBUG, "Custom connection/read timeout: %d", m_readTimeout);
  if (m_streamReadChunkSize)
    kodi::Log(ADDON_LOG_DEBUG, "Stream read chunk size: %d kb", m_streamReadChunkSize);
}

ADDON_STATUS Settings::SetValue(const std::string name, const kodi::addon::CSettingValue& value)
{
  if (name == "host")
  {
    if (m_hostname.compare(value.GetString()) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "webport")
  {
    if (m_webPort != value.GetInt())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "user")
  {
    if (m_username.compare(value.GetString()) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "pass")
  {
    if (m_password.compare(value.GetString()) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "profileid")
  {
    if (m_profileId != value.GetInt())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "usewol")
  {
    m_useWoL = value.GetBoolean();
  }
  else if (name == "mac")
  {
    m_mac = value.GetString();
  }
  else if (name == "usefavourites")
  {
    if (m_useFavourites != value.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "usefavouritesfile")
  {
    if (m_useFavouritesFile != value.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "favouritesfile")
  {
    if (m_favouritesFile.compare(value.GetString()) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "timeshift")
  {
    Timeshift newValue = value.GetEnum<Timeshift>();
    if (m_timeshift != newValue)
    {
      kodi::Log(ADDON_LOG_DEBUG, "%s: Changed setting '%s' from '%d' to '%d'",
          __FUNCTION__, name.c_str(), m_timeshift, newValue);
      m_timeshift = newValue;
    }
  }
  else if (name == "timeshiftpath")
  {
    std::string newValue = value.GetString();
    if (m_timeshiftBufferPath != newValue && !newValue.empty())
    {
      kodi::Log(ADDON_LOG_DEBUG, "%s: Changed setting '%s' from '%s' to '%s'",
          __FUNCTION__, name.c_str(), m_timeshiftBufferPath.c_str(),
          newValue.c_str());
      m_timeshiftBufferPath = newValue;
    }
  }
  else if (name == "edl")
  {
    m_edl.enabled = value.GetBoolean();
  }
  else if (name == "edl_padding_start")
  {
    m_edl.padding_start = value.GetInt();
  }
  else if (name == "edl_padding_stop")
  {
    m_edl.padding_stop = value.GetInt();
  }
  else if (name == "usefavouritesfile")
  {
    if (m_useFavouritesFile != value.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "prependoutline")
  {
    PrependOutline newValue = value.GetEnum<PrependOutline>();
    if (m_prependOutline != newValue)
    {
      m_prependOutline = newValue;
      // EPG view seems cached, so TriggerEpgUpdate isn't reliable
      // also if PVR is currently disabled we don't get notified at all
      kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30507));
    }
  }
  else if (name == "lowperformance")
  {
    if (m_lowPerformance != value.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "readtimeout")
  {
    m_readTimeout = value.GetInt();
  }
  else if (name == "stream_readchunksize")
  {
    m_streamReadChunkSize = value.GetInt();
  }
  else if (name == "transcoding")
  {
    m_transcoding = value.GetEnum<Transcoding>();
  }
  else if (name == "recording_transcoding")
  {
    m_recordingTranscoding = value.GetEnum<Transcoding>();
  }
  else if (name == "transcodingparams")
  {
    m_transcodingParams = value.GetString();
    StringUtils::Replace(m_transcodingParams, " ", "+");
  }
  else if (name == "recording_transcodingparams")
  {
    m_recordingTranscodingParams = value.GetString();
    StringUtils::Replace(m_recordingTranscodingParams, " ", "+");
  }
  return ADDON_STATUS_OK;
}

bool Settings::IsTimeshiftBufferPathValid() const
{
  return kodi::vfs::DirectoryExists(m_timeshiftBufferPath);
}

std::string Settings::BaseURL(bool credentials) const
{
  std::string auth = (credentials && !m_username.empty() && !m_password.empty())
      ? StringUtils::Format("%s:%s@", URLEncode(m_username).c_str(),
          URLEncode(m_password).c_str())
      : "";
  return StringUtils::Format("http://%s%s:%u/", auth.c_str(),
      m_hostname.c_str(), m_webPort);
}

/***************************************************************************
 * Backend settings
 **************************************************************************/
void Settings::ResetBackendSettings()
{
  m_priority = 50;
  m_recordingTask = "";
}

bool Settings::ReadFromBackend(Dvb &cli)
{
  ResetBackendSettings();

  std::unique_ptr<const Dvb::httpResponse> res = cli.GetFromAPI(
    "api/getconfigfile.html?file=config%%5Cservice.xml");
  if (res->error)
    return false;

  TiXmlDocument doc;
  doc.Parse(res->content.c_str());
  if (doc.Error())
  {
    kodi::Log(ADDON_LOG_ERROR, "Unable to parse service.xml. Error: %s",
        doc.ErrorDesc());
    return false;
  }

  for (auto xSection = doc.RootElement()->FirstChildElement("section");
    xSection; xSection = xSection->NextSiblingElement("section"))
  {
    const char *name = xSection->Attribute("name");
    if (!strcmp(name, "Recording"))
    {
      for (auto xEntry = xSection->FirstChildElement("entry");
        xEntry; xEntry = xEntry->NextSiblingElement("entry"))
      {
        name = xEntry->Attribute("name");
        if (!strcmp(name, "DefPrio"))
          m_priority = atoi(xEntry->GetText());
        else if (!strcmp(name, "DefTask"))
          m_recordingTask = xEntry->GetText();
      }
    }
  }

  return true;
}
