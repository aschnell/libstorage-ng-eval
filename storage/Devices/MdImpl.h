#ifndef STORAGE_MD_IMPL_H
#define STORAGE_MD_IMPL_H


#include "storage/Devices/Md.h"
#include "storage/Devices/PartitionableImpl.h"
#include "storage/Utils/Enum.h"
#include "storage/Action.h"


namespace storage
{

    using namespace std;


    template <> struct DeviceTraits<Md> { static const char* classname; };

    template <> struct EnumTraits<MdLevel> { static const vector<string> names; };
    template <> struct EnumTraits<MdParity> { static const vector<string> names; };


    class Md::Impl : public Partitionable::Impl
    {
    public:

	Impl(const string& name);
	Impl(const xmlNode* node);

	virtual const char* get_classname() const override { return DeviceTraits<Md>::classname; }

	virtual Impl* clone() const override { return new Impl(*this); }

	virtual void save(xmlNode* node) const override;

	MdUser* add_device(BlkDevice* blk_device);
	void remove_device(BlkDevice* blk_device);

	vector<BlkDevice*> get_devices();
	vector<const BlkDevice*> get_devices() const;

	unsigned int get_number() const;

	MdLevel get_md_level() const { return md_level; }
	void set_md_level(MdLevel md_level);

	MdParity get_md_parity() const { return md_parity; }
	void set_md_parity(MdParity md_parity) { Impl::md_parity = md_parity; }

	unsigned long get_chunk_size_k() const { return chunk_size_k; }
	void set_chunk_size_k(unsigned long chunk_size_k);

	static bool is_valid_name(const string& name);

	static vector<string> probe_mds(SystemInfo& systeminfo);

	virtual void probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo) override;
	virtual void probe_pass_2(Devicegraph* probed, SystemInfo& systeminfo) override;

	virtual void add_create_actions(Actiongraph::Impl& actiongraph) const override;
	virtual void add_delete_actions(Actiongraph::Impl& actiongraph) const override;

	virtual bool equal(const Device::Impl& rhs) const override;
	virtual void log_diff(std::ostream& log, const Device::Impl& rhs_base) const override;

	virtual void print(std::ostream& out) const override;

	virtual void process_udev_ids(vector<string>& udev_ids) const override;

	virtual Text do_create_text(Tense tense) const override;
	virtual void do_create() const override;

	virtual Text do_delete_text(Tense tense) const override;
	virtual void do_delete() const override;

	virtual Text do_add_etc_mdadm_text(Tense tense) const;
	virtual void do_add_etc_mdadm(const Actiongraph::Impl& actiongraph) const;

	virtual Text do_remove_etc_mdadm_text(Tense tense) const;
	virtual void do_remove_etc_mdadm(const Actiongraph::Impl& actiongraph) const;

	virtual Text do_reallot_text(ReallotMode reallot_mode, const BlkDevice* blk_device,
				     Tense tense) const override;
	virtual void do_reallot(ReallotMode reallot_mode, const BlkDevice* blk_device)
	    const override;
	virtual void do_reduce(const BlkDevice* blk_device) const;
	virtual void do_extend(const BlkDevice* blk_device) const;

    private:

	void calculate_region_and_topology();

	MdLevel md_level;

	MdParity md_parity;

	unsigned long chunk_size_k;

    };


    namespace Action
    {

	class AddEtcMdadm : public Modify
	{
	public:

	    AddEtcMdadm(sid_t sid)
		: Modify(sid) {}

	    virtual Text text(const Actiongraph::Impl& actiongraph, Tense tense) const override;
	    virtual void commit(const Actiongraph::Impl& actiongraph) const override;

	    virtual void add_dependencies(Actiongraph::Impl::vertex_descriptor v,
					  Actiongraph::Impl& actiongraph) const override;

	};


	class RemoveEtcMdadm : public Modify
	{
	public:

	    RemoveEtcMdadm(sid_t sid)
		: Modify(sid) {}

	    virtual Text text(const Actiongraph::Impl& actiongraph, Tense tense) const override;
	    virtual void commit(const Actiongraph::Impl& actiongraph) const override;

	};

    }


    bool compare_by_number(const Md* lhs, const Md* rhs);

}

#endif
