/*
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2020 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "client.h"
#include "DvbData.h"
#include "LocalizedString.h"
#include "Settings.h"
#include "StreamReader.h"
#include "TimeshiftBuffer.h"
#include "RecordingReader.h"

#include "kodi/xbmc_pvr_dll.h"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"

#include <stdlib.h>

using namespace dvbviewer;
using namespace ADDON;

ADDON_STATUS m_curStatus    = ADDON_STATUS_UNKNOWN;
CHelper_libXBMC_addon *XBMC = nullptr;
CHelper_libXBMC_pvr   *PVR  = nullptr;
Dvb *DvbData                = nullptr;
IStreamReader   *strReader  = nullptr;
RecordingReader *recReader  = nullptr;

extern "C"
{
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

  Settings settings;
  settings.ReadFromKodi();

  DvbData = new Dvb(settings);
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
  if (!XBMC || !DvbData)
    return ADDON_STATUS_OK;

  return DvbData->GetSettings().SetValue(settingName, settingValue);
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/
PVR_ERROR GetCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bSupportsChannelScan        = false;
  pCapabilities->bSupportsChannelSettings    = false;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = false;
  pCapabilities->bSupportsRecordingPlayCount = false;
  pCapabilities->bSupportsLastPlayedPosition = false;
  pCapabilities->bSupportsRecordingEdl       = true;
  pCapabilities->bSupportsRecordingsRename   = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo     = false;

  pCapabilities->iRecordingsLifetimesSize = 0;

  if (DvbData && DvbData->IsConnected())
  {
    if (DvbData->IsGuest())
      pCapabilities->bSupportsTimers = false;

    if (DvbData->HasKVStore())
    {
      pCapabilities->bSupportsRecordingPlayCount = true;
      pCapabilities->bSupportsLastPlayedPosition = true;
    }
  }

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return "unknown";

  static std::string name;
  name = DvbData->GetBackendName();
  return name.c_str();
}

const char *GetBackendVersion(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return "-1";

  static std::string version;
  unsigned int iver = DvbData->GetBackendVersion();
  version = StringUtils::Format("%u.%u.%u.%u", iver >> 24 & 0xFF,
      iver >> 16 & 0xFF, iver >> 8  & 0xFF, iver & 0xFF);
  return version.c_str();
}

const char *GetConnectionString(void)
{
  if (!DvbData)
    return "Not initialized!";

  static std::string conn;
  const Settings &settings = DvbData->GetSettings();
  conn = StringUtils::Format("%s:%u", settings.m_hostname.c_str(),
      settings.m_webPort);

  if (!DvbData->IsConnected())
    conn += " (Not connected!)";
  return conn.c_str();
}

const char *GetBackendHostname(void)
{
  if (!DvbData)
    return "Unknown";
  return DvbData->GetSettings().m_hostname.c_str();
}

PVR_ERROR GetSignalStatus(int channelUid, PVR_SIGNAL_STATUS *signalStatus)
{
  // the RS api doesn't provide information about signal quality (yet)
  PVR_STRCPY(signalStatus->strAdapterName, "DVBViewer Media Server");
  PVR_STRCPY(signalStatus->strAdapterStatus, "OK");
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

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, int iChannelUid,
    time_t start, time_t end)
{
  return (DvbData && DvbData->IsConnected()
      && DvbData->GetEPGForChannel(handle, iChannelUid, start, end))
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
  *size = 0;
  if (DvbData && DvbData->IsConnected())
    DvbData->GetTimerTypes(types, size);
  return PVR_ERROR_NO_ERROR;
}

int GetTimersAmount(void)
{
  if (!DvbData || !DvbData->IsConnected())
    return 0;

  return DvbData->GetTimersAmount();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
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

PVR_ERROR GetStreamReadChunkSize(int* chunksize)
{
  if (!chunksize)
    return PVR_ERROR_INVALID_PARAMETERS;
  int size = DvbData->GetSettings().m_streamReadChunkSize;
  if (!size)
    return PVR_ERROR_NOT_IMPLEMENTED;
  *chunksize = DvbData->GetSettings().m_streamReadChunkSize * 1024;
  return PVR_ERROR_NO_ERROR;
}

/* live stream functions */
bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (!DvbData || !DvbData->IsConnected())
    return false;

  if (!DvbData->OpenLiveStream(channel))
    return false;

  const Settings &settings = DvbData->GetSettings();

  /* queue a warning if the timeshift buffer path does not exist */
  if (settings.m_timeshift != Timeshift::OFF
      && !settings.IsTimeshiftBufferPathValid())
    XBMC->QueueNotification(QUEUE_ERROR, LocalizedString(30514).c_str());

  std::string streamURL = DvbData->GetLiveStreamURL(channel);
  strReader = new StreamReader(streamURL, settings);
  if (settings.m_timeshift == Timeshift::ON_PLAYBACK)
    strReader = new TimeshiftBuffer(strReader, settings);
  return strReader->Start();
}

