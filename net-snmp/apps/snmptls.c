/*
 * Note: this file originally auto-generated by mib2c using
 *  $
 */

#include <net-snmp/net-snmp-config.h>
#undef NETSNMP_USE_ASSERT
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <ctype.h>

#include <openssl/x509.h>
#include <net-snmp/library/cert_util.h>

#include "tlstm-mib.h"
#include "tlstm-mib/snmpTlstmAddrTable/snmpTlstmAddrTable.h"
#include "tlstm-mib/snmpTlstmParamsTable/snmpTlstmParamsTable.h"
#include "tlstm-mib/snmpTlstmCertToTSNTable/snmpTlstmCertToTSNTable.h"



/*
#define COL_SNMPTLSTMCERTTOTSN_ID                       1
#define COL_SNMPTLSTMCERTTOTSN_FINGERPRINT              2
#define COL_SNMPTLSTMCERTTOTSN_MAPTYPE                  3
#define COL_SNMPTLSTMCERTTOTSN_DATA                     4
#define COL_SNMPTLSTMCERTTOTSN_STORAGETYPE              5
#define COL_SNMPTLSTMCERTTOTSN_ROWSTATUS                6
*/
const oid certNum[] = { SNMP_TLS_TM_CERT_COUNT };
const oid certChg[] = { SNMP_TLS_TM_CERT_CHANGED };
const oid certTbl[] = { SNMP_TLS_TM_CERT_TABLE };

/*
#define COLUMN_SNMPTLSTMPARAMSCLIENTFINGERPRINT    1
#define COLUMN_SNMPTLSTMPARAMSSTORAGETYPE    2
#define COLUMN_SNMPTLSTMPARAMSROWSTATUS    3
*/

const oid paramsNum[] = { SNMP_TLS_TM_PARAMS_COUNT };
const oid paramsChg[] = { SNMP_TLS_TM_PARAMS_CHANGED };
const oid paramsTbl[] = { SNMP_TLS_TM_PARAMS_TABLE };

const oid addrNum[] = { SNMP_TLS_TM_ADDR_COUNT };
const oid addrChg[] = { SNMP_TLS_TM_ADDR_CHANGED };
const oid addrTbl[] = { SNMP_TLS_TM_ADDR_TABLE };


const oid tlstmCertSpecified[]     = { SNMP_TLS_TM_BASE, 1, 1, 1 };
const oid tlstmCertSANRFC822Name[] = { SNMP_TLS_TM_BASE, 1, 1, 2 };
const oid tlstmCertSANDNSName[]    = { SNMP_TLS_TM_BASE, 1, 1, 3 };
const oid tlstmCertSANIpAddress[]  = { SNMP_TLS_TM_BASE, 1, 1, 4 };
const oid tlstmCertSANAny[]        = { SNMP_TLS_TM_BASE, 1, 1, 5 };
const oid tlstmCertCommonName[]    = { SNMP_TLS_TM_BASE, 1, 1, 6 };

const oid *certMapTypes[TSNM_tlstmCert_MAX + 1] = {
    0, tlstmCertSpecified, tlstmCertSANRFC822Name, tlstmCertSANDNSName,
    tlstmCertSANIpAddress, tlstmCertSANAny, tlstmCertCommonName };

/** **************************************************************************
 *
 * cert rows
 *
 */
