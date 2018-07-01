//
// "$Id: Hosts.cxx 21019 2018-05-25 21:55:10 $"
//
// tcpHost sshHost confHost comHost
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
void term_print(Fl_Term *pTerm, const char *s);
const char *term_gets(Fl_Term *pTerm, const char *prompt, int echo);

const char *errmsgs[] = { 
"TCP connected",
"IP failure",
"TCP failure",
"Session failure",
"Hostkey failure",
"Authentication failure",
"Channel failure",
"Subsystem failure",
"Shell failure",
};

void Fan_Host::print( const char *fmt, ... ) 
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	if ( term!=NULL ) {
		vsnprintf(buff, 4096, (char *)fmt, args);
		term_print(term, buff);
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

sshHost::sshHost(const char *address) : tcpHost(address)
{
	port = 0;
	*username = 0; 
	*password = 0;
	*passphrase = 0;

	char options[256];
	strncpy(options, address, 255);
	char *p = options;
	char *phost=NULL, *pport=NULL, *puser=NULL, *ppass=NULL, *pphrase=NULL;
	while ( (p!=NULL) && (*p!=0) ) {
		while ( *p==' ' ) p++;
		if ( *p=='-' ) {
			switch ( p[1] ) {
			case 'l': p+=3; puser = p; break;
			case 'p': if ( p[2]=='w' ) { p+=4; ppass = p; } 
						if ( p[2]=='p' ) { p+=4; pphrase = p; }
						break;
			case 'P': p+=3; pport = p; break;
			}
			p = strchr( p, ' ' ); 
			if ( p!=NULL ) *p++ = 0;
		}
		else { 
			phost = p; 
			p = strchr( phost, '@' );
			if ( p!=NULL ) {
				puser = phost; 
				phost = p+1;
				*p=0; 
			}
			p = strchr(phost, ':');
			if ( p!=NULL ) {
				char *p1 = strchr(p+1, ':');
				if ( p1==NULL ) { //ipv6 address if more than one ':'
					pport = p+1;
					*p=0;
				}
			}
			break;
		}
	}
	if ( phost!=NULL ) strncpy(hostname, phost, 63);
	if ( pport!=NULL ) port = atoi(pport);
	if ( puser!=NULL ) strncpy(username, puser, 63);
	if ( ppass!=NULL ) strncpy(password, ppass, 63);
	if ( pphrase!=NULL ) strncpy(passphrase, pphrase, 63);
	if ( port==0 ) port = 22;
} 
const char *knownhostfile=".ssh/known_hosts";
const char *pubkeyfile=".ssh/id_rsa.pub";
const char *privkeyfile=".ssh/id_rsa";
const char *keytypes[]={"unknown", "ssh-rsa", "ssh-dss", "ssh-ecdsa"};
int sshHost::ssh_knownhost()
{
    int type, check, buff_len;
	size_t len;
	char buff[256];

	const char *key = libssh2_session_hostkey(session, &len, &type);
	if ( key==NULL ) return -4;
	buff_len=sprintf(buff, "%s fingerprint: ", keytypes[type]);
	if ( type>0 ) type++;
	
	const char *fingerprint = libssh2_hostkey_hash(session, 
										LIBSSH2_HOSTKEY_HASH_SHA1);	
	if ( fingerprint==NULL ) return -4;
	for( int i=0; i<20; i++, buff_len+=3) 
		sprintf(buff+buff_len, "%02X ", (unsigned char)fingerprint[i] );
    
    LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(session);
	if ( nh==NULL ) return -4;
	struct libssh2_knownhost *host;
	libssh2_knownhost_readfile(nh, knownhostfile,
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MISMATCH ) {
		if ( type==((host->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			print("%s\n !!!host key changed, proceed with care!!!\n", buff);
		}
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
			fprintf(fp, "%s", buf );
			fclose(fp);
			print("%s host key added to .ssh/known_hosts.\n", buff);
		} 
		else 
			print("couldn't open .ssh/known_hosts to add host key.\n\n");
	}
    libssh2_knownhost_free(nh);
	return 0;
}
static void kbd_callback(const char *name, int name_len,
                         const char *instruction, int instruction_len,
                         int num_prompts,
                         const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                         LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                         void **abstract)
{
//	(void)name;
//	(void)name_len;
//	(void)instruction;
//	(void)instruction_len;
    for ( int i=0; i<num_prompts; i++) {
		char *prom = strdup(prompts[i].text);
		prom[prompts[i].length] = 0;
		const char *p = term_gets(NULL, prom, prompts[i].echo==0);
		if ( p!=NULL ) {
			responses[i].text = strdup(p);
			responses[i].length = strlen(p);
		}
    }
//	(void)prompts;
//	(void)abstract;
} 
int sshHost::ssh_authentication()
{
	int rc = -5;
	const char *p = NULL;
	if ( *username==0 ) {
		if ( term!=NULL ) p = term_gets(term, "username:", false);
		if ( p==NULL ) return rc; 
		strncpy(username, p, 31);
	}
	char *authlist=libssh2_userauth_list(session, username, strlen(username));
	if ( authlist==NULL ) return 0;	// null authentication passed
	if ( strstr(authlist, "publickey")!=NULL ) {
		if ( !libssh2_userauth_publickey_fromfile(session, username, 
										pubkeyfile, privkeyfile, passphrase) )
			return 0;				// public key authentication passed
	}
	if ( term!=NULL && strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ )
			if (!libssh2_userauth_keyboard_interactive(session, username,
                                              			&kbd_callback) )
				return 0;	
	}
	else if ( strstr(authlist, "password")!=NULL ) {
		if ( *password!=0 )
			if ( !libssh2_userauth_password(session, username, password) )
				return 0;				//password authentication passed
		if ( term!=NULL ) for ( int i=0; i<3; i++ ) {
			p = term_gets(term, "password:", true);
			if ( p!=NULL ) {
				strncpy(password, p, 31);
				if ( !libssh2_userauth_password(session, username, password) )
					return 0;				//password authentication passed
			}
			else 
				break;
		}
	}
	*username=0; *password=0; *passphrase=0;
	return rc;
}
int sshHost::connect()
{
	int rc =  tcp();
	if ( rc>0 ) {
		sock = rc;
		session = libssh2_session_init();
		if ( libssh2_session_handshake(session, sock)!=0 ) { 
			rc=-3; goto Session_Close; 
		}
		if ( ssh_knownhost()!=0 ) { 
			rc=-4; goto Session_Close; 
		}
		if ( ssh_authentication()!=0 ) { 
			rc=-5; goto Session_Close; 
		}

	//	libssh2_keepalive_config(session, false, 60);  
	//	FlashWave will close connection on keep alive
		if ( !(channel=libssh2_channel_open_session(session)) ) 
			{ rc=-6; goto Session_Close; }
		if ( libssh2_channel_request_pty(channel, "xterm")) 
			{ rc=-7; goto Session_Close; }
		if ( libssh2_channel_shell(channel)) 
			{ rc=-8; goto Session_Close; }

		libssh2_session_set_blocking(session, 0); 
		bConnected = true;
		return 0;

Session_Close:
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
	}
	print("%s\n", errmsgs[-rc]);
	return rc;
}
int sshHost::wait_socket() 
{
//	struct timeval timeout = {10,0};
	fd_set rfd, wfd;
	FD_ZERO(&rfd); FD_ZERO(&wfd);
	int dir = libssh2_session_block_directions(session);
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) FD_SET(sock, &rfd);;
	if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) FD_SET(sock, &wfd);;
	int	rc=select(sock+1, &rfd, &wfd, NULL, NULL );
	if ( rc==-1 ) {
		if ( errno==0 ) return 0;
		fprintf(stderr, "select error %d %s\n", errno, strerror(errno));
	}
	return rc;
}
int sshHost::read(char *buf, int len)
{
	int cch=-1;
	
	if ( libssh2_channel_eof(channel)==0 ) {
		while ( true ) {
			mtx.lock();
			cch=libssh2_channel_read(channel, buf, len);
			mtx.unlock();
			if ( cch==LIBSSH2_ERROR_EAGAIN ) {
				if ( wait_socket()==-1 ) break;
			}
			else break;
		}
	}
	if ( cch<0 ) {
		libssh2_channel_close(channel);
		libssh2_channel_free(channel);
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);	
		*username = 0;
		*password = 0;
	}
	return cch;
}
void sshHost::write(const char *buf, int len) {
	if ( !bConnected ) return;
	int total=0, cch=0;
	while ( total<len ) {
		mtx.lock();
		cch=libssh2_channel_write(channel, buf+total, len-total); 
		mtx.unlock();
		if ( cch<0 ) {
			if ( cch==LIBSSH2_ERROR_EAGAIN ) {
				if ( wait_socket()==-1 ) break;
			}
			else break;
		}
		else total += cch; 
	}
	if ( cch<0 ) {
		libssh2_channel_close(channel);
		libssh2_channel_free(channel);
	}
}
void sshHost::send_size(int sx, int sy) 
{
	if ( bConnected ) 
		libssh2_channel_request_pty_size( channel, sx, sy );
}

