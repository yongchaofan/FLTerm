//
// "$Id: ssh2.cxx 36261 2018-08-31 21:55:10 $"
//
// sshHost sftpHost confHost
//
// implementation for terminal simulator
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
#include <assert.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#ifndef WIN32
	#include <pwd.h>
#endif
#include "ssh2.h"

static const char *errmsgs[] = { 
"Disconnected",
"Connection",
"Session",
"Verification",
"Authentication",
"Channel",
"Subsystem",
"Shell"};

const char *kb_gets(const char *prompt, int echo);
sshHost::sshHost(const char *name) : tcpHost(name)
{
	port = 0;
	*username = 0; 
	*password = 0;
	*passphrase = 0;
	session = NULL;
	channel = NULL;
	
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

	*homedir = 0;
#ifdef WIN32
	const char *dir = getenv("USERPROFILE");
#else
	const char *dir = getenv("HOME");
	if ( dir==NULL ) dir = getpwuid(getuid())->pw_dir;
#endif
	if ( dir!=NULL ) {
		strncpy(homedir, dir, MAX_PATH-2);
		homedir[MAX_PATH-2] = 0;
		strcat(homedir, "/");
	}
}
const char *keytypes[]={"unknown", "ssh-rsa", "ssh-dss", "ssh-ecdsa"};
int sshHost::ssh_knownhost()
{
    int type, check, buff_len;
	size_t len;
	char buff[256];
	char knownhostfile[MAX_PATH+64];
	strcpy(knownhostfile, homedir);
	strcat(knownhostfile, ".ssh/known_hosts");

	const char *key = libssh2_session_hostkey(session, &len, &type);
	if ( key==NULL ) return -4;
	buff_len=sprintf(buff, "host key fingerprint(%s):\n", keytypes[type]);
	if ( type>0 ) type++;
	
	const char *fingerprint = libssh2_hostkey_hash(session, 
										LIBSSH2_HOSTKEY_HASH_SHA1);	
	if ( fingerprint==NULL ) return -4;
	for( int i=0; i<20; i++, buff_len+=3) 
		sprintf(buff+buff_len, "%02X ", (unsigned char)fingerprint[i] );
    print("%s\n", buff);
    
    LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(session);
	if ( nh==NULL ) return -4;
	struct libssh2_knownhost *host;
	libssh2_knownhost_readfile(nh, knownhostfile,
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MATCH ) {
		print("\033[32mmatch found in .ssh/known_hosts\n\n");
		goto Done;
	}
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MISMATCH ) {
		if ( type==((host->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			print("\033[31m!!!host key changed! proceed with care!!!\n\n");
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
		if ( fp==NULL ) {
			int len = strlen(knownhostfile);
			knownhostfile[len-12]=0;
#ifdef WIN32
			mkdir(knownhostfile);
#else
			mkdir(knownhostfile, 0700);
#endif
			knownhostfile[len-12]='/';
			fp = fopen(knownhostfile, "a+");
		}
		if ( fp ) {
			char buf[2048];
			libssh2_knownhost_writeline(nh, host, buf, 2048, &len, 
								LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			fprintf(fp, "%s", buf );
			fclose(fp);
			print("\033[32madded to .ssh/known_hosts.\n\n");
		} 
		else 
			print("\033[33mcouldn't open .ssh/known_hosts to add host key\n\n");
	}
Done:
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
	if ( *password && strstr(authlist, "password")!=NULL ) {
		if ( !libssh2_userauth_password(session, username, password) ) 
			return 0;		//password authentication pass
		else	// password provided, it either works or not
			return rc;		//won't try anyting else on failure 
	}
	if ( strstr(authlist, "publickey")!=NULL ) {
		char pubkeyfile[MAX_PATH+64], privkeyfile[MAX_PATH+64];
		strcpy(pubkeyfile, homedir);
		strcat(pubkeyfile, ".ssh/id_rsa.pub");
		strcpy(privkeyfile, homedir);
		strcat(privkeyfile, ".ssh/id_rsa");

		if ( !libssh2_userauth_publickey_fromfile(session, username, 
								pubkeyfile, privkeyfile, passphrase) ) {
			print("\033[32mpublic key authentication passed\n");
			return 0;			// public key authentication passed
		}
	}
	if ( strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ )
			if (!libssh2_userauth_keyboard_interactive(session, username,
                                              			&kbd_callback) ) 
				return 0;
	}
	if ( strstr(authlist, "password")!=NULL ) {
		//password was not set, get it interactively
		for ( int i=0; i<3; i++ ) {
			const char *p = ssh_gets("password:", false);
			if ( p!=NULL ) {
				strncpy(password, p, 31);
				if (!libssh2_userauth_password(session,username,password))
					return 0;//password authentication passed
			}
			else 
				break;
		}
	}
	*username=0; *password=0; *passphrase=0;
	return rc;
}
int sshHost::wait_socket() 
{
	fd_set fds, *rfd=NULL, *wfd=NULL;
	FD_ZERO(&fds); FD_SET(sock, &fds);
	int dir = libssh2_session_block_directions(session);
	if ( dir==0 ) return 1;
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) rfd = &fds;
	if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) wfd = &fds;
	return select(sock+1, rfd, wfd, NULL, NULL );
//	if ( rc==-1 ) 
//		fprintf(stderr, "select error %d %s\n", errno, strerror(errno));
}
int sshHost::read()
{
	do_callback("Connecting", 0);	//indicates connecting
	int rc = tcp();
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
	//	some host will close connection on keep alive
	if ( !(channel=libssh2_channel_open_session(session)) ) 
		{ rc=-5; goto Session_Close; }
	if ( libssh2_channel_request_pty(channel, "xterm")) 
		{ rc=-6; goto Channel_Close; }
	if ( libssh2_channel_shell(channel)) 
		{ rc=-7; goto Channel_Close; }

	libssh2_session_set_blocking(session, 0); 

	bConnected = true;
	do_callback("Connected", 0);
	while ( true ) {
		int len;
		char buf[4096];
		if ( rc!=0 ) break;
		mtx.lock();
		len=libssh2_channel_read(channel, buf, 4096);
		mtx.unlock();
		if ( len>0 ) 
			do_callback(buf, len);
		else {
			if ( len==LIBSSH2_ERROR_EAGAIN ) 
				if ( wait_socket()>0 ) continue;
			if ( len==0 ) 
				if ( !libssh2_channel_eof(channel) )continue;
			break;
		}
	}
	bConnected = false;
	*username = 0;
	*password = 0;

Channel_Close:
	mtx.lock();
	libssh2_channel_free(channel);
	mtx.unlock();
	channel = NULL;
Session_Close:
	mtx.lock();
	libssh2_session_free(session);
	mtx.unlock();
	session = NULL;
TCP_Close:	
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);	//tell Fl_Term host has disconnected
	readerThread = 0;
	return rc;
}
int sshHost::write(const char *buf, int len) 
{
	if ( bConnected ) {
		if ( bEcho ) do_callback(buf, len);
		int total=0, cch=0;
		while ( total<len ) {
			mtx.lock();
			cch=libssh2_channel_write(channel, buf+total, len-total); 
			mtx.unlock();
			if ( cch>0 ) 
				total += cch;
			else  {
				if ( cch==LIBSSH2_ERROR_EAGAIN )
					if ( wait_socket()>0 ) continue;
				disconn();
				break;
			}
		}
		return cch<0 ? cch : total;
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
	if ( bConnected ) {
		mtx.lock();
		libssh2_channel_request_pty_size( channel, sx, sy );
		mtx.unlock();
	}
}
void sshHost::disconn()
{
	if ( readerThread!=0 ) {
		bGets = false;
//		if ( session!=NULL ) {
//			mtx.lock();
//			libssh2_session_disconnect(session, "Shutdown");
//			mtx.unlock();
//		}
		shutdown(sock, 1);	//SD_SEND=1 on Win32, SHUT_WR=1 on posix
		pthread_join(readerThread, NULL);
	}
}
int sshHost::scp_read(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	int err_no=0;
	do {
        mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
        if (!scp_channel) {
            if ( err_no==LIBSSH2_ERROR_EAGAIN) 
                if ( wait_socket()>0 ) continue;
			print("\n\033[31mSCP: couldn't open remote file \033[32m%s",rpath);
            return -1;
        }
    } while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp==NULL ) {
		print("\n\033[31mSCP: couldn't write to \033[32m%s", lpath);
		return -2;
	}
	print("\n\033[32mSCP: %s\t ", lpath);

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
		else {
			if ( rc==LIBSSH2_ERROR_EAGAIN ) 
				if ( wait_socket()>0 ) continue; 
            print("\033[31minterrupted at %ld bytes", total);
            break;
		}
    }
	fclose(fp);

    int duration = (int)(time(NULL)-start);
	print(" %lld bytes in %d seconds", got, duration);
	mtx.lock();
    libssh2_channel_free(scp_channel);
	mtx.unlock();
	return 0;
}
int sshHost::scp_write(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct stat fileinfo;
    FILE *fp =fopen(lpath, "rb");
	if ( !fp ) {
		print("\n\033[31mSCP: couldn't read from \033[32m%s", lpath);
		return -1;
	}
    stat(lpath, &fileinfo);	//fl_stat causes wrong file size been sent on win32
	
    int err_no = 0;
	do {
        mtx.lock();
		scp_channel = libssh2_scp_send(session, rpath, fileinfo.st_mode & 0777,
                           (unsigned long)fileinfo.st_size);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
        if ( !scp_channel ) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN ) 
				if ( wait_socket()>0 ) continue;
			print("\n\033[31mSCP: couldn't open remote file \033[32m%s",rpath);
			fclose(fp);
			return -2;
		}
    } while ( !scp_channel );
 
    print("\n\033[32mSCP: %s\t", rpath);
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
			if ( rc>0 ) {
                nread -= rc;
                ptr += rc;
			}
			else {
				if ( rc==LIBSSH2_ERROR_EAGAIN ) 
               		if ( wait_socket()>0 ) continue;
                print("\033[31minterrupted at %ld bytes", total);
                break;
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
	mtx.lock();
    libssh2_channel_free(scp_channel);
	mtx.unlock();
	return 0;
}
void sshHost::tun_worker(int tun_sock, LIBSSH2_CHANNEL *tun_channel)
{
	char buff[16384];
	int rc, i;
	int len, wr;
    fd_set fds;
	struct timeval tv;
	while ( true ) {
		mtx.lock();
		rc = libssh2_channel_eof(tun_channel);
		mtx.unlock();
		if ( rc!=0 ) break;

        FD_ZERO(&fds);
		FD_SET(sock, &fds);
        FD_SET(tun_sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        rc = select(tun_sock + 1, &fds, NULL, NULL, &tv);
        if ( rc==-1 ) {
            print("\n\033[31mselect error\033[37m\n");
            break;
        }
		if ( rc==0 ) continue;
        if ( FD_ISSET(tun_sock, &fds) ) {
            len = recv(tun_sock, buff, sizeof(buff), 0);
            if ( len<=0 ) break;
			for ( wr=0, i=0; wr<len; wr+=i ) {
                mtx.lock();
                i = libssh2_channel_write(tun_channel, buff+wr, len-wr);
                mtx.unlock();
                if ( i==LIBSSH2_ERROR_EAGAIN ) continue;
				if ( i<=0 ) goto shutdown;
            }
        }
		if ( FD_ISSET(sock, &fds) ) while ( true )
		{
            mtx.lock();
            len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
 	        mtx.unlock();
 	        if ( len==LIBSSH2_ERROR_EAGAIN ) break;
            if ( len<=0 ) goto shutdown;
            for ( wr=0, i=0; wr<len; wr+=i ) {
                i = send(tun_sock, buff + wr, len - wr, 0);
                if ( i<=0 ) break;
            }
        }
    }
shutdown:
    print("\n\033[32mTunnel %d disconnected!\n", tun_sock); 
	mtx.lock();
    libssh2_channel_free(tun_channel);
	mtx.unlock();
	closesocket(tun_sock);
}
int sshHost::tun_local(const char *lpath, const char *rpath)
{
    char sockopt = 1;
    char shost[256], dhost[256], *client_host, *p;
    unsigned short sport, dport, client_port;

	strncpy(shost, lpath, 255);
	strncpy(dhost, rpath, 255);
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);

	struct sockaddr_in sin;
 	socklen_t sinlen=sizeof(sin);
    struct addrinfo *ainfo;    
    if ( getaddrinfo(shost, NULL, NULL, &ainfo)!=0 ) {
		print("\033[31minvalid address: %s\n", shost);
		return -1;
	}
    int listensock = socket(ainfo->ai_family, SOCK_STREAM, 0);
    setsockopt(listensock,SOL_SOCKET,SO_REUSEADDR,&sockopt,sizeof(sockopt));
    ((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(sport);
    int rc = bind(listensock, ainfo->ai_addr, ainfo->ai_addrlen);
    freeaddrinfo(ainfo);
	if ( rc==-1 ) {
        print("\033[31mport %d invalid or in use\n", sport);
        closesocket(listensock);
        return -1;
    }
    if ( listen(listensock, 2)==-1 ) {
        print("\033[31mlisten error\n");
        closesocket(listensock);
        return -1;
    }
	print("\n\033[32mTunnel listening on %s:%d\n", shost, sport);

	int tun_sock;
	LIBSSH2_CHANNEL *tun_channel;
	while ((tun_sock=accept(listensock,(struct sockaddr*)&sin,&sinlen))!=-1) {
		client_host = inet_ntoa(sin.sin_addr);
		client_port = ntohs(sin.sin_port);
		do {
			int rc = 0;
			mtx.lock();
			tun_channel = libssh2_channel_direct_tcpip_ex(session, 
									dhost, dport, client_host, client_port);
			if (!tun_channel) rc = libssh2_session_last_errno(session);
			mtx.unlock();
			if ( !tun_channel ) {
				if ( rc==LIBSSH2_ERROR_EAGAIN )	
					if ( wait_socket()>0 ) continue;
				print("\033[31mCouldn't establish tunnel, is it supported?\n");
				closesocket(tun_sock);
				goto shutdown;
			}
		} while ( !tun_channel );
		print("\n\033[32mTunnel %d from local %s:%d to remote %s:%d\n",
       			tun_sock,client_host, client_port, dhost, dport); 
		std::thread tun_thread(&sshHost::tun_worker, this, 
										tun_sock, tun_channel);
		tun_thread.detach();
	}
shutdown:
    closesocket(listensock);
	return 0;
}
int sshHost::tun_remote(const char *rpath, const char *lpath)
{
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

	do {
		int err_no = 0;
		mtx.lock();
		listener = libssh2_channel_forward_listen_ex(session, shost,
										sport, &r_listenport, 1);
		if ( !listener ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
		if (!listener) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN ) 
				if ( wait_socket()>0 ) continue;
			print("\033[31mCouldn't listen for tunnel, is it supported?\n");
			return -1;
		}
	} while ( !listener );
    print("\n\033[32mTunnel server is listening on R%s:%d\n", shost, r_listenport);

again:
	do {
		int err_no = 0;
		mtx.lock();
	    tun_channel = libssh2_channel_forward_accept(listener);
		if ( !tun_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
		if (!tun_channel) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN )
				if ( wait_socket()>0 ) continue;
			print("\033[31mCouldn't accept tunnel connection!\n");
			return -1;
		}
	} while ( !tun_channel );

    struct addrinfo *ainfo;    
    if ( getaddrinfo(shost, NULL, NULL, &ainfo)==0 ) {
		int tun_sock = socket(ainfo->ai_family, SOCK_STREAM, 0);
		((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(dport);
		int rc = ::connect(tun_sock,  ainfo->ai_addr, ainfo->ai_addrlen);
		freeaddrinfo(ainfo);
		
		if ( rc==0 ) {
			print("\n\033[31mTunnel %d from remote %s:%d to local %s:%d\n",
						tun_sock, shost, r_listenport, dhost, dport);
			std::thread tun_thread(&sshHost::tun_worker, this, 
												tun_sock, tun_channel);
			tun_thread.detach();
			goto again;
		}
		else {
			print("\n\033[31mremote tunneling connect error\n");
			closesocket(tun_sock);
		}
	}
	else {
		print("\n\033[31mremote tunneling address error\n");
	}	
	mtx.lock();
	libssh2_channel_free(tun_channel);
	libssh2_channel_forward_cancel(listener);
	mtx.unlock();
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
	int len, len2;
	do {
		char *delim;
		do { 
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
		}
		while ( len>0 );
		if ( len!=LIBSSH2_ERROR_EAGAIN ) break;

		if ( channel2!=NULL ) {
			do {
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
			}
			while ( len2>0 );
			if ( len2!=LIBSSH2_ERROR_EAGAIN ) break;
		}
	}
	while ( wait_socket()>0 );
	bConnected = false;

Channel_Close:
	mtx.lock();
	libssh2_channel_free(channel);
	if ( channel2 ) libssh2_channel_free(channel2);
	mtx.unlock();
Session_Close:
	mtx.lock();
	libssh2_session_free(session);
	mtx.unlock();
TCP_Close:
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);	//tell Fl_Term host has disconnected
	readerThread = 0;
	return rc;
}
int confHost::write(const char *msg, int len)
{
	if ( bConnected ) {
		char buf[8192];
		len = sprintf(buf, IETF_MSG, ++msg_id, msg);
		if ( bEcho ) do_callback(buf, len);

		int total=0, cch=0;
		while ( total<len ) {
			mtx.lock();
			cch=libssh2_channel_write(channel, buf+total, len-total); 
			mtx.unlock();
			if ( cch>0 ) 
				total += cch;
			else  {
				if ( cch==LIBSSH2_ERROR_EAGAIN ) 
					if ( wait_socket()>0 ) continue;
				disconn();
				break;
			}
		}
		return cch<0 ? cch : msg_id;
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
            print("\n\033[31mcouldn't open channel2" );
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
            print("\n\033[31mcouldn't netconf channel2" );
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

/*******************sftpHost*******************************/
int sftpHost::sftp_lcd(char *cmd)
{
	if ( cmd==NULL || *cmd==0 ) {
		char buf[4096];
		if ( getcwd(buf, 4096)!=NULL ) {
			print("\t\033[32m%s \033[37mis current local directory\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't get current local directory\n");
		}
	}
	else {
		while ( *cmd==' ' ) cmd++;
		if ( chdir(cmd)==0 ) {
			print("\t\033[32m%s\033[37m is now local directory!\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't change local directory to\033[32m%s\n", cmd);
		}
	}
	return 0;
}
int sftpHost::sftp_cd(char *path) 
{
	char newpath[1024];
	if ( path!=NULL ) {
		LIBSSH2_SFTP_HANDLE *sftp_handle;
		sftp_handle = libssh2_sftp_opendir(sftp_session, path);
		if (!sftp_handle) {
			print("\t\033[31mUnable to change working directory to\033[32m%s\n", path);
			return 0;
		}
	    libssh2_sftp_closedir(sftp_handle);		
		int rc = libssh2_sftp_realpath(sftp_session, path, newpath, 1024);
		if ( rc>0 ) strcpy( realpath, newpath );
	}
	print("\t\033[32m%s \033[37mis current working directory\n", realpath);
	return 0;
}
int sftpHost::sftp_ls(char *path, int ll)
{
    char *pattern = NULL;
	char mem[512], longentry[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_opendir(sftp_session, path);
    if (!sftp_handle) {
        if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
			print("\t\033[31mUnable to open dir\033[32m%s\n", path);
        	return 0;
		}
		pattern = strrchr(path, '/');
		if ( pattern!=path ) {
			*pattern++ = 0;
			sftp_handle = libssh2_sftp_opendir(sftp_session, path);
		}
		else {
			pattern++;
			sftp_handle = libssh2_sftp_opendir(sftp_session, "/");
		}
		if ( !sftp_handle ) {
			print("\t\033[31munable to open dir\033[32m%s\n", path);
			return 0;
		}
    }

    while ( libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem),
                        	longentry, sizeof(longentry), &attrs)>0 ) {
		if ( pattern==NULL || fnmatch(pattern, mem, 0)==0 ) 
			print("\t%s\n", ll ? longentry : mem);
    }
    libssh2_sftp_closedir(sftp_handle);
	return 0;
}
int sftpHost::sftp_rm(char *path)
{
	if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
		if ( libssh2_sftp_unlink(sftp_session, path) ) 
			print("\t\033[31mcouldn't delete file\033[32m%s\n", path);
		return 0;
	}
    char mem[512], rfile[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftp_session, path);
	if ( !sftp_handle ) {
		print("\t\033[31munable to open dir\033[32m%s\n", path);
		return 0;
	}

    while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftp_session, rfile) ) 
				print("\t\033[31mcouldn't delete file\033[32m%s\n", rfile);
		}
    }
    libssh2_sftp_closedir(sftp_handle);
	return 0;
}
int sftpHost::sftp_md(char *path)
{
 	int rc = libssh2_sftp_mkdir(sftp_session, path,
                            LIBSSH2_SFTP_S_IRWXU|
                            LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
                            LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);
    if ( rc ) {
        print("\t\033[31mcouldn't create directory\033[32m%s\n", path);	
	}
	return 0;
}
int sftpHost::sftp_rd(char *path)
{
 	int rc = libssh2_sftp_rmdir(sftp_session, path);
    if ( rc ) {
        print("\t\033[31mcouldn't remove directory\033[32m%s\n", path);	
	}
	return 0;
}
int sftpHost::sftp_ren(char *src, char *dst)
{
	int rc = libssh2_sftp_rename(sftp_session, src, dst);
	if ( rc ) 
		print("\t\033[31mcouldn't rename file\033[32m%s\n", src);
	return 0;	
}
int sftpHost::sftp_get_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftp_session, 
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
        print("\t\033[31mUnable to read file\033[32m%s\n", src);
		return 0;
    }
    FILE *fp = fopen(dst, "wb");
	if ( fp==NULL ) {
		print("\t\033[31munable to create local file\033[32m%s\n", dst);
    	libssh2_sftp_close(sftp_handle);
		return 0;
	}
	print("\t\033[32m%s ", dst);
    char mem[1024*64];
	unsigned int rc, block=0;
	long total=0;
	time_t start = time(NULL);
    while ( (rc=libssh2_sftp_read(sftp_handle, mem, sizeof(mem)))>0 ) {
        if ( fwrite(mem, 1, rc, fp)<rc ) break;
		total += rc;
		block +=rc;
		if ( block>1024*1024 ) { block=0; print("."); }
    } 
    int duration = (int)(time(NULL)-start);
	print("%ld bytes %d seconds\n", total, duration);
	fclose(fp);
    libssh2_sftp_close(sftp_handle);
	return 0;	
}
int sftpHost::sftp_put_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_open(sftp_session, dst,
                      LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                      LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
                      LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
    if (!sftp_handle) {
		print("\t\033[31mcouldn't open remote file\033[32m%s\n", dst);
		return 0;
    }
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\t\033[31mcouldn't open local file\033[32m%s\n", src);
		return 0;
	}
	print("\t\033[32m%s ", dst);
    char mem[1024*64];
	int nread, block=0;
	long total=0;
    time_t start = time(NULL);
	while ( (nread=fread(mem, 1, sizeof(mem), fp))>0 ) {
        int nwrite=0;
        while ( nread>nwrite ){
			int rc=libssh2_sftp_write(sftp_handle, mem+nwrite, nread-nwrite);
			if ( rc<0 ) break;
            nwrite += rc;
			total += rc;
        }
        block += nwrite;
		if ( block>1024*1024 ) { block=0; print("."); }
	}
    int duration = (int)(time(NULL)-start);
	fclose(fp);
	print("%ld bytes %d seconds\n", total, duration);
    libssh2_sftp_close(sftp_handle);	
	return 0;
}
int sftpHost::sftp(char *p){
	char *p1, *p2, src[1024], dst[1024];
	p1 = strchr(p, ' ');		//p1 is first parameter of the command
	if ( p1==NULL ) 
		p1 = p+strlen(p);
	else 
		while ( *p1==' ' ) *p1++=0;

	p2 = strchr(p1, ' '); 		//p2 is second parameter of the command
	if ( p2==NULL ) 
		p2 = p1+strlen(p1);
	else
		while ( *p2==' ' ) *p2++=0;

	strcpy(src, p1);			//src is remote source file
	if ( *p1!='/') {
		strcpy(src, realpath);	
		if ( *p1!=0 ) { 
			if ( *src!='/' || strlen(src)>1 ) strcat(src, "/"); 
			strcat(src, p1); 
		}
	}

	strcpy(dst, p2);			//dst is remote destination file	
	if ( *p2!='/' ) {
		strcpy( dst, realpath );	
		if ( *p2!=0 ) {
			if ( *dst!='/' || strlen(dst)>1 ) strcat( dst, "/" );
			strcat( dst, p2 );
		}
	}
	if ( strncmp(p, "lpwd",4)==0 ) sftp_lcd(NULL);
	else if ( strncmp(p, "lcd",3)==0 ) sftp_lcd(p1);
	else if ( strncmp(p, "pwd",3)==0 ) sftp_cd(NULL);
	else if ( strncmp(p, "cd", 2)==0 ) sftp_cd(*p1==0?homepath:src);
	else if ( strncmp(p, "ls", 2)==0 ) sftp_ls(src);
	else if ( strncmp(p, "dir",3)==0 ) sftp_ls(src, true);		
	else if ( strncmp(p, "mkdir",5)==0 ) sftp_md(src);
	else if ( strncmp(p, "rmdir",5)==0 ) sftp_rd(src);
	else if ( strncmp(p, "rm", 2)==0
			||strncmp(p, "del",3)==0)  sftp_rm(src);		
	else if ( strncmp(p, "ren",3)==0)  sftp_ren(src, dst);
	else if ( strncmp(p, "get",3)==0 ) sftp_get(src, p2);
	else if ( strncmp(p, "put",3)==0 ) sftp_put(p1, dst);
	else if ( strncmp(p, "bye",3)==0 ) return -1;
	else print("\t\033[31m%s is not supported command, \033[37mtry %s\n\t%s\n",
				p, "lcd, lpwd, cd, pwd,", 
				"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
int sftpHost::read()
{
	do_callback("Connecting", 0);
	int rc = tcp();
	if ( rc!=0 ) goto TCP_Close;
	
	session = libssh2_session_init();
	if ( !session ) { rc=-2;  goto sftp_Close; }
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		rc=-3;  goto sftp_Close; 
	}
	if ( ssh_knownhost()!=0 ) { 
		rc=-4; goto sftp_Close; 
	}
	if ( ssh_authentication()!=0 ) { 
		rc=-5; goto sftp_Close; 
	}
	if ( !(sftp_session=libssh2_sftp_init(session)) ) { 
		rc = -6; goto sftp_Close; 
	}
	if ( libssh2_sftp_realpath(sftp_session, ".", realpath, 1024)<0 )
		*realpath=0;
	strcpy( homepath, realpath );
	bConnected = true;

	const char *p;
	while ( (p=ssh_gets("sftp> ", true))!=NULL ) {
		if ( *p==0 ) continue;
		if ( sftp((char *)p)==-1 ) break;
	} 
	
	libssh2_sftp_shutdown(sftp_session);
	bConnected = false;
	readerThread = 0;
	*username = 0;
	*password = 0;

sftp_Close:
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
TCP_Close:
	closesocket(sock);
	do_callback(errmsgs[-rc], -1);
	return rc;
}
int sftpHost::write(const char *buf, int len)
{
	if ( readerThread!=0 ) 
		write_keys(buf, len);
	else
		if ( *buf=='\r' ) connect();
	return 0;
}
int sftpHost::sftp_get(char *src, char *dst)
{
    char mem[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	if ( strchr(src,'*')==NULL && strchr(src, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, *dst?dst:".");
		struct stat statbuf;
		if ( stat(lfile, &statbuf)!=-1 ) {
			if ( S_ISDIR(statbuf.st_mode) ) {
				strcat(lfile, "/");
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(lfile, p);
			}
		}	
		sftp_get_one(src, lfile);
	}
	else {
		char *pattern = strrchr(src, '/');
		*pattern++ = 0;
		sftp_handle = libssh2_sftp_opendir(sftp_session, src);
		if ( !sftp_handle ) {
			print("\t\033[31mcould't open remote diretory\033[32m%s\n", src);
			return 0;
		}
		
		char rfile[1024], lfile[1024];
		strcpy(rfile, src); strcat(rfile, "/");
		int rlen = strlen(rfile);
		strcpy(lfile, dst); if ( *lfile ) strcat(lfile, "/");
		int llen = strlen(lfile);
		while ( libssh2_sftp_readdir(sftp_handle, mem, 
								sizeof(mem), &attrs)>0 ) {
			if ( fnmatch(pattern, mem, 0)==0 ) {
				strcpy(rfile+rlen, mem);
				strcpy(lfile+llen, mem);
				sftp_get_one(rfile, lfile);
			}
		}
	}
	return 0;
}
int sftpHost::sftp_put(char *src, char *dst)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	if ( stat(src, &statbuf)!=-1 ) {
		char rfile[1024];
		strcpy(rfile, *dst?dst:".");
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		if ( libssh2_sftp_stat(sftp_session, rfile, &attrs)==0 ) {
			if ( LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ) {
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}		
		sftp_put_one(src, rfile);
	}
	else {
		char *pattern=src;
		char lfile[1024]=".", rfile[1024];
		char *p = strrchr(src, '/');
		if ( p!=NULL ) {
			*p++ = 0; 
			pattern = p;
			strcpy(lfile, src);
		}

		if ( (dir=opendir(lfile) ) == NULL ){
			print("\t\033[31mcouldn't open local directory\033[32m%s\n",lfile);
			return 0;
		}
		strcat(lfile, "/");
		int llen = strlen(lfile);
		strcpy(rfile, dst); 
		if ( *rfile!='/' || strlen(rfile)>1 ) strcat(rfile, "/");
		int rlen = strlen(rfile);
		while ( (dp=readdir(dir)) != NULL ) {
			if ( fnmatch(pattern, dp->d_name, 0)==0 ) {
				strcpy(lfile+llen, dp->d_name);
				strcpy(rfile+rlen, dp->d_name);
				sftp_put_one(lfile, rfile);
			}
		}
	}
	return 0;
}