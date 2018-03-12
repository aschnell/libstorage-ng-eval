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


#ifndef STORAGE_PARTITION_TABLE_IMPL_H
#define STORAGE_PARTITION_TABLE_IMPL_H


#include "storage/Devices/PartitionTable.h"
#include "storage/Devices/DeviceImpl.h"
#include "storage/Utils/Enum.h"
#include "storage/Utils/Alignment.h"


namespace storage
{

    using namespace std;


    template <> struct EnumTraits<PtType> { static const vector<string> names; };

    template <> struct DeviceTraits<PartitionTable> { static const char* classname; };


    // abstract class

    class PartitionTable::Impl : public Device::Impl
    {
    public:

	virtual void probe_pass_1c(Prober& prober) override;

	virtual void check(const CheckCallbacks* check_callbacks) const override;

	virtual Partition* create_partition(const string& name, const Region& region, PartitionType type);

	virtual void delete_partition(Partition* partition);

	void delete_partition(const string& name);

	virtual PtType get_type() const = 0;

	Partition* get_partition(const string& name);

	virtual vector<Partition*> get_partitions();
	virtual vector<const Partition*> get_partitions() const;

	Partitionable* get_partitionable();
	const Partitionable* get_partitionable() const;

	virtual bool equal(const Device::Impl& rhs) const override = 0;
	virtual void log_diff(std::ostream& log, const Device::Impl& rhs_base) const override = 0;

	virtual void print(std::ostream& out) const override = 0;

	/**
	 * Returns whether the partition type is supported for the partition
	 * table. PRIMARY is supported for GPT and DASD.
	 */
	virtual bool is_partition_type_supported(PartitionType type) const = 0;

	/**
	 * Returns whether the id is supported for the partition table. May
	 * return true for ID_UNKNOWN but committing ID_UNKNOWN to disk is not
	 * supported.
	 */
	virtual bool is_partition_id_supported(unsigned int id) const = 0;

	/**
	 * Returns whether the value of the partition id enum is standardized
	 * and should be included in e.g. action messages. The values on Msdos
	 * are standardized, others not.
	 *
	 * The intention here is to not display e.g. "Set id to BIOS Boot
	 * (0x101)" since 0x101 is non-standardized value thus meaningless to
	 * the user.
	 *
	 * On the other hand, displaying e.g. "Set id to EFI System Partition
	 * (0xEF)" is meaningful since the user can lookup the standardized
	 * 0xEF.
	 */
	virtual bool are_partition_id_values_standardized() const { return false; }

	virtual bool is_partition_boot_flag_supported() const { return false; }

	virtual bool is_partition_legacy_boot_flag_supported() const { return false; }

	virtual unsigned int max_primary() const = 0;
	virtual bool extended_possible() const { return false; }
	virtual unsigned int max_logical() const { return 0; }

	virtual unsigned int num_primary() const;
	virtual bool has_extended() const { return false; }
	virtual unsigned int num_logical() const { return 0; }

	virtual const Partition* get_extended() const;

	/**
	 * Number of unusable sectors at the front and back of the
	 * partitionable (due to e.g. data for partition table or
	 * unaddressable by partition table).
	 */
	virtual pair<unsigned long long, unsigned long long> unusable_sectors() const = 0;

	/**
	 * Returns if the partition table has a usable region. Also see
	 * get_usable_region().
	 */
	bool has_usable_region() const;

	/**
	 * Returns the usable region of the partition table. This is the
	 * region of the partitionable not used for the partition table data
	 * or MBR gap.
	 */
	Region get_usable_region() const;

	virtual Alignment get_alignment(AlignType align_type) const;

	virtual vector<PartitionSlot> get_unused_partition_slots(AlignPolicy align_policy,
								 AlignType align_type) const;

	Region align(const Region& region, AlignPolicy align_policy, AlignType align_type) const;

	static void run_dependency_manager(Actiongraph::Impl& actiongraph);

	virtual void do_delete() const override;

    protected:

	Impl()
	    : Device::Impl(), read_only(false) {}

	Impl(const xmlNode* node);

	virtual void save(xmlNode* node) const override;

    private:

	/**
	 * TODO document and use
	 */
	bool read_only;

    };

}

#endif
