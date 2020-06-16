//
// "$Id: ssh2.cxx 38469 2020-06-08 11:55:10 $"
//
// sshHost sftpHost
//
// ssh2 host implementation for terminal simulator tinyTerm2
//
// Copyright 2018-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm2/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm2/issues/new
//

#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "ssh2.h"
void sleep_ms(int ms)
{
#ifdef WIN32
	Sleep(ms);
#else
	usleep(ms*1000);
#endif
}

#ifndef WIN32
	#include <pwd.h>
	#include <dirent.h>
	#include <fnmatch.h>
#else
	#include <shlwapi.h>
int wchar_to_utf8(WCHAR *wbuf, int wcnt, char *buf, int cnt)
{
	return WideCharToMultiByte(CP_UTF8, 0, wbuf, wcnt, buf, cnt, NULL, NULL);
}
int utf8_to_wchar(const char *buf, int cnt, WCHAR *wbuf, int wcnt)
{
	return MultiByteToWideChar(CP_UTF8, 0, buf, cnt, wbuf, wcnt);
}
int fnmatch( char *pattern, char *file, int flag)
{
	return PathMatchSpecA(file, pattern) ? 0 : 1;
}
/****************************************************************************
  local dirent implementation for compiling on vs2017
****************************************************************************/
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3

struct dirent {
	unsigned char d_type;
	char d_name[MAX_PATH * 3];
};

typedef struct tagDIR {
	struct dirent dd_dir;
	HANDLE dd_handle;
	int dd_stat;
} DIR;

