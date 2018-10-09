//
// "$Id: Hosts.cxx 8522 2018-10-08 22:15:10 $"
//
// Fan_Host tcpHost comHost
//
//	  host implementation for terminal simulator
//    used with the Fl_Term widget in flTerm
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#ifndef WIN32
	#include <termios.h>
	#include <fcntl.h>
	#include <errno.h>
#endif
#include "Hosts.h"

void* host_reader(void *p)
{
	((Fan_Host *)p)->read();
	return NULL;
}
void Fan_Host::connect()
{
	if ( readerThread==0 ) 
		pthread_create(&readerThread, NULL, host_reader, this);
}
void Fan_Host::print( const char *fmt, ... ) 
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	do_callback(buff, len);
	do_callback("\033[37m",5);
}
static const char *errmsgs[]={
	"Disconnected",
	"Connection",
	"Timeout setting",
	"port setting"
};
/**********************************comHost******************************/
comHost::comHost(const char *address)
{
	bConnected = false;
	*portname = 0;
#ifdef WIN32
	strcpy(portname, "\\\\.\\");
#endif //WIN32
	strncat(portname, address, 63);
	portname[63] = 0;

	char *p = strchr(portname, ':' );
	if ( p!=NULL ) { *p++ = 0; strcpy(settings, p); }
	if ( p==NULL || *p==0 ) strcpy(settings, "9600,n,8,1");
}
void comHost::disconn()
{
	if ( bConnected ) {
		bConnected = false;
		pthread_join(readerThread, NULL);
	}
}
#ifdef WIN32
int comHost::read()
{
	int rc=0;
	COMMTIMEOUTS timeouts={10,0,1,0,0};	//timeout and buffer settings
										//WriteTotalTimeoutMultiplier = 0;
										//WriteTotalTimeoutConstant = 0;
										//ReadTotalTimeoutConstant = 1;
										//ReadIntervalTimeout = 10;

	hCommPort = CreateFile( portname, GENERIC_READ|GENERIC_WRITE, 0, NULL,
													OPEN_EXISTING, 0, NULL);
	if ( hCommPort==INVALID_HANDLE_VALUE) {
		rc = -1;
		goto shutdown;
	}

	if ( SetCommTimeouts(hCommPort,&timeouts)==0) {
		rc = -2;
		CloseHandle(hCommPort);
		goto shutdown;
	}
	SetupComm( hCommPort, 4096, 1024 );			//comm buffer sizes

	DCB dcb;									// comm port settings
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	BuildCommDCBA(settings, &dcb);
	if ( SetCommState(hCommPort, &dcb)==0 ) {
		rc = -3;
		CloseHandle(hCommPort);
		goto shutdown;
	}

	bConnected = true;
	do_callback("Connected", 0);
	while ( bConnected ) {
		DWORD cch;
		char buf[4096];
		if ( ReadFile(hCommPort, buf, 4096, &cch, NULL) )
			if ( cch > 0 )
				do_callback(buf, cch);
		else
			if ( !ClearCommError(hCommPort, NULL, NULL ) ) break;
	}
	bConnected = false;
	CloseHandle(hCommPort);
shutdown:
	do_callback(errmsgs[-rc], -1);
	readerThread = 0;
	return rc;
}
int comHost::write(const char *buf, int len)
{
	DWORD dwWrite=0;
	if ( bConnected ) {
		if ( !WriteFile(hCommPort, buf, len, &dwWrite, NULL) )
			disconn();
	}
	else
		if ( readerThread==0 && *buf=='\r' )
			connect();
	return dwWrite;
}
#else
int comHost::read()
{
	int rc = 0;
	speed_t baud = B9600;
	ttySfd = open(portname, O_RDWR|O_NDELAY);
	if ( ttySfd<0 ) {
		rc = -1;
		goto shutdown;
	}
	tcflush(ttySfd, TCIOFLUSH);

	struct termios SerialPortSettings;
	tcgetattr(ttySfd, &SerialPortSettings);
	if ( 	  strncmp(settings, "230400", 6)==0 )baud = B230400;
	else if ( strncmp(settings, "115200", 6)==0 )baud = B115200;
	else if ( strncmp(settings, "57600", 5)==0 ) baud = B57600;
	else if ( strncmp(settings, "38400", 5)==0 ) baud = B38400;
	else if ( strncmp(settings, "19200", 5)==0 ) baud = B19200;
	cfsetispeed(&SerialPortSettings, baud);
	cfsetospeed(&SerialPortSettings, baud);
	cfmakeraw(&SerialPortSettings);
	SerialPortSettings.c_cflag |=  CREAD|CLOCAL;	//turn on reader
//	SerialPortSettings.c_cflag &= ~CSIZE; 	//Clears the size Mask
//	SerialPortSettings.c_cflag |=  CS8;  	//Set the data bits = 8
//	SerialPortSettings.c_cflag &= ~PARENB; 	//CLEAR Parity Bit
//	SerialPortSettings.c_cflag &= ~CSTOPB; 	//Stop bits = 1
//	SerialPortSettings.c_cflag &= ~CRTSCTS;	//no RTS/CTS
//	SerialPortSettings.c_iflag &= ~(IXON|IXOFF|IXANY);	//no XON/XOFF
//	SerialPortSettings.c_iflag &= ~(ICANON|ECHO|ECHOE|ISIG);//NON Canonical
	SerialPortSettings.c_cc[VMIN]  = 0; 	//read can return 0 at timeout
	SerialPortSettings.c_cc[VTIME] = 1;  	//timeout at 100ms
	tcsetattr(ttySfd,TCSANOW,&SerialPortSettings);

	bConnected = true;
	do_callback("Connected", 0);
	while ( bConnected ) {
		char buf[4096];
		int len = ::read(ttySfd, buf, 4096);
		if ( len>0 )
			do_callback(buf, len);
		else
			if ( len<0 && errno!=EAGAIN ) break;
	}
	bConnected = false;
	close(ttySfd);
shutdown:
	do_callback(errmsgs[-rc], -1);
	readerThread = 0;
	return -1;
}
int comHost::write(const char *buf, int len)
{
	int rc = 0;
	if ( bConnected ) {
		if ( (rc=::write(ttySfd, buf, len))<0 )
			disconn();
	}
	else
		if ( readerThread==0 && *buf=='\r' )
			connect();
	return 0;
}
#endif //WIN32

