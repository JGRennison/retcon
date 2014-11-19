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
#include "../taccount.h"
#include "../flags.h"
#include "../tpanel-data.h"
#include "../map.h"
#define PCRE_STATIC
#include <pcre.h>
#include <list>

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
	virtual ~filter_item() { }
	virtual bool test_recursion(filter_run_state &frs, std::string &err) { return true; }
};

struct filter_item_cond : public filter_item {
	virtual bool test(tweet &tw, filter_run_state &frs) { return true; }
	void exec(tweet &tw, filter_run_state &frs) override {
		if(flags & FIF::ENDIF) {
			frs.recursion.pop_back();
			return;
		}
		else if(flags & FIF::ELIF) {
			if(frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
				return;
			}
		}
		else if(flags & FIF::ORIF) {
			if(frs.recursion.back() & FRSF::ACTIVE) {
				//leave the active bit set
				return;
			}
			if(frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
				return;
			}
		}
		else if(flags & FIF::ELSE) {
			if(frs.recursion.back() & (FRSF::DONEIF | FRSF::PARENTINACTIVE)) {
				frs.recursion.back() &= ~FRSF::ACTIVE;
			}
			else {
				frs.recursion.back() |= FRSF::DONEIF | FRSF::ACTIVE;
			}
			return;
		}
		else {
			if(!frs.recursion.empty() && !(frs.recursion.back() & FRSF::ACTIVE)) {
				//this is a nested if, the parent if is not active
				frs.recursion.push_back(FRSF::PARENTINACTIVE);
				return;
			}
			frs.recursion.push_back(0);
		}

		bool testresult = test(tw, frs);
		if(flags & FIF::NEG) testresult = !testresult;
		if(testresult) {
			frs.recursion.back() |= FRSF::DONEIF | FRSF::ACTIVE;
		}
		else {
			frs.recursion.back() &= ~FRSF::ACTIVE;
		}
	}
	bool test_recursion(filter_run_state &frs, std::string &err) override {
		if(flags & FIF::ENDIF) {
			if(frs.recursion.empty()) {
				err = "endif/fi without opening if";
				return false;
			}
			frs.recursion.pop_back();
		}
		else if(flags & FIF::ELIF || flags & FIF::ELSE || flags & FIF::ORIF) {
			if(frs.recursion.empty()) {
				err = "elif/orif/else without corresponding if";
				return false;
			}
		}
		else {
			frs.recursion.push_back(0);
		}
		return true;
	}
};

using tweetmodefptr = const std::string & (*)(tweet &, filter_run_state &);
using usrmodefptr = const std::string & (*)(userdatacontainer *u, tweet &, filter_run_state &frs);

struct filter_item_cond_regex : public filter_item_cond {
	pcre *ptn = nullptr;
	pcre_extra *extra = nullptr;
	std::string regexstr;
	std::function<bool(filter_item_cond_regex &, tweet &, filter_run_state &)> dotests;

	struct user_cache_entry {
		unsigned int revision;
		bool result;
	};
	container::map<uint64_t, user_cache_entry> usertestcache;

	bool tweet_test(tweetmodefptr fptr, tweet &top_tw, tweet &tw, filter_run_state &frs) {
		return regex_test(fptr(tw, frs), top_tw);
	}

	bool user_test(usrmodefptr fptr, tweet &top_tw, userdatacontainer *u, filter_run_state &frs) {
		if(!u) return regex_test(fptr(u, top_tw, frs), top_tw);

		auto iter = usertestcache.insert(std::make_pair(u->id, user_cache_entry()));
		bool new_insertion = iter.second;
		user_cache_entry &uce = iter.first->second;
		if(!new_insertion && uce.revision == u->GetUser().revision_number) {
			//cached result
			return uce.result;
		}

		uce.revision = u->GetUser().revision_number;
		uce.result = regex_test(fptr(u, top_tw, frs), top_tw);
		return uce.result;
	}

