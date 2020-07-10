//
// "$Id: Hosts.cxx 28436 2020-06-30 12:15:10 $"
//
// HOST tcpHost comHost pipeHost and daemon hosts
//
//	host implementation for terminal simulator
//	used with the Fl_Term widget in flTerm
//
// Copyright 2017-2020 by Yongchao Fan.
//
//     https://github.com/yongchaofan/tinyTerm2/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm2/issues/new
//

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef WIN32
	#include <sys/stat.h>
	#include <io.h>
	#include <time.h>
#else
	#include <termios.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <sys/ioctl.h>
	#include <signal.h>
	#include <errno.h>
#endif
#include "host.h"
using namespace std;

void HOST::connect()
{
	if ( reader.joinable() == false ) {
		std::thread new_reader(&HOST::read, this);
		reader.swap(new_reader);
	}
}
void HOST::print(const char *fmt, ...)
{
	char buff[4096];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(buff, 4096, fmt, args);
	va_end(args);
	do_callback(buff, len);
	do_callback("\033[37m",5);
}
/**********************************comHost******************************/
comHost::comHost(const char *address)
{
#ifdef WIN32
	strcpy(portname, "\\\\.\\");
#else
	strcpy(portname, "/dev/");
#endif //WIN32
	strncat(portname, address, 58);
	portname[63] = 0;
	bXmodem = false;

	char *p = strchr(portname, ':' );
	if ( p!=NULL ) { *p++ = 0; strcpy(settings, p); }
	if ( p==NULL || *p==0 ) strcpy(settings, "9600,n,8,1");
}
void comHost::disconn()
{
	if ( status()==HOST_CONNECTED ) {
		status( HOST_IDLE );
//		if ( reader.joinable() ) reader.join();
	}
}
const char STX = 0x02;
const char EOT = 0x04;
const char ACK = 0x06;
const char NAK = 0x15;
void comHost::block_crc()
{
	unsigned short crc = 0;
	for ( int i=3; i<131; i++ ) {
		crc = crc ^ xmodem_buf[i] << 8;
		for ( int j=0; j<8; j++ ) {
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
		}
	}
	xmodem_buf[131] = (crc>>8) & 0xff;
	xmodem_buf[132] = crc & 0xff;
 }
