[![Build Status](https://travis-ci.org/kodi-pvr/pvr.dvbviewer.svg?branch=master)](https://travis-ci.org/kodi-pvr/pvr.dvbviewer)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5120/badge.svg)](https://scan.coverity.com/projects/5120)

# DVBViewer PVR
DVBViewer PVR client addon for [Kodi] (http://kodi.tv)

supporting streaming of Live TV & Recordings, EPG, Timers.

## Build instructions

### Linux

1. `git clone https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/kodi-pvr/pvr.dvbviewer.git`
3. `cd pvr.dvbviewer && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.dvbviewer -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/project/cmake/addons`
5. `make`

##### Useful links

* [DVBViewer PVR setup guide] (http://kodi.wiki/view/Add-on:DVBViewer_Client)
* [DVBViewer PVR user support] (http://forum.kodi.tv/forumdisplay.php?fid=219)
* [Kodi's PVR user support] (http://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support] (http://forum.kodi.tv/forumdisplay.php?fid=136)
