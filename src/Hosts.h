//
// "$Id: Hosts.h 4340 2018-05-08 23:48:10 $"
//
// tcpHost sshHost sftpHost confHost 
//
//	  host implementation for terminal simulator
//    to be used with the Fl_Term widget.
//
// Copyright 2017-2018 by Yongchao Fan.
//
// This library is free software distributed under GUN LGPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//

#ifdef WIN32
	#include <winsock2.h>
	#include <windows.h>
	#include <direct.h>		//for _chdir
	#define socklen_t int
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#define closesocket close
	#define MAX_PATH 4096
#endif
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <mutex>


#ifndef _FL_HOST_H_
#define _FL_HOST_H_

#define HOST_COM	0
#define HOST_TCP	1
#define HOST_SSH	2
#define HOST_SFTP	3
#define HOST_CONF	4
#define HOST_FTPD	5
#define HOST_TFTPD	6

class Fl_Term;
class Fl_Host {
protected:
	Fl_Term *term;
	int bConnected;

public: 
	virtual ~Fl_Host(){}
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

class tcpHost : public Fl_Host {
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
private:
	char username[64];
	char password[64];
	char passphrase[64];
	
protected:
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	std::mutex mtx;
	int tunStarted;
	
	int wait_socket();
	int ssh_knownhost();
	int ssh_authentication();

	int scp_read_one(const char *rpath, const char *lpath);
	int scp_read(const char *rpath, const char *lpath);
	int scp_write_one(const char *lpath, const char *rpath);
	int scp_write(const char *lpath, const char *rpath);
	int tun_local(const char *lpath, const char *rpath);
	int tun_remote(const char *rpath,const char *lpath);

public:
	sshHost(const char *address); 

	void set_user_pass( const char *user, const char *pass ) { 
		if ( *user ) strncpy(username, user, 31); 
		if ( *pass ) strncpy(password, pass, 31); 
	}
	int scp(const char *cmd);
	int tunnel(const char *cmd);
	
//	virtual const char *name();
	virtual int type() { return HOST_SSH; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
	virtual void send_size(int sx, int sy);
//	virtual void disconn();				
};

class sftpHost : public sshHost {
private:
	LIBSSH2_SFTP *sftp_session;
	char realpath[1024];
	char homepath[1024];
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
	int sftp_put_one(char *src, char *dst);
	int sftp_put(char *src, char *dst);

public:
	sftpHost(const char *address) : sshHost(address) {}
//	virtual const char *name();
	virtual int type() { return HOST_SFTP; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len){}
	virtual void send_size(int sx, int sy){}
//	virtual void disconn();				
};

#define BUFLEN 65536
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
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len);
//	virtual void send_size(int sx, int sy);
//	virtual void disconn();
};

class tftpDaemon : public Fl_Host {
private:
	char rootDir[1023];
	int tftp_s0;
	int tftp_s1;

	int tftp_read(const char *fn);
	int tftp_write(const char *fn);

public:
	tftpDaemon(const char *root) {
		strncpy(rootDir, root, 1023);
	}
	virtual const char *name(){ return "tftpd"; }
	virtual int type() { return HOST_TFTPD; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len){};
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
};

#ifdef WIN32
class comHost : public Fl_Host {
protected:
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
#endif //WIN32
class ftpDaemon : public Fl_Host {
private:
	char rootDir[1023];
	int ftp_s0;
	int ftp_s1;
	int ftp_s2;
	int ftp_s3;
	void sock_send(const char *reply );

public:
	ftpDaemon(const char *root) {
		strncpy(rootDir, root, 1023);
	}
	virtual const char *name(){ return "ftpd"; }
	virtual int type() { return HOST_FTPD; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len){};
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
};

#endif //_FL_HOST_H_