/*********************************confHost*******************************/
const char *ietf_hello="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
  <capabilities>\
    <capability>urn:ietf:params:netconf:base:1.0</capability>\
  </capabilities>\
</hello>\
]]>]]>";
const char *ietf_subscribe="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\">\
  <create-subscription xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\">\
    <stream>NETCONF</stream>\
  </create-subscription>\
</rpc>\
]]>]]>";
const char *oroadm_subscribe="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\">\
  <create-subscription xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\">\
    <stream>OPENROADM</stream>\
  </create-subscription>\
</rpc>\
]]>]]>";
const char *ietf_header="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"%d\">";
const char *ietf_footer="</rpc>]]>]]>";
int confHost::connect()
{
	int rc = sock = tcp();
	if ( rc<0 ) goto TCP_Close;
	
	bConnected = false;
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		rc=-3;	goto Channel_Close; 
	}
	if ( ssh_knownhost()!=0 ) { 
		rc=-4; goto Channel_Close; 
	}
	if ( ssh_authentication()!=0 ) { 
		rc=-5; goto Channel_Close; 
	}
	channel = libssh2_channel_open_session(session);
	if ( !channel ) { 
		rc=-6; goto Channel_Close; 
	}
	if ( libssh2_channel_subsystem(channel, "netconf") ) { 
		rc=-7; goto Channel_Close; 
	}
	//must be nonblocking for 2 channels on the same session
	libssh2_session_set_blocking(session, 0); 
	int len;
	for ( int i=0; i<100; i++ ) {
		mtx.lock();
		len=libssh2_channel_read(channel,reply, BUFLEN-1);
		mtx.unlock();
		if (len==LIBSSH2_ERROR_EAGAIN ) {
			usleep(100000); continue;
		}
		else break;
	}
	if ( len<0 ) { rc = -8; goto Channel_Close; }
	reply[len] = 0;
	if ( strstr(reply, "interleave")!=NULL ) {
		libssh2_channel_write( channel, ietf_hello, strlen(ietf_hello) );
		if ( strstr(reply, "openroadm")!=NULL ) 
			libssh2_channel_write( channel, oroadm_subscribe, strlen(oroadm_subscribe) );
		else
			libssh2_channel_write( channel, ietf_subscribe, strlen(ietf_subscribe) );
		bConnected = true;
		msg_id = 0;
		rd = 0;
		*reply = 0;
		channel2 = NULL;
		return 0;		//success
	}
	else {
		libssh2_channel_close(channel);
		libssh2_channel_free(channel);
		channel = NULL;
		libssh2_session_disconnect(session, "Reader Shutdown");
		libssh2_session_free(session);
		closesocket(sock);
		return connect2();
	} 
