//
// "$Id: ftpd.h 1675 2018-08-18 23:48:10 $"
//
// ftpDaemon tftpDaemon
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

#ifndef _FTPD_H_
#define _FTPD_H_

class ftpDaemon : public Fan_Host {
private:
	char rootDir[1024];
	int ftp_s0;
	int ftp_s1;
	int ftp_s2;
	int ftp_s3;
	void sock_send(const char *reply );

public:
	ftpDaemon(const char *root) {
		strncpy(rootDir, root, 1023);
		rootDir[1023]=0;
	}
	virtual const char *name(){ return "ftpd"; }
	virtual int type() { return HOST_FTPD; }
	virtual int read();
	virtual int write(const char *buf, int len){return 0;};
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
//	virtual void connect();
};

class tftpDaemon : public Fan_Host {
private:
	char rootDir[1024];
	int tftp_s0;
	int tftp_s1;

	int tftp_read(const char *fn);
	int tftp_write(const char *fn);

public:
	tftpDaemon(const char *root) {
		strncpy(rootDir, root, 1023);
		rootDir[1023]=0;
	}
	virtual const char *name(){ return "tftpd"; }
	virtual int type() { return HOST_TFTPD; }
	virtual int read();
	virtual int write(const char *buf, int len){return 0;};
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
//	virtual void connect();
};

#endif //_FTPD_H_