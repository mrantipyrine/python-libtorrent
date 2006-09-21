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
typedef handles_t::iterator handles_t_iterator;

// Global variables

session_settings *settings = NULL;
session          *ses = NULL;
handles_t        *handles = NULL;
PyObject         *constants;

// Internal functions

long get_torrent_index(torrent_handle &handle)
{
	for (unsigned long i = 0; i < handles->size(); i++)
		if ((*handles)[i] == handle)
			return i;

	assert(1 == 0);
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

	return (handles->size()-1);
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
	settings = new session_settings;
	ses      = new session;
	handles  = new handles_t;

	// Init values

	handles->reserve(10); // It doesn't cost us too much, 10 handles, does it? Reserve that space.

	settings->user_agent = "client_test " LIBTORRENT_VERSION;

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
	// Gracefully shutdown torrents
	for (handles_t::iterator i = handles->begin(); i != handles->end(); ++i)
	{
		torrent_handle& h = *i;
		if (!h.is_valid() || !h.has_metadata()) continue;

		h.pause();

		entry data = h.write_resume_data();

		std::stringstream s;
		s << h.get_torrent_info().name() << ".fastresume";

		boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);

		out.unsetf(std::ios_base::skipws);

		bencode(std::ostream_iterator<char>(out), data);

		ses->remove_torrent(h);
	}

	delete ses; // SLOWPOKE!
	delete settings;
	delete handles;

	Py_INCREF(constants);

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
	const char *name;
	PyArg_ParseTuple(args, "s", &name);

	return Py_BuildValue("i", internal_add_torrent(name, 0, true, "./"));
}

static PyObject *torrent_removeTorrent(PyObject *self, PyObject *args)
{
	long index;
	PyArg_ParseTuple(args, "i", &index);

	assert(index < handles->size());

	handles_t_iterator it = handles->begin() + index;

	handles->erase(it);

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
	long index;
	PyArg_ParseTuple(args, "i", &index);

	handles->at(index).pause();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_resume(PyObject *self, PyObject *args)
{
	long index;
	PyArg_ParseTuple(args, "i", &index);

	handles->at(index).resume();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_getName(PyObject *self, PyObject *args)
{
	long index;
	PyArg_ParseTuple(args, "i", &index);

	return Py_BuildValue("s", handles->at(index).get_torrent_info().name().c_str());
}

static PyObject *torrent_getState(PyObject *self, PyObject *args)
{
	long index;
	PyArg_ParseTuple(args, "i", &index);

	torrent_status s = handles->at(index).status();

	return Py_BuildValue("{s:i,s:i,s:i,s:f,s:f,s:i,s:f,s:i,s:f,s:i,s:s,s:s,s:f,s:i,s:i,s:i,s:i}",
								"state",					s.state,
								"numPeers", 			s.num_peers,
								"numSeeds", 			s.num_seeds,
								"distributedCopies", s.distributed_copies,
								"downloadRate", 		s.download_rate,
								"totalDownload", 		long(s.total_download),
								"uploadRate", 			s.upload_rate,
								"totalUpload", 		long(s.total_upload),
								"ratio",					float(s.total_payload_download)/float(s.total_payload_upload),
								"trackerOK",			!s.current_tracker.empty(),
								"nextAnnounce",		boost::posix_time::to_simple_string(s.next_announce).c_str(),
								"tracker",				s.current_tracker.c_str(),
								"progress",				float(s.progress),
								"totalDone",			long(s.total_done),
								"totalPieces",			long(s.pieces),
								"piecesDone",			long(s.num_pieces),
								"blockSize",			long(s.block_size));
};

static PyObject *torrent_popEvent(PyObject *self, PyObject *args)
{
	std::auto_ptr<alert> a;

	a = ses->pop_alert();

	alert *poppedAlert = a.get();

	if (!poppedAlert)
		return Py_BuildValue("{s:i}", "eventType", EVENT_NULL);
	else if (dynamic_cast<torrent_finished_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<torrent_finished_alert*>(poppedAlert))->handle;

		return Py_BuildValue("{s:i,s:i}", "eventType", EVENT_FINISHED,
													 "torrentIndex", get_torrent_index(handle));
	} else if (dynamic_cast<peer_error_alert*>(poppedAlert))
	{
		peer_id peerID = (dynamic_cast<peer_error_alert*>(poppedAlert))->pid;

		return Py_BuildValue("{s:i,s:s,s:s}",	"eventType", EVENT_PEER_ERROR,
															"clientID",  identify_client(peerID).c_str(),
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
	long index;
	PyArg_ParseTuple(args, "i", &index);

	std::vector<peer_info> peers;
	handles->at(index).get_peer_info(peers);

	PyObject *peerInfo;

	PyObject *ret = PyTuple_New(peers.size());

	for (unsigned long i = 0; i < peers.size(); i++)
	{
		peerInfo = Py_BuildValue("{s:f,s:i,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:s}",
								"downloadSpeed", 			float(peers[i].down_speed),
								"totalDownload", 			long(peers[i].total_download),
								"uploadSpeed", 			float(peers[i].up_speed),
								"totalUpload", 			long(peers[i].total_upload),
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
								"client",					peers[i].client.c_str());

		PyTuple_SetItem(ret, i, peerInfo);
	};

	return ret;
};


static PyObject *torrent_constants(PyObject *self, PyObject *args)
{
	Py_INCREF(constants); return constants;
}

/*		if (i->downloading_piece_index >= 0)
		{
			out << progress_bar(
				i->downloading_progress / float(i->downloading_total), 15);
		}
		else
		{
			out << progress_bar(0.f, 15);
		}
*/

/*						for (int j = 0; j < i->blocks_in_piece; ++j)
						{
							char const* peer_str = peer_index(i->peer[j], peers);
#ifdef ANSI_TERMINAL_COLORS
							if (i->finished_blocks[j]) out << esc("32;7") << peer_str << esc("0");
							else if (i->requested_blocks[j]) out << peer_str;
							else out << "-";
#else
							if (i->finished_blocks[j]) out << "#";
							else if (i->requested_blocks[j]) out << peer_str;
							else out << "-";
#endif
						}
*/


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
	{"constants",						torrent_constants, 				METH_VARARGS, "Get the constants."},
	{NULL}        /* Sentinel */
};


PyMODINIT_FUNC
inittorrent(void)
{
	Py_InitModule("torrent", TorrentMethods);
}
