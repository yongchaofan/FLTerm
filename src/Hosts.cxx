//
// "$Id: Hosts.cxx 31174 2018-05-25 21:55:10 $"
//
// tcpHost sshHost confHost
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

const char *errmsgs[] = { 
"Disconnected",
"Connection",
"SSH Session",
"Verification",
"Authentication",
"Channel",
"Subsystem",
"Shell"};

const char *kb_gets(const char *prompt, int echo);
void* host_reader(void *p)
{
	((Fan_Host *)p)->read();
	return NULL;
}
int Fan_Host::connect()
{
	return pthread_create(&readerThread, NULL, host_reader, this);
}
void Fan_Host::print( const char *fmt, ... ) 
{
	assert(host_cb!=NULL);
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	do_callback(buff, len);
}
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
    struct sockaddr_in *addr_in = (struct sockaddr_in *)ainfo->ai_addr;
    addr_in->sin_port = htons(port);

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
		char *p=buf;
		while ( (p=strchr(p, 0xff))!=NULL ) {
			char *p1 = buf+cch+1;
			char *p0 = (char *)telnet_options((unsigned char *)p);
			memcpy(p, p0, p1-p0);
			cch -= p0-p;	//cch could become 0 after this
		}
		if ( cch>0 ) do_callback(buf, cch);
	}
	bConnected = false;

