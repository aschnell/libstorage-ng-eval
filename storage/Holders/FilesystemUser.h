/*
 * Copyright (c) [2016-2017] SUSE LLC
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


#ifndef STORAGE_FILESYSTEM_USER_H
#define STORAGE_FILESYSTEM_USER_H


#include "storage/Holders/User.h"


namespace storage
{

    class FilesystemUser : public User
    {
    public:

	static FilesystemUser* create(Devicegraph* devicegraph, const Device* source, const Device* target);
	static FilesystemUser* load(Devicegraph* devicegraph, const xmlNode* node);

	virtual FilesystemUser* clone() const override;

	/**
	 * Indicates whether the block device is used as an external journal device.
	 */
	bool is_journal() const;

	void set_journal(bool journal);

    public:

	class Impl;

	Impl& get_impl();
	const Impl& get_impl() const;

    protected:

	FilesystemUser(Impl* impl);

    };


    bool is_filesystem_user(const Holder* holder);

    FilesystemUser* to_filesystem_user(Holder* device);
    const FilesystemUser* to_filesystem_user(const Holder* device);

}

#endif
