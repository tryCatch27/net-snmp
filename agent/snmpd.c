/*
 * snmpd.c - rrespond to SNMP queries from management stations
 *
 */
/***********************************************************
	Copyright 1988, 1989 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/
#include <config.h>

#include <stdio.h>
#include <errno.h>
#if HAVE_STRINGS_H
#include <strings.h>
#else
#include <string.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <sys/socket.h>
#include <net/if.h>
#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#if HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <sys/wait.h>
#include <signal.h>

#ifndef FD_SET
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
typedef long    fd_mask;
#define NFDBITS (sizeof(fd_mask) * NBBY)        /* bits per mask */
#define FD_SET(n, p)    ((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
#define FD_CLR(n, p)    ((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
#define FD_ISSET(n, p)  ((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
#define FD_ZERO(p)      memset((p), 0, sizeof(*(p)))
#endif

#include "mib_module_config.h"
#include "asn1.h"
#include "snmp_api.h"
#include "snmp_impl.h"
#include "system.h"
#include "snmp.h"
#include "m2m.h"
#include "party.h"
#ifdef USING_ALARM_MODULE
#include "mibgroup/alarm.h"
#endif
#include "event.h"
#include "view.h"
#include "context.h"
#include "acl.h"
#include "mib.h"
#include "mibgroup/snmp_mib.h"
#include "snmp_client.h"
#include "snmpd.h"
#include "var_struct.h"
#include "read_config.h"
#include "mibgroup/struct.h"
#include "mibgroup/util_funcs.h"

extern int  errno;
extern char *version_descr;
extern oid version_id[];
extern int version_id_len;
struct timeval starttime;
int log_addresses = 0;
int verbose = 0;
int snmp_dump_packet;

oid version_id[] = {EXTENSIBLEMIB,AGENTID,OSTYPE};
int version_id_len = sizeof(version_id)/sizeof(version_id[0]);

struct addrCache {
    u_long addr;
    int status;
#define UNUSED	0
#define USED	1
#define OLD	2
};

#define ADDRCACHE 10

static struct addrCache addrCache[ADDRCACHE];
static int lastAddrAge = 0;

extern void init_snmp __P((void));

int receive __P((int *, int));
int snmp_read_packet __P((int));
char *sprintf_stamp __P((time_t *));
int agent_party_init __P((in_addr_t, u_short, char *));
int create_v1_trap_session __P((char *, char *));
void send_v1_trap __P((struct snmp_session *, int, int));
char *reverse_bytes __P((char *, int));
void usage __P((char *));
int main __P((int, char **));
int snmp_input __P((int, struct snmp_session *, int, struct snmp_pdu *, void *));
RETSIGTYPE SnmpTrapNodeDown __P((int));

char *sprintf_stamp (now)
    time_t *now;
{
    time_t Now;
    struct tm *tm;
    static char sbuf [20];

    if (now == NULL) {
	now = &Now;
	time (now);
    }
    tm = localtime (now);
    sprintf(sbuf, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
	    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);
    return sbuf;
}

/*
 * In: My ip address, View subtree
 * Initializes a noAuth/noPriv party pair, a context, 2 acl entries, and
 * a view subtree. (Are two acl entries really needed?)
 * The view subtree is included by default and has no Mask.
 * Out: returns 0 if OK, 1 if conflict with a pre-installed
 * party/context/acl/view, -1 if an error occurred.
 */
