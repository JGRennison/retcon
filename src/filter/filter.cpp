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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "../univdefs.h"
#include "../log.h"
#include "../util.h"
#include "../twit.h"
#include "../alldata.h"
#include "../cfg.h"
#include "filter.h"
#include "filter-intl.h"
#include "../taccount.h"
#include "../flags.h"
#include "../tpanel-data.h"
#include "../map.h"
#include "../db-lazy.h"
#include "../db-intl.h"
#define PCRE_STATIC
#include <pcre.h>
#include <list>
#include <functional>
#include <type_traits>

//This is such that PCRE_STUDY_JIT_COMPILE can be used pre PCRE 8.20
#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

enum class FRSF {
	DONEIF          = 1<<0,
	ACTIVE          = 1<<1,
	PARENTINACTIVE  = 1<<2,
};
template<> struct enum_traits<FRSF> { static constexpr bool flags = true; };

struct filter_run_state {
	std::vector<flagwrapper<FRSF>> recursion;
	taccount *tac = nullptr;
	const std::string empty_str;
	std::string test_temp;
	observer_ptr<filter_undo_action> filter_undo;
};

enum class FIF {
	COND      = 1<<0,
	ELIF      = 1<<1,
	NEG       = 1<<2,
	ENDIF     = 1<<3,
	ELSE      = 1<<4,
	ORIF      = 1<<5,
};
template<> struct enum_traits<FIF> { static constexpr bool flags = true; };

struct filter_item {
	flagwrapper<FIF> flags = 0;
	virtual void exec(tweet &tw, filter_run_state &frs) = 0;
	virtual void exec(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) = 0;
	virtual ~filter_item() { }
	virtual bool test_recursion(filter_run_state &frs, std::string &err) { return true; }
};

struct filter_item_cond : public filter_item {
	virtual bool test(tweet &tw, filter_run_state &frs) {
		return true;
	}

	virtual bool test(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) {
		return true;
	}

	// return true if OK to continue
	bool pre_exec(filter_run_state &frs) {
		if (flags & FIF::ENDIF) {
			frs.recursion.pop_back();
			return false;
		} else if (flags & FIF::ELIF) {
			if (frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
				return false;
			}
		} else if (flags & FIF::ORIF) {
			if (frs.recursion.back() & FRSF::ACTIVE) {
				//leave the active bit set
				return false;
			}
			if (frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
				return false;
			}
		} else if (flags & FIF::ELSE) {
			if (frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
			} else {
				frs.recursion.back() |= FRSF::DONEIF | FRSF::ACTIVE;
			}
			return false;
		} else {
			if (!frs.recursion.empty() && !(frs.recursion.back() & FRSF::ACTIVE)) {
				//this is a nested if, the parent if is not active
				frs.recursion.push_back(FRSF::PARENTINACTIVE);
				return false;
			}
			frs.recursion.push_back(0);
		}
		return true;
	}

	void post_exec(bool testresult, filter_run_state &frs) {
		if (flags & FIF::NEG) {
			testresult = !testresult;
		}
		if (testresult) {
			frs.recursion.back() |= FRSF::DONEIF | FRSF::ACTIVE;
		} else {
			frs.recursion.back() &= ~FRSF::ACTIVE;
		}
	}

	void exec(tweet &tw, filter_run_state &frs) override {
		if (pre_exec(frs)) {
			post_exec(test(tw, frs), frs);
		}
	}

	void exec(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		if (pre_exec(frs)) {
			post_exec(test(state, tweet_id, frs), frs);
		}
	}

	bool test_recursion(filter_run_state &frs, std::string &err) override {
		if (flags & FIF::ENDIF) {
			if (frs.recursion.empty()) {
				err = "endif/fi without opening if";
				return false;
			}
			frs.recursion.pop_back();
		} else if (flags & FIF::ELIF || flags & FIF::ELSE || flags & FIF::ORIF) {
			if (frs.recursion.empty()) {
				err = "elif/orif/else without corresponding if";
				return false;
			}
		} else {
			frs.recursion.push_back(0);
		}
		return true;
	}
};

