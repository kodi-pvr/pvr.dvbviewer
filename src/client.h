/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "DvbData.h"

#include <mutex>

#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL CDVBViewerAddon
  : public kodi::addon::CAddonBase
{
public:
  CDVBViewerAddon() = default;

  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
      KODI_ADDON_INSTANCE_HDL& hdl) override;
  void DestroyInstance(const kodi::addon::IInstanceInfo& instance,
      const KODI_ADDON_INSTANCE_HDL hdl) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
      const kodi::addon::CSettingValue& settingValue) override;

private:
  dvbviewer::Dvb* m_dvbData = nullptr;
  std::mutex m_mutex;
};
