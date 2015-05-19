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
#include "tpanel.h"
#include "tpanel-data.h"
#include "log.h"
#include "twit.h"
#include "util.h"
#include "alldata.h"
#include "taccount.h"
#include "retcon.h"
#include "db.h"
#include <algorithm>
#include <array>

#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

bool tpanel::PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags) {
	return PushTweet(t->id, t, pushflags);
}

bool tpanel::PushTweet(uint64_t id, optional_tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags) {
	LogMsgFormat(LOGT::TPANELTRACE, "Pushing tweet id %" llFmtSpec "d to panel %s (pushflags: 0x%X)", id, cstr(name), pushflags);
	bool adding_tweet = RegisterTweet(id, t);
	if(adding_tweet) {
		for(auto &i : twin) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: Pushing tweet id %" llFmtSpec "d to tpanel window", id);
			#endif
			i->PushTweet(id, t, pushflags);
		}
	}
	else {	//already have this in tpanel, update it
		for(auto &i : twin) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: Updating tpanel window tweet: id %" llFmtSpec "d", id);
			#endif
			i->UpdateOwnTweet(id, false);
		}
	}
	return adding_tweet;
}

void tpanel::BulkPushTweet(tweetidset ids, flagwrapper<PUSHFLAGS> pushflags, optional_observer_ptr<tweetidset> actually_added) {
	if(!twin.empty()) {
		// There are windows for this panel open, slow path
		SetNoUpdateFlag_TP();
		for(auto &it : ids) {
			bool added = PushTweet(it, nullptr, pushflags);
			if(added && actually_added)
				actually_added->insert(it);
		}
		CheckClearNoUpdateFlag_TP();
	}
	else {
		if(!tweetlist.empty()) {
			// Panel already has stuff in it, merge sets
			for(auto &it : ids) {
				auto result = tweetlist.insert(it);
				if(result.second && actually_added)
					actually_added->insert(it);
			}
		}
		else {
			// Panel is empty, fast path
			if(actually_added)
				*actually_added = ids;
			tweetlist = std::move(ids);
		}
		RecalculateSets();
	}
}

bool tpanel::RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags) {
	LogMsgFormat(LOGT::TPANELTRACE, "Removing tweet id %" llFmtSpec "d from panel %s (pushflags: 0x%X)", id, cstr(name), pushflags);
	bool removing_tweet = UnRegisterTweet(id);
	if(removing_tweet) {
		for(auto &i : twin) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: Removing tweet id %" llFmtSpec "d from tpanel window", id);
			#endif
			i->RemoveTweet(id, pushflags);
		}
	}
	return removing_tweet;
}

//returns true if new tweet
bool tpanel::RegisterTweet(uint64_t id, optional_tweet_ptr_p t) {
	if(t)
		cids.CheckTweet(*t);
	else
		cids.CheckTweetID(id);

	if(tweetlist.count(id)) {
		//already have this tweet
		return false;
	}
	else {
		tweetlist.insert(id);
		return true;
	}
}

//returns true if tweet was present
bool tpanel::UnRegisterTweet(uint64_t id) {
	if(tweetlist.count(id)) {
		tweetlist.erase(id);
		cids.RemoveTweet(id);
		return true;
	}
	else {
		return false;
	}
}

tpanel::tpanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_, std::vector<tpanel_auto_udc> tpudcautos_)
: name(name_), dispname(dispname_), flags(flags_) {
	twin.clear();
	tpautos = std::move(tpautos_);
	tpudcautos = std::move(tpudcautos_);
	for(auto &it : tpautos) {
		if(it.autoflags & TPF::AUTO_HIGHLIGHTED) {
			intl_flags |= TPIF::RECALCSETSONCIDSCHANGE | TPIF::INCCIDS_HIGHLIGHT;
		}
		if(it.autoflags & TPF::AUTO_UNREAD) {
			intl_flags |= TPIF::RECALCSETSONCIDSCHANGE | TPIF::INCCIDS_UNREAD;
		}
	}
	if(!tpautos.empty() || !tpudcautos.empty()) {
		RecalculateSets();
	}
}

