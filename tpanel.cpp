#include "retcon.h"

void tpanel::PushTweet(std::shared_ptr<tweet> t) {
	if(tweetlist.count(t->id)) {
		//already have this tweet
		return;
	}
	else {
		auto td=std::make_shared<tweetdisp>();
		tweetlist[t->id]=td;
	}
}

tpanel::tpanel(std::string name_) {
	name=name_;
}

tpanelparentwin::tpanelparentwin(std::shared_ptr<tpanel> tp_)
: wxPanel(topframe) {
	tp=tp_;
	tpanelwin *tpw = new tpanelwin(this);
	wxBoxSizer *vbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(tpw, 1, wxALIGN_TOP | wxEXPAND, 0);
	SetSizer(vbox);

	topframe->auim->AddPane(this, wxAuiPaneInfo().Resizable().Top().Caption(wxstrstd(tp->name)).Movable().GripperTop().Dockable(false).TopDockable());
	topframe->auim->Update();

	ad.tpanelpwin[tp->name]=this;
}

tpanelparentwin::~tpanelparentwin() {
	ad.tpanelpwin.erase(tp->name);
}

tpanelwin::tpanelwin(tpanelparentwin *tppw_)
: wxRichTextCtrl(tppw_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxRE_READONLY) {
	tp=tppw_->tp;
	GetCaret()->Hide();
}
