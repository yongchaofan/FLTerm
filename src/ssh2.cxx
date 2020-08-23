//
// "$Id: ssh2.cxx 39364 2020-08-21 11:55:10 $"
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
#include <thread>

#ifndef WIN32
	#include <pwd.h>
	#include <dirent.h>
	#include <fnmatch.h>
	#define Sleep(x) usleep((x)*1000);
#else
	#define getcwd _getcwd
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

const char *kb_gets(const char *prompt, int echo);	//define in tiny2.cxx
sshHost::sshHost(const char *name) : tcpHost(name)
{
	port = 0;
	*username = 0;
	*password = 0;
	*passphrase = 0;
	*subsystem = 0;
	session = NULL;
	channel = NULL;

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
	if ( phost!=NULL ) {
		strncpy(hostname, phost, 127);
		hostname[127]=0;
	}
	if ( psubsys!=NULL ) {
		strncpy(subsystem, psubsys, 63);
		subsystem[63]=0;
	}
	if ( pport!=NULL ) port = atoi(pport);
	if ( puser!=NULL ) {
		strncpy(username, puser, 63);
		puser[63]=0;
	}
	if ( ppass!=NULL ) {
		strncpy(password, ppass, 63);
		ppass[63]=0;
	}
	if ( pphrase!=NULL ) {
		strncpy(passphrase, pphrase, 63);
		pphrase[63]=0;
	}
	if ( port==0 ) port = (*subsystem==0)?22:830;
}
const char *keytypes[] = {
	"unknown", "rsa", "dss", "ecdsa256", "ecdsa384", "ecdsa521", "ed25519"
};
const char *knownhostfile=".ssh/known_hosts";
int sshHost::ssh_knownhost()
{
	int type, check, buff_len;
	size_t len;
	char keybuf[256];

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

	struct stat sb;
	if ( stat(knownhostfile, &sb)==-1 ) {
#ifdef WIN32
		mkdir(".ssh");
#else
		mkdir(".ssh", 0700);
#endif
	}
	struct libssh2_knownhost *host;
	libssh2_knownhost_readfile(nh, knownhostfile,
							   LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
	int rc = -4;
	const char *p=NULL, *msg="Disconnected!";
	switch ( check ) {
	case LIBSSH2_KNOWNHOST_CHECK_MATCH: rc=0; msg=""; break;
	case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
		if ( type==((host->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			print("%s\r\n\033[31m!!!host key changed!!!\r\n", keybuf);
			p = term_gets("Update hostkey and continue?(Yes/No) ", true);
			if ( p==NULL ) {
				term_puts("\r\n", 2);
				break;
			}
			if ( *p!='y' && *p!='Y' ) break;
			libssh2_knownhost_del(nh, host);
			msg = "\033[32mhostkey updated!\r\n";
		}
		//fall through for key type mismatch or hostkey update
	case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
		if ( p==NULL ) {
			print("%s\r\n\033[33mhostkey unknown!\r\n", keybuf);
			p = term_gets("Add to .ssh/known_hosts?(Yes/No)", true);
		}
		if ( p==NULL ) {
			term_puts("\r\n", 2);
			break;
		}
		if ( *p!='y' && *p!='Y' ) break;
		rc = 0;
		libssh2_knownhost_addc(nh, hostname, "",
							key, len, "***tinyTerm2***", 15,
							LIBSSH2_KNOWNHOST_TYPE_PLAIN|
							LIBSSH2_KNOWNHOST_KEYENC_RAW|
							(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), &host);
		if ( libssh2_knownhost_writefile(nh, knownhostfile,
							LIBSSH2_KNOWNHOST_FILE_OPENSSH)==0 ) {
			if ( *msg=='D' ) msg = "\033[32mhostkey added!\r\n";
		}
		else {
			msg = "\033[33mfailed to update known_hosts\r\n";
		}
	}
	print(msg);
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
		const char *p = kb_gets(prompts[i].text, prompts[i].echo);
		if ( p!=NULL ) {
			responses[i].text = strdup(p);
			responses[i].length = strlen(p);
		}
	}
}
const char *pubkeys[] = {".ssh/id_ed25519.pub", ".ssh/id_ecdsa.pub",
						".ssh/id_rsa.pub" };
const char *privkeys[] = {".ssh/id_ed25519", ".ssh/id_ecdsa", ".ssh/id_rsa"};
int sshHost::ssh_authentication()
{
	int rc = -5;
	if ( *username==0 ) {
		const char *p = term_gets("\r\nusername: ", true);
		if ( p==NULL ) return rc;
		if ( *p==0 ) return rc;
		strncpy(username, p, 31);
	}
	char *authlist=libssh2_userauth_list(session, username, strlen(username));
	if ( authlist==NULL ) return 0;	// null authentication passed
	if ( *password && strstr(authlist, "password")!=NULL ) {
		if ( !libssh2_userauth_password(session, username, password) )
			rc = 0;		//password authentication pass
		else	// password provided, it either works or not
			return rc;	//won't try anyting else on failure
	}
	if ( rc!=0 && strstr(authlist, "publickey")!=NULL ) {
		struct stat buf;
		for ( int i=0; i<3; i++ ) {
			if ( stat(privkeys[i], &buf)==0 ) {
				if ( !libssh2_userauth_publickey_fromfile(session,
						username, pubkeys[i], privkeys[i], passphrase) ) {
					print("\033[32mpublic key(%s) authenticated\r\n", 
													privkeys[i]+8);
					rc=0;
					break;
				}
			}
		}
	}
	if ( rc!=0 && strstr(authlist, "password")!=NULL ) {
		//password was not set, get it interactively
		for ( int i=0; i<3; i++ ) {
			const char *p = term_gets("password: ", false);
			if ( p!=NULL ) {
				strncpy(password, p, 31);
				if (!libssh2_userauth_password(session,username,password)) {
					rc=0;//password authentication passed
					break;
				}
			}
			else
				term_puts("\r\n", 2);
		}
	}
	else if ( rc!=0 && strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ ) {
			if (!libssh2_userauth_keyboard_interactive(session, username,
														&kbd_callback) ) {
				print("\033[32mkeyboard interactive authenticated\r\n");
				rc=0;
				break;
			}
		}
	}
	memset(username, 0, sizeof(username));
	memset(password, 0, sizeof(password));
	memset(passphrase,0,sizeof(passphrase));
	return rc;
}
int sshHost::wait_socket()
{
	timeval tv = {0, 10000};	//tv=NULL works on Windows but not MacOS
	fd_set fds, *rfd=NULL, *wfd=NULL;
	FD_ZERO(&fds); FD_SET(sock, &fds);
	int dir = libssh2_session_block_directions(session);
	if ( dir==0 ) return 1;
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) rfd = &fds;
	if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) wfd = &fds;
	return select(sock+1, rfd, wfd, NULL, &tv);
}
const char *IETF_HELLO="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
<capabilities><capability>urn:ietf:params:netconf:base:1.0</capability>\
</capabilities></hello>]]>]]>";
int sshHost::read()
{
	status(HOST_CONNECTING);
	if ( tcp()==-1 ) goto TCP_Close;

	channel = NULL;
	session = libssh2_session_init();
	int rc;
	while ((rc=libssh2_session_handshake(session,sock))==LIBSSH2_ERROR_EAGAIN)
		if ( wait_socket()<0 ) break;
	if ( rc!=0 ) {
		term_puts(errmsgs[2], -2);
		goto Session_Close;
	}
	const char *banner;
	banner=libssh2_session_banner_get(session);
	if ( banner!=NULL ) print("%s\r\n", banner);

	status(HOST_AUTHENTICATING);
	if ( ssh_knownhost()!=0 ) {
		term_puts(errmsgs[3], -3);
		goto Session_Close;
	}
	if ( ssh_authentication()!=0 ) {
		term_puts(errmsgs[4], -4);
		goto Session_Close;
	}
	if ( !(channel=libssh2_channel_open_session(session)) ) {
		term_puts(errmsgs[5], -5);
		goto Session_Close;
	}
	if ( *subsystem==0 ) {
		if (libssh2_channel_request_pty(channel, "xterm")) {
			term_puts(errmsgs[6], -6);
			goto Channel_Close;
		}
		if ( libssh2_channel_shell(channel)) {
			term_puts(errmsgs[7], -7);
			goto Channel_Close;
		}
	}
	else {
		if (libssh2_channel_subsystem(channel, subsystem)) {
			term_puts(errmsgs[8], -8);
			goto Channel_Close;
		}
		libssh2_channel_write(channel, IETF_HELLO, strlen(IETF_HELLO));
	}
	libssh2_session_set_blocking(session, 0);

	status(HOST_CONNECTED);
	term_puts("Connected", 0);
	while ( true ) {
		char buf[32768];
		mtx.lock();
		int len=libssh2_channel_read(channel, buf, 32768);
		mtx.unlock();
		if ( len>0 ) {
			term_puts(buf, len);
		}
		else {//len<=0
			if ( len!=LIBSSH2_ERROR_EAGAIN || wait_socket()<0 ) break;
		}
	}
	term_puts("Disconnected", -1);
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
	status(HOST_IDLE);
	reader.detach();
	return 0;
}
int sshHost::write(const char *buf, int len)
{
	int total=0, cch=0;
	while ( total<len && channel!=NULL ) {
		mtx.lock();
		cch=libssh2_channel_write(channel, buf+total, len-total);
		mtx.unlock();
		if ( cch>0 ) {
			total += cch;
		}
		else  {
			if ( cch!=LIBSSH2_ERROR_EAGAIN || wait_socket()<0 ) break;
		}
	}
	return cch<0 ? cch : total;
}
void sshHost::send_size(int sx, int sy)
{
	mtx.lock();
	if ( channel!=NULL )
		libssh2_channel_request_pty_size( channel, sx, sy );
	mtx.unlock();
}
void sshHost::keepalive(int interval)
{//some host will close connection when interval!=0
	if ( session!=NULL ) {
		mtx.lock();
		libssh2_keepalive_config(session, false, interval);
		mtx.unlock();
	}
}
void sshHost::disconn()
{
	if ( reader.joinable() ) {
		if ( channel!=NULL ) {
			mtx.lock();
			libssh2_channel_send_eof(channel);
			mtx.unlock();
		}
		if ( session!=NULL ) {
			mtx.lock();
			libssh2_session_disconnect(session, "close");
			mtx.unlock();
		}
	}
}
void sshHost::print_total(time_t start, long total)
{
	double duration = difftime(time(NULL), start);
	print("\033[12D%ld bytes", total);
	if ( duration>0 ) print(", %dMB/s", (int)((total>>20)/duration));
}
int sshHost::scp_read_one(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	print("\r\n\033[32mscp: %s\t\t\t", lpath);
	int err_no=0;
	do {
		mtx.lock();
		scp_channel = libssh2_scp_recv2(session, rpath, &fileinfo);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
		if (!scp_channel) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN)
				if ( wait_socket()>=0 ) continue;
			print("\033[31mcouldn't open remote file");
			return -1;
		}
	} while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp!=NULL ) {
		int blocks = 0;
		time_t start = time(NULL);
		libssh2_struct_stat_size total = 0;
		libssh2_struct_stat_size fsize = fileinfo.st_size;
		while  ( total<fsize ) {
			char mem[1024*32];
			int amount=1024*32;
			if ( (fsize-total) < amount) {
				amount = (int)(fsize-total);
			}
			mtx.lock();
			int rc = libssh2_channel_read(scp_channel, mem, amount);
			mtx.unlock();
			if ( rc>0 ) {
				int nwrite = fwrite(mem, 1,rc,fp);
				if ( nwrite>0 ) {
					total += nwrite;
					if ( ++blocks==32 ) {
						blocks = 0;
						print("\033[12D% 10lldKB", total>>10);
					}
				}
				if ( nwrite!=rc ) {
					print("\033[31merror writing to file");
					break;
				}
			}
			else {
				if ( rc!=LIBSSH2_ERROR_EAGAIN || wait_socket()<0 ) {
					print("\033[31merror reading from host");
					break;
				}
			}
		}
		fclose(fp);
		if ( total==fsize ) print_total(start, total);
	}
	else
		print("\033[32mcouldn't open local file %s", lpath);

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
	print("\r\n\033[32mscp: %s\t\t\t", rpath);
	FILE *fp =fopen(lpath, "rb");
	if ( fp==NULL ) {
		print("\033[31mcouldn't read local file %s", lpath);
		return -1;
	}
	stat(lpath, &fileinfo);

	int err_no = 0;
	do {
		mtx.lock();
		scp_channel = libssh2_scp_send(session, rpath, fileinfo.st_mode&0777,
						   (unsigned long)fileinfo.st_size);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(session);
		mtx.unlock();
		if ( !scp_channel ) {
			if ( err_no!=LIBSSH2_ERROR_EAGAIN || wait_socket()<0 ) {
				print("\033[31mcouldn't open remote file");
				fclose(fp);
				return -2;
			}
		}
	} while ( !scp_channel );
	int rc, blocks = 0;
	time_t start = time(NULL);
	size_t nread = 0;
	long total = 0;
	char mem[1024*32];
	while ( (nread=fread(mem, 1, 1024*32, fp)) >0 ) {
		char *ptr = mem;
		while ( nread>0 ) {
			mtx.lock();
			rc = libssh2_channel_write(scp_channel, ptr, nread);
			mtx.unlock();
			if ( rc>0 ) {
				ptr += rc;
				nread -= rc;
				total += rc;
			}
			else {
				if ( rc!=LIBSSH2_ERROR_EAGAIN || wait_socket()<0 ) break;
			}
		}
		if ( rc>0 ) {
			if ( ++blocks==32 ) {
				blocks = 0;
				print("\033[12D% 10ldKB", total>>10);
			}
		}
		else {
			print(", \033[31minterrupted");
			break;
		}
	}
	if ( nread==0 ) print_total(start, total);	//file completed
	fclose(fp);

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
{
	int localIsDir = false;
	struct stat statbuf;
	if ( stat(lpath, &statbuf)!=-1 ) {
		if ( (statbuf.st_mode & S_IFMT)==S_IFDIR )
			localIsDir = true;
	}

	char *p = rpath;
	char lfile[4096];
	strcpy(lfile, lpath);
	if ( localIsDir ) {
		char *p2 = strrchr(p, '/');
		if ( p2==NULL ) p2=p; else p2++;
		if ( lfile[strlen(lfile)-1]!='/' ) strcat(lfile, "/");
		strcat(lfile, p2);
	}
	scp_read_one(p, lfile);

	return 0;
}
int sshHost::scp_write(char *lpath, char *rpath)
//lpath could have wildcard characters('*','?') in it
//rpath ends with '/' if it's a directory
{
	struct stat statbuf;
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
			print("\r\n\033[31mSCP: couldn't open %s\r\n", ldir);
	}
	return 0;
}
void sshHost::send_file(char *src, char *dst)
{
	if ( *subsystem==0 ) {	//ssh
		for ( char *p=src; *p; p++ ) if ( *p=='\\' ) *p='/';
		scp_write(src, dst);
	}
	else {					//netconf
		FILE *fp = fopen(src, "rb");
		if ( fp!=NULL ) {
			char buf[1024];
			int n;
			while ( (n=fread(buf, 1, 1024, fp))>0 ) {
				write(buf, n);
			}
			fclose(fp);
		}
	}
}
void sshHost::command(const char *cmd)
{
	if ( strncmp(cmd, "scp", 3)==0 ) {
		if ( cmd[3]!=' ' ) return;
		char *path = strdup(cmd+4);
		for ( char *p=path; *p; p++ ) 
			if ( *p=='\\' && p[1]!=' ' ) *p='/';

		char *p = path;
		while ( *p==' ' ) p++;
		char *r = NULL;
		do {
			p = strchr(p, ' ');
			if ( p==NULL ) break;
			if ( p[-1]=='\\' ) {	//dealing with ' ' in file names
				for ( char *q=p-1; *q; q++ ) *q = q[1];
			}
			else {
				*p++ = 0;
				r = p;
			}
		} while ( p!=NULL );
		
		if ( r!=NULL ) {
			if ( *path==':' )
				scp_read(r, path+1);
			else
				scp_write(path, r+1);
			write("\r", 1);
		}
		free(path);
		return;		
	}
	if ( strncmp(cmd, "tun", 3)==0 ) tun(cmd+3);
}

