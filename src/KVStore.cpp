/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2013-2022 Manuel Mausz
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "KVStore.h"
#include "client.h"
#include "DvbData.h"

#include <kodi/tools/StringUtils.h>

#define CACHE_TTL 60

using namespace dvbviewer;

bool KVStore::IsErrorState() const
{
  return m_error;
}

void KVStore::Reset()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_error = false;
  m_cache.clear();

  /* UUID like section name for our keys. Prefixed for better readability.
   * Suffixed by our PVR instance/profile.
   */
  m_section = kodi::tools::StringUtils::Format("kodi-bfa5-4ac6-8bc2-profile%02x",
    m_cli.GetSettings().m_profileId);
}

void KVStore::OnError(errorfunc_t func)
{
  m_errorfuncs.emplace_back(func);
}

void KVStore::SetErrorState(const KVStore::Error err)
{
  m_error = true;
  for(auto func : m_errorfuncs)
    func(err);
}

bool KVStore::IsExpired(std::pair<std::chrono::steady_clock::time_point,
    std::string> &value) const
{
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  return ((now - value.first) > std::chrono::seconds(CACHE_TTL));
}

bool KVStore::InCoolDown() const
{
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  return ((now - m_lastRefresh) <= std::chrono::seconds(CACHE_TTL));
}

KVStore::Error KVStore::FetchAll()
{
  if (InCoolDown())
    return NOT_FOUND;

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI("api/store.html"
    "?action=read&sec=%s", m_section.c_str());
  if (res->error)
    return RESPONSE_ERROR;

  m_cache.clear();
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::string::size_type key_beg = 0, key_end;
  const std::string &s = res->content;
  while((key_end = s.find('=', key_beg)) != std::string::npos)
  {
    std::string key = s.substr(key_beg, key_end - key_beg);

    std::string::size_type val_end = s.find("\r\n", key_end);
    if (val_end == std::string::npos)
    {
      kodi::Log(ADDON_LOG_ERROR, "Unable to parse key-value entry: %s", key.c_str());
      return GENERIC_PARSE_ERROR;
    }

    std::string value = s.substr(key_end + 1, val_end - (key_end + 1));
    m_cache.emplace(key, std::make_pair(now, value));
    key_beg = val_end + 2;
  }

  m_lastRefresh = std::chrono::steady_clock::now();
  return SUCCESS;
}

KVStore::Error KVStore::FetchSingle(const std::string &key)
{
  if (InCoolDown())
    return NOT_FOUND;

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI("api/store.html"
    "?action=read&sec=%s&key=%s", m_section.c_str(), key.c_str());
  if (res->error)
    return RESPONSE_ERROR;

  m_cache[key] = std::make_pair(std::chrono::steady_clock::now(), res->content);
  return (res->content.empty()) ? NOT_FOUND : SUCCESS;
}

bool KVStore::Get(const std::string &key, std::string &value, Hint hint)
{
  if (IsErrorState())
    return false;

  std::unique_lock<std::mutex> lock(m_mutex);
  auto it = m_cache.find(key);
  if (it == m_cache.end() || IsExpired(it->second))
  {
    if (hint == KVStore::Hint::CACHE_ONLY)
      return false;

    KVStore::Error err = (hint == KVStore::Hint::FETCH_ALL)
      ? FetchAll() : FetchSingle(key);
    if (err == NOT_FOUND)
      return false;
    if (err != SUCCESS)
    {
      SetErrorState(err);
      return false;
    }
    lock.unlock();
    return Get(key, value, CACHE_ONLY);
  }

  /* empty value is a negative cache entry */
  if (it->second.second.empty())
    return false;

  value = it->second.second;
  return true;
}

bool KVStore::Set(const std::string &key, const std::string &value)
{
  if (IsErrorState() || value.empty())
    return false;

  std::unique_ptr<const Dvb::httpResponse> res = m_cli.GetFromAPI("api/store.html"
    "?action=write&sec=%s&key=%s&value=%s", m_section.c_str(),
    key.c_str(), value.c_str());
  if (res->error)
  {
    SetErrorState(RESPONSE_ERROR);
    return false;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  m_cache[key] = std::make_pair(std::chrono::steady_clock::now(), value);
  m_dirty = true;
  return true;
}

bool KVStore::Has(const std::string &key, Hint hint)
{
  std::string value;
  return Get(key, value, hint);
}

void KVStore::Save()
{
  if (IsErrorState() || !m_dirty)
    return;

  /* we don't care about the result */
  m_cli.GetFromAPI("api/store.html?action=updatefile");
  m_dirty = false;
}