Channel_Close:
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
TCP_Close:
	print("%s\n", errmsgs[-rc]);
	return rc;
}
int confHost::connect2()
{
	int rc = sock = tcp();
	if ( rc<0 ) goto TCP_Close2;
	
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		rc=-3;	goto Channel_Close2; 
	}
	if ( ssh_knownhost()!=0 ) { 
		rc=-4; goto Channel_Close2; 
	}
	if ( ssh_authentication()!=0 ) { 
		rc=-5; goto Channel_Close2; 
	}
	channel = libssh2_channel_open_session(session);
	channel2 = libssh2_channel_open_session(session);
	if ( !channel || !channel2 ) { 
		rc=-6; goto Channel_Close2; 
	}
	if ( libssh2_channel_subsystem(channel, "netconf") 
		|| libssh2_channel_subsystem(channel2, "netconf") ) { 
		rc=-7; goto Channel_Close2; 
	}
	//must be nonblocking for 2 channels on the same session
	libssh2_session_set_blocking(session, 0); 
	bConnected = true;
	msg_id = 0;
	rd = rd2 = 0;
	*reply = 0;
	*notif = 0;
	libssh2_channel_write( channel, ietf_hello, strlen(ietf_hello) );
	libssh2_channel_write( channel2, ietf_hello, strlen(ietf_hello) );
	libssh2_channel_write( channel2, ietf_subscribe, strlen(ietf_subscribe) );
	return 0;		//success
