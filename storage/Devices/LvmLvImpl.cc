/*
 * Copyright (c) [2016-2019] SUSE LLC
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

#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Utils/XmlFile.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/Math.h"
#include "storage/Utils/CallbacksImpl.h"
#include "storage/SystemInfo/SystemInfo.h"
#include "storage/Devices/LvmLvImpl.h"
#include "storage/Devices/LvmVgImpl.h"
#include "storage/Holders/Subdevice.h"
#include "storage/Storage.h"
#include "storage/FreeInfo.h"
#include "storage/Holders/User.h"
#include "storage/Devicegraph.h"
#include "storage/Action.h"
#include "storage/FindBy.h"
#include "storage/Prober.h"
#include "storage/Redirect.h"
#include "storage/Utils/Format.h"


using namespace std;


namespace storage
{

    const char* DeviceTraits<LvmLv>::classname = "LvmLv";


    const vector<string> EnumTraits<LvType>::names({
	"unknown", "normal", "thin-pool", "thin", "raid"
    });


    LvmLv::Impl::Impl(const string& vg_name, const string& lv_name, LvType lv_type)
	: BlkDevice::Impl(make_name(vg_name, lv_name)), lv_name(lv_name), lv_type(lv_type),
	  uuid(), stripes(lv_type == LvType::THIN ? 0 : 1), stripe_size(0), chunk_size(0)
    {
	set_active(lv_type != LvType::THIN_POOL);

	set_dm_table_name(make_dm_table_name(vg_name, lv_name));
    }


    LvmLv::Impl::Impl(const xmlNode* node)
	: BlkDevice::Impl(node), lv_name(), lv_type(LvType::NORMAL), uuid(), stripes(0),
	  stripe_size(0), chunk_size(0)
    {
	string tmp;

	if (get_dm_table_name().empty())
	    ST_THROW(Exception("no dm-table-name"));

	if (!getChildValue(node, "lv-name", lv_name))
	    ST_THROW(Exception("no lv-name"));

	if (getChildValue(node, "lv-type", tmp))
	    lv_type = toValueWithFallback(tmp, LvType::NORMAL);

	getChildValue(node, "uuid", uuid);

	getChildValue(node, "stripes", stripes);
	getChildValue(node, "stripe-size", stripe_size);

	getChildValue(node, "chunk-size", chunk_size);
    }


    string
    LvmLv::Impl::get_pretty_classname() const
    {
	// TRANSLATORS: name of object
	return _("LVM Logical Volume").translated;
    }


    void
    LvmLv::Impl::save(xmlNode* node) const
    {
	BlkDevice::Impl::save(node);

	setChildValue(node, "lv-name", lv_name);

	setChildValue(node, "lv-type", toString(lv_type));

	setChildValue(node, "uuid", uuid);

	setChildValueIf(node, "stripes", stripes, stripes != 0);
	setChildValueIf(node, "stripe-size", stripe_size, stripe_size != 0);

	setChildValueIf(node, "chunk-size", chunk_size, chunk_size != 0);
    }


    bool
    LvmLv::Impl::is_usable_as_blk_device() const
    {
	return lv_type == LvType::NORMAL || lv_type == LvType::THIN || lv_type == LvType::RAID;
    }


    void
    LvmLv::Impl::check(const CheckCallbacks* check_callbacks) const
    {
	BlkDevice::Impl::check(check_callbacks);

	if (get_lv_name().empty())
	    ST_THROW(Exception("LvmLv has no lv-name"));

	if (get_region().get_start() != 0)
	    ST_THROW(Exception("LvmLv region start not zero"));

	if (check_callbacks)
	{
	    const LvmVg* lvm_vg = get_lvm_vg();

	    if (stripes > 1)
	    {
		if (!is_multiple_of(number_of_extents(), stripes))
		    check_callbacks->error(sformat("Number of extents not a multiple of stripes "
						   "of logical volume %s in volume group %s.",
						   lv_name, lvm_vg->get_vg_name()));
	    }

	    if (stripe_size > 0)
	    {
		if (stripe_size > lvm_vg->get_extent_size())
		    check_callbacks->error(sformat("Stripe size is greater then the extent size "
						   "of logical volume %s in volume group %s.",
						   lv_name, lvm_vg->get_vg_name()));
	    }

	    // the constant 265289728 is calculated from the LVM sources
	    if (lv_type == LvType::THIN_POOL && chunk_size > 0 && get_size() > chunk_size * 265289728)
		check_callbacks->error(sformat("Chunk size is too small for thin pool logical "
					       "volume %s in volume group %s.", lv_name,
					       lvm_vg->get_vg_name()));
	}
    }


    bool
    LvmLv::Impl::activate_lvm_lvs(const ActivateCallbacks* activate_callbacks)
    {
	y2mil("activate_lvm_lvs");

	try
	{
	    size_t number_of_inactive = CmdLvs().number_of_inactive();

	    if (number_of_inactive == 0)
		return false;

	    // TRANSLATORS: progress message
	    message_callback(activate_callbacks, _("Activating LVM"));

	    try
	    {
		SystemCmd cmd(VGCHANGEBIN " --ignoreskippedcluster --activate y", SystemCmd::DoThrow);
	    }
	    catch (const Exception& exception)
	    {
		// TRANSLATORS: error message
		error_callback(activate_callbacks, _("Activating LVM failed"), exception);
	    }

	    bool ret = number_of_inactive != CmdLvs().number_of_inactive();

	    if (ret)
		SystemCmd(UDEVADMBIN_SETTLE);

	    return ret;
	}
	catch (const Exception& exception)
	{
	    ST_CAUGHT(exception);

	    if (typeid(exception) == typeid(Aborted))
		ST_RETHROW(exception);

	    // Ignore failure to detect whether LVM needs to be activated.

	    return false;
	}
    }


    bool
    LvmLv::Impl::deactivate_lvm_lvs()
    {
	y2mil("deactivate_lvm_lvs");

	string cmd_line = VGCHANGEBIN " --ignoreskippedcluster --activate n";

	SystemCmd cmd(cmd_line);

	return cmd.retcode() == 0;
    }


    void
    LvmLv::Impl::probe_lvm_lvs(Prober& prober)
    {
	vector<CmdLvs::Lv> lvs = prober.get_system_info().getCmdLvs().get_lvs();

	// ensure thin-pools are probed before thins

	stable_partition(lvs.begin(), lvs.end(), [](const CmdLvs::Lv& lv) {
	    return lv.lv_type != LvType::THIN;
	});

	vector<string> unsupported_lvs;

	for (const CmdLvs::Lv& lv : lvs)
	{
	    LvmLv* lvm_lv = nullptr;

	    switch (lv.lv_type)
	    {
		case LvType::NORMAL:
		case LvType::THIN_POOL:
		case LvType::RAID:
		{
		    LvmVg* lvm_vg = LvmVg::Impl::find_by_uuid(prober.get_system(), lv.vg_uuid);
		    lvm_lv = lvm_vg->create_lvm_lv(lv.lv_name, lv.lv_type, lv.size);
		}
		break;

		case LvType::THIN:
		{
		    LvmLv* thin_pool = LvmLv::Impl::find_by_uuid(prober.get_system(), lv.pool_uuid);
		    lvm_lv = thin_pool->create_lvm_lv(lv.lv_name, lv.lv_type, lv.size);
		}
		break;

		case LvType::UNKNOWN:
		{
		    // private lvm lvs (e.g. metadata) are ignored,
		    // for public ones the user is informed

		    y2war("unsupported lvm_lv " << lv.vg_name << " " << lv.lv_name);

		    if (lv.role == CmdLvs::Role::PUBLIC)
			unsupported_lvs.push_back(DEV_DIR "/" + lv.vg_name + "/" + lv.lv_name);

		    continue;
		}
	    }

	    ST_CHECK_PTR(lvm_lv);

	    lvm_lv->get_impl().set_uuid(lv.lv_uuid);
	    lvm_lv->get_impl().set_active(lv.active && lv.lv_type != LvType::THIN_POOL);
	    lvm_lv->get_impl().probe_pass_1a(prober);
	}

	if (!unsupported_lvs.empty())
	{
	    sort(unsupported_lvs.begin(), unsupported_lvs.end());

	    // TRANSLATORS: Error message displayed during probing,
	    // %1$s is replaced by a list of device names joined by newlines (e.g.
	    // /dev/test/cached\n/dev/test/snapshot)
	    Text text = sformat(_("Detected LVM logical volumes of unsupported types:\n\n%1$s\n\n"
				  "These logical volumes are ignored. Operations on the\n"
				  "correponding volume groups may fail."),
				join(unsupported_lvs, JoinMode::NEWLINE, 10));

	    error_callback(prober.get_probe_callbacks(), text);
	}
    }


    void
    LvmLv::Impl::probe_pass_1a(Prober& prober)
    {
	BlkDevice::Impl::probe_pass_1a(prober);

	const LvmVg* lvm_vg = get_lvm_vg();

	set_dm_table_name(make_dm_table_name(lvm_vg->get_vg_name(), lv_name));

	// Use the stripes, stripe_size and chunk_size from the first segment.

	if (lv_type == LvType::THIN_POOL)
	{
	    const CmdLvs::Lv& lv = prober.get_system_info().getCmdLvs().find_by_lv_uuid(uuid);
	    chunk_size = lv.segments[0].chunk_size;

	    const CmdLvs::Lv& data_lv = prober.get_system_info().getCmdLvs().find_by_lv_uuid(lv.data_uuid);
	    stripes = data_lv.segments[0].stripes;
	    stripe_size = data_lv.segments[0].stripe_size;
	}
	else
	{
	    const CmdLvs::Lv& lv = prober.get_system_info().getCmdLvs().find_by_lv_uuid(uuid);
	    stripes = lv.segments[0].stripes;
	    stripe_size = lv.segments[0].stripe_size;
	}
    }


    bool
    LvmLv::Impl::supports_stripes() const
    {
	return lv_type == LvType::NORMAL || lv_type == LvType::THIN_POOL;
    }


    void
    LvmLv::Impl::set_stripes(unsigned int stripes)
    {
	if (stripes > 128)
	    ST_THROW(Exception("stripes above 128"));

	Impl::stripes = stripes;
    }


    void
    LvmLv::Impl::set_stripe_size(unsigned long long stripe_size)
    {
	if (stripe_size > 0)
	{
	    if (stripe_size < 4 * KiB)
		ST_THROW(Exception("stripe size below 4 KiB"));

	    if (!is_power_of_two(stripe_size))
		ST_THROW(Exception("stripe size not a power of two"));
	}

	Impl::stripe_size = stripe_size;
    }


    bool
    LvmLv::Impl::supports_chunk_size() const
    {
	return lv_type == LvType::THIN_POOL;
    }


    void
    LvmLv::Impl::set_chunk_size(unsigned long long chunk_size)
    {
	if (chunk_size > 0)
	{
	    if (chunk_size < 64 * KiB)
		ST_THROW(Exception("chunk size below 64 KiB"));

	    if (chunk_size > 1 * GiB)
		ST_THROW(Exception("chunk size above 1 GiB"));

	    if (!is_multiple_of(chunk_size, 64 * KiB))
		ST_THROW(Exception("chunk size not multiple of 64 KiB"));
	}

	Impl::chunk_size = chunk_size;
    }


    unsigned long long
    LvmLv::Impl::default_chunk_size(unsigned long long size)
    {
	// Calculation researched, limits can be found in the LVM documentation.

	unsigned long long tmp = next_power_of_two(size >> 21);

	return clamp(tmp, 64 * KiB, 1 * GiB);
    }


    unsigned long long
    LvmLv::Impl::default_chunk_size() const
    {
	return default_chunk_size(get_size());
    }


    unsigned long long
    LvmLv::Impl::default_metadata_size(unsigned long long size, unsigned long long chunk_size,
				       unsigned long long extent_size)
    {
	// Calculation and limits can be found in the LVM documentation.

	unsigned long long tmp = round_up(size / chunk_size * 64 * B, extent_size);

	return clamp(tmp, 2 * MiB, 16 * GiB);
    }


    unsigned long long
    LvmLv::Impl::default_metadata_size() const
    {
	unsigned long long tmp = chunk_size > 0 ? chunk_size : default_chunk_size();

	return default_metadata_size(get_size(), tmp, get_region().get_block_size());
    }


    void
    LvmLv::Impl::set_lv_name(const string& lv_name)
    {
	Impl::lv_name = lv_name;

	const LvmVg* lvm_vg = get_lvm_vg();
	set_name(make_name(lvm_vg->get_vg_name(), lv_name));
	set_dm_table_name(make_dm_table_name(lvm_vg->get_vg_name(), lv_name));

	// TODO clear or update udev-ids; update looks difficult/impossible.
    }


    const LvmVg*
    LvmLv::Impl::get_lvm_vg() const
    {
	if (lv_type == LvType::THIN)
	    return get_thin_pool()->get_lvm_vg();

	return get_single_parent_of_type<const LvmVg>();
    }


    const LvmLv*
    LvmLv::Impl::get_thin_pool() const
    {
	if (lv_type != LvType::THIN)
	    ST_THROW(Exception("not a thin logical volume"));

	return get_single_parent_of_type<const LvmLv>();
    }


    unsigned long long
    LvmLv::Impl::max_size_for_lvm_lv(LvType lv_type, const vector<sid_t>& ignore_sids) const
    {
	switch (lv_type)
	{
	    case LvType::THIN:
	    {
		return LvmVg::Impl::max_extent_number * get_lvm_vg()->get_extent_size();
	    }

	    default:
	    {
		return 0;
	    }
	}
    }


    LvmLv*
    LvmLv::Impl::create_lvm_lv(const string& lv_name, LvType lv_type, unsigned long long size)
    {
	Devicegraph* devicegraph = get_devicegraph();

	const LvmVg* lvm_vg = get_lvm_vg();

	LvmLv* lvm_lv = LvmLv::create(devicegraph, lvm_vg->get_vg_name(), lv_name, lv_type);
	Subdevice::create(devicegraph, get_non_impl(), lvm_lv);

	unsigned long long extent_size = lvm_vg->get_region().get_block_size();
	lvm_lv->set_region(Region(0, size / extent_size, extent_size));

	return lvm_lv;
    }


    LvmLv*
    LvmLv::Impl::get_lvm_lv(const string& lv_name)
    {
	Devicegraph::Impl& devicegraph = get_devicegraph()->get_impl();
	Devicegraph::Impl::vertex_descriptor vertex = get_vertex();

	for (LvmLv* lvm_lv : devicegraph.filter_devices_of_type<LvmLv>(devicegraph.children(vertex)))
	{
	    if (lvm_lv->get_lv_name() == lv_name)
		return lvm_lv;
	}

	ST_THROW(LvmLvNotFoundByLvName(lv_name));
    }


    vector<LvmLv*>
    LvmLv::Impl::get_lvm_lvs()
    {
	Devicegraph::Impl& devicegraph = get_devicegraph()->get_impl();
	Devicegraph::Impl::vertex_descriptor vertex = get_vertex();

	return devicegraph.filter_devices_of_type<LvmLv>(devicegraph.children(vertex));
    }


    vector<const LvmLv*>
    LvmLv::Impl::get_lvm_lvs() const
    {
	const Devicegraph::Impl& devicegraph = get_devicegraph()->get_impl();
	Devicegraph::Impl::vertex_descriptor vertex = get_vertex();

	return devicegraph.filter_devices_of_type<LvmLv>(devicegraph.children(vertex));
    }


    LvmLv*
    LvmLv::Impl::find_by_uuid(Devicegraph* devicegraph, const string& uuid)
    {
	return storage::find_by_uuid<LvmLv>(devicegraph, uuid);
    }


    const LvmLv*
    LvmLv::Impl::find_by_uuid(const Devicegraph* devicegraph, const string& uuid)
    {
	return storage::find_by_uuid<const LvmLv>(devicegraph, uuid);
    }


    bool
    LvmLv::Impl::equal(const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	if (!BlkDevice::Impl::equal(rhs))
	    return false;

	return lv_name == rhs.lv_name && lv_type == rhs.lv_type && uuid == rhs.uuid &&
	    stripes == rhs.stripes && stripe_size == rhs.stripe_size &&
	    chunk_size == rhs.chunk_size;
    }


    void
    LvmLv::Impl::log_diff(std::ostream& log, const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	BlkDevice::Impl::log_diff(log, rhs);

	storage::log_diff(log, "lv-name", lv_name, rhs.lv_name);

	storage::log_diff_enum(log, "lv-type", lv_type, rhs.lv_type);

	storage::log_diff(log, "uuid", uuid, rhs.uuid);

	storage::log_diff(log, "stripes", stripes, rhs.stripes);
	storage::log_diff(log, "stripe-size", stripe_size, rhs.stripe_size);

	storage::log_diff(log, "chunk-size", chunk_size, rhs.chunk_size);
    }


    void
    LvmLv::Impl::print(std::ostream& out) const
    {
	BlkDevice::Impl::print(out);

	out << " lv-name:" << lv_name << " lv-type:" << toString(lv_type) << " uuid:" << uuid;

	if (stripes != 0)
	    out << " stripes:" << stripes;
	if (stripe_size != 0)
	    out << " stripe-size:" << stripe_size;

	if (chunk_size != 0)
	    out << " chunk-size:" << chunk_size;
    }


    ResizeInfo
    LvmLv::Impl::detect_resize_info() const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	unsigned long long extent_size = lvm_vg->get_extent_size();

	// A logical volume must have at least one extent.

	switch (lv_type)
	{
	    case LvType::NORMAL:
	    {
		ResizeInfo resize_info = BlkDevice::Impl::detect_resize_info();

		unsigned long long free_extents = lvm_vg->get_impl().number_of_free_extents({ get_sid() });

		unsigned long long data_size = free_extents * extent_size;

		resize_info.combine(ResizeInfo(true, 0, extent_size, data_size));

		if (get_region().get_length() <= 1)
		    resize_info.reasons |= RB_MIN_SIZE_FOR_LVM_LV;

		if (get_region().get_length() >= free_extents)
		    resize_info.reasons |= RB_NO_SPACE_IN_LVM_VG;

		resize_info.combine_block_size(max(1U, stripes) * extent_size);

		return resize_info;
	    }

	    case LvType::THIN_POOL:
	    {
		if (exists_in_system())
		{
		    // Shrinking thin pools is not supported by LVM. Since the
		    // metadata is already on disk and does not get resized no
		    // need to handle them here.

		    const LvmLv* tmp_lvm_lv = to_lvm_lv(redirect_to_system(get_non_impl()));

		    unsigned long long data_size = (lvm_vg->get_impl().number_of_free_extents() +
						    number_of_extents()) * extent_size;

		    ResizeInfo resize_info(true, RB_SHRINK_NOT_SUPPORTED_FOR_LVM_LV_TYPE,
					   tmp_lvm_lv->get_size(), data_size);

		    if (get_region().get_length() * extent_size >= data_size)
			resize_info.reasons |= RB_NO_SPACE_IN_LVM_VG;

		    resize_info.combine_block_size(max(1U, stripes) * extent_size);

		    return resize_info;
		}
		else
		{
		    unsigned long long data_size = lvm_vg->get_impl().max_size_for_lvm_lv(lv_type, { get_sid() });

		    ResizeInfo resize_info(true, 0, extent_size, data_size);

		    if (get_region().get_length() <= 1)
			resize_info.reasons |= RB_MIN_SIZE_FOR_LVM_LV;

		    if (get_region().get_length() * extent_size >= data_size)
			resize_info.reasons |= RB_NO_SPACE_IN_LVM_VG;

		    resize_info.combine_block_size(max(1U, stripes) * extent_size);

		    return resize_info;
		}
	    }

	    case LvType::THIN:
	    {
		ResizeInfo resize_info = BlkDevice::Impl::detect_resize_info();

		const LvmLv* thin_pool = get_thin_pool();

		unsigned long long data_size = thin_pool->max_size_for_lvm_lv(LvType::THIN);

		resize_info.combine(ResizeInfo(true, 0, extent_size, data_size));

		if (get_region().get_length() <= 1)
		    resize_info.reasons |= RB_MIN_SIZE_FOR_LVM_LV;

		if (get_region().get_length() * extent_size >= data_size)
		    resize_info.reasons |= RB_MAX_SIZE_FOR_LVM_LV_THIN;

		resize_info.combine_block_size(extent_size);

		return resize_info;
	    }

	    default:
	    {
		return ResizeInfo(false, RB_RESIZE_NOT_SUPPORTED_FOR_LVM_LV_TYPE);
	    }
	}
    }


    void
    LvmLv::Impl::add_modify_actions(Actiongraph::Impl& actiongraph, const Device* lhs_base) const
    {
	BlkDevice::Impl::add_modify_actions(actiongraph, lhs_base);

	const Impl& lhs = dynamic_cast<const Impl&>(lhs_base->get_impl());

	if (get_name() != lhs.get_name())
	{
	    Action::Base* action = new Action::Rename(get_sid());
	    actiongraph.add_vertex(action);
	    action->first = action->last = true;
	}

	if (get_lv_type() != lhs.get_lv_type())
	{
	    ST_THROW(Exception("changing lv type not supported"));
	}

	if (get_stripes() != lhs.get_stripes())
	{
	    ST_THROW(Exception("changing stripes not supported"));
	}

	if (get_stripe_size() != lhs.get_stripe_size())
	{
	    ST_THROW(Exception("changing stripe size not supported"));
	}

	if (get_chunk_size() != lhs.get_chunk_size())
	{
	    ST_THROW(Exception("changing chunk size not supported"));
	}
    }


    Text
    LvmLv::Impl::do_create_text(Tense tense) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	Text text;

	switch (lv_type)
	{
	    case LvType::THIN_POOL:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Create thin pool logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Creating thin pool logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    case LvType::THIN:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Create thin logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Creating thin logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    default:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Create logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Creating logical volume %1$s (%2$s) on volume group %3$s"));
		break;
	}

	return sformat(text, lv_name, get_size_text(), lvm_vg->get_vg_name());
    }


    void
    LvmLv::Impl::do_create()
    {
	const LvmVg* lvm_vg = get_lvm_vg();
	const Region& region = get_region();

	string cmd_line = LVCREATEBIN;

	switch (lv_type)
	{
	    case LvType::NORMAL:
	    {
		cmd_line += " --zero=y --wipesignatures=y --yes --extents " +
		    to_string(region.get_length());
	    }
	    break;

	    case LvType::THIN_POOL:
	    {
		cmd_line += " --type thin-pool --zero=y --yes --extents " +
		    to_string(region.get_length());
	    }
	    break;

	    case LvType::THIN:
	    {
		const LvmLv* thin_pool = get_thin_pool();

		cmd_line += " --type thin --wipesignatures=y --yes --virtualsize " +
		    to_string(region.to_bytes(region.get_length())) + "B --thin-pool " +
		    quote(thin_pool->get_lv_name());
	    }
	    break;

	    case LvType::UNKNOWN:
	    case LvType::RAID:
	    {
		ST_THROW(UnsupportedException(sformat("creating LvmLv with type %s is unsupported",
						      toString(lv_type))));
	    }
	    break;
	}

	if (supports_stripes() && stripes > 1)
	{
	    cmd_line += " --stripes " + to_string(stripes);
	    if (stripe_size > 0)
		cmd_line += " --stripesize " + to_string(stripe_size / KiB);
	}

	if (supports_chunk_size() && chunk_size > 0)
	{
	    cmd_line += " --chunksize " + to_string(chunk_size / KiB);
	}

	cmd_line += " --name " + quote(lv_name) + " " + quote(lvm_vg->get_vg_name());

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    void
    LvmLv::Impl::do_create_post_verify() const
    {
	// log some data about the logical volume that might be useful for debugging

	const LvmVg* lvm_vg = get_lvm_vg();

	string cmd_line = LVSBIN " --options vg_name,lv_name,lv_uuid,lv_size --units b " +
	    quote(lvm_vg->get_vg_name() + "/" + lv_name);

	SystemCmd cmd(cmd_line, SystemCmd::NoThrow);
    }


    Text
    LvmLv::Impl::do_rename_text(const Impl& lhs, Tense tense) const
    {
	// TRANSLATORS:
	// %1$s is replaced with the old logical volume name (e.g. foo),
	// %2$s is replaced with the new logical volume name (e.g. bar)
	Text text = _("Rename %1$s to %2$s");

	return sformat(text, lhs.get_displayname(), get_displayname());
    }


    void
    LvmLv::Impl::do_rename(const Impl& lhs) const
    {
    }


    Text
    LvmLv::Impl::do_resize_text(ResizeMode resize_mode, const Device* lhs, const Device* rhs,
				Tense tense) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	const LvmLv* lvm_lv_lhs = to_lvm_lv(lhs);
	const LvmLv* lvm_lv_rhs = to_lvm_lv(rhs);

	Text text;

	switch (resize_mode)
	{
	    case ResizeMode::SHRINK:

		switch (lv_type)
		{
		    case LvType::THIN_POOL:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrink thin pool logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrinking thin pool logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;

		    case LvType::THIN:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrink thin logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrinking thin logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;

		    default:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrink logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 2GiB),
				      // %4$s is replaced by new size (e.g. 1GiB)
				      _("Shrinking logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;
		}
		break;

	    case ResizeMode::GROW:

		switch (lv_type)
		{
		    case LvType::THIN_POOL:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Grow thin pool logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Growing thin pool logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;

		    case LvType::THIN:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Grow thin logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Growing thin logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;

		    default:
			text = tenser(tense,
				      // TRANSLATORS: displayed before action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Grow logical volume %1$s on volume group %2$s from %3$s to %4$s"),
				      // TRANSLATORS: displayed during action,
				      // %1$s is replaced by logical volume name (e.g. root),
				      // %2$s is replaced by volume group name (e.g. system),
				      // %3$s is replaced by old size (e.g. 1GiB),
				      // %4$s is replaced by new size (e.g. 2GiB)
				      _("Growing logical volume %1$s on volume group %2$s from %3$s to %4$s"));
			break;
		}
		break;

	    default:
		ST_THROW(LogicException("invalid value for resize_mode"));
	}

	return sformat(text, lv_name, lvm_vg->get_vg_name(),
		       lvm_lv_lhs->get_impl().get_size_text(),
		       lvm_lv_rhs->get_impl().get_size_text());
    }


    void
    LvmLv::Impl::do_resize(ResizeMode resize_mode, const Device* rhs) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	const LvmLv* lvm_lv_rhs = to_lvm_lv(rhs);

	string cmd_line = LVRESIZEBIN;

	if (resize_mode == ResizeMode::SHRINK)
	    cmd_line += " --force";

	cmd_line += " " + quote(lvm_vg->get_vg_name() + "/" + lv_name) + " --extents " +
	    to_string(lvm_lv_rhs->get_region().get_length());

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    LvmLv::Impl::do_delete_text(Tense tense) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	Text text;

	switch (lv_type)
	{
	    case LvType::THIN_POOL:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Delete thin pool logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deleting thin pool logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    case LvType::THIN:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Delete thin logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deleting thin logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    default:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Delete logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deleting logical volume %1$s (%2$s) on volume group %3$s"));
		break;
	}

	return sformat(text, lv_name, get_size_text(), lvm_vg->get_vg_name());
    }


    void
    LvmLv::Impl::do_delete() const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	string cmd_line = LVREMOVEBIN " --force " + quote(lvm_vg->get_vg_name() + "/" + lv_name);

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    LvmLv::Impl::do_activate_text(Tense tense) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	Text text;

	switch (lv_type)
	{
	    case LvType::THIN_POOL:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activate thin pool logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activating thin pool logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    case LvType::THIN:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activate thin logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activating thin logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    default:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activate logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Activating logical volume %1$s (%2$s) on volume group %3$s"));
		break;
	}

	return sformat(text, lv_name, get_size_text(), lvm_vg->get_vg_name());
    }


    void
    LvmLv::Impl::do_activate() const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	string cmd_line = LVCHANGEBIN " --activate y " + quote(lvm_vg->get_vg_name() + "/" + lv_name);

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    Text
    LvmLv::Impl::do_deactivate_text(Tense tense) const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	Text text;

	switch (lv_type)
	{
	    case LvType::THIN_POOL:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivate thin pool logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivating thin pool logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    case LvType::THIN:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivate thin logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivating thin logical volume %1$s (%2$s) on volume group %3$s"));
		break;

	    default:
		text = tenser(tense,
			      // TRANSLATORS: displayed before action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivate logical volume %1$s (%2$s) on volume group %3$s"),
			      // TRANSLATORS: displayed during action,
			      // %1$s is replaced by logical volume name (e.g. root),
			      // %2$s is replaced by size (e.g. 2GiB),
			      // %3$s is replaced by volume group name (e.g. system)
			      _("Deactivating logical volume %1$s (%2$s) on volume group %3$s"));
		break;
	}

	return sformat(text, lv_name, get_size_text(), lvm_vg->get_vg_name());
    }


    void
    LvmLv::Impl::do_deactivate() const
    {
	const LvmVg* lvm_vg = get_lvm_vg();

	string cmd_line = LVCHANGEBIN " --activate n " + quote(lvm_vg->get_vg_name() + "/" + lv_name);

	SystemCmd cmd(cmd_line, SystemCmd::DoThrow);
    }


    namespace Action
    {

	Text
	Rename::text(const CommitData& commit_data) const
	{
	    const LvmLv* lhs_lvm_lv = to_lvm_lv(get_device(commit_data.actiongraph, LHS));
	    const LvmLv* rhs_lvm_lv = to_lvm_lv(get_device(commit_data.actiongraph, RHS));
	    return rhs_lvm_lv->get_impl().do_rename_text(lhs_lvm_lv->get_impl(), commit_data.tense);
	}

	void
	Rename::commit(CommitData& commit_data, const CommitOptions& commit_options) const
	{
	    const LvmLv* lhs_lvm_lv = to_lvm_lv(get_device(commit_data.actiongraph, LHS));
	    const LvmLv* rhs_lvm_lv = to_lvm_lv(get_device(commit_data.actiongraph, RHS));
	    return rhs_lvm_lv->get_impl().do_rename(lhs_lvm_lv->get_impl());
	}

    }


    string
    LvmLv::Impl::make_name(const string& vg_name, const string& lv_name)
    {
	return DEV_DIR "/" + vg_name + "/" + lv_name;
    }


    string
    LvmLv::Impl::make_dm_table_name(const string& vg_name, const string& lv_name)
    {
	return boost::replace_all_copy(vg_name, "-", "--") + "-" +
	    boost::replace_all_copy(lv_name, "-", "--");
    }

}