std::shared_ptr<tpanel> tpanel::MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::shared_ptr<taccount> *acc) {
	std::vector<tpanel_auto> tpautos;
	flagwrapper<TPF> autoflags_ = flags_ & TPF::AUTO_MASK;
	if((acc && *acc) || autoflags_ & (TPF::AUTO_ALLACCS | TPF::AUTO_NOACC)) {
		tpautos.emplace_back();
		tpautos.back().autoflags = autoflags_;
		if(acc) tpautos.back().acc = *acc;
	}
	return std::move(MkTPanel(name_, dispname_, flags_ & TPF::MASK, std::move(tpautos)));
}

std::shared_ptr<tpanel> tpanel::MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_, std::vector<tpanel_auto_udc> tpudcautos_) {
	std::string name = name_;
	std::string dispname = dispname_;

	NameDefaults(name, dispname, tpautos_, tpudcautos_);

	std::shared_ptr<tpanel> &ref = ad.tpanels[name];
	if(!ref) {
		ref = std::make_shared<tpanel>(name, dispname, flags_, std::move(tpautos_), std::move(tpudcautos_));
	}
	return ref;
}

void tpanel::NameDefaults(std::string &name, std::string &dispname, const std::vector<tpanel_auto> &tpautos, const std::vector<tpanel_auto_udc> &tpudcautos) {
	bool newname = name.empty();
	bool newdispname = dispname.empty();

	if(newname) name = "__ATL";


	if(newname || newdispname) {
		std::array<std::vector<std::string>, 8> buildarray;
		std::vector<std::string> extras;

		const flagwrapper<TPF> flagmask = TPF::AUTO_TW | TPF::AUTO_MN | TPF::AUTO_DM;
		const unsigned int flagshift = TPF_AUTO_SHIFT;
		for(auto &it : tpautos) {
			std::string accname;
			std::string accdispname;
			std::string type;

			if(it.autoflags & TPF::AUTO_NOACC) {
				accname = "!";
				if(it.autoflags & TPF::AUTO_HIGHLIGHTED) {
					type += "H";
					if(newdispname) extras.emplace_back("All Highlighted");
				}
				if(it.autoflags & TPF::AUTO_UNREAD) {
					type += "U";
					if(newdispname) extras.emplace_back("All Unread");
				}
			}
			else {
				if(it.acc) {
					accname = it.acc->name.ToUTF8();
					accdispname = it.acc->dispname.ToUTF8();
				}
				else {
					accname = "*";
					accdispname = "All Accounts";
				}

				if(it.autoflags & TPF::AUTO_TW) type += "T";
				if(it.autoflags & TPF::AUTO_DM) type += "D";
				if(it.autoflags & TPF::AUTO_MN) type += "M";

				if(newdispname) buildarray[flag_unwrap<TPF>(it.autoflags & flagmask) >> flagshift].emplace_back(accdispname);
			}

			if(newname) name += "_" + accname + "_" + type;
		}
		std::string dm_names;
		for(auto &it : tpudcautos) {
			if(it.autoflags & TPFU::DMSET) {
				if(newname) name += "_#_" + std::to_string(it.u->id);
				if(newdispname) {
					if(!dm_names.empty()) dm_names += ", ";
					dm_names += "@" + it.u->GetUser().screen_name;
				}
			}
		}
		if(!dm_names.empty()) extras.emplace_back("DM Conversation: " + dm_names);

		if(newdispname) {
			for(auto &it : extras) {
				if(dispname.size() > 1) dispname += ", ";
				dispname += it;
			}
			for(unsigned int i = 0; i < buildarray.size(); i++) {
				if(buildarray[i].empty()) continue;
				flagwrapper<TPF> autoflags = flag_wrap<TPF>(i << flagshift);

				std::string disptype;
				if(autoflags & TPF::AUTO_TW && autoflags & TPF::AUTO_MN && autoflags & TPF::AUTO_DM) disptype = "All";
				else if(autoflags & TPF::AUTO_TW && autoflags & TPF::AUTO_MN) disptype = "Tweets & Mentions";
				else if(autoflags & TPF::AUTO_MN && autoflags & TPF::AUTO_DM) disptype = "Mentions & DMs";
				else if(autoflags & TPF::AUTO_TW && autoflags & TPF::AUTO_DM) disptype = "Tweets & DMs";
				else if(autoflags & TPF::AUTO_TW) disptype = "Tweets";
				else if(autoflags & TPF::AUTO_DM) disptype = "DMs";
				else if(autoflags & TPF::AUTO_MN) disptype = "Mentions";

				for(auto &it : buildarray[i]) {
					if(dispname.size() > 1) dispname += ", ";
					dispname += it;
				}
				dispname += " - " + disptype;
			}
		}
	}
}