struct filter_item_cond_regex : public filter_item_cond {
	pcre *ptn = nullptr;
	pcre_extra *extra = nullptr;
	std::string regexstr;

	enum class PROP {
		TWEET_TEXT,
		TWEET_SOURCE,

		USER_NAME,
		USER_SCREENNAME,
		USER_DESCRIPTION,
		USER_LOCATION,
		USER_NOTES,
		USER_IDSTR,
	};
	PROP property;

	enum class TYPE_FLAGS {
		IS_USER_TEST                 = 1<<0, // if not set, is a tweet test

		// flags for tweet tests
		TRY_RETWEET                  = 1<<1,

		// flags for user test
		TRY_RETWEET_USER             = 1<<2,
		TRY_RECIP_USER               = 1<<3,
		TRY_RETWEET_USER_IF_TRUE     = 1<<4,
		TRY_RECIP_USER_IF_TRUE       = 1<<5,
		TRY_ACC_USER                 = 1<<6,
	};
	flagwrapper<TYPE_FLAGS> type_flags;

	struct user_cache_entry {
		unsigned int revision;
		bool result;
	};
	container::map<uint64_t, user_cache_entry> usertestcache;

	bool regex_test(const std::string &str) {
		const int ovecsize = 30;
		int ovector[30];
		bool result = (pcre_exec(ptn, extra,  str.c_str(), str.size(), 0, 0, ovector, ovecsize) >= 1);
		return result;
	}

	template <typename T> const std::string &get_tweet_prop(T tweet_access, filter_run_state &frs) {
		switch (property) {
			case PROP::TWEET_TEXT:
				return tweet_access->GetText();

			case PROP::TWEET_SOURCE:
				return tweet_access->GetSource();

			default:
				return frs.empty_str;
		}
	}

	template <typename T> const std::string &get_user_prop(T user_access, filter_run_state &frs) {
		switch (property) {
			case PROP::USER_NAME:
				return user_access->GetName();

			case PROP::USER_SCREENNAME:
				return user_access->GetScreenName();

			case PROP::USER_DESCRIPTION:
				return user_access->GetDescription();

			case PROP::USER_LOCATION:
				return user_access->GetLocation();

			case PROP::USER_NOTES:
				return user_access->GetNotes();

			case PROP::USER_IDSTR:
				frs.test_temp = string_format("%" llFmtSpec "u", user_access->GetCurrentUserID());
				return frs.test_temp;

			default:
				return frs.empty_str;
		}
	}

	template <typename T> bool user_test(T user_access, filter_run_state &frs) {
		if (!user_access->IsValid())
			return regex_test("");

		auto iter = usertestcache.insert(std::make_pair(user_access->GetCurrentUserID(), user_cache_entry()));
		bool new_insertion = iter.second;
		user_cache_entry &uce = iter.first->second;
		if (!new_insertion && uce.revision == user_access->GetRevisionNumber()) {
			//cached result
			return uce.result;
		}

		uce.revision = user_access->GetRevisionNumber();
		uce.result = regex_test(get_user_prop(user_access, frs));
		return uce.result;
	}

	template <typename T> bool test_generic(T tweet_generic, filter_run_state &frs) {
		if (type_flags & TYPE_FLAGS::IS_USER_TEST) {
			if (type_flags & TYPE_FLAGS::TRY_RETWEET_USER) {
				if (tweet_generic.HasRT()) {
					return user_test(tweet_generic.GetRTUser(), frs);
				}
			}
			if (type_flags & TYPE_FLAGS::TRY_RECIP_USER) {
				if (tweet_generic.HasRecipUser()) {
					return user_test(tweet_generic.GetRecipUser(), frs);
				}
			}
			if (type_flags & TYPE_FLAGS::TRY_RETWEET_USER_IF_TRUE) {
				if (tweet_generic.HasRT() && user_test(tweet_generic.GetRTUser(), frs)) {
					return true;
				}
			}
			if (type_flags & TYPE_FLAGS::TRY_RECIP_USER_IF_TRUE) {
				if (tweet_generic.HasRecipUser() && user_test(tweet_generic.GetRecipUser(), frs)) {
					return true;
				}
			}
			if (type_flags & TYPE_FLAGS::TRY_ACC_USER && std::is_same<T, generic_tweet_access_loaded>::value && frs.tac) {
				// This does not make sense in a lazy DB context, so test for template type
				return user_test(db_lazy_user_compat_accessor(frs.tac->usercont.get()), frs);
			}
			return user_test(tweet_generic.GetUser(), frs);
		} else {
			if (type_flags & TYPE_FLAGS::TRY_RETWEET) {
				if (tweet_generic.HasRT()) {
					return regex_test(get_tweet_prop(tweet_generic.GetRetweet(), frs));
				}
			}
			return regex_test(get_tweet_prop(tweet_generic.GetTweet(), frs));
		}
	}

