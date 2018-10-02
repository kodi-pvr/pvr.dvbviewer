#include "Settings.h"
#include "client.h"
#include "DvbData.h"
#include "LocalizedString.h"

#include "p8-platform/util/StringUtils.h"

using namespace dvbviewer;
using namespace ADDON;

Settings::Settings()
{
  ResetBackendSettings();
}

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromKodi()
{
  char buffer[1024];

  if (XBMC->GetSetting("host", buffer))
    m_hostname = buffer;

  if (!XBMC->GetSetting("webport", &m_webPort))
    m_webPort = DEFAULT_WEB_PORT;

  if (XBMC->GetSetting("user", buffer))
    m_username = buffer;

  if (XBMC->GetSetting("pass", buffer))
    m_password = buffer;

  if (!XBMC->GetSetting("profileid", &m_profileId))
    m_profileId = 0;

  if (!XBMC->GetSetting("usewol", &m_useWoL))
    m_useWoL = false;

  if (m_useWoL && XBMC->GetSetting("mac", buffer))
    m_mac = buffer;

  if (!XBMC->GetSetting("usefavourites", &m_useFavourites))
    m_useFavourites = false;

  if (!XBMC->GetSetting("usefavouritesfile", &m_useFavouritesFile))
    m_useFavouritesFile = false;

  if (XBMC->GetSetting("favouritesfile", buffer))
    m_favouritesFile = buffer;

  if (!XBMC->GetSetting("grouprecordings", &m_groupRecordings))
    m_groupRecordings = RecordGrouping::DISABLED;

  if (!XBMC->GetSetting("timeshift", &m_timeshift))
    m_timeshift = Timeshift::OFF;

  if (XBMC->GetSetting("timeshiftpath", buffer) && !std::string(buffer).empty())
    m_timeshiftBufferPath = buffer;

  if (!XBMC->GetSetting("edl", &m_edl.enabled))
    m_edl.enabled = false;

  if (!XBMC->GetSetting("edl_padding_start", &m_edl.padding_start))
    m_edl.padding_start = 0;

  if (!XBMC->GetSetting("edl_padding_stop", &m_edl.padding_stop))
    m_edl.padding_stop = 0;

  if (!XBMC->GetSetting("prependoutline", &m_prependOutline))
    m_prependOutline = PrependOutline::IN_EPG;

  if (!XBMC->GetSetting("lowperformance", &m_lowPerformance))
    m_lowPerformance = false;

  if (!XBMC->GetSetting("readtimeout", &m_readTimeout))
    m_readTimeout = 0;

  if (!XBMC->GetSetting("stream_readchunksize", &m_streamReadChunkSize))
    m_streamReadChunkSize = 0;

  if (!XBMC->GetSetting("transcoding", &m_transcoding))
    m_transcoding = Transcoding::OFF;

  if (!XBMC->GetSetting("recording_transcoding", &m_recordingTranscoding))
    m_recordingTranscoding = Transcoding::OFF;

  if (XBMC->GetSetting("transcodingparams", buffer))
  {
    m_transcodingParams = buffer;
    StringUtils::Replace(m_transcodingParams, " ", "+");
  }

  if (XBMC->GetSetting("recording_transcodingparams", buffer))
  {
    m_recordingTranscodingParams = buffer;
    StringUtils::Replace(m_recordingTranscodingParams, " ", "+");
  }

  /* Log the current settings for debugging purposes */
  /* general tab */
  XBMC->Log(LOG_DEBUG, "DVBViewer Addon Configuration options");
  XBMC->Log(LOG_DEBUG, "Backend: http://%s:%d/", m_hostname.c_str(), m_webPort);
  if (!m_username.empty() && !m_password.empty())
    XBMC->Log(LOG_DEBUG, "Login credentials: %s/%s", m_username.c_str(),
        m_password.c_str());
  XBMC->Log(LOG_DEBUG, "Profile ID: %d", m_profileId);
  if (m_useWoL)
    XBMC->Log(LOG_DEBUG, "WoL MAC: %s", m_mac.c_str());

  /* livetv tab */
  XBMC->Log(LOG_DEBUG, "Use favourites: %s", (m_useFavourites) ? "yes" : "no");
  if (m_useFavouritesFile)
    XBMC->Log(LOG_DEBUG, "Favourites file: %s", m_favouritesFile.c_str());
  XBMC->Log(LOG_DEBUG, "Timeshift mode: %d", m_timeshift);
  if (m_timeshift != Timeshift::OFF)
    XBMC->Log(LOG_DEBUG, "Timeshift buffer path: %s", m_timeshiftBufferPath.c_str());
  if (m_transcoding != Transcoding::OFF)
    XBMC->Log(LOG_DEBUG, "Transcoding: format=%d params=%s",
        m_transcoding, m_transcodingParams.c_str());

  /* recordings tab */
  if (m_groupRecordings != RecordGrouping::DISABLED)
    XBMC->Log(LOG_DEBUG, "Group recordings: %d", m_groupRecordings);
  if (m_edl.enabled)
    XBMC->Log(LOG_DEBUG, "EDL enabled. Padding: start=%d stop=%d",
      m_edl.padding_start, m_edl.padding_stop);
  if (m_recordingTranscoding != Transcoding::OFF)
    XBMC->Log(LOG_DEBUG, "Recording transcoding: format=%d, params=%s",
      m_recordingTranscoding, m_recordingTranscodingParams.c_str());

  /* advanced tab */
  if (m_prependOutline != PrependOutline::NEVER)
    XBMC->Log(LOG_DEBUG, "Prepend outline: %d", m_prependOutline);
  XBMC->Log(LOG_DEBUG, "Low performance mode: %s", (m_lowPerformance) ? "yes" : "no");
  if (m_readTimeout)
    XBMC->Log(LOG_DEBUG, "Custom connection/read timeout: %d", m_readTimeout);
  if (m_streamReadChunkSize)
    XBMC->Log(LOG_DEBUG, "Stream read chunk size: %d kb", m_streamReadChunkSize);
}

