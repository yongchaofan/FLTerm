//
// "$Id: ssh2.cxx 41064 2018-05-25 21:55:10 $"
//
// sshHost sftpHost confHost
//
//	  host implementation for terminal simulator
//    used with the Fl_Term widget in flTerm
//
// Copyright 2017-2018 by Yongchao Fan.
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

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

#include "Fl_Host.h"
#include "Fl_Term.h"
#include "ssh2.h"

const char *sshmsgs[] = { 
"TCP connected\n",
"Address resolution failed\n",
"TCP connection failed\n",
"SSH session failed\n",
"Host verification failed\n",
"SSH authentication failed\n",
"SSH channel failed\n",
"SSH subsystem error\n",
"Unable to request shell\n",
"SFTP session failed\n"};

/***********************************sshHost***************************/
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
				pport = p+1;
				*p=0;
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
const char *term_get(const char *prompt, int echo);
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
		const char *p = term_get(prom, prompts[i].echo==0);
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
	char *p = NULL;
	if ( *username==0 ) {
		if ( term!=NULL ) p = term->gets("username:", false);
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
			p = term->gets("password:", true);
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
	print("%s\n", sshmsgs[-rc]);
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
int sshHost::scp_read_one(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	do {
        mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		mtx.unlock();
        if (!scp_channel) {
            if ( libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                print("\n\033[31mSCP: couldn't open remote file");
                return -1;
            }
            else 
                if ( wait_socket()==-1 ) return -1;
        }
    } while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp==NULL ) {
		print("\n\033[31mSCP: couldn't write local file \033[32m%s\033[30m", lpath);
		return -2;
	}
	print("\n\033[32mSCP: %s\t\033[30m ", lpath);

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
int sshHost::scp_write_one(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct stat fileinfo;

    FILE *fp =fopen(lpath, "rb");
	if ( !fp ) {
		print("\n\033[31mSCP: couldn't read local file\033[32m%s\033[30m", lpath);
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
            print("\n\033[31mSCP: couldn't open remote file" );
            return -2;
        }
    } while ( !scp_channel );
 
    print("\n\033[32mSCP: %s\t\033[30m", rpath);
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
                print("\033[31minterrupted at %ld bytes\033[30m", total);
                break;
            }
            else {
                nread -= rc;
                ptr += rc;
            }
        }
    }/* only continue if nread was drained */ 
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
int sshHost::scp_read(const char *rpath, const char *lpath)
{
	if ( strchr(rpath,'*')==NULL && strchr(rpath, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, lpath);
		struct stat statbuf;
		if ( stat(lpath, &statbuf)!=-1 ) {
			if ( S_ISDIR(statbuf.st_mode) ) {
				strcat(lfile, "/");
				const char *p = strrchr(rpath, '/');
				if ( p!=NULL ) p++; else p=rpath;
				strcat(lfile, p);
			}
		}	
		scp_read_one(rpath, lfile);
	}
	else {
		char rnames[4096]="ls -1 ", *rlist;
		if ( *rpath!='/' ) strcat(rnames, "~/");
		strcat(rnames, rpath);
		if ( term->command(rnames, &rlist )>0 ) {
			char rdir[1024], rfile[1024], lfile[1024];
			char *p1, *p2, *p = strrchr(rnames, '/');
			if ( p!=NULL ) *p=0;
			strncpy(rdir, rnames+6, 1023);
			strncpy(rnames, rlist, 4095);
			p = strchr(rnames, '\012');
			if ( p==NULL ) return 0;
			p++;
			while ( (p1=strchr(p, '\012'))!=NULL ) {
				*p1++ = 0; 
				strcpy(rfile, p);
				p2 = strrchr(p, '/');
				if ( p2==NULL ) p2=p; else p2++;
				strcpy(lfile, lpath);
				strcat(lfile, "/");
				strcat(lfile, p2);
				scp_read_one(rfile, lfile);
				p = p1;
			}
		}
	}
	return 0;
}
int sshHost::scp_write(const char *lpath, const char *rpath)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	if ( stat(lpath, &statbuf)!=-1 ) {
		char rnames[1024]="ls -ld ", *rlist, rfile[1024];
		if ( *rpath!='/' ) strcat(rnames, "~/");
		strcat(rnames, rpath);
		strcpy(rfile, *rpath?rpath:".");
		if ( term->command(rnames, &rlist )>0 ) {
			const char *p = strchr(rlist, '\012');
			if ( p!=NULL ) if ( p[1]=='d' ) {
				p = strrchr(lpath, '/');
				if ( p!=NULL ) p++; else p=lpath;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}
		scp_write_one(lpath, rfile);
	}
	else {
		const char *lname=lpath;
		char ldir[1024]=".";
		char *p = (char *)strrchr(lpath, '/');
		if ( p!=NULL ) {
			*p++ = 0; 
			lname = p;
			strcpy(ldir, lpath);
		}

		if ( (dir=opendir(ldir) ) == NULL ){
			print("\n\033[31mSCP: couldn't open local directory\033[32m%s\033[30m\n", ldir);
			return 0;
		}
		while ( (dp=readdir(dir)) != NULL ) {
			if ( fnmatch(lname, dp->d_name, 0)==0 ) {
				char lfile[1024], rfile[1024];
				strcpy(lfile, ldir);
				strcat(lfile, "/");
				strcat(lfile, dp->d_name);
				strcpy(rfile, rpath);
				strcat(rfile, "/");
				strcat(rfile, dp->d_name);
				scp_write_one(lfile, rfile);
			}
		}
	}
	return 0;
}
int sshHost::scp(const char *cmd)
{
	static char path[256];

	if ( !bConnected ) return 0;
	strncpy(path, cmd, 255);
	char *p = strchr(path, ' ');
	if ( p==NULL ) return -1;

	*p=0; 
	char *rpath = path;
	char *lpath = p+1;
	if ( *rpath==':' ) {
		scp_read(rpath+1, lpath);
	}
	else if ( *lpath==':') {
		scp_write(rpath, lpath+1);
	}
	write("\015",1);
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
        printf("ip address error\n");
        return -1;
    }
	
	int forwardsock;
	int listensock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listensock == -1) {
        fprintf(stderr, "failed to open listen socket!\n");
        return -1;
    } 
    
	int tun_sock = tcp();
	if ( tun_sock<=0 ) return -1;
	LIBSSH2_CHANNEL *tun_channel;
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

    setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    if ( bind(listensock, (struct sockaddr *)&sin, sinlen)==-1 ) {
        printf("address:port %s:%d invalid or in use\n", shost, sport);
        goto shutdown0;
    }
    if ( listen(listensock, 2)==-1 ) {
        fprintf(stderr, "listen error\n");
        goto shutdown0;
    }
	print("\nTunnel listening on %s:%d\n", shost, sport);

