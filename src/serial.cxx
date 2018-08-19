//
// "$Id: serial.cxx 4616 2018-06-29 21:55:10 $"
//
// commHost implementation for terminal simulator
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#ifndef WIN32
	#include <sys/termios.h>
#endif

#include "serial.h"
static const char *errmsgs[]={
	"Disconnected",
	"Port open",
	"Timeout setting",
	"port setting"
};
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