netsnmp_variable_list *
cert_row_create(u_int32_t priority, int hash_type, const char *fp,
                const oid *map_type, int map_type_len, const u_char *data,
                int data_len, uint32_t st, int *row_status_index)
{
    oid                    name[] = { SNMP_TLS_TM_CERT_TABLE, 1, -1, -1 };
    int                    name_len = OID_LENGTH(name), col_pos = name_len - 2;
    int                    rc, rs_index = 4;
    u_char                 bin_fp[SNMP_MAXBUF_SMALL], *bin_fp_ptr = bin_fp;
    u_int                  rs, bin_fp_len;
    netsnmp_variable_list *vl = NULL, *vb;

    netsnmp_require_ptr_LRV( fp, NULL );

    DEBUGMSGT(("cert:create", "creating varbinds for pri %d, fp %s\n", priority,
               fp));

    bin_fp_len = sizeof(bin_fp);
    rc = netsnmp_tls_fingerprint_build(hash_type, fp, &bin_fp_ptr, &bin_fp_len,
                                       0);

    name[name_len-1] = priority;
    name[col_pos] = COL_SNMPTLSTMCERTTOTSN_FINGERPRINT;
    vl = snmp_varlist_add_variable(&vl, name, name_len, ASN_OCTET_STR,
                                   &bin_fp, bin_fp_len);
    netsnmp_require_ptr_LRV(vl, NULL);

    if (map_type_len && map_type) {
        name[col_pos] = COL_SNMPTLSTMCERTTOTSN_MAPTYPE;
        vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_OBJECT_ID,
                                       map_type, map_type_len * sizeof(oid));
        if (NULL == vb) {
            snmp_free_varbind(vl);
            return NULL;
        }
    }
    else
        --rs_index;

    if (data) {
        name[col_pos] = COL_SNMPTLSTMCERTTOTSN_DATA;
        vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_OCTET_STR,
                                       data, data_len);
        if (NULL == vb) {
            snmp_free_varbind(vl);
            return NULL;
        }
    }
    else
        --rs_index;

    if (st) {
        name[col_pos] = COL_SNMPTLSTMCERTTOTSN_STORAGETYPE;
        vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                       &st, sizeof(st));
        if (NULL == vb) {
            snmp_free_varbind(vl);
            return NULL;
        }
    }
    else
        --rs_index;

    name[col_pos] = COL_SNMPTLSTMCERTTOTSN_ROWSTATUS;
    rs = RS_CREATEANDGO;
    vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                   &rs, sizeof(rs));
    if (NULL == vb) {
        snmp_free_varbind(vl);
        return NULL;
    }

    if (row_status_index)
        *row_status_index = rs_index;

    return vl;
}

/** **************************************************************************
 *
 * param rows
 *
 */
netsnmp_variable_list *
params_row_create(const char *param_name, int hash_type, const char *fp,
                  uint32_t st, int *row_status_index)
{
    oid                    name[MAX_OID_LEN];
    int                    name_len, col_pos, rc, rs_index = 2;
    u_char                 bin_fp[SNMP_MAXBUF_SMALL], *bin_fp_ptr = bin_fp;
    u_int                  rs, bin_fp_len;
    netsnmp_variable_list *vl = NULL, *vb;

    netsnmp_require_ptr_LRV( param_name, NULL );
    netsnmp_require_ptr_LRV( fp, NULL );

    DEBUGMSGT(("params:create", "creating varbinds for %s params, fp %s\n",
               param_name, fp));

    /*
     * build base name
     */
    name_len = OID_LENGTH(paramsTbl);
    memcpy(name, paramsTbl, sizeof(paramsTbl));
    name[name_len++] = 1; /* entry */
    col_pos = name_len++; /* column */
    while (*param_name)
        name[name_len++] = *param_name++;

    bin_fp_len = sizeof(bin_fp);
    rc = netsnmp_tls_fingerprint_build(hash_type, fp, &bin_fp_ptr, &bin_fp_len,
                                       0);

    name[col_pos] = COLUMN_SNMPTLSTMPARAMSCLIENTFINGERPRINT;
    vl = snmp_varlist_add_variable(&vl, name, name_len, ASN_OCTET_STR,
                                   &bin_fp, bin_fp_len);
    netsnmp_require_ptr_LRV(vl, NULL);

    if (st) {
        name[col_pos] = COLUMN_SNMPTLSTMPARAMSSTORAGETYPE;
        vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                       &st, sizeof(st));
        if (NULL == vb) {
            snmp_free_varbind(vl);
            return NULL;
        }
    }
    else
        --rs_index;

    name[col_pos] = COLUMN_SNMPTLSTMPARAMSROWSTATUS;
    rs = RS_CREATEANDGO;
    vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                   &rs, sizeof(rs));
    if (NULL == vb) {
        snmp_free_varbind(vl);
        return NULL;
    }

    if (row_status_index)
        *row_status_index = rs_index;

    return vl;
}

