//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "retcon.h"
#include "SimpleOpt.h"

typedef CSimpleOptTempl<wxChar> CSO;

enum { OPT_LOGWIN, OPT_FILE, OPT_STDERR, OPT_FILEAUTO };

CSO::SOption g_rgOptions[] =
{
	{ OPT_LOGWIN,  wxT("-w"),             SO_REQ_SHRT  },
	{ OPT_FILE,    wxT("-f"),             SO_REQ_SHRT  },
	{ OPT_STDERR,  wxT("-s"),             SO_REQ_SHRT  },
	{ OPT_FILEAUTO,wxT("-a"),             SO_REQ_SHRT  },
	{ OPT_LOGWIN,  wxT("--log-window"),   SO_REQ_SHRT  },
	{ OPT_FILE,    wxT("--log-file"),     SO_REQ_SHRT  },
	{ OPT_FILEAUTO,wxT("--log-file-auto"),SO_REQ_SHRT  },
	{ OPT_STDERR,  wxT("--log-stderr"),   SO_REQ_SHRT  },

	SO_END_OF_OPTIONS
};

static const wxChar* cmdlineargerrorstr(ESOError err) {
	switch (err) {
	case SO_OPT_INVALID:
		return wxT("Unrecognized option");
	case SO_OPT_MULTIPLE:
		return wxT("Option matched multiple strings");
	case SO_ARG_INVALID:
		return wxT("Option does not accept argument");
	case SO_ARG_INVALID_TYPE:
		return wxT("Invalid argument format");
	case SO_ARG_MISSING:
		return wxT("Required argument is missing");
	case SO_ARG_INVALID_DATA:
		return wxT("Option argument appears to be another option");
	default:
		return wxT("Unknown Error");
	}
}

int cmdlineproc(wxChar ** argv, int argc) {
	CSO args(argc, argv, g_rgOptions, SO_O_CLUMP|SO_O_EXACT|SO_O_SHORTARG|SO_O_FILEARG|SO_O_CLUMP_ARGD|SO_O_NOSLASH);
	while (args.Next()) {
		if (args.LastError() != SO_SUCCESS) {
			wxLogError(wxT("Command line processing error: %s, arg: %s"), cmdlineargerrorstr(args.LastError()), args.OptionText());
			return 1;
		}
		switch(args.OptionId()) {
			case OPT_LOGWIN: {
				logflagtype flagmask=StrToLogFlags(args.OptionArg());
				if(!globallogwindow) new log_window(0, flagmask, true);
				else {
					globallogwindow->lo_flags=flagmask;
					Update_currentlogflags();
				}
				break;
			}
			case OPT_FILE: {
				if(args.m_nNextOption+1>args.m_nLastArg) {
					wxLogError(wxT("Command line processing error: -f/--log-file requires filename argument, arg: %s"), args.OptionText());
					return 1;
				}
				logflagtype flagmask=StrToLogFlags(args.OptionArg());
				wxString filename=args.m_argv[args.m_nNextOption++];
				new log_file(flagmask, filename.char_str());
				break;
			}
			case OPT_FILEAUTO: {
				time_t now=time(0);
				wxString filename=wxT("retcon-log-")+rc_wx_strftime(wxT("%Y%m%dT%H%M%SZ.log"), gmtime(&now), now, false);
				logflagtype flagmask=StrToLogFlags(args.OptionArg());
				new log_file(flagmask, filename.char_str());
				break;
			}
			case OPT_STDERR: {
				logflagtype flagmask=StrToLogFlags(args.OptionArg());
				new log_file(flagmask, stderr);
				break;
			}
		}
	}
	return 0;
}
