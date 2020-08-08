//
// "$Id: Hosts.h 3773 2020-08-01 21:12:15 $"
//
// HOST pipeHost comHost tcpHost ftpd tftpd
//
//	  host implementation for terminal simulator
//    to be used with the Fl_Term widget.
//
// Copyright 2017-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm2/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm2/issues/new
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
#include <thread>

#ifndef _HOST_H_
#define _HOST_H_

enum {  HOST_NULL=0, HOST_PIPE, HOST_COM, HOST_TCP,
		HOST_SSH, HOST_SFTP, HOST_CONF }; 
enum {  HOST_IDLE=0, HOST_CONNECTING, HOST_AUTHENTICATING, HOST_CONNECTED };
typedef void ( host_callback )(void *, const char *, int);

class HOST {
protected:
	int state;
	void *host_data_;
	host_callback* host_cb;
	std::thread reader;

public:
	HOST()
	{
		host_cb = NULL;
		state = HOST_IDLE;
	}
	virtual ~HOST(){}
	virtual	void connect();
	virtual const char *name()	{ return ""; }
	virtual int type()			{ return HOST_NULL; }
	virtual	int read()			{ return 0; }
	virtual	int write(const char *buf, int len)	{ return 0; }
	virtual	void disconn()		{}
	virtual void command(const char *cmd){}
	virtual void send_size(int sx, int sy){}
	virtual void send_file(char *src, char *dst){}
	virtual char *gets(const char *prompt, int echo){return NULL;}
	virtual void set_user_pass( const char *user, const char *pass ){};

	void callback(host_callback *cb, void *data)
	{
		host_cb = cb;
		host_data_ = data;
	}
	void do_callback(const char *buf, int len)
	{
		host_cb(host_data_, buf, len);
	}
	int live() { return reader.joinable(); }
	int status() { return state; }
	void status(int s) { state = s; }
	void print(const char *fmt, ...);
};

class comHost : public HOST {
private:
	char portname[64];
	char settings[64];
#ifdef WIN32
	HANDLE hCommPort;
	HANDLE hExitEvent;
#else
	int ttySfd;
#endif //WIN32

	char xmodem_buf[133];
	unsigned char xmodem_blk;
	int xmodem_timeout;
	bool bXmodem, xmodem_crc, xmodem_started;
	FILE *xmodem_fp;
	void block_crc();
	void xmodem_block();
	void xmodem_send();
	void xmodem_recv(char op);

public:
	comHost(const char *address);

//	virtual void connect();
	virtual const char *name() { return portname+4; }
	virtual int type() { return HOST_COM; }
	virtual int read();
	virtual int write(const char *buf, int len);
	virtual void disconn();
	virtual void command(const char *cmd);
	virtual void send_file(char *src, char *dst);
};

class pipeHost : public HOST {
private:
	char cmdline[256];
#ifdef WIN32
	HANDLE hStdioRead;
	HANDLE hStdioWrite;
	PROCESS_INFORMATION piStd;
#else
	int pty_master;		//pty master
	int pty_slave;		//pty slave
	pid_t shell_pid;
#endif
public:
	pipeHost(const char *name);
	~pipeHost(){}

//	virtual void connect();
	virtual const char *name(){ return cmdline; }
	virtual int type() { return HOST_PIPE; }
	virtual	int read();
	virtual int write(const char *buf, int len);
	virtual void disconn();
	virtual void send_size(int sx, int sy);
};

class tcpHost : public HOST {
protected:
	char hostname[128];
	short port;
	int sock;
	int tcp();

public:
	tcpHost(const char *name);
	~tcpHost(){}

//	virtual void connect();
	virtual const char *name(){ return hostname; }
	virtual int type() { return HOST_TCP; }
	virtual	int read();
	virtual int write(const char *buf, int len);
	virtual void disconn();
};
#endif//_HOST_H_