std::string tpanel::ManualName(std::string dispname) {
	return "__M_" + dispname;
}

tpanel::~tpanel() {

}

//Do not assume that *acc is non-null
bool tpanel::TweetMatches(tweet_ptr_p t, const std::shared_ptr<taccount> &acc) const {
	for(auto &tpa : tpautos) {
		if((tpa.autoflags & TPF::AUTO_DM && t->flags.Get('D')) || (tpa.autoflags & TPF::AUTO_TW && t->flags.Get('T')) || (tpa.autoflags & TPF::AUTO_MN && t->flags.Get('M'))) {
			if(tpa.autoflags & TPF::AUTO_ALLACCS && t->IsArrivedHereAnyPerspective()) return true;
			else {
				bool found = false;
				t->IterateTP([&](const tweet_perspective &twp) {
					if(found) return;
					if(twp.acc.get() == tpa.acc.get() && twp.IsArrivedHere()) {
						found = true;
					}
				});
				if(found == true) return true;
			}
		}
		if(tpa.autoflags & TPF::AUTO_NOACC) {
			if(tpa.autoflags & TPF::AUTO_HIGHLIGHTED && t->flags.Get('H')) return true;
			if(tpa.autoflags & TPF::AUTO_UNREAD && t->flags.Get('u')) return true;
		}
	}
	for(auto &it : tpudcautos) {
		if(it.autoflags & TPFU::DMSET && t->flags.Get('D')) {
			optional_observer_ptr<user_dm_index> udi = ad.GetExistingUserDMIndexById(it.u->id);
			if(udi) {
				if(udi->ids.find(t->id) != udi->ids.end())
					return true;
			}
		}
	}
	return false;
}

