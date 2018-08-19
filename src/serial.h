//
// "$Id: serial.h 1129 2018-08-18 23:48:10 $"
//
// commHost implementation for terminal simulator
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

#ifndef _SERIAL_H_
#define _SERIAL_H_

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
	virtual void send_size(int sx, int sy){};
	virtual int read();
	virtual int write(const char *buf, int len);
	virtual void disconn();	
//	virtual void connect();
};

#endif //_SERIAL_H_