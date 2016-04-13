
#include <iostream>

#include "storage/Devices/GptImpl.h"
#include "storage/Devices/PartitionableImpl.h"
#include "storage/Devicegraph.h"
#include "storage/Action.h"
#include "storage/Utils/StorageTmpl.h"
#include "storage/Utils/XmlFile.h"
#include "storage/SystemInfo/SystemInfo.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/StorageDefines.h"


namespace storage
{

    using namespace std;


    const char* DeviceTraits<Gpt>::classname = "Gpt";


    Gpt::Impl::Impl(const xmlNode* node)
	: PartitionTable::Impl(node), enlarge(false)
    {
	getChildValue(node, "enlarge", enlarge);
    }


    void
    Gpt::Impl::probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo)
    {
	PartitionTable::Impl::probe_pass_1(probed, systeminfo);

	const Partitionable* partitionable = get_partitionable();

	const Parted& parted = systeminfo.getParted(partitionable->get_name());

	if (parted.getGptEnlarge())
	    enlarge = true;
    }


    void
    Gpt::Impl::save(xmlNode* node) const
    {
	PartitionTable::Impl::save(node);

	setChildValueIf(node, "enlarge", enlarge, enlarge);
    }


    void
    Gpt::Impl::add_delete_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	actions.push_back(new Action::Delete(get_sid(), true));

	actiongraph.add_chain(actions);
    }


    bool
    Gpt::Impl::equal(const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	if (!PartitionTable::Impl::equal(rhs))
	    return false;

	return enlarge == rhs.enlarge;
    }


    void
    Gpt::Impl::log_diff(std::ostream& log, const Device::Impl& rhs_base) const
    {
	const Impl& rhs = dynamic_cast<const Impl&>(rhs_base);

	PartitionTable::Impl::log_diff(log, rhs);

	storage::log_diff(log, "enlarge", enlarge, rhs.enlarge);
    }


    void
    Gpt::Impl::print(std::ostream& out) const
    {
	PartitionTable::Impl::print(out);

	if (get_enlarge())
	    out << " enlarge";
    }


    unsigned int
    Gpt::Impl::max_primary() const
    {
	return min(128U, get_partitionable()->get_range());
    }


    Text
    Gpt::Impl::do_create_text(Tense tense) const
    {
	const Partitionable* partitionable = get_partitionable();

	return sformat(_("Create GPT on %1$s"), partitionable->get_displayname().c_str());
    }


    void
    Gpt::Impl::do_create() const
    {
	const Partitionable* partitionable = get_partitionable();

	string cmd_line = PARTEDBIN " --script " + quote(partitionable->get_name()) + " mklabel gpt";
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("create gpt failed"));
    }

}
