/*
 *  Copyright © 2006 Alon Zakai ('Kripken') <kripkensteiner@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Thank You: Some code portions were derived from BSD-licensed work by
 *             Arvid Norberg, and GPL-licensed work by Christophe Dumez
 */


#include <Python.h>

#include <boost/filesystem/exception.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/ip_filter.hpp"

#include <boost/filesystem/operations.hpp>


//#include <fstream>

//Needed just for debug purposes
#include <stdio.h>

//// Debug only
//#include <iostream.h> 

using namespace libtorrent;
using boost::filesystem::path;

//-----------------
// START
//-----------------

#ifdef AMD64
#define pythonLong int
#else
#define pythonLong long
#endif

#define EVENT_NULL            				0
#define EVENT_FINISHED        				1
#define EVENT_PEER_ERROR      				2
#define EVENT_INVALID_REQUEST 				3
#define EVENT_FILE_ERROR						4
#define EVENT_HASH_FAILED_ERROR				5
#define EVENT_PEER_BAN_ERROR					6
#define EVENT_FASTRESUME_REJECTED_ERROR	8
#define EVENT_TRACKER							9
#define EVENT_OTHER				           	10

#define STATE_QUEUED           0
#define STATE_CHECKING         1
#define STATE_CONNECTING       2
#define STATE_DOWNLOADING_META 3
#define STATE_DOWNLOADING      4
#define STATE_FINISHED         5
#define STATE_SEEDING          6
#define STATE_ALLOCATING       7

#define DHT_ROUTER_PORT 6881

#define ERROR_INVALID_ENCODING  -10
#define ERROR_FILESYSTEM        -20
#define ERROR_DUPLICATE_TORRENT -30


typedef std::vector<torrent_handle> handles_t;
typedef std::vector<long> 				uniqueIDs_t;
typedef handles_t::iterator 			handles_t_iterator;
typedef uniqueIDs_t::iterator 		uniqueIDs_t_iterator;
typedef std::vector<bool>				filterOut_t;
typedef std::vector<filterOut_t>		filterOuts_t;
typedef filterOuts_t::iterator		filterOuts_t_iterator;
typedef std::vector<std::string>		torrentNames_t;
typedef torrentNames_t::iterator		torrentNames_t_iterator;

// Global variables

session_settings *settings 		= NULL;
session          *ses 				= NULL;
handles_t        *handles 			= NULL;
uniqueIDs_t      *uniqueIDs		= NULL;
filterOuts_t     *filterOuts		= NULL;
PyObject         *constants		= NULL;
long					uniqueCounter	= 0;
torrentNames_t   *torrentNames   = NULL;

// Internal functions

bool empty_name_check(const std::string & name)
{
	return 1;
}

long handle_exists(torrent_handle &handle)
{
	for (unsigned long i = 0; i < handles->size(); i++)
		if ((*handles)[i] == handle)
			return 1;

	return 0;
}

long get_torrent_index(torrent_handle &handle)
{
	for (unsigned long i = 0; i < handles->size(); i++)
		if ((*handles)[i] == handle)
		{
//			printf("Found: %li\r\n", i);
			return i;
		}

	printf("Handle not found!\r\n");
	assert(1 == 0);
	return -1;
}

void print_uniqueIDs()
{
//#ifdef AMD64
//	for (unsigned long i = 0; i < uniqueIDs->size(); i++)
//		printf("--uniqueIDs[%ld] = %ld\r\n", i, (*uniqueIDs)[i]);
//#endif
}

long get_index_from_unique(long uniqueID)
{
	assert(handles->size() == uniqueIDs->size());

//#ifdef AMD64
//	printf("Request for uniqueID: %ld\r\n", uniqueID);
//#endif
	print_uniqueIDs();

	for (unsigned long i = 0; i < uniqueIDs->size(); i++)
		if ((*uniqueIDs)[i] == uniqueID)
			return i;

	assert(1 == 0);
	printf("Critical Error! No such uniqueID (%ld, %ld)\r\n", uniqueID, (long)uniqueIDs->size());
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
		s << torrent << ".fastresume";
//		printf("Loading fastresume from: %s\r\n", s.str().c_str());
		boost::filesystem::ifstream resume_file(s.str(), std::ios_base::binary);
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

//	h.set_max_connections(60); // Setting it only works once...
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

	torrentNames->push_back(torrent);

	assert(handles->size() == torrentNames->size());

//	printf("Added torrent, uniqueID: %ld\r\n", uniqueCounter - 1);
//	print_uniqueIDs();

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
		s << torrentNames->at(index) << ".fastresume";
//		printf("Saving fastresume to: %s\r\n", s.str().c_str());
		boost::filesystem::ofstream out(s.str(), std::ios_base::binary);

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

	torrentNames_t_iterator it4 = torrentNames->begin() + index;
	torrentNames->erase(it4);

	assert(handles->size() == uniqueIDs->size());
	assert(handles->size() == filterOuts->size());
	assert(handles->size() == torrentNames->size());
}

