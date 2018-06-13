#include "Settings.h"
#include "client.h"
#include "DvbData.h"

using namespace dvbviewer;
using namespace ADDON;

Settings::Settings()
{
  ResetBackendSettings();
}

void Settings::ReadFromKodi()
{
  //TODO
}

void Settings::ResetBackendSettings()
{
  m_priority = 50;
  m_recordingTask = "";
}

bool Settings::ReadFromBackend(Dvb &cli)
{
  ResetBackendSettings();

  const Dvb::httpResponse &res = cli.GetFromAPI(
    "api/getconfigfile.html?file=config%%5Cservice.xml");
  if (res.error)
    return false;

  TiXmlDocument doc;
  doc.Parse(res.content.c_str());
  if (doc.Error())
  {
    XBMC->Log(LOG_ERROR, "Unable to parse service.xml. Error: %s",
        doc.ErrorDesc());
    return false;
  }

  if (auto xRecording = doc.RootElement()->FirstChildElement("Recording"))
  {
    for (auto xEntry = xRecording->FirstChildElement("entry");
      xEntry; xEntry = xEntry->NextSiblingElement("entry"))
    {
      const char *name = xEntry->Attribute("name");
      if (!strcmp(name, "DefPrio"))
        m_priority = atoi(xEntry->GetText());
      else if (!strcmp(name, "DefTask"))
        m_recordingTask = xEntry->GetText();
    }
  }

  return true;
}