	bool test(tweet &tw, filter_run_state &frs) override {
		return test_generic(generic_tweet_access_loaded(tw), frs);
	}

	bool test(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		return test_generic(generic_tweet_access_dblazy(state, tweet_id), frs);
	}

	virtual ~filter_item_cond_regex() {
		if (ptn) {
			pcre_free(ptn);
		}
		if (extra) {
			pcre_free_study(extra);
		}
	}

	void parse_setup(const std::string &part1, const std::string &part2, bool &ok, std::string &errmsgs);
};
template<> struct enum_traits<filter_item_cond_regex::TYPE_FLAGS> { static constexpr bool flags = true; };

void filter_item_cond_regex::parse_setup(const std::string &part1, const std::string &part2, bool &ok, std::string &errmsgs) {
	auto tweetmode = [&]() {
		if (part2 == "text") {
			property = PROP::TWEET_TEXT;
		} else if (part2 == "source") {
			property = PROP::TWEET_SOURCE;
		} else {
			errmsgs += string_format("No such tweet field: %s\n", part2.c_str());
			ok = false;
		}
	};

	auto usermode = [&]() {
		if (part2 == "name") {
			property = PROP::USER_NAME;
		} else if (part2 == "screenname" || part2 == "sname") {
			property = PROP::USER_SCREENNAME;
		} else if (part2 == "description" || part2 == "desc") {
			property = PROP::USER_DESCRIPTION;
		} else if (part2 == "loc" || part2 == "location") {
			property = PROP::USER_LOCATION;
		} else if (part2 == "id") {
			property = PROP::USER_IDSTR;
		} else if (part2 == "notes") {
			property = PROP::USER_NOTES;
		} else {
			errmsgs += string_format("No such user field: %s\n", part2.c_str());
			ok = false;
		}
	};

	if (part1 == "retweet") {
		tweetmode();
		type_flags = TYPE_FLAGS::TRY_RETWEET;
	} else if (part1 == "tweet") {
		tweetmode();
		type_flags = 0;
	} else if (part1 == "user") {
		usermode();
		type_flags = TYPE_FLAGS::IS_USER_TEST;
	} else if (part1 == "retweetuser") {
		usermode();
		type_flags = TYPE_FLAGS::IS_USER_TEST | TYPE_FLAGS::TRY_RETWEET_USER;
	} else if (part1 == "userrecipient") {
		usermode();
		type_flags = TYPE_FLAGS::IS_USER_TEST | TYPE_FLAGS::TRY_RECIP_USER;
	} else if (part1 == "anyuser") {
		usermode();
		type_flags = TYPE_FLAGS::IS_USER_TEST | TYPE_FLAGS::TRY_RECIP_USER_IF_TRUE | TYPE_FLAGS::TRY_RETWEET_USER_IF_TRUE;
	} else if (part1 == "accountuser") {
		usermode();
		type_flags = TYPE_FLAGS::IS_USER_TEST | TYPE_FLAGS::TRY_ACC_USER;
	} else {
		errmsgs += string_format("No such field type: %s\n", part1.c_str());
		ok = false;
	}
}

struct filter_item_cond_flags : public filter_item_cond {
	uint64_t any = 0;
	uint64_t all = 0;
	uint64_t none = 0;
	uint64_t missing = 0;
	bool retweet;
	std::string teststr;

