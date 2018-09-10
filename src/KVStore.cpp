#include "KVStore.h"
#include "client.h"
#include "DvbData.h"

#include "p8-platform/util/StringUtils.h"

#define CACHE_TTL 60

using namespace dvbviewer;
using namespace ADDON;

bool KVStore::IsSupported() const
{
  return m_cli.GetBackendVersion() >= DMS_VERSION_NUM(2, 1, 2, 0);
}

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
   * Can be suffixed by PVR instance/profile at a later time.
   */
  m_section = StringUtils::Format("kodi-bfa5-4ac6-8bc2-profile%02x", 0);
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

bool KVStore::IsExpired(std::pair<std::time_t, std::string> &value) const
{
  return value.first + CACHE_TTL < std::time(nullptr);
}

KVStore::Error KVStore::FetchAll()
{
  if (m_lastRefresh + CACHE_TTL >= std::time(nullptr))
    return NOT_FOUND;

  const Dvb::httpResponse &res = m_cli.GetFromAPI("api/store.html"
    "?action=read&sec=%s", m_section.c_str());
  if (res.error)
    return RESPONSE_ERROR;

  std::time_t now = std::time(nullptr);
  std::string::size_type key_beg = 0, key_end;
  const std::string &s = res.content;
  while((key_end = s.find('=', key_beg)) != std::string::npos)
  {
    std::string key = s.substr(key_beg, key_end - key_beg);

    std::string::size_type val_end = s.find("\r\n", key_end);
    if (val_end == std::string::npos)
    {
      XBMC->Log(LOG_ERROR, "Unable to parse key-value entry: %s", key.c_str());
      return GENERIC_PARSE_ERROR;
    }

    std::string value = s.substr(key_end + 1, val_end - (key_end + 1));
    m_cache.emplace(key, std::make_pair(now, value));
    key_beg = val_end + 2;
  }

  m_lastRefresh = std::time(nullptr);
  return SUCCESS;
}

KVStore::Error KVStore::FetchSingle(const std::string &key)
{
  if (m_lastRefresh + CACHE_TTL >= std::time(nullptr))
    return NOT_FOUND;

  const Dvb::httpResponse &res = m_cli.GetFromAPI("api/store.html"
    "?action=read&sec=%s&key=%s", m_section.c_str(), key.c_str());
  if (res.error)
    return RESPONSE_ERROR;

  m_cache[key] = std::make_pair(std::time(nullptr), res.content);
  return (res.content.empty()) ? NOT_FOUND : SUCCESS;
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

  const Dvb::httpResponse &res = m_cli.GetFromAPI("api/store.html"
    "?action=write&sec=%s&key=%s&value=%s", m_section.c_str(),
    key.c_str(), value.c_str());
  if (res.error)
  {
    SetErrorState(RESPONSE_ERROR);
    return false;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  m_cache[key] = std::make_pair(std::time(nullptr), value);
  return true;
}

bool KVStore::Has(const std::string &key, Hint hint)
{
  std::string value;
  return Get(key, value, hint);
}
