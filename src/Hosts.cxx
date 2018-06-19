//
// "$Id: Fan_Host.cxx 20949 2018-05-25 21:55:10 $"
//
// tcpHost ftpDaemon tftpDaemon comHost
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

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

#include "Hosts.h"
#include "Fl_Term.h"

const char *errmsgs[] = { 
"TCP connected\n",
"Address resolution failed\n",
"TCP connection failed\n"};

void Fan_Host::print( const char *fmt, ... ) 
{
	char buff[1024];
	va_list args;
	va_start(args, (char *)fmt);
	if ( term!=NULL ) {
		vsnprintf(buff, 1024, (char *)fmt, args);
		term->print(buff);
	}
	else {
		fprintf(stderr, "\n%s:", name());
		vfprintf(stderr, (char *)fmt, args);
	}
	va_end(args);
}
/**********************************tcpHost******************************/
tcpHost::tcpHost(const char *address):Fan_Host() 
{
	term = NULL;
	bConnected = false;
	if ( address!=NULL ) strncpy(hostname, address, 63);
	port = 23;
	char *p = strchr(hostname, ':');
	if ( p!=NULL ) {
		port = atoi(p+1);
		*p=0;
	}
}
int tcpHost::tcp()
{
    struct addrinfo *ainfo;    
    if ( getaddrinfo(hostname, NULL, NULL, &ainfo)!=0 ) return -1;
	
    int so = socket(ainfo->ai_family, SOCK_STREAM, 0);
    struct sockaddr_in *addr_in = (struct sockaddr_in *)ainfo->ai_addr;
    addr_in->sin_port = htons(port);

    int rc = ::connect(so, ainfo->ai_addr, ainfo->ai_addrlen);
    freeaddrinfo(ainfo);
    
	return rc==-1 ? -2 : so;
}
int tcpHost::connect()
{
	int rc = tcp();
	if ( rc>0 ) {
		sock = rc;
		rc = 0;
		bConnected = true;
	}
	print("%s\n",errmsgs[-rc]);
	return rc;
}
int tcpHost::read(char *buf, int len)
{
	int cch=recv(sock, buf, len, 0);
	if ( cch > 0 ) {
		buf[cch]=0;			
		char *p=buf;
		while ( (p=strchr(p, 0xff))!=NULL ) {
			char *p1 = buf+cch+1;
			char *p0 = (char *)telnet_options((unsigned char *)p);
			memcpy(p, p0, p1-p0);
			cch -= p0-p;
		}
	}
	return cch;
}
void tcpHost::write(const char *buf, int len ) 
{
	send( sock, buf, len, 0);
}
void tcpHost::disconn()
{
	closesocket(sock);
	bConnected = false;
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
//				if ( *p==TNO_ECHO ) bEcho = TRUE;
			}
			else if ( *p!=TNO_AHEAD ) {
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
//			if ( *p==TNO_ECHO ) bEcho = FALSE;
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

int sock_select( int s, int secs )
{
	struct timeval tv = { 0, 0 };
	tv.tv_sec = secs;
	fd_set svrset;
	FD_ZERO( &svrset );
	FD_SET( s, &svrset );
	return select( s+1, &svrset, NULL, NULL, &tv );
}
/**********************************TFTPd*******************************/
int tftpDaemon::tftp_read(const char *fn)
{
	char dataBuf[516], ackBuf[516];
	unsigned short nCnt, nRetry=0;
	int nLen, len;

	FILE *fp = fopen(fn, "rb");
    if ( fp==NULL ) return -1;
	nCnt=1;
	nLen = fread(dataBuf+4, 1, 512, fp);
    len = nLen;
    do {
		dataBuf[0]=0; dataBuf[1]=3;
		dataBuf[2]=(nCnt>>8)&0xff; dataBuf[3]=nCnt&0xff;
		send(tftp_s1, dataBuf, nLen+4, 0);

		if ( sock_select( tftp_s1, 5 ) == 1 ) {
			if ( recv(tftp_s1, ackBuf, 516, 0)==-1 ) break;
			if ( ackBuf[1]==4 && ackBuf[2]==dataBuf[2] && ackBuf[3]==dataBuf[3]) {
				nRetry=0;
				nCnt++;
				len = nLen;
				nLen = fread(dataBuf+4, 1, 512 , fp);
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	} while ( len==512 );
    fclose(fp);
    return 0;
}
int tftpDaemon::tftp_write(const char *fn)
{
	char dataBuf[516], ackBuf[516];
	unsigned short ntmp, nCnt=0, nRetry=0;
	int nLen=516;

	FILE *fp = fopen(fn, "wb");
    if ( fp==NULL ) return -1;
	while ( nLen > 0 ) {
		ackBuf[0]=0; ackBuf[1]=4;
		ackBuf[2]=(nCnt>>8)&0xff; ackBuf[3]=nCnt&0xff;
		send(tftp_s1, ackBuf, 4, 0);
		if ( nLen!=516 ) break;
		if ( sock_select( tftp_s1, 5) == 1 ) {
			nLen = recv(tftp_s1, dataBuf, 516, 0);
			if ( nLen == -1 ) break;
			ntmp=dataBuf[2];
			ntmp=(ntmp<<8)+dataBuf[3];
			if ( dataBuf[1]==3 && ntmp==nCnt+1 ) {
				fwrite(dataBuf+4, 1, nLen-4, fp);
				nRetry=0;
				nCnt++;
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	} 
    fclose(fp);
    return 0;
}
int tftpDaemon::connect()
{
	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
    bConnected = false;

	tftp_s0 = socket(AF_INET, SOCK_DGRAM, 0);
	tftp_s1 = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=INADDR_ANY;
	svraddr.sin_port=htons(69);
	if ( bind(tftp_s0, (struct sockaddr*)&svraddr,
							addrsize)!=-1 ) {
		svraddr.sin_port=0;
		if ( bind(tftp_s1, (struct sockaddr *)&svraddr,
								addrsize)!=-1 ) {
			bConnected = true;
            print( "TFTPd started\n" );
            return 0;
		}
	}
	else
		print( "Couldn't bind TFTPd port\n");
    return -1;
}
void tftpDaemon::disconn()
{
	closesocket(tftp_s1);
	closesocket( tftp_s0 );
	tftp_s0 = -1;
}
int tftpDaemon::read(char *buf, int len)
{
	char dataBuf[516];
	struct sockaddr_in clientaddr;
	socklen_t addrsize=sizeof(clientaddr);

	char fn[MAX_PATH];

	int ret;
	while ( (ret=sock_select( tftp_s0, 300 )) == 1 ) {
		ret = recvfrom( tftp_s0, dataBuf, 516, 0, 
                                    (struct sockaddr *)&clientaddr, &addrsize );
		if ( ret<0 ) break;
		::connect(tftp_s1, (struct sockaddr *)&clientaddr, addrsize);
		if ( dataBuf[1]==1  || dataBuf[1]==2 ) {
			int bRead = dataBuf[1]==1; 
			print( "%cRQ %s from %s\n", bRead?'R':'W', dataBuf+2,
									inet_ntoa(clientaddr.sin_addr) ); 
			strcpy(fn, rootDir); 
			strcat(fn, "/");
            strcat(fn, dataBuf+2);
			int rc = bRead ? tftp_read(fn) : tftp_write(fn);
			if ( rc!=0 ) {
				dataBuf[3]=dataBuf[1]; dataBuf[0]=0; dataBuf[1]=5; dataBuf[2]=0; 
				int len = sprintf( dataBuf+4, "Couldn't open %s\n", 
                                                        fn+strlen(rootDir) );
				send( tftp_s1, dataBuf, len+4, 0 );
			}
		}
	}
	print( ret==0 ? "TFTPD timed out" : "TFTPd stopped\n" );
	return -1;
}
/**********************************FTPd*******************************/
void ftpDaemon::sock_send(const char *reply )
{
	send( ftp_s1, reply, strlen(reply), 0 );
}
int ftpDaemon::connect()
{
	struct sockaddr_in serveraddr;
	int addrsize=sizeof(serveraddr);
	ftp_s0 = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serveraddr, 0, addrsize);
	serveraddr.sin_family=AF_INET;
	serveraddr.sin_addr.s_addr=INADDR_ANY;
	serveraddr.sin_port=htons(21);
	if ( bind(ftp_s0, (struct sockaddr*)&serveraddr, 
							addrsize) != -1 ) {
		if ( listen(ftp_s0, 1) != -1 ) {
			bConnected = true;
            print( "FTPd started\n" );
            return 0;
		}
	}
	else 
		print("Couldn't bind to FTP port\n");
    return -1;
}
void ftpDaemon::disconn()
{
    if ( ftp_s2!=-1 ) closesocket(ftp_s2);
    if ( ftp_s3!=-1 ) closesocket(ftp_s3);
    closesocket( ftp_s1 );
    closesocket( ftp_s0 );
	ftp_s0 = -1;
}

int ftpDaemon::read(char *buf, int len)
{
	unsigned long  dwIp;
	unsigned short wPort;
	unsigned int c[6], ii[2];
	char szBuf[MAX_PATH], workDir[MAX_PATH],fn[MAX_PATH];

	struct sockaddr_in svraddr, clientaddr;		// for data connection
    socklen_t addrsize=sizeof(clientaddr);
	int iRootLen = strlen( rootDir ) - 1;
	if ( rootDir[iRootLen]=='/' ) rootDir[iRootLen]=0;

	int ret0, ret1;
	while( (ret0=sock_select(ftp_s0, 900)) == 1 ) {
		ftp_s1 = accept( ftp_s0, (struct sockaddr*)&clientaddr, &addrsize );
		if ( ftp_s1 ==-1 ) continue;
		ftp_s2=-1; 								// ftp_s2 data connection
		ftp_s3=-1;								// ftp_s3 data listen

		sock_send( "220 Welcome\n");
		getpeername(ftp_s1, (struct sockaddr *)&clientaddr, &addrsize);
		print("FTPd: connected from %s\n", inet_ntoa(clientaddr.sin_addr));
		strcpy(workDir, "/");
		
		FILE *fp;
		int bPassive = false;
		int bUser=false, bPass=false;
		while ( (ret1=sock_select(ftp_s1, 300)) == 1 ) {
			int cnt=recv( ftp_s1, szBuf, 1024, 0 );
			if ( cnt<=0 ) {
				print( "FTPd: client disconnected\n");
				break;
			}

			szBuf[cnt-2]=0;
			print( "%s\n", szBuf); 
			char *p = strchr(szBuf, ' ');
			if ( p!=NULL ) 
				*p++ = 0;
			else 
				p = szBuf+cnt-2;
				
			// *** Process FTP commands ***
			if (strcmp("USER", szBuf) == 0){
				sock_send( "331 Password required\n");
				bUser = strcmp(p, "tiny")==0 ? true : false;
				continue;
			}
			if (strcmp("PASS", szBuf) == 0){
				bPass = bUser && strcmp(p, "term")==0;
				sock_send( bPass ? "230 Logged in okay\n": 
									"530 Login incorrect\n");
				continue;
			}
			if ( !bPass ) {
				sock_send( "530 Login required\n");
				 continue;
			}
			if (strcmp("SYST", szBuf) ==0 ){
				sock_send( "215 UNIX emulated by tinyTerm\n");
				continue;
			}
			else if(strcmp("TYPE", szBuf) == 0){
				sock_send( "200 Type can only be binary\n");
				continue;
			}
			else if(strcmp("PWD", szBuf) == 0 || strcmp("XPWD", szBuf) == 0){
				sprintf( szBuf, "257 \"%s\" is current directory\n", workDir );
				sock_send( szBuf);
				continue;
			}
			else if(strcmp("PORT", szBuf) == 0){
				sscanf(p, "%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], 
												&c[3], &c[4], &c[5]);
				ii[0] = c[1]+(c[0]<<8);
				ii[1] = c[3]+(c[2]<<8);
				wPort = c[5]+(c[4]<<8);
				dwIp = ii[1]+(ii[0]<<16);
				clientaddr.sin_addr.s_addr = htonl(dwIp);
				clientaddr.sin_port=htons(wPort);
				sock_send( "200 PORT command successful\n");
				continue;
			}

			strcpy(fn, rootDir);
			if ( *p=='/' ) {
				if ( p[1]!=0 ) strcat(fn, p);
			}
			else { 
				strcat(fn, workDir);
				if ( *p ) 
					strcat(fn, p);
				else
					fn[strlen(fn)-1] = 0;
			}
            if ( strstr(p, ".." )!=NULL ) {
				sock_send( "550 \"..\" is not allowed\n");
				continue;                    
            }

			if(strcmp("CWD", szBuf) == 0){
				struct stat statbuf;
                int rc = stat(fn, &statbuf);
				if ( rc==-1 || (!S_ISDIR(statbuf.st_mode)) ) 
					sock_send( "550 No such directory\n");
				else {
					strcpy(workDir, fn+strlen(rootDir));
					strcat(workDir, "/");
					sock_send( "250 CWD command sucessful\n");
				}
			}
			else if(strcmp("CDUP", szBuf) == 0){
				char *p = strrchr(workDir, '/');
				if ( p!=NULL ) {
					*p = 0;
					char *p1 = strrchr(workDir, '/');
					if ( p1!=NULL ) {
						p1[1]=0;
						sock_send( "250 CWD command sucessful\n");
						continue;
					}
					else 
						*p = '/';
				}
				sock_send( "550 No such file or directory.\n");
			}
			else if(strcmp("PASV", szBuf)==0 || strcmp("EPSV", szBuf)==0 ){
				getsockname(ftp_s1, (struct sockaddr *)&svraddr, &addrsize);
				svraddr.sin_port = 0;
				ftp_s3 = socket(AF_INET, SOCK_STREAM, 0);
				bind(ftp_s3, (struct sockaddr *)&svraddr, addrsize);
				listen(ftp_s3, 1);

				getsockname(ftp_s3, (struct sockaddr *)&svraddr, &addrsize);
				dwIp = svraddr.sin_addr.s_addr;
				wPort = svraddr.sin_port;

				if ( *szBuf=='p' || *szBuf=='P'  ) {
					ii[1]=(dwIp>>16)&0xffff; ii[0]=(dwIp)&0xffff;
					c[1]=(ii[0]>>8)&0xff; c[0]=(ii[0])&0xff;
					c[3]=(ii[1]>>8)&0xff; c[2]=(ii[1])&0xff;
					c[5]=(wPort>>8)&0xff; c[4]=(wPort)&0xff;
					sprintf(szBuf, "227 PASV Mode (%d,%d,%d,%d,%d,%d)\n",
										c[0], c[1], c[2], c[3], c[4], c[5]);
				}
				else sprintf(szBuf, "229 EPSV Mode (|||%d|)\n", ntohs(wPort));
				sock_send( szBuf);
				bPassive=true;
			}
			else if( strcmp("NLST", szBuf)==0 || strcmp("LIST", szBuf)==0 ){
                char pattern[256] = "*";
                char ldir[MAX_PATH];
				DIR *dir;
				struct dirent *dp;
				struct stat statbuf;
                int rc = stat(fn, &statbuf);
				if ( rc==-1 || (!S_ISDIR(statbuf.st_mode)) ) {
					p = strrchr(fn, '/');
					if ( p!=NULL ) { 
						*p++ = 0; 
						strncpy(pattern, p, 255); 
						strcpy(ldir, fn);
					}
					else {
						strcpy(ldir, ".");
						strncpy(pattern, fn, 255);
					}
                }
                else 
					strcpy(ldir, fn);

                if ( (dir=opendir(ldir))==NULL ){
					sock_send( "550 No such file or directory\n");
					continue;
				}
				sock_send( "150 Opening ASCII connection for list\n");
				if ( bPassive ) {
					ftp_s2 = accept(ftp_s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					ftp_s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(ftp_s2, (struct sockaddr *)&clientaddr, 
														sizeof(clientaddr));
				}
				int bNlst = szBuf[0]=='N';
				while ( (dp=readdir(dir)) != NULL ) {
					if ( fnmatch(pattern, dp->d_name, 0)==0 ) {
                        if ( bNlst ) {
                            sprintf(szBuf, "%s\n", dp->d_name);
                        }
                        else {
                            char buf[256];
                            strcpy(fn, ldir);
                            strcat(fn, "/");
                            strcat(fn, dp->d_name);
                            stat(fn, &statbuf);
                            strcpy(buf, ctime(&statbuf.st_mtime));
                            buf[24]=0;
                            sprintf(szBuf, "%c%c%c%c%c%c%c%c%c%c %s\t% 8lld  %s\n", 
                                          S_ISDIR(statbuf.st_mode)?'d':'-',
                                         (statbuf.st_mode&S_IRUSR)?'r':'-',
                                         (statbuf.st_mode&S_IWUSR)?'w':'-',
                                         (statbuf.st_mode&S_IXUSR)?'x':'-',
                                         (statbuf.st_mode&S_IRGRP)?'r':'-',
                                         (statbuf.st_mode&S_IWGRP)?'w':'-',
                                         (statbuf.st_mode&S_IXGRP)?'x':'-',
                                         (statbuf.st_mode&S_IROTH)?'r':'-',
                                         (statbuf.st_mode&S_IWOTH)?'w':'-',
                                         (statbuf.st_mode&S_IXOTH)?'x':'-',
                                         buf+4, statbuf.st_size, dp->d_name);
                        }
                        send(ftp_s2, szBuf, strlen(szBuf), 0);
                    }
				} 
				sock_send( "226 Transfer complete.\n");
				closesocket(ftp_s2);
				ftp_s2 = -1;
			}
			else if(strcmp("STOR", szBuf) == 0){
				fp = fopen(fn, "wb");
				if(fp == NULL){
					sock_send( "550 Unable to create file\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
				if ( bPassive ) {
					ftp_s2 = accept(ftp_s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					ftp_s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(ftp_s2, (struct sockaddr *)&clientaddr,
														sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0;
				char buff[4096];
				do {
					nLen = recv(ftp_s2, buff, 4096, 0);
					if ( nLen>0 ) {
						lSize += nLen;
						fwrite(buff, nLen, 1, fp);
					}
				}while ( nLen!=0);
				fclose(fp);
				sock_send( "226 Transfer complete\n");
				print( "FTPd %lu bytes received\n", lSize);
				closesocket(ftp_s2);
				ftp_s2 = -1;
			}
			else if(strcmp("RETR", szBuf) == 0){
				fp = fopen(fn, "rb");
				if(fp == NULL) {
					sock_send( "550 No such file or directory\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
				if ( bPassive ) {
					ftp_s2 = accept(ftp_s3, (struct sockaddr*)&clientaddr, 
																&addrsize);
				}
				else {
					ftp_s2 = socket(AF_INET, SOCK_STREAM, 0);
					::connect(ftp_s2, (struct sockaddr *)&clientaddr, 
														sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0;
				char buff[4096];
				do {
					nLen = fread(buff, 1, 4096, fp);
					if ( send(ftp_s2, buff, nLen, 0) == 0) break;
					lSize += nLen;
				}
				while ( nLen==4096);
				fclose(fp);
				sock_send( "226 Transfer complete\n");
				print( "FTPd %lu bytes sent\n", lSize);
				closesocket(ftp_s2);
				ftp_s2 = -1;
			}
			else if(strcmp("SIZE", szBuf) == 0){
				struct stat statbuf;
				if ( stat(fn, &statbuf)==-1 ) 
					sock_send( "550 No such file or directory\n");
				else {
					sprintf(szBuf, "213 %lld\n", statbuf.st_size);
					sock_send( szBuf );
				}
			}
			else if(strcmp("MDTM", szBuf) == 0) {
				struct stat statbuf;
				if ( stat(fn, &statbuf)==-1 ) 
					 sock_send( "550 No such file or directory\n");
				else {
					struct tm *t_mod = localtime( &statbuf.st_mtime);
					sprintf(szBuf, "213 %4d%02d%02d%02d%02d%02d\n", 
							t_mod->tm_year+1900,t_mod->tm_mon+1,t_mod->tm_mday, 
							t_mod->tm_hour, t_mod->tm_min, t_mod->tm_sec);	
					sock_send( szBuf );
				}
			}
			else if(strcmp("QUIT", szBuf) == 0){
				sock_send( "221 Bye!\n");
				break;
			}
			else {
				sock_send( "500 Command not supported\n");
			}
		}
		if( ret1==0 ) {
			sock_send( "500 Timeout\n");
			print( "FTPd: client timed out\n");
		}
		closesocket(ftp_s1);
	}
	print( ret0==0? "FTPd timed out\n" : "FTPd stopped\n" );
	return -1;
}

#ifdef WIN32
comHost::comHost(const char *address)
{
	bConnected = false;
	strcpy(portname, "\\\\.\\");
	strncat(portname, address, 27);

	char *p = strchr(portname, ':' );
	if ( p!=NULL ) { *p++ = 0; strcpy(settings, p); }
	if ( p==NULL || *p==0 ) strncpy(settings, "9600,n,8,1", 31);
}
int comHost::connect()
{
	hCommPort = CreateFile( portname, GENERIC_READ|GENERIC_WRITE, 0, NULL,
													OPEN_EXISTING, 0, NULL);
	if ( hCommPort==INVALID_HANDLE_VALUE) {
		print("Couldn't open COM port\n");
		return -1;
	}
	else {											
		COMMTIMEOUTS timeouts = {10, 0, 1, 0, 0};	//timeout and buffer settings
		if ( SetCommTimeouts(hCommPort,&timeouts)==0) {
			print("couldn't set comm timeout\n"); 	//WriteTotalTimeoutMultiplier = 0;
			return -1;								//WriteTotalTimeoutConstant = 0;
		}											//ReadTotalTimeoutConstant = 1;
		else {										//ReadIntervalTimeout = 10;
			bConnected = true;
			SetupComm( hCommPort, 4096, 1024 );		//comm buffer sizes
		}
	}
	if ( bConnected ) {
		DCB dcb;									// comm port settings
		memset(&dcb, 0, sizeof(dcb));
		dcb.DCBlength = sizeof(dcb);
		BuildCommDCBA(settings, &dcb);
		if ( SetCommState(hCommPort, &dcb)==0 ) {
			print("Invalid comm port settings\n" );
			bConnected = false;
			return -1;
		}
	}
	hExitEvent = CreateEvent( NULL, TRUE, FALSE, "COM exit" );
	return 0;
}
int comHost::read(char *buf, int len )
{
	DWORD dwCCH;
	while ( WaitForSingleObject( hExitEvent, 0 ) == WAIT_TIMEOUT ) { 
		if ( ReadFile(hCommPort, buf, len, &dwCCH, NULL) ) {
			if ( dwCCH > 0 ) return dwCCH;
		}
		else
			if ( !ClearCommError(hCommPort, NULL, NULL ) ) break;
	}
	CloseHandle(hExitEvent);
	CloseHandle(hCommPort);
	return -1;
}
void comHost::write(const char *buf, int len)
{
	DWORD dwWrite;
	WriteFile(hCommPort, buf, len, &dwWrite, NULL); 
}
void comHost::disconn()
{
	SetEvent( hExitEvent );
}
#endif //WIN32