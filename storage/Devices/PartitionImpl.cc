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


#include "storage/Utils/AppUtil.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Devices/PartitionImpl.h"
#include "storage/Devices/PartitionTableImpl.h"
#include "storage/Devices/Msdos.h"
#include "storage/Devices/Gpt.h"
#include "storage/Devices/MsdosImpl.h"
#include "storage/Devices/DasdPt.h"
#include "storage/Devices/ImplicitPt.h"
#include "storage/Devices/DiskImpl.h"
#include "storage/Filesystems/FilesystemImpl.h"
#include "storage/Devicegraph.h"
#include "storage/SystemInfo/SystemInfo.h"
#include "storage/Storage.h"
#include "storage/FreeInfo.h"
#include "storage/Utils/XmlFile.h"
#include "storage/Prober.h"


namespace storage
{

    using namespace std;


    const char* DeviceTraits<Partition>::classname = "Partition";


    // strings must match what parted understands
    const vector<string> EnumTraits<PartitionType>::names({
	"primary", "extended", "logical"
    });


    Partition::Impl::Impl(const string& name, const Region& region, PartitionType type)
	: BlkDevice::Impl(name, region), type(type), id(default_id_for_type(type)), boot(false),
	  legacy_boot(false)
    {
    }


    Partition::Impl::Impl(const xmlNode* node)
	: BlkDevice::Impl(node), type(PartitionType::PRIMARY), id(ID_LINUX), boot(false),
	  legacy_boot(false)
    {
	string tmp;

	if (getChildValue(node, "type", tmp))
	    type = toValueWithFallback(tmp, PartitionType::PRIMARY);
	getChildValue(node, "id", id);
	getChildValue(node, "boot", boot);
	getChildValue(node, "legacy-boot", legacy_boot);
    }


    string
    Partition::Impl::get_pretty_classname() const
    {
	// TRANSLATORS: name of object
	return _("Partition").translated;
    }


    string
    Partition::Impl::get_sort_key() const
    {
	const Partitionable* partitionable = get_partitionable();

	return partitionable->get_impl().get_sort_key() + pad_front(to_string(get_number()), 3, '0');
    }


    void
    Partition::Impl::probe_pass_1a(Prober& prober)
    {
	BlkDevice::Impl::probe_pass_1a(prober);

	const Partitionable* partitionable = get_partitionable();

	const Parted& parted = prober.get_system_info().getParted(partitionable->get_name());
	Parted::Entry entry;
	if (!parted.get_entry(get_number(), entry))
	    throw;

	id = entry.id;
	boot = entry.boot;
	legacy_boot = entry.legacy_boot;
    }


    void
    Partition::Impl::save(xmlNode* node) const
    {
	BlkDevice::Impl::save(node);

	setChildValue(node, "type", toString(type));
	setChildValueIf(node, "id", id, id != 0);
	setChildValueIf(node, "boot", boot, boot);
	setChildValueIf(node, "legacy-boot", legacy_boot, legacy_boot);
    }


    bool
    Partition::Impl::is_usable_as_blk_device() const
    {
	return type == PartitionType::PRIMARY || type == PartitionType::LOGICAL;
    }


    vector<MountByType>
    Partition::Impl::possible_mount_bys() const
    {
	return get_partitionable()->get_impl().possible_mount_bys();
    }


    void
    Partition::Impl::check(const CheckCallbacks* check_callbacks) const
    {
	BlkDevice::Impl::check(check_callbacks);

	const Device* parent = get_single_parent_of_type<const Device>();

	switch (type)
	{
	    case PartitionType::PRIMARY:
		if (!is_partition_table(parent))
		    ST_THROW(Exception("parent of primary partition is not partition table"));
		break;

	    case PartitionType::EXTENDED:
		if (!is_partition_table(parent))
		    ST_THROW(Exception("parent of extended partition is not partition table"));
		break;

	    case PartitionType::LOGICAL:
		if (!is_partition(parent) || to_partition(parent)->get_type() != PartitionType::EXTENDED)
		    ST_THROW(Exception("parent of logical partition is not extended partition"));
		break;
	}
    }


    unsigned int
    Partition::Impl::get_number() const
    {
	return device_to_name_and_number(get_name()).second;
    }


