//
// "$Id: ftp.cxx 44210 2018-06-29 21:55:10 $"
//
// scpHost sftpHost ftpDaemon tftpDaemon
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
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

#include "Fl_Term.h"
#include "ftp.h"

const char *sshmsgs[] = { 
"TCP connected",
"Address resolution failed",
"TCP connection failed",
"SSH session failed",
"Host verification failed",
"SSH authentication failed",
"SSH channel failed",
"SSH subsystem error\n",
"Unable to request shell",
"SFTP session failed"};

/***********************************scpHost***************************/
scpHost::scpHost(const char *address) : sshHost(address)
{
} 
int scpHost::scp_read_one(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	do {
        mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		mtx.unlock();
        if (!scp_channel) {
            if ( libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                print("\n\033[31mSCP: couldn't open remote file\033[32m%s\033[30m", rpath);
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
int scpHost::scp_write_one(const char *lpath, const char *rpath)
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
            print("\n\033[31mSCP: couldn't open remote file \033[32m%s\033[30m",rpath );
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
int scpHost::scp_read(const char *rpath, const char *lpath)
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
int scpHost::scp_write(const char *lpath, const char *rpath)
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
int scpHost::scp(const char *cmd)
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
int scpHost::tun_local(const char *lpath, const char *rpath)
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
int scpHost::tun_remote(const char *rpath, const char *lpath)
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
int scpHost::tunnel(const char *cmd)
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
		std::thread tun_thread(&scpHost::tun_remote, this, path+1, p);
		tun_thread.detach();
	}
	else {				//start local tunnel
		std::thread tun_thread(&scpHost::tun_local, this, path, p);
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
			print("\t\033[32m%s \033[30mis current local directory\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't get current local directory\033[30m\n");
		}
	}
	else {
		while ( *cmd==' ' ) cmd++;
		if ( chdir(cmd)==0 ) {
			print("\t\033[32m%s\033[30m is now local directory!\n", cmd);
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
	print("\t\033[32m%s \033[30mis current working directory\n", realpath);
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
    if ( rc ) {
        print("\t\033[31mcouldn't create directory\033[32m%s\033[30m\n", path);	
	}
	return 0;
}
int sftpHost::sftp_rd(char *path)
{
 	int rc = libssh2_sftp_rmdir(sftp_session, path);
    if ( rc ) {
        print("\t\033[31mcouldn't remove directory\033[32m%s\033[30m\n", path);	
	}
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
	print("\t\033[32m%s\033[30m ", dst);
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
		print("\t\033[31mcouldn't open remote file\033[32m%s\033[30m\n", dst);
		return 0;
    }
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\t\033[31mcouldn't open local file\033[32m%s\033[30m\n", src);
		return 0;
	}
	print("\t\033[32m%s\033[30m ", dst);
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
	else print("\t\033[31m%s is not supported command, \033[30mtry %s\n\t%s\n",
				p, "lcd, lpwd, cd, pwd,", 
				"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
int sftpHost::read(char *buf, int len)
{
	char *p;
	term->command("#Timeout 300", NULL);
	do {
		p=term->gets("sftp > ", 0);
		if ( p==NULL ) break;
		if ( *p==0 ) continue;
	} 
	while ( sftp(p)!=-1 ) ;
	
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

/**********************************TFTPd*******************************/
int sock_select( int s, int secs )
{
	struct timeval tv = { 0, 0 };
	tv.tv_sec = secs;
	fd_set svrset;
	FD_ZERO( &svrset );
	FD_SET( s, &svrset );
	return select( s+1, &svrset, NULL, NULL, &tv );
}
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
	print( "%s", ret==0 ? "TFTPD timed out" : "TFTPd stopped\n" );
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
	print( "%s", ret0==0? "FTPd timed out\n" : "FTPd stopped\n" );
	return -1;
}