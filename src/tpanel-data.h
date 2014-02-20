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

#ifndef HGUARD_SRC_TPANEL_DATA
#define HGUARD_SRC_TPANEL_DATA

#include "univdefs.h"
#include "tpanel-common.h"
#include "twit-common.h"
#include <forward_list>
#include <vector>
#include <string>
#include <memory>

struct tpanelparentwin_nt;
struct tpanelparentwin;
struct tweet;
struct mainframe;
struct taccount;

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::string dispname;
	tweetidset tweetlist;
	std::forward_list<tpanelparentwin_nt*> twin;
	flagwrapper<TPF> flags;
	uint64_t upperid;
	uint64_t lowerid;
	cached_id_sets cids;
	std::vector<tpanel_auto> tpautos;

	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_ = 0, std::shared_ptr<taccount> *acc = 0);
	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_);
	tpanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_);		//don't use this directly
	~tpanel();

	static void NameDefaults(std::string &name, std::string &dispname, const std::vector<tpanel_auto> &tpautos);
	static std::string ManualName(std::string dispname);

	void PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	void RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	bool RegisterTweet(tweet_ptr_p t);
	bool UnRegisterTweet(uint64_t id);
	tpanelparentwin *MkTPanelWin(mainframe *parent, bool select = false);
	void OnTPanelWinClose(tpanelparentwin_nt *tppw);
	bool IsSingleAccountTPanel() const;
	void SetNoUpdateFlag_TP() const;
	void SetClabelUpdatePendingFlag_TP() const;
	void UpdateCLabelLater_TP() const;
	bool TweetMatches(tweet_ptr_p t, const std::shared_ptr<taccount> &acc) const;

	//id must correspond to a usable tweet in ad.tweetobjs, if adding
	void NotifyCIDSChange(uint64_t id, tweetidset cached_id_sets::*ptr, bool add, flagwrapper<PUSHFLAGS> pushflags  = PUSHFLAGS::DEFAULT);
	void NotifyCIDSChange_AddRemove(uint64_t id, tweetidset cached_id_sets::*ptr, bool add, flagwrapper<PUSHFLAGS> pushflags  = PUSHFLAGS::DEFAULT);
	void RecalculateCIDS();

	private:
	enum class TPIF {
		RECALCSETSONCIDSCHANGE       = 1<<0,
		INCCIDS_HIGHLIGHT            = 1<<1,
		INCCIDS_UNREAD               = 1<<2,
	};
	flagwrapper<TPIF> intl_flags;

	void RecalculateSets();
	void RecalculateTweetSet();
	void NotifyCIDSChange_AddRemoveIntl(uint64_t id, tweetidset cached_id_sets::*ptr, bool add, flagwrapper<PUSHFLAGS> pushflags);
};
template<> struct enum_traits<tpanel::TPIF> { static constexpr bool flags = true; };

inline void tpanel::NotifyCIDSChange_AddRemove(uint64_t id, tweetidset cached_id_sets::*ptr, bool add, flagwrapper<PUSHFLAGS> pushflags) {
	if(intl_flags & tpanel::TPIF::RECALCSETSONCIDSCHANGE) NotifyCIDSChange_AddRemoveIntl(id, ptr, add, pushflags);
}

#endif