void tpanel::RecalculateTweetSet() {
	LogMsgFormat(LOGT::TPANELINFO, "tpanel::RecalculateTweetSet START: panel %s", cstr(name));

	std::vector<observer_ptr<tweetidset>> id_sets;
	for(auto &tpa : tpautos) {
		auto doacc = [&](taccount *it) {
			if(tpa.autoflags & TPF::AUTO_DM) id_sets.push_back(&(it->dm_ids));
			if(tpa.autoflags & TPF::AUTO_TW) id_sets.push_back(&(it->tweet_ids));
			if(tpa.autoflags & TPF::AUTO_MN) id_sets.push_back(&(it->usercont->mention_set));
		};

		if(tpa.autoflags & TPF::AUTO_ALLACCS) {
			for(auto &it : alist) doacc(it.get());
		}
		else if(tpa.autoflags & TPF::AUTO_NOACC) {
			if(tpa.autoflags & TPF::AUTO_HIGHLIGHTED) id_sets.push_back(&(ad.cids.highlightids));
			if(tpa.autoflags & TPF::AUTO_UNREAD) id_sets.push_back(&(ad.cids.unreadids));
		}
		else doacc(tpa.acc.get());
	}
	for(auto &it : tpudcautos) {
		if(it.autoflags & TPFU::DMSET) {
			optional_observer_ptr<user_dm_index> udi = ad.GetExistingUserDMIndexById(it.u->id);
			if(udi) {
				id_sets.push_back(&(udi->ids));
			}
		}
	}

	// Sort lowest to highest in size
	std::sort(id_sets.begin(), id_sets.end(), [](observer_ptr<tweetidset> a, observer_ptr<tweetidset> b) {
		return a->size() < b->size();
	});
	if(id_sets.size() == 1) {
		// Only one set, just copy it
		tweetlist = *(id_sets[0]);
	}
	else if(id_sets.size() >= 2) {
		observer_ptr<tweetidset> a = id_sets.back();
		id_sets.pop_back();
		observer_ptr<tweetidset> b = id_sets.back();
		id_sets.pop_back();

		// Union the two largest sets
		// This avoids N log N insertion and lots of re-writes/rebalancing if the second set is also large
		std::set_union(a->begin(), a->end(), b->begin(), b->end(), std::inserter(tweetlist, tweetlist.end()), tweetlist.key_comp());

		// Individually insert any remainders
		for(auto &it : id_sets) {
			tweetlist.insert(it->begin(), it->end());
		}
	}

	LogMsgFormat(LOGT::TPANELINFO, "tpanel::RecalculateTweetSet END: %zu ids", tweetlist.size());
}

//! This handles all CIDS changes
//! Bulk CIDS operations do not use this however
void tpanel::NotifyCIDSChange(uint64_t id, tweetidset cached_id_sets::*ptr, bool add, flagwrapper<PUSHFLAGS> pushflags) {
	if(add) {
		if(tweetlist.count(id)) {
			auto result = (cids.*ptr).insert(id);
			if(result.second) UpdateCLabelLater_TP();
		}
	}
	else {
		size_t erasecount = (cids.*ptr).erase(id);
		if(erasecount) UpdateCLabelLater_TP();
	}

	NotifyCIDSChange_AddRemove(id, ptr, add, pushflags);
}

bool tpanel::NotifyCIDSChange_AddRemove_IsApplicable(tweetidset cached_id_sets::* ptr) const {
	return (intl_flags & TPIF::INCCIDS_HIGHLIGHT && ptr == &cached_id_sets::highlightids)
			|| (intl_flags & TPIF::INCCIDS_UNREAD && ptr == &cached_id_sets::unreadids);
}

//! This only handles tpanels which includes CIDS sets as auto sources
//! This is used by bulk CIDS operations
void tpanel::NotifyCIDSChange_AddRemoveIntl(uint64_t id, tweetidset cached_id_sets::* ptr, bool add, flagwrapper<PUSHFLAGS> pushflags) {
	if(NotifyCIDSChange_AddRemove_IsApplicable(ptr)) {
		if(add) {
			PushTweet(id, nullptr, pushflags);
		}
		else if(tweetlist.count(id)) {
			//we have this tweet, and may be removing it

			size_t havetweet = 0;
			for(auto &tpa : tpautos) {
				auto doacc = [&](taccount *it) {
					if(tpa.autoflags & TPF::AUTO_DM) havetweet += it->dm_ids.count(id);
					if(tpa.autoflags & TPF::AUTO_TW) havetweet += it->tweet_ids.count(id);
					if(tpa.autoflags & TPF::AUTO_MN) {
						const tweetidset &mentions = it->usercont->GetMentionSet();
						havetweet += mentions.count(id);
					}
				};

				if(tpa.autoflags & TPF::AUTO_ALLACCS) {
					for(auto &it : alist) doacc(it.get());
				}
				else if(tpa.autoflags & TPF::AUTO_NOACC) {
					if(tpa.autoflags & TPF::AUTO_HIGHLIGHTED && ptr != &cached_id_sets::highlightids) havetweet += ad.cids.highlightids.count(id);
					if(tpa.autoflags & TPF::AUTO_UNREAD  && ptr != &cached_id_sets::unreadids) havetweet += ad.cids.unreadids.count(id);
				}
				else doacc(tpa.acc.get());
				if(havetweet) break;
			}

			if(!havetweet) {
				//we are removing the tweet
				RemoveTweet(id, pushflags);
			}
		}
	}
}