/** **************************************************************************
 *
 * addr rows
 *
 */
netsnmp_variable_list *
addr_row_create(const char *target_name, int hash_type, const char *fp,
                const char *identity, uint32_t st, int *row_status_index)
{
    oid                    name[MAX_OID_LEN];
    int                    name_len, col_pos, rc, rs_index = 3;
    u_char                 bin_fp[SNMP_MAXBUF_SMALL], *bin_fp_ptr = bin_fp;
    u_int                  rs, bin_fp_len;
    netsnmp_variable_list *vl = NULL, *vb;

    netsnmp_require_ptr_LRV( target_name, NULL );

    DEBUGMSGT(("addr:create", "creating varbinds for %s addr, fp %s, id %s\n",
               target_name, fp, identity));

    /*
     * build base name
     */
    name_len = OID_LENGTH(addrTbl);
    memcpy(name, addrTbl, sizeof(addrTbl));
    name[name_len++] = 1; /* entry */
    col_pos = name_len++; /* column */
    while (*target_name)
        name[name_len++] = *target_name++;

    if (fp) {
        bin_fp_len = sizeof(bin_fp);
        rc = netsnmp_tls_fingerprint_build(hash_type, fp, &bin_fp_ptr,
                                           &bin_fp_len, 0);

        name[col_pos] = COLUMN_SNMPTLSTMADDRSERVERFINGERPRINT;
        vl = snmp_varlist_add_variable(&vl, name, name_len, ASN_OCTET_STR,
                                       &bin_fp, bin_fp_len);
        netsnmp_require_ptr_LRV(vl, NULL);
    }
    else
        --rs_index;

    if (identity) {
        name[col_pos] = COLUMN_SNMPTLSTMADDRSERVERIDENTITY;
        vl = snmp_varlist_add_variable(&vl, name, name_len, ASN_OCTET_STR,
                                       identity, strlen(identity));
        netsnmp_require_ptr_LRV(vl, NULL);
    }
    else
        --rs_index;

    if (st) {
        name[col_pos] = COLUMN_SNMPTLSTMADDRSTORAGETYPE;
        vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                       &st, sizeof(st));
        if (NULL == vb) {
            snmp_free_varbind(vl);
            return NULL;
        }
    }
    else
        --rs_index;

    name[col_pos] = COLUMN_SNMPTLSTMADDRROWSTATUS;
    rs = RS_CREATEANDGO;
    vb = snmp_varlist_add_variable(&vl, name, name_len, ASN_INTEGER,
                                   &rs, sizeof(rs));
    if (NULL == vb) {
        snmp_free_varbind(vl);
        return NULL;
    }

    if (row_status_index)
        *row_status_index = rs_index;

    return vl;
}

/** **************************************************************************
 *
 * application code
 *
 */
static char         *_data = NULL, *_map_type_str = NULL, *_id_str = NULL;
static char         *_storage_type_str = NULL, *_fp_str = NULL;
static int           _storage_type = ST_NONE, _hash_type = NS_HASH_NONE;
static size_t        _data_len;

