/* 
Copyright: A. Zakai ('Kripken') <kripkensteiner@gmail.com> http://6thsenseless.blogspot.com

2006-15-9

This code is licensed under the terms of the GNU General Public License (GPL),
version 2 or above; See /usr/share/common-licenses/GPL , or see
http://www.fsf.org/licensing/licenses/gpl.html

Some code portions were derived from work by Arvid Norberg.
*/


#include <Python.h>

#include <boost/filesystem/exception.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"

//#include <iostream>
//#include <fstream>

//Needed just for debug purposes
#include <stdio.h>

using namespace libtorrent;
using boost::filesystem::path;


//-----------------
// START
//-----------------

#define EVENT_NULL            0
#define EVENT_FINISHED        1
#define EVENT_PEER_ERROR      2
#define EVENT_INVALID_REQUEST 3
#define EVENT_OTHER           4

#define STATE_QUEUED           0
#define STATE_CHECKING         1
#define STATE_CONNECTING       2
#define STATE_DOWNLOADING_META 3
#define STATE_DOWNLOADING      4
#define STATE_FINISHED         5
#define STATE_SEEDING          6
#define STATE_ALLOCATING       7


typedef std::vector<torrent_handle> handles_t;
typedef std::vector<long> 				uniqueIDs_t;
typedef handles_t::iterator 			handles_t_iterator;
typedef uniqueIDs_t::iterator 		uniqueIDs_t_iterator;
typedef std::vector<bool>				filterOut_t;
typedef std::vector<filterOut_t>		filterOuts_t;
typedef filterOuts_t::iterator		filterOuts_t_iterator;

// Global variables

session_settings *settings 		= NULL;
session          *ses 				= NULL;
handles_t        *handles 			= NULL;
uniqueIDs_t      *uniqueIDs		= NULL;
filterOuts_t     *filterOuts		= NULL;
PyObject         *constants;
long					uniqueCounter	= 0;

// Internal functions

bool empty_name_check(const std::string & name)
{
	return 1;
}

long get_torrent_index(torrent_handle &handle)
{
	for (unsigned long i = 0; i < handles->size(); i++)
		if ((*handles)[i] == handle)
			return i;

	assert(1 == 0);
	return -1;
}

long get_index_from_unique(long uniqueID)
{
	assert(handles->size() == uniqueIDs->size());

	for (unsigned long i = 0; i < uniqueIDs->size(); i++)
		if ((*uniqueIDs)[i] == uniqueID)
			return i;

	assert(1 == 0);
	printf("Critical Error! No such uniqueID (%d, %d)\r\n", int(uniqueID), int(uniqueIDs->size()));
	return -1;
}

long internal_add_torrent(std::string const& torrent
	, float preferred_ratio
	, bool compact_mode
	, path const& save_path)
{
	std::ifstream in(torrent.c_str(), std::ios_base::binary);
	in.unsetf(std::ios_base::skipws);
	entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
	torrent_info t(e);

//	std::cout << t.name() << "\n";

	entry resume_data;
	try
	{
		std::stringstream s;
		s << t.name() << ".fastresume";
		boost::filesystem::ifstream resume_file(save_path / s.str(), std::ios_base::binary);
		resume_file.unsetf(std::ios_base::skipws);
		resume_data = bdecode(
			std::istream_iterator<char>(resume_file)
			, std::istream_iterator<char>());
	}
	catch (invalid_encoding&) {}
	catch (boost::filesystem::filesystem_error&) {}

	torrent_handle h = ses->add_torrent(t, save_path, resume_data
		, compact_mode, 16 * 1024);

	handles->push_back(h);

	h.set_max_connections(60);
	h.set_max_uploads(-1);
	h.set_ratio(preferred_ratio);

	uniqueIDs->push_back(uniqueCounter);
	uniqueCounter++;

	long numFiles = h.get_torrent_info().num_files();

	filterOuts->push_back(filterOut_t(numFiles));

	filterOut_t &newFilterOut = filterOuts->at(filterOuts->size()-1);

	for (long i = 0; i < numFiles; i++)
		newFilterOut.at(i) = 0;

	assert(handles->size() == uniqueIDs->size());
	assert(handles->size() == filterOuts->size());

	return (uniqueCounter - 1);
}