int
agent_party_init(myaddr, dest_port, view)
    in_addr_t myaddr;
    u_short dest_port;
    char *view;
{
    u_int addr;
    unsigned char *adp;
    u_short port;
    oid partyid1[64];
    int partyidlen1;
    oid partyid2[64];
    int partyidlen2;
    oid contextid[64];
    int contextidlen;
    struct partyEntry *pp1, *pp2, *rp;
    struct contextEntry *cxp, *rxp;
    int viewIndex;
    oid viewSubtree[64];
    int viewSubtreeLen;
    struct viewEntry *vwp;
    struct aclEntry *ap;
    int oneIndex, twoIndex, cxindex;

    
    /*
     * Check for existence of the party, context, acl, and view and
     * exit if any of them exist.  We must create the parties to get the
     * partyIndexes for acl creation, so we delete these parties if we
     * fail anywhere else.
     */
    /* This would be better written as follows:
       We currently check for the existence of each of the
       src/dst/context/acl/view entries before creating anything.
       The problem is that in order to check for the existence of the
       acl entry, we need to create the src/dst/context to get their
       indexes.  So we create them with the proviso that we delete them
       if checks for other src/dst/context/view/acl fail.  [BUG:  we don't
       delete context and view if acl fails or context if view fails].
       Observation:  Because each index for the acl table is taken from
       a newly-created and therefore unique src/dst/context index, there
       is no reason to check for the existence of such an acl entry.
       Therefore, there is no reason to create the party entries until
       *after* we have checked everything.  This greatly simplifies this code.
       In addition, nobody cares what the view index is, so there is no need
       to check for the view's existence (just choose something that isn't
       in use.

       Suggestion:
       check src
       check dst
       check context
       if any used, fail 1
       create src, dst, context
       create acl(src.index, dst.index, context.index) and its brother
       find an unused view index (preferably one)
       create viewEntry(viewIndex, viewSubtree)
       context.viewIndex = viewIndex
     */
    partyidlen1 = 64;
    if (!read_objid(".1.3.6.1.6.3.3.1.3.128.2.35.55.1",
		    partyid1, &partyidlen1)){
	fprintf(stderr, "Bad object identifier: %s\n",
		".1.3.6.1.6.3.3.1.3.128.2.35.55.1");
	return -1;
    }
    adp = (unsigned char *)&myaddr;
    partyid1[9] =  adp[0];
    partyid1[10] = adp[1];
    partyid1[11] = adp[2];
    partyid1[12] = adp[3];
    partyid1[13] = 1;
    pp1 = party_getEntry(partyid1, partyidlen1);
    if (pp1){
	return 1;
    }
    pp1 = party_createEntry(partyid1, partyidlen1);
    oneIndex = pp1->partyIndex;

    partyidlen2 = 64;
    if (!read_objid(".1.3.6.1.6.3.3.1.3.128.2.35.55.1",
		    partyid2, &partyidlen2)){
	fprintf(stderr, "Bad object identifier: %s\n",
		".1.3.6.1.6.3.3.1.3.128.2.35.55.1");
	party_destroyEntry(partyid1, partyidlen1);
	return -1;
    }
    partyid2[9] =  adp[0];
    partyid2[10] = adp[1];
    partyid2[11] = adp[2];
    partyid2[12] = adp[3];
    partyid2[13] = 2;
    pp2 = party_getEntry(partyid2, partyidlen2);
    if (pp2){
	party_destroyEntry(partyid1, partyidlen1);
	return 1;
    }
    pp2 = party_createEntry(partyid2, partyidlen2);
    twoIndex = pp2->partyIndex;

    contextidlen = 64;
    if (!read_objid(".1.3.6.1.6.3.3.1.4.128.2.35.55.1",
		    contextid, &contextidlen)){
	fprintf(stderr, "Bad object identifier: %s\n",
		".1.3.6.1.6.3.3.1.4.128.2.35.55.1");
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return -1;
    }
    contextid[9] =  adp[0];
    contextid[10] = adp[1];
    contextid[11] = adp[2];
    contextid[12] = adp[3];
    contextid[13] = 1;
    cxp = context_getEntry(contextid, contextidlen);
    if (cxp){
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return 1;
    }

    viewIndex = 1;
    viewSubtreeLen = 64;
    if (!read_objid(view, viewSubtree, &viewSubtreeLen)){
	fprintf(stderr, "Bad object identifier: %s\n", view);
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return -1;
    }
    vwp = view_getEntry(viewIndex, viewSubtree, viewSubtreeLen);
    if (vwp){
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return 1;
    }

    ap = acl_getEntry(oneIndex, twoIndex, 1);
    if (ap){
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return 1;
    }
    ap = acl_getEntry(twoIndex, oneIndex, 1);
    if (ap){
	party_destroyEntry(partyid1, partyidlen1);
	party_destroyEntry(partyid2, partyidlen2);
	return 1;
    }

    rp = pp1->reserved;
    strcpy(pp1->partyName, "noAuthAgent");
    pp1->partyTDomain = rp->partyTDomain = DOMAINSNMPUDP;
    addr = htonl(myaddr);
    port = htons(dest_port);
    memcpy(pp1->partyTAddress, &addr, sizeof(addr));
    memcpy(pp1->partyTAddress + 4, &port, sizeof(port));
    memcpy(rp->partyTAddress, pp1->partyTAddress, 6);
    pp1->partyTAddressLen = rp->partyTAddressLen = 6;
    pp1->partyAuthProtocol = rp->partyAuthProtocol = NOAUTH;
    pp1->partyAuthClock = rp->partyAuthClock = 0;
    pp1->tv.tv_sec = pp1->partyAuthClock;
    pp1->partyAuthPublicLen = 0;
    pp1->partyAuthLifetime = rp->partyAuthLifetime = 0;
    pp1->partyPrivProtocol = rp->partyPrivProtocol = NOPRIV;
    pp1->partyPrivPublicLen = 0;
    pp1->partyMaxMessageSize = rp->partyMaxMessageSize = 1500;
    pp1->partyLocal = 1; /* TRUE */
    pp1->partyAuthPrivateLen = rp->partyAuthPrivateLen = 0;
    pp1->partyPrivPrivateLen = rp->partyPrivPrivateLen = 0;
    pp1->partyStorageType = SNMP_STORAGE_VOLATILE;
    pp1->partyStatus = rp->partyStatus = SNMP_ROW_ACTIVE;
#define PARTYCOMPLETE_MASK              65535
    /* all collumns - from party_vars.c XXX */
    pp1->partyBitMask = rp->partyBitMask = PARTYCOMPLETE_MASK;

    rp = pp2->reserved;
    strcpy(pp2->partyName, "noAuthMS");
    pp2->partyTDomain = rp->partyTDomain = DOMAINSNMPUDP;
    memset(pp2->partyTAddress, 0, 6);
    memcpy(rp->partyTAddress, pp2->partyTAddress, 6);
    pp2->partyTAddressLen = rp->partyTAddressLen = 6;
    pp2->partyAuthProtocol = rp->partyAuthProtocol = NOAUTH;
    pp2->partyAuthClock = rp->partyAuthClock = 0;
    pp2->tv.tv_sec = pp2->partyAuthClock;
    pp2->partyAuthPublicLen = 0;
    pp2->partyAuthLifetime = rp->partyAuthLifetime = 0;
    pp2->partyPrivProtocol = rp->partyPrivProtocol = NOPRIV;
    pp2->partyPrivPublicLen = 0;
    pp2->partyMaxMessageSize = rp->partyMaxMessageSize = 484; /* ??? */
    pp2->partyLocal = 2; /* FALSE */
    pp2->partyAuthPrivateLen = rp->partyAuthPrivateLen = 0;
    pp2->partyPrivPrivateLen = rp->partyPrivPrivateLen = 0;
    pp2->partyStorageType = SNMP_STORAGE_VOLATILE;
    pp2->partyStatus = rp->partyStatus = SNMP_ROW_ACTIVE;
    pp2->partyBitMask = rp->partyBitMask = PARTYCOMPLETE_MASK;
 
    cxp = context_createEntry(contextid, contextidlen);
    rxp = cxp->reserved;
    strcpy(cxp->contextName, "noAuthContext");
    cxp->contextLocal = 1; /* TRUE */
    cxp->contextViewIndex = 1;
    cxp->contextLocalEntityLen = 0;
    cxp->contextLocalTime = CURRENTTIME;
    cxp->contextProxyContextLen = 0;
    cxp->contextStorageType = SNMP_STORAGE_VOLATILE;
    cxp->contextStatus = rxp->contextStatus = SNMP_ROW_ACTIVE;
#define CONTEXTCOMPLETE_MASK              0x03FF
    /* all collumns - from context_vars.c XXX */
    cxp->contextBitMask = rxp->contextBitMask = CONTEXTCOMPLETE_MASK;
    cxindex = cxp->contextIndex;

    vwp = view_createEntry(viewIndex, viewSubtree, viewSubtreeLen);
    vwp->viewType = VIEWINCLUDED;
    vwp->viewMaskLen = 0;
    vwp->viewStorageType = SNMP_STORAGE_VOLATILE;
    vwp->viewStatus = SNMP_ROW_ACTIVE;
#define VIEWCOMPLETE_MASK              0x3F
    /* all collumns - from view_vars.c XXX */
    vwp->viewBitMask = VIEWCOMPLETE_MASK;
    vwp->reserved->viewBitMask = vwp->viewBitMask;

    viewSubtreeLen = 64;
    if (!read_objid(".2.6.6", viewSubtree, &viewSubtreeLen)){
	fprintf(stderr, "Bad object identifier: .2.6.6\n");
	return -1;
    }
    vwp = view_createEntry(viewIndex, viewSubtree, viewSubtreeLen);
    vwp->viewType = VIEWINCLUDED;
    vwp->viewMaskLen = 0;
    vwp->viewStorageType = SNMP_STORAGE_VOLATILE;
    vwp->viewStatus = SNMP_ROW_ACTIVE;
    vwp->viewBitMask = VIEWCOMPLETE_MASK;
    vwp->reserved->viewBitMask = vwp->viewBitMask;

    ap = acl_createEntry(oneIndex, twoIndex, cxindex);
    ap->aclPriveleges = 132;
    ap->aclStorageType = SNMP_STORAGE_VOLATILE;
    ap->aclStatus = SNMP_ROW_ACTIVE;
#define ACLCOMPLETE_MASK              0x3F
    /* all collumns - from acl_vars.c XXX */
    ap->aclBitMask = ACLCOMPLETE_MASK;
    ap->reserved->aclBitMask = ap->aclBitMask;

    ap = acl_createEntry(twoIndex, oneIndex, cxindex);
    /* To play around with SETs with a minimum of hassle, set this to 43
       and noAuth/noPriv parties will be able to set in this default view.
       Remember to turn it back off when you're done! */
    ap->aclPriveleges = 35;
    ap->aclStorageType = SNMP_STORAGE_VOLATILE;
    ap->aclStatus = SNMP_ROW_ACTIVE;
    ap->aclBitMask = ACLCOMPLETE_MASK;
    ap->reserved->aclBitMask = ap->aclBitMask;

    return 0; /* SUCCESS */
}