void CloseLiveStream(void)
{
  DvbData->CloseLiveStream();
  SAFE_DELETE(strReader);
}

bool IsRealTimeStream()
{
  return (strReader) ? strReader->IsRealTime() : false;
}

bool CanPauseStream(void)
{
  if (!DvbData)
    return false;

  const Settings &settings = DvbData->GetSettings();
  if (settings.m_timeshift != Timeshift::OFF && strReader)
    return (strReader->IsTimeshifting() || settings.IsTimeshiftBufferPathValid());
  return false;
}

bool CanSeekStream(void)
{
  if (!DvbData)
    return false;

  // pause button seems to check CanSeekStream() too
  //return (strReader && strReader->IsTimeshifting());
  return (DvbData->GetSettings().m_timeshift != Timeshift::OFF);
}

int ReadLiveStream(unsigned char *buffer, unsigned int size)
{
  return (strReader) ? static_cast<int>(strReader->ReadData(buffer, size)) : 0;
}

long long SeekLiveStream(long long position, int whence)
{
  return (strReader) ? strReader->Seek(position, whence) : -1;
}

long long LengthLiveStream(void)
{
  return (strReader) ? strReader->Length() : -1;
}

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *times)
{
  if (!times)
    return PVR_ERROR_INVALID_PARAMETERS;

  int64_t timeStart, timeEnd;
  if (strReader)
  {
    timeStart = timeEnd = 0;
    if (strReader->IsTimeshifting())
    {
      timeStart = strReader->TimeStart();
      timeEnd   = strReader->TimeEnd();
    }
  }
  else if (recReader && recReader->TimeStart() > 0)
  {
    timeStart = recReader->TimeStart();
    timeEnd   = recReader->TimeRecorded();
  }
  else
    return PVR_ERROR_NOT_IMPLEMENTED;

  times->startTime = timeStart;
  times->ptsStart  = 0;
  times->ptsBegin  = 0;
  times->ptsEnd    = (timeEnd - timeStart) * DVD_TIME_BASE;
  return PVR_ERROR_NO_ERROR;
}

void PauseStream(bool paused)
{
  if (!DvbData)
    return;

  /* start timeshift on pause */
  const Settings &settings = DvbData->GetSettings();
  if (paused && settings.m_timeshift == Timeshift::ON_PAUSE
      && strReader && !strReader->IsTimeshifting()
      && settings.IsTimeshiftBufferPathValid())
  {
    strReader = new TimeshiftBuffer(strReader, settings);
    (void)strReader->Start();
  }
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

  return static_cast<int>(recReader->ReadData(buffer, size));
}

long long SeekRecordedStream(long long position, int whence)
{
  if (!recReader)
    return 0;

  return recReader->Seek(position, whence);
}

long long LengthRecordedStream(void)
{
  if (!recReader)
    return -1;

  return recReader->Length();
}

PVR_ERROR GetRecordingEdl(const PVR_RECORDING &recording, PVR_EDL_ENTRY edl[],
    int *size)
{
  if (!DvbData)
    return PVR_ERROR_SERVER_ERROR;

  if (!DvbData->GetSettings().m_edl.enabled)
  {
    *size = 0;
    return PVR_ERROR_NO_ERROR;
  }

  return (DvbData && DvbData->IsConnected()
      && DvbData->GetRecordingEdl(recording, edl, size))
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
  if (!DvbData || !DvbData->IsConnected())
    return PVR_ERROR_SERVER_ERROR;
  if (!DvbData->HasKVStore())
    return PVR_ERROR_NOT_IMPLEMENTED;
  return DvbData->SetRecordingPlayCount(recording, count)
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  if (!DvbData || !DvbData->IsConnected() || !DvbData->HasKVStore())
    return -1;
  return DvbData->GetRecordingLastPlayedPosition(recording);
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording,
  int position)
{
  if (!DvbData || !DvbData->IsConnected())
    return PVR_ERROR_SERVER_ERROR;
  if (!DvbData->HasKVStore())
    return PVR_ERROR_NOT_IMPLEMENTED;
  return DvbData->SetRecordingLastPlayedPosition(recording, position)
    ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

/** UNUSED API FUNCTIONS */
void OnSystemSleep(void) {}
void OnSystemWake(void) {}
void OnPowerSavingActivated(void) {}
void OnPowerSavingDeactivated(void) {}
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK&, const PVR_MENUHOOK_DATA&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL&) { return PVR_ERROR_NOT_IMPLEMENTED; }
DemuxPacket *DemuxRead(void) { return NULL; }
void DemuxAbort(void) {}
void DemuxReset(void) {}
void DemuxFlush(void) {}
void FillBuffer(bool mode) {}
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameRecording(const PVR_RECORDING&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR UndeleteRecording(const PVR_RECORDING&) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingSize(const PVR_RECORDING* recording, int64_t* sizeInBytes) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool SeekTime(double, bool, double*) { return false; }
void SetSpeed(int) {}
PVR_ERROR GetDescrambleInfo(int, PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }

}