static void
optProc(int argc, char *const *argv, int opt)
{
    if ('C' != opt)
        return;

    while (*optarg) {
        switch (*optarg++) {
            case 'm':
                if (optind < argc)
                    _map_type_str = argv[optind++];
                else {
                    fprintf(stderr, "Bad -Cm option: no argument given\n");
                    exit(1);
                }
                break;
                
            case 'd':
                if (optind < argc) {
                    _data = argv[optind++];
                    _data_len = strlen(_data);
                }
                else {
                    fprintf(stderr, "Bad -Cd option: no argument given\n");
                    exit(1);
                }
                break;

	    case 's':
                if (optind < argc) {
                    if (isdigit(argv[optind][0]))
                        _storage_type = atoi(argv[optind++]);
                    else
                        _storage_type_str = argv[optind++];
                }
                else {
                    fprintf(stderr, "Bad -Cs option: no argument given\n");
                    exit(1);
                }
		break;
                
	    case 'h':
                if (optind < argc) {
                    if (isdigit(argv[optind][0]))
                        _hash_type = atoi(argv[optind++]);
                }
                else {
                    fprintf(stderr, "Bad -Ch option: no argument given\n");
                    exit(1);
                }
		break;
                
	    case 'f':
                if (optind < argc)
                    _fp_str = argv[optind++];
                else {
                    fprintf(stderr, "Bad -Cf option: no argument given\n");
                    exit(1);
                }
		break;
                
	    case 'i':
                if (optind < argc)
                    _id_str = argv[optind++];
                else {
                    fprintf(stderr, "Bad -Ci option: no argument given\n");
                    exit(1);
                }
		break;
                
            default:
                fprintf(stderr, "Unknown flag passed to -C: %c\n",
                        optarg[-1]);
                exit(1);
        }
    }
}

void
_parse_storage_type(const char *arg)
{
    netsnmp_pdu dummy;
    oid name[] = { SNMP_TLS_TM_CERT_TABLE, 1,
                   COL_SNMPTLSTMCERTTOTSN_STORAGETYPE };
    int name_len = OID_LENGTH(name);

    if (NULL == arg)
        return;

    memset(&dummy, 0x00, sizeof(dummy));
    snmp_add_var(&dummy, name, name_len, 'i', arg);
    if (dummy.variables) {
        _storage_type = *dummy.variables->val.integer;
        snmp_free_varbind(dummy.variables);
    }
    else {
        fprintf(stderr, "unknown storage type %s for -Cs\n", arg);
        exit(1);
    }

    return;
}

void
usage(void)
{
    fprintf(stderr, "USAGE: snmptls [-Cm mapTypeOID] [-Cd data] [-Cs storageType] ");
    snmp_parse_args_usage(stderr);
    fprintf(stderr, "<command> [command options]\n\n");
    snmp_parse_args_descriptions(stderr);
    fprintf(stderr, "  [options]   certToSecName add <priority> <hashType> <fingerprint>\n");
    fprintf(stderr, "\t-Cm\t\tMaptype; [snmpTlstmCertCommonName|snmpTlstmCertSANRFC822Name|snmpTlstmCertSANIpAddress|snmpTlstmCertSANDNSName|snmpTlstmCertSpecified]\n");
    fprintf(stderr, "\t\t\t(default is snmpTlstmCertSpecified)\n");
    fprintf(stderr, "\t-Cd\t\tData; data for snmpTlstmCertSpecified.\n");
    fprintf(stderr, "\t-Cs\t\tstorageType; default is nonVolatile.\n");

    fprintf(stderr, "  [options]   targetParamsFingerprint add <params-name> <hashType> <fingerprint>\n");
    fprintf(stderr, "\t-Cs\t\tstorageType; default is nonVolatile.\n");

    fprintf(stderr, "  [options]   targetAddr add <target-name> <hashType> [<hash_type> <remote-fingerprint>] [server-identity]\n");
    fprintf(stderr, "\t-Cs\t\tstorageType; default is nonVolatile.\n");

    exit(1);
}