/**********************************tcpHost******************************/
tcpHost::tcpHost(const char *name):Fan_Host() 
{
	strncpy(hostname, name, 63);
	hostname[63]=0;
	port = 23;
	char *p = strchr(hostname, ':');
	if ( p!=NULL ) {
		char *p1 = strchr(p+1, ':');
		if ( p1==NULL ) { //ipv6 address if more than one ':'
			port = atoi(p+1);
			*p=0;
		}
	}
}
int tcpHost::tcp()
{
    struct addrinfo *ainfo;    
    if ( getaddrinfo(hostname, NULL, NULL, &ainfo)!=0 ) return -1;
    sock = socket(ainfo->ai_family, SOCK_STREAM, 0);
    ((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(port);
    int rc = ::connect(sock, ainfo->ai_addr, ainfo->ai_addrlen);
    freeaddrinfo(ainfo);
	return rc;
}
int tcpHost::read()
{
	do_callback("Connecting", 0);	//indicates connecting
	int rc = tcp();
	if ( rc<0 ) goto TCP_Close;

	bConnected = true;
	do_callback("Connected", 0);	//indicates connected
	int cch;
	char buf[1536], *p, *p0;
	while ( (cch=recv(sock, buf, 1536, 0))>0 ) {
		for ( p=buf; p<buf+cch; p++ ) {
			while ( *p==-1 && p<buf+cch ) {
				p0 = (char *)telnet_options((unsigned char *)p);
				memcpy(p, p0, buf+cch-p0);
				cch -= p0-p;	//cch could become 0 after this
			}
		}
		if ( cch>0 ) do_callback(buf, cch);
	}
	bConnected = false;

TCP_Close:
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);	//tell Fl_Term host has disconnected
	readerThread = 0;
	return rc;
}
int tcpHost::write(const char *buf, int len ) 
{
	if ( bConnected ) {
		if ( bEcho ) do_callback(buf, len);
		int total=0, cch=0;
		while ( total<len ) {
			cch = send( sock, buf+total, len-total, 0); 
			if ( cch<0 ) {
				disconn();
				return cch;
			}
			total+=cch;
		}
		return total;
	}
	else {
		if ( readerThread==0 && *buf=='\r' ) connect();
	}
	return 0;
}
void tcpHost::disconn()
{
	if ( readerThread!=0 ) {
		shutdown(sock, 1);	//SD_SEND=1 on Win32, SHUT_WR=1 on posix
		pthread_join(readerThread, NULL);
	}
}
#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
unsigned char NEGOBEG[]={0xff, 0xfb, 0x03, 0xff, 0xfd, 0x03, 0xff, 0xfd, 0x01};
unsigned char TERMTYPE[]={0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0};
unsigned char *tcpHost::telnet_options( unsigned char *buf )
{
	unsigned char negoreq[]={0xff,0,0,0, 0xff, 0xf0};
	unsigned char *p = buf+1;
	switch ( *p++ ) {
		case TNO_DO:
			if ( *p==TNO_TERMTYPE || *p==TNO_NEWENV || *p==TNO_ECHO ) {
				negoreq[1]=TNO_WILL; negoreq[2]=*p;
				send(sock, (char *)negoreq, 3, 0);
				if ( *p==TNO_ECHO ) bEcho = true;
			}
			else  {						// if ( *p!=TNO_AHEAD ), 08/10 why?
				negoreq[1]=TNO_WONT; negoreq[2]=*p;
				send(sock, (char *)negoreq, 3, 0);
			}
			break;
		case TNO_SUB:
			if ( *p==TNO_TERMTYPE ) {
				send(sock, (char *)TERMTYPE, sizeof(TERMTYPE), 0);
			}
			if ( *p==TNO_NEWENV ) {
				negoreq[1]=TNO_SUB; negoreq[2]=*p;
				send(sock, (char *)negoreq, 6, 0);
			}
			p += 3;
			break;
		case TNO_WILL: 
			if ( *p==TNO_ECHO ) bEcho = false;
			negoreq[1]=TNO_DO; negoreq[2]=*p;
			send(sock, (char *)negoreq, 3, 0);
			break;
		case TNO_WONT: 
			negoreq[1]=TNO_DONT; negoreq[2]=*p;
			send(sock, (char *)negoreq, 3, 0);
		   break;
		case TNO_DONT:
			break;
	}
	return p+1; 
}