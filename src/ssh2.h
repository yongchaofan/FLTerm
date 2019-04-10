//
// "$Id: ssh2.h 3982 2019-04-10 23:48:10 $"
//
//  sshHost sftpHost confHost
//
//	host implementation for terminal simulator
//    to be used with the Fl_Term widget.
//
// Copyright 2017-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//

#include "Hosts.h"
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <thread>
#include <mutex>

#ifndef _SSH2_H_
#define _SSH2_H_

struct TUNNEL
{
	int socket;
	char *localip;
	char *remoteip;
	unsigned short localport;
	unsigned short remoteport;
	LIBSSH2_CHANNEL *channel;
	TUNNEL *next;
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
	TUNNEL *tunnel_list = NULL;
	
	int wait_socket();
	int ssh_knownhost( int interactive=true );
	int ssh_authentication();
	void write_keys(const char *buf, int len);

	int scp_read_one(const char *rpath, const char *lpath);
	int scp_write_one(const char *lpath, const char *rpath);
	TUNNEL *tun_add( int tun_sock, LIBSSH2_CHANNEL *tun_channel, 
							char *localip, unsigned short localport, 
							char *remoteip, unsigned short remoteport);
	void tun_del(int tun_sock);
	void tun_closeall();
	void tun_worker(int forwardsock, LIBSSH2_CHANNEL *tun_channel);
	int tun_local(const char *lpath, const char *rpath);
	int tun_remote(const char *rpath,const char *lpath);

public:
	sshHost(const char *name); 

//	virtual const char *name();
	virtual int type() { return HOST_SSH; }
	virtual	int read();
	virtual int write(const char *buf, int len);
	virtual void send_size(int sx, int sy);
	virtual void keepalive(int interval);
	virtual void disconn();
//	virtual void connect();
	int scp_read(char *rpath, char *lpath);
	int scp_write(char *lpath, char *rpath);
	void tun(char *cmd);
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
	int sftp_get(char *src, char *dst);
	int sftp_put(char *src, char *dst);
	int sftp(char *p);
};

class confHost : public sshHost {
private: 
	LIBSSH2_CHANNEL *channel2;
	int msg_id;
	int interactive;
	
public:
	confHost(const char *name, int in=true );	//Nodes in NETable use 
												//non-interactive connection
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

#endif //_SSH2_H_