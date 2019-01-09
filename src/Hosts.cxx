//
// "$Id: Hosts.cxx 8522 2018-11-15 22:15:10 $"
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

	hCommPort = CreateFileA( portname, GENERIC_READ|GENERIC_WRITE, 0, NULL,
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
				Sleep(1);
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
	char buf[1536];
	while ( (cch=recv(sock, buf, 1536, 0))>0 ) {
		do_callback(buf, cch);
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