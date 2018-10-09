//
// "$Id: Hosts.h 2951 2018-10-08 21:12:15 $"
//
// Fan_Host comHost tcpHost
//
//	  host implementation for terminal simulator
//    to be used with the Fl_Term widget.
//
// Copyright 2017-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//

#ifdef WIN32
	#include <direct.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#define socklen_t int
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#define closesocket close
	#define MAX_PATH 4096
#endif
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#ifndef _FAN_HOST_H_
#define _FAN_HOST_H_

enum {  HOST_COM=1, HOST_TCP, HOST_SSH, HOST_SFTP, HOST_CONF };

typedef void ( host_callback )(void *, const char *, int);

class Fan_Host {
protected:
	int bConnected;
	int bEcho;
	void *host_data_;
	host_callback* host_cb;
	pthread_t readerThread;

public: 
	Fan_Host()
	{
		bConnected = bEcho = false;
		readerThread = 0;
		host_cb = NULL;
	}
	virtual ~Fan_Host(){}
	virtual const char *name()					=0;
	virtual int type()							=0;
	virtual	void connect();
	virtual	int read()							=0;
	virtual	int write(const char *buf, int len)	=0;
	virtual void send_size(int sx, int sy)		=0;
	virtual	void disconn()						=0;	

	void callback(host_callback *cb, void *data)
	{
		host_cb = cb;
		host_data_ = data;
	}
	void do_callback(const char *buf, int len)
	{
		host_cb(host_data_, buf, len);
	}
	void *host_data() { return host_data_; }
	int echo() { return bEcho; }
	void echo(int e) { bEcho = e; }
	void print(const char *fmt, ...);
};

class comHost : public Fan_Host {
private:
	char portname[64];
	char settings[64];
#ifdef WIN32
	HANDLE hCommPort;
	HANDLE hExitEvent;
#else
	int ttySfd;
#endif //WIN32

public:
	comHost(const char *address);
	
	virtual const char *name()
	{ 
#ifdef WIN32
		return portname+4;
#else
		return portname;
#endif //WIN32
	}
	virtual int type() { return HOST_COM; }
	virtual int read();
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
//	virtual void connect();
};

class tcpHost : public Fan_Host {
protected:
	char hostname[64];
	short port;
	int sock;
	int tcp();
	unsigned char *telnet_options(unsigned char *buf);

public:
	tcpHost(const char *name);
	
	virtual const char *name(){ return hostname; }
	virtual int type() { return HOST_TCP; }
	virtual	int read();
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
//	virtual void connect();
};

#endif //_FAN_HOST_H_