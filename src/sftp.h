//
// "$Id: sftp.h 1500 2018-06-30 23:48:10 $"
//
//  sftpHost 
//
//	  sftp host implementation for terminal simulator
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

#ifndef _SFTP_H_
#define _SFTP_H_

class sftpHost : public sshHost {
private:
	LIBSSH2_SFTP *sftp_session;
	char realpath[4096];
	char homepath[4096];
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
	virtual int connect();
	virtual int read(parse_callback_t, void *);
	virtual int write(const char *buf, int len){return 0;}
	virtual void send_size(int sx, int sy){}
	virtual void disconn(){ bConnected=false; };	
	int sftp(char *p);
};

#endif //_SFTP_H_