struct trap_sink {
    struct snmp_session ses;
    struct snmp_session *sesp;
    struct trap_sink *next;
} *sinks = NULL;

int snmp_enableauthentraps = 2;		/* default: 2 == disabled */
char *snmp_trapsink;
char *snmp_trapcommunity = "public";

int create_v1_trap_session (sink, com)
    char *sink, *com;
{
    struct trap_sink *new_sink =
      (struct trap_sink *) malloc (sizeof (*new_sink));

    memset (&new_sink->ses, 0, sizeof (struct snmp_session));
    new_sink->ses.peername = strdup(sink);
    new_sink->ses.version = SNMP_VERSION_1;
    if (com) {
        new_sink->ses.community = strdup (com);
        new_sink->ses.community_len = strlen (com);
    }
    new_sink->ses.remote_port = SNMP_TRAP_PORT;
    new_sink->sesp = snmp_open (&new_sink->ses);
    if (new_sink->sesp) {
	new_sink->next = sinks;
	sinks = new_sink;
	return 1;
    }
    snmp_perror("snmpd");
    free(new_sink);
    return 0;
}

void
send_v1_trap (ss, trap, specific)
    struct snmp_session *ss;
    int trap, specific;
{
    struct snmp_pdu *pdu;
    struct timeval now, diff;

    gettimeofday(&now, NULL);
    now.tv_sec--;
    now.tv_usec += 1000000L;
    diff.tv_sec = now.tv_sec - starttime.tv_sec;
    diff.tv_usec = now.tv_usec - starttime.tv_usec;
    if (diff.tv_usec > 1000000L){
	diff.tv_usec -= 1000000L;
	diff.tv_sec++;
    }

    pdu = snmp_pdu_create (SNMP_MSG_TRAP);
    pdu->enterprise = version_id;
    pdu->enterprise_length = version_id_len;
    pdu->agent_addr.sin_addr.s_addr = get_myaddr();
    pdu->trap_type = trap;
    pdu->specific_type = specific;
    pdu->time = diff.tv_sec * 100 + diff.tv_usec / 10000;
    if (snmp_send (ss, pdu) == 0) {
        snmp_perror ("snmpd: send_trap");
    }
#ifdef USING_SNMP_MODULE       
    snmp_outtraps++;
#endif
}

