#include "retcon.h"
#include <cstring>

void userdataparse::DoProcessValue(const char* str, rapidjson::SizeType length) {
	if(recdepth!=objdepth) return;
	bool usessl=tac?tac->ssl:1;
	if(strncmp("id",str,length+1)==0) { mode=PJ_UINT64; curuint64=&current->id; }
	else if(strncmp("name",str,length+1)==0) { mode=PJ_STRING; curstr=&current->name; }
	else if(strncmp("screen_name",str,length+1)==0) { mode=PJ_STRING; curstr=&current->screen_name; }
	else if(!usessl && strncmp("profile_image_url",str,length+1)==0) { mode=PJ_STRING; curstr=&current->profile_img_url; }
	else if(usessl && strncmp("profile_image_url_https",str,length+1)==0) { mode=PJ_STRING; curstr=&current->profile_img_url; }
	else if(strncmp("protected",str,length+1)==0) { mode=PJ_BOOL; curbool=&current->isprotected; }
}

void userdata::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d"),
		id, wxstrstd(name).c_str(), wxstrstd(screen_name).c_str(), wxstrstd(profile_img_url).c_str(), isprotected);
}

void userdataparse::StartObject() {
	bool good=true;
	if(recdepth==0) {
		baseisarray=false;
		objdepth=1;
	}
	else if(recdepth==1 && baseisarray) {
		objdepth=2;
	}
	else good=false;

	if(good) {
		current=std::make_shared<userdata>();
		current->acc=tac;
	}

	jsonp::StartObject();
}
void userdataparse::EndObject(rapidjson::SizeType memberCount) {
	if(recdepth==objdepth) {
		list.push_front(std::move(current));
	}
	jsonp::EndObject(memberCount);
}
void userdataparse::StartArray() {
	if(recdepth==0) {
		baseisarray=true;
	}
	jsonp::StartArray();
}

std::shared_ptr<userdata> userdataparse::pop_front() {
	std::shared_ptr<userdata> current=std::move(list.front());
	list.pop_front();
	return current;
}

void tweetparse::DoProcessValue(const char* str, rapidjson::SizeType length) {
	if(recdepth!=objdepth) return;

	if(strncmp("id",str,length+1)==0) { mode=PJ_UINT64; curuint64=&current->id; }
	else if(strncmp("in_reply_to_status_id",str,length+1)==0) { mode=PJ_UINT64; curuint64=&current->in_reply_to_status_id; }
	else if(strncmp("retweet_count",str,length+1)==0) { mode=PJ_INT; curint=&current->retweet_count; }
	else if(strncmp("retweeted",str,length+1)==0) { mode=PJ_BOOL; curbool=&current->retweeted; }
	else if(strncmp("source",str,length+1)==0) { mode=PJ_STRING; curstr=&current->source; }
	else if(strncmp("text",str,length+1)==0) { mode=PJ_STRING; curstr=&current->text; }
	else if(strncmp("favorited",str,length+1)==0) { mode=PJ_BOOL; curbool=&current->favourited; }
	else if(strncmp("created_at",str,length+1)==0) { mode=PJ_STRING; curstr=&current->created_at; }
}

void tweet::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nreply_id: %" wxLongLongFmtSpec "d\nretweet_count: %d\retweeted: %d\n"
		"source: %s\ntext: %s\nfavourited: %d\ncreated_at: %s"),
		id, in_reply_to_status_id, retweet_count, retweeted, wxstrstd(source).c_str(),
		wxstrstd(text).c_str(), favourited, wxstrstd(created_at).c_str());
}

void tweetparse::StartObject() {
	bool good=true;
	if(recdepth==0) {
		baseisarray=false;
		objdepth=1;
	}
	else if(recdepth==1 && baseisarray) {
		objdepth=2;
	}
	else good=false;

	if(good) {
		current=std::make_shared<tweet>();
		current->acc=tac;
	}

	jsonp::StartObject();
}
void tweetparse::EndObject(rapidjson::SizeType memberCount) {
	if(recdepth==objdepth) {
		current->createtime.ParseDateTime(wxstrstd(current->created_at));
		list.push_front(std::move(current));
	}
	jsonp::EndObject(memberCount);
}
void tweetparse::StartArray() {
	if(recdepth==0) {
		baseisarray=true;
	}
	jsonp::StartArray();
}

std::shared_ptr<tweet> tweetparse::pop_front() {
	std::shared_ptr<tweet> current=std::move(list.front());
	list.pop_front();
	return current;
}

jsonp::jsonp() {
	recdepth=0;
	inobj=false;
	mode=PJ_NONE;
	isvalue=false;
}

void jsonp::ParseJson(std::shared_ptr<taccount>) {
	this->tac=tac;
	Preparse();
	rapidjson::StringStream ss(json.c_str());
	rd.Parse<0>(ss, *this);
	Postparse();
}

void jsonp::Null() {
	switch(mode) {
		case PJ_STRING: { *curstr=""; break; }
		default: { Int(0); return; }
	}
	CommonPostFunc();
}
void jsonp::Bool(bool b) {
	Int(b);
}
void jsonp::Int(int i) {
	switch(mode) {
		case PJ_NONE: break;
		case PJ_STRING: { *curstr=""; break; }
		case PJ_BOOL: { *curbool=(bool) i; break; }
		case PJ_INT: { *curint=i; break; }
		case PJ_UINT64: { *curuint64=(uint64_t) i; break; }
	}
	CommonPostFunc();
}
void jsonp::Uint(unsigned i) {
	Uint64(i);
}
void jsonp::Int64(int64_t i) {
	Uint64(i);
}
void jsonp::Uint64(uint64_t i) {
	switch(mode) {
		case PJ_UINT64: { *curuint64=i; break; }
		default: { Int(i); return; }
	}
	CommonPostFunc();
}
void jsonp::Double(double d) {
	Null();
}
void jsonp::String(const char* str, rapidjson::SizeType length, bool copy) {
	if(isvalue) {
		switch(mode) {
			case PJ_STRING: { curstr->assign(str, length); break; }
			default: { Null(); return; }
		}
		CommonPostFunc();
	}
	else {
		isvalue=true;
		mode=PJ_NONE;
		DoProcessValue(str, length);
	}
}
void jsonp::StartObject() {
	recdepth++;
	CommonPostFunc();
}
void jsonp::EndObject(rapidjson::SizeType memberCount) {
	recdepth--;
}
void jsonp::StartArray() {
	recdepth++;
}
void jsonp::EndArray(rapidjson::SizeType elementCount) {
	recdepth--;
}
void jsonp::CommonPostFunc() {
	if(isvalue) {
		isvalue=0;
		mode=PJ_NONE;
	}
}
