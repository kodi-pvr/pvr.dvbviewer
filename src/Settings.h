#pragma once

#include <string>

class Dvb;

namespace dvbviewer
{

class Settings
{
public:
  Settings();
  void ReadFromKodi();
  bool ReadFromBackend(Dvb &cli);

private:
  void ResetBackendSettings();

public:
  /* settings fetched from backend */
  int m_priority;
  std::string m_recordingTask;
};

} //namespace dvbviewer