void
send_easy_trap (trap)
      int trap;
{
    struct trap_sink *sink = sinks;

    if ((snmp_enableauthentraps == 1 || trap != 4) && sink != NULL) {
	while (sink) {
	    send_v1_trap (sink->sesp, trap, 0);
	    sink = sink->next;
	}
    }
}
  
char *reverse_bytes(buf,num)
  char *buf;
  int num;
{
  static char outbuf[100];
  int i;
  
  for(i=num-1;i>=0;i--)
    outbuf[i] = *buf++;
  return(outbuf);
}

char **argvrestartp;
char *argvrestart;
char *argvrestartname;

#include "version.h"

extern char *optconfigfile;
extern char dontReadConfigFiles;

void usage(prog)
char *prog;
{
  printf("\nUsage:  %s [-h] [-v] [-f] [-a] [-d] [-q] [-D] [-p NUM] [-L] [-l LOGFILE]\n",prog);
  printf("\n\tVersion:  %s\n",VersionInfo);
  printf("\tAuthor:   Wes Hardaker\n");
  printf("\tEmail:    ucd-snmp-coders@ece.ucdavis.edu\n");
  printf("\n-h\t\tThis usage message.\n");
  printf("-v\t\tVersion information.\n");
  printf("-f\t\tDon't fork from the shell.\n");
  printf("-a\t\tLog addresses.\n");
  printf("-d\t\tDump sent and received UDP SNMP packets\n");
  printf("-q\t\tPrint information in a more parsable format (quick-print)\n");
  printf("-D\t\tTurn on debugging output\n");
  printf("-p NUM\t\tRun on port NUM instead of the default:  161\n");
  printf("-c CONFFILE\tRead CONFFILE as a configuration file.\n");
  printf("-C\t\tDon't read the default configuration files.\n");
  printf("-L\t\tPrint warnings/messages to stdout/err rather than a logfile\n");
  printf("-l LOGFILE\tPrint warnings/messages to LOGFILE\n");
  printf("\t\t(By default LOGFILE=%s)\n",
#ifdef LOGFILE
         LOGFILE
#else
    "stdout/err"
#endif
    );
  printf("\n");
  exit(1);
}