void comHost::xmodem_block()
{
	xmodem_buf[0] = 0x01; //SOH
	xmodem_buf[1] = ++xmodem_blk;
	xmodem_buf[2] = 255-xmodem_blk;
	int cnt = fread( xmodem_buf+3, 1, 128, xmodem_fp );
	if ( cnt <= 0 ) {
		xmodem_buf[0] = EOT;
		fclose(xmodem_fp);
	}
	if ( cnt>0 && cnt<128 )
		for ( int i=cnt+3; i<131; i++ ) xmodem_buf[i] = 0;
	if ( xmodem_crc ) {
		block_crc();
	}
	else {
		unsigned char chksum = 0;
		for ( int i=3; i<131; i++ ) chksum += xmodem_buf[i];
		xmodem_buf[131] = chksum;
	}
}
void comHost::xmodem_send()
{
	xmodem_started = true;
	if ( xmodem_buf[0]==EOT )
		write(xmodem_buf, 1);
	else
		write(xmodem_buf, xmodem_crc?133:132);
}
void comHost::xmodem_recv(char op)
{
	switch( op ) {
	case 0:		//nothing received,resend every 10 seconds
				if ( ++xmodem_timeout%10000==0 && xmodem_started ) {
					xmodem_send();
					do_callback("R",1);
				}
				if ( xmodem_timeout>60000 ) {//timeout after 60 seconds
					bXmodem = false;
					fclose(xmodem_fp);
					do_callback("Aborted\n", 9);
				}
				break;
	case 0x06:	xmodem_timeout = 0;			//ACK
				if ( xmodem_buf[0] == EOT ) {
					do_callback("Completed\n",10);
					bXmodem = false;
					return;
				}
				xmodem_block();
				xmodem_send();
				if ( xmodem_blk==0 ) do_callback(".",1);
				break;
	case 0x15:	do_callback("N",1);			//NAK
				xmodem_send();
				break;
	case 'C':	do_callback("CRC",3);		//start CRC
				xmodem_crc = true;
				block_crc();
				xmodem_send();
				break;
	}
}
void comHost::xmodem(FILE *fp)
{
	xmodem_fp = fp;
	bXmodem = true;
	xmodem_crc = false;
	xmodem_started = false;
	xmodem_timeout = 0;
	xmodem_blk = 0;
	xmodem_block();
	do_callback("xmodem",6);
}
#ifdef WIN32
int comHost::read()
{
	COMMTIMEOUTS timeouts={1,0,1,0,0};	//timeout and buffer settings
										//WriteTotalTimeoutMultiplier = 0;
										//WriteTotalTimeoutConstant = 0;
										//ReadTotalTimeoutConstant = 1;
										//ReadIntervalTimeout = 1;

	hCommPort = CreateFileA( portname, GENERIC_READ|GENERIC_WRITE, 0, NULL,
													OPEN_EXISTING, 0, NULL);
	if ( hCommPort==INVALID_HANDLE_VALUE) {
		do_callback("Connection", -1);
		goto shutdown;
	}

	if ( SetCommTimeouts(hCommPort,&timeouts)==0) {
		do_callback("Set timeout failure", -2);
		CloseHandle(hCommPort);
		goto shutdown;
	}
	SetupComm( hCommPort, 4096, 1024 );			//comm buffer sizes

	DCB dcb;									// comm port settings
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	BuildCommDCBA(settings, &dcb);
	if ( SetCommState(hCommPort, &dcb)==0 ) {
		do_callback("Settings failure", -3);
		CloseHandle(hCommPort);
		goto shutdown;
	}

	do_callback("Connected", 0);
	status( HOST_CONNECTED );
	while ( status()==HOST_CONNECTED ) {
		DWORD cch;
		char buf[255];
		if ( ReadFile(hCommPort, buf, 255, &cch, NULL) ) {
			if ( bXmodem ) {
				char op = 0;
				if ( cch>0 ) op = buf[cch-1];
				xmodem_recv(op);
				continue;
			}
			if ( cch > 0 )
				do_callback(buf, cch);
			else
				Sleep(1);
		}
		else
			if ( !ClearCommError(hCommPort, NULL, NULL ) ) break;
	}
	CloseHandle(hCommPort);
	status( HOST_IDLE );
	do_callback("Disconnected", -1);

shutdown:
	reader.detach();
	return 0;
}
int comHost::write(const char *buf, int len)
{
	DWORD dwWrite=0;
	if ( status()==HOST_CONNECTED ) {
		if ( !WriteFile(hCommPort, buf, len, &dwWrite, NULL) )
			disconn();
	}
	else
		if ( reader.joinable()==false && *buf=='\r' )
			connect();
	return dwWrite;
}
#else
int comHost::read()
{
	speed_t baud = B9600;
	ttySfd = open(portname, O_RDWR|O_NDELAY);
	if ( ttySfd<0 ) {
		do_callback("Port openning", -1);
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
	SerialPortSettings.c_cflag |=  CREAD|CLOCAL;		//turn on reader
//	SerialPortSettings.c_cflag &= ~CSIZE;				//Clears the size Mask
//	SerialPortSettings.c_cflag |=  CS8;					//Set the data bits = 8
//	SerialPortSettings.c_cflag &= ~PARENB;				//CLEAR Parity Bit
//	SerialPortSettings.c_cflag &= ~CSTOPB;				//Stop bits = 1
//	SerialPortSettings.c_cflag &= ~CRTSCTS;				//no RTS/CTS
//	SerialPortSettings.c_iflag &= ~(IXON|IXOFF|IXANY);	//no XON/XOFF
//	SerialPortSettings.c_iflag &= ~(ICANON|ECHO|ECHOE|ISIG);//NON Canonical
	SerialPortSettings.c_cc[VMIN]  = 0; 		//read can return 0 at timeout
	SerialPortSettings.c_cc[VTIME] = 1; 		//timeout at 100ms
	tcsetattr(ttySfd,TCSANOW,&SerialPortSettings);

	status( HOST_CONNECTED );
	do_callback("Connected", 0);
	while ( status()==HOST_CONNECTED ) {
		char buf[4096];
		int len = ::read(ttySfd, buf, 4096);
		if ( bXmodem ) {
			char op = 0;
			if ( len>0 ) op = buf[len-1];
			xmodem_recv(op);
			continue;
		}
		if ( len>0 )
			do_callback(buf, len);
		else
			if ( len<0 && errno!=EAGAIN ) break;
	}
	close(ttySfd);
	status( HOST_IDLE );
	do_callback("Disconnected", -1);

shutdown:
	reader.detach();
	return 0;
}
int comHost::write(const char *buf, int len)
{
	if ( status()==HOST_CONNECTED ) {
		if ( ::write(ttySfd, buf, len) < 0 )
			disconn();
	}
	else {
		if ( live()==false && *buf=='\r' )
			connect();
	}
	return 0;
}
#endif //WIN32

/**********************************tcpHost******************************/
tcpHost::tcpHost(const char *name):HOST()
{
	strncpy(hostname, name, 63);
	hostname[63]=0;
	port = 23;
	sock = -1;
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
	if ( getaddrinfo(hostname, NULL, NULL, &ainfo)!=0 ) {
		do_callback("invalid hostname or ip address", -1);
		return -1;
	}
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(port);

	sock = socket(ainfo->ai_family, SOCK_STREAM, 0);
	if ( sock!=-1 )
		print("Trying...");
	int rc = ::connect(sock, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	if ( rc!=-1 )
		print("connected\r\n");
	else {
		const char *errmsg = "";
#ifdef WIN32
		switch( WSAGetLastError() ) {
		case WSAEHOSTUNREACH:
		case WSAENETUNREACH: errmsg ="unreachable"; break;
		case WSAECONNRESET:  errmsg ="reset"; break;
		case WSAETIMEDOUT:   errmsg ="timeout"; break;
		case WSAECONNREFUSED:errmsg ="refused"; break;
		}
#else
		switch( errno ) {
		case EHOSTUNREACH:
		case ENETUNREACH: errmsg ="unreachable"; break;
		case ECONNRESET:  errmsg ="reset"; break;
		case ETIMEDOUT:   errmsg ="timeout"; break;
		case ECONNREFUSED:errmsg ="refused"; break;
		}
#endif
		do_callback( errmsg, -1 );
		closesocket(sock);
	}
	return rc;
}
int tcpHost::read()
{
	status( HOST_CONNECTING );
	if ( tcp()==0 ) {
		status( HOST_CONNECTED );
		do_callback("Connected", 0);
		int cch;
		char buf[1536];
		while ( (cch=recv(sock, buf, 1536, 0))>0 ) {
			do_callback(buf, cch);
		}
		do_callback("Disconnected", -1);
	}

	if ( sock!=-1 ) {
		closesocket(sock);
		sock = -1;
	}
	reader.detach();
	status(HOST_IDLE);
	return 0;
}
int tcpHost::write(const char *buf, int len)
{
	if ( sock!=-1 ) {
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
		if (  reader.joinable()==false && *buf=='\r' ) connect();
	}
	return 0;
}
void tcpHost::disconn()
{
	if ( reader.joinable() ) {
		shutdown(sock, 1);	//SD_SEND=1 on Win32, SHUT_WR=1 on posix
//		reader.join();//cause crash when compiled with visual studio or xcode
	}
}
pipeHost::pipeHost(const char *name):HOST()
{
	strncpy(cmdline, name, 255);
	cmdline[255] = 0;
#ifdef WIN32
	hStdioRead = NULL;
	hStdioWrite = NULL;
#else
	pty_master = -1;
	pty_slave = -1;
	shell_pid = -1;
#endif
}
#ifdef WIN32
int pipeHost::read()
{
	HANDLE Stdin_Rd, Stdin_Wr ;
	HANDLE Stdout_Rd, Stdout_Wr, Stderr_Wr;
	memset( &piStd, 0, sizeof(PROCESS_INFORMATION) );

	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = true;			//Set the bInheritHandle flag
	saAttr.lpSecurityDescriptor = NULL;		//so pipe handles are inherited

	CreatePipe(&Stdout_Rd, &Stdout_Wr, &saAttr, 0);//pipe for child's STDOUT
	SetHandleInformation(Stdout_Rd, HANDLE_FLAG_INHERIT, 0);
	// Ensure the read handle to the pipe for STDOUT is not inherited
	CreatePipe(&Stdin_Rd, &Stdin_Wr, &saAttr, 0);	//pipe for child's STDIN
	SetHandleInformation(Stdin_Wr, HANDLE_FLAG_INHERIT, 0);
	// Ensure the write handle to the pipe for STDIN is not inherited
	DuplicateHandle(GetCurrentProcess(),Stdout_Wr,
					GetCurrentProcess(),&Stderr_Wr,0,
					true,DUPLICATE_SAME_ACCESS);
	DuplicateHandle(GetCurrentProcess(),Stdout_Rd,
					GetCurrentProcess(),&hStdioRead,0,
					true,DUPLICATE_SAME_ACCESS);
	DuplicateHandle(GetCurrentProcess(),Stdin_Wr,
					GetCurrentProcess(),&hStdioWrite,0,
					true,DUPLICATE_SAME_ACCESS);
	CloseHandle( Stdin_Wr );
	CloseHandle( Stdout_Rd );

	struct _STARTUPINFOA siStartInfo;
	memset( &siStartInfo, 0, sizeof(STARTUPINFO) );	// Set STARTUPINFO
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = Stderr_Wr;
	siStartInfo.hStdOutput = Stdout_Wr;
	siStartInfo.hStdInput = Stdin_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	if ( CreateProcessA( NULL,			// Create the child process.
						cmdline,		// command line
						NULL,			// process security attributes
						NULL,			// primary thread security attributes
						true,			// handles are inherited
						CREATE_NO_WINDOW,// creation flags
						NULL,			// use parent's environment
						NULL,			// use parent's current directory
						&siStartInfo,	// STARTUPINFO pointer
						&piStd) ) {		// receives PROCESS_INFORMATION
		CloseHandle( Stdin_Rd );
		CloseHandle( Stdout_Wr );
		CloseHandle( Stderr_Wr );

		status( HOST_CONNECTED );
		while ( true ) {
			DWORD dwCCH;
			char buf[1536];
			if ( ReadFile( hStdioRead, buf, 1500, &dwCCH, NULL) > 0 ) {
				if ( dwCCH > 0 ) {
					buf[dwCCH] = 0;
					do_callback( buf, dwCCH );
				}
				else
					Sleep(1);
			}
			else
				break;
		}
		status( HOST_IDLE );
	}
	else
		do_callback( "\033[31m\tunsupported!", 18);
	
	do_callback( "", -1 );
	CloseHandle( hStdioRead );
	CloseHandle( hStdioWrite );
	reader.detach();
	return 1;
}
int pipeHost::write( const char *buf, int len )
{
	if ( *buf==3 && len==1 )
		disconn();
	return 0;
}
void pipeHost::disconn()
{
	if ( WaitForSingleObject(piStd.hProcess, 100)==WAIT_TIMEOUT )
		TerminateProcess(piStd.hProcess,0);
	CloseHandle(piStd.hThread);
	CloseHandle(piStd.hProcess);
}//end of pipeHost WIN32 definition
#else
int pipeHost::read()
{
	char *slave_name;
	pty_master = posix_openpt(O_RDWR|O_NOCTTY);
	if ( pty_master==-1 ) {
		do_callback("possix_openpt", -1);
		return -1;
	}
	if ( grantpt(pty_master)==-1 ) {
		do_callback("grantpt", -2);
		goto pty_close;
	}
	if ( unlockpt(pty_master)==-1 ) {
		do_callback("unlockpt", -3);
		goto pty_close;
	}

	slave_name = ptsname(pty_master);
	if ( slave_name==NULL ) {
		do_callback("slave name", -4);
		goto pty_close;
	}
	pty_slave = open(slave_name, O_RDWR|O_NOCTTY);
	if ( pty_slave==-1 ) {
		do_callback("slave open", -5);
		goto pty_close;
	}
	
	shell_pid = fork();
	if ( shell_pid<0 ) {
		do_callback("fork", -6);
	}
	else if ( shell_pid==0 ) {//child process for shell
		close(pty_master);
		setsid();
		if ( ioctl(pty_slave, TIOCSCTTY, NULL)==-1 ) {
			perror("ioctl(TIOCSCTTY)");
			goto pty_close;
		}
		dup2(pty_slave, 0);
		dup2(pty_slave, 1);
		dup2(pty_slave, 2);
		execl(cmdline, cmdline, (char *)NULL);
		printf("\033[31m\terror executing shell command!");
		return false;
	}
	else {
		close(pty_slave);
		do_callback("shell started", 0);
		status( HOST_CONNECTED );
		char buf[4096];
		int len = 0;
		while ( (len=::read(pty_master, buf, 4095))>0 ) {
			buf[len] = 0;
			do_callback(buf, len);
		}
		status( HOST_IDLE );
		do_callback("", -1);
	}
pty_close:
	close(pty_master);
	reader.detach();
	return 0;
}
int pipeHost::write( const char *buf, int len )
{
	for ( int i=0; i<len; i++ ) 
		if ( ::write( pty_master, buf+i, 1 )<0 ) 
			close(pty_master);
	return 0;
}
void pipeHost::send_size(int sx, int sy)
{
	struct winsize ws; 
	ws.ws_col = (unsigned short)sx; 
	ws.ws_row = (unsigned short)sy;

	if ( ioctl(pty_master, TIOCSWINSZ, &ws)==-1 ) 
		do_callback("\r\nerror setting window size\r\n", 29);
}
void pipeHost::disconn()
{
	kill( shell_pid, SIGTERM );	
}
#endif


#ifdef WIN32
/*begin ftpd tftpd Win32 definition
int sock_select( SOCKET s, int secs )
{
	struct timeval tv = { 0, 0 };
	tv.tv_sec = secs;
	FD_SET svrset;
	FD_ZERO( &svrset );
	FD_SET( s, &svrset );
	return select( 1, &svrset, NULL, NULL, &tv );
}
bool is_directory(const char *dir)
{
	struct stat sb;
	if ( stat(dir, &sb)==-1 ) return false;
	if ( (sb.st_mode&S_IFMT)!=S_IFDIR ) return false;
	return true;
}
void ftpdHost::sock_send( const char *reply )
{
	send( ftp_s1, reply, strlen(reply), 0 );
}
ftpdHost::ftpdHost(const char *root)
{
	ftp_s0 = INVALID_SOCKET;
	ftp_s1 = INVALID_SOCKET;
	strncpy( rootDir, root, 1023 );
	rootDir[1023] = 0;
}
int ftpdHost::read()
{
	unsigned long  dwIp;
	unsigned short wPort;
	unsigned int c[6], ii[2];
	char *param=0, szBuf[32768];
	char fn[MAX_PATH], workDir[MAX_PATH];
	bool bPassive;

	SOCKET s2=-1, s3=-1; 		// s2 data connection, s3 data listen
	struct sockaddr_in svraddr, clientaddr;		// for data connection
	int addrsize=sizeof(svraddr);

	if ( !is_directory(rootDir) ) {
		print("Invalid root direcotry\n");
		return -1;
	}

	ftp_s0 = socket(AF_INET, SOCK_STREAM, 0);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=ADDR_ANY;
	svraddr.sin_port=htons(21);
	if ( bind(ftp_s0, (struct sockaddr*)&svraddr,
							addrsize) == SOCKET_ERROR ) {
		print("Couldn't bind to TCP port 21\n");
		goto tcp_close;
	}
	if ( listen(ftp_s0, 1) == SOCKET_ERROR ) {
		print("Couldn't listen on TCP port 21\n");
		goto tcp_close;
	}

	print("FTP server started at %s\n", rootDir);

	int ret0, ret1;
	while( (ret0=sock_select(ftp_s0, 900)) == 1 ) {
		strcpy(workDir, "\\");;
		ftp_s1 = accept( ftp_s0, (struct sockaddr*)&clientaddr, &addrsize );
		if ( ftp_s1 ==INVALID_SOCKET ) continue;

		sock_send( "220 Welcome\n");
		getpeername(ftp_s1, (struct sockaddr *)&clientaddr, &addrsize);
		print( "FTPd: connected from %s\n",
						inet_ntoa(clientaddr.sin_addr));

		FILE *fp;
		bPassive = false;
		bool bUser=false, bPass=false;
		while ( (ret1=sock_select(ftp_s1, 300)) == 1 ) {
			int cnt=recv( ftp_s1, szBuf, 1024, 0 );
			if ( cnt<=0 ) {
				print(  "FTPd: client disconnected\n");
				break;
			}
			szBuf[cnt--]=0;
			print( szBuf);
			while (szBuf[cnt]=='\r' || szBuf[cnt]=='\n' ) szBuf[cnt--]=0;
			if ( (param=strchr(szBuf, ' '))!=NULL )
				*param++=0;
			else
				param = szBuf+cnt+1;

			// *** Process FTP commands ***
			if (stricmp("user", szBuf) == 0){
				sock_send( "331 Password required\n");
				bUser = strncmp( param, "tiny", 4 )==0 ? true : false;
				continue;
			}
			if (stricmp("pass", szBuf) == 0){
				bPass = bUser && strncmp(param, "term", 4)==0;
				sock_send( bPass ?  "230 Logged in okay\n":
									"530 Login incorrect\n");
				continue;
			}
			if ( !bPass ) {
				sock_send( "530 Login required\n");
				continue;
			}
			strcpy(fn, rootDir);
			strcat(fn, workDir);
			if ( workDir[strlen(workDir)-1]!='\\' ) strcat(fn, "\\");
			strcat(fn, param);

			if (stricmp("syst", szBuf) ==0 ){
				sock_send( "215 tinyTerm ftpd\n");
			}
			else if(stricmp("port", szBuf) == 0){
				sscanf(param, "%d,%d,%d,%d,%d,%d",
							&c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
				ii[0] = c[1]+(c[0]<<8);
				ii[1] = c[3]+(c[2]<<8);
				wPort = c[5]+(c[4]<<8);
				dwIp = ii[1]+(ii[0]<<16);
				clientaddr.sin_addr.s_addr = htonl(dwIp);
				clientaddr.sin_port=htons(wPort);
				sock_send( "200 PORT command successful\n");
			}
			else if(stricmp("type", szBuf) == 0){
				sock_send( "200 Type can only be binary\n");
			}
			else if(stricmp("pwd", szBuf) == 0 || stricmp("xpwd", szBuf) == 0){
				sprintf( szBuf, "257 \"%s\" is current directory\n", workDir );
				sock_send( szBuf);
			}
			else if(stricmp("cwd", szBuf) == 0){
				char *pPart, fullDir[MAX_PATH];
				if ( GetFullPathNameA(fn, MAX_PATH, fullDir, &pPart)==0 ) {
					sock_send( "550 Invalid path\n");
					continue;
				}
				if ( strncmp(rootDir, fullDir, strlen(rootDir))!=0 ) {
					sock_send( "550 Invalid path\n");
					continue;
				}
				if ( !is_directory( fullDir ) ) {
					sock_send( "550 No such directory\n");
					continue;
				}
				sock_send( "250 CWD sucessful\n");
				strcpy(workDir, fullDir+strlen(rootDir));
			}
			else if(stricmp("cdup", szBuf) == 0) {
				char *p = strrchr(workDir, '\\');
				if( p!=NULL && p!=workDir ) {
					*p = 0;
					sock_send( "250 CWD sucessful\n");
				}
				else
					sock_send( "550 Invalid path\n");
			}
			else if(stricmp("pasv", szBuf)==0 || stricmp("epsv", szBuf)==0 ){
				getsockname(ftp_s1, (struct sockaddr *)&svraddr, &addrsize);
				svraddr.sin_port = 0;
				s3 = socket(AF_INET, SOCK_STREAM, 0);
				bind(s3, (struct sockaddr *)&svraddr, addrsize);
				listen(s3, 1);

				getsockname(s3, (struct sockaddr *)&svraddr, &addrsize);
				dwIp = svraddr.sin_addr.s_addr;
				wPort = svraddr.sin_port;

				if ( *szBuf=='p' || *szBuf=='P'  ) {
					ii[1]=HIWORD(dwIp); ii[0]=LOWORD(dwIp);
					c[1]=HIBYTE(ii[0]); c[0]=LOBYTE(ii[0]);
					c[3]=HIBYTE(ii[1]); c[2]=LOBYTE(ii[1]);
					c[5]=HIBYTE(wPort); c[4]=LOBYTE(wPort);
					sprintf(szBuf, "227 PASV Mode (%d,%d,%d,%d,%d,%d)\n",
										c[0], c[1], c[2], c[3], c[4], c[5]);
				}
				else sprintf(szBuf, "229 EPSV Mode (|||%d|)\n", ntohs(wPort));
				sock_send( szBuf);
				bPassive=true;
			}
			else if( stricmp("nlst", szBuf)==0 || stricmp("list", szBuf)==0 ){
				struct _finddata_t  ffblk;
				if ( *(param+strlen(param)-1)=='/') strcat(fn, "*.*");
				if ( *param==0 ) strcat(fn, "\\*.*");
				intptr_t hFile = _findfirst(fn, &ffblk);
				if ( hFile==-1 ) {
					sock_send( "550 No such file or directory\n");
					continue;
				}
				sock_send( "150 Opening ASCII connection for list\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(s2, (struct sockaddr *)&clientaddr,
															sizeof(clientaddr));
				}
				bool bNlst = szBuf[0]=='n' || szBuf[0]=='N';
				do {
					if ( ffblk.name[0]=='.' ) continue;
					if ( bNlst ) {
						sprintf(szBuf, "%s\r\n", ffblk.name);
					}
					else {
						char buf[256];
						strcpy(buf, ctime(&ffblk.time_write));
						buf[24]=0;
						if ( ffblk.attrib&_A_SUBDIR )
							sprintf(szBuf, "%s %s %s\r\n", buf+4,
										"       <DIR>", ffblk.name);
						else
							sprintf(szBuf, "%s % 12ld %s\r\n", buf+4,
											ffblk.size, ffblk.name);
					}
					send(s2, szBuf, strlen(szBuf), 0);
				} while ( _findnext(hFile, &ffblk)==0 );
				_findclose(hFile);
				sock_send( "226 Transfer complete.\n");
				closesocket(s2);
			}
			else if(stricmp("stor", szBuf) == 0){
				fp = NULL;
				if ( strstr(param, ".." )==NULL )
					fp = fopen(fn, "wb");
				if(fp == NULL){
					sock_send( "550 Unable write file\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(s2, (struct sockaddr *)&clientaddr,
											sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0, nCnt=0;
				do {
					nLen = recv(s2, szBuf, 32768, 0);
					if ( nLen>0 ) {
						lSize += nLen;
						fwrite(szBuf, nLen, 1, fp);
					}
					if ( ++nCnt==256 ) {
						print( "\r%lu bytes received", lSize);
						nCnt = 0;
					}
				}while ( nLen!=0);
				fclose(fp);
				print( "\r%lu bytes received\n", lSize);
				sock_send( "226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("retr", szBuf) == 0){
				fp = NULL;
				if ( strstr(param, ".." )==NULL )
					fp = fopen(fn, "rb");
				if(fp == NULL) {
					sock_send( "550 Unable to read file\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(s2, (struct sockaddr *)&clientaddr,
											sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0, nCnt=0;
				do {
					nLen = fread(szBuf, 1, 32768, fp);
					if ( send(s2, szBuf, nLen, 0) == 0) break;
					lSize += nLen;
					if ( ++nCnt==32 ) {
						print( "\r%lu bytes sent", lSize);
						nCnt = 0;
					}
				}
				while ( nLen==32768);
				fclose(fp);
				print( "\r%lu bytes sentd\n", lSize);
				sock_send( "226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("size", szBuf) == 0){
				struct _finddata_t  ffblk;
				intptr_t  hFile = _findfirst(fn, &ffblk);
				if ( hFile!=-1 )
				{
					sprintf(szBuf, "213 %lu\n", ffblk.size);
					sock_send( szBuf );
					_findclose(hFile);
				}
				else
					sock_send( "550 No such file or directory\n");
			}
			else if(stricmp("mdtm", szBuf) == 0) {
				struct _finddata_t ffblk;
				intptr_t hFile = _findfirst(fn, &ffblk);
				if ( hFile!=-1 )
				{
					struct tm *t_mod = localtime( &ffblk.time_write);
					sprintf(szBuf, "213 %4d%02d%02d%02d%02d%02d\n",
							t_mod->tm_year+1900, t_mod->tm_mon+1,
							t_mod->tm_mday, t_mod->tm_hour,
							t_mod->tm_min, t_mod->tm_sec );
					sock_send( szBuf );
					_findclose(hFile);
				}
				else
					 sock_send( "550 No such file or directory\n");
			}
			else if(stricmp("quit", szBuf) == 0){
				sock_send( "221 Bye!\n");
				break;
			}
			else {
				sock_send( "500 Command not supported\n");
			}
		}
		if( ret1 == 0 ) {
			sock_send( "500 Timeout\n");
			print(  "client timed out\n");
		}
		closesocket(ftp_s1);
	}
	print(  ret0==0? "FTP server timed out\n" : "FTP server stopped\n" );

tcp_close:
	closesocket(ftp_s0);
	ftp_s0 = INVALID_SOCKET;
	reader.detach();
	return 0;
}
int ftpdHost::write(const char *buf, int len)
{
	if ( !reader.joinable() )
		if ( *buf=='\r' ) connect();
	return 0;
}
void ftpdHost::disconn()
{
	if ( ftp_s0 != INVALID_SOCKET ) {
		closesocket( ftp_s0 );
		ftp_s0 = INVALID_SOCKET;
	}
}

tftpdHost::tftpdHost(const char *root)
{
	tftp_s0 = INVALID_SOCKET;
	tftp_s1 = INVALID_SOCKET;
	strncpy( rootDir, root, 1023 );
	rootDir[1023] = 0;
}
void tftpdHost::tftp_Read( FILE *fp )
{
	char dataBuf[516], ackBuf[516];
	unsigned short nCnt, nRetry=0;
	int nLen, len;

	nCnt=1;
	len = nLen = fread(dataBuf+4, 1, 512 , fp);
	do {
		dataBuf[0]=0; dataBuf[1]=3;
		dataBuf[2]=(nCnt>>8)&0xff; dataBuf[3]=nCnt&0xff;
		send(tftp_s1, dataBuf, nLen+4, 0);
		if ( nCnt%512==0 ) print("#");
		if ( sock_select( tftp_s1, 5 ) == 1 ) {
			if ( recv(tftp_s1, ackBuf, 516, 0)==SOCKET_ERROR ) break;
			if ( ackBuf[1]==4 && ackBuf[2]==dataBuf[2] &&
								 ackBuf[3]==dataBuf[3]) {
				nRetry=0;
				nCnt++;
				len = nLen;
				nLen = fread(dataBuf+4, 1, 512 , fp);
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	} while ( len==512 );
	if ( nCnt>=512 ) print("\n");
}
void tftpdHost::tftp_Write( FILE *fp )
{
	char dataBuf[516], ackBuf[516];
	unsigned short ntmp, nCnt=0, nRetry=0;
	int nLen=516;
	while ( nLen > 0 ) {
		ackBuf[0]=0; ackBuf[1]=4;
		ackBuf[2]=(nCnt>>8)&0xff; ackBuf[3]=nCnt&0xff;
		send(tftp_s1, ackBuf, 4, 0);
		if ( nLen!=516 ) break;
		if ( sock_select( tftp_s1, 5) == 1 ) {
			nLen = recv(tftp_s1, dataBuf, 516, 0);
			if ( nLen == SOCKET_ERROR ) break;
			ntmp=(unsigned char)(dataBuf[2]);
			ntmp=(ntmp<<8)+(unsigned char )(dataBuf[3]);
			if ( dataBuf[1]==3 && ntmp==nCnt+1 ) {
				fwrite(dataBuf+4, 1, nLen-4, fp);
				nRetry=0;
				nCnt++;
				if ( nCnt%512==0 ) print("#");
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	}
	if ( nCnt>=512 ) print("\n");
}
int tftpdHost::read()
{
	if ( !is_directory(rootDir) ) {
		print("Invalid root direcotry\n");
		return -1;
	}

	struct sockaddr_in svraddr, clientaddr;
	int addrsize=sizeof(svraddr);
	tftp_s0 = socket(AF_INET, SOCK_DGRAM, 0);
	tftp_s1 = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=ADDR_ANY;
	svraddr.sin_port=htons(69);
	if ( bind(tftp_s0, (struct sockaddr*)&svraddr, addrsize)==SOCKET_ERROR ) {
		print( "Couldn't bind to tftp port, is another server running?\n");
		goto udp_close;
	}
	svraddr.sin_port=0;
	if ( bind(tftp_s1, (struct sockaddr *)&svraddr,addrsize)==SOCKET_ERROR ) {
		print( "Couldn't bind to tftp port, is another server running?\n");
		goto udp_close;
	}

	print( "TFTP server started at %s\n", rootDir);
	char fn[MAX_PATH];
	char dataBuf[516];
	int ret;
	while ( (ret=sock_select( tftp_s0, 300 )) == 1 ) {
		ret = recvfrom( tftp_s0, dataBuf, 516, 0,
						(struct sockaddr *)&clientaddr, &addrsize );
		if ( ret==SOCKET_ERROR ) break;

		::connect(tftp_s1, (struct sockaddr *)&clientaddr, addrsize);
		if ( dataBuf[1]==1  || dataBuf[1]==2 ) {
			bool bRead = dataBuf[1]==1;
			print( "TFTPd: %cRQ %s from %s\n", bRead?'R':'W', dataBuf+2,
									inet_ntoa(clientaddr.sin_addr) );
			strcpy(fn, rootDir);
			strcat(fn, "\\");
			strcat(fn, dataBuf+2);
			FILE *fp = fopen(fn,  bRead?"rb":"wb");
			if ( fp == NULL ) {
				dataBuf[3]=dataBuf[1]; dataBuf[0]=0;
				dataBuf[1]=5; dataBuf[2]=0;
				int len = sprintf( dataBuf+4, "Couldn't open %s", fn );
				send( tftp_s1, dataBuf, len+4, 0 );
			}
			else {
				bRead ? tftp_Read(fp) : tftp_Write(fp);
				fclose(fp);
			}
		}
	}
	print(  ret==0 ? "TFTP server timed out\n" : "TFTP server stopped\n" );

udp_close:
	closesocket( tftp_s1 );
	closesocket( tftp_s0 );
	tftp_s0 = INVALID_SOCKET;
	reader.detach();
	return 0;
}
int tftpdHost::write(const char *buf, int len)
{
	if ( !reader.joinable() )
		if ( *buf=='\r' ) connect();
	return 0;
}
void tftpdHost::disconn( )
{
	if ( tftp_s0 != INVALID_SOCKET ) {
		closesocket(tftp_s1);
		closesocket( tftp_s0 );
		tftp_s0 = INVALID_SOCKET;
	}
}
*/
#endif //WIN32

