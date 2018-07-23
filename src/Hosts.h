//
// "$Id: Hosts.h 3911 2018-06-29 13:48:10 $"
//
// tcpHost sshHost confHost
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
#endif
#include <stdio.h>
#include <string.h>
#include <libssh2.h>
#include <mutex>

#ifndef _FAN_HOST_H_
#define _FAN_HOST_H_

enum {  HOST_COM=1, HOST_TCP, HOST_SSH,
		HOST_SCP, HOST_SFTP, HOST_CONF,
		HOST_FTPD,HOST_TFTPD };

typedef const char* (*gets_callback_t)(void *, int);
typedef void (*parse_callback_t)(void *, const char *, int);

class Fan_Host {
protected:
	int bConnected;
	void *puts_data;
	void *gets_data;
	parse_callback_t puts_cb;
	gets_callback_t gets_cb;

public: 
	Fan_Host();
	virtual ~Fan_Host(){}
	virtual const char *name()					=0;
	virtual int type()							=0;
	virtual	int connect()						=0;
	virtual	int read(parse_callback_t, void *)	=0;
	virtual	int write(const char *buf, int len)	=0;
	virtual void send_size(int sx, int sy)		=0;
	virtual	void disconn()						=0;	

	int connected() { return bConnected; }
	void gets_callback(gets_callback_t cb, void *data);
	void puts_callback(parse_callback_t cb, void *data);
	void print(const char *fmt, ...);
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
	virtual int connect();
	virtual	int read(parse_callback_t, void *);
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
};

class sshHost : public tcpHost {
protected:
	char username[64];
	char password[64];
	char passphrase[64];
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	std::mutex mtx;
	int tunStarted;
	char path[1024];
	
	int wait_socket();
	int ssh_knownhost();
	int ssh_authentication();

public:
	sshHost(const char *name); 

//	virtual const char *name();
	virtual int type() { return HOST_SSH; }
	virtual int connect();
	virtual	int read(parse_callback_t, void *);
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy);
//	virtual void disconn();			//use from tcpHost	
	void set_user_pass( const char *user, const char *pass ) { 
		if ( *user ) strncpy(username, user, 31); 
		if ( *pass ) strncpy(password, pass, 31); 
	}
	int scp_read(const char *rpath, const char *lpath);
	int scp_write(const char *lpath, const char *rpath);
	int tun(const char *cmd);
	int tun_local(const char *lpath, const char *rpath);
	int tun_remote(const char *rpath,const char *lpath);
};

#define BUFLEN 65536*2-1
class confHost : public sshHost {
private: 
	LIBSSH2_CHANNEL *channel2;
	char notif[BUFLEN+1];
	char reply[BUFLEN+1];
	int rd;
	int rd2;
	int msg_id;

public:
	confHost(const char *name);

//	virtual const char *name();
	virtual int type() { return HOST_CONF; }
	virtual int connect();
	virtual	int read(parse_callback_t, void *);
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
//	virtual void disconn();		//use from tcpHost
	int write2(const char *buf, int len);
};

#endif //_FAN_HOST_H_