	bool regex_test(const std::string &str, tweet &tw) {
		const int ovecsize = 30;
		int ovector[30];
		bool result = (pcre_exec(ptn, extra,  str.c_str(), str.size(), 0, 0, ovector, ovecsize) >= 1);
		LogMsgFormat(LOGT::FILTERTRACE, "String Regular Expression Test for Tweet: %" llFmtSpec "d, String: '%s', Regex: '%s', Result: %smatch",
				tw.id, cstr(str), cstr(regexstr), result ? "" : "no ");
		return result;
	}

	bool test(tweet &tw, filter_run_state &frs) override {
		return dotests(*this, tw, frs);
	}

	virtual ~filter_item_cond_regex() {
		if(ptn) pcre_free(ptn);
		if(ptn) pcre_free_study(extra);
	}
};

struct filter_item_cond_flags : public filter_item_cond {
	uint64_t any = 0;
	uint64_t all = 0;
	uint64_t none = 0;
	uint64_t missing = 0;
	bool retweet;
	std::string teststr;

	bool test(tweet &tw, filter_run_state &frs) override {
		uint64_t curflags = tw.flags.Save();
		bool result = true;
		if(any && !(curflags&any)) result = false;
		if(all && (curflags&all)!=all) result = false;
		if(none && (curflags&none)) result = false;
		if(missing && (curflags|missing)==curflags) result = false;
		LogMsgFormat(LOGT::FILTERTRACE, "Tweet Flag Test for Tweet: %" llFmtSpec "d, Flags: %s, Criteria: %s, Result: %smatch",
				tw.id, cstr(tw.flags.GetString()), cstr(teststr), result ? "" : "no ");
		return result;
	}
};

struct filter_item_action : public filter_item {
	virtual void action(tweet &tw) = 0;
	void exec(tweet &tw, filter_run_state &frs) {
		if(frs.recursion.empty()) action(tw);
		else if(frs.recursion.back() & FRSF::ACTIVE) action(tw);
	}
};

const char setflags_allowed[] = "hnpruH";
struct filter_item_action_setflag : public filter_item_action {
	uint64_t setflags = 0;
	uint64_t unsetflags = 0;
	std::string setstr;

	void action(tweet &tw) override {
		unsigned long long oldflags = tw.flags.Save();
		unsigned long long newflags = (oldflags | setflags) & ~unsetflags;
		tw.flags = tweet_flags(newflags);
		LogMsgFormat(LOGT::FILTERTRACE, "Setting Tweet Flags for Tweet: %" llFmtSpec "d, Flags: Before %s, Action: %s, Result: %s",
				tw.id, cstr(tweet_flags::GetValueString(oldflags)), cstr(setstr), cstr(tweet_flags::GetValueString(newflags)));
	}
};

struct filter_item_action_panel : public filter_item_action {
	bool remove;
	std::string panel_name;

	void action(tweet &tw) override {
		if(remove) {
			std::shared_ptr<tpanel> tp = ad.tpanels[tpanel::ManualName(panel_name)];
			if(tp) {
				tp->RemoveTweet(tw.id);
				LogMsgFormat(LOGT::FILTERTRACE, "Removing Tweet: %" llFmtSpec "d from Panel: %s", tw.id, cstr(panel_name));
			}
		}
		else {
			std::shared_ptr<tpanel> tp = tpanel::MkTPanel(tpanel::ManualName(panel_name), panel_name, TPF::MANUAL | TPF::SAVETODB);
			tp->PushTweet(ad.GetTweetById(tw.id));
			LogMsgFormat(LOGT::FILTERTRACE, "Adding Tweet: %" llFmtSpec "d to Panel: %s", tw.id, cstr(panel_name));
		}
	}
};