TUNNEL *sshHost::tun_add(int tun_sock, LIBSSH2_CHANNEL *tun_channel,
							char *localip, unsigned short localport,
							char *remoteip, unsigned short remoteport)
{
	TUNNEL *tun = new TUNNEL;
	if ( tun!=NULL ) {
		tun->socket = tun_sock;
		tun->channel = tun_channel;
		tun->localip = strdup(localip);
		tun->localport = localport;
		tun->remoteip = strdup(remoteip);
		tun->remoteport = remoteport;
		tunnel_mtx.lock();
		tunnel_list.insert(tunnel_list.end(), tun);
		tunnel_mtx.unlock();
		print("\r\n\033[32mtunnel %d %s:%d %s:%d\r\n", tun_sock,
						localip, localport, remoteip, remoteport);
	}
	return tun;
}
void sshHost::tun_del(int tun_sock)
{
	tunnel_mtx.lock();
	for ( auto it=tunnel_list.begin(); it!=tunnel_list.end(); it++ ) {
		TUNNEL *tun = *it;
		if ( tun->socket==tun_sock ) {
			free(tun->localip);
			free(tun->remoteip);
			delete(tun);
			tunnel_list.erase(it);
			print("\r\n\033[32mtunnel %d closed\r\n", tun_sock);
			break;
		}
	}
	tunnel_mtx.unlock();
}
void sshHost::tun_closeall()
{
	tunnel_mtx.lock();
	for ( auto &tun : tunnel_list ) closesocket(tun->socket);
	tunnel_mtx.unlock();
}

