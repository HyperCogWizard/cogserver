/*
 * opencog/cogserver/server/WebServer.cc
 *
 * Copyright (C) 2022 Linas Vepstas
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifdef HAVE_OPENSSL

#include <string>
#include <openssl/sha.h>

#include <opencog/util/exceptions.h>
#include <opencog/util/Logger.h>
#include <opencog/util/misc.h>

#include <opencog/cogserver/server/CogServer.h>
#include <opencog/cogserver/server/WebServer.h>

using namespace opencog;

WebServer::WebServer(void)
{
}

WebServer::~WebServer()
{
}

// ==================================================================

// Called before any data is sent/received.
void WebServer::OnConnection(void)
{
	// If the the socket didn't connect as a websocet, then just
	// report the stats as an HTML page.
	if (not _got_websock_header)
	{
		Send (html_stats());
		throw SilentException();
	}

	// We expect teh URL to have the form


#if 0
	// Check for supported protocols
	if (0 != _url.compare("/json"))
	{
		logger().info("[WebServer] Unsupported request %s", line.c_str());
		Send("HTTP/1.1 404 Not Found\r\n"
			"Server: CogServer\r\n"
			"Content-Type: text/plain\r\n"
			"\r\n"
			"404 Not Found\n"
			"Cogserver currently supports only /json\n");
		throw SilentException();
	}
#endif

}

// Called for each newline-terminated line received.
void WebServer::OnLine(const std::string& line)
{
printf("duuuude websock recv'd >>%s<<\n", line.c_str());

	Send("yeah baby go for it\n");
}


// ==================================================================

std::string WebServer::html_stats(void)
{
	std::string response =
		"HTTP/1.1 200 OK\r\n"
		"Server: CogServer\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		"<!DOCTYPE html>"
		"<html>"
		"<head><title>CogServer Stats</title>"
		"  <meta charset=\"UTF-8\"></head>"
		"<body>"
		"<h2>CogServer Stats</h2>"
		"<pre>\n";
	response += cogserver().display_stats();
	response +=
		"</pre>"
		"<h2>Stats Legend</h2>"
		"<pre>";
	response += CogServer::stats_legend();
	response += "</pre></body></html>";

	return response;
}

#endif // HAVE_OPENSSL
// ==================================================================