const char condsyntax[] = R"(^\s*(?:(?:(el)(?:s(?:e\s*)?)?)?|(or\s*))if(n)?\s+)";
const char regexsyntax[] = R"(^(\w+)\.(\w+)\s+(.*\S)\s*$)";
const char flagtestsyntax[] = R"(^(re)?tweet\.flags?\s+([a-zA-Z0-9+=\-/]+)\s*)";

const char elsesyntax[] = R"(^\s*else\s*$)";
const char endifsyntax[] = R"(^\s*(?:end\s*if|fi)\s*$)";

const char flagsetsyntax[] = R"(^\s*set\s+tweet\.flags\s+([a-zA-Z0-9+\-]+)\s*$)";
const char panelsyntax[] = R"(^\s*panel\s+(add|remove)\s+(\S.*\S)\s*$)";

const char blanklinesyntax[] = R"(^(?:\s*#.*)?\s*$)"; //this also filters comments


void ParseFilter(const std::string &input, filter_set &filter_output, std::string &errmsgs) {
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
		if(!ptn) {
			ok = false;
			return;
		}
		if(*ptn) {
			return;
		}

		const char *errptr;
		int erroffset;

		*ptn = pcre_compile(str, PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, nullptr);
		if(!*ptn) {
			LogMsgFormat(LOGT::FILTERERR, "pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(str));
			ok = false;
			return;
		}
		if(extra && !*extra) {
			*extra = pcre_study(*ptn, PCRE_STUDY_JIT_COMPILE, &errptr);
#if PCRE_STUDY_JIT_COMPILE
			int value = 0;
			pcre_fullinfo(*ptn, *extra, PCRE_INFO_JIT, &value);
			if(value) jit_count++;
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

	if(!ok) return;

	LogMsgFormat(LOGT::FILTERTRACE, "ParseFilter: %u JITed", jit_count);

	const int ovecsize = 60;
	int ovector[60];

	size_t linestart = 0;

	while(linestart < input.size()) {
		size_t linelen = 0;
		size_t lineeol = 0;
		while(linestart + linelen < input.size()) {
			char c = input[linestart + linelen];
			if(c == '\r' || c == '\n') {
				lineeol++;
				break;
			}
			else linelen++;
		}

		const char *pos = input.c_str() + linestart;

		linestart += linelen + lineeol;

		if(pcre_exec(blankline_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) continue;  //eat blank lines

		if(pcre_exec(cond_pattern, cond_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			// this is a conditional

			int nextsectionoffset = ovector[1];

			bool iselif = ovector[2] >= 0;
			bool isorif = ovector[4] >= 0;
			bool isnegative = ovector[6] >= 0;

			flagwrapper<FIF> flags = FIF::COND;
			if(iselif) flags |= FIF::ELIF;
			if(isnegative) flags |= FIF::NEG;
			if(isorif) flags |= FIF::ORIF;

			if(pcre_exec(flagtest_pattern, flagtest_patextra, pos + nextsectionoffset, linelen - nextsectionoffset, 0, 0, ovector, ovecsize) >= 1) {
				std::unique_ptr<filter_item_cond_flags> fitem(new filter_item_cond_flags);
				fitem->teststr = std::string(pos + nextsectionoffset + ovector[4], ovector[5] - ovector[4]);

				uint64_t *current = &(fitem->any);
				for(auto c : fitem->teststr) {
					switch(c) {
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
			}
			else if(pcre_exec(regex_pattern, regex_patextra,  pos + nextsectionoffset, linelen - nextsectionoffset, 0, 0, ovector, ovecsize) >= 1) {
				std::string part1(pos + nextsectionoffset + ovector[2], ovector[3] - ovector[2]);
				std::string part2(pos + nextsectionoffset + ovector[4], ovector[5] - ovector[4]);

				std::unique_ptr<filter_item_cond_regex> ritem(new filter_item_cond_regex);
				ritem->flags = flags;

				const char *errptr;
				int erroffset;
				std::string userptnstr(pos + nextsectionoffset + ovector[6], ovector[7] - ovector[6]);
				ritem->ptn = pcre_compile(userptnstr.c_str(), PCRE_NO_UTF8_CHECK | PCRE_UTF8, &errptr, &erroffset, 0);
				if(!ritem->ptn) {
					LogMsgFormat(LOGT::FILTERERR, "pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(userptnstr));
					ok = false;
				}
				else {
					ritem->extra = pcre_study(ritem->ptn, PCRE_STUDY_JIT_COMPILE, &errptr);
					if(currentlogflags & LOGT::FILTERTRACE) {
						int jit = 0;
#if PCRE_STUDY_JIT_COMPILE
						pcre_fullinfo(ritem->ptn, ritem->extra, PCRE_INFO_JIT, &jit);
#endif
						LogMsgFormat(LOGT::FILTERTRACE, "ParseFilter: pcre_compile and pcre_study success: JIT: %u\n%s", jit, cstr(userptnstr));
					}
				}
				ritem->regexstr = std::move(userptnstr);

				auto tweetmode = [&]() -> tweetmodefptr{
					if(part2 == "text") {
						return [](tweet &tw, filter_run_state &frs) -> const std::string & {
							return tw.text;
						};
					}
					else if(part2 == "source") {
						return [](tweet &tw, filter_run_state &frs) -> const std::string &  {
							return tw.source;
						};
					}
					else {
						errmsgs += string_format("No such tweet field: %s\n", part2.c_str());
						ok = false;
						return [](tweet &tw, filter_run_state &frs) -> const std::string &  {
							return frs.empty_str;
						};
					}
				};

				auto usermode = [&]() -> usrmodefptr {
					if(part2 == "name") {
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string &  {
							return u ? u->user.name : frs.empty_str;
						};
					}
					else if(part2 == "screenname" || part2 == "sname") {
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string &  {
							return u ? u->user.screen_name : frs.empty_str;
						};
					}
					else if(part2 == "description" || part2 == "desc") {
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string & {
							return u ? u->user.description : frs.empty_str;
						};
					}
					else if(part2 == "loc" || part2 == "location") {
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string &  {
							return u ? u->user.location : frs.empty_str;
						};
					}
					else if(part2 == "id") {
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string &  {
							frs.test_temp = string_format("%" llFmtSpec "u", u->id);
							return frs.test_temp;
						};
					}
					else {
						errmsgs += string_format("No such user field: %s\n", part2.c_str());
						ok = false;
						return [](userdatacontainer *u, tweet &tw, filter_run_state &frs) -> const std::string &  {
							return frs.empty_str;
						};
					}
				};

				if(part1 == "retweet") {
					auto fptr = tweetmode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) -> bool {
						if(tw.rtsrc) return r.tweet_test(fptr, tw, *tw.rtsrc, frs);
						else return r.tweet_test(fptr, tw, tw, frs);
					};
				}
				else if(part1 == "tweet") {
					auto fptr = tweetmode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						return r.tweet_test(fptr, tw, tw, frs);
					};
				}
				else if(part1 == "user") {
					auto fptr = usermode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						return r.user_test(fptr, tw, tw.user.get(), frs);
					};
				}
				else if(part1 == "retweetuser") {
					auto fptr = usermode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						if(tw.rtsrc) return r.user_test(fptr, tw, tw.rtsrc->user.get(), frs);
						else return r.user_test(fptr, tw, tw.user.get(), frs);
					};
				}
				else if(part1 == "userrecipient") {
					auto fptr = usermode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						if(tw.user_recipient) return r.user_test(fptr, tw, tw.user_recipient.get(), frs);
						else return r.user_test(fptr, tw, tw.user.get(), frs);
					};
				}
				else if(part1 == "anyuser") {
					auto fptr = usermode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						if(tw.user_recipient && r.user_test(fptr, tw, tw.user_recipient.get(), frs)) return true;
						if(tw.rtsrc && r.user_test(fptr, tw, tw.rtsrc->user.get(), frs)) return true;
						return r.user_test(fptr, tw, tw.user.get(), frs);
					};
				}
				else if(part1 == "accountuser") {
					auto fptr = usermode();
					ritem->dotests = [fptr](filter_item_cond_regex &r, tweet &tw, filter_run_state &frs) {
						if(frs.tac) return r.user_test(fptr, tw, frs.tac->usercont.get(), frs);
						else return r.regex_test("", tw);
					};
				}
				else {
					errmsgs += string_format("No such field type: %s\n", part1.c_str());
					ok = false;
				}

				if(ok) filter_output.filters.emplace_back(std::move(ritem));
			}
			else {
				//conditional doesn't match
				errmsgs += "Cannot parse condition: '" + std::string(pos + nextsectionoffset, linelen - nextsectionoffset) + "'\n";
				ok = false;
			}
		}
		else if(pcre_exec(else_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_cond> citem(new filter_item_cond);
			citem->flags = FIF::COND | FIF::ELSE;
			filter_output.filters.emplace_back(std::move(citem));
		}
		else if(pcre_exec(endif_pattern, nullptr,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_cond> citem(new filter_item_cond);
			citem->flags = FIF::COND | FIF::ENDIF;
			filter_output.filters.emplace_back(std::move(citem));
		}
		else if(pcre_exec(flagset_pattern, flagset_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			static unsigned long long allowed_flags = tweet_flags::GetFlagStringValue(setflags_allowed);

			std::unique_ptr<filter_item_action_setflag> fitem(new filter_item_action_setflag);
			fitem->setstr = std::string(pos + ovector[2], ovector[3] - ovector[2]);

			uint64_t *current = &(fitem->setflags);
			for(auto c : fitem->setstr) {
				switch(c) {
					case '+': current = &(fitem->setflags); break;
					case '-': current = &(fitem->unsetflags); break;
					default: {
						unsigned long long flag = tweet_flags::GetFlagValue(c);
						if(flag & allowed_flags) *current |= flag;
						else {
							errmsgs += string_format("Setting tweet flag '%c' is not allowed. Allowed flags: %s\n", c, setflags_allowed);
							ok = false;
						}
						break;
					}
				}
			}
			filter_output.filters.emplace_back(std::move(fitem));
		}
		else if(pcre_exec(panel_pattern, panel_patextra,  pos, linelen, 0, 0, ovector, ovecsize) >= 1) {
			std::unique_ptr<filter_item_action_panel> fitem(new filter_item_action_panel);
			std::string verb(pos + ovector[2], ovector[3] - ovector[2]);
			fitem->remove = (verb == "remove");
			fitem->panel_name = std::string(pos + ovector[4], ovector[5] - ovector[4]);

			filter_output.filters.emplace_back(std::move(fitem));
		}
		else {
			//line doesn't match
			errmsgs += "Cannot parse line: '" + std::string(pos, linelen) + "'\n";
			ok = false;
		}
	}

	if(!ok) {
		filter_output.filters.clear();
		return;
	}

	filter_run_state frs;
	for(auto &it : filter_output.filters) {
		std::string err;
		if(!it->test_recursion(frs, err)) {
			errmsgs += "Mismatched conditionals: " + err + "\n";
			return;
		}
	}
	if(!frs.recursion.empty()) {
		errmsgs += "Mismatched conditionals: if(s) without terminating endif/fi\n";
		return;
	}
}

bool LoadFilter(const std::string &input, filter_set &out) {
	std::string errmsgs;
	ParseFilter(input, out, errmsgs);
	if(!errmsgs.empty()) {
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
	for(auto &f : filters) {
		f->exec(tw, frs);
	}
}

filter_set & filter_set::operator=(filter_set &&other) {
	filters = std::move(other.filters);
	return *this;
}

void filter_set::clear() {
	filters.clear();
}
