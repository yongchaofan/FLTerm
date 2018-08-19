//
// "$Id: Hosts.h 5266 2018-08-18 21:12:15 $"
//
// tcpHost sshHost confHost sftpHost
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
#include <libssh2_sftp.h>
#include <pthread.h>
#include <thread>
#include <mutex>

#ifndef _FAN_HOST_H_
#define _FAN_HOST_H_

enum {  HOST_COM=1, HOST_TCP, HOST_SSH,
		HOST_SCP, HOST_SFTP, HOST_CONF,
		HOST_FTPD,HOST_TFTPD };

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
//	int connected() { return bConnected; }
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

class sshHost : public tcpHost {
protected:
	char username[64];
	char password[64];
	char passphrase[64];
	char homedir[MAX_PATH];
	int bGets;				//gets() function is waiting for return bing pressed
	int bReturn;			//true if during gets() return has been pressed
	int bPassword;
	int cursor;		
	char keys[64];

	std::mutex mtx;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	
	int wait_socket();
	int ssh_knownhost();
	int ssh_authentication();
	void write_keys(const char *buf, int len);
	void tun_worker(int forwardsock, LIBSSH2_CHANNEL *tun_channel);

public:
	sshHost(const char *name); 

//	virtual const char *name();
	virtual int type() { return HOST_SSH; }
	virtual	int read();
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy);
	virtual void disconn();
//	virtual void connect();
	int scp_read(const char *rpath, const char *lpath);
	int scp_write(const char *lpath, const char *rpath);
	int tun_local(const char *lpath, const char *rpath);
	int tun_remote(const char *rpath,const char *lpath);
	char *ssh_gets(const char *prompt, int echo);
};

class sftpHost : public sshHost {
private:
	LIBSSH2_SFTP *sftp_session;
	char realpath[MAX_PATH];
	char homepath[MAX_PATH];
protected:
	int sftp_lcd(char *path);
	int sftp_cd(char *path);
	int sftp_md(char *path);
	int sftp_rd(char *path);
	int sftp_ls(char *path, int ll=false);
	int sftp_rm(char *path);
	int sftp_ren(char *src, char *dst);
	int sftp_get_one(char *src, char *dst);
	int sftp_get(char *src, char *dst);
	int sftp_put(char *src, char *dst);
	int sftp_put_one(char *src, char *dst);

public:
	sftpHost(const char *name) : sshHost(name) {}
//	virtual const char *name();
	virtual int type() { return HOST_SFTP; }
	virtual int read();
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy){}
//	virtual void disconn();		//from sshHost	
//	virtual void connect();
	int sftp(char *p);
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
	virtual	int read();
	virtual int write(const char *buf, int len);
	int write2(const char *buf, int len);
	virtual void send_size(int sx, int sy){};
//	virtual void disconn();		//use from sshHost
//	virtual void connect();		
	void set_user_pass( const char *user, const char *pass ) { 
		if ( *user ) strncpy(username, user, 31); 
		if ( *pass ) strncpy(password, pass, 31); 
	}
};

#endif //_FAN_HOST_H_