static inline void finddata2dirent(struct dirent *ent, WIN32_FIND_DATAW *fdata)
{
	wchar_to_utf8(fdata->cFileName, -1, ent->d_name, sizeof(ent->d_name));

	if (fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		ent->d_type = DT_DIR;
	else
		ent->d_type = DT_REG;
}

DIR *opendir(const char *name)
{
	WCHAR pattern[MAX_PATH + 2];
	int len = utf8_to_wchar(name, -1, pattern, MAX_PATH+2);
	if ( len < 0) return NULL;
	if ( len && pattern[len - 1]!=L'/' ) wcscat(pattern, L"\\");
	wcscat(pattern, L"*.*");

	WIN32_FIND_DATAW fdata;
	HANDLE h = FindFirstFileW(pattern, &fdata);
	if ( h == INVALID_HANDLE_VALUE ) return NULL;

	DIR *dir = (DIR *)malloc(sizeof(DIR));
	dir->dd_handle = h;
	dir->dd_stat = 0;
	finddata2dirent(&dir->dd_dir, &fdata);
	return dir;
}

struct dirent *readdir(DIR *dir)
{
	if (!dir) return NULL;

	if (dir->dd_stat) {
		WIN32_FIND_DATAW fdata;
		if (FindNextFileW(dir->dd_handle, &fdata)) {
			finddata2dirent(&dir->dd_dir, &fdata);
		}
		else
			return NULL;
	}

	++dir->dd_stat;
	return &dir->dd_dir;
}

int closedir(DIR *dir)
{
	if (dir) {
		FindClose(dir->dd_handle);
		free(dir);
	}
	return 0;
}
#endif //WIN32

static const char *errmsgs[] = {
"Disconnected", "Connection", "Session failure",
"Verification failure", "Authentication failure",
"Channel failure", "pty failure", "Shell failure", "Subsystem failure"
};

const char *kb_gets(const char *prompt, int echo);
sshHost::sshHost(const char *name) : tcpHost(name)
{
	port = 0;
	*username = 0;
	*password = 0;
	*passphrase = 0;
	*subsystem = 0;
	session = NULL;
	channel = NULL;
	tunnel_list = NULL;

	char options[256];
	strncpy(options, name, 255);
	char *p = options;
	char *phost=NULL, *pport=NULL, *psubsys=NULL;
	char *puser=NULL, *ppass=NULL, *pphrase=NULL;
	while ( (p!=NULL) && (*p!=0) ) {
		while ( *p==' ' ) p++;
		if ( *p=='-' ) {
			switch ( p[1] ) {
			case 'l': p+=3; puser = p; break;
			case 'p': if ( p[2]=='w' ) { p+=4; ppass = p; }
						if ( p[2]=='p' ) { p+=4; pphrase = p; }
						break;
			case 'P': p+=3; pport = p; break;
			case 's': p+=3; psubsys = p; break;
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
	if ( psubsys!=NULL ) strncpy(subsystem, psubsys, 63);
	if ( pport!=NULL ) port = atoi(pport);
	if ( puser!=NULL ) strncpy(username, puser, 63);
	if ( ppass!=NULL ) strncpy(password, ppass, 63);
	if ( pphrase!=NULL ) strncpy(passphrase, pphrase, 63);
	if ( port==0 ) port = (*subsystem==0) ? 22 : 830;

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
const char *keytypes[] = {
	"unknown", "rsa", "dss", "ecdsa256", "ecdsa384", "ecdsa521", "ed25519"
};
int sshHost::ssh_knownhost()
{
	int type, check, buff_len;
	size_t len;
	char keybuf[256];
	char knownhostfile[MAX_PATH+64];
	strcpy(knownhostfile, homedir);
	strcat(knownhostfile, ".ssh");
	struct stat sb;
	if ( stat(knownhostfile, &sb)!=0) {
#ifdef WIN32
		mkdir(knownhostfile);
#else
		mkdir(knownhostfile, 0700);
#endif
	}
	strcat(knownhostfile, "/known_hosts");

	const char *key = libssh2_session_hostkey(session, &len, &type);
	if ( key==NULL ) return -4;
	buff_len=sprintf(keybuf, "%s key fingerprint", keytypes[type]);
	if ( type>0 ) type++;

	const char *fingerprint = libssh2_hostkey_hash(session,
										LIBSSH2_HOSTKEY_HASH_SHA1);
	if ( fingerprint==NULL ) return -4;
	for( int i=0; i<20; i++, buff_len+=3)
		sprintf(keybuf+buff_len, ":%02x", (unsigned char)fingerprint[i] );

	LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(session);
	if ( nh==NULL ) return -4;
	struct libssh2_knownhost *host;
	libssh2_knownhost_readfile(nh, knownhostfile,
							   LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
	int rc = 0;
	const char *p = NULL;
	switch ( check ) {
	case LIBSSH2_KNOWNHOST_CHECK_MATCH: break;
	case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
		if ( type==((host->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			print("%s\r\n\033[31m!!!Danger, host key changed!!!\r\n", keybuf);
			p = ssh_gets("Update hostkey and continue?(Yes/No) ", true);
			if ( p!=NULL ) {
				if ( *p=='y' || *p=='Y' )
					libssh2_knownhost_del(nh, host);
				else {
					rc = -4;
					print("\033[32mDisconnected, stay safe\r\n");
					break;
				}
			}
			else {
				rc = -4;
				print("\033[32mDisconnected, stay safe\r\n");
				break;
			}
		}
		//fall through if hostkey type mismatch or hostkey deleted for update
	case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
		if ( p==NULL ) {
			print("%s\r\n\033[33munknown host!\n", keybuf);
			p = ssh_gets("Add hostkey to .ssh/known_hosts?(Yes/No)", true);
		}
		if ( p!=NULL ) {
			if ( *p=='y' || *p=='Y' ) {
				libssh2_knownhost_addc(nh, hostname, "",
								key, len, "***tinyTerm2***", 15,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW|
								(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), &host);
				if ( libssh2_knownhost_writefile(nh, knownhostfile,
								LIBSSH2_KNOWNHOST_FILE_OPENSSH)==0 )
					print("\033[32mhostkey added to known_hosts\r\n");
				else
					print("\033[33mcouldn't write to known_hosts\r\n");
			}
			else
				print("\033[33mhostkey ignored\r\n");
		}
		else {
			rc = -4;
			print("\033[32mDisconnected, stay safe\r\n");
			break;
		}
	}

	libssh2_knownhost_free(nh);
	return rc;
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
			do_callback("\r\n", 2);
		}
		else if ( buf[i]=='\177' || buf[i]=='\b' ) {
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
	for ( int i=0; i<3000&&bGets; i++ ) {
		if ( bReturn ) return keys;
		if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
		sleep_ms(100);
	}
	return NULL;
}

int sshHost::ssh_authentication()
{
	int rc = -5;
	if ( *username==0 ) {
		const char *p = ssh_gets("\r\nusername: ", true);
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
			print("\033[32m\npublic key authentication passed\n");
			return 0;			// public key authentication passed
		}
	}
	if ( strstr(authlist, "password")!=NULL ) {
		//password was not set, get it interactively
		for ( int i=0; i<3; i++ ) {
			const char *p = ssh_gets("password: ", false);
			if ( p!=NULL ) {
				strncpy(password, p, 31);
				if (!libssh2_userauth_password(session,username,password))
					return 0;//password authentication passed
			}
			else
				break;
		}
	}
	else if ( strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ ) {
			if (!libssh2_userauth_keyboard_interactive(session, username,
														&kbd_callback) ) {
				print("\033[32m\ninteractive authentication passed\n");
				return 0;
			}
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
}
const char *IETF_HELLO="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
<capabilities><capability>urn:ietf:params:netconf:base:1.0</capability>\
</capabilities></hello>]]>]]>";
int sshHost::read()
{
	status( HOST_CONNECTING );
	if ( tcp()==-1 ) goto TCP_Close;

	channel = NULL;
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) {
		do_callback(errmsgs[2], -2);
		goto Session_Close;
	}
	const char *banner;
	banner=libssh2_session_banner_get(session);
	if ( banner!=NULL ) {
		do_callback(banner, strlen(banner));
		print("\r\n");
	} 

	status( HOST_AUTHENTICATING );
	if ( ssh_knownhost()!=0 ) {
		do_callback(errmsgs[3], -3);
		goto Session_Close;
	}
	if ( ssh_authentication()!=0 ) {
		do_callback(errmsgs[4], -4);
		goto Session_Close;
	}
	if ( !(channel=libssh2_channel_open_session(session)) ) {
		do_callback(errmsgs[5], -5);
		goto Session_Close;
	}
	if ( *subsystem==0 ) {
		if (libssh2_channel_request_pty(channel, "xterm")) {
			do_callback(errmsgs[6], -6);
			goto Channel_Close;
		}
		if ( libssh2_channel_shell(channel)) {
			do_callback(errmsgs[7], -7);
			goto Channel_Close;
		}
	}
	else {
		if (libssh2_channel_subsystem(channel, subsystem)) {
			do_callback(errmsgs[8], -8);
			goto Channel_Close;
		}
		libssh2_channel_write(channel, IETF_HELLO, strlen(IETF_HELLO));
	}
	libssh2_session_set_blocking(session, 0);

	status( HOST_CONNECTED );
	do_callback("Connected", 0);
	while ( true ) {
		int len;
		char buf[32768];
		mtx.lock();
		len=libssh2_channel_read(channel, buf, 32767);
		mtx.unlock();
		if ( len>0 ) {
			buf[len] = 0;
			do_callback(buf, len);
		}
		else {
			if ( len==LIBSSH2_ERROR_EAGAIN )
				if ( wait_socket()>0 ) continue;
			if ( len==0 )
				if ( !libssh2_channel_eof(channel) ) continue;
			break;
		}
	}
	status( HOST_IDLE );
	do_callback("Disonnected", -1);
	tun_closeall();
	*username = 0;
	*password = 0;

Channel_Close:
	if ( channel!=NULL ) {
		mtx.lock();
		libssh2_channel_close(channel);
		mtx.unlock();
		channel = NULL;
	}
Session_Close:
	if ( session!=NULL ) {
		mtx.lock();
		libssh2_session_free(session);
		mtx.unlock();
		session = NULL;
	}
TCP_Close:
	closesocket(sock);
	reader.detach();
	return 0;
}
int sshHost::write(const char *buf, int len)
{
	if ( channel!=NULL ) {
		int total=0, cch=0;
		while ( total<len ) {
			mtx.lock();
			if ( channel!=NULL )
				cch=libssh2_channel_write(channel, buf+total, len-total);
			else
				cch = 0;
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
		if ( reader.joinable() )
			write_keys(buf, len);
		else
			if ( *buf=='\r' ) connect();
	}
	return 0;
}
void sshHost::send_size(int sx, int sy)
{
	mtx.lock();
	if ( channel!=NULL )
		libssh2_channel_request_pty_size( channel, sx, sy );
	mtx.unlock();
}
void sshHost::keepalive(int interval)
{
	mtx.lock();
	if ( session!=NULL ) //some host will close connection when interval!=0
		libssh2_keepalive_config(session, false, interval);
	mtx.unlock();
}
void sshHost::disconn()
{
	if ( reader.joinable() ) {
		bGets = false;
		if ( channel!=NULL ) {
			mtx.lock();
			libssh2_channel_eof(channel);
			mtx.unlock();
		}
		if ( session!=NULL ) {
			mtx.lock();
			libssh2_session_disconnect(session, "close");
			mtx.unlock();
		}
//		reader.join();//cause crash when compiled with visual studio or xcode
	}
}
int sshHost::scp_read_one(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	print("\n\033[32mSCP: %s\t ", lpath);
	int err_no=0;
	do {
		mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
		if (!scp_channel) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN)
				if ( wait_socket()>0 ) continue;
			print("\033[31mcouldn't open remote file %s",rpath);
			return -1;
		}
	} while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp!=NULL ) {
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
		print(" %lld bytes in %d seconds", got, (int)(time(NULL)-start));
	}
	else
		print("\033[32mcouldn't write local file %s", lpath);

	mtx.lock();
	libssh2_channel_close(scp_channel);
	libssh2_channel_free(scp_channel);
	mtx.unlock();
	return 0;
}
int sshHost::scp_write_one(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct stat fileinfo;
	print("\n\033[32mSCP: %s\t", rpath);
	FILE *fp =fopen(lpath, "rb");
	if ( fp==NULL ) {
		print("\n\033[31mcouldn't read local file %s", lpath);
		return -1;
	}
	stat(lpath, &fileinfo);

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
			print("\n\033[31mcouldn't open remote file %s",rpath);
			fclose(fp);
			return -2;
		}
	} while ( !scp_channel );

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
	}
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
	libssh2_channel_close(scp_channel);
	libssh2_channel_free(scp_channel);
	mtx.unlock();
	return 0;
}
int sshHost::scp_read(char *lpath, char *rpath)
//rpath is a list of files from "ls -1 "
{
	for ( char *p=lpath; *p; p++ ) if ( *p=='\\' ) *p='/';

	int localIsDir = false;
	struct stat statbuf;
	if ( stat(lpath, &statbuf)!=-1 ) {
		if ( (statbuf.st_mode & S_IFMT) == S_IFDIR )
			localIsDir = true;
	}

	char *p1, *p = rpath;
	while ( (p1=strchr(p, 0x0a))!=NULL ) {
		*p1++=0;
		if ( localIsDir ) {
			char lfile[4096];
			char *p2 = strrchr(p, '/');
			if ( p2==NULL ) p2=p; else p2++;
			strcpy(lfile, lpath);
			strcat(lfile, "/");
			strcat(lfile, p2);
			scp_read_one(p, lfile);
		}
		else
			scp_read_one(p, lpath);
		p = p1;
	}
	return 0;
}
int sshHost::scp_write(char *lpath, char *rpath)
//lpath could have wildcard characters('*','?') in it
//rpath ends with '/' if it's a directory
{
	struct stat statbuf;
	for ( char *p=lpath; *p; p++ ) if ( *p=='\\' ) *p='/';

	if ( stat(lpath, &statbuf)!=-1 ) {		//lpath is a single file
		char rfile[4096];
		strncpy(rfile, rpath, 1024);
		rfile[1024] = 0;
		if ( rpath[strlen(rpath)-1]=='/' ) {//rpath is a directory
			char *p = strrchr(lpath, '/');
			if ( p!=NULL ) p++; else p=lpath;
			strcat(rfile, p);
		}
		scp_write_one(lpath, rfile);
	}
	else {									//lpath has wildcard chars
		const char *ldir=".";
		char *lpattern = strrchr(lpath, '/');
		if ( lpattern!=NULL ) {
			*lpattern++ = 0;
			if ( *lpath ) ldir = lpath;
		}
		else
			lpattern = lpath;

		DIR *dir;
		struct dirent *dp;
		if ( (dir=opendir(ldir) ) != NULL ) {
			while ( (dp=readdir(dir)) != NULL ) {
				if ( fnmatch(lpattern, dp->d_name, 0)==0 ) {
					char lfile[1024], rfile[1024];
					strcpy(lfile, ldir);
					strcat(lfile, "/");
					strcat(lfile, dp->d_name);
					strcpy(rfile, rpath);
					if ( rpath[strlen(rpath)-1]=='/' )
						strcat(rfile, dp->d_name);
					scp_write_one( lfile, rfile );
				}
			}
			closedir(dir);
		}
		else
			print("\n\033[31mSCP: couldn't open %s\n", ldir);
	}
	return 0;
}
TUNNEL *sshHost::tun_add( int tun_sock, LIBSSH2_CHANNEL *tun_channel,
							char *localip, unsigned short localport,
							char *remoteip, unsigned short remoteport)
{
	TUNNEL *tun = (TUNNEL *)malloc(sizeof(TUNNEL));
	if ( tun!=NULL ) {
		tun->socket = tun_sock;
		tun->channel = tun_channel;
		tun->localip = strdup(localip);
		tun->localport = localport;
		tun->remoteip = strdup(remoteip);
		tun->remoteport = remoteport;
		mtx.lock();
		tun->next = tunnel_list;
		tunnel_list = tun;
		mtx.unlock();
		print("\n\033[32mtunnel %d %s:%d %s:%d\n", tun_sock,
							localip, localport, remoteip, remoteport);
	}
	return tun;
}
void sshHost::tun_del(int tun_sock)
{
	mtx.lock();
	TUNNEL *tun_pre = NULL;
	TUNNEL *tun = tunnel_list;
	while ( tun!=NULL ) {
		if ( tun->socket==tun_sock ) {
			free(tun->localip);
			free(tun->remoteip);
			if ( tun_pre!=NULL )
				tun_pre->next = tun->next;
			else
				tunnel_list = tun->next;
			free(tun);
			print("\n\033[32mtunnel %d closed\n", tun_sock);
			break;
		}
		tun_pre = tun;
		tun = tun->next;
	}
	mtx.unlock();
}
void sshHost::tun_closeall()
{
	mtx.lock();
	TUNNEL *tun = tunnel_list;
	while ( tun!=NULL ) {
		closesocket(tun->socket);
		tun = tun->next;
	}
	mtx.unlock();
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
	libssh2_channel_close(tun_channel);
	libssh2_channel_free(tun_channel);
	mtx.unlock();
	closesocket(tun_sock);
	tun_del(tun_sock);
}
int sshHost::tun_local(const char *lpath, const char *rpath)
{
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
//	  char sockopt = 1;
//	  setsockopt(listensock,SOL_SOCKET,SO_REUSEADDR,&sockopt,sizeof(sockopt));
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
	tun_add(listensock, NULL, shost, sport, dhost, dport);

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
		tun_add(tun_sock, tun_channel, client_host, client_port, dhost, dport);
		std::thread tun_thread(&sshHost::tun_worker,this,tun_sock,tun_channel);
		tun_thread.detach();
	}
