#pragma once
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

#ifndef PVR_DVBVIEWER_CLIENT_H
#define PVR_DVBVIEWER_CLIENT_H

#include "DvbData.h"
#include "libXBMC_addon.h"
#include "libXBMC_pvr.h"

#ifndef _UNUSED
#if defined(__GNUC__)
# define _UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define _UNUSED(x) /*@unused@*/ x
#else
# define _UNUSED(x) x
#endif
#endif

/*!
 * @brief PVR macros for string exchange
 */
#define PVR_STRCPY(dest, source) do { strncpy(dest, source, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0'; } while(0)
#define PVR_STRCLR(dest) memset(dest, 0, sizeof(dest))

#define DEFAULT_HOST                 "127.0.0.1"
#define DEFAULT_WEB_PORT             8089
#define DEFAULT_TSBUFFERPATH         "special://userdata/addon_data/pvr.dvbviewer"
#define DEFAULT_RECORDING_EDL_FOLDER ""

enum class Timeshift
  : int // same type as addon settings
{
  OFF = 0,
  ON_PLAYBACK,
  ON_PAUSE
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

extern std::string    g_hostname;
extern int            g_webPort;
extern std::string    g_username;
extern std::string    g_password;
extern bool           g_useWoL;
extern std::string    g_mac;
extern bool           g_useFavourites;
extern bool           g_useFavouritesFile;
extern std::string    g_favouritesFile;
extern DvbRecording::Grouping g_groupRecordings;
extern bool           g_enable_recording_edl;
extern std::string    g_recordingEdlFolder;
extern int            g_recording_edl_start_padding;
extern int            g_recording_edl_end_padding ;
extern Timeshift      g_timeshift;
extern std::string    g_timeshiftBufferPath;
extern PrependOutline g_prependOutline;
extern bool           g_lowPerformance;
extern Transcoding    g_transcoding;
extern std::string    g_transcodingParams;

extern ADDON::CHelper_libXBMC_addon *XBMC;
extern CHelper_libXBMC_pvr *PVR;

#endif
