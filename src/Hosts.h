//
// "$Id: Hosts.h 3910 2018-06-29 13:48:10 $"
//
// tcpHost sshHost confHost comHost
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
#include <libssh2.h>
#include <mutex>

#ifndef _FAN_HOST_H_
#define _FAN_HOST_H_

enum {  HOST_COM=1, HOST_TCP, HOST_SSH,
		HOST_SCP, HOST_SFTP, HOST_CONF,
		HOST_FTPD,HOST_TFTPD };
		
class Fl_Term;

class Fan_Host {
protected:
	Fl_Term *term;
	int bConnected;

public: 
	virtual ~Fan_Host(){}
	virtual const char *name()					=0;
	virtual int type()							=0;
	virtual	int connect()						=0;
	virtual	int read(char *buf, int len)		=0;
	virtual	void write(const char *buf, int len)=0;
	virtual void send_size(int sx, int sy)		=0;
	virtual	void disconn()						=0;	

	void set_term(Fl_Term *pTerm){ term=pTerm; }
	int connected() { return bConnected; }
	void print(const char *fmt, ...);
};

class tcpHost : public Fan_Host {
protected:
	char hostname[64];
	short port;
	int sock;
	unsigned char *telnet_options(unsigned char *buf);
	int tcp();

public:
	tcpHost(const char *address);
	
	virtual const char *name(){ return hostname; }
	virtual int type() { return HOST_TCP; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
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
	
	int wait_socket();
	int ssh_knownhost();
	int ssh_authentication();

public:
	sshHost(const char *address); 

	void set_user_pass( const char *user, const char *pass ) { 
		if ( *user ) strncpy(username, user, 31); 
		if ( *pass ) strncpy(password, pass, 31); 
	}
//	virtual const char *name();
	virtual int type() { return HOST_SSH; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
	virtual void send_size(int sx, int sy);
//	virtual void disconn();				
};

#define BUFLEN 65536*2
class confHost : public sshHost {
private: 
	LIBSSH2_CHANNEL *channel2;
	char notif[BUFLEN];
	char reply[BUFLEN];
	int rd;
	int rd2;
	int msg_id;
public:
	confHost(const char *address) : sshHost(address)
	{
		if ( port==22 ) port = 830;
	}	

//	virtual const char *name();
	virtual int type() { return HOST_CONF; }
	virtual int connect();
	virtual int connect2();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
//	virtual void send_size(int sx, int sy);
//	virtual void disconn();
};

#ifdef WIN32
class comHost : public Fan_Host {
private:
	char portname[32];
	char settings[32];
	HANDLE hCommPort;
	HANDLE hExitEvent;

public:
	comHost(const char *address);
	
	virtual const char *name(){ return portname+4; }
	virtual int type() { return HOST_COM; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
};

char *SHA( char *msg );
#endif //WIN32

#endif //_FAN_HOST_H_