//! This only handles tpanels which includes CIDS sets as auto sources
//! This is used by bulk CIDS operations
void tpanel::NotifyCIDSChange_AddRemove_Bulk(const tweetidset &ids, tweetidset cached_id_sets::* ptr, bool add) {
	if(!NotifyCIDSChange_AddRemove_IsApplicable(ptr))
		return;

	if(!(intl_flags & tpanel::TPIF::RECALCSETSONCIDSCHANGE))
		return;

	for(auto &tweet_id : ids) {
		NotifyCIDSChange_AddRemoveIntl(tweet_id, ptr, add, PUSHFLAGS::SETNOUPDATEFLAG);
	}
}

void tpanel::RecalculateCIDS() {
	LogMsgFormat(LOGT::TPANELINFO, "tpanel::RecalculateCIDS START: panel %s", cstr(name));
	ad.cids.foreach(this->cids, [&](tweetidset &adtis, tweetidset &thistis) {
		std::set_intersection(tweetlist.begin(), tweetlist.end(), adtis.begin(), adtis.end(), std::inserter(thistis, thistis.end()), tweetlist.key_comp());
	});
	LogMsgFormat(LOGT::TPANELINFO, "tpanel::RecalculateCIDS END: %zu ids, %s", tweetlist.size(), cstr(this->cids.DumpInfo()));
}

void tpanel::MarkSetRead(optional_observer_ptr<undo::item> undo_item) {
	MarkSetReadOrUnread(std::move(cids.unreadids), undo_item, true);
	cids.unreadids.clear(); //do not rely on presence of move semantics
}

//! This does not clear the subset
//! Note that the tpanel cids should be cleared/modified *before* calling this function
void tpanel::MarkSetReadOrUnread(tweetidset &&subset, optional_observer_ptr<undo::item> undo_item, bool mark_read) {
	tweet_flags add_flags, remove_flags;
	if(mark_read) {
		add_flags.Set('r');
		remove_flags.Set('u');
	}
	else {
		add_flags.Set('u');
		remove_flags.Set('r');
	}

	MarkCIDSSetGenericUndoable(&cached_id_sets::unreadids, this, std::move(subset), undo_item, mark_read, add_flags, remove_flags);
}

void tpanel::MarkSetUnhighlighted(optional_observer_ptr<undo::item> undo_item) {
	MarkSetHighlightState(std::move(cids.highlightids), undo_item, true);
	cids.highlightids.clear(); //do not rely on presence of move semantics
}

//! This does not clear the subset
//! Note that the tpanel cids should be cleared/modified *before* calling this function
void tpanel::MarkSetHighlightState(tweetidset &&subset, optional_observer_ptr<undo::item> undo_item, bool unhighlight) {
	tweet_flags add_flags, remove_flags;
	if(unhighlight) {
		remove_flags.Set('H');
	}
	else {
		add_flags.Set('H');
	}

	MarkCIDSSetGenericUndoable(&cached_id_sets::highlightids, this, std::move(subset), undo_item, unhighlight, add_flags, remove_flags);
}

observer_ptr<undo::item> tpanel::MakeUndoItem(const std::string &prefix) {
	return wxGetApp().undo_state.NewItem(string_format("%s: %s", cstr(prefix), cstr(dispname)));
}

