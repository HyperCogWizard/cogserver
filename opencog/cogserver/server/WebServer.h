/*
 * opencog/cogserver/server/WebServer.h
 *
 * Copyright (C) 2022 Linas Vepstas
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef _OPENCOG_WEB_SERVER_H
#define _OPENCOG_WEB_SERVER_H

#include <condition_variable>
#include <mutex>
#include <string>

#include <opencog/network/ServerSocket.h>

namespace opencog
{
/** \addtogroup grp_server
 *  @{
 */

/**
 * This class implements a super-simple WebSockets server.
 */
class WebServer : public ServerSocket
{
private:

protected:

public:
    WebServer(void);
    ~WebServer();

}; // class

/** @}*/
}  // namespace

#endif // _OPENCOG_WEB_SERVER_H