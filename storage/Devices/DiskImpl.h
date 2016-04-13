#ifndef STORAGE_DISK_IMPL_H
#define STORAGE_DISK_IMPL_H


#include "storage/Utils/Enum.h"
#include "storage/Devices/Disk.h"
#include "storage/Devices/PartitionableImpl.h"


namespace storage
{

    using namespace std;


    template <> struct DeviceTraits<Disk> { static const char* classname; };

    template <> struct EnumTraits<Transport> { static const vector<string> names; };
    template <> struct EnumTraits<DasdType> { static const vector<string> names; };
    template <> struct EnumTraits<DasdFormat> { static const vector<string> names; };


    class Disk::Impl : public Partitionable::Impl
    {
    public:

	Impl(const string& name)
	    : Partitionable::Impl(name), rotational(false), transport(Transport::UNKNOWN) {}

	Impl(const xmlNode* node);

	virtual const char* get_classname() const override { return DeviceTraits<Disk>::classname; }

	virtual Impl* clone() const override { return new Impl(*this); }

	virtual void save(xmlNode* node) const override;

	bool get_rotational() const { return rotational; }
	void set_rotational(bool rotational) { Impl::rotational = rotational; }

	Transport get_transport() const { return transport; }
	void set_transport(Transport transport) { Impl::transport = transport; }

	static vector<string> probe_disks(SystemInfo& systeminfo);

	virtual void probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo) override;

	virtual void add_create_actions(Actiongraph::Impl& actiongraph) const override;
	virtual void add_delete_actions(Actiongraph::Impl& actiongraph) const override;

	virtual bool equal(const Device::Impl& rhs) const override;
	virtual void log_diff(std::ostream& log, const Device::Impl& rhs_base) const override;

	virtual void print(std::ostream& out) const override;

	virtual void process_udev_ids(vector<string>& udev_ids) const override;

	Text do_create_text(Tense tense) const override;

    private:

	bool rotational;

	Transport transport;

    };


    bool compare_by_name(const Disk* lhs, const Disk* rhs);

}

#endif