long get_peer_index(libtorrent::tcp::endpoint addr, std::vector<peer_info> const& peers)
{
	long index = -1;

	for (unsigned long i = 0; i < peers.size(); i++)
		if (peers[i].ip == addr)
			index = i;

	return index;
}

// The following function contains code by Christophe Dumez and Arvid Norberg
void internal_add_files(torrent_info& t, path const& p, path const& l)
{
	path f(p / l); // change default checker, perhaps?
	if (is_directory(f))
	{
		for (boost::filesystem::directory_iterator i(f), end; i != end; ++i)
			internal_add_files(t, p, l / i->leaf());
	} else
		t.add_file(l, file_size(f));
}


//=====================
// External functions
//=====================

static PyObject *torrent_init(PyObject *self, PyObject *args)
{
	printf("python-libtorrent, using libtorrent %s. Compiled with NDEBUG value: %d\r\n",
			 LIBTORRENT_VERSION,
			 NDEBUG);

	// Tell Boost that we are on *NIX, so bloody '.'s are ok inside a directory name!
	path::default_name_check(empty_name_check);

	char *clientID, *userAgent;
	pythonLong v1,v2,v3,v4;

	PyArg_ParseTuple(args, "siiiis", &clientID, &v1, &v2, &v3, &v4, &userAgent);

	settings   		= new session_settings;
	ses        		= new session(libtorrent::fingerprint(clientID, v1, v2, v3, v4));
	handles    		= new handles_t;
	uniqueIDs  		= new uniqueIDs_t;
	filterOuts 		= new filterOuts_t;
	torrentNames	= new torrentNames_t;

	// Init values

	handles->reserve(10); // It doesn't cost us too much, 10 handles, does it? Reserve that space.
	uniqueIDs->reserve(10);
	filterOuts->reserve(10);
	torrentNames->reserve(10);

	settings->user_agent = std::string(userAgent);// + " (libtorrent " LIBTORRENT_VERSION ")";

//	printf("ID: %s\r\n", clientID);
//	printf("User Agent: %s\r\n", settings->user_agent.c_str());

//	std::deque<std::string> events;

	ses->set_max_half_open_connections(-1);
	ses->set_download_rate_limit(-1);
	ses->set_upload_rate_limit(-1);
//	ses->listen_on(std::make_pair(6881, 6889), ""); // 6881, usually
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
	constants = Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
										"EVENT_NULL",					EVENT_NULL,
										"EVENT_FINISHED",				EVENT_FINISHED,
										"EVENT_PEER_ERROR",			EVENT_PEER_ERROR,
										"EVENT_INVALID_REQUEST",	EVENT_INVALID_REQUEST,
										"EVENT_FILE_ERROR",			EVENT_FILE_ERROR,
										"EVENT_HASH_FAILED_ERROR",	EVENT_HASH_FAILED_ERROR,
										"EVENT_PEER_BAN_ERROR",		EVENT_PEER_BAN_ERROR,
										"EVENT_FASTRESUME_REJECTED_ERROR", EVENT_FASTRESUME_REJECTED_ERROR,
										"EVENT_TRACKER",				EVENT_TRACKER,
										"EVENT_OTHER",					EVENT_OTHER,
										"STATE_QUEUED",				STATE_QUEUED,
										"STATE_CHECKING",				STATE_CHECKING,
										"STATE_CONNECTING",			STATE_CONNECTING,
										"STATE_DOWNLOADING_META",	STATE_DOWNLOADING_META,
										"STATE_DOWNLOADING",			STATE_DOWNLOADING,
										"STATE_FINISHED",				STATE_FINISHED,
										"STATE_SEEDING",				STATE_SEEDING,
										"STATE_ALLOCATING",			STATE_ALLOCATING,
										"ERROR_INVALID_ENCODING",	ERROR_INVALID_ENCODING,
										"ERROR_FILESYSTEM",			ERROR_FILESYSTEM,
										"ERROR_DUPLICATE_TORRENT",	ERROR_DUPLICATE_TORRENT);

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
	pythonLong arg;
	PyArg_ParseTuple(args, "i", &arg);

	ses->set_max_half_open_connections(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setDownloadRateLimit(PyObject *self, PyObject *args)
{
	pythonLong arg;
	PyArg_ParseTuple(args, "i", &arg);
printf("Capping download to %d bytes per second\r\n", (int)arg);
	ses->set_download_rate_limit(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setUploadRateLimit(PyObject *self, PyObject *args)
{
	pythonLong arg;
	PyArg_ParseTuple(args, "i", &arg);
printf("Capping upload to %d bytes per second\r\n", (int)arg);
	ses->set_upload_rate_limit(arg);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setListenOn(PyObject *self, PyObject *args)
{
	pythonLong portStart, portEnd;
	PyArg_ParseTuple(args, "ii", &portStart, &portEnd);

	ses->listen_on(std::make_pair(portStart, portEnd), "");

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_isListening(PyObject *self, PyObject *args)
{
	long ret = (ses->is_listening() != 0);

	return Py_BuildValue("i", ret);
}

static PyObject *torrent_listeningPort(PyObject *self, PyObject *args)
{
	return Py_BuildValue("i", (pythonLong)ses->listen_port());
}

static PyObject *torrent_setMaxUploads(PyObject *self, PyObject *args)
{
	pythonLong max_up;
	PyArg_ParseTuple(args, "i", &max_up);

	ses->set_max_uploads(max_up);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_setMaxConnections(PyObject *self, PyObject *args)
{
	pythonLong max_conn;
	PyArg_ParseTuple(args, "i", &max_conn);

//	printf("Setting max connections: %d\r\n", max_conn);
	ses->set_max_connections(max_conn);

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_addTorrent(PyObject *self, PyObject *args)
{
	const char *name, *saveDir;
	PyArg_ParseTuple(args, "ss", &name, &saveDir);

	path saveDir_2	(saveDir, empty_name_check);

	try
	{
		return Py_BuildValue("i", internal_add_torrent(name, 0, true, saveDir_2));
	}
	catch (invalid_encoding&)
	{
		return Py_BuildValue("i", ERROR_INVALID_ENCODING);
	}
	catch (boost::filesystem::filesystem_error&)
	{
		return Py_BuildValue("i", ERROR_FILESYSTEM);
	}
	catch (duplicate_torrent&)
	{
		return Py_BuildValue("i", ERROR_DUPLICATE_TORRENT);
	}
}

static PyObject *torrent_removeTorrent(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
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
	pythonLong uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	handles->at(index).force_reannounce();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_pause(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	handles->at(index).pause();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_resume(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	handles->at(index).resume();

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_getName(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
	PyArg_ParseTuple(args, "i", &uniqueID);
	long index = get_index_from_unique(uniqueID);

	return Py_BuildValue("s", handles->at(index).get_torrent_info().name().c_str());
}

static PyObject *torrent_getState(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
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

	return Py_BuildValue("{s:l,s:l,s:l,s:f,s:f,s:d,s:f,s:l,s:f,s:l,s:s,s:s,s:f,s:d,s:l,s:l,s:l,s:d,s:l,s:l,s:l,s:l,s:l,s:l,s:d,s:d,s:l,s:l}",
								"state",					s.state,
								"numPeers", 			s.num_peers,
								"numSeeds", 			s.num_seeds,
								"distributedCopies", s.distributed_copies,
								"downloadRate", 		s.download_rate,
								"totalDownload", 		double(s.total_payload_download),
								"uploadRate", 			s.upload_rate,
								"totalUpload", 		long(s.total_payload_upload),
								"ratio",					float(-1),//float(s.total_payload_download)/float(s.total_payload_upload),
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
								"totalPeers",			total_peers,
								"isPaused",				long(handles->at(index).is_paused()),
								"isSeed",				long(handles->at(index).is_seed()),
								"totalWanted",			double(s.total_wanted),
								"totalWantedDone",	double(s.total_wanted_done),
								"numComplete",			long(s.num_complete),
								"numIncomplete",		long(s.num_incomplete));
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

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i}", "eventType", EVENT_FINISHED,
														 "uniqueID",  uniqueIDs->at(get_torrent_index(handle)));
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<peer_error_alert*>(poppedAlert))
	{
		peer_id     peerID = (dynamic_cast<peer_error_alert*>(poppedAlert))->pid;
		std::string peerIP = (dynamic_cast<peer_error_alert*>(poppedAlert))->ip.address().to_string();

		return Py_BuildValue("{s:i,s:s,s:s,s:s}",	"eventType", EVENT_PEER_ERROR,
																"clientID",  identify_client(peerID).c_str(),
																"ip",			 peerIP.c_str(),
																"message",   a->msg().c_str()                 );
	} else if (dynamic_cast<invalid_request_alert*>(poppedAlert))
	{
		peer_id peerID = (dynamic_cast<invalid_request_alert*>(poppedAlert))->pid;

		return Py_BuildValue("{s:i,s:s,s:s}",  "eventType", EVENT_INVALID_REQUEST,
															"clientID",  identify_client(peerID).c_str(),
													 		"message",   a->msg().c_str()                 );
	} else if (dynamic_cast<file_error_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<file_error_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s}",  "eventType", EVENT_FILE_ERROR,
																"uniqueID",  uniqueIDs->at(get_torrent_index(handle)),
														 		"message",   a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<hash_failed_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<hash_failed_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:i,s:s}",  "eventType",  EVENT_HASH_FAILED_ERROR,
																"uniqueID",   uniqueIDs->at(get_torrent_index(handle)),
																"pieceIndex", long((dynamic_cast<hash_failed_alert*>(poppedAlert))->piece_index),
														 		"message",    a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<peer_ban_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<peer_ban_alert*>(poppedAlert))->handle;
		std::string peerIP = (dynamic_cast<peer_ban_alert*>(poppedAlert))->ip.address().to_string();

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s,s:s}",  "eventType",  EVENT_PEER_BAN_ERROR,
																"uniqueID",   uniqueIDs->at(get_torrent_index(handle)),
																"ip",			  peerIP.c_str(),
														 		"message",    a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<fastresume_rejected_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<fastresume_rejected_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s}",  "eventType",  EVENT_FASTRESUME_REJECTED_ERROR,
																"uniqueID",   uniqueIDs->at(get_torrent_index(handle)),
														 		"message",    a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<tracker_announce_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<tracker_announce_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s,s:s}", "eventType",  		EVENT_TRACKER,
																	"uniqueID",   		uniqueIDs->at(get_torrent_index(handle)),
																	"trackerStatus",	"Announce sent",
														 			"message",    		a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<tracker_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<tracker_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s,s:s}", "eventType",  		EVENT_TRACKER,
																	"uniqueID",   		uniqueIDs->at(get_torrent_index(handle)),
																	"trackerStatus",	"Bad response (status code=?)",
														 			"message",    		a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<tracker_reply_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<tracker_reply_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s,s:s}", "eventType",  		EVENT_TRACKER,
																	"uniqueID",   		uniqueIDs->at(get_torrent_index(handle)),
																	"trackerStatus",	"Announce succeeded",
														 			"message",    		a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	} else if (dynamic_cast<tracker_warning_alert*>(poppedAlert))
	{
		torrent_handle handle = (dynamic_cast<tracker_warning_alert*>(poppedAlert))->handle;

		if (handle_exists(handle))
			return Py_BuildValue("{s:i,s:i,s:s,s:s}", "eventType",  		EVENT_TRACKER,
																	"uniqueID",   		uniqueIDs->at(get_torrent_index(handle)),
																	"trackerStatus",	"Warning in response",
														 			"message",    		a->msg().c_str()                 );
		else
		{ Py_INCREF(Py_None); return Py_None; }
	}

	return Py_BuildValue("{s:i,s:s}", "eventType", EVENT_OTHER,
												 "message",   a->msg().c_str()     );
}

