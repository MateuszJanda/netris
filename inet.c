﻿/*
 * Netris -- A free networked version of T*tris
 * Copyright (C) 1994,1995,1996  Mark H. Weaver <mhw@netris.org>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: inet.c,v 1.18 1996/02/09 08:22:13 mhw Exp $
 */

#include "netris.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#define HEADER_SIZE sizeof(netint2[2])

static MyEventType NetGenFunc(EventGenRec *gen, MyEvent *event);

static int sock = -1;
static EventGenRec netGen = { NULL, 0, FT_read, -1, NetGenFunc, EM_net };

static char netBuf[64];
static char traceBuf[256];
static int netBufSize, netBufGoal = HEADER_SIZE;
static int isServer, lostConn, gotEndConn;

ExtFunc void InitNet(void)
{
	lostConn = 0;
	gotEndConn = 0;
	AtExit(CloseNet);
}

ExtFunc int WaitForConnection(char *portStr)
{
	struct sockaddr_in addr;
	struct hostent *host;
	int sockListen;
	socklen_t addrLen;
	short port;
	int val1;
	struct linger val2;

	if (portStr)
		port = atoi(portStr);	/* XXX Error checking */
	else
		port = DEFAULT_PORT;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	sockListen = socket(AF_INET, SOCK_STREAM, 0);
	if (sockListen < 0)
		die("socket");
	val1 = 1;
	setsockopt(sockListen, SOL_SOCKET, SO_REUSEADDR,
			(void *)&val1, sizeof(val1));
	if (bind(sockListen, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		die("bind");
	if (listen(sockListen, 1) < 0)
		die("listen");
	addrLen = sizeof(addr);
	sock = accept(sockListen, (struct sockaddr *)&addr, &addrLen);
	if (sock < 0)
		die("accept");
	close(sockListen);
	val2.l_onoff = 1;
	val2.l_linger = 0;
	setsockopt(sock, SOL_SOCKET, SO_LINGER,
			(void *)&val2, sizeof(val2));
	netGen.fd = sock;
	strcpy(opponentHost, "???");
	if (addr.sin_family == AF_INET) {
		host = gethostbyaddr((void *)&addr.sin_addr,
				sizeof(struct in_addr), AF_INET);
		if (host) {
			strncpy(opponentHost, host->h_name, sizeof(opponentHost)-1);
			opponentHost[sizeof(opponentHost)-1] = 0;
		}
	}
	AddEventGen(&netGen);
	isServer = 1;
	return 0;
}

ExtFunc int InitiateConnection(char *hostStr, char *portStr)
{
	struct sockaddr_in addr;
	struct hostent *host;
	short port;
	int mySock;

	if (portStr)
		port = atoi(portStr);	/* XXX Error checking */
	else
		port = DEFAULT_PORT;
	host = gethostbyname(hostStr);
	if (!host)
		die("gethostbyname");
	assert(host->h_addrtype == AF_INET);
	strncpy(opponentHost, host->h_name, sizeof(opponentHost)-1);
	opponentHost[sizeof(opponentHost)-1] = 0;
 again:
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = host->h_addrtype;
	memcpy(&addr.sin_addr, host->h_addr, host->h_length);
	addr.sin_port = htons(port);
	mySock = socket(AF_INET, SOCK_STREAM, 0);
	if (mySock < 0)
		die("socket");
	if (connect(mySock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errno != ECONNREFUSED)
			die("connect");
		close(mySock);
		sleep(1);
		goto again;
	}
	netGen.fd = sock = mySock;
	AddEventGen(&netGen);
	return 0;
}

static MyEventType NetGenFunc(EventGenRec *gen, MyEvent *event)
{
	int result;
	short type, size;
	netint2 data[2];

	result = MyRead(sock, netBuf + netBufSize, netBufGoal - netBufSize);
	if (result < 0) {
		lostConn = 1;
		return E_lostConn;
	}
	netBufSize += result;
	if (netBufSize < netBufGoal)
		return E_none;
	memcpy(data, netBuf, sizeof(data));
	type = ntoh2(data[0]);
	size = ntoh2(data[1]);
	if (size >= sizeof(netBuf))
		fatal("Received an invalid packet (too large), possibly an attempt\n"
			  "  to exploit a vulnerability in versions before 0.52 !");
	netBufGoal = size;
	if (netBufSize < netBufGoal)
		return E_none;
	netBufSize = 0;
	netBufGoal = HEADER_SIZE;
	event->u.net.type = type;
	event->u.net.size = size - HEADER_SIZE;
	event->u.net.data = netBuf + HEADER_SIZE;

	TraceNetPacket(T_incoming, event);

	if (type == NP_endConn) {
		gotEndConn = 1;
		return E_lostConn;
	}
	else if (type == NP_byeBye) {
		lostConn = 1;
		return E_lostConn;
	}

	return E_net;
}

ExtFunc void CheckNetConn(void)
{
}

ExtFunc void SendPacket(NetPacketType type, int size, void *data)
{
	MyEvent event;
	event.type = E_net;
	event.u.net.type = type;
	event.u.net.size = size;
	event.u.net.data = data;
	TraceNetPacket(T_outgoing, &event);

	netint2 header[2];

	header[0] = hton2(type);
	header[1] = hton2(size + HEADER_SIZE);
	if (MyWrite(sock, header, HEADER_SIZE) != HEADER_SIZE)
		die("write");
	if (size > 0 && data && MyWrite(sock, data, size) != size)
		die("write");
}

ExtFunc void TraceNetPacket(TraceDir dir, MyEvent *event)
{
	const char* marker = (dir == T_incoming) ? "[>]" : "[<]";

	if (traceFile) {
		fprintf(traceFile, "%s %s\n", marker, StrNetPacketType(event));
	}
}

ExtFunc char *StrMyEventType(MyEvent *event)
{
	switch (event->type) {
	case E_none:
		return "E_none";
	case E_alarm:
		return "E_alarm";
	case E_key:
		return "E_key";
	case E_net:
		return "E_net";
	case E_lostConn:
		return "E_lostConn";
	case E_robot:
		return "E_robot";
	case E_lostRobot:
		return "E_lostRobot";
	}

	return "Unknown MyEventType";
}

ExtFunc char *StrNetPacketType(MyEvent *event)
{
	switch (event->u.net.type) {
	case NP_endConn:
		return "NP_endConn";
	case NP_giveJunk:
	{
		netint2 data;
		memcpy(&data, event->u.net.data, sizeof(data));
		sprintf(traceBuf, "NP_giveJunk count=%d", ntoh2(data));
		return traceBuf;
	}
	case NP_newPiece:
	{
		netint2 data;
		memcpy(&data, event->u.net.data, sizeof(data));
		short shapeNum = ntoh2(data);
		sprintf(traceBuf, "NP_newPiece shape=%d", shapeNum);
		return traceBuf;
	}
	case NP_down:
		return "NP_down";
	case NP_left:
		return "NP_left";
	case NP_right:
		return "NP_right";
	case NP_rotate:
		return "NP_rotate";
	case NP_drop:
		return "NP_drop";
	case NP_clear:
		return "NP_clear";
	case NP_insertJunk:
	{
		netint2 data[2];
		memcpy(data, event->u.net.data, sizeof(data));
		sprintf(traceBuf, "NP_insertJunk count=%d, column=%d", ntoh2(data[0]), ntoh2(data[1]));
		return traceBuf;
	}
	case NP_startConn:
		return "NP_startConn";
	case NP_userName:
		return "NP_userName";
	case NP_pause:
		return "NP_pause";
	case NP_version:
		return "NP_version";
	case NP_byeBye:
		return "NP_byeBye";
	case NP_boardDump:
	{
		if (event->u.net.size % sizeof(netint2) ||
			event->u.net.size * HEX_IN_CHAR >= sizeof(traceBuf)) {
			TracePrint("[!] Incorect size of board dump");
			die("Incorect size of board dump");
		}

		sprintf(traceBuf, "NP_boardDump size=%d dump=", event->u.net.size);
		char hex[10];
		netint2 data;
		for (int i = 0; i < event->u.net.size; i += sizeof(data)) {
			memcpy(&data, (char *)event->u.net.data + i, sizeof(data));
			sprintf(hex, "%04x", ntoh2(data));
			strcat(traceBuf, hex);
		}

		return traceBuf;
	}
	case NP_points:
	{
		netint2 data;
		memcpy(&data, event->u.net.data, sizeof(data));
		sprintf(traceBuf, "NP_points points=%d", ntoh2(data));
		return traceBuf;
	}
	}

	return "Unknown NetPacketType";
}

ExtFunc void CloseNet(void)
{
	MyEvent event;

	if (sock >= 0) {
		if (!lostConn) {
			SendPacket(NP_endConn, 0, NULL);
			if (isServer) {
				while (!lostConn)
					WaitMyEvent(&event, EM_net);
			}
			else {
				while (!gotEndConn)
					WaitMyEvent(&event, EM_net);
				SendPacket(NP_byeBye, 0, NULL);
			}
		}
		close(sock);
		sock = -1;
	}
	if (netGen.next)
		RemoveEventGen(&netGen);
}

ExtFunc void CloseTraceLog(void)
{
	if (traceFile) {
		fclose(traceFile);
		traceFile = NULL;
	}
}

/*
 * vi: ts=4 ai
 * vim: noai si
 */