TCP_Close:
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);	//indicates disconnected
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
		if ( readerThread==0 && *buf=='\r' )
			return connect();
	}
	return 0;
}
void tcpHost::disconn()
{
	if ( bConnected ) {
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

sshHost::sshHost(const char *name) : tcpHost(name)
{
	port = 0;
	*username = 0; 
	*password = 0;
	*passphrase = 0;

	char options[256];
	strncpy(options, name, 255);
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
    for ( int i=0; i<num_prompts; i++) {
		char *prompt = strdup(prompts[i].text);
		prompt[prompts[i].length] = 0;
		const char *p = kb_gets(prompt, prompts[i].echo);
		free(prompt);
		if ( p!=NULL ) {
			responses[i].text = strdup(p);
			responses[i].length = strlen(p);
		}
    }
} 
void sshHost::write_keys(const char *buf, int len)
{ 
	for ( int i=0; i<len&&bGets; i++ ) {
		if ( buf[i]=='\015' ) {
			keys[cursor++]=0; 
			bReturn=true; 
			do_callback("\n", 1);
		}
		else if ( buf[i]=='\177' ) {
			if ( cursor>0 ) {
				cursor--; 
				if ( !bPassword ) 
					do_callback("\010 \010", 3);									
			}
		}
		else {
			keys[cursor++]=buf[i]; 
			if ( cursor>63 ) cursor=63;
			if ( !bPassword ) do_callback(buf+i, 1);
		}
	}	
}
char* sshHost::ssh_gets( const char *prompt, int echo )
{
	do_callback(prompt, strlen(prompt));
	cursor=0;
	bGets = true;
	bReturn = false; 
	bPassword = !echo;
	int old_cursor = cursor;
	for ( int i=0; i<300&&bGets; i++ ) {
		if ( bReturn ) return keys;
		if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
		usleep(100000);
	}
	return NULL;
}

int sshHost::ssh_authentication()
{
	int rc = -5;
	if ( *username==0 ) {
		const char *p = ssh_gets("username:", true);
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
	if ( strstr(authlist, "password")!=NULL ) {
		if ( *password ) {			// password provided, it either works or not
			if ( !libssh2_userauth_password(session, username, password) )
				return 0;			//password authentication passed
			else
				return rc;			//no need to get password interactively
		}
		else {						//password was not set 
				for ( int i=0; i<3; i++ ) {	//get it interactively 
				const char *p = ssh_gets("password:", false);
				if ( p!=NULL ) {
					strncpy(password, p, 31);
					if ( !libssh2_userauth_password(session,username,password) )
						return 0;				//password authentication passed
				}
				else 
					break;
			}
		}
	}
	if ( strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ )
			if (!libssh2_userauth_keyboard_interactive(session, username,
                                              			&kbd_callback) )
				return 0;	
	}
	*username=0; *password=0; *passphrase=0;
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
int sshHost::read()
{
	do_callback("Connecting", 0);	//indicates connecting
	int rc =  tcp();
	if ( rc<0 ) goto TCP_Close;

	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		rc=-2; goto Session_Close; 
	}
	if ( ssh_knownhost()!=0 ) { 
		rc=-3; goto Session_Close; 
	}
	if ( ssh_authentication()!=0 ) { 
		rc=-4; goto Session_Close; 
	}

	//	libssh2_keepalive_config(session, false, 60);  
	//	FlashWave will close connection on keep alive
	if ( !(channel=libssh2_channel_open_session(session)) ) 
		{ rc=-5; goto Session_Close; }
	if ( libssh2_channel_request_pty(channel, "xterm")) 
		{ rc=-6; goto Channel_Close; }
	if ( libssh2_channel_shell(channel)) 
		{ rc=-7; goto Channel_Close; }

	libssh2_session_set_blocking(session, 0); 
	bConnected = true;
	do_callback("Connected", 0);
	while ( libssh2_channel_eof(channel)==0 ) {
		int cch;
		char buf[4096];
		mtx.lock();
		cch=libssh2_channel_read(channel, buf, 4096);
		mtx.unlock();
		if ( cch>=0 ) {
			if ( cch>0 ) do_callback(buf, cch);
		}
		else {	//cch<0
			if ( cch!=LIBSSH2_ERROR_EAGAIN ) break;
			if ( wait_socket()==-1 ) break;
		}
	}
	bConnected = false;
	*username = 0;
	*password = 0;

Channel_Close:
	libssh2_channel_free(channel);
Session_Close:
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
TCP_Close:	
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);
	readerThread = 0;
	return rc;
}
int sshHost::write(const char *buf, int len) 
{
	if ( bConnected ) {
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
		if ( cch<0 ) { disconn(); total = cch; }
		return total;
	}
	else {
		if ( readerThread!=0 ) 
			write_keys(buf, len);
		else
			if ( *buf=='\r' ) connect();
	}
	return 0;
}
void sshHost::send_size(int sx, int sy) 
{
	if ( bConnected ) 
		libssh2_channel_request_pty_size( channel, sx, sy );
}
void sshHost::disconn()
{
	if ( readerThread!=0 ) {
		shutdown(sock, 1);
		bGets = false;
		pthread_join(readerThread, NULL);
	}
}
int sshHost::scp_read(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	do {
        mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		mtx.unlock();
        if (!scp_channel) {
            if ( libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                print("\n\033[31mSCP: couldn't open remote file\033[32m%s\033[37m", rpath);
                return -1;
            }
            else 
                if ( wait_socket()==-1 ) return -1;
        }
    } while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp==NULL ) {
		print("\n\033[31mSCP: couldn't write local file \033[32m%s\033[37m", lpath);
		return -2;
	}
	print("\n\033[32mSCP: %s\t\033[37m ", lpath);

	time_t start = time(NULL);
    libssh2_struct_stat_size got = 0;
    libssh2_struct_stat_size total = fileinfo.st_size;
    int rc, nmeg=0;
    while  ( got<total ) {
        char mem[1024*32];
        int amount=sizeof(mem);
        if ( (total-got) < amount) {
            amount = (int)(total-got);
        }
        mtx.lock();
		rc = libssh2_channel_read(scp_channel, mem, amount);
        mtx.unlock();
		if (rc > 0) {
            fwrite(mem, 1,rc,fp);
            got += rc;
			if ( ++nmeg%32==0 ) print(".");
        }
		else 
			if ( rc==LIBSSH2_ERROR_EAGAIN ) 
				if ( wait_socket()!=-1 ) continue; 
    }
	fclose(fp);

    int duration = (int)(time(NULL)-start);
	print(" %lld bytes in %d seconds", got, duration);
    libssh2_channel_free(scp_channel);
	return 0;
}
int sshHost::scp_write(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct stat fileinfo;
    FILE *fp =fopen(lpath, "rb");
	if ( !fp ) {
		print("\n\033[31mSCP: couldn't read local file\033[32m%s\033[37m", lpath);
		return -1;
	}
    stat(lpath, &fileinfo);	//fl_stat causes wrong file size been sent on windows
	
    do {
        mtx.lock();
		scp_channel = libssh2_scp_send(session, rpath, fileinfo.st_mode & 0777,
                           (unsigned long)fileinfo.st_size);
		mtx.unlock();
        if ( (!scp_channel) && 
			 (libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN)) {
            print("\n\033[31mSCP: couldn't open remote file \033[32m%s\033[37m",rpath );
			fclose(fp);
            return -2;
        }
    } while ( !scp_channel );
 
    print("\n\033[32mSCP: %s\t\033[37m", rpath);
	time_t start = time(NULL);
	size_t nread = 0, total = 0;
	int rc, nmeg = 0;
    while ( nread==0 ) {
		char mem[1024*32];
        if ( (nread=fread(mem, 1, sizeof(mem), fp))<=0 ) break;// end of file 
        total += nread;
		if ( ++nmeg%32==0 ) print(".");

        char *ptr = mem;
        while ( nread>0 ) {
            mtx.lock();
			rc = libssh2_channel_write(scp_channel, ptr, nread);
			mtx.unlock();
			if ( rc==LIBSSH2_ERROR_EAGAIN ) {
                if ( wait_socket()==-1 ) break;
				continue;
            }
            if ( rc<0 ) {
                print("\033[31minterrupted at %ld bytes\033[37m", total);
                break;
            }
            else {
                nread -= rc;
                ptr += rc;
            }
        }
    }/* only continue if nread was drained */ 
	fclose(fp);
    int duration = (int)(time(NULL)-start);
	print("%ld bytes in %d seconds", total, duration);

	do {
		mtx.lock();
		rc = libssh2_channel_send_eof(scp_channel);
		mtx.unlock();
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
	do {
		mtx.lock();
		rc = libssh2_channel_wait_eof(scp_channel);
		mtx.unlock();
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
    do {
		mtx.lock();
		rc = libssh2_channel_wait_closed(scp_channel);
		mtx.unlock();
	} while ( rc == LIBSSH2_ERROR_EAGAIN);
    libssh2_channel_free(scp_channel);
	return 0;
}

int sshHost::tun_local(const char *lpath, const char *rpath)
{
	struct sockaddr_in sin;
    char sockopt = 1;
    char shost[256], dhost[256], *client_host, *p;
    unsigned int sport, dport, client_port;

	strncpy(shost, lpath, 255);
	strncpy(dhost, rpath, 255);
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);
	tunStarted = true;

	socklen_t sinlen=sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(sport);
	sin.sin_addr.s_addr = inet_addr(shost);
    if ( sin.sin_addr.s_addr==INADDR_NONE) {
        print("IP address error\n");
        return -1;
    }
	
	int forwardsock;
	int listensock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listensock == -1) {
        fprintf(stderr, "failed to open listen socket!\n");
        return -1;
    } 
    
    setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    if ( bind(listensock, (struct sockaddr *)&sin, sinlen)==-1 ) {
        print("address:port %s:%d invalid or in use\n", shost, sport);
        goto shutdown0;
    }
    if ( listen(listensock, 2)==-1 ) {
        fprintf(stderr, "listen error\n");
        goto shutdown0;
    }
	print("\nTunnel listening on %s:%d\n", shost, sport);

/*	int tun_sock = tcp();
	if ( tun_sock<=0 ) return -1;
	LIBSSH2_SESSION *tun_session = libssh2_session_init();
	if ( libssh2_session_handshake(tun_session, tun_sock)!=0 ) { 
		fprintf(stderr, "failed to setup tunnel session\n");
		goto shutdown0; 
	}
	if ( libssh2_userauth_password(tun_session, username, password)!=0 ) {
		fprintf(stderr, "failed to authenticate tunnel session\n");
		goto shutdown0;
	}
	libssh2_session_set_blocking(tun_session, 0);
*/
	LIBSSH2_CHANNEL *tun_channel;
accept_again:
	forwardsock = accept(listensock, (struct sockaddr *)&sin, &sinlen);
    if ( forwardsock==-1) {
        fprintf(stderr, "accept error\n");
        goto shutdown0;
    }
	client_host = inet_ntoa(sin.sin_addr);
	client_port = ntohs(sin.sin_port);

	do {
		mtx.lock();
		tun_channel = libssh2_channel_direct_tcpip_ex(session, 
									dhost, dport, client_host, client_port);
		mtx.unlock();
		if (!tun_channel) {
			if ( libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN ) 
			{
				printf("Could not open tunneling channel! Does server support it?\n");
				goto shutdown1;
			}
		}
	} while ( !tun_channel );
	print("\nTunneling from local %s:%d to remote %s:%d\n",
       				client_host, client_port, dhost, dport); 
 
	while ( true ) {
		int rc, i;
		struct timeval tv;
		int len, wr;
	    fd_set fds;
        FD_ZERO(&fds);
		FD_SET(sock, &fds);
        FD_SET(forwardsock, &fds);
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        rc = select(forwardsock + 1, &fds, NULL, NULL, &tv);
        if ( rc==-1 ) {
            fprintf(stderr, "select error\n");
            goto shutdown2;
        }
		if ( rc==0 ) continue;
        if ( FD_ISSET(forwardsock, &fds) ) {
			char buf[16384];
            len = recv(forwardsock, buf, sizeof(buf), 0);
            if ( len<=0 ) {
				fprintf(stderr, "socket read error %d\n", len);
                goto shutdown2;
            }
            wr = 0; 
			while ( wr<len ) {
                mtx.lock();
                i = libssh2_channel_write(tun_channel, buf+wr, len-wr);
                mtx.unlock();
                if ( i==LIBSSH2_ERROR_EAGAIN ) continue;
				if ( i<0 ) {
					fprintf(stderr, "libssh2_channel_write error: %d\n", i);
					goto shutdown2;
				}
				wr += i;
            }
        }
        if ( FD_ISSET(sock, &fds) ) while ( true ) {
			char buff[16384];
            mtx.lock();
            len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
 	        mtx.unlock();
 	        if ( len==LIBSSH2_ERROR_EAGAIN ) break;
            if ( len<0 ) {
                fprintf(stderr, "libssh2_channel_read error: %d", len);
                goto shutdown2;
            }
            for ( wr=0; wr<len; wr+=i ) {
                i = send(forwardsock, buff + wr, len - wr, 0);
                if ( i<=0 ) {
                    fprintf(stderr, "socket write error %d\n", i);
                    goto shutdown2;
                }
            }
            if ( libssh2_channel_eof(tun_channel) ) {
				print("\nTunneling server at %s:%s disconnected!\n",
                    						dhost, dport);
                goto shutdown2;
            }
        }
    }
 
shutdown2:
    print("\nTunneling client at %s:%d disconnected!\n", 
								client_host, client_port);
    if (tun_channel) libssh2_channel_free(tun_channel);
	goto accept_again;
shutdown1:
    closesocket(forwardsock);
shutdown0:
//	libssh2_session_disconnect(tun_session, "Normal Shutdown");
//	libssh2_session_free(tun_session);
//	closesocket(tun_sock);
    closesocket(listensock);
	return 0;
}
int sshHost::tun_remote(const char *rpath, const char *lpath)
{
    int forwardsock=0;
    struct sockaddr_in sin;
    socklen_t sinlen = sizeof(sin);
	int r_listenport;
	LIBSSH2_LISTENER *listener = NULL;
    LIBSSH2_CHANNEL *tun_channel = NULL;

	char shost[256], dhost[256], *p;
	unsigned short sport, dport;
	strncpy(shost, rpath, 255);
	strncpy(dhost, lpath, 255);
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);
	tunStarted = true;

	do {
		mtx.lock();
		listener = libssh2_channel_forward_listen_ex(session, shost,
										sport, &r_listenport, 1);
		mtx.unlock();
		if (!listener) {
			if ( libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN ) {
				fprintf(stderr, "Could not start the tcpip-forward listener!\n");
				goto shutdown;
			}       
			else if ( wait_socket()==-1 ) goto shutdown;
		}
	} while ( !listener );
    print("\nTunnel server is listening on R%s:%d\n", shost, r_listenport);

	do {
		mtx.lock();
	    tun_channel = libssh2_channel_forward_accept(listener);
		mtx.unlock();
		if (!tun_channel) {
			if ( libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN ) {
				fprintf(stderr, "Could not accept connection!\n");
				goto shutdown;
			}       
			else if ( wait_socket()==-1 ) goto shutdown;
		}
	} while ( !tun_channel );

    forwardsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( forwardsock ==-1 ) {
        fprintf(stderr, "failed to open forward socket!\n");
        goto shutdown;
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(dport);
	sin.sin_addr.s_addr = inet_addr(dhost);
    if ( sin.sin_addr.s_addr==INADDR_NONE ) {
        fprintf(stderr, "inet_addr error\n");
        goto shutdown;
    }
    if ( ::connect(forwardsock, (struct sockaddr *)&sin, sinlen)==-1 ) {
        fprintf(stderr, "connect error\n");
        goto shutdown;
    }
 
    print("\nTunneling from remote %s:%d to local %s:%s\n",
    							    shost, r_listenport, dhost, dport);
	fd_set fds;
    struct timeval tv;
    ssize_t len, wr;
    char buf[16384];
	int rc, i;
    while ( true ) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
		FD_SET(forwardsock, &fds);
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        rc = select(forwardsock + 1, &fds, NULL, NULL, &tv);
        if ( rc==-1 ) {
            fprintf(stderr, "select error\n");
            goto shutdown;
        }
        if (rc && FD_ISSET(forwardsock, &fds)) {
            len = recv(forwardsock, buf, sizeof(buf), 0);
            if (len < 0) {
                fprintf(stderr, "socket read error\n");
                goto shutdown;
            } 
			else if ( len==0 ) {
                fprintf(stderr, "The local server at %s:%d disconnected!\n",
                    shost, sport);
                goto shutdown;
            }
            wr = 0;
            for ( wr=0; wr<len; wr+=i) {
                mtx.lock();
				i = libssh2_channel_write(tun_channel, buf, len);
				mtx.unlock();
                if ( i<0 ) {
                    if ( i==LIBSSH2_ERROR_EAGAIN ) break;
					fprintf(stderr, "libssh2_channel_write: %d\n", i);
                    goto shutdown;
                }
            }
        }
        if (rc && FD_ISSET(forwardsock, &fds)) while ( true ) {
            mtx.lock();
			len = libssh2_channel_read(tun_channel, buf, sizeof(buf));
			mtx.unlock();
            if (len < 0) {
            	if ( len==LIBSSH2_ERROR_EAGAIN ) break;
                fprintf(stderr, "libssh2_channel_read: %d", (int)len);
                goto shutdown;
            }
            for ( wr=0; wr<len; wr+=i ) {
                i = send(forwardsock, buf + wr, len - wr, 0);
                if (i <= 0) {
                    fprintf(stderr, "socket write error \n");
                    goto shutdown;
                }
            }
            if (libssh2_channel_eof(tun_channel)) {
                print("\nTunneling client at %s:%d disconnected!\n",
                    shost, r_listenport);
                goto shutdown;
            }
        }
    }
 
