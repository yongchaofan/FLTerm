//
// "$Id: sshHost.cxx 10033 2017-08-04 13:48:10 $"
//
// tcpHost and sshHost -- 
//
//	  host implementation for terminal simulator
//    used with the Fl_Term widget in flTerm
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

#include <unistd.h>
#include "sshHost.h"

void *reader( void *pv )
{
	tcpHost *host = (tcpHost *)pv;
	if ( host->connect()==0 ) host->read();
	return NULL;
}
/**********************************tcpHost******************************/

int tcpHost::set_term(Fl_Term *pTerm) 
{ 
	term=pTerm;
	if (term!=NULL) 
		term->set_host(this);
	else 
		if ( state()!=HOST_IDLE ) disconn();

	return true;
}

void tcpHost::start(const char *address )
{
	if ( address!=NULL ) strncpy(hostname, address, 255);
	pthread_create( &reader_Id, NULL, reader, this );
}
int tcpHost::connect()
{
	struct hostent *lpstHost;
	struct sockaddr_in neaddr;
	int addrsize = sizeof(neaddr);
	char *p=strchr(hostname, ':');
	if ( p!=NULL ) port=atoi(p+1);
	
	sock= socket(AF_INET, SOCK_STREAM, 0);
	neaddr.sin_family=AF_INET;
	neaddr.sin_port=htons(port);
	if ( p!=NULL ) *p=0;
	unsigned long lAddr = inet_addr(hostname);
	if ( lAddr==INADDR_NONE ) {
		if ( (lpstHost=gethostbyname(hostname)) )
			lAddr = *(u_long*)lpstHost->h_addr;
		else {
			disp("Couldn't resolve address\n");
			goto TCP_Close;
		}
	}
	neaddr.sin_addr.s_addr = lAddr;
	if ( p!=NULL ) *p=':';

	disp("Connecting...");
	stage = HOST_CONNECTING;
//	if ( term!=NULL ) {
//		pthread_t dotter_Id;
//		pthread_create( &dotter_Id, NULL, TERM_dotter,  term);
//	}
	if ( ::connect(sock, (struct sockaddr *)&neaddr, addrsize)!=-1 ) {
		stage = HOST_CONNECTED;
		return 0;
	}
	disp("TCP connection failed\n");
TCP_Close:
	stage = HOST_IDLE;
	closesocket(sock);
	return TCP_FAILURE;
}
void tcpHost::read()
{
	int cch, negotiated=false;
	char buf[1536];
	while ( (cch=recv(sock, buf, 1500, 0)) > 0 ) {
		buf[cch]=0;			
		if ( !negotiated ) {
			char *p = strchr(buf, 0xff);
//			if ( p==NULL ) negotiated = true;
			while ( p!=NULL ) {
				char *p1 = buf+cch+1;
				char *p0 = (char *)telnet_options((unsigned char *)p);
				memcpy(p, p0, p1-p0);
				cch -= p0-p;
				p = strchr(p, 0xff);
			}
		}
		term->append( buf, cch );
	}
	disp("\n!!!Disconnected!!!\n");
	closesocket(sock);
	stage = HOST_IDLE;
}
void tcpHost::disp( const char *msg ) 
{
	if ( term!=NULL ) 
		term->append(msg, strlen(msg));
	else 
		fprintf(stderr, "%s:%s\n", hostname, msg);
}
void tcpHost::write( const char *buf ) 
{
	send( sock, buf, strlen(buf), 0);
}
void tcpHost::disconn()
{
	closesocket(sock);
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

int sshHost::get_keys( int bPass )
{
	bReturn=false; bPassword=bPass; cursor=0;
	term->append(bPassword?"password:":"username:", 9);
	int old_cursor = cursor;
	for ( int i=0; i<300&&bReturn==false; i++ ) {
		if ( state()!=HOST_AUTHENTICATING ) break;
		if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
		usleep(100000);
	}
	return bReturn;
}
const char *knownhostfile=".ssh/known_hosts";
const char *pubkeyfile=".ssh/id_rsa.pub";
const char *privkeyfile=".ssh/id_rsa";
int sshHost::connect()
{
	int rc = tcpHost::connect();
	if ( rc!=0 ) return rc;

	stage = HOST_CONNECTING;
	session = libssh2_session_init();
	do { 
		rc = libssh2_session_handshake(session, sock);
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
	if ( rc!=0 ) {
		disp("SSH session failed\n");
		goto Session_Close;
	}
	
	stage = HOST_AUTHENTICATING;
    LIBSSH2_KNOWNHOSTS *nh;
	struct libssh2_knownhost *host;
    int type, check;
	size_t len;
	const char *key, *fingerprint;
	key =  libssh2_session_hostkey(session, &len, &type);
	fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
	if ( !key ) goto Session_Close;
	if ( type>0 ) type++;
	switch ( type ) {
		case 2: disp(" ssh-rsa"); break;
		case 3: disp(" ssh-dss"); break;
		default:disp(" unknown"); break;
	}
	{
		char buf[128]=" key fingerprint: ";
		for( int i=0; i<20; i++) {
			sprintf(buf+i*3+18, "%02X ", (unsigned char)fingerprint[i] );
		}
	    disp(buf);disp("\n\n");
	}

	nh = libssh2_knownhost_init(session);
    if ( !nh ) goto Session_Close;
    /* read all hosts from here */ 
    libssh2_knownhost_readfile(nh, knownhostfile,
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MISMATCH ) {
		if ( type==((host->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) 
			disp("!!!host key changed, proceed with care!!!\n\n");
		else 
			check=LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;
	}
 
	if ( check==LIBSSH2_KNOWNHOST_CHECK_NOTFOUND ) {
		libssh2_knownhost_addc(nh, hostname, "", key, len, "**flTerm**", 10,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW|
								(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), &host);
		FILE *fp = fopen(knownhostfile, "a+");
		if ( fp ) {
			char buf[2048];
			libssh2_knownhost_writeline(nh, host, buf, 2048, &len, 
								LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			fprintf(fp, "%s\n", buf );
			disp("host key added to .ssh/known_hosts.\n\n");
			fclose(fp);
		} 
		else 
			disp("couldn't open .ssh/known_hosts to add host key.\n\n");
	}
    libssh2_knownhost_free(nh);

	if ( *username==0 && term!=NULL ) 
		if ( !get_keys(false) ) goto Session_Close; 
	
	if ( *password==0 ) {
// null authentication
		if ( libssh2_userauth_list(session, username, strlen(username))==NULL )
			goto authenticated;	
// try public key 
		if ( !libssh2_userauth_publickey_fromfile(session, username, 
										pubkeyfile, privkeyfile, "") )
			goto authenticated;
	}
	else {
		if ( !libssh2_userauth_password(session, username, password) )
			goto authenticated;
	}	
	for ( int rep=0; rep<3&&term!=NULL; rep++ ){	// 3 chances to enter password
		if ( !get_keys(true) ) goto Session_Close;		
		if ( !libssh2_userauth_password(session, username, password) ) 
			goto authenticated;
	}
	*username=0; *password=0;
	disp("too many tries!\n");
	goto Session_Close;
	
authenticated:
	channel = libssh2_channel_open_session(session);
	if ( channel ) {
		stage = HOST_CONNECTED;
		return 0;
	}
	
	disp("Unable to open session\n");
Session_Close:
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
	closesocket(sock);
	stage = HOST_IDLE;
	return SSH_FAILURE;
}
void sshHost::read()
{
	if (libssh2_channel_request_pty(channel, "xterm")) {
		disp("Failed requesting pty\n");
		goto Channel_Close;
	}
	if (libssh2_channel_shell(channel)) {
		disp("Unable to request shell\n");
		goto Channel_Close; 
	}
//	libssh2_keepalive_config(session, false, 60);	
//FlashWave will close on keep alive
	if ( term!=NULL ) 
		send_size(term->get_size_x(), term->get_size_y());

	while ( libssh2_channel_eof(channel) == 0 ) {
		char buf[1536];
		int cch=libssh2_channel_read(channel, buf, 1500);
		if ( cch > 0 ) {
			buf[cch]=0;
			term->append(buf, cch);
		}
		else if ( cch!=LIBSSH2_ERROR_TIMEOUT ) break;
	}

	disp("\n!!!Disconnected!!!\n");
Channel_Close:
	libssh2_channel_close(channel);
	libssh2_channel_free(channel);
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
	closesocket(sock);
	stage = HOST_IDLE;
}
void sshHost::write( const char *buf ) {
	if ( state()==HOST_CONNECTED )
		libssh2_channel_write( channel, buf, strlen(buf));
		
	if ( state()==HOST_AUTHENTICATING ) {
		char *keys = bPassword?password:username;
		int len = strlen(buf);
		for ( int i=0; i<len; i++ ) {
			switch ( buf[i] ) {
			case '\015': keys[cursor++]=0; bReturn=true; 
						 term->append("\n", 1);
						 break;
			case '\177': if ( --cursor<0 ) cursor=0; 
						else if ( !bPassword )
								term->append( "\010 \010", 3);
						break;
			default: keys[cursor++]=buf[i]; 
					if ( cursor>31 ) cursor=31;
					if ( !bPassword ) term->append(buf+i, 1);
			}	
		}
	}
}
void sshHost::send_size(int sx, int sy) 
{
	libssh2_channel_request_pty_size( channel, sx, sy );
}