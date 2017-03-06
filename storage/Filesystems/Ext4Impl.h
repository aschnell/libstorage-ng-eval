/*
 * Copyright (c) [2014-2015] Novell, Inc.
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


#ifndef STORAGE_EXT4_IMPL_H
#define STORAGE_EXT4_IMPL_H


#include "storage/Filesystems/Ext4.h"
#include "storage/Filesystems/ExtImpl.h"


namespace storage
{

    using namespace std;


    template <> struct DeviceTraits<Ext4> { static const char* classname; };


    class Ext4::Impl : public Ext::Impl
    {
    public:

	Impl()
	    : Ext::Impl() {}

	Impl(const xmlNode* node);

	virtual FsType get_type() const override { return FsType::EXT4; }

	virtual const char* get_classname() const override { return DeviceTraits<Ext4>::classname; }

	virtual string get_displayname() const override { return "ext4"; }

	virtual Impl* clone() const override { return new Impl(*this); }

	virtual ResizeInfo detect_resize_info() const override;

	virtual uint64_t used_features() const override;

    };


    static_assert(!std::is_abstract<Ext4>(), "Ext4 ought not to be abstract.");
    static_assert(!std::is_abstract<Ext4::Impl>(), "Ext4::Impl ought not to be abstract.");

}

#endif
