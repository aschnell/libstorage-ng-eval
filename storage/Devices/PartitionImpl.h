#ifndef PARTITION_IMPL_H
#define PARTITION_IMPL_H


#include "storage/Devices/Partition.h"
#include "storage/Devices/BlkDeviceImpl.h"
#include "storage/Action.h"
#include "storage/Utils/Region.h"


namespace storage
{

    using namespace std;


    class Partition::Impl : public BlkDevice::Impl
    {
    public:

	Impl(const string& name)
	    : BlkDevice::Impl(name), region(), type(PRIMARY), id(ID_LINUX), boot(false) {}

	Impl(const xmlNode* node);

	virtual Impl* clone() const override { return new Impl(*this); }

	void probe(SystemInfo& systeminfo);

	virtual void save(xmlNode* node) const override;

	unsigned int get_number() const;

	const Region& get_region() const { return region; }
	void set_region(const Region& region) { Impl::region = region; }

	PartitionType get_type() const { return type; }
	void set_type(PartitionType type) { Impl::type = type; }

	unsigned get_id() const { return id; }
	void set_id(unsigned id) { Impl::id = id; }

	bool get_boot() const { return boot; }
	void set_boot(bool boot) { Impl::boot = boot; }

	const PartitionTable* get_partition_table() const;

	virtual void add_create_actions(Actiongraph& actiongraph) const override;
	virtual void add_delete_actions(Actiongraph& actiongraph) const override;

    private:

	Region region;
	PartitionType type;
	unsigned id;
	bool boot;

    };


    namespace Action
    {

	class CreatePartition : public Create
	{
	public:

	    CreatePartition(sid_t sid) : Create(sid) {}

	    virtual Text text(const Actiongraph& actiongraph, bool doing) const override;
	    virtual void commit(const Actiongraph& actiongraph) const override;

	};


	class SetPartitionId : public Modify
	{
	public:

	    SetPartitionId(sid_t sid) : Modify(sid) {}

	    virtual Text text(const Actiongraph& actiongraph, bool doing) const override;
	    virtual void commit(const Actiongraph& actiongraph) const override;

	};


	class DeletePartition : public Delete
	{
	public:

	    DeletePartition(sid_t sid) : Delete(sid) {}

	    virtual Text text(const Actiongraph& actiongraph, bool doing) const override;
	    virtual void commit(const Actiongraph& actiongraph) const override;

	};

    }

}

#endif
