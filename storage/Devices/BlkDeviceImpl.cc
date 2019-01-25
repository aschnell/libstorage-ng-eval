/*
 * Copyright (c) [2014-2015] Novell, Inc.
 * Copyright (c) [2016-2018] SUSE LLC
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


#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "storage/Utils/XmlFile.h"
#include "storage/Utils/HumanString.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Devices/BlkDeviceImpl.h"
#include "storage/Devices/LuksImpl.h"
#include "storage/Devices/BackedBcacheImpl.h"
#include "storage/Devices/BcacheCsetImpl.h"
#include "storage/Devices/LvmPv.h"
#include "storage/Holders/FilesystemUser.h"
#include "storage/Filesystems/BlkFilesystemImpl.h"
#include "storage/Filesystems/Ext2.h"
#include "storage/Filesystems/Ext3.h"
#include "storage/Filesystems/Ext4.h"
#include "storage/Filesystems/Btrfs.h"
#include "storage/Filesystems/Reiserfs.h"
#include "storage/Filesystems/Xfs.h"
#include "storage/Filesystems/Jfs.h"
#include "storage/Filesystems/F2fs.h"
#include "storage/Filesystems/Swap.h"
#include "storage/Filesystems/Ntfs.h"
#include "storage/Filesystems/Vfat.h"
#include "storage/Filesystems/Exfat.h"
#include "storage/Filesystems/Iso9660.h"
#include "storage/Filesystems/Udf.h"
#include "storage/SystemInfo/SystemInfo.h"
#include "storage/FreeInfo.h"
#include "storage/Prober.h"
#include "storage/Utils/Format.h"


namespace storage
{


    const char* DeviceTraits<BlkDevice>::classname = "BlkDevice";


    BlkDevice::Impl::Impl(const string& name)
	: Impl(name, Region(0, 0, 512))
    {
    }


    BlkDevice::Impl::Impl(const string& name, const Region& region)
	: Device::Impl(), name(name), active(true), region(region), udev_paths(), udev_ids(),
	  dm_table_name()
    {
	if (!is_valid_name(name))
	    ST_THROW(Exception("invalid BlkDevice name"));
    }


    BlkDevice::Impl::Impl(const xmlNode* node)
	: Device::Impl(node), name(), active(true), region(0, 0, 512), udev_paths(), udev_ids(),
	  dm_table_name()
    {
	if (!getChildValue(node, "name", name))
	    ST_THROW(Exception("no name"));

	getChildValue(node, "sysfs-name", sysfs_name);
	getChildValue(node, "sysfs-path", sysfs_path);

	getChildValue(node, "active", active);

	getChildValue(node, "region", region);

	getChildValue(node, "udev-path", udev_paths);
	getChildValue(node, "udev-id", udev_ids);

	getChildValue(node, "dm-table-name", dm_table_name);
    }


    void
    BlkDevice::Impl::probe_pass_1a(Prober& prober)
    {
	Device::Impl::probe_pass_1a(prober);

	if (active)
	{
	    const CmdUdevadmInfo& cmdudevadminfo = prober.get_system_info().getCmdUdevadmInfo(name);

	    sysfs_name = cmdudevadminfo.get_name();
	    sysfs_path = cmdudevadminfo.get_path();

	    // region is probed in subclasses

	    if (!cmdudevadminfo.get_by_path_links().empty())
	    {
		udev_paths = cmdudevadminfo.get_by_path_links();
		process_udev_paths(udev_paths);
	    }

	    if (!cmdudevadminfo.get_by_id_links().empty())
	    {
		udev_ids = cmdudevadminfo.get_by_id_links();
		process_udev_ids(udev_ids);
	    }
	}
    }


    void
    BlkDevice::Impl::probe_size(Prober& prober)
    {
	const File size_file = prober.get_system_info().getFile(SYSFS_DIR + get_sysfs_path() + "/size");
	const File logical_block_size_file = prober.get_system_info().getFile(SYSFS_DIR + get_sysfs_path() +
                                                                              "/queue/logical_block_size");

	// size is always in 512 byte blocks
	unsigned long long a = size_file.get<unsigned long long>();
	unsigned long long b = logical_block_size_file.get<unsigned long long>();
	unsigned long long c = a * 512 / b;
	set_region(Region(0, c, b));
    }


    void
    BlkDevice::Impl::save(xmlNode* node) const
    {
	Device::Impl::save(node);

	setChildValue(node, "name", name);

	setChildValueIf(node, "sysfs-name", sysfs_name, !sysfs_name.empty());
	setChildValueIf(node, "sysfs-path", sysfs_path, !sysfs_path.empty());

	setChildValueIf(node, "active", active, !active);

	setChildValue(node, "region", region);

	setChildValueIf(node, "udev-path", udev_paths, !udev_paths.empty());
	setChildValueIf(node, "udev-id", udev_ids, !udev_ids.empty());

	setChildValueIf(node, "dm-table-name", dm_table_name, !dm_table_name.empty());
    }


    void
    BlkDevice::Impl::check(const CheckCallbacks* check_callbacks) const
    {
	Device::Impl::check(check_callbacks);

	if (region.get_block_size() == 0)
	    ST_THROW(Exception(sformat("block size is zero for %s", get_name())));

	if (!is_valid_name(get_name()))
	    ST_THROW(Exception("BlkDevice has invalid name"));
    }


    void
    BlkDevice::Impl::set_name(const string& name)
    {
	Impl::name = name;
    }


    void
    BlkDevice::Impl::set_region(const Region& region)
    {
	Impl::region = region;

	for (Device* child : get_non_impl()->get_children())
	    child->get_impl().parent_has_new_region(get_non_impl());
    }


    unsigned long long
    BlkDevice::Impl::get_size() const
    {
	return region.to_bytes(region.get_length());
    }


    void
    BlkDevice::Impl::set_size(unsigned long long size)
    {
	// Direct to virtual set_region so that derived classes can perform
	// checks and that children can be informed.

	set_region(Region(region.get_start(), region.to_blocks(size), region.get_block_size()));
    }


    Text
    BlkDevice::Impl::get_size_text() const
    {
	// TODO but maybe ByteCount

	return Text(byte_to_humanstring(get_size(), true, 2, false),
		    byte_to_humanstring(get_size(), false, 2, false));
    }


    vector<MountByType>
    BlkDevice::Impl::possible_mount_bys() const
    {
	vector<MountByType> ret = { MountByType::DEVICE };

	if (!udev_paths.empty())
	    ret.push_back(MountByType::PATH);

	if (!udev_ids.empty())
	    ret.push_back(MountByType::ID);

	return ret;
    }


    string
    BlkDevice::Impl::get_mount_by_name(MountByType mount_by_type) const
    {
	string ret;

	switch (mount_by_type)
	{
	    case MountByType::UUID:
		y2err("no uuid possible, using fallback");
		break;

	    case MountByType::LABEL:
		y2err("no label possible, using fallback");
		break;

	    case MountByType::ID:
		if (!get_udev_ids().empty())
		    ret = DEV_DISK_BY_ID_DIR "/" + get_udev_ids().front();
		else
		    y2err("no udev-id defined, using fallback");
		break;

	    case MountByType::PATH:
		if (!get_udev_paths().empty())
		    ret = DEV_DISK_BY_PATH_DIR "/" + get_udev_paths().front();
		else
		    y2err("no udev-path defined, using fallback");
		break;

	    case MountByType::DEVICE:
		break;
	}

	if (ret.empty())
	{
	    ret = get_name();
	}

	return ret;
    }


    ResizeInfo
    BlkDevice::Impl::detect_resize_info() const
    {
	ResizeInfo resize_info(true, 0);

	for (const Device* child : get_non_impl()->get_children())
	    resize_info.combine(child->detect_resize_info());

	return resize_info;
    }


    bool
    BlkDevice::Impl::exists_by_any_name(const Devicegraph* devicegraph, const string& name,
					SystemInfo& system_info)
    {
	if (!devicegraph->get_impl().is_system() && !devicegraph->get_impl().is_probed())
	    ST_THROW(Exception("function called on wrong devicegraph"));

	for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	{
	    const BlkDevice* blk_device = dynamic_cast<const BlkDevice*>(devicegraph->get_impl()[vertex]);
	    if (blk_device)
	    {
		if (blk_device->get_name() == name)
		    return true;
	    }
	}

	try
	{
	    string sysfs_path = system_info.getCmdUdevadmInfo(name).get_path();

	    for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	    {
		const BlkDevice* blk_device = dynamic_cast<const BlkDevice*>(devicegraph->get_impl()[vertex]);
		if (blk_device && blk_device->get_impl().active)
		{
		    if (blk_device->get_sysfs_path() == sysfs_path)
			return true;
		}
	    }
	}
	catch (const Exception& exception)
	{
	    ST_CAUGHT(exception);
	}

	return false;
    }


    BlkDevice*
    BlkDevice::Impl::find_by_any_name(Devicegraph* devicegraph, const string& name,
				      SystemInfo& system_info)
    {
	if (!devicegraph->get_impl().is_system() && !devicegraph->get_impl().is_probed())
	    ST_THROW(Exception("function called on wrong devicegraph"));

	for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	{
	    BlkDevice* blk_device = dynamic_cast<BlkDevice*>(devicegraph->get_impl()[vertex]);
	    if (blk_device)
	    {
		if (blk_device->get_name() == name)
		    return blk_device;
	    }
	}

	try
	{
	    string sysfs_path = system_info.getCmdUdevadmInfo(name).get_path();

	    for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	    {
		BlkDevice* blk_device = dynamic_cast<BlkDevice*>(devicegraph->get_impl()[vertex]);
		if (blk_device && blk_device->get_impl().active)
		{
		    if (blk_device->get_sysfs_path() == sysfs_path)
			return blk_device;
		}
	    }
	}
	catch (const Exception& exception)
	{
	    ST_CAUGHT(exception);
	}

	ST_THROW(DeviceNotFoundByName(name));
    }


    const BlkDevice*
    BlkDevice::Impl::find_by_any_name(const Devicegraph* devicegraph, const string& name,
				      SystemInfo& system_info)
    {
	if (!devicegraph->get_impl().is_system() && !devicegraph->get_impl().is_probed())
	    ST_THROW(Exception("function called on wrong devicegraph"));

	for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	{
	    const BlkDevice* blk_device = dynamic_cast<const BlkDevice*>(devicegraph->get_impl()[vertex]);
	    if (blk_device)
	    {
		if (blk_device->get_name() == name)
		    return blk_device;
	    }
	}

	try
	{
	    string sysfs_path = system_info.getCmdUdevadmInfo(name).get_path();

	    for (Devicegraph::Impl::vertex_descriptor vertex : devicegraph->get_impl().vertices())
	    {
		const BlkDevice* blk_device = dynamic_cast<const BlkDevice*>(devicegraph->get_impl()[vertex]);
		if (blk_device && blk_device->get_impl().active)
		{
		    if (blk_device->get_sysfs_path() == sysfs_path)
			return blk_device;
		}
	    }
	}
	catch (const Exception& exception)
	{
	    ST_CAUGHT(exception);
	}

	ST_THROW(DeviceNotFoundByName(name));
    }


    namespace
    {

	vector<const Device*>
	devices_to_resize(const Device* device)
	{
	    vector<const Device*> ret;

	    for (const Device* child : device->get_children())
	    {
		if (is_blk_device(child) && !is_md(child))
		{
		    ret.push_back(child);

		    vector<const Device*> tmp = devices_to_resize(child);
		    ret.insert(ret.end(), tmp.begin(), tmp.end());
		}

		if (is_lvm_pv(child))
		{
		    ret.push_back(child);
		}

		if (is_filesystem(child))
		{
		    ret.push_back(child);
		}
	    }

	    return ret;
	}

    }


    void
    BlkDevice::Impl::add_modify_actions(Actiongraph::Impl& actiongraph, const Device* lhs_base) const
    {
	Device::Impl::add_modify_actions(actiongraph, lhs_base);

	const Impl& lhs = dynamic_cast<const Impl&>(lhs_base->get_impl());

	// The lowest underlying blk device handles the resize, so Partitions
	// and LvmLvs but not Luks.

	if (lhs.get_size() != get_size() && (is_partition(get_non_impl()) || is_lvm_lv(get_non_impl())))
	{
	    ResizeMode resize_mode = get_size() < lhs.get_size() ? ResizeMode::SHRINK :
		ResizeMode::GROW;

	    vector<const Device*> devices_to_resize_lhs = devices_to_resize(lhs.get_non_impl());
	    vector<const Device*> devices_to_resize_rhs = devices_to_resize(get_non_impl());

	    const BlkFilesystem* blk_filesystem_lhs = nullptr;
	    const BlkFilesystem* blk_filesystem_rhs = nullptr;

	    for (const Device* device_to_resize : devices_to_resize_lhs)
	    {
		if (is_blk_filesystem(device_to_resize))
		    blk_filesystem_lhs = to_blk_filesystem(device_to_resize);
	    }

	    for (const Device* device_to_resize : devices_to_resize_rhs)
	    {
		if (is_blk_filesystem(device_to_resize))
		    blk_filesystem_rhs = to_blk_filesystem(device_to_resize);
	    }

	    // Only tmp unmounts are inserted in the actiongraph. tmp mounts
	    // are simply handled in the do_resize() functions.

	    bool need_tmp_unmount = false;

	    if (blk_filesystem_rhs)
	    {
		if (!blk_filesystem_rhs->get_impl().supports_mounted_resize(resize_mode))
		    need_tmp_unmount = true;
	    }

	    // Only insert mount and resize actions if the devices exist in
	    // LHS and RHS.

	    vector<Action::Base*> actions;

	    if (need_tmp_unmount && blk_filesystem_lhs)
	    {
		if (blk_filesystem_lhs->exists_in_devicegraph(actiongraph.get_devicegraph(RHS)))
		    blk_filesystem_lhs->get_impl().insert_unmount_action(actions);
	    }

	    if (resize_mode == ResizeMode::SHRINK)
	    {
		for (const Device* device_to_resize : boost::adaptors::reverse(devices_to_resize_lhs))
		    if (device_to_resize->exists_in_devicegraph(actiongraph.get_devicegraph(RHS)))
			actions.push_back(new Action::Resize(device_to_resize->get_sid(), resize_mode));
	    }

	    actions.push_back(new Action::Resize(get_sid(), resize_mode));

	    if (resize_mode == ResizeMode::GROW)
	    {
		for (const Device* device_to_resize : devices_to_resize_rhs)
		    if (device_to_resize->exists_in_devicegraph(actiongraph.get_devicegraph(LHS)))
			actions.push_back(new Action::Resize(device_to_resize->get_sid(), resize_mode));
	    }

	    if (need_tmp_unmount && blk_filesystem_rhs)
	    {
		if (blk_filesystem_rhs->exists_in_devicegraph(actiongraph.get_devicegraph(LHS)))
		    blk_filesystem_rhs->get_impl().insert_mount_action(actions);
	    }

	    actiongraph.add_chain(actions);
	}

	if (!lhs.is_active() && is_active())
	{
	    Action::Base* action = new Action::Activate(get_sid());
	    actiongraph.add_vertex(action);
	}
	else if (lhs.is_active() && !is_active())
	{
	    Action::Base* action = new Action::Deactivate(get_sid());
	    actiongraph.add_vertex(action);
	}
    }


    bool
    BlkDevice::Impl::equal(const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	if (!Device::Impl::equal(rhs))
	    return false;

	return name == rhs.name && sysfs_name == rhs.sysfs_name && sysfs_path == rhs.sysfs_path &&
	    region == rhs.region && active == rhs.active && udev_paths == rhs.udev_paths &&
	    udev_ids == rhs.udev_ids && dm_table_name == rhs.dm_table_name;
    }


    void
    BlkDevice::Impl::log_diff(std::ostream& log, const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	Device::Impl::log_diff(log, rhs);

	storage::log_diff(log, "name", name, rhs.name);

	storage::log_diff(log, "sysfs-name", sysfs_name, rhs.sysfs_name);
	storage::log_diff(log, "sysfs-path", sysfs_path, rhs.sysfs_path);

	storage::log_diff(log, "active", active, rhs.active);

	storage::log_diff(log, "region", region, rhs.region);

	storage::log_diff(log, "udev-paths", udev_paths, rhs.udev_paths);
	storage::log_diff(log, "udev-ids", udev_ids, rhs.udev_ids);

	storage::log_diff(log, "dm-table-name", dm_table_name, rhs.dm_table_name);
    }


    void
    BlkDevice::Impl::print(std::ostream& out) const
    {
	Device::Impl::print(out);

	out << " name:" << get_name();

	if (!sysfs_name.empty())
	    out << " sysfs-name:" << sysfs_name;

	if (!sysfs_path.empty())
	    out << " sysfs-path:" << sysfs_path;

	if (!active)
	    out << " active:" << active;

	out << " region:" << get_region();

	if (!udev_paths.empty())
	    out << " udev-paths:" << udev_paths;

	if (!udev_ids.empty())
	    out << " udev-ids:" << udev_ids;

	if (!dm_table_name.empty())
	    out << " dm-table-name:" << dm_table_name;
    }


    typedef std::function<BlkFilesystem* (Devicegraph* devicegraph)> blk_filesystem_create_fnc;

    const map<FsType, blk_filesystem_create_fnc> blk_filesystem_create_registry = {
	{ FsType::BTRFS, &Btrfs::create },
	{ FsType::EXT2, &Ext2::create },
	{ FsType::EXT3, &Ext3::create },
	{ FsType::EXT4, &Ext4::create },
	{ FsType::ISO9660, &Iso9660::create },
	{ FsType::NTFS, &Ntfs::create },
	{ FsType::REISERFS, &Reiserfs::create },
	{ FsType::SWAP, &Swap::create },
	{ FsType::UDF, &Udf::create },
	{ FsType::VFAT, &Vfat::create },
	{ FsType::EXFAT, &Exfat::create },
	{ FsType::XFS, &Xfs::create },
	{ FsType::JFS, &Jfs::create },
	{ FsType::F2FS, &F2fs::create }
    };


    BlkFilesystem*
    BlkDevice::Impl::create_blk_filesystem(FsType fs_type)
    {
	if (num_children() != 0)
	    ST_THROW(WrongNumberOfChildren(num_children(), 0));

	map<FsType, blk_filesystem_create_fnc>::const_iterator it = blk_filesystem_create_registry.find(fs_type);
	if (it == blk_filesystem_create_registry.end())
	{
	    if (fs_type == FsType::NFS || fs_type == FsType::NFS4)
		ST_THROW(UnsupportedException("cannot create Nfs on BlkDevice"));

	    ST_THROW(UnsupportedException("unsupported filesystem type " + toString(fs_type)));
	}

	Devicegraph* devicegraph = get_devicegraph();

	BlkFilesystem* blk_filesystem = it->second(devicegraph);

	FilesystemUser::create(devicegraph, get_non_impl(), blk_filesystem);

	return blk_filesystem;
    }


    bool
    BlkDevice::Impl::has_blk_filesystem() const
    {
	return has_single_child_of_type<const BlkFilesystem>();
    }


    BlkFilesystem*
    BlkDevice::Impl::get_blk_filesystem()
    {
	return get_single_child_of_type<BlkFilesystem>();
    }


    const BlkFilesystem*
    BlkDevice::Impl::get_blk_filesystem() const
    {
	return get_single_child_of_type<const BlkFilesystem>();
    }


    Encryption*
    BlkDevice::Impl::create_encryption(const string& dm_name)
    {
	Devicegraph* devicegraph = get_devicegraph();

	vector<Devicegraph::Impl::edge_descriptor> out_edges =
	    devicegraph->get_impl().out_edges(get_vertex());

	Encryption* encryption = Luks::create(devicegraph, dm_name);
	Devicegraph::Impl::vertex_descriptor encryption_vertex = encryption->get_impl().get_vertex();

	User::create(devicegraph, get_non_impl(), encryption);

	for (Devicegraph::Impl::edge_descriptor out_edge : out_edges)
	{
	    devicegraph->get_impl().set_source(out_edge, encryption_vertex);
	}

	encryption->set_default_mount_by();

	// TODO maybe add parent_added() next to parent_has_new_region() for this?
	encryption->get_impl().parent_has_new_region(get_non_impl());

	return encryption;
    }


    void
    BlkDevice::Impl::remove_encryption()
    {
	Devicegraph* devicegraph = get_devicegraph();

	Encryption* encryption = get_encryption();
	Devicegraph::Impl::vertex_descriptor encryption_vertex = encryption->get_impl().get_vertex();

	vector<Devicegraph::Impl::edge_descriptor> out_edges =
	    devicegraph->get_impl().out_edges(encryption_vertex);

	for (Devicegraph::Impl::edge_descriptor out_edge : out_edges)
	{
	    devicegraph->get_impl().set_source(out_edge, get_vertex());
	}

	devicegraph->get_impl().remove_vertex(encryption_vertex);
    }


    bool
    BlkDevice::Impl::has_encryption() const
    {
	return has_single_child_of_type<const Encryption>();
    }


    Encryption*
    BlkDevice::Impl::get_encryption()
    {
	return get_single_child_of_type<Encryption>();
    }


    const Encryption*
    BlkDevice::Impl::get_encryption() const
    {
	return get_single_child_of_type<const Encryption>();
    }


    BackedBcache*
    BlkDevice::Impl::create_bcache(const string& name)
    {
	Devicegraph* devicegraph = get_devicegraph();

	// TODO reuse code with create_encryption

	vector<Devicegraph::Impl::edge_descriptor> out_edges =
	    devicegraph->get_impl().out_edges(get_vertex());

	BackedBcache* bcache = BackedBcache::create(devicegraph, name);
	Devicegraph::Impl::vertex_descriptor bcache_vertex = bcache->get_impl().get_vertex();

	User::create(devicegraph, get_non_impl(), bcache);

	for (Devicegraph::Impl::edge_descriptor out_edge : out_edges)
	{
	    devicegraph->get_impl().set_source(out_edge, bcache_vertex);
	}

	bcache->get_impl().parent_has_new_region(get_non_impl());

	return bcache;
    }


    BcacheCset*
    BlkDevice::Impl::create_bcache_cset()
    {
	if (num_children() != 0)
	    ST_THROW(WrongNumberOfChildren(num_children(), 0));

	Devicegraph* devicegraph = get_devicegraph();

	BcacheCset* bcache_cset = BcacheCset::create(devicegraph);

	User::create(devicegraph, get_non_impl(), bcache_cset);

	return bcache_cset;
    }


    void
    BlkDevice::Impl::wipe_device() const
    {
	string cmd_line = WIPEFSBIN " --all " + quote(get_name());

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    bool
    BlkDevice::Impl::is_valid_name(const string& name)
    {
	return boost::starts_with(name, DEV_DIR "/");
    }


    void
    wait_for_devices(const vector<const BlkDevice*>& blk_devices)
    {
	SystemCmd(UDEVADMBIN_SETTLE);

	for (const BlkDevice* blk_device : blk_devices)
	{
	    string name = blk_device->get_name();

	    bool exists = access(name.c_str(), R_OK) == 0;
	    y2mil("name:" << name << " exists:" << exists);

	    if (!exists)
	    {
		for (int count = 0; count < 500; ++count)
		{
		    usleep(10000);
		    exists = access(name.c_str(), R_OK) == 0;
		    if (exists)
			break;
		}
		y2mil("name:" << name << " exists:" << exists);
	    }

	    if (!exists)
		ST_THROW(Exception("wait_for_devices failed " + name));
	}
    }

}
