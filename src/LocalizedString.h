/*
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2020 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "client.h"

#include <string>

namespace dvbviewer
{

class LocalizedString
{
public:
  explicit LocalizedString(int id)
  {
    Load(id);
  }

  bool Load(int id)
  {
    char *str;
    if ((str = XBMC->GetLocalizedString(id)))
    {
      m_localizedString = str;
      XBMC->FreeString(str);
      return true;
    }

    m_localizedString = "";
    return false;
  }

  std::string Get()
  {
    return m_localizedString;
  }

  operator std::string()
  {
    return Get();
  }

  const char* c_str()
  {
    return m_localizedString.c_str();
  }

private:
  LocalizedString() = delete;
  LocalizedString(const LocalizedString&) = delete;
  LocalizedString &operator =(const LocalizedString&) = delete;

  std::string m_localizedString;
};

} //namespace dvbviewer
