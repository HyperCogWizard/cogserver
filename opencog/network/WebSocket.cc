/*
 * opencog/network/WebSocket.cc
 *
 * Copyright (C) 2022 Linas Vepstas
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// OpenSSL is needed for only one thing: the SHA1 hash of the websocket
// key. It is not used for anything else.
#ifdef HAVE_OPENSSL

#include <string>
#include <openssl/sha.h>

#include <opencog/util/exceptions.h>
#include <opencog/util/Logger.h>

#include "ServerSocket.h"

using namespace opencog;

// ==================================================================

/// Read from the websocket, decoding all framing and control bits,
/// and return the text data as a string. This returns one frame
/// at a time. No attempt is made to consolidate fragments.
std::string ServerSocket::get_websocket_line()
{
	// If we are here, then we are expecting a frame header.
	// Get frame and opcode
	unsigned char fop;
	boost::asio::read(*_socket, boost::asio::buffer(&fop, 1));

	// bool finbit = fop & 0x80;
	unsigned char opcode = fop & 0xf;

	// Handle pings
	while (9 == opcode)
	{
		// Mask and payload length
		unsigned char mpay;
		boost::asio::read(*_socket, boost::asio::buffer(&mpay, 1));
		bool maskbit = mpay & 0x80;
		unsigned char paylen = mpay & 0x7f;

		// Not expecting a mask in a ping
		if (maskbit)
		{
			logger().warn("Not expecting a mask in a websocket ping");
			throw SilentException();
		}

		char data[paylen+1];
		if (0 < paylen)
			boost::asio::read(*_socket, boost::asio::buffer(data, paylen));

		// Send a pong, copying the data.
		char header[2];
		header[0] = 0x8a;
		header[1] = (char) paylen;
		Send(boost::asio::const_buffer(header, 2));
		if (0 < paylen)
			Send(boost::asio::const_buffer(data, paylen));

		// And wait for the next frame...
		boost::asio::read(*_socket, boost::asio::buffer(&fop, 1));
		// finbit = fop & 0x80;
		opcode = fop & 0xf;
	}

	// Socket close message .. just quit.
	if (8 == opcode)
		throw SilentException();

	// We only support text data.
	if (1 != opcode)
	{
		logger().warn("Not expecting binary websocket data; opcode=%d",
			opcode);
		throw SilentException();
	}

	// mask and payload length
	unsigned char mpay;
	boost::asio::read(*_socket, boost::asio::buffer(&mpay, 1));
	bool maskbit = mpay & 0x80;
	size_t paylen = mpay & 0x7f;

	// It is an error if the maskbit is not set. Bail out.
	if (not maskbit)
		throw SilentException();

	uint32_t mask;
	boost::asio::read(*_socket, boost::asio::buffer(&mask, 4));

	// Use malloc inside of std::string to get a buffer.
	std::string blob;
	blob.resize(paylen+1);
	char* data = blob.data();
	boost::asio::read(*_socket, boost::asio::buffer(data, paylen));

	// Bulk unmask the data, using XOR.
	uint32_t *dp = (uint32_t *) data;
	size_t i=0;
	while (i < paylen)
	{
		*dp = *dp ^ mask;
		++dp;
		i += 4;
	}

	// Unmask any remaining bytes.
	i -= 4;
	for (unsigned int j=0; j<i%4; j++)
		data[i+j] = data[i+j] ^ ((mask >> (8*j)) & 0xff);

	// Null-terminated string.
	data[paylen] = 0x0;

	// We're not actually going to use a line protocol, when we're
	// using websockets. If teh user wants to search for newline
	// chars in the datastream, they are welcome to. We're not
	// going to futz with that.
	return blob;
}

// ==================================================================

/// Given a char buffer of data (possibly including nulls)
/// return the base64 encoding of it.  Found on stackexchange.
static std::string base64_encode(unsigned char* buf, int len)
{
	std::string out;

	unsigned int val = 0;
	int valb = -6;
	for (int i=0; i<len; i++)
	{
		unsigned char c = buf[i];
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0)
		{
			out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val>>valb)&0x3F]);
			valb -= 6;
		}
	}
	if (valb > -6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val<<8)>>(valb+8))&0x3F]);
	while (out.size()%4) out.push_back('=');
	return out;
}

/// Perform the websockets handshake. That is, listen for the HTTP
/// header, verify that it has an `Upgrade: websocket` line in it,
/// and then do the magic-key exchange, etc. Upon compltion, the
/// socket is ready to send and receive websocket frames.
void ServerSocket::HandshakeLine(const std::string& line)
{
	// The very first HTTP line.
	if (not _got_first_line)
	{
		_got_first_line = true;

		if (0 != line.compare(0, 4, "GET "))
		{
			Send("HTTP/1.1 501 Not Implemented\r\n"
				"Server: CogServer\r\n"
				"\r\n");
			throw SilentException();
		}
		_url = line.substr(4, line.find(" ", 4) - 4);
		return;
	}

	// If the line-size is zero, then we've reached the end of
	// header sent by the client.
	if (not _got_http_header and 0 == line.size())
	{
		_got_http_header = true;
	}

	// Extract stuff from the header the client is sending us.
	if (not _got_http_header)
	{
		static const char* upg = "Upgrade: websocket";
		if (0 == line.compare(0, strlen(upg), upg))
			{ _got_websock_header = true; return; }

		static const char* key = "Sec-WebSocket-Key: ";
		if (0 == line.compare(0, strlen(key), key))
			{ _webkey = line.substr(strlen(key)); return; }

		return;
	}

	// If we are here, then the full HTTP header was received. This
	// is enough to get started: call the user's OnConnection()
	// method. The user is supposed to check two things:
	// (a) Do they like the URL in the header? If not, they
	//     should send some response e.g. 404 Not Found
	//     and then `throw SilentException()` to close the sock.
	// (b) Was an actual WebSocket negotiated? If not, then the
	//     user should send some response, e.g. 200 OK and some
	//     HTML, and then `throw SilentException()` to close the
	//     sock.
	OnConnection();

	// In case the user blew it above, we close the sock.
	if (not _got_websock_header)
		throw SilentException();

	// If we are here, we've received an HTTP header, and it
	// as a WebSocket header. Do the websocket reply.
	_webkey += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	unsigned char hash[SHA_DIGEST_LENGTH]; // == 20
	memset(hash, 0, SHA_DIGEST_LENGTH);
	SHA1((const unsigned char*) _webkey.c_str(), _webkey.size(), hash);
	std::string b64hash = base64_encode(hash, SHA_DIGEST_LENGTH);

	std::string response =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: ";
	response += b64hash;
	response +=
		"\r\n"
		"\r\n";

	Send(response);

	// After this point, websockets will send frames.
	// Need to change the mode to work with frames.
	_do_frame_io = true;
}

#endif // HAVE_OPENSSL
// ==================================================================
