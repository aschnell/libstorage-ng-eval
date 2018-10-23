/*
 * Copyright (c) 2017 SUSE LLC
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
 * with this program; if not, contact SUSE LLC.
 *
 * To contact SUSE LLC about this file by physical or electronic mail, you may
 * find current contact information at www.suse.com.
 */


#ifndef STORAGE_FORMATTER_PARTITION_H
#define STORAGE_FORMATTER_PARTITION_H


#include "storage/CompoundAction/Formatter.h"
#include "storage/Devices/Partition.h"
#include "storage/Filesystems/MountPoint.h"
#include "storage/Filesystems/BlkFilesystem.h"


namespace storage
{

    class CompoundAction::Formatter::Partition : public CompoundAction::Formatter
    {

    public:

	Partition(const CompoundAction::Impl* compound_action);

    private:

	Text text() const override;

	Text delete_text() const;

	Text create_encrypted_pv_text() const;
	Text create_pv_text() const;
	Text encrypted_pv_text() const;
	Text pv_text() const;

	Text create_encrypted_with_swap_text() const;
	Text create_with_swap_text() const;

	Text create_encrypted_with_fs_and_mount_point_text() const;
	Text create_encrypted_with_fs_text() const;
	Text create_encrypted_text() const;
	Text create_with_fs_and_mount_point_text() const;
	Text create_with_fs_text() const;
	Text create_text() const;
	Text encrypted_with_fs_and_mount_point_text() const;
	Text encrypted_with_fs_text() const;
	Text encrypted_text() const;
	Text fs_and_mount_point_text() const;
	Text fs_text() const;
	Text mount_point_text() const;

        // Predicates for better code readability

        bool creating()   const { return has_create<storage::Partition>();     }
        bool deleting()   const { return has_delete<storage::Partition>();     }
        bool encrypting() const { return has_create<storage::Encryption>();    }
        bool formatting() const { return has_create<storage::BlkFilesystem>(); }
        bool mounting()   const { return has_create<storage::MountPoint>();    }

        // Getters for better code readability

        string get_device_name()     const { return partition->get_name();        }
        string get_size()            const { return partition->get_size_string(); }
        string get_mount_point()     const { return get_created_filesystem()->get_mount_point()->get_path(); }
        string get_filesystem_type() const { return get_created_filesystem()->get_displayname(); }

    private:

	const storage::Partition * partition;

    };

}

#endif