void internal_remove_torrent(long index)
{
	assert(index < handles->size());

	torrent_handle& h = handles->at(index);

	// For valid torrents, save fastresume data
	if (h.is_valid() && h.has_metadata())
	{
		h.pause();

		entry data = h.write_resume_data();

		std::stringstream s;
		s << h.get_torrent_info().name() << ".fastresume";

		boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);

		out.unsetf(std::ios_base::skipws);

		bencode(std::ostream_iterator<char>(out), data);
	}

	ses->remove_torrent(h);

	handles_t_iterator it = handles->begin() + index;
	handles->erase(it);

	uniqueIDs_t_iterator it2 = uniqueIDs->begin() + index;
	uniqueIDs->erase(it2);

	filterOuts_t_iterator it3 = filterOuts->begin() + index;
	filterOuts->erase(it3);

	assert(handles->size() == uniqueIDs->size());
	assert(handles->size() == filterOuts->size());
}

long get_peer_index(libtorrent::tcp::endpoint addr, std::vector<peer_info> const& peers)
{
	long index = -1;

	for (unsigned long i = 0; i < peers.size(); i++)
		if (peers[i].ip == addr)
			index = i;

	return index;
}


//=====================
// External functions
//=====================

static PyObject *torrent_init(PyObject *self, PyObject *args)
{
	// Tell Boost that we are on *NIX, so bloody '.'s are ok inside a directory name!
	path::default_name_check(empty_name_check);

	char *clientID, *userAgent;
	long v1,v2,v3,v4;

	PyArg_ParseTuple(args, "siiiis", &clientID, &v1, &v2, &v3, &v4, &userAgent);

	settings   = new session_settings;
	ses        = new session(libtorrent::fingerprint(clientID, v1, v2, v3, v4));
	handles    = new handles_t;
	uniqueIDs  = new uniqueIDs_t;
	filterOuts = new filterOuts_t;

	// Init values

	handles->reserve(10); // It doesn't cost us too much, 10 handles, does it? Reserve that space.
	uniqueIDs->reserve(10);
	filterOuts->reserve(10);

	settings->user_agent = std::string(userAgent) + " (libtorrent " LIBTORRENT_VERSION ")";

//	printf("ID: %s\r\n", clientID);
//	printf("User Agent: %s\r\n", settings->user_agent.c_str());

//	std::deque<std::string> events;

	ses->set_max_half_open_connections(-1);
	ses->set_download_rate_limit(-1);
	ses->set_upload_rate_limit(-1);
	ses->listen_on(std::make_pair(19335, 19335 + 10), ""); // 6881, usually
	ses->set_settings(*settings);
	ses->set_severity_level(alert::debug);
//			ses.set_severity_level(alert::warning);
//			ses.set_severity_level(alert::fatal);
//			ses.set_severity_level(alert::info);

/*	// Load old DHT data
	std::ifstream dht_file;
	dht_file.open("dht.data", std::ios::in | std::ios::ate | std::ios::binary);
	if (dht_file.is_open())
	{
		long size = dht_file.tellg();
		char * memblock = new char[size];
		dht_file.seekg(0, std::ios::beg);
		dht_file.read(memblock, size);
		dht_file.close();
		std::vector<char> buffer(memblock, memblock+size);
		delete[] memblock;
		entry old_state = bdecode(buffer.begin(), buffer.end());

		ses->start_dht();//old_state);
		printf("DHT initialized from file. %d bytes read\r\n", int(size));
	} else
		printf("No DHT file found.\r\n");
*/
	constants = Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
										"EVENT_NULL",					EVENT_NULL,
										"EVENT_FINISHED",				EVENT_FINISHED,
										"EVENT_PEER_ERROR",			EVENT_PEER_ERROR,
										"EVENT_INVALID_REQUEST",	EVENT_INVALID_REQUEST,
										"EVENT_OTHER",					EVENT_OTHER,
										"STATE_QUEUED",				STATE_QUEUED,
										"STATE_CHECKING",				STATE_CHECKING,
										"STATE_CONNECTING",			STATE_CONNECTING,
										"STATE_DOWNLOADING_META",	STATE_DOWNLOADING_META,
										"STATE_DOWNLOADING",			STATE_DOWNLOADING,
										"STATE_FINISHED",				STATE_FINISHED,
										"STATE_SEEDING",				STATE_SEEDING,
										"STATE_ALLOCATING",			STATE_ALLOCATING);

	Py_INCREF(Py_None); return Py_None;
};

