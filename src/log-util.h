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

#ifndef HGUARD_SRC_LOG_UTIL
#define HGUARD_SRC_LOG_UTIL

#include "univdefs.h"
#include "log.h"

struct tweet;
struct taccount;

std::string truncate_tweet_text(const std::string &input);
std::string tweet_log_line(const tweet *t);
std::string tweet_long_log_line(const tweet *t);
std::string user_screenname_log(uint64_t id);
std::string user_short_log_line(uint64_t id);
std::string tweet_short_log_line(uint64_t id);
void dump_pending_acc(LOGT logflags, const std::string &indent, const std::string &indentstep, taccount *acc);
void dump_tweet_pendings(LOGT logflags, const std::string &indent, const std::string &indentstep);
void dump_pending_acc_failed_conns(LOGT logflags, const std::string &indent, const std::string &indentstep, taccount *acc);
void dump_pending_retry_conn(LOGT logflags, const std::string &indent, const std::string &indentstep);
void dump_pending_active_conn(LOGT logflags, const std::string &indent, const std::string &indentstep);
void dump_acc_socket_flags(LOGT logflags, const std::string &indent, taccount *acc);
void dump_id_stats(LOGT logflags, const std::string &indent, const std::string &indentstep);

#endif
