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
#include "TimeshiftBuffer.h"
#include "RecordingReader.h"
#include "kodi/xbmc_pvr_dll.h"
#include "kodi/libKODI_guilib.h"
#include "p8-platform/util/util.h"
#include <stdlib.h>

using namespace ADDON;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
CStdString g_hostname             = DEFAULT_HOST;
int        g_webPort              = DEFAULT_WEB_PORT;
CStdString g_username             = "";
CStdString g_password             = "";
bool       g_useFavourites        = false;
bool       g_useFavouritesFile    = false;
CStdString g_favouritesFile       = "";
int        g_groupRecordings      = DvbRecording::GROUPING_DISABLED;
bool       g_useTimeshift         = false;
CStdString g_timeshiftBufferPath  = DEFAULT_TSBUFFERPATH;
bool       g_useRTSP              = false;
int        g_prependOutline       = PrependOutline::IN_EPG;
bool       g_lowPerformance       = false;

ADDON_STATUS m_curStatus    = ADDON_STATUS_UNKNOWN;
CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr   *PVR  = NULL;
Dvb *DvbData                = NULL;
TimeshiftBuffer *tsBuffer   = NULL;
RecordingReader *recReader  = NULL;

extern "C"
{
void ADDON_ReadSettings(void)
{
  char buffer[1024];

  if (XBMC->GetSetting("host", buffer))
    g_hostname = buffer;

  if (XBMC->GetSetting("user", buffer))
    g_username = buffer;

  if (XBMC->GetSetting("pass", buffer))
    g_password = buffer;

  if (!XBMC->GetSetting("webport", &g_webPort))
    g_webPort = DEFAULT_WEB_PORT;

  if (!XBMC->GetSetting("usefavourites", &g_useFavourites))
    g_useFavourites = false;

  if (!XBMC->GetSetting("usefavouritesfile", &g_useFavouritesFile))
    g_useFavouritesFile = false;

  if (g_useFavouritesFile && XBMC->GetSetting("favouritesfile", buffer))
    g_favouritesFile = buffer;

  if (!XBMC->GetSetting("grouprecordings", &g_groupRecordings))
    g_groupRecordings = DvbRecording::GROUPING_DISABLED;

  if (!XBMC->GetSetting("usetimeshift", &g_useTimeshift))
    g_useTimeshift = false;

  if (XBMC->GetSetting("timeshiftpath", buffer))
    g_timeshiftBufferPath = buffer;

  if (!XBMC->GetSetting("usertsp", &g_useRTSP) || g_useTimeshift)
    g_useRTSP = false;

  if (!XBMC->GetSetting("prependoutline", &g_prependOutline))
    g_prependOutline = PrependOutline::IN_EPG;

  if (!XBMC->GetSetting("lowperformance", &g_lowPerformance))
    g_lowPerformance = false;

  /* Log the current settings for debugging purposes */
  XBMC->Log(LOG_DEBUG, "DVBViewer Addon Configuration options");
  XBMC->Log(LOG_DEBUG, "Hostname:   %s", g_hostname.c_str());
  if (!g_username.empty() && !g_password.empty())
  {
    XBMC->Log(LOG_DEBUG, "Username:   %s", g_username.c_str());
    XBMC->Log(LOG_DEBUG, "Password:   %s", g_password.c_str());
  }
  XBMC->Log(LOG_DEBUG, "WebPort:    %d", g_webPort);
  XBMC->Log(LOG_DEBUG, "Use favourites: %s", (g_useFavourites) ? "yes" : "no");
  if (g_useFavouritesFile)
    XBMC->Log(LOG_DEBUG, "Favourites file: %s", g_favouritesFile.c_str());
  if (g_groupRecordings != DvbRecording::GROUPING_DISABLED)
    XBMC->Log(LOG_DEBUG, "Group recordings: %d", g_groupRecordings);
  XBMC->Log(LOG_DEBUG, "Timeshift: %s", (g_useTimeshift) ? "enabled" : "disabled");
  if (g_useTimeshift)
    XBMC->Log(LOG_DEBUG, "Timeshift buffer path: %s", g_timeshiftBufferPath.c_str());
  XBMC->Log(LOG_DEBUG, "Use RTSP: %s", (g_useRTSP) ? "yes" : "no");
  if (g_prependOutline != PrependOutline::NEVER)
    XBMC->Log(LOG_DEBUG, "Prepend outline: %d", g_prependOutline);
  XBMC->Log(LOG_DEBUG, "Low performance mode: %s", (g_lowPerformance) ? "yes" : "no");
}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
    return ADDON_STATUS_UNKNOWN;

  XBMC = new CHelper_libXBMC_addon();
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr();
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s: Creating DVBViewer PVR-Client", __FUNCTION__);
  m_curStatus = ADDON_STATUS_UNKNOWN;

  ADDON_ReadSettings();

  DvbData = new Dvb();
  if (!DvbData->Open())
  {
    SAFE_DELETE(DvbData);
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    m_curStatus = ADDON_STATUS_LOST_CONNECTION;
    return m_curStatus;
  }

  m_curStatus = ADDON_STATUS_OK;
  return m_curStatus;
}

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