ADDON_STATUS Settings::SetValue(const std::string name, const void *value)
{
  if (name == "host")
  {
    if (m_hostname.compare((const char *)value) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "webport")
  {
    if (m_webPort != *(int *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "user")
  {
    if (m_username.compare((const char *)value) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "pass")
  {
    if (m_password.compare((const char *)value) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "profileid")
  {
    if (m_profileId != *(int *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "usewol")
  {
    m_useWoL = *(bool *)value;
  }
  else if (name == "mac")
  {
    m_mac = (const char *)value;
  }
  else if (name == "usefavourites")
  {
    if (m_useFavourites != *(bool *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "usefavouritesfile")
  {
    if (m_useFavouritesFile != *(bool *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "favouritesfile")
  {
    if (m_favouritesFile.compare((const char *)value) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "timeshift")
  {
    Timeshift newValue = *(const Timeshift *)value;
    if (m_timeshift != newValue)
    {
      XBMC->Log(LOG_DEBUG, "%s: Changed setting '%s' from '%d' to '%d'",
          __FUNCTION__, name.c_str(), m_timeshift, newValue);
      m_timeshift = newValue;
    }
  }
  else if (name == "timeshiftpath")
  {
    std::string newValue = (const char *)value;
    if (m_timeshiftBufferPath != newValue && !newValue.empty())
    {
      XBMC->Log(LOG_DEBUG, "%s: Changed setting '%s' from '%s' to '%s'",
          __FUNCTION__, name.c_str(), m_timeshiftBufferPath.c_str(),
          newValue.c_str());
      m_timeshiftBufferPath = newValue;
    }
  }
  else if (name == "edl")
  {
    m_edl.enabled = *(bool *)value;
  }
  else if (name == "edl_padding_start")
  {
    m_edl.padding_start = *(int *)value;
  }
  else if (name == "edl_padding_stop")
  {
    m_edl.padding_stop = *(int *)value;
  }
  else if (name == "usefavouritesfile")
  {
    if (m_useFavouritesFile != *(bool *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "prependoutline")
  {
    PrependOutline newValue = *(const PrependOutline *)value;
    if (m_prependOutline != newValue)
    {
      m_prependOutline = newValue;
      // EPG view seems cached, so TriggerEpgUpdate isn't reliable
      // also if PVR is currently disabled we don't get notified at all
      XBMC->QueueNotification(QUEUE_WARNING, LocalizedString(30507).c_str());
    }
  }
  else if (name == "lowperformance")
  {
    if (m_lowPerformance != *(bool *)value)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (name == "readtimeout")
  {
    m_readTimeout = *(int *)value;
  }
  else if (name == "stream_readchunksize")
  {
    m_streamReadChunkSize = *(int *)value;
  }
  else if (name == "transcoding")
  {
    m_transcoding = *(const Transcoding *)value;
  }
  else if (name == "recording_transcoding")
  {
    m_recordingTranscoding = *(const Transcoding *)value;
  }
  else if (name == "transcodingparams")
  {
    m_transcodingParams = (const char *)value;
    StringUtils::Replace(m_transcodingParams, " ", "+");
  }
  else if (name == "recording_transcodingparams")
  {
    m_recordingTranscodingParams = (const char *)value;
    StringUtils::Replace(m_recordingTranscodingParams, " ", "+");
  }
  return ADDON_STATUS_OK;
}

bool Settings::IsTimeshiftBufferPathValid() const
{
  return XBMC->DirectoryExists(m_timeshiftBufferPath.c_str());
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

  const Dvb::httpResponse &res = cli.GetFromAPI(
    "api/getconfigfile.html?file=config%%5Cservice.xml");
  if (res.error)
    return false;

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse service.xml. Error: %s",
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
