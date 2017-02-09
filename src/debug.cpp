//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "debug.h"
#include "log.h"
#include "log-impl.h"
#include "util.h"
#include "alldata.h"
#include "taccount.h"
#include "tpanel-pimpl.h"
#include "db-intl.h"
#include "uiutil.h"

// Do not run this at any point other than at termination after writing all state to DB and closing DB connection
void DebugFinalChecks() {
	#ifndef __WINDOWS__
	if (logimpl_flags & LOGIMPLF::LOGMEMUSAGE && currentlogflags & (LOGT::OTHERTRACE | LOGT::USERREQ)) {
		DebugDestructAlldata();
	}
	#endif
}

// Do not run this at any point other than at termination after writing all state to DB and closing DB connection
void DebugDestructAlldata() {
	#ifndef __WINDOWS__
	auto logflags = LOGT::OTHERTRACE | LOGT::USERREQ;
	LogMsgFormat(logflags, "DebugDestructAlldata: START");

	dbc.~dbconn();
	new (&dbc) dbconn();
	LogMsgFormat(logflags, "Cleared: dbc");

	ad.unloaded_db_tweet_ids.clear();
	LogMsgFormat(logflags, "Cleared: ad.unloaded_db_tweet_ids");

	ad.loaded_db_tweet_ids.clear();
	LogMsgFormat(logflags, "Cleared: ad.loaded_db_tweet_ids");

	ad.unloaded_db_user_ids.clear();
	LogMsgFormat(logflags, "Cleared: ad.unloaded_db_user_ids");

	ad.cids.foreach([](tweetidset &ids) {
		ids.clear();
	});
	LogMsgFormat(logflags, "Cleared: ad.cids");

	ad.incoming_filter.clear();
	ad.alltweet_filter.clear();
	LogMsgFormat(logflags, "Cleared: filters");

	ad.noacc_pending_tweetobjs.clear();
	LogMsgFormat(logflags, "Cleared: ad.noacc_pending_tweetobjs");

	ad.noacc_pending_userconts.clear();
	LogMsgFormat(logflags, "Cleared: ad.noacc_pending_userconts");

	ad.img_media_map.clear();
	LogMsgFormat(logflags, "Cleared: ad.img_media_map");

	ad.media_list.clear();
	LogMsgFormat(logflags, "Cleared: ad.media_list");

	ad.tpanels.clear();
	LogMsgFormat(logflags, "Cleared: ad.tpanels");

	for (auto &it : ad.tweetobjs) {
		it.second->pending_ops.clear();
	}
	ad.tweetobjs.clear();
	LogMsgFormat(logflags, "Cleared: ad.tweetobjs");

	for (auto &it : ad.userconts) {
		it.second->pendingtweets.clear();
	}
	ad.userconts.clear();
	LogMsgFormat(logflags, "Cleared: ad.userconts");

	ad.~alldata();
	new (&ad) alldata();
	LogMsgFormat(logflags, "Cleared: ad");

	alist.clear();
	LogMsgFormat(logflags, "Cleared: alist");

	tpanelparentwin_nt_impl::all_tweetid_count_map.clear();
	LogMsgFormat(logflags, "Cleared: tpanelparentwin_nt_impl::all_tweetid_count_map");

	tpanelparentwin_usertweets_impl::usertpanelmap.clear();
	LogMsgFormat(logflags, "Cleared: tpanelparentwin_usertweets_impl::usertpanelmap");

	tpanelparentwin_user_impl::pendingmap.clear();
	LogMsgFormat(logflags, "Cleared: tpanelparentwin_user_impl::pendingmap");

	tamd.clear();
	LogMsgFormat(logflags, "Cleared: tamd");

	LogMsgFormat(logflags, "DebugDestructAlldata: END");
	#endif
}
