

#include <ctype.h>
#include <iostream>
#include <boost/regex.hpp>

#include "storage/Devices/MdImpl.h"
#include "storage/Holders/MdUser.h"
#include "storage/Devicegraph.h"
#include "storage/Action.h"
#include "storage/Storage.h"
#include "storage/Environment.h"
#include "storage/SystemInfo/SystemInfo.h"
#include "storage/Utils/Exception.h"
#include "storage/Utils/Enum.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Utils/StorageTypes.h"
#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Utils/XmlFile.h"
#include "storage/Utils/HumanString.h"


namespace storage
{

    using namespace std;


    const char* DeviceTraits<Md>::classname = "Md";


    // strings must match /proc/mdstat
    const vector<string> EnumTraits<MdLevel>::names({
	"unknown", "RAID0", "RAID1", "RAID5", "RAID6", "RAID10"
    });


    // strings must match "mdadm --parity" option
    const vector<string> EnumTraits<MdParity>::names({
	"default", "left-asymmetric", "left-symmetric", "right-asymmetric",
	"right-symmetric", "parity-first", "parity-last", "left-asymmetric-6",
	"left-symmetric-6", "right-asymmetric-6", "right-symmetric-6",
	"parity-first-6", "n2", "o2", "f2", "n3", "o3", "f3"
    });


    Md::Impl::Impl(const string& name)
	: Partitionable::Impl(name), md_level(RAID0), md_parity(DEFAULT), chunk_size(0)
    {
	if (!is_valid_name(name))
	    ST_THROW(Exception("invalid Md name"));

	string::size_type pos = string(DEVDIR).size() + 1;
	set_sysfs_name(name.substr(pos));
	set_sysfs_path("/devices/virtual/block/" + name.substr(pos));
    }


    Md::Impl::Impl(const xmlNode* node)
	: Partitionable::Impl(node), md_level(RAID0), md_parity(DEFAULT), chunk_size(0)
    {
	string tmp;

	if (getChildValue(node, "md-level", tmp))
	    md_level = toValueWithFallback(tmp, RAID0);

	if (getChildValue(node, "md-parity", tmp))
	    md_parity = toValueWithFallback(tmp, DEFAULT);

	getChildValue(node, "chunk-size", chunk_size);
    }


    void
    Md::Impl::set_md_level(MdLevel md_level)
    {
	Impl::md_level = md_level;

	calculate_region_and_topology();
    }


    void
    Md::Impl::set_chunk_size(unsigned long chunk_size)
    {
	Impl::chunk_size = chunk_size;

	calculate_region_and_topology();
    }


    unsigned long
    Md::Impl::get_default_chunk_size() const
    {
	return 512 * KiB;
    }


    bool
    Md::Impl::is_valid_name(const string& name)
    {
	static boost::regex name_regex(DEVDIR "/md[0-9]+", boost::regex_constants::extended);

	return boost::regex_match(name, name_regex);
    }


    vector<string>
    Md::Impl::probe_mds(SystemInfo& systeminfo)
    {
	vector<string> ret;

	for (const string& short_name : systeminfo.getDir(SYSFSDIR "/block"))
	{
	    string name = DEVDIR "/" + short_name;

	    if (!is_valid_name(name))
		continue;

	    ret.push_back(name);
	}

	return ret;
    }


