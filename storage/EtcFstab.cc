/*
 * Copyright (c) [2004-2015] Novell, Inc.
 * Copyright (c) [2017] SUSE LLC
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

#include <stdlib.h>
#include <iostream>
#include <regex>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include "storage/EtcFstab.h"
#include "storage/Filesystems/FilesystemImpl.h"
#include "storage/Utils/LoggerImpl.h"
#include "storage/Utils/StorageTmpl.h"


#define FSTAB_COLUMN_COUNT	6


namespace storage
{

    MountOpts::MountOpts( const string & opt_string )
    {
        if ( ! opt_string.empty() )
            parse( opt_string );
    }


    string MountOpts::get_opt( int index ) const
    {
	if ( index >= 0 && index < (int) opts.size() )
	    return opts[ index ];
	else
	    return "";
    }


    void MountOpts::set_opt( int index, const string & new_val )
    {
	if ( index >= 0 && index < (int) opts.size() )
	    opts[ index ] = new_val;
    }


    bool MountOpts::contains( const string & wanted_opt ) const
    {
	for ( size_t i=0; i < opts.size(); i++)
	{
	    if ( opts[i] == wanted_opt )
		return true;
	}

	return false;
    }


    bool
    MountOpts::has_subvol() const
    {
	return any_of(opts.begin(), opts.end(), [](const string& opt) {
	    return boost::starts_with(opt, "subvolid=") || boost::starts_with(opt, "subvol=");
	});
    }


    bool
    MountOpts::has_subvol(long id, const string& path) const
    {
	regex re_id("subvolid=[0-9]*" + to_string(id), regex_constants::extended);
	regex re_path("subvol=/*" + path, regex_constants::extended);

	return any_of(opts.begin(), opts.end(), [re_id, re_path](const string& opt) {
	    return regex_match(opt, re_id) || regex_match(opt, re_path);
	});
    }


    int MountOpts::get_index_of( const string & wanted_opt ) const
    {
	for ( size_t i=0; i < opts.size(); i++)
	{
	    if ( opts[i] == wanted_opt )
		return i;
	}

	return -1;
    }


    void MountOpts::remove( int index )
    {
	if ( index >= 0 && index < (int) opts.size() )
	    opts.erase( opts.begin() + index );
    }


    void MountOpts::remove( const string & opt )
    {
	int index = get_index_of( opt );

	if ( index != -1 )
	    remove( index );
    }


    void MountOpts::append( const string & opt )
    {
	opts.push_back( opt );
    }


    string MountOpts::format() const
    {
	if ( opts.empty() )
	    return "defaults";

	string result;

	for ( size_t i=0; i < opts.size(); ++i )
	{
	    if ( ! result.empty() )
		result += ",";

	    result += opts[i];
	}

	return result;
    }


    bool MountOpts::parse( const string & opt_string, int line_no )
    {
        string decoded = EtcFstab::fstab_decode( opt_string );
	boost::split( opts,
		      decoded,
		      boost::is_any_of( "," ),
		      boost::token_compress_on );

	while ( contains( "defaults" ) )
	    remove( "defaults" );

        return true;
    }




    FstabEntry::FstabEntry()
	: fs_type(FsType::UNKNOWN), dump_pass(0), fsck_pass(0)
    {
    }


    FstabEntry::FstabEntry( const string & device,
			    const string & mount_point,
			    FsType	   fs_type ):
	device( device ),
	mount_point( mount_point ),
	fs_type( fs_type ),
	dump_pass( 0 ),
	fsck_pass( 0 )
    {

    }


    FstabEntry::~FstabEntry()
    {

    }


    void FstabEntry::populate_columns()
    {
	set_column_count( FSTAB_COLUMN_COUNT );

	int col = 0;
	set_column( col++, EtcFstab::fstab_encode( device      ) );
	set_column( col++, EtcFstab::fstab_encode( mount_point ) );

        if ( fs_type != FsType::UNKNOWN )
            set_column( col++, toString( fs_type ) );
        else
        {
            if ( ! get_column( col ).empty() )
                col++; // just leave the old content
            else
            {
                y2err( "File system type unknown for " << device << " at " << mount_point );
                set_column( col++, "unknown" );
            }
        }
        
	set_column( col++, mount_opts.format() );
	set_column( col++, std::to_string( dump_pass ) );
	set_column( col++, std::to_string( fsck_pass ) );
    }


    bool FstabEntry::parse( const string & line, int line_no )
    {
	// Delegate splitting into fields to parent class.
	//
	// This always returns 'true' (success), so there is no need to check
	// the result and possibly log an error.

	ColumnConfigFile::Entry::parse( line, line_no );

	if ( get_column_count() != FSTAB_COLUMN_COUNT )
	{
	    y2err( "fstab:" << line_no << " Error: wrong number of fields: \"" << line << "\"" );
	    return false;
	}

	int col = 0;
	device	    = EtcFstab::fstab_decode( get_column( col++ ) );
	mount_point = EtcFstab::fstab_decode( get_column( col++ ) );

	bool ok = toValue( get_column( col++ ), fs_type );

	if ( ! ok )
            fs_type = FsType::UNKNOWN;

	mount_opts.parse( EtcFstab::fstab_decode( get_column( col++ ) ), line_no );
	dump_pass = atoi( get_column( col++ ).c_str() );
	fsck_pass = atoi( get_column( col++ ).c_str() );

	return true; // success
    }




    EtcFstab::EtcFstab():
	ColumnConfigFile()
    {
	// Set reasonable field widths for /etc/fstab

	int col = 0;
	set_max_column_width( col++, 45 ); // device; just enough for UUID=...
	set_max_column_width( col++, 25 ); // mount point
	set_max_column_width( col++,  7 ); // fs type
	set_max_column_width( col++, 30 ); // mount options
	set_max_column_width( col++,  1 ); // dump pass
	set_max_column_width( col++,  1 ); // fsck pass

	set_pad_columns( true );
        set_diff_enabled();
    }


    EtcFstab::~EtcFstab()
    {

    }


    FstabEntry * EtcFstab::find_device( const string & device  ) const
    {
	for ( int i=0; i < get_entry_count(); ++i )
	{
	    FstabEntry * entry = get_entry( i );

	    if ( entry && entry->get_device() == device )
		return entry;
	}

	return 0;
    }


    FstabEntry * EtcFstab::find_device( const string_vec & devices ) const
    {
	for ( int i=0; i < get_entry_count(); ++i )
	{
	    FstabEntry * entry = get_entry( i );

	    if ( entry )
	    {
                const string & current_dev = entry->get_device();

		for ( size_t j=0; j < devices.size(); ++j )
		{
		    if ( current_dev == devices[j] )
			return entry;
		}
	    }
	}

	return 0;
    }


    vector<FstabEntry*>
    EtcFstab::find_all_devices(const vector<string>& devices)
    {
	vector<FstabEntry*> ret;

	for (int i = 0; i < get_entry_count(); ++i)
	{
	    FstabEntry* entry = get_entry(i);
	    if (entry && contains(devices, entry->get_device()))
		ret.push_back(entry);
	}

	return ret;
    }


    vector<const FstabEntry*>
    EtcFstab::find_all_devices(const vector<string>& devices) const
    {
	vector<const FstabEntry*> ret;

	for (int i = 0; i < get_entry_count(); ++i)
	{
	    FstabEntry* entry = get_entry(i);
	    if (entry && contains(devices, entry->get_device()))
		ret.push_back(entry);
	}

	return ret;
    }


    FstabEntry * EtcFstab::find_mount_point( const string & mount_point, int & index_ret ) const
    {
	for ( int i=0; i < get_entry_count(); ++i )
	{
	    FstabEntry * entry = get_entry( i );

	    if ( entry && entry->get_mount_point() == mount_point )
	    {
		index_ret = i;
		return entry;
	    }
	}

	index_ret = -1;
	return 0;
    }


    MountByType EtcFstab::get_mount_by( const string & device )
    {
	if ( boost::starts_with( device, "UUID=" ) ||
	     boost::starts_with( device, "/dev/disk/by-uuid/" ) )
	{
	    return MountByType::UUID;
	}

	if ( boost::starts_with( device, "LABEL=" ) ||
	     boost::starts_with( device, "/dev/disk/by-label/" ) )
	{
	    return MountByType::LABEL;
	}

	if ( boost::starts_with( device, "/dev/disk/by-id/" ) )
	    return MountByType::ID;

	if ( boost::starts_with( device, "/dev/disk/by-path/" ) )
	    return MountByType::PATH;

	return MountByType::DEVICE;
    }


    FstabEntry * EtcFstab::get_entry( int index ) const
    {
        ST_CHECK_INDEX( index, 0, get_entry_count() - 1 );

        CommentedConfigFile::Entry * entry =
            CommentedConfigFile::get_entry( index );

        ST_CHECK_PTR( entry );

        return dynamic_cast<FstabEntry *>( entry );
    }


    void EtcFstab::add( FstabEntry * entry )
    {
        int index = find_sort_index( entry );

        if ( index < 0 )
            append( entry );
        else
            insert( index, entry );
    }


    int EtcFstab::find_sort_index( FstabEntry * entry ) const
    {
        ST_CHECK_PTR( entry );

        string mount_point = entry->get_mount_point();

        for ( int i=0; i < get_entry_count(); ++i )
        {
            // Insert 'entry' before anything that is mounted into its mount tree

            if ( boost::starts_with( get_entry(i)->get_mount_point(), mount_point ) &&
                 entry != get_entry(i) )
            {
                return i;
            }
        }

        return -1;
    }


    int EtcFstab::next_mount_order_problem( int start_index ) const
    {
        for ( int i=start_index; i < get_entry_count(); ++i )
        {
            int index = find_sort_index( get_entry(i) );

            if ( index != -1 && index < i )
                return i;
        }

        return -1;
    }


    bool EtcFstab::check_mount_order() const
    {
        int index = next_mount_order_problem();

        return index == -1;
    }


    void EtcFstab::fix_mount_order()
    {
        int start_index = 0;

        while ( start_index < get_entry_count() )
        {
            int problem_index = next_mount_order_problem( start_index );

            if ( problem_index == -1 )
                return;

            // Take this entry out of the entries vector and put it back in at
            // the correct place.

            FstabEntry * entry = dynamic_cast<FstabEntry *>( take( problem_index ) );
            add( entry );

            // By definition, the entries vector is now fixed up to this index,
            // so continue fixing at the next index.

            start_index = problem_index + 1;

            // There is one pathological case, though:
            //
            // When two or more entries have the same mount point (which is
            // illegal, but somebody might write such an fstab manuallly),
            // there is no correct mount order; this algorithm would get into
            // an endless loop if we now checked the same index again. But by
            // just proceeding with the next one (and silently assuming that
            // the one we just changed is well and truly fixed), we can avoid
            // that endless loop.
        }
    }


    string EtcFstab::fstab_encode( const string & unencoded )
    {
	return boost::replace_all_copy( unencoded, " ", "\\040" );
    }


    string EtcFstab::fstab_decode( const string & encoded )
    {
	return boost::replace_all_copy( encoded, "\\040", " " );
    }


    void EtcFstab::log()
    {
        string_vec lines = format_lines();

        if ( ! get_filename().empty() )
            y2mil( get_filename() );

        for ( size_t i=0; i < lines.size(); ++i )
            y2mil( lines[i] );
    }


    void EtcFstab::log_diff()
    {
        string_vec lines = diff();

        if ( ! get_filename().empty() )
            y2mil( get_filename() << " diff: " );

        for ( size_t i=0; i < lines.size(); ++i )
            y2mil( lines[i] );
    }


    vector<string>
    EtcFstab::construct_device_aliases(const BlkDevice* blk_device, const BlkFilesystem* blk_filesystem)
    {
	vector<string> device_aliases = { blk_device->get_name() };

	for (const string& udev_path : blk_device->get_udev_paths())
	    device_aliases.push_back("/dev/disk/by-path/" + udev_path);

	for (const string& udev_id : blk_device->get_udev_ids())
	    device_aliases.push_back("/dev/disk/by-id/" + udev_id);

        if (!blk_filesystem->get_label().empty())
        {
            device_aliases.push_back("LABEL=" + blk_filesystem->get_label());
            device_aliases.push_back("/dev/disk/by-label/" + blk_filesystem->get_label());
        }

        if (!blk_filesystem->get_uuid().empty())
        {
            device_aliases.push_back("UUID=" + blk_filesystem->get_uuid());
            device_aliases.push_back("/dev/disk/by-uuid/" + blk_filesystem->get_uuid());
        }

	return device_aliases;
    }


}
