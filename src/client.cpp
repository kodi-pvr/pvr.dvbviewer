/*
 *      Copyright (C) 2005-2015 Team Kodi
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "client.h"
#include "DvbData.h"
#include "StreamReader.h"
#include "TimeshiftBuffer.h"
#include "RecordingReader.h"
#include "xbmc_pvr_dll.h"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"
#include <stdlib.h>

using namespace ADDON;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string    g_hostname             = DEFAULT_HOST;
int            g_webPort              = DEFAULT_WEB_PORT;
std::string    g_username             = "";
std::string    g_password             = "";
bool           g_useWoL               = false;
std::string    g_mac                  = "";
bool           g_useFavourites        = false;
bool           g_useFavouritesFile    = false;
std::string    g_favouritesFile       = "";
DvbRecording::Grouping g_groupRecordings = DvbRecording::Grouping::DISABLED;
Timeshift      g_timeshift            = Timeshift::OFF;
std::string    g_timeshiftBufferPath  = DEFAULT_TSBUFFERPATH;
PrependOutline g_prependOutline       = PrependOutline::IN_EPG;
bool           g_lowPerformance       = false;
Transcoding    g_transcoding          = Transcoding::OFF;
std::string    g_transcodingParams    = "";

ADDON_STATUS m_curStatus    = ADDON_STATUS_UNKNOWN;
CHelper_libXBMC_addon *XBMC = nullptr;
CHelper_libXBMC_pvr   *PVR  = nullptr;
Dvb *DvbData                = nullptr;
IStreamReader   *strReader  = nullptr;
RecordingReader *recReader  = nullptr;

extern "C"
{
void ADDON_ReadSettings(void)
{
  char buffer[1024];

  if (XBMC->GetSetting("host", buffer))
    g_hostname = buffer;

  if (!XBMC->GetSetting("webport", &g_webPort))
    g_webPort = DEFAULT_WEB_PORT;

  if (XBMC->GetSetting("user", buffer))
    g_username = buffer;

  if (XBMC->GetSetting("pass", buffer))
    g_password = buffer;

  if (!XBMC->GetSetting("usewol", &g_useWoL))
    g_useWoL = false;

  if (g_useWoL && XBMC->GetSetting("mac", buffer))
    g_mac = buffer;

  if (!XBMC->GetSetting("usefavourites", &g_useFavourites))
    g_useFavourites = false;

  if (!XBMC->GetSetting("usefavouritesfile", &g_useFavouritesFile))
    g_useFavouritesFile = false;

  if (g_useFavouritesFile && XBMC->GetSetting("favouritesfile", buffer))
    g_favouritesFile = buffer;

  if (!XBMC->GetSetting("grouprecordings", &g_groupRecordings))
    g_groupRecordings = DvbRecording::Grouping::DISABLED;

  if (!XBMC->GetSetting("timeshift", &g_timeshift))
    g_timeshift = Timeshift::OFF;

  if (XBMC->GetSetting("timeshiftpath", buffer) && !std::string(buffer).empty())
    g_timeshiftBufferPath = buffer;

  if (!XBMC->GetSetting("prependoutline", &g_prependOutline))
    g_prependOutline = PrependOutline::IN_EPG;

  if (!XBMC->GetSetting("lowperformance", &g_lowPerformance))
    g_lowPerformance = false;

  if (!XBMC->GetSetting("transcoding", &g_transcoding))
    g_transcoding = Transcoding::OFF;

  if (XBMC->GetSetting("transcodingparams", buffer))
  {
    g_transcodingParams = buffer;
    StringUtils::Replace(g_transcodingParams, " ", "+");
  }

  /* Log the current settings for debugging purposes */
  /* general tab */
  XBMC->Log(LOG_DEBUG, "DVBViewer Addon Configuration options");
  XBMC->Log(LOG_DEBUG, "Backend: http://%s:%d/", g_hostname.c_str(), g_webPort);
  if (!g_username.empty() && !g_password.empty())
    XBMC->Log(LOG_DEBUG, "Login credentials: %s/%s", g_username.c_str(),
        g_password.c_str());
  if (g_useWoL)
    XBMC->Log(LOG_DEBUG, "WoL MAC: %s", g_mac.c_str());

  /* livetv tab */
  XBMC->Log(LOG_DEBUG, "Use favourites: %s", (g_useFavourites) ? "yes" : "no");
  if (g_useFavouritesFile)
    XBMC->Log(LOG_DEBUG, "Favourites file: %s", g_favouritesFile.c_str());
  XBMC->Log(LOG_DEBUG, "Timeshift mode: %d", g_timeshift);
  if (g_timeshift != Timeshift::OFF)
    XBMC->Log(LOG_DEBUG, "Timeshift buffer path: %s", g_timeshiftBufferPath.c_str());

  /* recordings tab */
  if (g_groupRecordings != DvbRecording::Grouping::DISABLED)
    XBMC->Log(LOG_DEBUG, "Group recordings: %d", g_groupRecordings);

  /* advanced tab */
  if (g_prependOutline != PrependOutline::NEVER)
    XBMC->Log(LOG_DEBUG, "Prepend outline: %d", g_prependOutline);
  XBMC->Log(LOG_DEBUG, "Low performance mode: %s", (g_lowPerformance) ? "yes" : "no");
  XBMC->Log(LOG_DEBUG, "Transcoding: %d", g_transcoding);
  if (g_transcoding != Transcoding::OFF)
    XBMC->Log(LOG_DEBUG, "Transcoding params: %s", g_transcodingParams.c_str());
}