	bool test_common(uint64_t curflags) {
		bool result = true;
		if (any && !(curflags & any)) result = false;
		if (all && (curflags & all) != all) result = false;
		if (none && (curflags & none)) result = false;
		if (missing && (curflags | missing) == curflags) result = false;
		return result;
	}

	template <typename T> bool test_generic(T tweet_generic, filter_run_state &frs) {
		if (retweet && tweet_generic.HasRT()) {
			return test_common(tweet_generic.GetRetweet()->GetFlags().ToULLong());
		} else {
			return test_common(tweet_generic.GetTweet()->GetFlags().ToULLong());
		}
	}

	bool test(tweet &tw, filter_run_state &frs) override {
		return test_generic(generic_tweet_access_loaded(tw), frs);
	}

	bool test(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		return test_generic(generic_tweet_access_dblazy(state, tweet_id), frs);
	}
};

struct filter_item_action : public filter_item {
	virtual void action(tweet &tw, filter_run_state &frs) = 0;
	virtual void action(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) = 0;

	bool exec_common(filter_run_state &frs) {
		return frs.recursion.empty() || frs.recursion.back() & FRSF::ACTIVE;
	}

	void exec(tweet &tw, filter_run_state &frs) override {
		if (exec_common(frs)) {
			action(tw, frs);
		}
	}

	void exec(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		if (exec_common(frs)) {
			action(state, tweet_id, frs);
		}
	}
};

const char setflags_allowed[] = "hnpruH";
struct filter_item_action_setflag : public filter_item_action {
	uint64_t setflags = 0;
	uint64_t unsetflags = 0;
	std::string setstr;

	void RegisterBulkAction(filter_bulk_action &bulk_action, uint64_t tweet_id, uint64_t oldflags, uint64_t newflags) {
		if (oldflags != newflags) {
			auto it = bulk_action.flag_actions.insert(std::make_pair(tweet_id, filter_bulk_action::flag_action()));
			if (it.second) {
				// new flag_action
				it.first->second.new_flags = newflags;
			}
			it.first->second.old_flags = oldflags;
		}
	}

	void action(tweet &tw, filter_run_state &frs) override {
		uint64_t oldflags = tw.flags.ToULLong();
		uint64_t newflags = (oldflags | setflags) & ~unsetflags;
		tw.flags = tweet_flags(newflags);
		LogMsgFormat(LOGT::FILTERTRACE, "Setting Tweet Flags for Tweet: %" llFmtSpec "d, Flags: Before %s, Action: %s, Result: %s",
				tw.id, cstr(tweet_flags::GetValueString(oldflags)), cstr(setstr), cstr(tweet_flags::GetValueString(newflags)));

		if (frs.filter_undo) {
			RegisterBulkAction(frs.filter_undo->bulk_action, tw.id, newflags, oldflags); // Note flag order reversed as this is an undo action
		}
	}

	void action(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		state.dl_tweet.LoadTweetID(tweet_id);
		uint64_t oldflags = state.dl_tweet.GetFlags().ToULLong();
		uint64_t newflags = (oldflags | setflags) & ~unsetflags;
		state.dl_tweet.SetFlags(newflags);

		RegisterBulkAction(state.bulk_action, tweet_id, oldflags, newflags);
	}
};

static void PanelRemoveOneTweet(const std::shared_ptr<tpanel> &tp, const std::string &panel_name, uint64_t tweet_id, optional_observer_ptr<filter_bulk_action> undo_action) {
	bool actually_done = false;
	if (tp) {
		actually_done = tp->RemoveTweet(tweet_id);
	}
	if (undo_action && actually_done) {
		undo_action->panel_to_add[panel_name].insert(tweet_id);
	}
}

struct filter_item_action_panel : public filter_item_action {
	bool remove;
	std::string panel_name;