RETSIGTYPE
SnmpTrapNodeDown(a)
  int a;
{
  send_easy_trap (2); /* 2 - Node Down #define it as NODE_DOWN_TRAP */
  exit(1);
}

int
main(argc, argv)
    int	    argc;
    char    *argv[];
{
    int	arg,i;
    int sd, sdlist[32], portlist[32], sdlen = 0, index;
    struct sockaddr_in	me;
    int port_flag = 0, ret;
    u_short dest_port = 161;
    struct partyEntry *pp;
    in_addr_t myaddr;
    int dont_fork=0;
    char logfile[300], miscfile[300];
    char *cptr, **argvptr;

    logfile[0] = 0;
    optconfigfile = NULL;
    dontReadConfigFiles = 0;
    
#ifdef LOGFILE
    strcpy(logfile,LOGFILE);
#endif

    /*
     * usage: snmpd
     */
    for(arg = 1; arg < argc; arg++){
	if (argv[arg][0] == '-'){
	    switch(argv[arg][1]){
                case 'c':
                    optconfigfile = strdup(argv[++arg]);
                    break;
                case 'C':
                    dontReadConfigFiles = 1;
                    break;
		case 'd':
		    snmp_dump_packet++;
		    verbose = 1;
		    break;
		case 'q':
		    snmp_set_quick_print(1);
		    break;
		case 'D':
		    snmp_set_do_debugging(1);
		    break;
                case 'p':
                    port_flag++;
                    dest_port = atoi(argv[++arg]);
                    break;
		case 'a':
		    log_addresses++;
		    break;
		case 'V':
		    verbose = 1;
		    break;
		case 'f':
		    dont_fork = 1;
		    break;
                case 'l':
                    strcpy(logfile,argv[++arg]);
                    break;
                case 'L':
                    logfile[0] = 0;
                    break;
                case 'h':
                    usage(argv[0]);
                    break;
                case 'v':
                    printf("\nUCD-snmp version:  %s\n",VersionInfo);
                    printf("Author:            Wes Hardaker\n");
                    printf("Email:             ucd-snmp-coders@ece.ucdavis.edu\n\n");
                    exit (0);
                case '-':
                  switch(argv[arg][2]){
                    case 'v': 
                      printf("\nUCD-snmp version:  %s\n",VersionInfo);
                      printf("Author:            Wes Hardaker\n");
                      printf("Email:             ucd-snmp-coders@ece.ucdavis.edu\n\n");
                      exit (0);
                    case 'h':
                      usage(argv[0]);
                      exit(0);
                  }
		default:
		    printf("invalid option: %s\n", argv[arg]);
                    usage(argv[0]);
		    break;
	    }
	    continue;
	}
    }
    /* initialize a argv set to the current for restarting the agent */
    argvrestartp = (char **) malloc((argc+2) * sizeof (char *));
    argvptr = argvrestartp;
    for(i=0, ret = 1; i < argc; i++) {
      ret += strlen(argv[i])+1;
    }
    argvrestart = (char *) malloc((ret));
    argvrestartname = (char *) malloc(strlen(argv[0])+1);
    strcpy(argvrestartname,argv[0]);
    for(cptr = argvrestart,i = 0; i < argc; i++) {
      strcpy(cptr,argv[i]);
      *(argvptr++) = cptr;
      cptr += strlen(argv[i]) + 1;
    }
    *cptr = 0;
    *argvptr = NULL;

    /* open the logfile if necessary */
    if (logfile[0]) {
      close(1);
      open(logfile,O_WRONLY|O_CREAT|O_TRUNC,0644);
      close(2);
      dup(1);
      close(0);
    }
    setvbuf (stdout, NULL, _IOLBF, BUFSIZ);
    printf ("%s UCD-SNMP version %s\n", sprintf_stamp (NULL), VersionInfo);
    if (!dont_fork && fork() != 0)   /* detach from shell */
      exit(0);
    init_snmp();
    init_mib();
    sprintf(miscfile,"%s/party.conf",SNMPSHAREPATH);
    if (read_party_database(miscfile) > 0){
	fprintf(stderr, "Couldn't read party database from %s\n",miscfile);
	exit(0);
    }
    sprintf(miscfile,"%s/context.conf",SNMPSHAREPATH);
    if (read_context_database(miscfile) > 0){
	fprintf(stderr, "Couldn't read context database from %s\n",miscfile);
	exit(0);
    }
    sprintf(miscfile,"%s/acl.conf",SNMPSHAREPATH);
    if (read_acl_database(miscfile) > 0){
	fprintf(stderr, "Couldn't read acl database from %s\n",miscfile);
	exit(0);
    }
    sprintf(miscfile,"%s/view.conf",SNMPSHAREPATH);
    if (read_view_database(miscfile) > 0){
	fprintf(stderr, "Couldn't read view database from %s\n",miscfile);
	exit(0);
    }
    
    myaddr = get_myaddr();
    /* XXX mib-2 subtree only??? */
    if ((ret = agent_party_init(myaddr, dest_port, ".iso.org.dod.internet"))){
	if (ret == 1){
	    fprintf(stderr, "Conflict found with initial noAuth/noPriv parties... continuing\n");
	} else if (ret == -1){
	    fprintf(stderr, "Error installing initial noAuth/noPriv parties, exiting\n");
	    exit(1);
	} else {
	    fprintf(stderr, "Unknown error, exiting\n");
	    exit(2);
	}
    }

    printf("Opening port(s): "); 
    fflush(stdout);
    party_scanInit();
    for(pp = party_scanNext(); pp; pp = party_scanNext()){
#if WORDS_BIGENDIAN
        if ((pp->partyTDomain != DOMAINSNMPUDP)
	    || memcmp(&myaddr, pp->partyTAddress, 4))
          continue;	/* don't listen for non-local parties */
#else
	if ((pp->partyTDomain != DOMAINSNMPUDP)
	    || memcmp(reverse_bytes((char *) &myaddr,sizeof(myaddr)),
                    pp->partyTAddress, 4))
          continue;	/* don't listen for non-local parties */
#endif
	
	dest_port = 0;
#if WORDS_BIGENDIAN
	memcpy(&dest_port, pp->partyTAddress + 4, 2);
#else
	memcpy(&dest_port, reverse_bytes(pp->partyTAddress + 4,2), 2);
#endif
	for(index = 0; index < sdlen; index++)
	    if (dest_port == portlist[index])
		break;
	if (index < sdlen)  /* found a hit before the end of the list */
	    continue;
	printf("%u ", dest_port); 
	fflush(stdout);
	/* Set up connections */
	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0){
	    perror("socket");
	    return 1;
	}
	me.sin_family = AF_INET;
	me.sin_addr.s_addr = INADDR_ANY;
	/* already in network byte order (I think) */
	me.sin_port = htons(dest_port);
	if (bind(sd, (struct sockaddr *)&me, sizeof(me)) != 0){
	    fprintf(stderr,"bind/%d: ", ntohs(me.sin_port));
	    perror(NULL);
	    return 2;
	}
	sdlist[sdlen] = sd;
	portlist[sdlen] = dest_port;
        fcntl(sd,F_SETFD,1);           /* close on exec */
	if (++sdlen == 32){
	    printf("No more sockets... ignoring rest of file\n");
	    break;
	}	
    }
    printf("\n");
    fflush(stdout);

    /* get current time (ie, the time the agent started) */
    gettimeofday(&starttime, NULL);
    starttime.tv_sec--;
    starttime.tv_usec += 1000000L;

    /* send coldstart trap via snmptrap(1) if possible */
    send_easy_trap (0);
    signal(SIGTERM, SnmpTrapNodeDown);
    signal(SIGSTOP, SnmpTrapNodeDown);

    memset(addrCache, 0, sizeof(addrCache));
    receive(sdlist, sdlen);
    return 0;
}

