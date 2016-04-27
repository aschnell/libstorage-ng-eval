

#include <iostream>

#include "storage/Devices/BlkDeviceImpl.h"
#include "storage/Devices/SwapImpl.h"
#include "storage/Devicegraph.h"
#include "storage/Action.h"
#include "storage/Utils/StorageDefines.h"
#include "storage/Utils/SystemCmd.h"
#include "storage/Utils/HumanString.h"
#include "storage/FreeInfo.h"


namespace storage
{

    using namespace std;


    const char* DeviceTraits<Swap>::classname = "Swap";


    Swap::Impl::Impl(const xmlNode* node)
	: Filesystem::Impl(node)
    {
    }


    ResizeInfo
    Swap::Impl::detect_resize_info() const
    {
	return ResizeInfo(true, 40 * KiB, 1 * TiB);
    }


    ContentInfo
    Swap::Impl::detect_content_info() const
    {
	return ContentInfo();
    }


    void
    Swap::Impl::do_create() const
    {
	const BlkDevice* blk_device = get_blk_device();

	blk_device->get_impl().wait_for_device();

	string cmd_line = MKSWAPBIN " -f " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("create swap failed"));
    }


    void
    Swap::Impl::do_mount(const Actiongraph::Impl& actiongraph, const string& mountpoint) const
    {
	const BlkDevice* blk_device = get_blk_device();

	string cmd_line = SWAPONBIN " --fixpgsz " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("swapon failed"));
    }


    void
    Swap::Impl::do_umount(const Actiongraph::Impl& actiongraph, const string& mountpoint) const
    {
	const BlkDevice* blk_device = get_blk_device();

	string cmd_line = SWAPOFFBIN " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("swapoff failed"));
    }


    void
    Swap::Impl::do_resize(ResizeMode resize_mode) const
    {
	const BlkDevice* blk_device = get_blk_device();

	blk_device->get_impl().wait_for_device();

	string cmd_line = MKSWAPBIN;
	if (!get_label().empty())
	    cmd_line += " -L " + quote(get_label());
	if (!get_uuid().empty())
	    cmd_line += " -U " + quote(get_uuid());
	cmd_line += " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("resize swap failed"));
    }

    void
    Swap::Impl::do_set_label() const
    {
	const BlkDevice* blk_device = get_blk_device();

	string cmd_line = SWAPLABELBIN " -L " + quote(get_label()) + " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("set-label swap failed"));
    }

    void
    Swap::Impl::do_set_uuid() const
    {
	const BlkDevice* blk_device = get_blk_device();

	string cmd_line = SWAPLABELBIN " -U " + quote(get_uuid()) + " " + quote(blk_device->get_name());
	cout << cmd_line << endl;

	SystemCmd cmd(cmd_line);
	if (cmd.retcode() != 0)
	    ST_THROW(Exception("set-uuid swap failed"));
    }

}