shutdown:
    closesocket(forwardsock);
    if (tun_channel) libssh2_channel_free(tun_channel);
    if (listener) libssh2_channel_forward_cancel(listener);
	return 0;
}

/*********************************confHost*******************************/
const char *IETF_HELLO="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
<capabilities><capability>urn:ietf:params:netconf:base:1.0</capability>\
</capabilities></hello>]]>]]>";
const char *IETF_MSG="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"%d\">\n\
%s</rpc>]]>]]>";
confHost::confHost(const char *name):sshHost(name)
{
	if ( port==22 ) port = 830;
}	

int confHost::read()
{
	do_callback("Connecting", 0);	//indicates connecting
	int rc = tcp();
	if ( rc<0 ) goto TCP_Close;
	
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		rc=-2;	goto Session_Close; 
	}
	if ( ssh_knownhost()!=0 ) { 
		rc=-3; goto Session_Close; 
	}
	if ( ssh_authentication()!=0 ) { 
		rc=-4; goto Session_Close; 
	}
	channel=libssh2_channel_open_session(session);
	if ( !channel ) { 
		rc=-5; goto Session_Close; 
	}
	if ( libssh2_channel_subsystem(channel, "netconf") ) { 
		rc=-6; goto Channel_Close; 
	}
	//must be nonblocking for 2 channels on the same session
	libssh2_session_set_blocking(session, 0); 
	libssh2_channel_write( channel, IETF_HELLO, strlen(IETF_HELLO) );
	msg_id = 0;
	rd = rd2 = 0;
	*reply = *notif = 0;
	channel2 = NULL;
	bConnected = true;
	do_callback("Connected", 0);
	char *delim;
	int len, len2;
	while ( libssh2_channel_eof(channel)==0 ) {
		if ( wait_socket()==-1 ) break;
		mtx.lock();
		len=libssh2_channel_read(channel,reply+rd,BUFLEN-rd);
		mtx.unlock();
		if ( len>0 ) {
			rd += len; if ( rd==BUFLEN ) rd = 0;
			reply[rd] = 0;
			while ( (delim=strstr(reply, "]]>]]>")) != NULL ) {
				*delim=0; 
				do_callback(reply, delim-reply);
				delim+=6;
				rd -= delim-reply;
				memmove(reply, delim, rd+1); 
			}
		}
		else 
			if ( len!=LIBSSH2_ERROR_EAGAIN ) break;	//len<=0
			
		if ( channel2==NULL ) continue;
		mtx.lock();
		len2=libssh2_channel_read(channel2,notif+rd2,BUFLEN-rd2);
		mtx.unlock();
		if ( len2>0 ) {
			rd2 += len2; if ( rd2==BUFLEN ) rd2 = 0;
			notif[rd2] = 0;
			while( (delim=strstr(notif, "]]>]]>")) != NULL ) {
				*delim=0;
				do_callback(notif, delim-notif);
				delim+=6;
				rd2 -= delim - notif;
				memmove(notif, delim, rd2+1); 
			}
		}
		else 
			if ( len2!=LIBSSH2_ERROR_EAGAIN ) break;//len2<=0	
	}
	bConnected = false;