int
receive(sdlist, sdlen)
    int sdlist[];
    int sdlen;
{
    int numfds, index;
    fd_set fdset;
    struct timeval  timeout, *tvp = &timeout;
    struct timeval  sched, *svp = &sched, now, *nvp = &now;
    int count, block;


    gettimeofday(nvp, (struct timezone *) NULL);
    if (nvp->tv_usec < 500000L){
	svp->tv_usec = nvp->tv_usec + 500000L;
	svp->tv_sec = nvp->tv_sec;
    } else {
	svp->tv_usec = nvp->tv_usec - 500000L;
	svp->tv_sec = nvp->tv_sec + 1;
    }
    while(1){
	tvp =  &timeout;
	tvp->tv_sec = 0;
	tvp->tv_usec = 500000L;

	numfds = 0;
	FD_ZERO(&fdset);
	for(index = 0; index < sdlen; index++){
	    if (sdlist[index] + 1 > numfds)
		numfds = sdlist[index] + 1;
	    FD_SET(sdlist[index], &fdset);
	}
        block = 0;
        snmp_select_info(&numfds, &fdset, tvp, &block);
        if (block == 1)
            tvp = NULL; /* block without timeout */
	count = select(numfds, &fdset, 0, 0, tvp);
	if (count > 0){
	    for(index = 0; index < sdlen; index++){
		if(FD_ISSET(sdlist[index], &fdset)){
		    snmp_read_packet(sdlist[index]);
		    FD_CLR(sdlist[index], &fdset);
		}
	    }
	    snmp_read(&fdset);
	} else switch(count){
	    case 0:
                snmp_timeout();
                break;
	    case -1:
		if (errno == EINTR){
		    continue;
		} else {
		    perror("select");
		}
		return -1;
	    default:
		printf("select returned %d\n", count);
		return -1;
	}
        gettimeofday(nvp, (struct timezone *) NULL);
	if (nvp->tv_sec > svp->tv_sec
	    || (nvp->tv_sec == svp->tv_sec && nvp->tv_usec > svp->tv_usec)){
#ifdef USING_ALARM_MODULE
	    alarmTimer(nvp);
#endif
#ifdef USING_EVENT_MODULE
	    eventTimer(nvp);
#endif
	    if (nvp->tv_usec < 500000L){
		svp->tv_usec = nvp->tv_usec + 500000L;
		svp->tv_sec = nvp->tv_sec;
	    } else {
		svp->tv_usec = nvp->tv_usec - 500000L;
		svp->tv_sec = nvp->tv_sec + 1;
	    }
	    if (log_addresses && lastAddrAge++ > 600){
		int count;
		
		lastAddrAge = 0;
              for(count = 0; count < ADDRCACHE; count++){
		    if (addrCache[count].status == OLD)
			addrCache[count].status = UNUSED;
		    if (addrCache[count].status == USED)
			addrCache[count].status = OLD;
		}
	    }
	}
    }
    return 0;
}