void sshHost::tun_worker(int tun_sock, LIBSSH2_CHANNEL *tun_channel)
{
	char buff[16384];
	while ( true ) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		FD_SET(tun_sock, &fds);
		int rc = select(tun_sock + 1, &fds, NULL, NULL, NULL);
		if ( rc==-1 ) break;
		if ( rc==0 ) continue;
		if ( FD_ISSET(tun_sock, &fds) ) {
			int len = recv(tun_sock, buff, sizeof(buff), 0);
			if ( len<=0 ) break;
			for ( int wr=0, i=0; wr<len; wr+=i ) {
				mtx.lock();
				i = libssh2_channel_write(tun_channel, buff+wr, len-wr);
				mtx.unlock();
				if ( i==LIBSSH2_ERROR_EAGAIN ) { i=0; continue; }
				if ( i<=0 ) goto shutdown;
			}
		}
		if ( FD_ISSET(sock, &fds) ) while ( true )
		{
			mtx.lock();
			int len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
			mtx.unlock();
			if ( len==LIBSSH2_ERROR_EAGAIN ) break;
			if ( len<=0 ) goto shutdown;
			for ( int wr=0, i=0; wr<len; wr+=i ) {
				i = send(tun_sock, buff + wr, len - wr, 0);
				if ( i<=0 ) break;
			}
		}
	}
