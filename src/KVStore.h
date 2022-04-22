/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace dvbviewer ATTR_DLL_LOCAL
{

/* forward declaration */
class Dvb;

class KVStore
{
public:
  enum Error
  {
    SUCCESS,
    NOT_FOUND,
    GENERIC_PARSE_ERROR,
    RESPONSE_ERROR,
  };

  enum Hint
  {
    NONE = 0,
    FETCH_ALL,
    CACHE_ONLY,
  };

  KVStore(Dvb &cli)
    : m_cli(cli)
  {};
  KVStore(const KVStore &kvstore) = delete;

  bool IsErrorState() const;
  void Reset();

  typedef std::function<void (const KVStore::Error err)> errorfunc_t;
  void OnError(errorfunc_t func);

  bool Get(const std::string &key, std::string &value, Hint hint = Hint::NONE);
  bool Set(const std::string &key, const std::string &value);
  bool Has(const std::string &key, Hint hint = Hint::NONE);
  void Save();

  template <typename T>
  bool Get(const std::string &key, T &value, Hint hint = Hint::NONE)
  {
    std::string tmp;
    if (!Get(key, tmp, hint))
      return false;

    std::istringstream ss(tmp);
    ss >> value;
    if (!ss.eof() || ss.fail())
    {
      SetErrorState(GENERIC_PARSE_ERROR);
      return false;
    }
    return true;
  }

  template <typename T>
  bool Set(const std::string &key, const T &value)
  {
    return Set(key, std::to_string(value));
  }

private:
  void SetErrorState(const KVStore::Error err);
  bool IsExpired(std::pair<std::chrono::steady_clock::time_point, std::string>
      &value) const;
  bool InCoolDown() const;
  Error FetchAll();
  Error FetchSingle(const std::string &key);

  Dvb &m_cli;
  std::atomic<bool> m_error = { false };
  std::vector<errorfunc_t> m_errorfuncs;
  std::string m_section;
  std::map<std::string,
      std::pair<std::chrono::steady_clock::time_point, std::string> > m_cache;
  std::chrono::steady_clock::time_point m_lastRefresh;
  std::atomic<bool> m_dirty = { false };
  std::mutex m_mutex;
};

} //namespace dvbviewer
