/*
 * Copyright (c) 2018 SUSE LLC
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


#ifndef STORAGE_CALLBACKS_H
#define STORAGE_CALLBACKS_H


#include <string>


namespace storage
{

    class Callbacks
    {

    public:

	virtual ~Callbacks() {}

	/**
	 * Callback for progress messages.
	 *
	 * message is translated.
	 */
	virtual void message(const std::string& message) const = 0;

	/**
	 * Callback for errors.
	 *
	 * message is translated. what is usually not translated and may be
	 * empty.
	 *
	 * If it returns true the error is ignored as good as possible.
	 */
	virtual bool error(const std::string& message, const std::string& what) const = 0;

    };

}


#endif
