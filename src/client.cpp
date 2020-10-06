/*
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2020 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "client.h"
#include "DvbData.h"
#include "Settings.h"

ADDON_STATUS CDVBViewerAddon::CreateInstance(int instanceType,
    const std::string& instanceID, KODI_HANDLE instance,
    const std::string& version, KODI_HANDLE& addonInstance)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (instanceType == ADDON_INSTANCE_PVR)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: Creating DVBViewer PVR-Client", __FUNCTION__);

    dvbviewer::Settings settings;
    settings.ReadFromKodi();
    m_dvbData = new dvbviewer::Dvb(instance, version, settings);
    addonInstance = m_dvbData;

    return ADDON_STATUS_OK;
  }

  return ADDON_STATUS_UNKNOWN;
}

void CDVBViewerAddon::DestroyInstance(int instanceType,
    const std::string& instanceID, KODI_HANDLE addonInstance)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  kodi::Log(ADDON_LOG_DEBUG, "%s: Destroying DVBViewer PVR-Client", __FUNCTION__);

  // delete becomes done from Kodi's header
  m_dvbData = nullptr;
}

ADDON_STATUS CDVBViewerAddon::SetSetting(const std::string& settingName,
    const kodi::CSettingValue& settingValue)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!m_dvbData)
    return ADDON_STATUS_OK;

  return m_dvbData->GetSettings().SetValue(settingName, settingValue);
}

ADDONCREATOR(CDVBViewerAddon)
