NOTICE: this project has moved to the Deluge project server: http://deluge-torrent.org

This project provides a Python module for the bittorrent protocol. The torrent engine itself is taken from the Sourceforge libtorrent project (and not the Rakshasa project, which happens to also be called 'libtorrent'). This project provides both the libtorrent engine and Python wrapper code for it.

python-libtorrent is made specifically for the Deluge bittorrent client, but may be used by other apps, of course. Note, however, that future development will probably be oriented towards features needed by Deluge.

Reason for python-libtorrent: the 'mainline' bittorrent client is already written in Python, and is open-source. However, its current license is not considered 'free software' by the Debian project (and therefore up-to-date packages do not appear in the Debian or Ubuntu repos). This is one reason for this project.

Note that the libtorrent code was released under a BSD-type license, and can be used under that license if downloaded from their website, http://libtorrent.sourceforge.net/ . This project (python-libtorrent), however, is released under the GPL license (version 2 or above); this includes all code appearing here, both the libtorrent core and the wrapper code.