shutdown:
	closesocket(listensock);
	tun_del(listensock);
	return 0;
}
int sshHost::tun_remote(const char *lpath, const char *rpath)
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
			tun_add(tun_sock, tun_channel, shost, r_listenport, dhost, dport);
			std::thread tun_thread(&sshHost::tun_worker, this, tun_sock,
																tun_channel);
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
	libssh2_channel_forward_cancel(listener);
	mtx.unlock();
	return 0;
}
void sshHost::tun(char *cmd)
{
	if ( *cmd==' ' ) {
		char *p = strchr(++cmd, ' ');
		if ( p==NULL )
			closesocket(atoi(cmd));
		else {
			*p++ = 0;
			char *lpath=cmd, *rpath=p;
			if ( *cmd==':' ) {
				lpath = p;
				rpath = cmd+1;
			}
			std::thread tun_thread( *cmd==':' ? &sshHost::tun_remote :
									&sshHost::tun_local, this, lpath, rpath );
			tun_thread.detach();
			sleep_ms(100);
		}
	}
	else {
		TUNNEL *tun = tunnel_list;
		int listen_cnt = 0, active_cnt = 0;
		print("\nTunnels:\n");
		while ( tun!=NULL ) {
			print(tun->channel==NULL?"listen":"active");
			print(" socket %d\t%s:%d\t%s:%d\n", tun->socket,
						tun->localip, tun->localport,
						tun->remoteip, tun->remoteport);
			if ( tun->channel!=NULL )
				active_cnt++;
			else
				listen_cnt++;
			tun = tun->next;
		}
		print("\t%d listenning, %d active\n", listen_cnt, active_cnt);
	}
	write("\015", 1);
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
			print("\t\033[31mCouldn't change local dir to \033[32m%s\n", cmd);
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
			print("\t\033[31mCouldn't change dir to \033[32m%s\n", path);
			return 0;
		}
		libssh2_sftp_closedir(sftp_handle);
		int rc = libssh2_sftp_realpath(sftp_session, path, newpath, 1024);
		if ( rc>0 ) strcpy( realpath, newpath );
	}
	print("\033[32m%s\033[37m\n", realpath);
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
			if ( (statbuf.st_mode & S_IFMT) == S_IFDIR ) {
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
			print("\t\033[31mcould't open remote \033[32m%s\n", src);
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
			print("\t\033[31mcouldn't open \033[32m%s\n",lfile);
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
int sftpHost::sftp(char *cmd)
{
	for ( char *p=cmd; *p; p++ ) if ( *p=='\\' ) *p='/';

	char *p1, *p2, src[1024], dst[1024];
	p1 = strchr(cmd, ' ');		//p1 is first parameter of the command
	if ( p1==NULL )
		p1 = cmd+strlen(cmd);
	else
		while ( *p1==' ' ) p1++;

	p2 = strchr(p1, ' ');		//p2 is second parameter of the command
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
	if ( strncmp(cmd, "lpwd",4)==0 ) sftp_lcd(NULL);
	else if ( strncmp(cmd, "lcd",3)==0 ) sftp_lcd(p1);
	else if ( strncmp(cmd, "pwd",3)==0 ) sftp_cd(NULL);
	else if ( strncmp(cmd, "cd", 2)==0 ) sftp_cd(*p1==0?homepath:src);
	else if ( strncmp(cmd, "ls", 2)==0 ) sftp_ls(src);
	else if ( strncmp(cmd, "dir",3)==0 ) sftp_ls(src, true);
	else if ( strncmp(cmd, "mkdir",5)==0 ) sftp_md(src);
	else if ( strncmp(cmd, "rmdir",5)==0 ) sftp_rd(src);
	else if ( strncmp(cmd, "rm", 2)==0
			||strncmp(cmd, "del",3)==0)	 sftp_rm(src);
	else if ( strncmp(cmd, "ren",3)==0)	 sftp_ren(src, dst);
	else if ( strncmp(cmd, "get",3)==0 ) sftp_get(src, p2);
	else if ( strncmp(cmd, "put",3)==0 ) sftp_put(p1, dst);
	else if ( strncmp(cmd, "bye",3)==0 ) return -1;
	else if ( *cmd )
			print("\t\033[31m%s is not supported command,  %s\n\t%s\n",
					cmd, "\033[37mtry lcd, lpwd, cd, pwd,",
					"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
int sftpHost::read()
{
	status( HOST_CONNECTING );
	if ( tcp()==-1 ) goto TCP_Close;

	channel = NULL;
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) {
		do_callback(errmsgs[2], -2);
		goto Sftp_Close;
	}
	const char *banner;
	banner=libssh2_session_banner_get(session);
	if ( banner!=NULL ) do_callback(banner, strlen(banner));

	status( HOST_AUTHENTICATING );
	if ( ssh_knownhost()!=0 ) {
		do_callback(errmsgs[3], -3);
		goto Sftp_Close;
	}
	if ( ssh_authentication()!=0 ) {
		do_callback(errmsgs[4], -4);
		goto Sftp_Close;
	}
	if ( !(sftp_session=libssh2_sftp_init(session)) ) {
		do_callback(errmsgs[6], -6);
		goto Sftp_Close;
	}
	if ( libssh2_sftp_realpath(sftp_session, ".", realpath, 1024)<0 )
		*realpath=0;
	strcpy( homepath, realpath );

	status( HOST_CONNECTED );
	do_callback("Connected", 0);
	const char *p;
	do {
		print("sftp %s", realpath);
		if ( (p=ssh_gets("> ", true))==NULL ) break;
	} while ( sftp((char *)p)!=-1 );
	libssh2_sftp_shutdown(sftp_session);
	status( HOST_IDLE );
	do_callback("Disonnected", -1);

	*username = 0;
	*password = 0;

Sftp_Close:
	if ( session!=NULL ) {
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
		session = NULL;
	}
TCP_Close:
	closesocket(sock);
	reader.detach();
	return 0;
}
int sftpHost::write(const char *buf, int len)
{
	if ( reader.joinable() )
		write_keys(buf, len);
	else
		if ( *buf=='\r' ) connect();
	return 0;
}