static PyObject *torrent_getSessionInfo(PyObject *self, PyObject *args)
{
	session_status s = ses->status();

	return Py_BuildValue("{s:l,s:f,s:f,s:f,s:f,s:l}",
								"hasIncomingConnections",		long(s.has_incoming_connections),
								"uploadRate",						float(s.upload_rate),
								"downloadRate",					float(s.download_rate),
								"payloadUploadRate",				float(s.payload_upload_rate),
								"payloadDownloadRate",			float(s.payload_download_rate),
								"numPeers",							long(s.num_peers));
}

static PyObject *torrent_getPeerInfo(PyObject *self, PyObject *args)
{
	pythonLong uniqueID;
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
	pythonLong uniqueID;
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
	pythonLong uniqueID;
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

static PyObject *torrent_startDHT(PyObject *self, PyObject *args)
{
	const char *DHTpath;
	PyArg_ParseTuple(args, "s", &DHTpath);

	printf("Loading DHT state from %s\r\n", DHTpath);

	path tempPath(DHTpath, empty_name_check);
	boost::filesystem::ifstream dht_state_file(tempPath, std::ios_base::binary);
	dht_state_file.unsetf(std::ios_base::skipws);
	entry dht_state;
	try{
		dht_state = bdecode(std::istream_iterator<char>(dht_state_file),
								  std::istream_iterator<char>());
		ses->start_dht(dht_state);
	} catch (std::exception&) {
		printf("No DHT file to resume\r\n");
		ses->start_dht();
	}

	ses->add_dht_router(std::make_pair(std::string("router.bittorrent.com"), DHT_ROUTER_PORT));
	ses->add_dht_router(std::make_pair(std::string("router.utorrent.com"), DHT_ROUTER_PORT));
	ses->add_dht_router(std::make_pair(std::string("router.bitcomet.com"), DHT_ROUTER_PORT));

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_stopDHT(PyObject *self, PyObject *args)
{
	const char *DHTpath;
	PyArg_ParseTuple(args, "s", &DHTpath);

	printf("Saving DHT state to %s\r\n", DHTpath);

	path tempPath = path(DHTpath, empty_name_check);

	try {
		entry dht_state = ses->dht_state();
		boost::filesystem::ofstream out(tempPath, std::ios_base::binary);
		out.unsetf(std::ios_base::skipws);
		bencode(std::ostream_iterator<char>(out), dht_state);
	} catch (std::exception& e) {
		printf("An error occured in saving DHT\r\n");
      std::cerr << e.what() << "\n";
	}

	Py_INCREF(Py_None); return Py_None;
}

static PyObject *torrent_getDHTinfo(PyObject *self, PyObject *args)
{
//	printf("Pouring out DHT state:\r\n");
	entry DHTstate = ses->dht_state();
//	DHTstate.print(cout);

	entry *nodes = DHTstate.find_key("nodes");
	if (!nodes)
		return Py_BuildValue("l", -1); // No nodes - we are just starting up...

	entry::list_type &peers = nodes->list();
	entry::list_type::const_iterator i;

	pythonLong numPeers = 0;

	i = peers.begin();
	while (i != peers.end())
	{
//		printf("A:%s\r\n", i->string().c_str());
		numPeers++;
		i++;
	}
//	printf("All done.\r\n");
	return Py_BuildValue("l", numPeers);
//	Py_INCREF(Py_None); return Py_None;
}

// Create Torrents: call with something like:
// createTorrent("mytorrent.torrent", "directory or file to make a torrent out of",
//               "tracker1\ntracker2\ntracker3", "no comment", 256, "Deluge");
// That makes a torrent with pieces of 256K, with "Deluge" as the creator string.
//
// The following function contains code by Christophe Dumez and Arvid Norberg
static PyObject *torrent_createTorrent(PyObject *self, PyObject *args)
{
	char *destination, *comment, *creator_str, *input, *trackers;
	pythonLong piece_size;
	PyArg_ParseTuple(args, "ssssis", &destination, &input, &trackers, &comment, &piece_size, 													&creator_str);

	piece_size = piece_size * 1024;

	try
	{
		torrent_info t;
		path full_path = complete(path(input));
		boost::filesystem::ofstream out(complete(path(destination)), std::ios_base::binary);

		internal_add_files(t, full_path.branch_path(), full_path.leaf());
		t.set_piece_size(piece_size);

		storage st(t, full_path.branch_path());

		std::string stdTrackers(trackers);
		unsigned long index = 0, next = stdTrackers.find("\n");
		while (1 == 1)
		{
			t.add_tracker(stdTrackers.substr(index, next-index));
			index = next + 1;
			if (next >= stdTrackers.length())
				break;
			next = stdTrackers.find("\n", index);
			if (next == std::string::npos)
				break;
		}

		int num = t.num_pieces();
		std::vector<char> buf(piece_size);
		for (int i = 0; i < num; ++i)
		{
			st.read(&buf[0], i, 0, t.piece_size(i));
			hasher h(&buf[0], t.piece_size(i));
			t.set_hash(i, h.final());
		}

		t.set_creator(creator_str);
		t.set_comment(comment);

		entry e = t.create_torrent();
		libtorrent::bencode(std::ostream_iterator<char>(out), e);
		return Py_BuildValue("l", 1);
	} catch (std::exception& e)
	{
		std::cerr << e.what() << "\n";
		return Py_BuildValue("l", 0);
	}
}

static PyObject *torrent_applyIPFilter(PyObject *self, PyObject *args)
{
	PyObject *ranges;
	PyArg_ParseTuple(args, "O", &ranges);

	long numRanges = PyList_Size(ranges);

//	printf("Number of ranges: %l\r\n", numRanges);
//	Py_INCREF(Py_None); return Py_None;
	
	ip_filter theFilter;
	address_v4 from, to;
	PyObject *curr;

	printf("Can I 10.10.10.10? %d\r\n", theFilter.access(address_v4::from_string("10.10.10.10")));

	for (long i = 0; i < numRanges; i++)
	{
		curr = PyList_GetItem(ranges, i);
		from = address_v4::from_string(PyString_AsString(PyList_GetItem(curr, 0)));
		to   = address_v4::from_string(PyString_AsString(PyList_GetItem(curr, 1)));
		printf("Filtering: %s - %s\r\n", from.to_string().c_str(), to.to_string().c_str());
		theFilter.add_rule(from, to, ip_filter::blocked);
	};

	printf("Can I 10.10.10.10? %d\r\n", theFilter.access(address_v4::from_string("10.10.10.10")));

	ses->set_ip_filter(theFilter);

	printf("Can I 10.10.10.10? %d\r\n", theFilter.access(address_v4::from_string("10.10.10.10")));

	Py_INCREF(Py_None); return Py_None;
}


//====================
// Python Module data
//====================

static PyMethodDef TorrentMethods[] = {
	{"init",                      torrent_init,   METH_VARARGS, "."},
	{"quit",                      torrent_quit,   METH_VARARGS, "."},
	{"setMaxHalfOpenConnections", torrent_setMaxHalfOpenConnections, METH_VARARGS, "."},
	{"setDownloadRateLimit",      torrent_setDownloadRateLimit, METH_VARARGS,		 "."},
	{"setUploadRateLimit",        torrent_setUploadRateLimit,   METH_VARARGS,		 "."},
	{"setListenOn",               torrent_setListenOn,          METH_VARARGS,		 "."},
	{"isListening",				   torrent_isListening,				METH_VARARGS,		 "."},
	{"listeningPort",				   torrent_listeningPort,			METH_VARARGS,		 "."},
	{"setMaxUploads",             torrent_setMaxUploads,        METH_VARARGS,		 "."},
	{"setMaxConnections",         torrent_setMaxConnections,    METH_VARARGS,		 "."},
	{"addTorrent",                torrent_addTorrent,           METH_VARARGS,		 "."},
	{"removeTorrent",             torrent_removeTorrent,        METH_VARARGS,		 "."},
	{"getNumTorrents",            torrent_getNumTorrents,       METH_VARARGS,		 "."},
	{"reannounce",                torrent_reannounce,           METH_VARARGS, 		 "."},
	{"pause",                     torrent_pause,                METH_VARARGS, 		 "."},
	{"resume",                    torrent_resume,               METH_VARARGS,		 "."},
	{"getName",                   torrent_getName,              METH_VARARGS,		 "."},
	{"getState",                  torrent_getState,             METH_VARARGS, 		 "."},
	{"popEvent",                  torrent_popEvent,             METH_VARARGS, 		 "."},
	{"getSessionInfo",  				torrent_getSessionInfo, 		METH_VARARGS,		 "."},
	{"getPeerInfo",					torrent_getPeerInfo, 			METH_VARARGS, 		 "."},
	{"getFileInfo",					torrent_getFileInfo, 			METH_VARARGS, 		 "."},
	{"setFilterOut",					torrent_setFilterOut, 			METH_VARARGS, 		 "."},
	{"constants",						torrent_constants, 				METH_VARARGS,		 "."},
	{"startDHT",						torrent_startDHT, 				METH_VARARGS,		 "."},
	{"stopDHT",							torrent_stopDHT, 					METH_VARARGS,		 "."},
	{"getDHTinfo",						torrent_getDHTinfo, 				METH_VARARGS,		 "."},
	{"createTorrent",					torrent_createTorrent, 			METH_VARARGS,		 "."},
	{"applyIPFilter",					torrent_applyIPFilter, 			METH_VARARGS,		 "."},
	{NULL}        /* Sentinel */
};


PyMODINIT_FUNC
inittorrent(void)
{
	Py_InitModule("torrent", TorrentMethods);
}
