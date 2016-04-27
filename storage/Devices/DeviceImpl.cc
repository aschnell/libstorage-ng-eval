

#include "storage/Devices/DeviceImpl.h"
#include "storage/Devicegraph.h"
#include "storage/Action.h"
#include "storage/Utils/XmlFile.h"
#include "storage/Utils/StorageTmpl.h"


namespace storage
{

    using namespace std;


    sid_t Device::Impl::global_sid = 42;	// just a random number ;)


    Device::Impl::Impl()
	: sid(global_sid++), devicegraph(nullptr), vertex(), userdata()
    {
    }


    Device::Impl::Impl(const xmlNode* node)
	: sid(0), devicegraph(nullptr), vertex(), userdata()
    {
	if (!getChildValue(node, "sid", sid))
	    ST_THROW(Exception("no sid"));
    }


    bool
    Device::Impl::operator==(const Impl& rhs) const
    {
	if (typeid(*this) != typeid(rhs))
	    return false;

	return equal(rhs);
    }


    void
    Device::Impl::probe_pass_1(Devicegraph* probed, SystemInfo& systeminfo)
    {
    }


    void
    Device::Impl::probe_pass_2(Devicegraph* probed, SystemInfo& systeminfo)
    {
    }


    void
    Device::Impl::save(xmlNode* node) const
    {
	setChildValue(node, "sid", sid);
    }


    void
    Device::Impl::set_devicegraph_and_vertex(Devicegraph* devicegraph,
					     Devicegraph::Impl::vertex_descriptor vertex)
    {
	Impl::devicegraph = devicegraph;
	Impl::vertex = vertex;

	const Device* device = devicegraph->get_impl()[vertex];
	if (&device->get_impl() != this)
	    ST_THROW(LogicException("wrong vertex for back references"));
    }


    Devicegraph*
    Device::Impl::get_devicegraph()
    {
	if (!devicegraph)
	    ST_THROW(LogicException("not part of a devicegraph"));

	return devicegraph;
    }


    const Devicegraph*
    Device::Impl::get_devicegraph() const
    {
	if (!devicegraph)
	    ST_THROW(LogicException("not part of a devicegraph"));

	return devicegraph;
    }


    Devicegraph::Impl::vertex_descriptor
    Device::Impl::get_vertex() const
    {
	if (!devicegraph)
	    ST_THROW(LogicException("not part of a devicegraph"));

	return vertex;
    }


    bool
    Device::Impl::has_children() const
    {
	return devicegraph->get_impl().num_children(vertex) > 0;
    }


    size_t
    Device::Impl::num_children() const
    {
	return devicegraph->get_impl().num_children(vertex);
    }


    bool
    Device::Impl::has_parents() const
    {
	return devicegraph->get_impl().num_parents(vertex) > 0;
    }


    size_t
    Device::Impl::num_parents() const
    {
	return devicegraph->get_impl().num_parents(vertex);
    }


    void
    Device::Impl::remove_descendants()
    {
	Devicegraph::Impl& devicegraph_impl = devicegraph->get_impl();

	for (Devicegraph::Impl::vertex_descriptor descendant : devicegraph_impl.descendants(vertex, false))
	    devicegraph_impl.remove_vertex(descendant);
    }


    void
    Device::Impl::add_create_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	actions.push_back(new Action::Create(sid));

