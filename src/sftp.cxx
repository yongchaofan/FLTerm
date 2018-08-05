//
// "$Id: sftp.cxx 12255 2018-06-29 21:55:10 $"
//
// sftpHost
//
//	  sftp host implementation for terminal simulator
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
#include "sftp.h"
extern const char *errmsgs[];

/*******************sftpHost*******************************/
int sftpHost::sftp_lcd(char *cmd)
{
	if ( cmd==NULL || *cmd==0 ) {
		char buf[4096];
		if ( getcwd(buf, 4096)!=NULL ) {
			print("\t\033[32m%s \033[37mis current local directory\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't get current local directory\033[37m\n");
		}
	}
	else {
		while ( *cmd==' ' ) cmd++;
		if ( chdir(cmd)==0 ) {
			print("\t\033[32m%s\033[37m is now local directory!\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't change local directory to\033[32m%s\033[37m\n", cmd);
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
			print("\t\033[31mUnable to change working directory to\033[32m%s\033[37m\n", path);
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
			print("\t\033[31mUnable to open dir\033[32m%s\n\033[37m", path);
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
			print("\t\033[31munable to open dir\033[32m%s\033[37m\n", path);
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
			print("\t\033[31mcouldn't delete file\033[32m%s\033[37m\n", path);
		return 0;
	}
    char mem[512], rfile[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftp_session, path);
	if ( !sftp_handle ) {
		print("\t\033[31munable to open dir\033[32m%s\033[37m\n", path);
		return 0;
	}

    while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftp_session, rfile) ) 
				print("\t\033[31mcouldn't delete file\033[32m%s\033[37m\n", rfile);
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
        print("\t\033[31mcouldn't create directory\033[32m%s\033[37m\n", path);	
	}
	return 0;
}
int sftpHost::sftp_rd(char *path)
{
 	int rc = libssh2_sftp_rmdir(sftp_session, path);
    if ( rc ) {
        print("\t\033[31mcouldn't remove directory\033[32m%s\033[37m\n", path);	
	}
	return 0;
}
int sftpHost::sftp_ren(char *src, char *dst)
{
	int rc = libssh2_sftp_rename(sftp_session, src, dst);
	if ( rc ) 
		print("\t\033[31mcouldn't rename file\033[32m%s\033[37m\n", src);
	return 0;	
}
int sftpHost::sftp_get_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftp_session, 
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
        print("\t\033[31mUnable to read file\033[32m%s\033[37m\n", src);
		return 0;
    }
    FILE *fp = fopen(dst, "wb");
	if ( fp==NULL ) {
		print("\t\033[31munable to create local file\033[32m%s\033[37m\n", dst);
    	libssh2_sftp_close(sftp_handle);
		return 0;
	}
	print("\t\033[32m%s\033[37m ", dst);
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
		print("\t\033[31mcouldn't open remote file\033[32m%s\033[37m\n", dst);
		return 0;
    }
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\t\033[31mcouldn't open local file\033[32m%s\033[37m\n", src);
		return 0;
	}
	print("\t\033[32m%s\033[37m ", dst);
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
			print("\t\033[31mcould't open remote diretory\033[32m%s\033[37m\n", src);
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
			print("\t\033[31mcouldn't open local directory\033[32m%s\033[37m\n", lfile);
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