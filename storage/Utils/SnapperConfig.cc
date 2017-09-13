/*
 * Copyright (c) 2017 SUSE LLC
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
 * with this program; if not, contact SUSE LLC.
 *
 * To contact SUSE LLC about this file by physical or electronic mail, you may
 * find current contact information at www.suse.com.
 */


#include <boost/algorithm/string.hpp>

#include "storage/Utils/SnapperConfig.h"

#include "storage/Devices/BlkDevice.h"
#include "storage/Devices/DeviceImpl.h"
#include "storage/Filesystems/Btrfs.h"
#include "storage/Filesystems/BtrfsImpl.h"
#include "storage/Filesystems/BtrfsSubvolume.h"
#include "storage/Filesystems/MountPoint.h"
#include "storage/Storage.h"
#include "storage/EtcFstab.h"
#include "storage/Utils/ExceptionImpl.h"
#include "storage/Utils/LoggerImpl.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/Text.h"

#define INSTALLATION_HELPER_BIN "/usr/lib/snapper/installation-helper"
#define SNAPSHOTS_DIR ".snapshots"

using namespace storage;


SnapperConfig::SnapperConfig( Btrfs * btrfs )
    : btrfs( btrfs )
    , do_exec( true )
{
    ST_CHECK_PTR( btrfs );
}


void
SnapperConfig::pre_mount()
{
    if ( ! sanity_check() )
        return;

    // TRANSLATORS: first snapshot description
    Text snapshot_description = _("first root filesystem");

    // See also
    // https://github.com/openSUSE/snapper/blob/master/client/installation-helper.cc

    vector<string> args = {
        "--step", "1",
        "--device", get_parent_device_name(),
        "--description", snapshot_description.translated
    };

    installation_helper( args );
}


void
SnapperConfig::post_mount()
{
    if ( ! sanity_check() )
        return;

    vector<string> args = {
        "--step", "2",
        "--device", get_parent_device_name(),
        "--root-prefix", get_root_prefix(),
        "--default-subvolume-name", get_default_subvolume_name()
    };

    installation_helper( args );
}


void
SnapperConfig::post_add_to_etc_fstab( EtcFstab & etc_fstab )
{
    if ( ! sanity_check() )
        return;

    // Don't call installation-helper as an external program for this step
    // because this would modify /etc/fstab on the target system while it is
    // already open and modified by libstorage during the commit phase, so any
    // changes made to that file by installation-helper are silently
    // overwritten by libstorage (bsc#1057443).

    // Sample fstab entry:
    //
    // UUID=5acfd198-963a-4740-a33e-030b7f305d67 /.snapshots btrfs subvol=/@/.snapshots 0 0


    FstabEntry * entry = new FstabEntry();

    entry->set_device( get_device_name() );
    entry->set_mount_point( "/" SNAPSHOTS_DIR );
    entry->set_fs_type( FsType::BTRFS );

    MountOpts mount_opts = btrfs->get_impl().get_mount_options();
    mount_opts.append( string( "subvol=/" ) + get_snapshots_subvol_name() );
    entry->set_mount_opts( mount_opts );

    etc_fstab.write(); // Just to make the next logged diff shorter
    y2mil( "Adding .snapshots subvolume to /etc/fstab:" );
    etc_fstab.add( entry );
    etc_fstab.log_diff();
    etc_fstab.write();
}


bool
SnapperConfig::installation_helper( const vector<string> & args )
{
    command_line = build_command_line( INSTALLATION_HELPER_BIN, args );

    if ( do_exec )
    {
        SystemCmd cmd( command_line, SystemCmd::NoThrow );

        return cmd.retcode() == 0;
    }
    else
    {
        y2mil( "NOT executing command: " << command_line );

        return true;
    }
}


string
SnapperConfig::build_command_line( const string & command,
                                   const vector<string> & args ) const
{
    string command_line = command;

    for ( const string & arg: args )
    {
        command_line += " ";

        if ( boost::starts_with( arg, "--" ) )
            command_line += arg;
        else
            command_line += SystemCmd::quote( arg );
    }

    return command_line;
}


bool
SnapperConfig::sanity_check() const
{
    if ( ! btrfs->get_configure_snapper() )
        return false;

    if ( ! btrfs->get_mount_point() ||
         btrfs->get_mount_point()->get_path() != "/" )
    {
        y2err( "Snapper configuration is only supported for root filesystems" );

        return false;
    }

    return true;
}


string
SnapperConfig::get_parent_device_name() const
{
    string result;

    if ( btrfs->has_parents() )
    {
        const BlkDevice * parent = to_blk_device( btrfs->get_parents().front() );

        if ( parent )
            result = parent->get_name();
    }

    return result;
}


string
SnapperConfig::get_default_subvolume_name() const
{
    return btrfs->get_default_btrfs_subvolume()->get_path();
}


string
SnapperConfig::get_root_prefix() const
{
    Device * device = to_device_of_type<Device>(btrfs);

    return device->get_impl().get_devicegraph()->get_storage()->get_rootprefix();
}


string
SnapperConfig::get_device_name() const
{
    return btrfs->get_impl().get_mount_by_name( btrfs->get_mount_point()->get_mount_by() );
}


string
SnapperConfig::get_snapshots_subvol_name() const
{
    string subvol_name = get_default_subvolume_name();

    if ( ! subvol_name.empty() )
        subvol_name += "/";

    subvol_name += SNAPSHOTS_DIR;

    return subvol_name;
}
