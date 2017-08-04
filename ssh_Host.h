//
// "$Id: ssh_Host.h 2364 2017-08-04 13:48:10 $"
//
// tcpHost and sshHost -- 
//
//	  host implementation for terminal simulator
//    to be used with the Fl_Term widget.
//
// Copyright 2017 by Yongchao Fan.
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
	#define socklen_t int
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#define closesocket close
#endif
#include <libssh2.h>
#include <pthread.h>
#include "Fl_Term.h"

#define TCP_FAILURE		-1
#define SSH_FAILURE		-2
#define CONF_FAILURE 	-4

class tcpHost : public Fl_Host {
protected:
	char hostname[256];
	short port;
	int sock;
	int stage;
	pthread_t reader_Id;
	unsigned char *telnet_options(unsigned char *buf);

public:
	tcpHost():Fl_Host() {
		stage = HOST_IDLE;
		port = 23;
	}
	void start( const char *address=NULL );
	void disp(const char *msg);
	int state() { return stage; }
	
	virtual int set_term(Fl_Term *pTerm);
	virtual int connect();
	virtual void read();
	virtual void write( const char *cmd );
	virtual void send_size(int sx, int sy){};
	virtual void disconn();	
};

class sshHost : public tcpHost {
private:
	char username[32];
	char password[32];
	char passphrase[32];
	int cursor;
	int bReturn;
	int bPassword;
	int get_keys( int bPass );
	
protected:
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;

public:
	sshHost() : tcpHost() {
		port = 22;
		*username = 0; 
		*password = 0;
	} 
	void set_user_pass( const char *user, const char *pass ) { 
		strncpy(username, user, 31); strncpy(password, pass, 31); 
	}
	void get_user_pass(const char **puser, const char **ppass) { 
		*puser = username; *ppass = password;
	}
	
	virtual int connect();
	virtual void read();
	virtual void write( const char *cmd );
	virtual void send_size(int sx, int sy);
//	virtual void disconn();				//from tcpHost
//	virtual int state();				//from tcpHost
//	virtual void disp(const char *msg);	//from tcpHost
};

int host_init();
void host_exit();