ADDON_STATUS ADDON_Create(void *hdl, void *props)
{
  if (!hdl || !props)
    return ADDON_STATUS_UNKNOWN;

  XBMC = new CHelper_libXBMC_addon();
  PVR  = new CHelper_libXBMC_pvr();
  if (!XBMC->RegisterMe(hdl) || !PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    SAFE_DELETE(PVR);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s: Creating DVBViewer PVR-Client", __FUNCTION__);
  m_curStatus = ADDON_STATUS_UNKNOWN;

  ADDON_ReadSettings();

  DvbData = new Dvb();
  m_curStatus = ADDON_STATUS_OK;
  return m_curStatus;
}

//TODO: I'm pretty sure ADDON_GetStatus can be removed
ADDON_STATUS ADDON_GetStatus()
{
  /* check whether we're still connected */
  if (m_curStatus == ADDON_STATUS_OK && !DvbData->IsConnected())
    m_curStatus = ADDON_STATUS_LOST_CONNECTION;

  return m_curStatus;
}

void ADDON_Destroy()
{
  SAFE_DELETE(DvbData);
  SAFE_DELETE(PVR);
  SAFE_DELETE(XBMC);

  m_curStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!XBMC)
    return ADDON_STATUS_OK;

  std::string sname(settingName);
  if (sname == "host")
  {
    if (g_hostname.compare((const char *)settingValue) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "webport")
  {
    if (g_webPort != *(int *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "user")
  {
    if (g_username.compare((const char *)settingValue) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "pass")
  {
    if (g_password.compare((const char *)settingValue) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "usewol")
  {
    g_useWoL = *(bool *)settingValue;
  }
  else if (sname == "mac")
  {
    g_mac = (const char *)settingValue;
  }
  else if (sname == "usefavourites")
  {
    if (g_useFavourites != *(bool *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "usefavouritesfile")
  {
    if (g_useFavouritesFile != *(bool *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "favouritesfile")
  {
    if (g_favouritesFile.compare((const char *)settingValue) != 0)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "grouprecordings")
  {
    if (g_groupRecordings != *(const DvbRecording::Grouping *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "timeshift")
  {
    Timeshift newValue = *(const Timeshift *)settingValue;
    if (g_timeshift != newValue)
    {
      XBMC->Log(LOG_DEBUG, "%s: Changed setting '%s' from '%d' to '%d'",
          __FUNCTION__, settingName, g_timeshift, newValue);
      g_timeshift = newValue;
    }
  }
  else if (sname == "timeshiftpath")
  {
    std::string newValue = (const char *)settingValue;
    if (g_timeshiftBufferPath != newValue && !newValue.empty())
    {
      XBMC->Log(LOG_DEBUG, "%s: Changed setting '%s' from '%s' to '%s'",
          __FUNCTION__, settingName, g_timeshiftBufferPath.c_str(),
          newValue.c_str());
      g_timeshiftBufferPath = newValue;
    }
  }
  else if (sname == "prependoutline")
  {
    PrependOutline newValue = *(const PrependOutline *)settingValue;
    if (g_prependOutline != newValue)
    {
      g_prependOutline = newValue;
      // EPG view seems cached, so TriggerEpgUpdate isn't reliable
      // also if PVR is currently disabled we don't get notified at all
      XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30507));
    }
  }
  else if (sname == "lowperformance")
  {
    if (g_lowPerformance != *(bool *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "transcoding")
  {
    g_transcoding = *(const Transcoding *)settingValue;
  }
  else if (sname == "transcodingparams")
  {
    g_transcodingParams = (const char *)settingValue;
    StringUtils::Replace(g_transcodingParams, " ", "+");
  }
  return ADDON_STATUS_OK;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
}

void OnSystemWake()
{
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bSupportsChannelScan        = false;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = false;
  pCapabilities->bSupportsLastPlayedPosition = false;
  pCapabilities->bSupportsRecordingsRename   = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const std::string &name = DvbData ? DvbData->GetBackendName()
    : "unknown";
  return name.c_str();
}

const char *GetBackendVersion(void)
{
  static const std::string &version = DvbData ? DvbData->GetBackendVersion()
    : "UNKNOWN";
  return version.c_str();
}

const char *GetConnectionString(void)
{
  static std::string conn;
  if (DvbData)
    conn = StringUtils::Format("%s%s", g_hostname.c_str(),
      DvbData->IsConnected() ? "" : " (Not connected!)");
  else
    conn = StringUtils::Format("%s (addon error!)", g_hostname.c_str());
  return conn.c_str();
}

const char *GetBackendHostname(void)
{
  return g_hostname.c_str();
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  // the RS api doesn't provide information about signal quality (yet)
  strncpy(signalStatus.strAdapterName, "DVBViewer Recording Service",
      sizeof(signalStatus.strAdapterName));
  strncpy(signalStatus.strAdapterStatus, "OK",
      sizeof(signalStatus.strAdapterStatus));
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetDriveSpace(long long *total, long long *used)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetDriveSpace(total, used))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

/* channel functions */
PVR_ERROR GetChannels(ADDON_HANDLE handle, bool radio)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetChannels(handle, radio))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel,
    time_t start, time_t end)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetEPGForChannel(handle, channel, start, end))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

int GetChannelsAmount(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return 0;

  return DvbData->GetChannelsAmount();
}

/* channel group functions */
int GetChannelGroupsAmount(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return 0;

  return DvbData->GetChannelGroupsAmount();
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool radio)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetChannelGroups(handle, radio))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle,
    const PVR_CHANNEL_GROUP &group)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetChannelGroupMembers(handle, group))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

/* timer functions */
PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  /* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

int GetTimersAmount(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return 0;

  return DvbData->GetTimersAmount();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  /* TODO: Change implementation to get support for the timer features introduced with PVR API 1.9.7 */
  return (DvbData && DvbData->IsConnected() && DvbData->GetTimers(handle))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  return (DvbData && DvbData->IsConnected() && DvbData->AddTimer(timer))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  return (DvbData && DvbData->IsConnected() && DvbData->AddTimer(timer, true))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool _UNUSED(bForceDelete))
{
  return (DvbData && DvbData->IsConnected() && DvbData->DeleteTimer(timer))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

/* live stream functions */
bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (!DvbData || !DvbData->IsConnected())
    return false;

  if (!DvbData->OpenLiveStream(channel))
    return false;

  std::string streamURL = DvbData->GetLiveStreamURL(channel);
  strReader = new StreamReader(streamURL);
  if (g_timeshift == Timeshift::ON_PLAYBACK
      && XBMC->DirectoryExists(g_timeshiftBufferPath.c_str()))
    strReader = new TimeshiftBuffer(strReader, g_timeshiftBufferPath);
  return strReader->Start();
}

void CloseLiveStream(void)
{
  DvbData->CloseLiveStream();
  SAFE_DELETE(strReader);
}

bool SwitchChannel(const PVR_CHANNEL &channel)
{
  if (channel.iUniqueId == DvbData->GetCurrentClientChannel())
    return true;

  /* as of late we need to close and reopen ourself */
  (void)CloseLiveStream();
  return OpenLiveStream(channel);
}

bool IsRealTimeStream()
{
  return (strReader) ? strReader->NearEnd() : false;
}

bool CanPauseStream(void)
{
  if (g_timeshift != Timeshift::OFF && strReader)
    return (strReader->CanTimeshift()
      || XBMC->DirectoryExists(g_timeshiftBufferPath.c_str()));
  return false;
}

bool CanSeekStream(void)
{
  // pause button seems to check CanSeekStream() too
  //return (strReader && strReader->CanTimeshift());
  return (g_timeshift != Timeshift::OFF);
}

int ReadLiveStream(unsigned char *buffer, unsigned int size)
{
  return (strReader) ? strReader->ReadData(buffer, size) : 0;
}

long long SeekLiveStream(long long position, int whence)
{
  return (strReader) ? strReader->Seek(position, whence) : -1;
}

long long PositionLiveStream(void)
{
  return (strReader) ? strReader->Position() : -1;
}

long long LengthLiveStream(void)
{
  return (strReader) ? strReader->Length() : -1;
}

bool IsTimeshifting(void)
{
  return (strReader && strReader->CanTimeshift());
}

time_t GetBufferTimeStart()
{
  return (strReader) ? strReader->TimeStart() : 0;
}

time_t GetBufferTimeEnd()
{
  return (strReader) ? strReader->TimeEnd() : 0;
}

void PauseStream(bool paused)
{
  /* start timeshift on pause */
  if (paused && g_timeshift != Timeshift::OFF
      && strReader && !strReader->CanTimeshift()
      && XBMC->DirectoryExists(g_timeshiftBufferPath.c_str()))
  {
    strReader = new TimeshiftBuffer(strReader, g_timeshiftBufferPath);
    (void)strReader->Start();
  }
}

time_t GetPlayingTime()
{
  //FIXME: this should rather return the time of the *current* position
  return GetBufferTimeEnd();
}

/* recording stream functions */
int GetRecordingsAmount(bool _UNUSED(deleted))
{
  if (!DvbData || !DvbData->IsConnected())
    return PVR_ERROR_SERVER_ERROR;

  return DvbData->GetRecordingsAmount();
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool _UNUSED(deleted))
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetRecordings(handle))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->DeleteRecording(recording))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  if (recReader)
    SAFE_DELETE(recReader);
  recReader = DvbData->OpenRecordedStream(recording);
  return recReader->Start();
}

void CloseRecordedStream(void)
{
  if (recReader)
    SAFE_DELETE(recReader);
}

int ReadRecordedStream(unsigned char *buffer, unsigned int size)
{
  if (!recReader)
    return 0;

  return recReader->ReadData(buffer, size);
}

long long SeekRecordedStream(long long position, int whence)
{
  if (!recReader)
    return 0;

  return recReader->Seek(position, whence);
}

long long PositionRecordedStream(void)
{
  if (!recReader)
    return -1;

  return recReader->Position();
}

long long LengthRecordedStream(void)
{
  if (!recReader)
    return -1;

  return recReader->Length();
}

/** UNUSED API FUNCTIONS */
const char *GetLiveStreamURL(const PVR_CHANNEL &_UNUSED(channel)) { return ""; }
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *_UNUSED(pProperties)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &_UNUSED(menuhook), const PVR_MENUHOOK_DATA &_UNUSED(item)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
DemuxPacket *DemuxRead(void) { return NULL; }
void DemuxAbort(void) {}
void DemuxReset(void) {}
void DemuxFlush(void) {}
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &_UNUSED(recording), int _UNUSED(count)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &_UNUSED(recording), int _UNUSED(lastplayedposition)) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &_UNUSED(recording)) { return -1; }
PVR_ERROR RenameRecording(const PVR_RECORDING &_UNUSED(recording)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR UndeleteRecording(const PVR_RECORDING& _UNUSED(recording)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int iDays) { return PVR_ERROR_NOT_IMPLEMENTED; }
unsigned int GetChannelSwitchDelay(void) { return 0; }
bool SeekTime(double time, bool backwards, double *startpts) { return false; }
void SetSpeed(int speed) {};
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
}