	void action(tweet &tw, filter_run_state &frs) override {
		if (remove) {
			PanelRemoveOneTweet(ad.tpanels[tpanel::ManualName(panel_name)], panel_name, tw.id,
				frs.filter_undo ? &(frs.filter_undo->bulk_action) : nullptr);
		} else {
			std::shared_ptr<tpanel> tp = tpanel::MkTPanel(tpanel::ManualName(panel_name), panel_name, TPF::MANUAL | TPF::SAVETODB);
			bool actually_done = tp->PushTweet(ad.GetTweetById(tw.id));
			if (actually_done && frs.filter_undo) {
				frs.filter_undo->bulk_action.panel_to_remove[panel_name].insert(tw.id);
			}
		}
	}

	void action(filter_db_lazy_state &state, uint64_t tweet_id, filter_run_state &frs) override {
		if (remove) {
			state.bulk_action.panel_to_remove[panel_name].insert(tweet_id);
		} else {
			state.bulk_action.panel_to_add[panel_name].insert(tweet_id);
		}
	}
};

void filter_bulk_action::execute(optional_observer_ptr<filter_bulk_action> undo_action) {
	LogMsgFormat(LOGT::FILTERTRACE, "filter_bulk_action::execute: %zu flag actions, %zu panel adds, %zu panel removes, undo: %d",
			flag_actions.size(), panel_to_add.size(), panel_to_remove.size(), undo_action ? 1 : 0);

	for (auto &it : flag_actions) {
		uint64_t mask = it.second.old_flags ^ it.second.new_flags;

		tweet::ChangeFlagsById(it.first, it.second.new_flags & mask, (~it.second.new_flags) & mask,
				tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
	}

	// Send tweet flags first and flush as panel adds below may well load tweets updated above
	dbc.FlushBatchQueue();

	for (auto &it : panel_to_add) {
		std::shared_ptr<tpanel> tp = tpanel::MkTPanel(tpanel::ManualName(it.first), it.first, TPF::MANUAL | TPF::SAVETODB);
		tp->BulkPushTweet(std::move(it.second), PUSHFLAGS::DEFAULT, undo_action ? &(undo_action->panel_to_remove[it.first]) : nullptr);
	}

	for (auto &it : panel_to_remove) {
		std::shared_ptr<tpanel> tp = ad.tpanels[tpanel::ManualName(it.first)];

		if (tp) {
			for (auto &jt : it.second) {
				PanelRemoveOneTweet(tp, it.first, jt, undo_action);
			}
		}
	}

	if (undo_action) {
		undo_action->flag_actions.clear();
		for (auto &it : flag_actions) {
			undo_action->flag_actions.insert(undo_action->flag_actions.end(),
					std::make_pair(it.first, flag_action { it.second.new_flags, it.second.old_flags })); // Note that flags are swapped
		}
	}
}

const char condsyntax[] = R"(^\s*(?:(?:(el)(?:s(?:e\s*)?)?)?|(or\s*))if(n)?\s+)";
const char regexsyntax[] = R"(^(\w+)\.(\w+)\s+(.*\S)\s*$)";
const char flagtestsyntax[] = R"(^(re)?tweet\.flags?\s+([a-zA-Z0-9+=\-/]+)\s*)";

const char elsesyntax[] = R"(^\s*else\s*$)";
const char endifsyntax[] = R"(^\s*(?:end\s*if|fi)\s*$)";

const char flagsetsyntax[] = R"(^\s*set\s+tweet\.flags\s+([a-zA-Z0-9+\-]+)\s*$)";
const char panelsyntax[] = R"(^\s*panel\s+(add|remove)\s+(\S.*\S)\s*$)";

const char blanklinesyntax[] = R"(^(?:\s*#.*)?\s*$)"; //this also filters comments


void ParseFilter(std::string input, filter_set &filter_output, std::string &errmsgs) {
	static pcre *cond_pattern = nullptr;
	static pcre_extra *cond_patextra = nullptr;
	static pcre *regex_pattern = nullptr;
	static pcre_extra *regex_patextra = nullptr;
	static pcre *flagtest_pattern = nullptr;
	static pcre_extra *flagtest_patextra = nullptr;

	static pcre *else_pattern = nullptr;
	static pcre_extra *else_patextra = nullptr;
	static pcre *endif_pattern = nullptr;
	static pcre_extra *endif_patextra = nullptr;

	static pcre *flagset_pattern = nullptr;
	static pcre_extra *flagset_patextra = nullptr;
	static pcre *panel_pattern = nullptr;
	static pcre_extra *panel_patextra = nullptr;

	static pcre *blankline_pattern = nullptr;
	static pcre_extra *blankline_patextra = nullptr;

	filter_output.filters.clear();
	errmsgs.clear();

	bool ok = true;
	unsigned int jit_count = 0;

	auto compile = [&](const char *str, pcre **ptn, pcre_extra **extra) {
		if (!ptn) {
			ok = false;
			return;
		}
		if (*ptn) {
			return;
		}

		const char *errptr;
		int erroffset;

		*ptn = pcre_compile(str, PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, nullptr);
		if (!*ptn) {
			LogMsgFormat(LOGT::FILTERERR, "pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(str));
			ok = false;
			return;
		}
		if (extra && !*extra) {
			*extra = pcre_study(*ptn, PCRE_STUDY_JIT_COMPILE, &errptr);
#if PCRE_STUDY_JIT_COMPILE
			int value = 0;
			pcre_fullinfo(*ptn, *extra, PCRE_INFO_JIT, &value);
			if (value) jit_count++;
#endif
		}
	};

	compile(condsyntax, &cond_pattern, &cond_patextra);
	compile(regexsyntax, &regex_pattern, &regex_patextra);
	compile(flagtestsyntax, &flagtest_pattern, &flagtest_patextra);
	compile(elsesyntax, &else_pattern, &else_patextra);
	compile(endifsyntax, &endif_pattern, &endif_patextra);
	compile(flagsetsyntax, &flagset_pattern, &flagset_patextra);
	compile(panelsyntax, &panel_pattern, &panel_patextra);
	compile(blanklinesyntax, &blankline_pattern, &blankline_patextra);

	if (!ok) return;

	LogMsgFormat(LOGT::FILTERTRACE, "ParseFilter: %u JITed", jit_count);

	const int ovecsize = 60;
	int ovector[60];

	size_t linestart = 0;

	while (linestart < input.size()) {
		size_t linelen = 0;
		size_t lineeol = 0;
		while (linestart + linelen < input.size()) {
			char c = input[linestart + linelen];
			if (c == '\r' || c == '\n') {
				lineeol++;
				break;
			} else {
				linelen++;
			}
		}

		const char *pos = input.c_str() + linestart;

		linestart += linelen + lineeol;

		if (pcre_exec(blankline_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			continue;  //eat blank lines
		}

		if (pcre_exec(cond_pattern, cond_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			// this is a conditional

			int nextsectionoffset = ovector[1];

			bool iselif = ovector[2] >= 0;
			bool isorif = ovector[4] >= 0;
			bool isnegative = ovector[6] >= 0;

			flagwrapper<FIF> flags = FIF::COND;
			if (iselif) flags |= FIF::ELIF;
			if (isnegative) flags |= FIF::NEG;
			if (isorif) flags |= FIF::ORIF;

			if (pcre_exec(flagtest_pattern, flagtest_patextra, pos + nextsectionoffset, linelen - nextsectionoffset, 0, 0, ovector, ovecsize) >= 1) {
				std::unique_ptr<filter_item_cond_flags> fitem(new filter_item_cond_flags);
				fitem->teststr = std::string(pos + nextsectionoffset + ovector[4], ovector[5] - ovector[4]);

				uint64_t *current = &(fitem->any);
				for (auto c : fitem->teststr) {
					switch (c) {
						case '+': current = &(fitem->any); break;
						case '=': current = &(fitem->all); break;
						case '-': current = &(fitem->none); break;
						case '/': current = &(fitem->missing); break;
						default: *current |= tweet_flags::GetFlagValue(c); break;
					}
				}

				fitem->retweet = ovector[2] >= 0;
				fitem->flags = flags;
				filter_output.filters.emplace_back(std::move(fitem));
			} else if (pcre_exec(regex_pattern, regex_patextra,  pos + nextsectionoffset, linelen - nextsectionoffset, 0, 0, ovector, ovecsize) >= 1) {
				std::string part1(pos + nextsectionoffset + ovector[2], ovector[3] - ovector[2]);
				std::string part2(pos + nextsectionoffset + ovector[4], ovector[5] - ovector[4]);

				std::unique_ptr<filter_item_cond_regex> ritem(new filter_item_cond_regex);
				ritem->flags = flags;

				const char *errptr;
				int erroffset;
				std::string userptnstr(pos + nextsectionoffset + ovector[6], ovector[7] - ovector[6]);
				ritem->ptn = pcre_compile(userptnstr.c_str(), PCRE_NO_UTF8_CHECK | PCRE_UTF8, &errptr, &erroffset, 0);
				if (!ritem->ptn) {
					LogMsgFormat(LOGT::FILTERERR, "pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(userptnstr));
					ok = false;
				} else {
					ritem->extra = pcre_study(ritem->ptn, PCRE_STUDY_JIT_COMPILE, &errptr);
					if (currentlogflags & LOGT::FILTERTRACE) {
						int jit = 0;
#if PCRE_STUDY_JIT_COMPILE
						pcre_fullinfo(ritem->ptn, ritem->extra, PCRE_INFO_JIT, &jit);
#endif
						LogMsgFormat(LOGT::FILTERTRACE, "ParseFilter: pcre_compile and pcre_study success: JIT: %u\n%s", jit, cstr(userptnstr));
					}
				}
				ritem->regexstr = std::move(userptnstr);

				ritem->parse_setup(part1, part2, ok, errmsgs);

				if (ok) {
					filter_output.filters.emplace_back(std::move(ritem));
				}
			} else {
				//conditional doesn't match
				errmsgs += "Cannot parse condition: '" + std::string(pos + nextsectionoffset, linelen - nextsectionoffset) + "'\n";
				ok = false;
			}
		} else if (pcre_exec(else_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_cond> citem(new filter_item_cond);
			citem->flags = FIF::COND | FIF::ELSE;
			filter_output.filters.emplace_back(std::move(citem));
		} else if (pcre_exec(endif_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_cond> citem(new filter_item_cond);
			citem->flags = FIF::COND | FIF::ENDIF;
			filter_output.filters.emplace_back(std::move(citem));
		} else if (pcre_exec(flagset_pattern, flagset_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			static uint64_t allowed_flags = tweet_flags::GetFlagStringValue(setflags_allowed);

			std::unique_ptr<filter_item_action_setflag> fitem(new filter_item_action_setflag);
			fitem->setstr = std::string(pos + ovector[2], ovector[3] - ovector[2]);

			uint64_t *current = &(fitem->setflags);
			for (auto c : fitem->setstr) {
				switch (c) {
					case '+': current = &(fitem->setflags); break;
					case '-': current = &(fitem->unsetflags); break;

					default: {
						uint64_t flag = tweet_flags::GetFlagValue(c);
						if (flag & allowed_flags) {
							*current |= flag;
						} else {
							errmsgs += string_format("Setting tweet flag '%c' is not allowed. Allowed flags: %s\n", c, setflags_allowed);
							ok = false;
						}
						break;
					}
				}
			}
			filter_output.filters.emplace_back(std::move(fitem));
		} else if (pcre_exec(panel_pattern, panel_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_action_panel> fitem(new filter_item_action_panel);
			std::string verb(pos + ovector[2], ovector[3] - ovector[2]);
			fitem->remove = (verb == "remove");
			fitem->panel_name = std::string(pos + ovector[4], ovector[5] - ovector[4]);

			filter_output.filters.emplace_back(std::move(fitem));
		} else {
			//line doesn't match
			errmsgs += "Cannot parse line: '" + std::string(pos, linelen) + "'\n";
			ok = false;
		}
	}

	if (!ok) {
		filter_output.filters.clear();
		return;
	}

	filter_output.filter_text = std::move(input);

	filter_run_state frs;
	for (auto &it : filter_output.filters) {
		std::string err;
		if (!it->test_recursion(frs, err)) {
			errmsgs += "Mismatched conditionals: " + err + "\n";
			return;
		}
	}
	if (!frs.recursion.empty()) {
		errmsgs += "Mismatched conditionals: if (s) without terminating endif/fi\n";
		return;
	}
}

bool LoadFilter(std::string input, filter_set &out) {
	std::string errmsgs;
	ParseFilter(std::move(input), out, errmsgs);
	if (!errmsgs.empty()) {
		LogMsgFormat(LOGT::FILTERERR, "Could not parse filter: Error: %s", cstr(errmsgs));
		return false;
	}
	return true;
}

bool InitGlobalFilters() {
	bool f1 = LoadFilter(stdstrwx(gc.gcfg.incoming_filter.val), ad.incoming_filter);
	bool f2 = LoadFilter(stdstrwx(gc.gcfg.alltweet_filter.val), ad.alltweet_filter);
	return f1 && f2;
}

filter_set::filter_set() { }
filter_set::~filter_set() { }

void filter_set::FilterTweet(tweet &tw, taccount *tac) {
	filter_run_state frs;
	frs.tac = tac;
	frs.filter_undo = filter_undo.get();
	for (auto &f : filters) {
		f->exec(tw, frs);
	}
}

void filter_set::FilterTweet(filter_db_lazy_state &state, uint64_t tweet_id) {
	filter_run_state frs;
	frs.filter_undo = filter_undo.get();
	for (auto &f : filters) {
		f->exec(state, tweet_id, frs);
	}
}

filter_set & filter_set::operator=(filter_set &&other) {
	filters = std::move(other.filters);
	filter_undo = std::move(other.filter_undo);
	filter_text = std::move(other.filter_text);
	return *this;
}

filter_set::filter_set(filter_set &&other) {
	*this = std::move(other);
}

void filter_set::clear() {
	filters.clear();
	filter_undo.reset();
	filter_text.clear();
}

void filter_set::EnableUndo() {
	if (!filter_undo) {
		filter_undo.reset(new filter_undo_action());
	}
}

std::unique_ptr<undo::action> filter_set::GetUndoAction() {
	return std::move(filter_undo);
}

void filter_set::DBFilterTweetIDs(filter_set fs, tweetidset ids, bool enable_undo, std::function<void(std::unique_ptr<undo::action>)> completion) {
	dbc.AsyncWriteBackStateMinimal();

	LogMsgFormat(LOGT::FILTERTRACE, "filter_set::DBFilterTweetIDs: sending %zu tweet IDs to DB thread", ids.size());

	struct db_filter_msg : public dbfunctionmsg_callback {
		filter_set fs;
		tweetidset ids;
		filter_bulk_action bulk_action;
	};

	std::unique_ptr<db_filter_msg> msg(new db_filter_msg());
	msg->fs = std::move(fs);
	msg->ids = std::move(ids);

	msg->db_func = [](sqlite3 *db, bool &ok, dbpscache &cache, dbfunctionmsg_callback &self_) {
		db_filter_msg &self = static_cast<db_filter_msg &>(self_);
		// We are now in the DB thread

		filter_db_lazy_state state(db);
		for (uint64_t id : self.ids) {
			self.fs.FilterTweet(state, id);
		}

		self.bulk_action = std::move(state.bulk_action);
	};

	msg->callback_func = [completion, enable_undo](std::unique_ptr<dbfunctionmsg_callback> self_) {
		db_filter_msg &self = static_cast<db_filter_msg &>(*self_);
		LogMsgFormat(LOGT::FILTERTRACE, "filter_set::DBFilterTweetIDs: got reply from DB thread");

		std::unique_ptr<filter_undo_action> undo_action;
		if (enable_undo) {
			undo_action.reset(new filter_undo_action());
		}

		self.bulk_action.execute(undo_action ? &(undo_action->bulk_action) : nullptr);
		LogMsgFormat(LOGT::FILTERTRACE, "filter_set::DBFilterTweetIDs: executed bulk actions");

		completion(std::move(undo_action));
	};

	dbc.SendFunctionMsgCallback(std::move(msg));
}
