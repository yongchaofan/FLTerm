//
// "$Id: ssh2.h 3357 2018-06-30 23:48:10 $"
//
// scpHost sftpHost ftpDaemon tftpDaemon
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

#include "Hosts.h"
#include <libssh2_sftp.h>
#include <mutex>


#ifndef _SFTP_H_
#define _SFTP_H_

class scpHost : public sshHost {
protected:
	int tunStarted;
	int scp_read_one(const char *rpath, const char *lpath);
	int scp_write_one(const char *lpath, const char *rpath);
	int tun_local(const char *lpath, const char *rpath);
	int tun_remote(const char *rpath,const char *lpath);

public:
	scpHost(const char *address); 

	int scp(const char *cmd);
	int scp_read(const char *rpath, const char *lpath);
	int scp_write(const char *lpath, const char *rpath);
	int tunnel(const char *cmd);
	
//	virtual const char *name();
	virtual int type() { return HOST_SCP; }
//	virtual int connect();
//	virtual int read(char *buf, int len);
//	virtual void write(const char *buf, int len);
//	virtual void send_size(int sx, int sy);
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
	int sftp_put(char *src, char *dst);
	int sftp_put_one(char *src, char *dst);

public:
	sftpHost(const char *address) : sshHost(address) {}
//	virtual const char *name();
	virtual int type() { return HOST_SFTP; }
	virtual int connect();
	virtual int read(char *buf, int len);
	virtual void write(const char *buf, int len){}
	virtual void send_size(int sx, int sy){}
//	virtual void disconn();	
	int sftp(char *p);
};

class ftpDaemon : public Fan_Host {
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

class tftpDaemon : public Fan_Host {
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

#endif //_SFTP_H_