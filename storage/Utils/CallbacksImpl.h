/*
 * Copyright (c) [2018-2020] SUSE LLC
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


#ifndef STORAGE_CALLBACKS_IMPL_H
#define STORAGE_CALLBACKS_IMPL_H


#include "storage/Utils/Callbacks.h"


namespace storage
{

    class Text;
    class Exception;
    class ProbeCallbacksV2;


    /**
     * Calls the begin() and end() functions of the callbacks in the
     * constructor respectively destructor.
     */
    class CallbacksGuard
    {
    public:

	CallbacksGuard(const Callbacks* callbacks);
	~CallbacksGuard();

    private:

	const Callbacks* callbacks;

    };


    /**
     * Call the message callback of callbacks.
     */
    void
    message_callback(const Callbacks* callbacks, const Text& message);


    /**
     * Call the error callback of callbacks and handle return value.
     */
    void
    error_callback(const Callbacks* callbacks, const Text& message);


    /**
     * Call the error callback of callbacks and handle return value.
     *
     * Also calls ST_CAUGHT().
     */
    void
    error_callback(const Callbacks* callbacks, const Text& message, const Exception& exception);


    /**
     * Call the missing_command callback of callback and handle return value.
     *
     * Also calls ST_CAUGHT().
     */
    void
    missing_command_callback(const ProbeCallbacksV2* callbacks, const Text& message, const std::string& command,
			     uint64_t features, const Exception& exception);

}


#endif
