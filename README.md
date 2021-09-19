[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/kodi-pvr/pvr.dvbviewer/actions/workflows/build.yml/badge.svg?branch=Nexus)](https://github.com/kodi-pvr/pvr.dvbviewer/actions/workflows/build.yml)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/manuelm/pvr.dvbviewer?svg=true)](https://ci.appveyor.com/project/manuelm/pvr.dvbviewer)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/kodi-pvr/job/pvr.dvbviewer/job/Nexus/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/kodi-pvr%2Fpvr.dvbviewer/branches/)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5120/badge.svg)](https://scan.coverity.com/projects/5120)

# DVBViewer PVR
DVBViewer PVR client addon for [Kodi](https://kodi.tv)

supporting streaming of Live TV & Recordings, EPG, Timers.

## Build instructions

When building the addon you have to use the correct branch depending on which version of Kodi you're building against.
If you want to build the addon to be compatible with the latest kodi `master` commit, you need to checkout the branch with the current kodi codename.
Also make sure you follow this README from the branch in question.

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone --branch Nexus https://github.com/kodi-pvr/pvr.dvbviewer.git`
3. `cd pvr.dvbviewer && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.dvbviewer -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

The addon files will be placed in `../../xbmc/kodi-build/addons` so if you build Kodi from source and run it directly
the addon will be available as a system addon.

##### Useful links

* [DVBViewer PVR setup guide](https://kodi.wiki/view/Add-on:DVBViewer_Client)
* [DVBViewer PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=219)
* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)
