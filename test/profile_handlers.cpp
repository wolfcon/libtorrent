/*

Copyright (c) 2019, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent_info.hpp"

#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test_utils.hpp"

#include <tuple>
#include <functional>

#include <fstream>
#include <iostream>

using namespace lt;

int main()
{
	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_profile_handlers", ec);
	remove_all("tmp2_profile_handlers", ec);

	session_proxy p1;
	session_proxy p2;

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48075");
	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:49075");
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_profile_handlers", ec);
	if (ec) std::cerr << ec.message() << '\n';

	std::ofstream file("tmp1_profile_handlers/temporary");
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 32 * 1024, 13, false);
	file.close();

	add_torrent_params params;
	params.storage_mode = storage_mode_sparse;
	params.flags &= ~torrent_flags::paused;
	params.flags &= ~torrent_flags::auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	using std::ignore;
	std::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, nullptr
		, true, false, true, "_profile_handlers", 1024 * 1024, &t, false, &params);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(std::size_t(num_pieces), 1);

	lt::time_point const start_time = lt::clock_type::now();

	for (int i = 0; i < 20000; ++i)
	{
		if (lt::clock_type::now() - start_time > seconds(10))
		{
			std::cerr << "timeout\n";
			break;
		}
		// sleep a bit
		ses2.wait_for_alert(lt::milliseconds(100));

		torrent_status const st1 = tor1.status();
		torrent_status const st2 = tor2.status();

		// wait 10 loops before we restart the torrent. This lets
		// us catch all events that failed (and would put the torrent
		// back into upload mode) before we restart it.

		if (st2.is_seeding) break;

		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	if (!tor2.status().is_seeding)
	{
		std::cerr << "tor2 is no seeding\n";
		return 1;
	}

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();

	remove_all("tmp1_profile_handlers", ec);
	remove_all("tmp2_profile_handlers", ec);
}