    void
    Md::Impl::probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo)
    {
	Partitionable::Impl::probe_pass_1(probed, systeminfo);

	string tmp = get_name().substr(strlen(DEVDIR "/"));

	ProcMdstat::Entry entry;
	if (!systeminfo.getProcMdstat().getEntry(tmp, entry))
	{
	    // TODO
	    throw;
	}

	md_level = entry.md_level;
	md_parity = entry.md_parity;

	chunk_size = entry.chunk_size;
    }


    void
    Md::Impl::probe_pass_2(Devicegraph* probed, SystemInfo& systeminfo)
    {
	string tmp = get_name().substr(strlen(DEVDIR "/"));

	ProcMdstat::Entry entry;
	if (!systeminfo.getProcMdstat().getEntry(tmp, entry))
	{
	    // TODO
	    throw;
	}

	for (const ProcMdstat::Device& device : entry.devices)
	{
	    BlkDevice* blk_device = BlkDevice::find(probed, device.name);
	    MdUser* md_user = MdUser::create(probed, blk_device, get_device());
	    md_user->set_spare(device.spare);
	    md_user->set_faulty(device.faulty);
	}
    }


    void
    Md::Impl::add_create_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	actions.push_back(new Action::Create(get_sid()));
	actions.push_back(new Action::AddEtcMdadm(get_sid()));

	actiongraph.add_chain(actions);
    }


    void
    Md::Impl::add_delete_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	actions.push_back(new Action::RemoveEtcMdadm(get_sid()));
	actions.push_back(new Action::Delete(get_sid()));

	actiongraph.add_chain(actions);
    }


    void
    Md::Impl::save(xmlNode* node) const
    {
	Partitionable::Impl::save(node);

	setChildValue(node, "md-level", toString(md_level));
	setChildValueIf(node, "md-parity", toString(md_parity), md_parity != DEFAULT);

	setChildValueIf(node, "chunk-size", chunk_size, chunk_size != 0);
    }


    MdUser*
    Md::Impl::add_device(BlkDevice* blk_device)
    {
	if (blk_device->num_children() != 0)
	    ST_THROW(WrongNumberOfChildren(blk_device->num_children(), 0));

	// TODO set partition id?

	MdUser* md_user = MdUser::create(get_devicegraph(), blk_device, get_device());

	calculate_region_and_topology();

	return md_user;
    }


    void
    Md::Impl::remove_device(BlkDevice* blk_device)
    {
	MdUser* md_user = to_md_user(get_devicegraph()->find_holder(blk_device->get_sid(), get_sid()));

	get_devicegraph()->remove_holder(md_user);

	calculate_region_and_topology();
    }


    vector<BlkDevice*>
    Md::Impl::get_devices()
    {
	Devicegraph::Impl& devicegraph = get_devicegraph()->get_impl();
	Devicegraph::Impl::vertex_descriptor vertex = get_vertex();

	// TODO sorting

	return devicegraph.filter_devices_of_type<BlkDevice>(devicegraph.parents(vertex));
    }


    vector<const BlkDevice*>
    Md::Impl::get_devices() const
    {
	const Devicegraph::Impl& devicegraph = get_devicegraph()->get_impl();
	Devicegraph::Impl::vertex_descriptor vertex = get_vertex();

	// TODO sorting

	return devicegraph.filter_devices_of_type<const BlkDevice>(devicegraph.parents(vertex));
    }


    unsigned int
    Md::Impl::get_number() const
    {
	string::size_type pos = get_name().find_last_not_of("0123456789");
	if (pos == string::npos || pos == get_name().size() - 1)
	    ST_THROW(Exception("md name has no number"));

	return atoi(get_name().substr(pos + 1).c_str());
    }


    bool
    Md::Impl::equal(const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	if (!Partitionable::Impl::equal(rhs))
	    return false;

	return md_level == rhs.md_level && md_parity == rhs.md_parity &&
	    chunk_size == rhs.chunk_size;
    }


    void
    Md::Impl::log_diff(std::ostream& log, const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	Partitionable::Impl::log_diff(log, rhs);

	storage::log_diff_enum(log, "md-level", md_level, rhs.md_level);
	storage::log_diff_enum(log, "md-parity", md_parity, rhs.md_parity);

	storage::log_diff(log, "chunk-size", chunk_size, rhs.chunk_size);
    }


    void
    Md::Impl::print(std::ostream& out) const
    {
	Partitionable::Impl::print(out);

	out << " md-level:" << toString(get_md_level());
	out << " md-parity:" << toString(get_md_parity());

	out << " chunk-size:" << get_chunk_size();
    }


    void
    Md::Impl::process_udev_ids(vector<string>& udev_ids) const
    {
	partition(udev_ids.begin(), udev_ids.end(), string_starts_with("md-uuid-"));
    }


    void
    Md::Impl::calculate_region_and_topology()
    {
	vector<BlkDevice*> devices = get_devices();

	long real_chunk_size = chunk_size;

	if (real_chunk_size == 0)
	    real_chunk_size = get_default_chunk_size();

	// mdadm uses a chunk size of 64 KiB just in case the RAID1 is ever reshaped to RAID5.
	if (md_level == RAID1)
	    real_chunk_size = 64 * KiB;

	int number = 0;
	unsigned long long sum = 0;
	unsigned long long smallest = std::numeric_limits<unsigned long long>::max();

	for (const BlkDevice* device : devices)
	{
	    // TODO handle spare

	    unsigned long long size = device->get_size();

	    // metadata for version 1.0 is 4 KiB block at end aligned to 4 KiB,
	    // https://raid.wiki.kernel.org/index.php/RAID_superblock_formats
	    size = (size & ~(0x1000 - 1)) - 0x2000;

	    // bitmap uses otherwise unused space,
	    // https://raid.wiki.kernel.org/index.php/Write-intent_bitmap

	    long rest = size % real_chunk_size;
	    if (rest > 0)
		size -= rest;

	    number++;
	    sum += size;
	    smallest = min(smallest, size);
	}

	unsigned long long size = 0;
	long optimal_io_size = 0;

	switch (md_level)
	{
	    case RAID0:
		if (number >= 2)
		{
		    size = sum;
		    optimal_io_size = real_chunk_size * number;
		}
		break;

	    case RAID1:
		if (number >= 2)
		{
		    size = smallest;
		    optimal_io_size = 32768;
		}
		break;

	    case RAID5:
		if (number >= 3)
		{
		    size = smallest * (number - 1);
		    optimal_io_size = real_chunk_size * (number - 1);
		}
		break;

	    case RAID6:
		if (number >= 4)
		{
		    size = smallest * (number - 2);
		    optimal_io_size = real_chunk_size * (number - 2);
		}
		break;

	    case RAID10:
		if (number >= 2)
		{
		    size = smallest * number / 2;
		    optimal_io_size = real_chunk_size * number / 2;
		    if (number % 2 == 1)
			optimal_io_size *= 2;
		}
		break;

	    case UNKNOWN:
		break;
	}

	set_size(size);
	set_topology(Topology(0, optimal_io_size));
    }


    Text
    Md::Impl::do_create_text(Tense tense) const
    {
	Text text = tenser(tense,
			   // TRANSLATORS: displayed before action,
			   // %1$s is replaced by RAID level (e.g. RAID0),
			   // %2$s is replaced by RAID name (e.g. /dev/md0),
			   // %3$s is replaced by size (e.g. 2GiB)
			   _("Create MD %1$s %2$s (%3$s)"),
			   // TRANSLATORS: displayed during action,
			   // %1$s is replaced by RAID level (e.g. RAID0),
			   // %2$s is replaced by RAID name (e.g. /dev/md0),
			   // %3$s is replaced by size (e.g. 2GiB)
			   _("Creating MD %1$s %2$s (%3$s)"));

	return sformat(text, get_md_level_name(md_level).c_str(), get_displayname().c_str(),
		       get_size_string().c_str());
    }


    void
    Md::Impl::do_create() const
    {
	// Note: Changing any parameter to "mdadm --create' requires the
	// function calculate_region_and_topology() to be checked!

	string cmd_line = MDADMBIN " --create " + quote(get_name()) + " --run --level=" +
	    boost::to_lower_copy(toString(md_level), locale::classic()) + " --metadata 1.0"
	    " --homehost=any";

	if (md_level == RAID1 || md_level == RAID5 || md_level == RAID6 || md_level == RAID10)
	    cmd_line += " --bitmap internal";

	if (chunk_size > 0)
	    cmd_line += " --chunk=" + to_string(chunk_size / KiB);

	if (md_parity != DEFAULT)
	    cmd_line += " --parity=" + toString(md_parity);

	vector<string> devices;
	vector<string> spares;

	for (const BlkDevice* blk_device : get_devices())
	{
	    bool spare = false;

	    // TODO add get_out_holder that throws if num_children != 1, like get_single_child_of_type

	    for (const Holder* out_holder : blk_device->get_out_holders())
	    {
		if (to_md_user(out_holder)->is_spare())
		{
		    spare = true;
		    break;
		}
	    }

	    if (!spare)
		devices.push_back(blk_device->get_name());
	    else
		spares.push_back(blk_device->get_name());
	}

	cmd_line += " --raid-devices=" + to_string(devices.size());

	if (!spares.empty())
	    cmd_line += " --spare-devices=" + to_string(spares.size());

	cmd_line += " " + quote(devices) + " " + quote(spares);

	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("create md raid failed"));
    }


    Text
    Md::Impl::do_delete_text(Tense tense) const
    {
	Text text = tenser(tense,
			   // TRANSLATORS: displayed before action,
			   // %1$s is replaced by RAID level (e.g. RAID0),
			   // %2$s is replaced by RAID name (e.g. /dev/md0),
			   // %3$s is replaced by size (e.g. 2GiB)
			   _("Delete MD %1$s %2$s (%3$s)"),
			   // TRANSLATORS: displayed during action,
			   // %1$s is replaced by RAID level (e.g. RAID0),
			   // %2$s is replaced by RAID name (e.g. /dev/md0),
			   // %3$s is replaced by size (e.g. 2GiB)
			   _("Deleting MD %1$s %2$s (%3$s)"));

	return sformat(text, get_md_level_name(md_level).c_str(), get_displayname().c_str(),
		       get_size_string().c_str());
    }


    void
    Md::Impl::do_delete() const
    {
	// TODO split into deactivate and delete?

	string cmd_line = MDADMBIN " --stop " + quote(get_name());

	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("delete md raid failed"));

	for (const BlkDevice* blk_device : get_devices())
	{
	    blk_device->get_impl().wipe_device();
	}
    }


    Text
    Md::Impl::do_add_etc_mdadm_text(Tense tense) const
    {
	return sformat(_("Add %1$s to /etc/mdadm.conf"), get_name().c_str());
    }


    void
    Md::Impl::do_add_etc_mdadm(const Actiongraph::Impl& actiongraph) const
    {
	// TODO
    }


    Text
    Md::Impl::do_remove_etc_mdadm_text(Tense tense) const
    {
	return sformat(_("Remove %1$s from /etc/mdadm.conf"), get_name().c_str());
    }


    void
    Md::Impl::do_remove_etc_mdadm(const Actiongraph::Impl& actiongraph) const
    {
	// TODO
    }


    Text
    Md::Impl::do_reallot_text(ReallotMode reallot_mode, const BlkDevice* blk_device, Tense tense) const
    {
	Text text;

	switch (reallot_mode)
	{
	    case ReallotMode::REDUCE:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by device name (e.g. /dev/sdd),
			      // %2$s is replaced by device name (e.g. /dev/md0)
			      _("Remove %1$s from %2$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by device name (e.g. /dev/sdd),
			      // %2$s is replaced by device name (e.g. /dev/md0)
			      _("Removing %1$s from %2$s"));
		break;

	    case ReallotMode::EXTEND:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by device name (e.g. /dev/sdd),
			      // %2$s is replaced by device name (e.g. /dev/md0)
			      _("Add %1$s to %2$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by device name (e.g. /dev/sdd),
			      // %2$s is replaced by device name (e.g. /dev/md0)
			      _("Adding %1$s to %2$s"));
		break;

	    default:
		ST_THROW(LogicException("invalid value for reallot_mode"));
	}

	return sformat(text, blk_device->get_name().c_str(), get_displayname().c_str());
    }


    void
    Md::Impl::do_reallot(ReallotMode reallot_mode, const BlkDevice* blk_device) const
    {
	switch (reallot_mode)
	{
	    case ReallotMode::REDUCE:
		do_reduce(blk_device);
		return;

	    case ReallotMode::EXTEND:
		do_extend(blk_device);
		return;
	}

	ST_THROW(LogicException("invalid value for reallot_mode"));
    }


    void
    Md::Impl::do_reduce(const BlkDevice* blk_device) const
    {
	string cmd_line = MDADMBIN " --remove " + quote(get_name()) + " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("reduce md failed"));

	// Thanks to udev "md-raid-assembly.rules" running "parted <disk>
	// print" readds the device to the md if the signature is still
	// valid. Thus remove the signature.
	blk_device->get_impl().wipe_device();
    }


    void
    Md::Impl::do_extend(const BlkDevice* blk_device) const
    {
	string cmd_line = MDADMBIN " --add " + quote(get_name()) + " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("extend md failed"));
    }


    namespace Action
    {

	Text
	AddEtcMdadm::text(const Actiongraph::Impl& actiongraph, Tense tense) const
	{
	    const Md* md = to_md(get_device_rhs(actiongraph));
	    return md->get_impl().do_add_etc_mdadm_text(tense);
	}


	void
	AddEtcMdadm::commit(const Actiongraph::Impl& actiongraph) const
	{
	    const Md* md = to_md(get_device_rhs(actiongraph));
	    md->get_impl().do_add_etc_mdadm(actiongraph);
	}


	void
	AddEtcMdadm::add_dependencies(Actiongraph::Impl::vertex_descriptor v,
				      Actiongraph::Impl& actiongraph) const
	{
	    if (actiongraph.mount_root_filesystem != actiongraph.vertices().end())
		actiongraph.add_edge(*actiongraph.mount_root_filesystem, v);
	}


	Text
	RemoveEtcMdadm::text(const Actiongraph::Impl& actiongraph, Tense tense) const
	{
	    const Md* md = to_md(get_device_lhs(actiongraph));
	    return md->get_impl().do_remove_etc_mdadm_text(tense);
	}


	void
	RemoveEtcMdadm::commit(const Actiongraph::Impl& actiongraph) const
	{
	    const Md* md = to_md(get_device_lhs(actiongraph));
	    md->get_impl().do_remove_etc_mdadm(actiongraph);
	}

    }


    bool
    compare_by_number(const Md* lhs, const Md* rhs)
    {
	return lhs->get_number() < rhs->get_number();
    }

}
