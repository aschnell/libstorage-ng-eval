#ifndef STORAGE_GPT_IMPL_H
#define STORAGE_GPT_IMPL_H


#include "storage/Devices/Gpt.h"
#include "storage/Devices/PartitionTableImpl.h"


namespace storage
{

    using namespace std;


    template <> struct DeviceTraits<Gpt> { static const char* classname; };


    class Gpt::Impl : public PartitionTable::Impl
    {
    public:

	Impl()
	    : PartitionTable::Impl(), enlarge(false) {}

	Impl(const xmlNode* node);

	virtual const char* get_classname() const override { return DeviceTraits<Gpt>::classname; }

	virtual string get_displayname() const override { return "gpt"; }

	virtual PtType get_type() const override { return PtType::GPT; }

	virtual Impl* clone() const override { return new Impl(*this); }

	virtual void save(xmlNode* node) const override;

	virtual void probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo) override;

	virtual void add_delete_actions(Actiongraph::Impl& actiongraph) const override;

	virtual bool equal(const Device::Impl& rhs) const override;
	virtual void log_diff(std::ostream& log, const Device::Impl& rhs_base) const override;

	virtual void print(std::ostream& out) const override;

	virtual unsigned int max_primary() const override;

	bool get_enlarge() const { return enlarge; }
	void set_enlarge(bool enlarge) { Impl::enlarge = enlarge; }

	virtual Region get_usable_region() const override;

	virtual Text do_create_text(Tense tense) const override;
	virtual void do_create() const override;

    private:

	bool enlarge;

    };

}

#endif