shutdown:
	mtx.lock();
	libssh2_channel_close(tun_channel);
	libssh2_channel_free(tun_channel);
	mtx.unlock();
	closesocket(tun_sock);
	tun_del(tun_sock);
}
int sshHost::tun_local(char *parameters)
{//parameters example: 127.0.0.1:2222 127.0.0.1:22
	char shost[256], dhost[256], *client_host, *p;
	unsigned short sport, dport, client_port;

	char *lpath = parameters;
	char *rpath = strchr(lpath, ' ');
	*rpath++ = 0;
	strncpy(shost, lpath, 255);
	strncpy(dhost, rpath, 255);
	free(parameters);
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);

	struct sockaddr_in sin;
	socklen_t sinlen=sizeof(sin);
	struct addrinfo *ainfo;
	if ( getaddrinfo(shost, NULL, NULL, &ainfo)!=0 ) {
		print("\033[31minvalid address: %s\r\n", shost);
		return -1;
	}
	int listensock = socket(ainfo->ai_family, SOCK_STREAM, 0);
//	  char sockopt = 1;
//	  setsockopt(listensock,SOL_SOCKET,SO_REUSEADDR,&sockopt,sizeof(sockopt));
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(sport);
	int rc = bind(listensock, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	if ( rc==-1 ) {
		print("\033[31mport %d invalid or in use\r\n", sport);
		closesocket(listensock);
		return -1;
	}
	if ( listen(listensock, 2)==-1 ) {
		print("\033[31mlisten error\r\n");
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
					if ( wait_socket()>=0 ) continue;
				print("\033[31mCouldn't establish tunnel, is it supported?\r\n");
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
int sshHost::tun_remote(char *parameters)
{//parameters example: :192.168.1.1:2222 127.0.0.1:22
	int r_listenport;
	LIBSSH2_LISTENER *listener = NULL;
	LIBSSH2_CHANNEL *tun_channel = NULL;
	char shost[256], dhost[256], *p;
	unsigned short sport, dport;

	char *rpath = parameters+1;
	char *lpath = strchr(rpath, ' ');
	*lpath++ = 0;
	strncpy(shost, rpath, 255);
	strncpy(dhost, lpath, 255);
	free(parameters);
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
				if ( wait_socket()>=0 ) continue;
			print("\033[31mCouldn't listen for tunnel, is it supported?\r\n");
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
				if ( wait_socket()>=0 ) continue;
			print("\033[31mCouldn't accept tunnel connection!\r\n");
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
			print("\r\n\033[31mremote tunneling connect error\r\n");
			closesocket(tun_sock);
		}
	}
	else {
		print("\r\n\033[31mremote tunneling address error\r\n");
	}
	mtx.lock();
	libssh2_channel_forward_cancel(listener);
	mtx.unlock();
	return 0;
}
void sshHost::tun(const char *cmd)
{
	if ( *cmd==' ' ) {
		while( *cmd==' ' ) cmd++;
		if ( strchr(cmd, ' ')!=NULL ) {	//open new tunnel
			std::thread tun_thread( *cmd==':' ? &sshHost::tun_remote :
									&sshHost::tun_local, this, strdup(cmd) );
			tun_thread.detach();
		}
		else {							//close existing tunnel
			int sock = atoi(cmd);
			tunnel_mtx.lock();
			for ( auto &tun : tunnel_list ) 
				if ( tun->socket==sock ) closesocket(sock);
			tunnel_mtx.unlock();
		}
	}
	else {								//list all tunnels
		int listen_cnt = 0, active_cnt = 0;
		print("\r\nTunnels:\r\n");
		tunnel_mtx.lock();
		for ( auto &tun : tunnel_list ) {
			print(tun->channel==NULL?"listen":"active");
			print(" socket %d\t%s:%d\t%s:%d\r\n", tun->socket,
						tun->localip, tun->localport,
						tun->remoteip, tun->remoteport);
			if ( tun->channel!=NULL )
				active_cnt++;
			else
				listen_cnt++;
		}
		tunnel_mtx.unlock();
		print("\t%d listenning, %d active\r\n", listen_cnt, active_cnt);
	}
	write("\r", 1);
}
/*******************sftpHost*******************************/
void sftpHost::sftp_lcd(char *cmd)
{
	char buf[4096];
	if ( cmd!=NULL && *cmd!=0 ) {
		if ( chdir(cmd)!=0 ) 
			print("\033[31mCouldn't change local dir to \033[32m%s\r\n", cmd);
	}
	if ( getcwd(buf, 4096)!=NULL ) {
		print("\033[32m%s \033[37mis current local directory\r\n", buf);
	}
}
void sftpHost::sftp_cd(char *path)
{
	char newpath[1024];
	if ( path!=NULL ) {
		LIBSSH2_SFTP_HANDLE *sftp_handle;
		if ((sftp_handle=libssh2_sftp_opendir(sftp_session, path))!=NULL ) {
			libssh2_sftp_closedir(sftp_handle);
			if ( libssh2_sftp_realpath(sftp_session, path, newpath, 1024)>0 )
				strcpy(realpath, newpath);
		}
		else {
			print("\033[31mCouldn't change dir to \033[32m%s\r\n", path);
		}
	}
	print("%s\r\n", realpath);
}
struct direntry{
	char shortentry[512];
	char longentry[512];
};
void sftpHost::sftp_ls(char *path, bool ll)
{
	char *pattern = NULL;
	char root[2] = "/";
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	if ( (sftp_handle=libssh2_sftp_opendir(sftp_session, path))==NULL ) {
		pattern = strrchr(path, '/');
		if ( pattern!=path )
			*pattern = 0;
		else
			path = root;
		pattern++;
		sftp_handle=libssh2_sftp_opendir(sftp_session, path);
	}

	if ( sftp_handle==NULL ) {
		print("\033[31munable to open dir\033[32m%s\r\n", path);
		return;
	}
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	char mem[512], longentry[512];
	std::list<direntry *> dirlist;
	auto it = dirlist.begin();
	while ( libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem),
							longentry, sizeof(longentry), &attrs)>0 ) {
		if ( pattern==NULL || fnmatch(pattern, mem, 0)==0 ) {
			direntry *newentry = new direntry;
			strcpy(newentry->shortentry, mem);
			strcpy(newentry->longentry, longentry);
			for ( it=dirlist.begin(); it!=dirlist.end(); it++ ) {
				if ( strcmp(mem, (*it)->shortentry)<0 ) {
					it = dirlist.insert(it, newentry);
					break;
				}
			}
			if ( it==dirlist.end() ) dirlist.insert(it, newentry);
		}
	}

	for ( it=dirlist.begin(); it!=dirlist.end(); it++ ) {
		print("%s\r\n", ll? (*it)->longentry : (*it)->shortentry);
		delete(*it);
	}
	libssh2_sftp_closedir(sftp_handle);
}
void sftpHost::sftp_rm(char *path)
{
	if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
		if ( libssh2_sftp_unlink(sftp_session, path) )
			print("\033[31mcouldn't delete file\033[32m%s\r\n", path);
		return;
	}
	char mem[512], rfile[1024];
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftp_session, path);
	if ( !sftp_handle ) {
		print("\033[31munable to open dir \033[32m%s\r\n", path);
		return;
	}

	while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftp_session, rfile) )
				print("\033[31mcouldn't delete \033[32m%s\r\n", rfile);
		}
	}
	libssh2_sftp_closedir(sftp_handle);
}
void sftpHost::sftp_md(char *path)
{
	int rc = libssh2_sftp_mkdir(sftp_session, path,
							LIBSSH2_SFTP_S_IRWXU|
							LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
							LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);
	if ( rc )
		print("\033[31mcouldn't create directory\033[32m%s\r\n", path);
}
void sftpHost::sftp_rd(char *path)
{
	if ( libssh2_sftp_rmdir(sftp_session, path) )
		print("\033[31mcouldn't remove dir \033[32m%s, is it empty?\r\n",path);
}
void sftpHost::sftp_ren(char *src, char *dst)
{
	if ( libssh2_sftp_rename(sftp_session, src, dst) )
		print("\033[31mcouldn't rename file \033[32m%s\r\n", src);
}
void sftpHost::sftp_get_one(char *src, char *dst)
{
	print("get %s\t\t\t", dst);
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftp_session,
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
		print("\033[31mcouldn't open remote file\r\n");
		return;
	}
	FILE *fp = fopen(dst, "wb");
	if ( fp==NULL ) {
		print("\033[31mcouldn't open local file\r\n");
		libssh2_sftp_close(sftp_handle);
		return;
	}

	int rc, blocks=0;
	long total=0;
	char mem[1024*32];
	time_t start = time(NULL);
	while ( (rc=libssh2_sftp_read(sftp_handle, mem, 1024*32))>0 ) {
		int nwrite = fwrite(mem, 1, rc, fp);
		if ( nwrite>0 ) {
			total += nwrite;
			if ( ++blocks==32 ) {
				blocks = 0;
				print("\033[12D% 10ldKB", total>>10);
			}
		}
		if ( nwrite!=rc ) {
			print("\033[31merror writing to file\r\n");
			break;
		}
	}
	fclose(fp);
	libssh2_sftp_close(sftp_handle);
	if ( rc==0 ) print_total(start, total);
	print("\r\n");
}
void sftpHost::sftp_put_one(char *src, char *dst)
{
	print("put %s\t\t\t", dst);
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_open(sftp_session, dst,
					  LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
					  LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
					  LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
	if (!sftp_handle) {
		print("\033[31mcouldn't open remote file\r\n");
		return;
	}
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\033[31mcouldn't open local file\r\n");
		libssh2_sftp_close(sftp_handle);
		return;
	}

	int nread, blocks=0;
	long total=0;
	char mem[1024*32];
	time_t start = time(NULL);
	while ( (nread=fread(mem, 1, 1024*32, fp))>0 ) {
		int rc=0;
		for ( int nwrite=0; nwrite<nread && rc>=0; ) {
			rc=libssh2_sftp_write(sftp_handle, mem+nwrite, nread-nwrite);
			if ( rc>0 ) {
				nwrite += rc;
				total += rc;
			}
		}
		if ( rc>0 ) {
			if ( ++blocks==32 ) {
				blocks = 0;
				print("\033[12D% 10ldKB", total>>10);
			}
		}
		else{
			print("error writing to host\r\n");
			break;
		}
	}
	fclose(fp);
	libssh2_sftp_close(sftp_handle);
	if ( nread==0 ) print_total(start, total);
	print("\r\n");
}
void sftpHost::sftp_get(char *src, char *dst)
{
	char mem[512];
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	if ( strchr(src,'*')==NULL && strchr(src, '?')==NULL ) {
		char lfile[1024] = ".";
		if ( *dst ) strcpy(lfile, dst);
		struct stat statbuf;
		if ( stat(lfile, &statbuf)!=-1 ) {
			if ( (statbuf.st_mode & S_IFMT) == S_IFDIR ) {
				if ( lfile[strlen(lfile)-1]!='/' ) strcat(lfile, "/");
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(lfile, p);
			}
		}
		sftp_get_one(src, lfile);
	}
	else {
		char *pattern = strrchr(src, '/');
		if ( pattern!=NULL ) *pattern++ = 0;
		if ( (sftp_handle=libssh2_sftp_opendir(sftp_session, src))!=NULL ) {
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
			libssh2_sftp_closedir(sftp_handle);
		}
		else {
			print("\033[31mcould't open remote dir \033[32m%s\r\n", src);
		}
	}
}
void sftpHost::sftp_put(char *src, char *dst)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;
	if ( stat(src, &statbuf)!=-1 ) {
		char rfile[1024] = ".";
		if ( *dst ) strcpy(rfile, dst);
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		if ( libssh2_sftp_stat(sftp_session, rfile, &attrs)==0 ) {
			if ( LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ) {
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				if ( rfile[strlen(rfile)-1]!='/') strcat(rfile, "/");
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

		if ( (dir=opendir(lfile) )!=NULL ) {
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
		else {
			print("\033[31mcouldn't open local dir \033[32m%s\r\n",lfile);
		}
	}
}
int sftpHost::sftp(char *cmd)
{
	for ( char *p=cmd; *p; p++ ) if ( *p=='\\' && p[1]!=' ' ) *p='/';
	char *p1 = strchr(cmd, ' ');
	if ( p1==NULL ) p1 = cmd+strlen(cmd);
	while ( *p1==' ' ) p1++;

	char *p2 = NULL;
	char *p = strchr(p1, ' ');
	while ( p!=NULL ) {
		if ( p[-1]=='\\' ) {	//dealing with ' ' in file names
			for ( char *q=p-1; *q; q++ ) *q = q[1];
		}
		else {
			*p++ = 0;
			p2 = p;
		}
		p = strchr(p, ' ');
	}

	char src[1024], dst[1024];
	if ( p1==NULL )				//p1 is first parameter of the command
		p1 = cmd+strlen(cmd);
	else
		while ( *p1==' ' ) p1++;

	if ( p2==NULL )				//p2 is second parameter of the command
		p2 = p1+strlen(p1);
	else
		while ( *p2==' ' ) *p2++=0;

	if ( *p1=='/' ) {
		strcpy(src, p1);		//src is remote source file
	}
	else {
		strcpy(src, realpath);
		if ( *p1 ) {
			strcat(src, "/");
			strcat(src, p1);
		}
	}

	if ( *p2=='/' ) {
		strcpy(dst, p2);		//dst is remote destination file
	}
	else {
		strcpy( dst, realpath );
		if ( *p2 ) {
			strcat( dst, "/" );
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
	else if ( strncmp(cmd, "bye",3)==0 ) {
		term_puts("Logout!", 8);
		return -1;
	}
	else if ( *cmd )
			print("\033[31m%s is not supported command,  %s\r\n\t%s\r\n",
					cmd, "\033[37mtry lcd, lpwd, cd, pwd,",
					"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
int sftpHost::read()
{
	status(HOST_CONNECTING);
	if ( tcp()==-1 ) goto TCP_Close;

	channel = NULL;
	session = libssh2_session_init();
	int rc;
	while ((rc=libssh2_session_handshake(session,sock))==LIBSSH2_ERROR_EAGAIN)
		if ( wait_socket()<0 ) break;
	if ( rc!=0 ) {
		term_puts(errmsgs[2], -2);
		goto Sftp_Close;
	}
	const char *banner;
	banner=libssh2_session_banner_get(session);
	if ( banner!=NULL ) print("%s\r\n", banner);

	status(HOST_AUTHENTICATING);
	if ( ssh_knownhost()!=0 ) {
		term_puts(errmsgs[3], -3);
		goto Sftp_Close;
	}
	if ( ssh_authentication()!=0 ) {
		term_puts(errmsgs[4], -4);
		goto Sftp_Close;
	}
	if ( !(sftp_session=libssh2_sftp_init(session)) ) {
		term_puts(errmsgs[6], -6);
		goto Sftp_Close;
	}
	if ( libssh2_sftp_realpath(sftp_session, ".", realpath, 1024)<0 )
		*realpath=0;
	strcpy(homepath, realpath);

	status(HOST_CONNECTED);
	term_puts("Connected", 0);
	bRunning = true;
	while ( bRunning ) {
		const char *cmd = term_gets("\033[32msftp> \033[37m", true);
		for ( int i=0; i<10 && cmd==NULL && bRunning; i++ )
			cmd=term_gets("", true);
		if ( cmd!=NULL ) {
			mtx.lock();
			int rc = sftp((char *)cmd);
			mtx.unlock();
			if ( rc==-1 ) break;
		}
		else { 
			term_puts("TimeOut!", 8);
			break;
		} 
	}
	libssh2_sftp_shutdown(sftp_session);
	term_puts("Disonnected", -1);

	*username = 0;
	*password = 0;

Sftp_Close:
	if ( session!=NULL ) {
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
		session = NULL;
	}
TCP_Close:
	status(HOST_IDLE);
	closesocket(sock);
	reader.detach();
	return 0;
}
int sftpHost::write(const char *buf, int len)
{
	if ( *buf=='\r' )//after files dropped
		term_puts("\r\033[32msftp> \033[37m", 17);
	return 0;
}
void sftpHost::send_file(char *src, char *dst)
{
	for ( char *p=src; *p; p++ ) if ( *p=='\\' && p[1]!=' ' ) *p='/';
	mtx.lock();
	sftp_put(src, realpath);
	mtx.unlock();
}
void sftpHost::disconn()
{
	bRunning = false;
}