    void
    Partition::Impl::set_number(unsigned int number)
    {
	std::pair<string, unsigned int> pair = device_to_name_and_number(get_name());

	set_name(name_and_number_to_device(pair.first, number));

	update_sysfs_name_and_path();
	update_udev_paths_and_ids();
    }


    void
    Partition::Impl::set_region(const Region& region)
    {
	const Region& partitionable_region = get_partitionable()->get_region();
	if (region.get_block_size() != partitionable_region.get_block_size())
	    ST_THROW(DifferentBlockSizes(region.get_block_size(), partitionable_region.get_block_size()));

	BlkDevice::Impl::set_region(region);
    }


    PartitionTable*
    Partition::Impl::get_partition_table()
    {
	Devicegraph::Impl::vertex_descriptor vertex = get_devicegraph()->get_impl().parent(get_vertex());

	if (type == PartitionType::LOGICAL)
	    vertex = get_devicegraph()->get_impl().parent(vertex);

	return to_partition_table(get_devicegraph()->get_impl()[vertex]);
    }


    const PartitionTable*
    Partition::Impl::get_partition_table() const
    {
	Devicegraph::Impl::vertex_descriptor vertex = get_devicegraph()->get_impl().parent(get_vertex());

	if (type == PartitionType::LOGICAL)
	    vertex = get_devicegraph()->get_impl().parent(vertex);

	return to_partition_table(get_devicegraph()->get_impl()[vertex]);
    }


    const Partitionable*
    Partition::Impl::get_partitionable() const
    {
	const PartitionTable* partition_table = get_partition_table();

	return partition_table->get_partitionable();
    }


    void
    Partition::Impl::add_create_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	bool nop = is_implicit_pt(get_partition_table());

	actions.push_back(new Action::Create(get_sid(), false, nop));

	if (default_id_for_type(type) != id)
	{
	    // For some partition ids it is fine to skip do_set_id() since
	    // do_create() already sets the partition id correctly.

	    static const vector<unsigned int> skip_ids = {
		ID_LINUX, ID_SWAP, ID_DOS16, ID_DOS32, ID_NTFS, ID_WINDOWS_BASIC_DATA
	    };

	    if (!contains(skip_ids, id))
		actions.push_back(new Action::SetPartitionId(get_sid()));
	}

	if (boot)
	    actions.push_back(new Action::SetBoot(get_sid()));

	if (legacy_boot)
	    actions.push_back(new Action::SetLegacyBoot(get_sid()));