bool ADDON_HasSettings()
{
  return true;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***_UNUSED(sSet))
{
  return 0;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!XBMC)
    return ADDON_STATUS_OK;

  CStdString sname(settingName);
  if (sname == "host")
  {
    if (g_hostname.compare((const char *)settingValue) != 0)
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
  else if (sname == "webport")
  {
    if (g_webPort != *(int *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
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
  else if (sname == "usetimeshift")
  {
    if (g_useTimeshift != *(bool *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "grouprecordings")
  {
    if (g_groupRecordings != *(const DvbRecording::Grouping *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "timeshiftpath")
  {
    CStdString newValue = (const char *)settingValue;
    if (g_timeshiftBufferPath != newValue)
    {
      XBMC->Log(LOG_DEBUG, "%s: Changed setting '%s' from '%s' to '%s'",
          __FUNCTION__, settingName, g_timeshiftBufferPath.c_str(),
          newValue.c_str());
      g_timeshiftBufferPath = newValue;
    }
  }
  else if (sname == "usertsp")
  {
    if (g_useRTSP != *(bool *)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (sname == "prependoutline")
  {
    PrependOutline::options newValue = *(const PrependOutline::options *)settingValue;
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
  return ADDON_STATUS_OK;
}

void ADDON_Stop()
{
}

void ADDON_FreeSettings()
{
}

void ADDON_Announce(const char *_UNUSED(flag), const char *sender,
    const char *message, const void *_UNUSED(data))
{
  if (recReader != NULL && strcmp(sender, "xbmc") == 0)
    recReader->Announce(message);
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

const char* GetPVRAPIVersion(void)
{
  static const char *apiVersion = XBMC_PVR_API_VERSION;
  return apiVersion;
}

const char* GetMininumPVRAPIVersion(void)
{
  static const char *minApiVersion = XBMC_PVR_MIN_API_VERSION;
  return minApiVersion;
}

const char* GetGUIAPIVersion(void)
{
  return KODI_GUILIB_API_VERSION;
}

const char* GetMininumGUIAPIVersion(void)
{
  return KODI_GUILIB_MIN_API_VERSION;
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers          = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsChannelScan     = false;
  pCapabilities->bHandlesInputStream      = false;
  pCapabilities->bHandlesDemuxing         = false;
  pCapabilities->bSupportsLastPlayedPosition = false;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const CStdString name = DvbData ? DvbData->GetBackendName()
    : "unknown";
  return name.c_str();
}

const char *GetBackendVersion(void)
{
  static const CStdString version = DvbData ? DvbData->GetBackendVersion()
    : "UNKNOWN";
  return version.c_str();
}

const char *GetConnectionString(void)
{
  static CStdString conn;
  if (DvbData)
    conn.Format("%s%s", g_hostname, DvbData->IsConnected() ? "" : " (Not connected!)");
  else
    conn.Format("%s (addon error!)", g_hostname);
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

bool SwitchChannel(const PVR_CHANNEL &channel)
{
  if (!DvbData || !DvbData->IsConnected())
    return false;

  return DvbData->SwitchChannel(channel);
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

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
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

  if (channel.iUniqueId == DvbData->GetCurrentClientChannel())
    return true;

  if (!DvbData->OpenLiveStream(channel))
    return false;
  if (!g_useTimeshift)
    return true;

  CStdString streamURL = DvbData->GetLiveStreamURL(channel);
  XBMC->Log(LOG_INFO, "Timeshift starts; url=%s", streamURL.c_str());
  if (tsBuffer)
    SAFE_DELETE(tsBuffer);
  tsBuffer = new TimeshiftBuffer(streamURL, g_timeshiftBufferPath);
  return tsBuffer->IsValid();
}

void CloseLiveStream(void)
{
  DvbData->CloseLiveStream();
  if (tsBuffer)
    SAFE_DELETE(tsBuffer);
}

const char *GetLiveStreamURL(const PVR_CHANNEL &channel)
{
  if (!DvbData || !DvbData->IsConnected())
    return "";

  DvbData->SwitchChannel(channel);
  return DvbData->GetLiveStreamURL(channel).c_str();
}

bool CanPauseStream(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return false;

  return g_useTimeshift;
}

bool CanSeekStream(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return false;

  return g_useTimeshift;
}

int ReadLiveStream(unsigned char *buffer, unsigned int size)
{
  if (!tsBuffer)
    return 0;

  return tsBuffer->ReadData(buffer, size);
}

long long SeekLiveStream(long long position, int whence)
{
  if (!tsBuffer)
    return -1;

  return tsBuffer->Seek(position, whence);
}

long long PositionLiveStream(void)
{
  if (!tsBuffer)
    return -1;

  return tsBuffer->Position();
}

long long LengthLiveStream(void)
{
  if (!tsBuffer)
    return -1;

  return tsBuffer->Length();
}

time_t GetBufferTimeStart()
{
  if (!tsBuffer)
    return 0;

  return tsBuffer->TimeStart();
}

time_t GetBufferTimeEnd()
{
  if (!tsBuffer)
    return 0;

  return tsBuffer->TimeEnd();
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
  return recReader->IsValid();
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
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *_UNUSED(pProperties)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &_UNUSED(menuhook), const PVR_MENUHOOK_DATA &_UNUSED(item)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &_UNUSED(channel)) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool IsRealTimeStream(void) { return true; }
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
unsigned int GetChannelSwitchDelay(void) { return 0; }
void PauseStream(bool _UNUSED(bPaused)) {}
bool SeekTime(int, bool, double*) { return false; }
void SetSpeed(int) {};
bool IsTimeshifting(void) { return false; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
}
