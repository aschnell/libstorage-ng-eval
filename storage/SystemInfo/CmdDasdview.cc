/*
 * Copyright (c) [2004-2014] Novell, Inc.
 *
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, contact Novell, Inc.
 *
 * To contact Novell about this file by physical or electronic mail, you may
 * find current contact information at www.novell.com.
 */


#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/StorageTypes.h"
#include "storage/SystemInfo/CmdDasdview.h"
#include "storage/Devices/DiskImpl.h"


namespace storage
{

    Dasdview::Dasdview(const string& device)
	: device(device), dasd_format(DASDF_NONE), dasd_type(DASDTYPE_NONE)
    {
	SystemCmd cmd(DASDVIEWBIN " --extended " + quote(device));

	if (cmd.retcode() == 0)
	{
	    parse(cmd.stdout());
	}
	else
	{
	    y2err("dasdview failed");
	}
    }


    void
    Dasdview::parse(const vector<string>& lines)
    {
	vector<string>::const_iterator pos;

	pos = find_if(lines, string_starts_with("format"));
	if (pos != lines.end())
	{
	    y2mil("Format line:" << *pos);
	    string tmp = string(*pos, pos->find(':') + 1);
	    tmp = extractNthWord(4, tmp);
	    if (tmp == "CDL")
		dasd_format = DASDF_CDL;
	    else if (tmp == "LDL")
		dasd_format = DASDF_LDL;
	}

	pos = find_if(lines, string_starts_with("type"));
	if (pos != lines.end())
	{
	    y2mil("Type line:" << *pos);
	    string tmp = string(*pos, pos->find(':') + 1);
	    tmp = extractNthWord(0, tmp);
	    if (tmp == "ECKD")
		dasd_type = DASDTYPE_ECKD;
	    else if (tmp == "FBA")
		dasd_type = DASDTYPE_FBA;
	}

	y2mil(*this);
    }


    std::ostream& operator<<(std::ostream& s, const Dasdview& dasdview)
    {
	s << "device:" << dasdview.device << " dasd_format:"
	  << toString(dasdview.dasd_format) << " dasd_type:" << toString(dasdview.dasd_type);

	return s;
    }

}