static PyObject *torrent_quit(PyObject *self, PyObject *args)
{
	long Num = handles->size();

	// Shut down torrents gracefully
	for (long i = 0; i < Num; i++)
		internal_remove_torrent(0);

/*	// Shut down DHT gracefully, saving the current state
	entry curr_state = ses->dht_state();
	std::vector<char> buffer;
	bencode(std::back_inserter(buffer), curr_state);
	std::ofstream dht_file;
	dht_file.open("dht.data", std::ios::out | std::ios::trunc | std::ios::binary);
	dht_file.write(&buffer[0], buffer.size());
	dht_file.close();
	printf("DHT saved to file. %d bytes saved\r\n", buffer.size());

	ses->stop_dht();
*/
	delete ses; // SLOWPOKE because of waiting for the trackers before shutting down
	delete settings;
	delete handles;
	delete uniqueIDs;

	Py_DECREF(constants);

	Py_INCREF(Py_None); return Py_None;
};

static PyObject *torrent_setMaxHalfOpenConnections(PyObject *self, PyObject *args)
{
	long arg;
	PyArg_ParseTuple(args, "i", &arg);

	ses->set_max_half_open_connections(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setDownloadRateLimit(PyObject *self, PyObject *args)
{
	long arg;
	PyArg_ParseTuple(args, "i", &arg);

	ses->set_download_rate_limit(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setUploadRateLimit(PyObject *self, PyObject *args)
{
	long arg;
	PyArg_ParseTuple(args, "i", &arg);

	ses->set_upload_rate_limit(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setListenOn(PyObject *self, PyObject *args)
{
	long port;
	PyArg_ParseTuple(args, "i", &port);

	ses->listen_on(std::make_pair(port, port + 10), "");

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_isListening(PyObject *self, PyObject *args)
{
	long ret = (ses->is_listening() != 0);

	return Py_BuildValue("i", ret);
}

static PyObject *torrent_listeningPort(PyObject *self, PyObject *args)
{
	return Py_BuildValue("i", (long)ses->listen_port());
}

static PyObject *torrent_setMaxUploads(PyObject *self, PyObject *args)
{
	long max_up;
	PyArg_ParseTuple(args, "i", &max_up);

	ses->set_max_uploads(max_up);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setMaxConnections(PyObject *self, PyObject *args)
{
	long max_conn;
	PyArg_ParseTuple(args, "i", &max_conn);

	ses->set_max_connections(max_conn);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_addTorrent(PyObject *self, PyObject *args)
{
	const char *name, *saveDir;
	PyArg_ParseTuple(args, "ss", &name, &saveDir);

	path saveDir_2	(saveDir);// 	empty_name_check);

	return Py_BuildValue("i", internal_add_torrent(name, 0, true, saveDir_2));
}

static PyObject *torrent_removeTorrent(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	internal_remove_torrent(index);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_getNumTorrents(PyObject *self, PyObject *args)
{
	return Py_BuildValue("i", handles->size());
}

static PyObject *torrent_reannounce(PyObject *self, PyObject *args)
{
	long index;
	PyArg_ParseTuple(args, "i", &index);

	handles->at(index).force_reannounce();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_pause(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	handles->at(index).pause();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_resume(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	handles->at(index).resume();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_getName(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	return Py_BuildValue("s", handles->at(index).get_torrent_info().name().c_str());
}

static PyObject *torrent_getState(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	torrent_status			 s = handles->at(index).status();
	const torrent_info	&i = handles->at(index).get_torrent_info();

	std::vector<peer_info> peers;
	handles->at(index).get_peer_info(peers);

	long total_seeds = 0;
	long total_peers = 0;

	for (unsigned long i = 0; i < peers.size(); i++)
		if (peers[i].seed)
			total_seeds++;
		else
			total_peers++;

	return Py_BuildValue("{s:l,s:l,s:l,s:f,s:f,s:d,s:f,s:l,s:f,s:l,s:s,s:s,s:f,s:d,s:l,s:l,s:l,s:d,s:l,s:l,s:l,s:l}",
								"state",					s.state,
								"numPeers", 			s.num_peers,
								"numSeeds", 			s.num_seeds,
								"distributedCopies", s.distributed_copies,
								"downloadRate", 		s.download_rate,
								"totalDownload", 		double(s.total_download),
								"uploadRate", 			s.upload_rate,
								"totalUpload", 		long(s.total_upload),
								"ratio",					float(s.total_payload_download)/float(s.total_payload_upload),
								"trackerOK",			!s.current_tracker.empty(),
								"nextAnnounce",		boost::posix_time::to_simple_string(s.next_announce).c_str(),
								"tracker",				s.current_tracker.c_str(),
								"progress",				float(s.progress),
								"totalDone",			double(s.total_done),
								"totalPieces",			long(s.pieces),
								"piecesDone",			long(s.num_pieces),
								"blockSize",			long(s.block_size),
								"totalSize",			double(i.total_size()),
								"pieceLength",			long(i.piece_length()),
								"numPieces",			long(i.num_pieces()),
								"totalSeeds",			total_seeds,
								"totalPeers",			total_peers);
};

static PyObject *torrent_popEvent(PyObject *self, PyObject *args)
{
	std::auto_ptr<alert> a;

	a = ses->pop_alert();

	alert *poppedAlert = a.get();

	if (!poppedAlert)
	{
		Py_INCREF(Py_None); return Py_None;
	} else if (dynamic_cast<torrent_finished_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<torrent_finished_alert*>(poppedAlert))->handle;

		return Py_BuildValue("{s:i,s:i}", "eventType", EVENT_FINISHED,
													 "uniqueID",  uniqueIDs->at(get_torrent_index(handle)));
	} else if (dynamic_cast<peer_error_alert*>(poppedAlert))
	{
		peer_id     peerID = (dynamic_cast<peer_error_alert*>(poppedAlert))->pid;
		std::string peerIP = (dynamic_cast<peer_error_alert*>(poppedAlert))->ip.address().to_string();

		return Py_BuildValue("{s:i,s:s,s:s}",	"eventType", EVENT_PEER_ERROR,
															"clientID",  identify_client(peerID).c_str(),
															"clientIP",  peerIP.c_str(),
															"message",   a->msg().c_str()                 );
	} else if (dynamic_cast<invalid_request_alert*>(poppedAlert))
	{
		peer_id peerID = (dynamic_cast<invalid_request_alert*>(poppedAlert))->pid;

		return Py_BuildValue("{s:i,s:s,s:s}",  "eventType", EVENT_INVALID_REQUEST,
															"clientID",  identify_client(peerID).c_str(),
													 		"message",   a->msg().c_str()                 );
	}

	return Py_BuildValue("{s:i,s:s}", "eventType", EVENT_OTHER,
												 "message",   a->msg().c_str()     );
}

static PyObject *torrent_hasIncomingConnections(PyObject *self, PyObject *args)
{
	session_status sess_stat = ses->status();

	return Py_BuildValue("i", sess_stat.has_incoming_connections);
}

static PyObject *torrent_getPeerInfo(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	std::vector<peer_info> peers;
	handles->at(index).get_peer_info(peers);

	PyObject *peerInfo;

	PyObject *ret = PyTuple_New(peers.size());

	for (unsigned long i = 0; i < peers.size(); i++)
	{
		std::vector<bool> &pieces      = peers[i].pieces;
		unsigned long      pieces_had  = 0;

		for (unsigned long piece = 0; piece < pieces.size(); piece++)
			if (pieces[piece])
				pieces_had++;

		peerInfo = Py_BuildValue(
								"{s:f,s:d,s:f,s:d,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:s,s:i,s:s,s:f}",
								"downloadSpeed", 			float(peers[i].down_speed),
								"totalDownload", 			double(peers[i].total_download),
								"uploadSpeed", 			float(peers[i].up_speed),
								"totalUpload", 			double(peers[i].total_upload),
								"downloadQueueLength",  long(peers[i].download_queue_length),
								"uploadQueueLength", 	long(peers[i].upload_queue_length),
								"isInteresting",			long((peers[i].flags & peer_info::interesting) != 0),
								"isChoked",					long((peers[i].flags & peer_info::choked) != 0),
								"isRemoteInterested",	long((peers[i].flags & peer_info::remote_interested) != 0),
								"isRemoteChoked",			long((peers[i].flags & peer_info::remote_choked) != 0),
								"SupportsExtensions",	long((peers[i].flags & peer_info::supports_extensions) != 0),
								"isLocalConnection",		long((peers[i].flags & peer_info::local_connection) != 0),
								"isAwaitingHandshake",	long((peers[i].flags & peer_info::handshake) != 0),
								"isConnecting",			long((peers[i].flags & peer_info::connecting) != 0),
								"isQueued",					long((peers[i].flags & peer_info::queued) != 0),
								"client",					peers[i].client.c_str(),
								"isSeed",					long(peers[i].seed),
								"ip",							peers[i].ip.address().to_string().c_str(),
								"peerHas",					float(float(pieces_had)*100.0/pieces.size())
									);

		PyTuple_SetItem(ret, i, peerInfo);
	};

	return ret;
};

static PyObject *torrent_getFileInfo(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	std::vector<PyObject *> tempFiles;

	PyObject *fileInfo;

	std::vector<float> progresses;

	handles->at(index).file_progress(progresses);

	torrent_info::file_iterator start = handles->at(index).get_torrent_info().begin_files();
	torrent_info::file_iterator end   = handles->at(index).get_torrent_info().end_files();

	long fileIndex = 0;

	filterOut_t &filterOut = filterOuts->at(index);

	for(torrent_info::file_iterator i = start; i != end; ++i)
	{
		file_entry const &currFile = (*i);

		fileInfo = Py_BuildValue(
								"{s:s,s:d,s:d,s:f,s:i}",
								"path",				currFile.path.string().c_str(),
								"offset", 			double(currFile.offset),
								"size", 				double(currFile.size),
								"progress",			progresses[i - start]*100.0,
								"filteredOut",		long(filterOut.at(fileIndex))
										);

		fileIndex++;

		tempFiles.push_back(fileInfo);
	};

	PyObject *ret = PyTuple_New(tempFiles.size());
	
	for (unsigned long i = 0; i < tempFiles.size(); i++)
		PyTuple_SetItem(ret, i, tempFiles[i]);

	return ret;
};

static PyObject *torrent_setFilterOut(PyObject *self, PyObject *args)
{
	long uniqueID;
	PyObject *filterOutObject;
	PyArg_ParseTuple(args, "iO", &uniqueID, &filterOutObject);
	long index = get_index_from_unique(uniqueID);

	long numFiles = handles->at(index).get_torrent_info().num_files();
	assert(PyList_Size(filterOutObject) ==  numFiles);

	for (long i = 0; i < numFiles; i++)
	{
		filterOuts->at(index).at(i) = PyInt_AsLong(PyList_GetItem(filterOutObject, i));
	};

	handles->at(index).filter_files(filterOuts->at(index));

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_constants(PyObject *self, PyObject *args)
{
	Py_INCREF(constants); return constants;
}


//====================
// Python Module data
//====================

static PyMethodDef TorrentMethods[] = {
	{"init",                      torrent_init,   METH_VARARGS, "Initialize the torrenting engine."},
	{"quit",                      torrent_quit,   METH_VARARGS, "Shutdown the torrenting engine."},
	{"setMaxHalfOpenConnections", torrent_setMaxHalfOpenConnections, METH_VARARGS, "Shutdown the torrenting engine."},
	{"setDownloadRateLimit",      torrent_setDownloadRateLimit, METH_VARARGS, "Set the download speed limit."},
	{"setUploadRateLimit",        torrent_setUploadRateLimit,   METH_VARARGS, "Set the upload speed limit."},
	{"setListenOn",               torrent_setListenOn,          METH_VARARGS, "Set the port to listen on."},
	{"isListening",				   torrent_isListening,				METH_VARARGS, "Are we listening ok?"},
	{"listeningPort",				   torrent_listeningPort,			METH_VARARGS, "Port we ended up on."},
	{"setMaxUploads",             torrent_setMaxUploads,        METH_VARARGS, "Set the max uploads to do."},
	{"setMaxConnections",         torrent_setMaxConnections,    METH_VARARGS, "Set max connections."},
	{"addTorrent",                torrent_addTorrent,           METH_VARARGS, "Add a torrent."},
	{"removeTorrent",             torrent_removeTorrent,        METH_VARARGS, "Remove a torrent."},
	{"getNumTorrents",            torrent_getNumTorrents,       METH_VARARGS, "Get number of torrents."},
	{"reannounce",                torrent_reannounce,           METH_VARARGS, "Reannounce a torrent."},
	{"pause",                     torrent_pause,                METH_VARARGS, "Pause a torrent."},
	{"resume",                    torrent_resume,               METH_VARARGS, "Resume a torrent."},
	{"getName",                   torrent_getName,              METH_VARARGS, "Gets the name of a torrent."},
	{"getState",                  torrent_getState,             METH_VARARGS, "Get torrent state."},
	{"popEvent",                  torrent_popEvent,             METH_VARARGS, "Pops an event."},
	{"hasIncomingConnections",    torrent_hasIncomingConnections, METH_VARARGS, "Has Incoming Connections?"},
	{"getPeerInfo",					torrent_getPeerInfo, 			METH_VARARGS, "Get all peer info."},
	{"getFileInfo",					torrent_getFileInfo, 			METH_VARARGS, "Get all file info."},
	{"setFilterOut",					torrent_setFilterOut, 			METH_VARARGS, "."},
	{"constants",						torrent_constants, 				METH_VARARGS, "Get the constants."},
	{NULL}        /* Sentinel */
};


PyMODINIT_FUNC
inittorrent(void)
{
	Py_InitModule("torrent", TorrentMethods);
}