int
main(int argc, char **argv)
{
    netsnmp_session        session, *ss;
    netsnmp_variable_list *var_list = NULL;
    int                    arg, rc, rs_idx;
    u_int                  hash_type;
    char                  *fingerprint, *tmp;

#define RKS
#ifdef RKS
    debug_register_tokens("snmp_parse_args");
    snmp_set_do_debugging(1);
#endif

    /*
     * get the common command line arguments 
     */
    switch (arg = snmp_parse_args(argc, argv, &session, "C:", optProc)) {
    case NETSNMP_PARSE_ARGS_ERROR:
        exit(1);
    case NETSNMP_PARSE_ARGS_SUCCESS_EXIT:
        exit(0);
    case NETSNMP_PARSE_ARGS_ERROR_USAGE:
        usage();
    default:
        break;
    }

    /*
     * Open an SNMP session.
     */
    SOCK_STARTUP;
    ss = snmp_open(&session);
    if (ss == NULL) {
        /*
         * diagnose snmp_open errors with the input netsnmp_session pointer 
         */
        snmp_sess_perror("snmptls", &session);
        SOCK_CLEANUP;
        exit(1);
    }

    if (strcmp(argv[arg], "certToSecName") == 0) {

        oid           map_type[MAX_OID_LEN];
        u_int         pri, map_type_len;

        if (strcmp(argv[++arg], "add") != 0) {
            fprintf(stderr, "only add is supported at this time\n");
            exit(1);
        }

        pri = atoi(argv[++arg]);
        tmp = argv[++arg];
        hash_type = atoi(tmp);
        fingerprint = argv[++arg];

        DEBUGMSGT(("snmptls",
                   "create pri %d, hash type %d, fp %s",
                   pri, hash_type, fingerprint));
        if (_map_type_str) {
            map_type_len = MAX_OID_LEN;
            if (snmp_parse_oid(_map_type_str, map_type, &map_type_len) 
                == NULL) {
                snmp_perror(_map_type_str);
                exit(1);
            }
            DEBUGMSG(("snmptls", ", map type "));
            DEBUGMSGOID(("snmptls", map_type, map_type_len));
        }
        if (_data)
            DEBUGMSG(("snmptls", ", data %s", _data));

        _parse_storage_type(_storage_type_str);

        DEBUGMSG(("snmptls", "\n"));
        var_list = cert_row_create(pri, hash_type, fingerprint, map_type,
                                   map_type_len, (u_char*)_data, _data_len,
                                   _storage_type, &rs_idx);
    }
    else if (strcmp(argv[arg], "targetParamsFingerprint") == 0) {

        char * params_name;

        if (strcmp(argv[++arg], "add") != 0) {
            fprintf(stderr, "only add is supported at this time\n");
            exit(1);
        }

        params_name = argv[++arg];
        hash_type = atoi(argv[++arg]);
        fingerprint = argv[++arg];
        
        _parse_storage_type(_storage_type_str);

        DEBUGMSGT(("snmptls",
                   "create %s param fp, hash type %d, fp %s\n",
                   params_name, hash_type, fingerprint));

        var_list = params_row_create(params_name, hash_type, fingerprint,
                                     _storage_type, &rs_idx);
    }

    else if (strcmp(argv[arg], "targetAddr") == 0) {

        char * addr_name;

        if (strcmp(argv[++arg], "add") != 0) {
            fprintf(stderr, "only add is supported at this time\n");
            exit(1);
        }

        addr_name = argv[++arg];
        
        _parse_storage_type(_storage_type_str);

        DEBUGMSGT(("snmptls",
                   "create %s addr fp, hash type %d, fp %s, id %s\n",
                   addr_name, _hash_type, _fp_str, _id_str));

        var_list = addr_row_create(addr_name, _hash_type, _fp_str, _id_str,
                                     _storage_type, &rs_idx);
    }

    if (! var_list) {
        fprintf(stderr, "no command specified\n");
        usage();
    }

    rc = netsnmp_row_create(ss, var_list, rs_idx);

    SOCK_CLEANUP;
    return 0;
}
