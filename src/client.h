/*
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2020 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "DvbData.h"

#include "p8-platform/threads/threads.h"

#include <kodi/AddonBase.h>

class ATTRIBUTE_HIDDEN CDVBViewerAddon
  : public kodi::addon::CAddonBase
{
public:
  CDVBViewerAddon() = default;

  ADDON_STATUS CreateInstance(int instanceType, const std::string& instanceID,
      KODI_HANDLE instance, const std::string& version,
      KODI_HANDLE& addonInstance) override;
  void DestroyInstance(int instanceType, const std::string& instanceID,
      KODI_HANDLE addonInstance) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
      const kodi::CSettingValue& settingValue) override;

private:
  dvbviewer::Dvb* m_dvbData = nullptr;
  P8PLATFORM::CMutex m_mutex;
};