void tpanel::MarkCIDSSetGenericUndoable(tweetidset cached_id_sets::* idsetptr, tpanel *exclude, tweetidset &&subset, optional_observer_ptr<undo::item> undo_item,
		bool remove, tweet_flags add_flags, tweet_flags remove_flags) {

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanel::MarkCIDSSetGenericUndoable START");
	#endif

	tweet_flags all_flags = add_flags;
	all_flags |= remove_flags;

	tweetidset cached_ids = std::move(subset);
	MarkCIDSSetHandler(
		idsetptr,
		exclude,
		[&](tweet_ptr_p tw) {
			tw->flags |= add_flags;
			tw->flags &= ~remove_flags;
			tw->IgnoreChangeToFlagsByMask(all_flags.ToULLong());
			UpdateTweet(*tw, false);
		},
		cached_ids,
		remove
	);

	if(undo_item) {
		undo_item->AppendAction(std::unique_ptr<undo::generic_action>(new undo::generic_action(
			[idsetptr, cached_ids, remove, remove_flags, add_flags]() mutable {
				// Note: reversal of remove and add flags, and inversion of remove flag
				// Note: exclude set to nullptr
				tpanel::MarkCIDSSetGenericUndoable(idsetptr, nullptr, std::move(cached_ids), nullptr, !remove, remove_flags, add_flags);
			}
		)));
	}

	std::unique_ptr<dbupdatetweetsetflagsmsg_group> msg(new dbupdatetweetsetflagsmsg_group(std::move(cached_ids), add_flags.ToULLong(), remove_flags.ToULLong()));
	DBC_SendMessage(std::move(msg));

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanel::MarkCIDSSetGenericUndoable END");
	#endif
}

//! Iff exclude is set to this:
//! * This does not clear or add the subset
//! * Note that the tpanel cids should be cleared/modified/added *before* calling this function
//! Iff exclude is set to nullptr
//! * This does clear/add the subset
void tpanel::MarkCIDSSetHandler(tweetidset cached_id_sets::* idsetptr, tpanel *exclude, std::function<void(tweet_ptr_p)> existingtweetfunc, const tweetidset &subset, bool remove) {
	if(exclude) {
		exclude->SetNoUpdateFlag_TP();
		exclude->SetClabelUpdatePendingFlag_TP();
	}

	MarkTweetIDSetCIDS(subset, exclude, idsetptr, remove, existingtweetfunc);
	CheckClearNoUpdateFlag_All();
}

void tpanel::RecalculateSets() {
	RecalculateTweetSet();
	RecalculateCIDS();
}

void tpanel::OnTPanelWinClose(tpanelparentwin_nt *tppw) {
	twin.remove(tppw);
	if(twin.empty() && flags&TPF::DELETEONWINCLOSE) {
		ad.tpanels.erase(name);
	}
}

tpanelparentwin *tpanel::MkTPanelWin(mainframe *parent, bool select) {
	return new tpanelparentwin(shared_from_this(), parent, select);
}

bool tpanel::IsSingleAccountTPanel() const {
	if(alist.size() <= 1) return true;
	if(tpautos.size() > 1) return false;
	else if(tpautos.size() == 1) {
		if(tpautos[0].autoflags & (TPF::AUTO_ALLACCS | TPF::AUTO_NOACC)) return false;
		else return true;
	}
	if(flags & TPF::USER_TIMELINE) return true;
	return false;
}

void tpanel::SetNoUpdateFlag_TP() const {
	for(auto &jt : twin) {
		jt->SetNoUpdateFlag();
	}
}

void tpanel::CheckClearNoUpdateFlag_TP() const {
	for(auto &jt : twin) {
		jt->CheckClearNoUpdateFlag();
	}
}

void tpanel::SetClabelUpdatePendingFlag_TP() const {
	for(auto &jt : twin) {
		jt->SetClabelUpdatePendingFlag();
	}
}

void tpanel::UpdateCLabelLater_TP() const {
	for(auto &jt : twin) {
		jt->UpdateCLabelLater();
	}
}
