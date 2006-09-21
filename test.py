import torrent
from   time    import sleep

torrent.init()

myTorrent = torrent.addTorrent("ubuntu.torrent")

while True:
	print "STATE:"
	print torrent.getState(myTorrent)
	print ""

#	print "PEER INFO:"
	print torrent.getName(myTorrent)
#	print torrent.getPeerInfo(myTorrent)
#	print ""

	sleep(1)
