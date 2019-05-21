//
// "$Id: netconf.cxx 43169 2019-04-10 21:55:10 $"
//
// confHost ietfNode
//
// netconf host implementation for terminal simulator flTerm
//
// Copyright 2017-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//

#include <time.h>
#include <stdio.h>
#include "node.h"
#include "conf.h"

static const char *errmsgs[] = { 
"Disconnected", "Connection", "Session",
"Verification", "Authentication",
"Channel", "Subsystem"
};
void xml_print(const char *name, const char *msg, int len);

void netconf_cb(void *data, const char *msg, int len)
{
	ietfNode *pNode = (ietfNode *)data;
	if ( len<=0 ) {
		sql_queue(NODE_STATUS, msg, pNode->name() );
		if ( len==-8 ) {
			pNode->commloss(true);
			time_t now = time(0);
			char *emstime = ctime(&now);
			sql_queue(ALARM_INSERT, pNode->name(), 
					"netconf", "comm loss","warning", "no", "", emstime+4, "");
		}
		if ( len==0 && *msg=='C' && pNode->commloss() ) {
			pNode->commloss(false);
			time_t now = time(0);
			char *emstime = ctime(&now);
			sql_queue(ALARM_CLEARED, emstime+4, pNode->name(), 
					"netconf", "comm loss", "");
		}
	}
	else {
		if ( pNode->logg() )
			xml_print(pNode->name(), msg, len);
		pNode->parse(msg);
	}
}