	actiongraph.add_chain(actions);
    }


    void
    Device::Impl::add_modify_actions(Actiongraph::Impl& actiongraph, const Device* lhs) const
    {
	add_reallot_actions(actiongraph, lhs);
    }


    void
    Device::Impl::add_reallot_actions(Actiongraph::Impl& actiongraph, const Device* lhs) const
    {
	set<sid_t> lhs_sids;
	for (const Device* device : lhs->get_parents())
	    lhs_sids.insert(device->get_sid());

	set<sid_t> rhs_sids;
	for (const Device* device : get_device()->get_parents())
	    rhs_sids.insert(device->get_sid());

	vector<sid_t> added_sids;
	set_difference(rhs_sids.begin(), rhs_sids.end(), lhs_sids.begin(), lhs_sids.end(),
		       back_inserter(added_sids));

	vector<sid_t> removed_sids;
	set_difference(lhs_sids.begin(), lhs_sids.end(), rhs_sids.begin(), rhs_sids.end(),
		       back_inserter(removed_sids));

	for (sid_t sid : added_sids)
	{
	    const BlkDevice* blk_device = to_blk_device(actiongraph.get_devicegraph(RHS)->find_device(sid));
	    actiongraph.add_vertex(new Action::Reallot(get_sid(), ReallotMode::EXTEND, blk_device));
	}

	for (sid_t sid : removed_sids)
	{
	    const BlkDevice* blk_device = to_blk_device(actiongraph.get_devicegraph(LHS)->find_device(sid));
	    actiongraph.add_vertex(new Action::Reallot(get_sid(), ReallotMode::REDUCE, blk_device));
	}
    }


    void
    Device::Impl::add_delete_actions(Actiongraph::Impl& actiongraph) const
    {
	vector<Action::Base*> actions;

	actions.push_back(new Action::Delete(sid));

	actiongraph.add_chain(actions);
    }


    bool
    Device::Impl::equal(const Impl& rhs) const
    {
	return sid == rhs.sid;
    }


    void
    Device::Impl::log_diff(std::ostream& log, const Impl& rhs) const
    {
	storage::log_diff(log, "sid", sid, rhs.sid);
    }


    void
    Device::Impl::print(std::ostream& out) const
    {
	out << get_classname() << " sid:" << get_sid()
	    << " displayname:" << get_displayname();
    }


    Text
    Device::Impl::do_create_text(Tense tense) const
    {
	return _("error: stub function called");
    }


    void
    Device::Impl::do_create() const
    {
	throw std::logic_error("stub function called");
    }


    Text
    Device::Impl::do_delete_text(Tense tense) const
    {
	return _("error: stub function called");
    }


    void
    Device::Impl::do_delete() const
    {
	throw std::logic_error("stub function called");
    }


    Text
    Device::Impl::do_resize_text(ResizeMode resize_mode, const Device* lhs, Tense tense) const
    {
	return _("error: stub function called");
    }


    void
    Device::Impl::do_resize(ResizeMode resize_mode) const
    {
	throw std::logic_error("stub function called");
    }


    Text
    Device::Impl::do_reallot_text(ReallotMode reallot_mode, const BlkDevice* blk_device, Tense tense) const
    {
	return _("error: stub function called");
    }


    void
    Device::Impl::do_reallot(ReallotMode reallot_mode, const BlkDevice* blk_device) const
    {
	throw std::logic_error("stub function called");
    }


    namespace Action
    {

	Text
	Resize::text(const Actiongraph::Impl& actiongraph, Tense tense) const
	{
	    const Device* device_lhs = get_device_lhs(actiongraph);
	    const Device* device_rhs = get_device_rhs(actiongraph);
	    return device_rhs->get_impl().do_resize_text(resize_mode, device_lhs, tense);
	}


	void
	Resize::commit(const Actiongraph::Impl& actiongraph) const
	{
	    const Device* device = get_device_rhs(actiongraph);
	    device->get_impl().do_resize(resize_mode);
	}


	Text
	Reallot::text(const Actiongraph::Impl& actiongraph, Tense tense) const
	{
	    const Device* device_rhs = get_device_rhs(actiongraph);
	    return device_rhs->get_impl().do_reallot_text(reallot_mode, blk_device, tense);
	}


	void
	Reallot::commit(const Actiongraph::Impl& actiongraph) const
	{
	    const Device* device_rhs = get_device_rhs(actiongraph);
	    device_rhs->get_impl().do_reallot(reallot_mode, blk_device);
	}

    }

}