Channel_Close:
	libssh2_channel_free(channel);
	if ( channel2 ) libssh2_channel_free(channel2);
Session_Close:
	libssh2_session_disconnect(session, "Shutdown");
	libssh2_session_free(session);
TCP_Close:
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);
	readerThread = 0;
	return rc;
}
int confHost::write(const char *msg, int len)
{
	if ( bConnected ) {
		char buf[8192];
		len = sprintf(buf, IETF_MSG, ++msg_id, msg);
		print("\n%s\n", buf);

		int total=0, cch=0;
		while ( total<len ) {
			mtx.lock();
			cch=libssh2_channel_write(channel, buf+total, len-total); 
			mtx.unlock();
			if ( cch>=0 ) 
				total += cch;
			else  {
				if ( cch==LIBSSH2_ERROR_EAGAIN ) {
					if ( wait_socket()==-1 ) break;
				}
				else break;
			}
		}
		if ( cch<0 ) { 
			disconn(); 
			return cch; 
		}
		else 
			return msg_id;
	}
	else {
		if ( readerThread!=0 ) 
			write_keys(msg, len);
		else
			connect();
	}
	return 0;
} 
int confHost::write2(const char *msg, int len)
{
	assert(bConnected);
	if ( channel2!=NULL ) return 0;
	do {
		mtx.lock();
		channel2=libssh2_channel_open_session(session);
		mtx.unlock();
        if ( !channel2 && 
			 (libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN)) {
            print("\n\033[31mcouldn't open channel2\033[37m" );
            return -1;
        }
    } while ( !channel2 );
	
	int rc;
	do {
		mtx.lock();
		rc = libssh2_channel_subsystem(channel2, "netconf");
		mtx.unlock();
		if ( rc && 
			 (libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN)) {
            print("\n\033[31mcouldn't netconf channel2\033[37m" );
			return -2; 
		}
	} while ( rc );

	char buf[8192];
	len = sprintf(buf, IETF_MSG, ++msg_id, msg);
	print("\n%s\n", buf);
	mtx.lock();
	do {
		rc = libssh2_channel_write( channel2, IETF_HELLO, strlen(IETF_HELLO) );
	} while ( rc==LIBSSH2_ERROR_EAGAIN );

	do {
		rc = libssh2_channel_write( channel2, buf, strlen(buf)); 
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
	mtx.unlock();
	return 0;
} 