	actiongraph.add_chain(actions);
    }


    void
    Partition::Impl::add_modify_actions(Actiongraph::Impl& actiongraph, const Device* lhs_base) const
    {
	BlkDevice::Impl::add_modify_actions(actiongraph, lhs_base);

	const Impl& lhs = dynamic_cast<const Impl&>(lhs_base->get_impl());

	if (get_type() != lhs.get_type())
	{
	    ST_THROW(Exception("cannot change partition type"));
	}

	if (get_region().get_start() != lhs.get_region().get_start())
	{
	    ST_THROW(Exception("cannot move partition"));
	}

	if (get_id() != lhs.get_id())
	{
	    Action::Base* action = new Action::SetPartitionId(get_sid());
	    actiongraph.add_vertex(action);
	}

	if (boot != lhs.boot)
	{
	    Action::Base* action = new Action::SetBoot(get_sid());
	    actiongraph.add_vertex(action);
	}

	if (legacy_boot != lhs.legacy_boot)
	{
	    Action::Base* action = new Action::SetLegacyBoot(get_sid());
	    actiongraph.add_vertex(action);
	}
    }


    void
    Partition::Impl::add_delete_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	bool nop = is_implicit_pt(get_partition_table());

	actions.push_back(new Action::Delete(get_sid(), false, nop));

	actiongraph.add_chain(actions);
    }


    bool
    Partition::Impl::equal(const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	if (!BlkDevice::Impl::equal(rhs))
	    return false;

	return type == rhs.type && id == rhs.id && boot == rhs.boot &&
	    legacy_boot == rhs.legacy_boot;
    }


    void
    Partition::Impl::log_diff(std::ostream& log, const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	BlkDevice::Impl::log_diff(log, rhs);

	storage::log_diff_enum(log, "type", type, rhs.type);
	storage::log_diff_hex(log, "id", id, rhs.id);
	storage::log_diff(log, "boot", boot, rhs.boot);
	storage::log_diff(log, "legacy-boot", legacy_boot, rhs.legacy_boot);
    }


    void
    Partition::Impl::print(std::ostream& out) const
    {
	BlkDevice::Impl::print(out);

	out << " type:" << toString(type)
	    << " id:" << id;

	if (boot)
	    out << " boot:" << boot;

	if (legacy_boot)
	    out << " legacy-boot:" << legacy_boot;
    }


    void
    Partition::Impl::process_udev_paths(vector<string>& udev_paths) const
    {
	const Partitionable* partitionable = get_partitionable();

	partitionable->get_impl().process_udev_paths(udev_paths);
    }


    void
    Partition::Impl::process_udev_ids(vector<string>& udev_ids) const
    {
	const Partitionable* partitionable = get_partitionable();

	partitionable->get_impl().process_udev_ids(udev_ids);
    }


    void
    Partition::Impl::set_type(PartitionType type)
    {
	const PartitionTable* partition_table = get_partition_table();

	if (!partition_table->get_impl().is_partition_type_supported(type))
	    ST_THROW(Exception(sformat("illegal partition type %s on %s", toString(type).c_str(),
				       toString(partition_table->get_type()).c_str())));

	Impl::type = type;
    }


    void
    Partition::Impl::set_id(unsigned int id)
    {
	const PartitionTable* partition_table = get_partition_table();

	if (!partition_table->get_impl().is_partition_id_supported(id))
	    ST_THROW(Exception(sformat("illegal partition id %d on %s", id,
				       toString(partition_table->get_type()).c_str())));

	Impl::id = id;
    }


    void
    Partition::Impl::set_boot(bool boot)
    {
	const PartitionTable* partition_table = get_partition_table();

	if (!partition_table->get_impl().is_partition_boot_flag_supported())
	    ST_THROW(Exception(sformat("set_boot not supported on %s",
				       toString(partition_table->get_type()).c_str())));

	if (boot && !Impl::boot)
	{
	    PartitionTable* partition_table = get_partition_table();

	    for (Partition* partition : partition_table->get_partitions())
		partition->get_impl().boot = false;
	}

	Impl::boot = boot;
    }


    void
    Partition::Impl::set_legacy_boot(bool legacy_boot)
    {
	const PartitionTable* partition_table = get_partition_table();

	if (!partition_table->get_impl().is_partition_legacy_boot_flag_supported())
	    ST_THROW(Exception(sformat("set_boot not supported on %s",
				       toString(partition_table->get_type()).c_str())));

	Impl::legacy_boot = legacy_boot;
    }


    void
    Partition::Impl::update_sysfs_name_and_path()
    {
	const Partitionable* partitionable = get_partitionable();

	// TODO different for device-mapper partitions

	if (!partitionable->get_sysfs_name().empty() && !partitionable->get_sysfs_path().empty())
	{
	    set_sysfs_name(partitionable->get_sysfs_name() + to_string(get_number()));
	    set_sysfs_path(partitionable->get_sysfs_path() + "/" + get_sysfs_name());
	}
	else
	{
	    set_sysfs_name("");
	    set_sysfs_path("");
	}
    }


    void
    Partition::Impl::update_udev_paths_and_ids()
    {
	const Partitionable* partitionable = get_partitionable();

	string postfix = "-part" + to_string(get_number());

	vector<string> udev_paths;
	for (const string& udev_path : partitionable->get_udev_paths())
	    udev_paths.push_back(udev_path + postfix);
	set_udev_paths(udev_paths);

	vector<string> udev_ids;
	for (const string& udev_id : partitionable->get_udev_ids())
	    udev_ids.push_back(udev_id + postfix);
	set_udev_ids(udev_ids);
    }


    ResizeInfo
    Partition::Impl::detect_resize_info() const
    {
	if (is_implicit_pt(get_partition_table()))
	{
	    return ResizeInfo(false);
	}

	if (type == PartitionType::EXTENDED)
	{
	    // TODO resize is technical possible but would be a new feature.

	    return ResizeInfo(false);
	}

	ResizeInfo resize_info = BlkDevice::Impl::detect_resize_info();

	// minimal size is one sector

	resize_info.combine_min(get_region().get_block_size());

	// maximal size is limited by used space behind partition

	Region surrounding = get_unused_surrounding_region();

	unsigned long long unused_sectors_behind_partition = get_region().get_length() +
	    surrounding.get_end() - get_region().get_end();

	resize_info.combine_max(surrounding.to_bytes(unused_sectors_behind_partition));

	resize_info.combine_block_size(get_region().get_block_size());

	return resize_info;
    }


    Region
    Partition::Impl::get_unused_surrounding_region() const
    {
	const PartitionTable* partition_table = get_partition_table();
	vector<const Partition*> partitions = partition_table->get_partitions();

	switch (get_type())
	{
	    case PartitionType::PRIMARY:
	    case PartitionType::EXTENDED:
	    {
		if (!partition_table->get_impl().has_usable_region())
		    return get_region();

		Region tmp = partition_table->get_impl().get_usable_region();

		unsigned long long start = tmp.get_start();
		unsigned long long end = tmp.get_end();

		for (const Partition* partition : partitions)
		{
		    if (partition->get_type() == PartitionType::PRIMARY ||
			partition->get_type() == PartitionType::EXTENDED)
		    {
			if (partition->get_region().get_end() < get_region().get_start())
			    start = max(start, partition->get_region().get_end() + 1);

			if (partition->get_region().get_start() > get_region().get_end())
			    end = min(end, partition->get_region().get_start() - 1);
		    }
		}

		return Region(start, end - start + 1, tmp.get_block_size());
	    }

	    case PartitionType::LOGICAL:
	    {
		Region tmp = partition_table->get_extended()->get_region();

		unsigned long long start = tmp.get_start();
		unsigned long long end = tmp.get_end();

		for (const Partition* partition : partitions)
		{
		    if (partition->get_type() == PartitionType::LOGICAL)
		    {
			if (partition->get_region().get_end() < get_region().get_start())
			    start = max(start, partition->get_region().get_end() + 1);

			if (partition->get_region().get_start() > get_region().get_end())
			    end = min(end, partition->get_region().get_start() - 1);
		    }
		}

		// Keep space for EBRs.

		start += Msdos::Impl::num_ebrs;

		return Region(start, end - start + 1, tmp.get_block_size());
	    }
	}

	ST_THROW(Exception("illegal partition type"));
    }


    Text
    Partition::Impl::do_create_text(Tense tense) const
    {
	Text text;

	if (is_implicit_pt(get_partition_table()))
	{
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/dasda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Create implicit partition %1$s (%2$s)"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/dasda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Creating implicit partition %1$s (%2$s)"));
	}
	else if (!is_msdos(get_partition_table()))
	{
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Create partition %1$s (%2$s)"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Creating partition %1$s (%2$s)"));
	}
	else
	{
	    switch (type)
	    {
		case PartitionType::PRIMARY:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Create primary partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Creating primary partition %1$s (%2$s)"));
		    break;

		case PartitionType::EXTENDED:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Create extended partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Creating extended partition %1$s (%2$s)"));
		    break;

		case PartitionType::LOGICAL:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Create logical partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Creating logical partition %1$s (%2$s)"));
		    break;
	    }
	}

	return sformat(text, get_name().c_str(), get_size_string().c_str());
    }


    void
    Partition::Impl::do_create()
    {
	const Partitionable* partitionable = get_partitionable();
	const PartitionTable* partition_table = get_partition_table();

	string cmd_line = PARTEDBIN " --script --wipesignatures " + quote(partitionable->get_name()) +
	    " unit s mkpart ";

	if (is_msdos(partition_table))
	    cmd_line += toString(get_type()) + " ";

	if (is_gpt(partition_table))
	    // pass empty string as partition name, funny syntax (see
	    // https://bugzilla.suse.com/show_bug.cgi?id=1023818)
	    cmd_line += "'\"\"' ";

	if (get_type() != PartitionType::EXTENDED)
	{
	    // Telling parted the filesystem type sets the correct partition
	    // id in some cases. Used in add_create_actions() to avoid
	    // redundant actions.

	    switch (get_id())
	    {
		case ID_SWAP:
		    cmd_line += "linux-swap ";
		    break;

		case ID_DOS16:
		    cmd_line += "fat16 ";
		    break;

		case ID_DOS32:
		    cmd_line += "fat32 ";
		    break;

		case ID_NTFS:
		case ID_WINDOWS_BASIC_DATA:
		    cmd_line += "ntfs ";
		    break;

		default:
		    cmd_line += "ext2 ";
		    break;
	    }
	}

	// See fix_dasd_sector_size() in class Parted.
	if (is_dasd_pt(partition_table) && get_region().get_block_size() == 4096)
	    cmd_line += to_string(get_region().get_start() * 8) + " " + to_string(get_region().get_end() * 8 + 7);
	else
	    cmd_line += to_string(get_region().get_start()) + " " + to_string(get_region().get_end());

	SystemCmd(UDEVADMBIN_SETTLE);

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    Partition::Impl::do_set_id_text(Tense tense) const
    {
	const PartitionTable* partition_table = get_partition_table();

	string tmp = id_to_string(get_id());

	if (partition_table->get_impl().are_partition_id_values_standardized())
	{
	    if (tmp.empty())
	    {
		Text text = tenser(tense,
				   // TRANSLATORS: displayed before action,
				   // %1$s is replaced by partition name (e.g. /dev/sda1),
				   // 0x%2$02X is replaced by partition id (e.g. 0x8E)
				   _("Set id of partition %1$s to 0x%2$02X"),
				   // TRANSLATORS: displayed during action,
				   // %1$s is replaced by partition name (e.g. /dev/sda1),
				   // 0x%2$02X is replaced by partition id (e.g. 0x8E)
				   _("Setting id of partition %1$s to 0x%2$02X"));
		return sformat(text, get_name().c_str(), get_id());
	    }
	    else
	    {
		Text text = tenser(tense,
				   // TRANSLATORS: displayed before action,
				   // %1$s is replaced by partition name (e.g. /dev/sda1),
				   // %2$s is replaced by partition id string (e.g. Linux LVM),
				   // 0x%3$02X is replaced by partition id (e.g. 0x8E)
				   _("Set id of partition %1$s to %2$s (0x%3$02X)"),
				   // TRANSLATORS: displayed during action,
				   // %1$s is replaced by partition name (e.g. /dev/sda1),
				   // %2$s is replaced by partition id string (e.g. Linux LVM),
				   // 0x%3$02X is replaced by partition id (e.g. 0x8E)
				   _("Setting id of partition %1$s to %2$s (0x%3$02X)"));
		return sformat(text, get_name().c_str(), tmp.c_str(), get_id());
	    }
	}
	else
	{
	    if (tmp.empty())
		ST_THROW(Exception(sformat("partition id %d of %s does not have text representation",
					   get_id(), get_name().c_str())));

	    Text text = tenser(tense,
			       // TRANSLATORS: displayed before action,
			       // %1$s is replaced by partition name (e.g. /dev/sda1),
			       // %2$s is replaced by partition id string (e.g. Linux LVM)
			       _("Set id of partition %1$s to %2$s"),
			       // TRANSLATORS: displayed during action,
			       // %1$s is replaced by partition name (e.g. /dev/sda1),
			       // %2$s is replaced by partition id string (e.g. Linux LVM)
			       _("Setting id of partition %1$s to %2$s"));
	    return sformat(text, get_name().c_str(), tmp.c_str());
	}
    }


    void
    Partition::Impl::do_set_id() const
    {
	const Partitionable* partitionable = get_partitionable();
	const PartitionTable* partition_table = get_partition_table();

	string cmd_line = PARTEDBIN " --script " + quote(partitionable->get_name()) + " set " +
	    to_string(get_number()) + " ";

	if (is_msdos(partition_table))
	{
	    // Note: The type option is not available in upstream parted.

	    cmd_line += "type " + to_string(get_id());
	}
	else
	{
	    switch (get_id())
	    {
		case ID_LINUX:
		    // this is tricky but parted has no clearer way
		    cmd_line += "lvm on set " + to_string(get_number()) + " lvm off";
		    break;

		case ID_SWAP:
		    cmd_line += "swap on";
		    break;

		case ID_LVM:
		    cmd_line += "lvm on";
		    break;

		case ID_RAID:
		    cmd_line += "raid on";
		    break;

		case ID_ESP:
		    cmd_line += "esp on";
		    break;

		case ID_BIOS_BOOT:
		    cmd_line += "bios_grub on";
		    break;

		case ID_PREP:
		    cmd_line += "prep on";
		    break;

		case ID_WINDOWS_BASIC_DATA:
		    cmd_line += "msftdata";
		    break;

		case ID_MICROSOFT_RESERVED:
		    cmd_line += "msftres";
		    break;

		case ID_DIAG:
		    cmd_line += "diag";
		    break;
	    }
	}

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    Partition::Impl::do_set_boot_text(Tense tense) const
    {
	Text text;

	if (is_boot())
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Set boot flag of partition %1$s"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Setting boot flag of partition %1$s"));
	else
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Clear boot flag of partition %1$s"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Clearing boot flag of partition %1$s"));

	return sformat(text, get_name().c_str());
    }


    void
    Partition::Impl::do_set_boot() const
    {
	const Partitionable* partitionable = get_partitionable();

	string cmd_line = PARTEDBIN " --script " + quote(partitionable->get_name()) + " set " +
	    to_string(get_number()) + " boot " + (is_boot() ? "on" : "off");

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    Partition::Impl::do_set_legacy_boot_text(Tense tense) const
    {
	Text text;

	if (is_legacy_boot())
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Set legacy boot flag of partition %1$s"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Setting legacy boot flag of partition %1$s"));
	else
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Clear legacy boot flag of partition %1$s"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1)
			  _("Clearing legacy boot flag of partition %1$s"));

	return sformat(text, get_name().c_str());
    }


    void
    Partition::Impl::do_set_legacy_boot() const
    {
	const Partitionable* partitionable = get_partitionable();

	string cmd_line = PARTEDBIN " --script " + quote(partitionable->get_name()) + " set " +
	    to_string(get_number()) + " legacy_boot " + (is_legacy_boot() ? "on" : "off");

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    Partition::Impl::do_delete_text(Tense tense) const
    {
	Text text;

	if (is_implicit_pt(get_partition_table()))
	{
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/dasda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Delete implicit partition %1$s (%2$s)"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/dasda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Deleting implicit partition %1$s (%2$s)"));
	}
	else if (!is_msdos(get_partition_table()))
	{
	    text = tenser(tense,
			  // TRANSLATORS: displayed before action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Delete partition %1$s (%2$s)"),
			  // TRANSLATORS: displayed during action,
			  // %1$s is replaced by partition name (e.g. /dev/sda1),
			  // %2$s is replaced by size (e.g. 2 GiB)
			  _("Deleting partition %1$s (%2$s)"));
	}
	else
	{
	    switch (type)
	    {
		case PartitionType::PRIMARY:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Delete primary partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Deleting primary partition %1$s (%2$s)"));
		    break;

		case PartitionType::EXTENDED:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Delete extended partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Deleting extended partition %1$s (%2$s)"));
		    break;

		case PartitionType::LOGICAL:
		    text = tenser(tense,
				  // TRANSLATORS: displayed before action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Delete logical partition %1$s (%2$s)"),
				  // TRANSLATORS: displayed during action,
				  // %1$s is replaced by partition name (e.g. /dev/sda1),
				  // %2$s is replaced by size (e.g. 2 GiB)
				  _("Deleting logical partition %1$s (%2$s)"));
		    break;
	    }
	}

	return sformat(text, get_name().c_str(), get_size_string().c_str());
    }


    void
    Partition::Impl::do_delete() const
    {
	do_delete_efi_boot_mgr();

	const Partitionable* partitionable = get_partitionable();

	string cmd_line = PARTEDBIN " --script " + quote(partitionable->get_name()) + " rm " +
	    to_string(get_number());

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    void
    Partition::Impl::do_delete_efi_boot_mgr() const
    {
	if (!is_gpt(get_partition_table()))
	    return;

	if (!get_devicegraph()->get_storage()->get_arch().is_efiboot())
	    return;

	const Partitionable* partitionable = get_partitionable();

	string cmd_line = EFIBOOTMGRBIN " --verbose --delete --disk " +
	    quote(partitionable->get_name()) + " --part " + to_string(get_number());

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    Partition::Impl::do_resize_text(ResizeMode resize_mode, const Device* lhs, const Device* rhs,
				    Tense tense) const
    {
	const Partition* partition_lhs = to_partition(lhs);
	const Partition* partition_rhs = to_partition(rhs);

	Text text;

	switch (resize_mode)
	{
	    case ResizeMode::SHRINK:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by partition name (e.g. /dev/sda1),
			      // %2$s is replaced by old size (e.g. 2 GiB),
			      // %3$s is replaced by new size (e.g. 1 GiB)
			      _("Shrink partition %1$s from %2$s to %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by partition name (e.g. /dev/sda1),
			      // %2$s is replaced by old size (e.g. 2 GiB),
			      // %3$s is replaced by new size (e.g. 1 GiB)
			      _("Shrinking partition %1$s from %2$s to %3$s"));
		break;

	    case ResizeMode::GROW:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by partition name (e.g. /dev/sda1),
			      // %2$s is replaced by old size (e.g. 1 GiB),
			      // %3$s is replaced by new size (e.g. 2 GiB)
			      _("Grow partition %1$s from %2$s to %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by partition name (e.g. /dev/sda1),
			      // %2$s is replaced by old size (e.g. 1 GiB),
			      // %3$s is replaced by new size (e.g. 2 GiB)
			      _("Growing partition %1$s from %2$s to %3$s"));
		break;

	    default:
		ST_THROW(LogicException("invalid value for resize_mode"));
	}

	return sformat(text, get_name().c_str(), partition_lhs->get_size_string().c_str(),
		       partition_rhs->get_size_string().c_str());
    }


    void
    Partition::Impl::do_resize(ResizeMode resize_mode, const Device* rhs) const
    {
	const Partition* partition_rhs = to_partition(rhs);
	const Partitionable* partitionable = get_partitionable();
	const PartitionTable* partition_table = get_partition_table();

	string cmd_line = PARTEDBIN " --script --ignore-busy " + quote(partitionable->get_name()) +
	    " unit s resizepart " + to_string(get_number()) + " ";

	// See fix_dasd_sector_size() in class Parted.
	if (is_dasd_pt(partition_table) && get_region().get_block_size() == 4096)
	    cmd_line += to_string(partition_rhs->get_region().get_end() * 8 + 7);
	else
	    cmd_line += to_string(partition_rhs->get_region().get_end());

	wait_for_devices({ get_non_impl() });

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    namespace Action
    {

	Text
	SetPartitionId::text(const CommitData& commit_data) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    return partition->get_impl().do_set_id_text(commit_data.tense);
	}


	void
	SetPartitionId::commit(CommitData& commit_data, const CommitOptions& commit_options) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    partition->get_impl().do_set_id();
	}


	Text
	SetBoot::text(const CommitData& commit_data) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    return partition->get_impl().do_set_boot_text(commit_data.tense);
	}


	void
	SetBoot::commit(CommitData& commit_data, const CommitOptions& commit_options) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    partition->get_impl().do_set_boot();
	}


	Text
	SetLegacyBoot::text(const CommitData& commit_data) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    return partition->get_impl().do_set_legacy_boot_text(commit_data.tense);
	}


	void
	SetLegacyBoot::commit(CommitData& commit_data, const CommitOptions& commit_options) const
	{
	    const Partition* partition = to_partition(get_device(commit_data.actiongraph, RHS));
	    partition->get_impl().do_set_legacy_boot();
	}

    }


    unsigned int
    Partition::Impl::default_id_for_type(PartitionType type)
    {
	return type == PartitionType::EXTENDED ? ID_EXTENDED : ID_LINUX;
    }


    string
    id_to_string(unsigned int id)
    {
	// For every id used on GPT or DASD (where
	// is_partition_id_value_standardized returns false) a text is required
	// since it is used in do_set_id_text().

	switch (id)
	{
	    case ID_SWAP: return "Linux Swap";
	    case ID_LINUX: return "Linux";
	    case ID_LVM: return "Linux LVM";
	    case ID_RAID: return "Linux RAID";
	    case ID_ESP: return "EFI System Partition";
	    case ID_BIOS_BOOT: return "BIOS Boot Partition";
	    case ID_PREP: return "PReP Boot Partition";
	    case ID_WINDOWS_BASIC_DATA: return "Windows Data Partition";
	    case ID_MICROSOFT_RESERVED: return "Microsoft Reserved Partition";
	    case ID_DIAG: return "Diagnostics Partition";
	}

	return "";
    }

}