int
snmp_read_packet(sd)
    int sd;
{
    struct sockaddr_in	from;
    int length, out_length, fromlength;
    u_char  packet[1500], outpacket[1500];

    fromlength = sizeof from;
    length = recvfrom(sd, (char *) packet, 1500, 0, (struct sockaddr *)&from,
		      &fromlength);
    if (length == -1)
	perror("recvfrom");
#ifdef USING_SNMP_MODULE       
    snmp_inpkts++;
#endif
    if (snmp_dump_packet){
	printf("\nrecieved %d bytes from %s:\n", length,
	       inet_ntoa(from.sin_addr));
	xdump(packet, length, "");
	printf("\n");
        fflush(stdout);
    } else if (log_addresses){
	int count;
	
	for(count = 0; count < ADDRCACHE; count++){
	    if (addrCache[count].status > UNUSED /* used or old */
		&& from.sin_addr.s_addr == addrCache[count].addr)
		break;
	}
	if (count >= ADDRCACHE){
	    printf("%s Recieved SNMP packet(s) from %s\n",
		   sprintf_stamp(NULL), inet_ntoa(from.sin_addr));
	    for(count = 0; count < ADDRCACHE; count++){
		if (addrCache[count].status == UNUSED){
		    addrCache[count].addr = from.sin_addr.s_addr;
		    addrCache[count].status = USED;
		    break;
		}
	    }
	} else {
	    addrCache[count].status = USED;
	}
    }
    out_length = 1500;
    if (snmp_agent_parse(packet, length, outpacket, &out_length,
			 from.sin_addr.s_addr)){
	if (snmp_dump_packet){
	    printf("\nsent %d bytes to %s:\n", out_length,
		   inet_ntoa(from.sin_addr));
	    xdump(outpacket, out_length, "");
	    printf("\n");
            fflush(stdout);
	}
#ifdef USING_SNMP_MODULE       
	snmp_outpkts++;
#endif
	if (sendto(sd, (char *)outpacket, out_length, 0,
		   (struct sockaddr *)&from, sizeof(from)) < 0){
	    perror("sendto");
	    return 0;
	}

    }
    return 1;
}

