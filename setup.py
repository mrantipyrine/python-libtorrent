from distutils.core import setup, Extension

module1 = Extension('torrent',
                    include_dirs = ['./include','./include/libtorrent', '/usr/include/python2.4'],
                    libraries = ['boost_filesystem', 'boost_date_time', 'boost_program_options',
											'boost_regex', 'boost_serialization', 'boost_thread', 'z', 'pthread'],
                    sources = ['alert.cpp',              'identify_client.cpp',  'storage.cpp',
										 'allocate_resources.cpp', 'ip_filter.cpp',        'torrent.cpp',
										 'bt_peer_connection.cpp', 'peer_connection.cpp',  'torrent_handle.cpp',
										 'entry.cpp',              'piece_picker.cpp',     'torrent_info.cpp',
										 'escape_string.cpp',      'policy.cpp',           'tracker_manager.cpp',
										 'file.cpp',               'session.cpp',          'udp_tracker_connection.cpp',
                               'sha1.cpp',               'web_peer_connection.cpp',
										 'http_tracker_connection.cpp',  'stat.cpp',
										 'python-libtorrent.cpp'])

setup (name = 'Python-libtorrent',
       version = '1.0',
       description = 'Wrapper code for libtorrent C++ torrent library (Sourceforge, not Rakshasa)',
       ext_modules = [module1])