/*********************************confHost*******************************/
extern const char *IETF_HELLO;						//defined in ssh2.cxx
const char *IETF_MSG="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"%d\">\n\
%s</rpc>]]>]]>";
confHost::confHost(const char *name, int in):sshHost(name)
{
	if ( port==22 ) port = 830;
	interactive = in;
}	
const int BUFLEN=65536*4-1;
int confHost::read()
{
	do_callback("trying...", 0);	//indicates connecting
	if ( tcp()<0 ) {
		do_callback(errmsgs[1], -1);
		goto TCP_Close;
	}
	
	session = libssh2_session_init();
	if ( libssh2_session_handshake(session, sock)!=0 ) { 
		do_callback(errmsgs[2], -2);
		goto Session_Close; 
	}
	if ( ssh_knownhost(interactive)!=0 ) { //not interactive
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
	if ( libssh2_channel_subsystem(channel, "netconf") ) { 
		do_callback(errmsgs[6], -6);
		goto Channel_Close; 
	}
	//must be nonblocking for 2 channels on the same session
	libssh2_session_set_blocking(session, 0); 
	libssh2_channel_write( channel, IETF_HELLO, strlen(IETF_HELLO) );
	msg_id = 0;
	channel2 = NULL;

	do_callback("Connected", 0);
	int len, len2, rd, rd2;
	char reply[BUFLEN+1], notif[BUFLEN+1];
	rd = rd2 = 0;
	do {
		char *delim;
		do { 
			mtx.lock();
			len=libssh2_channel_read(channel,reply+rd,BUFLEN-rd);
			mtx.unlock();
			if ( len>0 ) {
				rd += len; 
				reply[rd] = 0;
				while ( (delim=strstr(reply, "]]>]]>")) != NULL ) {
					*delim=0; 
					do_callback(reply, delim-reply);
					delim+=6;
					rd -= delim-reply;
					memmove(reply, delim, rd+1); 
				}
				if ( rd==BUFLEN ) {
					do_callback(reply, BUFLEN);
					rd = 0;
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
					rd2 += len2;
					notif[rd2] = 0;
					while( (delim=strstr(notif, "]]>]]>")) != NULL ) {
						*delim=0;
						do_callback(notif, delim-notif);
						delim+=6;
						rd2 -= delim - notif;
						memmove(notif, delim, rd2+1); 
					}
					if ( rd2==BUFLEN ) {
						do_callback(notif, BUFLEN);
						rd2 = 0;
					}
				}
			}
			while ( len2>0 );
			if ( len2!=LIBSSH2_ERROR_EAGAIN ) break;
		}
	}
	while ( wait_socket()>0 );
	if ( channel==NULL ) 
		do_callback("Disconnected", 0);
	else
		do_callback("Comm loss", -8);

Channel_Close:
	if ( channel!=NULL ) {
		mtx.lock();
		libssh2_channel_close(channel);
		if ( channel2 ) libssh2_channel_close(channel2);
		channel = NULL;
		channel2 = NULL;
		mtx.unlock();
	}
Session_Close:
	if ( session!=NULL ) {
		mtx.lock();
		libssh2_session_free(session);
		session = NULL;
		mtx.unlock();
	}
TCP_Close:
	closesocket(sock);
	reader.detach();
	return 0;
}
int confHost::write(const char *msg, int len)
{
	if ( channel!=NULL ) {
		char buf[8192];
		len = sprintf(buf, IETF_MSG, ++msg_id, msg);
		do_callback(buf, len);

		int total=0, cch=0;
		while ( total<len ) {
			mtx.lock();
			if ( channel!=NULL ) 
				cch = libssh2_channel_write(channel, buf+total, len-total); 
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
		return cch<=0 ? cch : msg_id;
	}
	else {
		if ( reader.joinable() ) 
			write_keys(msg, len);
		else
			connect();
	}
	return 0;
} 
int confHost::write2(const char *msg, int len)
{
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
const char *child_text(tinyxml2::XMLElement *elem, const char *childname)
{
	const char *txt;
	static const char *empty="";
	tinyxml2::XMLElement *child = elem->FirstChildElement(childname);
	if ( child!=NULL ) {
		txt = child->GetText();
		if ( txt!=NULL ) return txt;
	}
	return empty;
}

const char *IETF_ALARM="<get><filter type=\"subtree\">\
<alarms xmlns=\"urn:ietf:params:xml:ns:yang:ietf-alarms\">\
<alarm-list/></alarms></filter></get>";
const char *IETF_STREAM="<get><filter type=\"subtree\">\
<netconf xmlns=\"urn:ietf:params:xml:ns:netmod:notification\">\
<streams/></netconf></filter></get>";
const char *IETF_SUBSCRIBE="<create-subscription xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\">\
<stream>%s</stream></create-subscription>";

ietfNode::~ietfNode()
{
	if ( host!=NULL ) {
		host->disconn();
		delete host;
	}
	sql_queue(NODE_STATUS, "", nodename );
	sql_queue(NODE_MEMO, "", nodename);
}
void ietfNode::keepalive()
{
	write( IETF_ALARM );
}
void ietfNode::get_pm_15min()
{
}
int ietfNode::command( const char *msg, char **reply )
{
	int msgid = write(msg);
	rpc_queue.push(msgid);
	for ( int i=0; i<300; i++ ) {
		if ( rpc_map.find(msgid)!=rpc_map.end() ) {
			*reply = rpc_map[msgid];
			rpc_map.erase(msgid);
			return strlen(*reply);
		}
#ifdef WIN32
		Sleep(100);
#else
		usleep(100000);
#endif
	}
	return 0;
}
int ietfNode::parse( const char *xml )
{
	tinyxml2::XMLDocument doc;
	if ( doc.Parse(xml)!=0 ) return false;
	msgCount1M++;
	msgCount15M++;

	confHost *pHost = (confHost *)host;
	tinyxml2::XMLElement *hello = doc.FirstChildElement("hello");
	if ( hello!=NULL ) {
		if ( strstr(xml, "capability:interleave:1.0")==NULL ) {
			isInterleave=false;
		}
		sync();
		write(IETF_STREAM);
		if ( strstr(xml, "S100-ALARMS")!=NULL ) {
			char req[1024];
			sprintf(req, IETF_SUBSCRIBE, "NETCONF");
			pHost->write2(req, strlen(req));
		}
	}
	tinyxml2::XMLElement *notif = doc.FirstChildElement("notification");
	if ( notif!=NULL ) return parse_notif( notif );

	tinyxml2::XMLElement *reply = doc.FirstChildElement("rpc-reply");
	if ( reply!=NULL ) {
		if ( !rpc_queue.empty() ) {
			int msgid = 0;
			const char *p = strstr(xml, "message-id=");
			if ( p!=NULL ) msgid = atoi(p+12);
			while ( !rpc_queue.empty() && msgid>rpc_queue.front() )
				rpc_queue.pop();
			if ( !rpc_queue.empty() && msgid==rpc_queue.front() ) {
				rpc_map[msgid] = strdup(xml);
				rpc_queue.pop();
				return true;
			}
		}
		tinyxml2::XMLElement *data = reply->FirstChildElement("data");
		if ( data==NULL ) return true;
		tinyxml2::XMLElement *netconf = data->FirstChildElement("netconf");
		if ( netconf==NULL ) return parse_reply( data );
		tinyxml2::XMLElement *streams = netconf->FirstChildElement("streams");
		if ( streams==NULL ) return true;

		const char *streamName = NULL;
		tinyxml2::XMLElement *stream = streams->FirstChildElement("stream");
		while ( stream!=NULL ) {
			streamName = child_text(stream, "name");
			stream = stream->NextSiblingElement("stream");
		}
		if ( streamName!=NULL ) {
			char req[1024];
			sprintf(req, IETF_SUBSCRIBE, streamName);
			if ( isInterleave )
				pHost->write(req, strlen(req));
			else
				pHost->write2(req, strlen(req));
		}
	}
	return true;
}
int ietfNode::parse_reply( tinyxml2::XMLElement *data )
{
	return true;
}
int ietfNode::parse_notif( tinyxml2::XMLElement *notification )
{
	return true;
}