/* deals with replies from remote alarm variables, and from inform pdus */
int
snmp_input(op, session, reqid, pdu, magic)
    int op;
    struct snmp_session *session;
    int reqid;
    struct snmp_pdu *pdu;
    void *magic;
{
    struct get_req_state *state = (struct get_req_state *)magic;
    
    if (op == RECEIVED_MESSAGE) {
	if (pdu->command == SNMP_MSG_GET) {
	    if (state->type == EVENT_GET_REQ) {
		/* this is just the ack to our inform pdu */
		return 1;
	    }
#ifdef USING_ALARM_MODULE
	    return alarmGetResponse(pdu, state, op, session);
#endif
	}
    }
    else if (op == TIMED_OUT) {
	if (state->type == ALARM_GET_REQ) {
#ifdef USING_ALARM_MODULE
	    return alarmGetResponse(pdu, state, op, session);
#endif
	}
    }
    return 1;
}
    
void snmpd_parse_config_authtrap(word, cptr)
  char *word;
  char *cptr;
{
  int i;
  
  i = atoi(cptr);
  if (i < 1 || i > 2)
    config_perror("authtrapenable must be 1 or 2");
  else
    snmp_enableauthentraps = i;
}

void snmpd_parse_config_trapsink(word, cptr)
  char *word;
  char *cptr;
{
  char tmpbuf[1024];
  
  if (create_v1_trap_session(cptr, snmp_trapcommunity) == 0) {
    sprintf(tmpbuf,"cannot create trapsink: %s", cptr);
    config_perror(tmpbuf);
  }
}

void snmpd_parse_config_trapcommunity(word,cptr)
  char *word;
  char *cptr;
{
  snmp_trapcommunity = malloc (strlen(cptr));
  copy_word(cptr, snmp_trapcommunity);
}