Channel_Close2:
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
TCP_Close2:
	print("%s\n", errmsgs[-rc]);
	return rc;
}
int confHost::read(char *buf, int buflen)
{
	int len, len2, rc; 
again:
	while ( true ) {
		char *delim = strstr(reply, "]]>]]>");
		if ( delim != NULL ) {
			*delim=0; 
			rc = delim - reply; 
			memcpy(buf, reply, rc);
			rd -= rc+6;
			memmove(reply, delim+6, rd+1); 
			return rc;
		}
		mtx.lock();
		len=libssh2_channel_read(channel,reply+rd,buflen-rd);
		mtx.unlock();
		if ( len<=0 ) break;

		rd += len; 
		reply[rd] = 0;
	}
	while ( channel2!=NULL ) {
		char *delim = strstr(notif, "]]>]]>");
		if ( delim != NULL ) {
			*delim=0;
			rc = delim - notif; 
			memcpy(buf, notif, rc);
			rd2 -= rc+6;
			memmove(notif, delim+6, rd2+1); 
			return rc;
		}
		mtx.lock();
		len2=libssh2_channel_read(channel2,notif+rd2,buflen-rd2);
		mtx.unlock();
		if ( len2<=0 ) break;

		rd2 += len2; 
		notif[rd2] = 0;
	}

	if ( len==LIBSSH2_ERROR_EAGAIN ) {
		if ( wait_socket()>0 ) goto again;
	}

	if ( channel ) {
		libssh2_channel_close(channel);
		libssh2_channel_free(channel);
	}
	if ( channel2 ) {
		libssh2_channel_close(channel2);
		libssh2_channel_free(channel2);
	}
	libssh2_session_disconnect(session, "Reader Shutdown");
	libssh2_session_free(session);
	return -1;
}
void confHost::write(const char *msg, int len)
{
	if ( channel==NULL ) return;
	char header[256];
	sprintf(header, ietf_header, ++msg_id);
	sshHost::write( header, strlen(header) );
	sshHost::write( msg, len );
	sshHost::write( "</rpc>]]>]]>", 12 );
	if ( term!=NULL ) 
		print("\n%s%s%s\n", header, msg, ietf_footer);
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

#include <wincrypt.h>
char * SHA ( char *msg )
{
const unsigned int ALGIDs[] = { CALG_SHA1, CALG_SHA_256, CALG_SHA_384, 0, CALG_SHA_512 };
	static char result[256];
	BYTE        bHash[64];
	DWORD       dwDataLen = 0;
	HCRYPTPROV  hProv = 0;
	HCRYPTHASH  hHash = 0;

	int algo = *msg - '1';
	msg = strchr(msg, '=')+1;
	CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
	CryptCreateHash( hProv, ALGIDs[algo], 0, 0, &hHash);
	CryptHashData( hHash, (BYTE *)msg, strlen(msg), 0);
	CryptGetHashParam( hHash, HP_HASHVAL, NULL, &dwDataLen, 0);
	CryptGetHashParam( hHash, HP_HASHVAL, bHash, &dwDataLen, 0);

	if(hHash) CryptDestroyHash(hHash);    
	if(hProv) CryptReleaseContext(hProv, 0);

	for ( int i=0; i<dwDataLen; i++ ) sprintf(result+i*2, "%02x", bHash[i]);
	return result;
}

#endif //WIN32