accept_again:
	forwardsock = accept(listensock, (struct sockaddr *)&sin, &sinlen);
    if ( forwardsock==-1) {
        fprintf(stderr, "accept error\n");
        goto shutdown0;
    }
	client_host = inet_ntoa(sin.sin_addr);
	client_port = ntohs(sin.sin_port);

	do {
		tun_channel = libssh2_channel_direct_tcpip_ex(tun_session, 
									dhost, dport, client_host, client_port);
		if (!tun_channel) {
			if ( libssh2_session_last_errno(tun_session)!=LIBSSH2_ERROR_EAGAIN ) 
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
		FD_SET(tun_sock, &fds);
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
                i = libssh2_channel_write(tun_channel, buf+wr, len-wr);
                if ( i==LIBSSH2_ERROR_EAGAIN ) continue;
				if ( i<0 ) {
					fprintf(stderr, "libssh2_channel_write error: %d\n", i);
					goto shutdown2;
				}
				wr += i;
            }
        }
        if ( FD_ISSET(tun_sock, &fds) ) while ( true ) {
			char buff[16384];
            len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
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
	libssh2_session_disconnect(tun_session, "Normal Shutdown");
	libssh2_session_free(tun_session);
	closesocket(tun_sock);
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
int sshHost::tunnel(const char *cmd)
{
	char path[256];

	if ( !bConnected ) return 0;
	if ( *cmd==0 ) { 	//list tunnels
		return 0;
	}
	
	while ( *cmd==' ' ) cmd++;
	strncpy(path, cmd, 255);
	char *p = strchr(path, ' ');
	if ( p==NULL ) { 	//close tunnel
		return 0;
	}
	*p=0; p++; 

	tunStarted = false;
	if ( *path=='R' ) { //start remote tunnel
		std::thread tun_thread(&sshHost::tun_remote, this, path+1, p);
		tun_thread.detach();
	}
	else {				//start local tunnel
		std::thread tun_thread(&sshHost::tun_local, this, path, p);
		tun_thread.detach();
	}
	while ( !tunStarted ) usleep(100000);
	return 0;
}

/*******************sftpHost*******************************/
int sftpHost::sftp_lcd(char *cmd)
{
	if ( cmd==NULL || *cmd==0 ) {
		char buf[4096];
		if ( getcwd(buf, 4096)!=NULL ) {
			print("\t\033[34m%s \033[30mis current local directory\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't get current local directory\033[30m\n");
		}
	}
	else {
		while ( *cmd==' ' ) cmd++;
		if ( chdir(cmd)==0 ) {
			print("\t\033[34m%s\033[30m is now local directory!\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't change local directory to\033[32m%s\033[30m\n", cmd);
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
			print("\t\033[31mUnable to change working directory to\033[32m%s\033[30m\n", path);
			return 0;
		}
	    libssh2_sftp_closedir(sftp_handle);		
		int rc = libssh2_sftp_realpath(sftp_session, path, newpath, 1024);
		if ( rc>0 ) strcpy( realpath, newpath );
	}
	print("\t\033[34m%s \033[30mis current working directory\n", realpath);
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
			print("\t\033[31mUnable to open dir\033[32m%s\n\033[30m", path);
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
			print("\t\033[31munable to open dir\033[32m%s\033[30m\n", path);
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
			print("\t\033[31mcouldn't delete file\033[32m%s\033[30m\n", path);
		return 0;
	}
    char mem[512], rfile[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftp_session, path);
	if ( !sftp_handle ) {
		print("\t\033[31munable to open dir\033[32m%s\033[30m\n", path);
		return 0;
	}

    while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftp_session, rfile) ) 
				print("\t\033[31mcouldn't delete file\033[32m%s\033[30m\n", rfile);
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
    if ( rc )
        print("\t\033[31mcouldn't create directory\033[32m%s\033[30m\n", path);	
	return 0;
}
int sftpHost::sftp_rd(char *path)
{
 	int rc = libssh2_sftp_rmdir(sftp_session, path);
    if ( rc )
        print("\t\033[31mcouldn't remove directory\033[32m%s\033[30m\n", path);	
	return 0;
}
int sftpHost::sftp_ren(char *src, char *dst)
{
	int rc = libssh2_sftp_rename(sftp_session, src, dst);
	if ( rc ) 
		print("\t\033[31mcouldn't rename file\033[32m%s\033[30m\n", src);
	return 0;	
}
int sftpHost::sftp_get_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftp_session, 
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
        print("\t\033[31mUnable to read file\033[32m%s\033[30m\n", src);
		return 0;
    }
    FILE *fp = fopen(dst, "wb");
	if ( fp==NULL ) {
		print("\t\033[31munable to create local file\033[32m%s\033[30m\n", dst);
    	libssh2_sftp_close(sftp_handle);
		return 0;
	}
	print("\t\033[34m%s\033[30m ", dst);
    char mem[1024*64];
	int rc, block=0;
	long total=0;
	time_t start = time(NULL);
    while ( (rc=libssh2_sftp_read(sftp_handle, mem, sizeof(mem)))>0 ) {
		total += rc;
		block +=rc;
		if ( block>1024*1024 ) { block=0; print("."); }
        fwrite(mem, 1, rc, fp);
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
		print("\t\033[31mcouldn't open remote file\033[32m%s\033[30m\n", dst);
		return 0;
    }
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\t\033[31mcouldn't open local file\033[32m%s\033[30m\n", src);
		return 0;
	}
	print("\t\033[34m%s\033[30m ", dst);
    char mem[1024*64];
	int nread;
	long total=0;
    time_t start = time(NULL);
	while ( (nread=fread(mem, 1, sizeof(mem), fp))>0 ) {
        char *ptr = mem;
		int rc;
        while ( (rc=libssh2_sftp_write(sftp_handle, ptr, nread))>0 ){
            ptr += rc;
			total += rc;
            nread -= rc;
			if ( nread<=0 ) break;
        }
		if ( (total%(1024*1024))==0 ) print(".");
	}
    int duration = (int)(time(NULL)-start);
	fclose(fp);
	print("%ld bytes %d seconds\n", total, duration);
    libssh2_sftp_close(sftp_handle);	
	return 0;
}
int sftpHost::connect()
{
	int rc = sock = tcp();
	if ( rc>0 ) {
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
			rc = -9; goto sftp_Close; 
		}
		if ( libssh2_sftp_realpath(sftp_session, ".", realpath, 1024)<0 )
			*realpath=0;
		strcpy( homepath, realpath );
		bConnected = true;
		return 0;

sftp_Close:
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
	}
	print("%s\n",sshmsgs[-rc]);
	return rc;
}
int sftpHost::read(char *buf, int len)
{
	char *p, *p1, *p2, src[1024], dst[1024];
	term->command("#Timeout 300", NULL);
	while ( (p=term->gets("sftp > ", 0))!=NULL ) {
		if ( *p==0 ) continue;
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
		else if ( strncmp(p, "bye",3)==0 ) break;
		else print("\t\033[31m%s is not supported command, \033[30mtry %s\n\t%s\n",
					p, "lcd, lpwd, cd, pwd,", 
					"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	}
	libssh2_sftp_shutdown(sftp_session);
	libssh2_session_disconnect(session, "Normal Shutdown");
	libssh2_session_free(session);
	return -1;
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
			print("\t\033[31mcould't open remote diretory\033[32m%s\033[30m\n", src);
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
			print("\t\033[31mcouldn't open local directory\033[32m%s\033[30m\n", lfile);
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
const char *ietf_header="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"%d\">";
const char *ietf_footer="</rpc>]]>]]>";
int confHost::connect()
{
	int rc = tcp();
	if ( rc>0 ) {
		bConnected = false;
		sock = rc;
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
		channel2 = libssh2_channel_open_session(session);
		if ( !channel || !channel2 ) { 
			rc=-6; goto Channel_Close; 
		}
		if ( libssh2_channel_subsystem(channel2, "netconf") ||
			libssh2_channel_subsystem(channel, "netconf")) { 
			rc=-7; goto Channel_Close; 
		}
		//must be nonblocking for 2 channels on the same session
		libssh2_session_set_blocking(session, 0); 
		bConnected = true;
		msg_id = 0;
		rd = rd2 = 0;
		libssh2_channel_write( channel, ietf_hello, strlen(ietf_hello) );
		libssh2_channel_write( channel2, ietf_hello, strlen(ietf_hello) );
		libssh2_channel_write( channel2, ietf_subscribe, strlen(ietf_subscribe) );
		return 0;		//success

	Channel_Close:
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
	}
	print("%s\n", sshmsgs[-rc]);
	return rc;
}
int confHost::read(char *buf, int len)
{
	char *delim;
	int len1, len2, rc; 
again:
	do {
		mtx.lock();
		len1=libssh2_channel_read(channel,reply+rd,BUFLEN-rd);
		mtx.unlock();

		if ( len1>0 ) {
			rd += len1; 
			reply[rd] = 0;
			delim = strstr(reply, "]]>]]>");
			if ( delim != NULL ) {
				*delim=0; 
				rc = delim - reply; if ( rc>len ) rc = len;
				strncpy(buf, reply, rc);
				strcpy(reply, delim+6); 
				rd = strlen(reply);
				return rc;
			}
			if ( rd>BUFLEN-6 ){ 
				fprintf(stderr, "buffer too small!\n"); 
				rd=0; 
			}
		}
	} while ( len1>0 );

	do {
		mtx.lock();
		len2=libssh2_channel_read(channel2,notif+rd2,BUFLEN-rd2);
		mtx.unlock();

		if ( len2>0 ) {
			rd2 += len2; 
			notif[rd2] = 0;
			delim = strstr(notif, "]]>]]>");
			if ( delim != NULL ) {
				*delim=0;
				rc = delim - notif; if ( rc>len ) rc = len; 
				strncpy(buf, notif, rc);
				strcpy(notif, delim+6); 
				rd2 = strlen(notif);
				return rc;
			}
			if ( rd2>BUFLEN-6 ){ 
				fprintf(stderr, "buffer too small!\n"); 
				rd2=0; 
			}
		}
	} while ( len2>0 );

	if ( len1==LIBSSH2_ERROR_EAGAIN && len2==LIBSSH2_ERROR_EAGAIN ) {
		if ( wait_socket()>0 ) goto again;
	}

	if ( len1<=0 || len2<=0 ) {
		libssh2_channel_close(channel);
		libssh2_channel_free(channel);
		libssh2_channel_close(channel2);
		libssh2_channel_free(channel2);
		libssh2_session_disconnect(session, "Reader Shutdown");
		libssh2_session_free(session);
		return -1;
	}
	return 0;
}
void confHost::write(const char *msg, int len)
{
	char header[256];
	sprintf(header, ietf_header, msg_id++);
	sshHost::write( header, strlen(header) );
	sshHost::write( msg, strlen(msg) );
	sshHost::write( ietf_footer, strlen(ietf_footer) );
	if ( term!=NULL ) 
		term->print("\n%s\n%s\n%s\n", header, msg, ietf_footer);
	else 
		fprintf(stderr, "\n************* sent to %s***********\n%s\n%s\n%s\n",
						 hostname, header, msg, ietf_footer);
}
