/* dnsmasq is Copyright (c) 2000-2025 Simon Kelley

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 dated June, 1991, or
   (at your option) version 3 dated 29 June, 2007.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
     
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* define this to get facilitynames */
#define SYSLOG_NAMES
#include "dnsmasq.h"
#include <setjmp.h>

static volatile int mem_recover = 0;
static jmp_buf mem_jmp;
static int one_file(char *file, int hard_opt);

/* Solaris headers don't have facility names. */
#ifdef HAVE_SOLARIS_NETWORK
static const struct {
  char *c_name;
  unsigned int c_val;
}  facilitynames[] = {
  { "kern",   LOG_KERN },
  { "user",   LOG_USER },
  { "mail",   LOG_MAIL },
  { "daemon", LOG_DAEMON },
  { "auth",   LOG_AUTH },
  { "syslog", LOG_SYSLOG },
  { "lpr",    LOG_LPR },
  { "news",   LOG_NEWS },
  { "uucp",   LOG_UUCP },
  { "audit",  LOG_AUDIT },
  { "cron",   LOG_CRON },
  { "local0", LOG_LOCAL0 },
  { "local1", LOG_LOCAL1 },
  { "local2", LOG_LOCAL2 },
  { "local3", LOG_LOCAL3 },
  { "local4", LOG_LOCAL4 },
  { "local5", LOG_LOCAL5 },
  { "local6", LOG_LOCAL6 },
  { "local7", LOG_LOCAL7 },
  { NULL, 0 }
};
#endif

#ifndef HAVE_GETOPT_LONG
struct myoption {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};
#endif

#define OPTSTRING "951yZDNLERKzowefnbvhdkqr:m:p:c:l:s:i:t:u:g:a:x:S:C:A:T:H:Q:I:B:F:G:O:M:X:V:U:j:P:J:W:Y:2:4:6:7:8:0:3:"

/* options which don't have a one-char version */
#define LOPT_RELOAD        256
#define LOPT_NO_NAMES      257
#define LOPT_TFTP          258
#define LOPT_SECURE        259
#define LOPT_PREFIX        260
#define LOPT_PTR           261
#define LOPT_BRIDGE        262
#define LOPT_TFTP_MAX      263
#define LOPT_FORCE         264
#define LOPT_NOBLOCK       265
#define LOPT_LOG_OPTS      266
#define LOPT_MAX_LOGS      267
#define LOPT_CIRCUIT       268
#define LOPT_REMOTE        269
#define LOPT_SUBSCR        270
#define LOPT_INTNAME       271
#define LOPT_BANK          272
#define LOPT_DHCP_HOST     273
#define LOPT_APREF         274
#define LOPT_OVERRIDE      275
#define LOPT_TFTPPORTS     276
#define LOPT_REBIND        277
#define LOPT_NOLAST        278
#define LOPT_OPTS          279
#define LOPT_DHCP_OPTS     280
#define LOPT_MATCH         281
#define LOPT_BROADCAST     282
#define LOPT_NEGTTL        283
#define LOPT_ALTPORT       284
#define LOPT_SCRIPTUSR     285
#define LOPT_LOCAL         286
#define LOPT_NAPTR         287
#define LOPT_MINPORT       288
#define LOPT_DHCP_FQDN     289
#define LOPT_CNAME         290
#define LOPT_PXE_PROMT     291
#define LOPT_PXE_SERV      292
#define LOPT_TEST          293
#define LOPT_TAG_IF        294
#define LOPT_PROXY         295
#define LOPT_GEN_NAMES     296
#define LOPT_MAXTTL        297
#define LOPT_NO_REBIND     298
#define LOPT_LOC_REBND     299
#define LOPT_ADD_MAC       300
#define LOPT_DNSSEC        301
#define LOPT_INCR_ADDR     302
#define LOPT_CONNTRACK     303
#define LOPT_FQDN          304
#define LOPT_LUASCRIPT     305
#define LOPT_RA            306
#define LOPT_DUID          307
#define LOPT_HOST_REC      308
#define LOPT_TFTP_LC       309
#define LOPT_RR            310
#define LOPT_CLVERBIND     311
#define LOPT_MAXCTTL       312
#define LOPT_AUTHZONE      313
#define LOPT_AUTHSERV      314
#define LOPT_AUTHTTL       315
#define LOPT_AUTHSOA       316
#define LOPT_AUTHSFS       317
#define LOPT_AUTHPEER      318
#define LOPT_IPSET         319
#define LOPT_SYNTH         320
#define LOPT_RELAY         323
#define LOPT_RA_PARAM      324
#define LOPT_ADD_SBNET     325
#define LOPT_QUIET_DHCP    326
#define LOPT_QUIET_DHCP6   327
#define LOPT_QUIET_RA      328
#define LOPT_SEC_VALID     329
#define LOPT_TRUST_ANCHOR  330
#define LOPT_DNSSEC_DEBUG  331
#define LOPT_REV_SERV      332
#define LOPT_SERVERS_FILE  333
#define LOPT_DNSSEC_CHECK  334
#define LOPT_LOCAL_SERVICE 335
#define LOPT_DNSSEC_TIME   336
#define LOPT_LOOP_DETECT   337
#define LOPT_IGNORE_ADDR   338
#define LOPT_MINCTTL       339
#define LOPT_DHCP_INOTIFY  340
#define LOPT_DHOPT_INOTIFY 341
#define LOPT_HOST_INOTIFY  342
#define LOPT_DNSSEC_STAMP  343
#define LOPT_TFTP_NO_FAIL  344
#define LOPT_MAXPORT       345
#define LOPT_CPE_ID        346
#define LOPT_SCRIPT_ARP    347
#define LOPT_DHCPTTL       348
#define LOPT_TFTP_MTU      349
#define LOPT_REPLY_DELAY   350
#define LOPT_RAPID_COMMIT  351
#define LOPT_DUMPFILE      352
#define LOPT_DUMPMASK      353
#define LOPT_UBUS          354
#define LOPT_NAME_MATCH    355
#define LOPT_CAA           356
#define LOPT_SHARED_NET    357
#define LOPT_IGNORE_CLID   358
#define LOPT_SINGLE_PORT   359
#define LOPT_SCRIPT_TIME   360
#define LOPT_PXE_VENDOR    361
#define LOPT_DYNHOST       362
#define LOPT_LOG_DEBUG     363
#define LOPT_UMBRELLA	   364
#define LOPT_CMARK_ALST_EN 365
#define LOPT_CMARK_ALST    366
#define LOPT_QUIET_TFTP    367
#define LOPT_NFTSET        368
#define LOPT_FILTER_A      369
#define LOPT_FILTER_AAAA   370
#define LOPT_STRIP_SBNET   371
#define LOPT_STRIP_MAC     372
#define LOPT_CONF_OPT      373
#define LOPT_CONF_SCRIPT   374
#define LOPT_RANDPORT_LIM  375
#define LOPT_FAST_RETRY    376
#define LOPT_STALE_CACHE   377
#define LOPT_NORR          378
#define LOPT_NO_IDENT      379
#define LOPT_CACHE_RR      380
#define LOPT_FILTER_RR     381
#define LOPT_NO_DHCP6      382
#define LOPT_NO_DHCP4      383
#define LOPT_MAX_PROCS     384
#define LOPT_DNSSEC_LIMITS 385
#define LOPT_PXE_OPT       386
#define LOPT_NO_ENCODE     387
#define LOPT_DO_ENCODE     388
#define LOPT_LEASEQUERY    389
#define LOPT_SPLIT_RELAY   390

#ifdef HAVE_GETOPT_LONG
static const struct option opts[] =  
#else
static const struct myoption opts[] = 
#endif
  { 
    { "version", 0, 0, 'v' },
    { "no-hosts", 0, 0, 'h' },
    { "no-poll", 0, 0, 'n' },
    { "help", 0, 0, 'w' },
    { "no-daemon", 0, 0, 'd' },
    { "log-queries", 2, 0, 'q' },
    { "user", 2, 0, 'u' },
    { "group", 2, 0, 'g' },
    { "resolv-file", 2, 0, 'r' },
    { "servers-file", 1, 0, LOPT_SERVERS_FILE },
    { "mx-host", 1, 0, 'm' },
    { "mx-target", 1, 0, 't' },
    { "cache-size", 2, 0, 'c' },
    { "port", 1, 0, 'p' },
    { "dhcp-leasefile", 2, 0, 'l' },
    { "dhcp-lease", 1, 0, 'l' },
    { "dhcp-host", 1, 0, 'G' },
    { "dhcp-range", 1, 0, 'F' },
    { "dhcp-option", 1, 0, 'O' },
    { "dhcp-boot", 1, 0, 'M' },
    { "domain", 1, 0, 's' },
    { "domain-suffix", 1, 0, 's' },
    { "interface", 1, 0, 'i' },
    { "listen-address", 1, 0, 'a' },
    { "local-service", 2, 0, LOPT_LOCAL_SERVICE },
    { "bogus-priv", 0, 0, 'b' },
    { "bogus-nxdomain", 1, 0, 'B' },
    { "ignore-address", 1, 0, LOPT_IGNORE_ADDR },
    { "selfmx", 0, 0, 'e' },
    { "filterwin2k", 0, 0, 'f' },
    { "filter-A", 0, 0, LOPT_FILTER_A },
    { "filter-AAAA", 0, 0, LOPT_FILTER_AAAA },
    { "filter-rr", 1, 0, LOPT_FILTER_RR },
    { "pid-file", 2, 0, 'x' },
    { "strict-order", 0, 0, 'o' },
    { "server", 1, 0, 'S' },
    { "rev-server", 1, 0, LOPT_REV_SERV },
    { "local", 1, 0, LOPT_LOCAL },
    { "address", 1, 0, 'A' },
    { "conf-file", 2, 0, 'C' },
    { "conf-script", 1, 0, LOPT_CONF_SCRIPT },
    { "no-resolv", 0, 0, 'R' },
    { "expand-hosts", 0, 0, 'E' },
    { "localmx", 0, 0, 'L' },
    { "local-ttl", 1, 0, 'T' },
    { "no-negcache", 0, 0, 'N' },
    { "no-round-robin", 0, 0, LOPT_NORR },
    { "no-0x20-encode", 0, 0, LOPT_NO_ENCODE },
    { "do-0x20-encode", 0, 0, LOPT_DO_ENCODE },
    { "cache-rr", 1, 0, LOPT_CACHE_RR },
    { "addn-hosts", 1, 0, 'H' },
    { "hostsdir", 1, 0, LOPT_HOST_INOTIFY },
    { "query-port", 1, 0, 'Q' },
    { "except-interface", 1, 0, 'I' },
    { "no-dhcp-interface", 1, 0, '2' },
    { "no-dhcpv4-interface", 1, 0, LOPT_NO_DHCP4 },
    { "no-dhcpv6-interface", 1, 0, LOPT_NO_DHCP6 },
    { "domain-needed", 0, 0, 'D' },
    { "dhcp-lease-max", 1, 0, 'X' },
    { "bind-interfaces", 0, 0, 'z' },
    { "read-ethers", 0, 0, 'Z' },
    { "alias", 1, 0, 'V' },
    { "dhcp-vendorclass", 1, 0, 'U' },
    { "dhcp-userclass", 1, 0, 'j' },
    { "dhcp-ignore", 1, 0, 'J' },
    { "edns-packet-max", 1, 0, 'P' },
    { "keep-in-foreground", 0, 0, 'k' },
    { "dhcp-authoritative", 0, 0, 'K' },
    { "srv-host", 1, 0, 'W' },
    { "localise-queries", 0, 0, 'y' },
    { "txt-record", 1, 0, 'Y' },
    { "caa-record", 1, 0 , LOPT_CAA },
    { "dns-rr", 1, 0, LOPT_RR },
    { "enable-dbus", 2, 0, '1' },
    { "enable-ubus", 2, 0, LOPT_UBUS },
    { "bootp-dynamic", 2, 0, '3' },
    { "dhcp-mac", 1, 0, '4' },
    { "no-ping", 0, 0, '5' },
    { "dhcp-script", 1, 0, '6' },
    { "conf-dir", 1, 0, '7' },
    { "log-facility", 1, 0 ,'8' },
    { "leasefile-ro", 0, 0, '9' },
    { "script-on-renewal", 0, 0, LOPT_SCRIPT_TIME},
    { "dns-forward-max", 1, 0, '0' },
    { "clear-on-reload", 0, 0, LOPT_RELOAD },
    { "dhcp-ignore-names", 2, 0, LOPT_NO_NAMES },
    { "enable-tftp", 2, 0, LOPT_TFTP },
    { "tftp-secure", 0, 0, LOPT_SECURE },
    { "tftp-no-fail", 0, 0, LOPT_TFTP_NO_FAIL },
    { "tftp-unique-root", 2, 0, LOPT_APREF },
    { "tftp-root", 1, 0, LOPT_PREFIX },
    { "tftp-max", 1, 0, LOPT_TFTP_MAX },
    { "tftp-mtu", 1, 0, LOPT_TFTP_MTU },
    { "tftp-lowercase", 0, 0, LOPT_TFTP_LC },
    { "tftp-single-port", 0, 0, LOPT_SINGLE_PORT },
    { "ptr-record", 1, 0, LOPT_PTR },
    { "naptr-record", 1, 0, LOPT_NAPTR },
    { "bridge-interface", 1, 0 , LOPT_BRIDGE },
    { "shared-network", 1, 0, LOPT_SHARED_NET },
    { "dhcp-option-force", 1, 0, LOPT_FORCE },
    { "dhcp-option-pxe", 1, 0, LOPT_PXE_OPT },
    { "tftp-no-blocksize", 0, 0, LOPT_NOBLOCK },
    { "log-dhcp", 0, 0, LOPT_LOG_OPTS },
    { "log-async", 2, 0, LOPT_MAX_LOGS },
    { "dhcp-circuitid", 1, 0, LOPT_CIRCUIT },
    { "dhcp-remoteid", 1, 0, LOPT_REMOTE },
    { "dhcp-subscrid", 1, 0, LOPT_SUBSCR },
    { "dhcp-pxe-vendor", 1, 0, LOPT_PXE_VENDOR },
    { "interface-name", 1, 0, LOPT_INTNAME },
    { "dhcp-hostsfile", 1, 0, LOPT_DHCP_HOST },
    { "dhcp-optsfile", 1, 0, LOPT_DHCP_OPTS },
    { "dhcp-hostsdir", 1, 0, LOPT_DHCP_INOTIFY },
    { "dhcp-optsdir", 1, 0, LOPT_DHOPT_INOTIFY },
    { "dhcp-no-override", 0, 0, LOPT_OVERRIDE },
    { "tftp-port-range", 1, 0, LOPT_TFTPPORTS },
    { "stop-dns-rebind", 0, 0, LOPT_REBIND },
    { "rebind-domain-ok", 1, 0, LOPT_NO_REBIND },
    { "all-servers", 0, 0, LOPT_NOLAST }, 
    { "dhcp-match", 1, 0, LOPT_MATCH },
    { "dhcp-name-match", 1, 0, LOPT_NAME_MATCH },
    { "dhcp-broadcast", 2, 0, LOPT_BROADCAST },
    { "neg-ttl", 1, 0, LOPT_NEGTTL },
    { "max-ttl", 1, 0, LOPT_MAXTTL },
    { "min-cache-ttl", 1, 0, LOPT_MINCTTL },
    { "max-cache-ttl", 1, 0, LOPT_MAXCTTL },
    { "dhcp-alternate-port", 2, 0, LOPT_ALTPORT },
    { "dhcp-scriptuser", 1, 0, LOPT_SCRIPTUSR },
    { "min-port", 1, 0, LOPT_MINPORT },
    { "max-port", 1, 0, LOPT_MAXPORT },
    { "dhcp-fqdn", 0, 0, LOPT_DHCP_FQDN },
    { "cname", 1, 0, LOPT_CNAME },
    { "pxe-prompt", 1, 0, LOPT_PXE_PROMT },
    { "pxe-service", 1, 0, LOPT_PXE_SERV },
    { "test", 0, 0, LOPT_TEST },
    { "tag-if", 1, 0, LOPT_TAG_IF },
    { "dhcp-proxy", 2, 0, LOPT_PROXY },
    { "dhcp-generate-names", 2, 0, LOPT_GEN_NAMES },
    { "rebind-localhost-ok", 0, 0,  LOPT_LOC_REBND },
    { "add-mac", 2, 0, LOPT_ADD_MAC },
    { "strip-mac", 0, 0, LOPT_STRIP_MAC },
    { "add-subnet", 2, 0, LOPT_ADD_SBNET },
    { "strip-subnet", 0, 0, LOPT_STRIP_SBNET },
    { "add-cpe-id", 1, 0 , LOPT_CPE_ID },
    { "proxy-dnssec", 0, 0, LOPT_DNSSEC },
    { "dhcp-sequential-ip", 0, 0,  LOPT_INCR_ADDR },
    { "conntrack", 0, 0, LOPT_CONNTRACK },
    { "dhcp-client-update", 0, 0, LOPT_FQDN },
    { "dhcp-luascript", 1, 0, LOPT_LUASCRIPT },
    { "enable-ra", 0, 0, LOPT_RA },
    { "dhcp-duid", 1, 0, LOPT_DUID },
    { "host-record", 1, 0, LOPT_HOST_REC },
    { "bind-dynamic", 0, 0, LOPT_CLVERBIND },
    { "auth-zone", 1, 0, LOPT_AUTHZONE },
    { "auth-server", 1, 0, LOPT_AUTHSERV },
    { "auth-ttl", 1, 0, LOPT_AUTHTTL },
    { "auth-soa", 1, 0, LOPT_AUTHSOA },
    { "auth-sec-servers", 1, 0, LOPT_AUTHSFS },
    { "auth-peer", 1, 0, LOPT_AUTHPEER }, 
    { "ipset", 1, 0, LOPT_IPSET },
    { "nftset", 1, 0, LOPT_NFTSET },
    { "connmark-allowlist-enable", 2, 0, LOPT_CMARK_ALST_EN },
    { "connmark-allowlist", 1, 0, LOPT_CMARK_ALST },
    { "synth-domain", 1, 0, LOPT_SYNTH },
    { "dnssec", 0, 0, LOPT_SEC_VALID },
    { "trust-anchor", 1, 0, LOPT_TRUST_ANCHOR },
    { "dnssec-debug", 0, 0, LOPT_DNSSEC_DEBUG },
    { "dnssec-check-unsigned", 2, 0, LOPT_DNSSEC_CHECK },
    { "dnssec-no-timecheck", 0, 0, LOPT_DNSSEC_TIME },
    { "dnssec-timestamp", 1, 0, LOPT_DNSSEC_STAMP },
    { "dnssec-limits", 1, 0, LOPT_DNSSEC_LIMITS },
    { "dhcp-relay", 1, 0, LOPT_RELAY },
    { "dhcp-split-relay", 1, 0, LOPT_SPLIT_RELAY },
    { "ra-param", 1, 0, LOPT_RA_PARAM },
    { "quiet-dhcp", 0, 0, LOPT_QUIET_DHCP },
    { "quiet-dhcp6", 0, 0, LOPT_QUIET_DHCP6 },
    { "quiet-ra", 0, 0, LOPT_QUIET_RA },
    { "dns-loop-detect", 0, 0, LOPT_LOOP_DETECT },
    { "script-arp", 0, 0, LOPT_SCRIPT_ARP },
    { "dhcp-ttl", 1, 0 , LOPT_DHCPTTL },
    { "dhcp-reply-delay", 1, 0, LOPT_REPLY_DELAY },
    { "dhcp-rapid-commit", 0, 0, LOPT_RAPID_COMMIT },
    { "dumpfile", 1, 0, LOPT_DUMPFILE },
    { "dumpmask", 1, 0, LOPT_DUMPMASK },
    { "dhcp-ignore-clid", 0, 0,  LOPT_IGNORE_CLID },
    { "dynamic-host", 1, 0, LOPT_DYNHOST },
    { "log-debug", 0, 0, LOPT_LOG_DEBUG },
    { "umbrella", 2, 0, LOPT_UMBRELLA },
    { "quiet-tftp", 0, 0, LOPT_QUIET_TFTP },
    { "port-limit", 1, 0, LOPT_RANDPORT_LIM },
    { "fast-dns-retry", 2, 0, LOPT_FAST_RETRY },
    { "use-stale-cache", 2, 0 , LOPT_STALE_CACHE },
    { "no-ident", 0, 0, LOPT_NO_IDENT },
    { "max-tcp-connections", 1, 0, LOPT_MAX_PROCS },
    { "leasequery", 2, 0, LOPT_LEASEQUERY },
    { NULL, 0, 0, 0 }
  };


#define ARG_DUP       OPT_LAST
#define ARG_ONE       OPT_LAST + 1
#define ARG_USED_CL   OPT_LAST + 2
#define ARG_USED_FILE OPT_LAST + 3

static struct {
  int opt;
  unsigned int rept;
  char * const flagdesc;
  char * const desc;
  char * const arg;
} usage[] = {
  { 'a', ARG_DUP, "<ipaddr>",  gettext_noop("Specify local address(es) to listen on."), NULL },
  { 'A', ARG_DUP, "/<domain>/<ipaddr>", gettext_noop("Return ipaddr for all hosts in specified domains."), NULL },
  { 'b', OPT_BOGUSPRIV, NULL, gettext_noop("Fake reverse lookups for RFC1918 private address ranges."), NULL },
  { 'B', ARG_DUP, "<ipaddr>", gettext_noop("Treat ipaddr as NXDOMAIN (defeats Verisign wildcard)."), NULL }, 
  { 'c', ARG_ONE, "<integer>", gettext_noop("Specify the size of the cache in entries (defaults to %s)."), "$" },
  { 'C', ARG_DUP, "<path>", gettext_noop("Specify configuration file (defaults to %s)."), CONFFILE },
  { 'd', OPT_DEBUG, NULL, gettext_noop("Do NOT fork into the background: run in debug mode."), NULL },
  { 'D', OPT_NODOTS_LOCAL, NULL, gettext_noop("Do NOT forward queries with no domain part."), NULL }, 
  { 'e', OPT_SELFMX, NULL, gettext_noop("Return self-pointing MX records for local hosts."), NULL },
  { 'E', OPT_EXPAND, NULL, gettext_noop("Expand simple names in /etc/hosts with domain-suffix."), NULL },
  { 'f', OPT_FILTER, NULL, gettext_noop("Don't forward spurious DNS requests from Windows hosts."), NULL },
  { LOPT_FILTER_A, ARG_DUP, NULL, gettext_noop("Don't include IPv4 addresses in DNS answers."), NULL },
  { LOPT_FILTER_AAAA, ARG_DUP, NULL, gettext_noop("Don't include IPv6 addresses in DNS answers."), NULL },
  { LOPT_FILTER_RR, ARG_DUP, "<RR-type>", gettext_noop("Don't include resource records of the given type in DNS answers."), NULL },
  { 'F', ARG_DUP, "<ipaddr>,...", gettext_noop("Enable DHCP in the range given with lease duration."), NULL },
  { 'g', ARG_ONE, "<groupname>", gettext_noop("Change to this group after startup (defaults to %s)."), CHGRP },
  { 'G', ARG_DUP, "<hostspec>", gettext_noop("Set address or hostname for a specified machine."), NULL },
  { LOPT_DHCP_HOST, ARG_DUP, "<path>", gettext_noop("Read DHCP host specs from file."), NULL },
  { LOPT_DHCP_OPTS, ARG_DUP, "<path>", gettext_noop("Read DHCP option specs from file."), NULL },
  { LOPT_DHCP_INOTIFY, ARG_DUP, "<path>", gettext_noop("Read DHCP host specs from a directory."), NULL }, 
  { LOPT_DHOPT_INOTIFY, ARG_DUP, "<path>", gettext_noop("Read DHCP options from a directory."), NULL }, 
  { LOPT_TAG_IF, ARG_DUP, "tag-expression", gettext_noop("Evaluate conditional tag expression."), NULL },
  { 'h', OPT_NO_HOSTS, NULL, gettext_noop("Do NOT load %s file."), HOSTSFILE },
  { 'H', ARG_DUP, "<path>", gettext_noop("Specify a hosts file to be read in addition to %s."), HOSTSFILE },
  { LOPT_HOST_INOTIFY, ARG_DUP, "<path>", gettext_noop("Read hosts files from a directory."), NULL },
  { 'i', ARG_DUP, "<interface>", gettext_noop("Specify interface(s) to listen on."), NULL },
  { 'I', ARG_DUP, "<interface>", gettext_noop("Specify interface(s) NOT to listen on.") , NULL },
  { 'j', ARG_DUP, "set:<tag>,<class>", gettext_noop("Map DHCP user class to tag."), NULL },
  { LOPT_CIRCUIT, ARG_DUP, "set:<tag>,<circuit>", gettext_noop("Map RFC3046 circuit-id to tag."), NULL },
  { LOPT_REMOTE, ARG_DUP, "set:<tag>,<remote>", gettext_noop("Map RFC3046 remote-id to tag."), NULL },
  { LOPT_SUBSCR, ARG_DUP, "set:<tag>,<remote>", gettext_noop("Map RFC3993 subscriber-id to tag."), NULL },
  { LOPT_PXE_VENDOR, ARG_DUP, "<vendor>[,...]", gettext_noop("Specify vendor class to match for PXE requests."), NULL },
  { 'J', ARG_DUP, "tag:<tag>...", gettext_noop("Don't do DHCP for hosts with tag set."), NULL },
  { LOPT_BROADCAST, ARG_DUP, "[=tag:<tag>...]", gettext_noop("Force broadcast replies for hosts with tag set."), NULL }, 
  { 'k', OPT_NO_FORK, NULL, gettext_noop("Do NOT fork into the background, do NOT run in debug mode."), NULL },
  { 'K', OPT_AUTHORITATIVE, NULL, gettext_noop("Assume we are the only DHCP server on the local network."), NULL },
  { 'l', ARG_ONE, "<path>", gettext_noop("Specify where to store DHCP leases (defaults to %s)."), LEASEFILE },
  { 'L', OPT_LOCALMX, NULL, gettext_noop("Return MX records for local hosts."), NULL },
  { 'm', ARG_DUP, "<host_name>,<target>,<pref>", gettext_noop("Specify an MX record."), NULL },
  { 'M', ARG_DUP, "<bootp opts>", gettext_noop("Specify BOOTP options to DHCP server."), NULL },
  { 'n', OPT_NO_POLL, NULL, gettext_noop("Do NOT poll %s file, reload only on SIGHUP."), RESOLVFILE }, 
  { 'N', OPT_NO_NEG, NULL, gettext_noop("Do NOT cache failed search results."), NULL },
  { LOPT_STALE_CACHE, ARG_ONE, "[=<max_expired>]", gettext_noop("Use expired cache data for faster reply."), NULL },
  { 'o', OPT_ORDER, NULL, gettext_noop("Use nameservers strictly in the order given in %s."), RESOLVFILE },
  { 'O', ARG_DUP, "<optspec>", gettext_noop("Specify options to be sent to DHCP clients."), NULL },
  { LOPT_FORCE, ARG_DUP, "<optspec>", gettext_noop("DHCP option sent even if the client does not request it."), NULL},
  { LOPT_PXE_OPT, ARG_DUP, "<optspec>", gettext_noop("DHCP option sent only to PXE clients."), NULL},
  { 'p', ARG_ONE, "<integer>", gettext_noop("Specify port to listen for DNS requests on (defaults to 53)."), NULL },
  { 'P', ARG_ONE, "<integer>", gettext_noop("Maximum supported UDP packet size for EDNS.0 (defaults to %s)."), "*" },
  { 'q', ARG_DUP, NULL, gettext_noop("Log DNS queries."), NULL },
  { 'Q', ARG_ONE, "<integer>", gettext_noop("Force the originating port for upstream DNS queries."), NULL },
  { LOPT_RANDPORT_LIM, ARG_ONE, "#ports", gettext_noop("Set maximum number of random originating ports for a query."), NULL },
  { 'R', OPT_NO_RESOLV, NULL, gettext_noop("Do NOT read resolv.conf."), NULL },
  { 'r', ARG_DUP, "<path>", gettext_noop("Specify path to resolv.conf (defaults to %s)."), RESOLVFILE }, 
  { LOPT_SERVERS_FILE, ARG_ONE, "<path>", gettext_noop("Specify path to file with server= options"), NULL },
  { 'S', ARG_DUP, "/<domain>/<ipaddr>", gettext_noop("Specify address(es) of upstream servers with optional domains."), NULL },
  { LOPT_REV_SERV, ARG_DUP, "<addr>/<prefix>,<ipaddr>", gettext_noop("Specify address of upstream servers for reverse address queries"), NULL },
  { LOPT_LOCAL, ARG_DUP, "/<domain>/", gettext_noop("Never forward queries to specified domains."), NULL },
  { 's', ARG_DUP, "<domain>[,<range>]", gettext_noop("Specify the domain to be assigned in DHCP leases."), NULL },
  { 't', ARG_ONE, "<host_name>", gettext_noop("Specify default target in an MX record."), NULL },
  { 'T', ARG_ONE, "<integer>", gettext_noop("Specify time-to-live in seconds for replies from /etc/hosts."), NULL },
  { LOPT_NEGTTL, ARG_ONE, "<integer>", gettext_noop("Specify time-to-live in seconds for negative caching."), NULL },
  { LOPT_MAXTTL, ARG_ONE, "<integer>", gettext_noop("Specify time-to-live in seconds for maximum TTL to send to clients."), NULL },
  { LOPT_MAXCTTL, ARG_ONE, "<integer>", gettext_noop("Specify time-to-live ceiling for cache."), NULL },
  { LOPT_MINCTTL, ARG_ONE, "<integer>", gettext_noop("Specify time-to-live floor for cache."), NULL },
  { LOPT_FAST_RETRY, ARG_ONE, "<milliseconds>", gettext_noop("Retry DNS queries after this many milliseconds."), NULL},
  { 'u', ARG_ONE, "<username>", gettext_noop("Change to this user after startup. (defaults to %s)."), CHUSER }, 
  { 'U', ARG_DUP, "set:<tag>,<class>", gettext_noop("Map DHCP vendor class to tag."), NULL },
  { 'v', 0, NULL, gettext_noop("Display dnsmasq version and copyright information."), NULL },
  { 'V', ARG_DUP, "<ipaddr>,<ipaddr>,<netmask>", gettext_noop("Translate IPv4 addresses from upstream servers."), NULL },
  { 'W', ARG_DUP, "<name>,<target>,...", gettext_noop("Specify a SRV record."), NULL },
  { 'w', 0, NULL, gettext_noop("Display this message. Use --help dhcp or --help dhcp6 for known DHCP options."), NULL },
  { 'x', ARG_ONE, "<path>", gettext_noop("Specify path of PID file (defaults to %s)."), RUNFILE },
  { 'X', ARG_ONE, "<integer>", gettext_noop("Specify maximum number of DHCP leases (defaults to %s)."), "&" },
  { 'y', OPT_LOCALISE, NULL, gettext_noop("Answer DNS queries based on the interface a query was sent to."), NULL },
  { 'Y', ARG_DUP, "<name>,<txt>[,<txt]", gettext_noop("Specify TXT DNS record."), NULL },
  { LOPT_PTR, ARG_DUP, "<name>,<target>", gettext_noop("Specify PTR DNS record."), NULL },
  { LOPT_INTNAME, ARG_DUP, "<name>,<interface>", gettext_noop("Give DNS name to IPv4 address of interface."), NULL },
  { 'z', OPT_NOWILD, NULL, gettext_noop("Bind only to interfaces in use."), NULL },
  { 'Z', OPT_ETHERS, NULL, gettext_noop("Read DHCP static host information from %s."), ETHERSFILE },
  { '1', ARG_ONE, "[=<busname>]", gettext_noop("Enable the DBus interface for setting upstream servers, etc."), NULL },
  { LOPT_UBUS, ARG_ONE, "[=<busname>]", gettext_noop("Enable the UBus interface."), NULL },
  { '2', ARG_DUP, "<interface>", gettext_noop("Do not provide DHCP on this interface, only provide DNS."), NULL },
  { LOPT_NO_DHCP6, ARG_DUP, "<interface>", gettext_noop("Do not provide DHCPv6 on this interface."), NULL },
  { LOPT_NO_DHCP4, ARG_DUP, "<interface>", gettext_noop("Do not provide DHCPv4 on this interface."), NULL },
  { '3', ARG_DUP, "[=tag:<tag>]...", gettext_noop("Enable dynamic address allocation for bootp."), NULL },
  { '4', ARG_DUP, "set:<tag>,<mac address>", gettext_noop("Map MAC address (with wildcards) to option set."), NULL },
  { LOPT_BRIDGE, ARG_DUP, "<iface>,<alias>..", gettext_noop("Treat DHCP requests on aliases as arriving from interface."), NULL },
  { LOPT_SHARED_NET, ARG_DUP, "<iface>|<addr>,<addr>", gettext_noop("Specify extra networks sharing a broadcast domain for DHCP"), NULL},
  { LOPT_LEASEQUERY, ARG_DUP, "[<addr>[/prefix>]]", gettext_noop("Enable RFC 4388 leasequery functions for DHCPv4"), NULL },
  { '5', OPT_NO_PING, NULL, gettext_noop("Disable ICMP echo address checking in the DHCP server."), NULL },
  { '6', ARG_ONE, "<path>", gettext_noop("Shell script to run on DHCP lease creation and destruction."), NULL },
  { LOPT_LUASCRIPT, ARG_DUP, "path", gettext_noop("Lua script to run on DHCP lease creation and destruction."), NULL },
  { LOPT_SCRIPTUSR, ARG_ONE, "<username>", gettext_noop("Run lease-change scripts as this user."), NULL },
  { LOPT_SCRIPT_ARP, OPT_SCRIPT_ARP, NULL, gettext_noop("Call dhcp-script with changes to local ARP table."), NULL },
  { '7', ARG_DUP, "<path>", gettext_noop("Read configuration from all the files in this directory."), NULL },
  { LOPT_CONF_SCRIPT, ARG_DUP, "<path>", gettext_noop("Execute file and read configuration from stdin."), NULL },
  { '8', ARG_ONE, "<facility>|<file>", gettext_noop("Log to this syslog facility or file. (defaults to DAEMON)"), NULL },
  { '9', OPT_LEASE_RO, NULL, gettext_noop("Do not use leasefile."), NULL },
  { '0', ARG_ONE, "<integer>", gettext_noop("Maximum number of concurrent DNS queries. (defaults to %s)"), "!" }, 
  { LOPT_RELOAD, OPT_RELOAD, NULL, gettext_noop("Clear DNS cache when reloading %s."), RESOLVFILE },
  { LOPT_NO_NAMES, ARG_DUP, "[=tag:<tag>]...", gettext_noop("Ignore hostnames provided by DHCP clients."), NULL },
  { LOPT_OVERRIDE, OPT_NO_OVERRIDE, NULL, gettext_noop("Do NOT reuse filename and server fields for extra DHCP options."), NULL },
  { LOPT_TFTP, ARG_DUP, "[=<intr>[,<intr>]]", gettext_noop("Enable integrated read-only TFTP server."), NULL },
  { LOPT_PREFIX, ARG_DUP, "<dir>[,<iface>]", gettext_noop("Export files by TFTP only from the specified subtree."), NULL },
  { LOPT_APREF, ARG_DUP, "[=ip|mac]", gettext_noop("Add client IP or hardware address to tftp-root."), NULL },
  { LOPT_SECURE, OPT_TFTP_SECURE, NULL, gettext_noop("Allow access only to files owned by the user running dnsmasq."), NULL },
  { LOPT_TFTP_NO_FAIL, OPT_TFTP_NO_FAIL, NULL, gettext_noop("Do not terminate the service if TFTP directories are inaccessible."), NULL },
  { LOPT_TFTP_MAX, ARG_ONE, "<integer>", gettext_noop("Maximum number of concurrent TFTP transfers (defaults to %s)."), "#" },
  { LOPT_TFTP_MTU, ARG_ONE, "<integer>", gettext_noop("Maximum MTU to use for TFTP transfers."), NULL },
  { LOPT_NOBLOCK, OPT_TFTP_NOBLOCK, NULL, gettext_noop("Disable the TFTP blocksize extension."), NULL },
  { LOPT_TFTP_LC, OPT_TFTP_LC, NULL, gettext_noop("Convert TFTP filenames to lowercase"), NULL },
  { LOPT_TFTPPORTS, ARG_ONE, "<start>,<end>", gettext_noop("Ephemeral port range for use by TFTP transfers."), NULL },
  { LOPT_SINGLE_PORT, OPT_SINGLE_PORT, NULL, gettext_noop("Use only one port for TFTP server."), NULL },
  { LOPT_LOG_OPTS, OPT_LOG_OPTS, NULL, gettext_noop("Extra logging for DHCP."), NULL },
  { LOPT_MAX_LOGS, ARG_ONE, "[=<integer>]", gettext_noop("Enable async. logging; optionally set queue length."), NULL },
  { LOPT_REBIND, OPT_NO_REBIND, NULL, gettext_noop("Stop DNS rebinding. Filter private IP ranges when resolving."), NULL },
  { LOPT_LOC_REBND, OPT_LOCAL_REBIND, NULL, gettext_noop("Allow rebinding of 127.0.0.0/8, for RBL servers."), NULL },
  { LOPT_NO_REBIND, ARG_DUP, "/<domain>/", gettext_noop("Inhibit DNS-rebind protection on this domain."), NULL },
  { LOPT_NOLAST, OPT_ALL_SERVERS, NULL, gettext_noop("Always perform DNS queries to all servers."), NULL },
  { LOPT_MATCH, ARG_DUP, "set:<tag>,<optspec>", gettext_noop("Set tag if client includes matching option in request."), NULL },
  { LOPT_NAME_MATCH, ARG_DUP, "set:<tag>,<string>[*]", gettext_noop("Set tag if client provides given name."), NULL },
  { LOPT_ALTPORT, ARG_ONE, "[=<ports>]", gettext_noop("Use alternative ports for DHCP."), NULL },
  { LOPT_NAPTR, ARG_DUP, "<name>,<naptr>", gettext_noop("Specify NAPTR DNS record."), NULL },
  { LOPT_MINPORT, ARG_ONE, "<port>", gettext_noop("Specify lowest port available for DNS query transmission."), NULL },
  { LOPT_MAXPORT, ARG_ONE, "<port>", gettext_noop("Specify highest port available for DNS query transmission."), NULL },
  { LOPT_DHCP_FQDN, OPT_DHCP_FQDN, NULL, gettext_noop("Use only fully qualified domain names for DHCP clients."), NULL },
  { LOPT_GEN_NAMES, ARG_DUP, "[=tag:<tag>]", gettext_noop("Generate hostnames based on MAC address for nameless clients."), NULL},
  { LOPT_PROXY, ARG_DUP, "[=<ipaddr>]...", gettext_noop("Use these DHCP relays as full proxies."), NULL },
  { LOPT_RELAY, ARG_DUP, "<local-addr>,<server>[,<iface>]", gettext_noop("Relay DHCP requests to a remote server"), NULL},
  { LOPT_SPLIT_RELAY, ARG_DUP, "<local-addr>,<server>,<iface>", gettext_noop("Relay DHCP requests to a remote server"), NULL},
  { LOPT_CNAME, ARG_DUP, "<alias>,<target>[,<ttl>]", gettext_noop("Specify alias name for LOCAL DNS name."), NULL },
  { LOPT_PXE_PROMT, ARG_DUP, "<prompt>,[<timeout>]", gettext_noop("Prompt to send to PXE clients."), NULL },
  { LOPT_PXE_SERV, ARG_DUP, "<service>", gettext_noop("Boot service for PXE menu."), NULL },
  { LOPT_TEST, 0, NULL, gettext_noop("Check configuration syntax."), NULL },
  { LOPT_ADD_MAC, ARG_DUP, "[=base64|text]", gettext_noop("Add requestor's MAC address to forwarded DNS queries."), NULL },
  { LOPT_STRIP_MAC, OPT_STRIP_MAC, NULL, gettext_noop("Strip MAC information from queries."), NULL },
  { LOPT_ADD_SBNET, ARG_ONE, "<v4 pref>[,<v6 pref>]", gettext_noop("Add specified IP subnet to forwarded DNS queries."), NULL },
  { LOPT_STRIP_SBNET, OPT_STRIP_ECS, NULL, gettext_noop("Strip ECS information from queries."), NULL },
  { LOPT_CPE_ID, ARG_ONE, "<text>", gettext_noop("Add client identification to forwarded DNS queries."), NULL },
  { LOPT_DNSSEC, OPT_DNSSEC_PROXY, NULL, gettext_noop("Proxy DNSSEC validation results from upstream nameservers."), NULL },
  { LOPT_INCR_ADDR, OPT_CONSEC_ADDR, NULL, gettext_noop("Attempt to allocate sequential IP addresses to DHCP clients."), NULL },
  { LOPT_IGNORE_CLID, OPT_IGNORE_CLID, NULL, gettext_noop("Ignore client identifier option sent by DHCP clients."), NULL },
  { LOPT_CONNTRACK, OPT_CONNTRACK, NULL, gettext_noop("Copy connection-track mark from queries to upstream connections."), NULL },
  { LOPT_FQDN, OPT_FQDN_UPDATE, NULL, gettext_noop("Allow DHCP clients to do their own DDNS updates."), NULL },
  { LOPT_RA, OPT_RA, NULL, gettext_noop("Send router-advertisements for interfaces doing DHCPv6"), NULL },
  { LOPT_DUID, ARG_ONE, "<enterprise>,<duid>", gettext_noop("Specify DUID_EN-type DHCPv6 server DUID"), NULL },
  { LOPT_HOST_REC, ARG_DUP, "<name>,<address>[,<ttl>]", gettext_noop("Specify host (A/AAAA and PTR) records"), NULL },
  { LOPT_DYNHOST, ARG_DUP, "<name>,[<IPv4>][,<IPv6>],<interface-name>", gettext_noop("Specify host record in interface subnet"), NULL },
  { LOPT_CAA, ARG_DUP, "<name>,<flags>,<tag>,<value>", gettext_noop("Specify certification authority authorization record"), NULL },  
  { LOPT_RR, ARG_DUP, "<name>,<RR-number>,[<data>]", gettext_noop("Specify arbitrary DNS resource record"), NULL },
  { LOPT_CLVERBIND, OPT_CLEVERBIND, NULL, gettext_noop("Bind to interfaces in use - check for new interfaces"), NULL },
  { LOPT_AUTHSERV, ARG_ONE, "<NS>,<interface>", gettext_noop("Export local names to global DNS"), NULL },
  { LOPT_AUTHZONE, ARG_DUP, "<domain>,[<subnet>...]", gettext_noop("Domain to export to global DNS"), NULL },
  { LOPT_AUTHTTL, ARG_ONE, "<integer>", gettext_noop("Set TTL for authoritative replies"), NULL },
  { LOPT_AUTHSOA, ARG_ONE, "<serial>[,...]", gettext_noop("Set authoritative zone information"), NULL },
  { LOPT_AUTHSFS, ARG_DUP, "<NS>[,<NS>...]", gettext_noop("Secondary authoritative nameservers for forward domains"), NULL },
  { LOPT_AUTHPEER, ARG_DUP, "<ipaddr>[,<ipaddr>...]", gettext_noop("Peers which are allowed to do zone transfer"), NULL },
  { LOPT_IPSET, ARG_DUP, "/<domain>[/<domain>...]/<ipset>...", gettext_noop("Specify ipsets to which matching domains should be added"), NULL },
  { LOPT_NFTSET, ARG_DUP, "/<domain>[/<domain>...]/<nftset>...", gettext_noop("Specify nftables sets to which matching domains should be added"), NULL },
  { LOPT_CMARK_ALST_EN, ARG_ONE, "[=<mask>]", gettext_noop("Enable filtering of DNS queries with connection-track marks."), NULL },
  { LOPT_CMARK_ALST, ARG_DUP, "<connmark>[/<mask>][,<pattern>[/<pattern>...]]", gettext_noop("Set allowed DNS patterns for a connection-track mark."), NULL },
  { LOPT_SYNTH, ARG_DUP, "<domain>,<range>,[<prefix>]", gettext_noop("Specify a domain and address range for synthesised names"), NULL },
  { LOPT_SEC_VALID, OPT_DNSSEC_VALID, NULL, gettext_noop("Activate DNSSEC validation"), NULL },
  { LOPT_TRUST_ANCHOR, ARG_DUP, "<domain>,[<class>,]...", gettext_noop("Specify trust anchor key digest."), NULL },
  { LOPT_DNSSEC_DEBUG, OPT_DNSSEC_DEBUG, NULL, gettext_noop("Disable upstream checking for DNSSEC debugging."), NULL },
  { LOPT_DNSSEC_CHECK, ARG_DUP, NULL, gettext_noop("Ensure answers without DNSSEC are in unsigned zones."), NULL },
  { LOPT_DNSSEC_TIME, OPT_DNSSEC_TIME, NULL, gettext_noop("Don't check DNSSEC signature timestamps until first cache-reload"), NULL },
  { LOPT_DNSSEC_STAMP, ARG_ONE, "<path>", gettext_noop("Timestamp file to verify system clock for DNSSEC"), NULL },
  { LOPT_DNSSEC_LIMITS, ARG_ONE, "<limit>,..", gettext_noop("Set resource limits for DNSSEC validation"), NULL },
  { LOPT_RA_PARAM, ARG_DUP, "<iface>,[mtu:<value>|<interface>|off,][<prio>,]<intval>[,<lifetime>]", gettext_noop("Set MTU, priority, resend-interval and router-lifetime"), NULL },
  { LOPT_QUIET_DHCP, OPT_QUIET_DHCP, NULL, gettext_noop("Do not log routine DHCP."), NULL },
  { LOPT_QUIET_DHCP6, OPT_QUIET_DHCP6, NULL, gettext_noop("Do not log routine DHCPv6."), NULL },
  { LOPT_QUIET_RA, OPT_QUIET_RA, NULL, gettext_noop("Do not log RA."), NULL },
  { LOPT_LOG_DEBUG, OPT_LOG_DEBUG, NULL, gettext_noop("Log debugging information."), NULL }, 
  { LOPT_LOCAL_SERVICE, ARG_ONE, NULL, gettext_noop("Accept queries only from directly-connected networks."), NULL },
  { LOPT_LOOP_DETECT, OPT_LOOP_DETECT, NULL, gettext_noop("Detect and remove DNS forwarding loops."), NULL },
  { LOPT_IGNORE_ADDR, ARG_DUP, "<ipaddr>", gettext_noop("Ignore DNS responses containing ipaddr."), NULL }, 
  { LOPT_DHCPTTL, ARG_ONE, "<ttl>", gettext_noop("Set TTL in DNS responses with DHCP-derived addresses."), NULL }, 
  { LOPT_REPLY_DELAY, ARG_ONE, "<integer>", gettext_noop("Delay DHCP replies for at least number of seconds."), NULL },
  { LOPT_RAPID_COMMIT, OPT_RAPID_COMMIT, NULL, gettext_noop("Enables DHCPv4 Rapid Commit option."), NULL },
  { LOPT_DUMPFILE, ARG_ONE, "<path>", gettext_noop("Path to debug packet dump file."), NULL },
  { LOPT_DUMPMASK, ARG_ONE, "<hex>", gettext_noop("Mask which packets to dump."), NULL },
  { LOPT_SCRIPT_TIME, OPT_LEASE_RENEW, NULL, gettext_noop("Call dhcp-script when lease expiry changes."), NULL },
  { LOPT_UMBRELLA, ARG_ONE, "[=<optspec>]", gettext_noop("Send Cisco Umbrella identifiers including remote IP."), NULL },
  { LOPT_QUIET_TFTP, OPT_QUIET_TFTP, NULL, gettext_noop("Do not log routine TFTP."), NULL },
  { LOPT_NORR, OPT_NORR, NULL, gettext_noop("Suppress round-robin ordering of DNS records."), NULL },
  { LOPT_NO_ENCODE, OPT_NO_0x20, NULL, gettext_noop("Suppress DNS bit 0x20 encoding."), NULL },
  { LOPT_DO_ENCODE, OPT_DO_0x20, NULL, gettext_noop("Enable DNS bit 0x20 encoding."), NULL },
  { LOPT_NO_IDENT, OPT_NO_IDENT, NULL, gettext_noop("Do not add CHAOS TXT records."), NULL },
  { LOPT_CACHE_RR, ARG_DUP, "<RR-type>", gettext_noop("Cache this DNS resource record type."), NULL },
  { LOPT_MAX_PROCS, ARG_ONE, "<integer>", gettext_noop("Maximum number of concurrent tcp connections."), NULL },
  { 0, 0, NULL, NULL, NULL }
}; 

/* We hide metacharacters in quoted strings by mapping them into the ASCII control
   character space. Note that the \0, \t \b \r \033 and \n characters are carefully placed in the
   following sequence so that they map to themselves: it is therefore possible to call
   unhide_metas repeatedly on string without breaking things.
   The transformation gets undone by opt_canonicalise, atoi_check and opt_string_alloc, and a 
   couple of other places. 
   Note that space is included here so that
   --dhcp-option=3, string
   has five characters, whilst
   --dhcp-option=3," string"
   has six.
*/

static const char meta[] = "\000123456 \b\t\n78\r90abcdefABCDE\033F:,.";

static char hide_meta(char c)
{
  unsigned int i;

  for (i = 0; i < (sizeof(meta) - 1); i++)
    if (c == meta[i])
      return (char)i;
  
  return c;
}

static char unhide_meta(char cr)
{ 
  unsigned int c = cr;
  
  if (c < (sizeof(meta) - 1))
    cr = meta[c];
  
  return cr;
}

static void unhide_metas(char *cp)
{
  if (cp)
    for(; *cp; cp++)
      *cp = unhide_meta(*cp);
}

static void *opt_malloc(size_t size)
{
  void *ret;

  if (mem_recover)
    {
      ret = whine_malloc(size);
      if (!ret)
	longjmp(mem_jmp, 1);
    }
  else
    ret = safe_malloc(size);
  
  return ret;
}

static char *opt_string_alloc(const char *cp)
{
  char *ret = NULL;
  size_t len;
  
  if (cp && (len = strlen(cp)) != 0)
    {
      ret = opt_malloc(len+1);
      memcpy(ret, cp, len+1); 
      
      /* restore hidden metachars */
      unhide_metas(ret);
    }
    
  return ret;
}


/* find next comma, split string with zero and eliminate spaces.
   return start of string following comma */

static char *split_chr(char *s, char c)
{
  char *comma, *p;

  if (!s || !(comma = strchr(s, c)))
    return NULL;
  
  p = comma;
  *comma = ' ';
  
  for (; *comma == ' '; comma++);
 
  for (; (p >= s) && *p == ' '; p--)
    *p = 0;
    
  return comma;
}

static char *split(char *s)
{
  return split_chr(s, ',');
}

static char *canonicalise_opt(char *s)
{
  char *ret;
  int nomem;

  if (!s)
    return 0;

  if (strlen(s) == 0)
    return opt_malloc(1); /* Heap-allocated empty string */

  unhide_metas(s);
  if (!(ret = canonicalise(s, &nomem)) && nomem)
    {
      if (mem_recover)
	longjmp(mem_jmp, 1);
      else
	die(_("could not get memory"), NULL, EC_NOMEM);
    }

  return ret;
}

static int numeric_check(char *a)
{
  char *p;

  if (!a)
    return 0;

  unhide_metas(a);
  
  for (p = a; *p; p++)
     if (*p < '0' || *p > '9')
       return 0;

  return 1;
}

static int atoi_check(char *a, int *res)
{
  if (!numeric_check(a))
    return 0;
  *res = atoi(a);
  return 1;
}

static int strtoul_check(char *a, u32 *res)
{
  unsigned long x;
  
  if (!numeric_check(a))
    return 0;
  x = strtoul(a, NULL, 10);
  if (errno || x > UINT32_MAX) {
    errno = 0;
    return 0;
  }
  *res = (u32)x;
  return 1;
}

static int atoi_check16(char *a, int *res)
{
  if (!(atoi_check(a, res)) ||
      *res < 0 ||
      *res > 0xffff)
    return 0;

  return 1;
}

#ifdef HAVE_DNSSEC
static int atoi_check8(char *a, int *res)
{
  if (!(atoi_check(a, res)) ||
      *res < 0 ||
      *res > 0xff)
    return 0;

  return 1;
}
#endif

#ifndef NO_ID
static void add_txt(char *name, char *txt, int stat)
{
  struct txt_record *r = opt_malloc(sizeof(struct txt_record));

  if (txt)
    {
      size_t len = strlen(txt);
      r->txt = opt_malloc(len+1);
      r->len = len+1;
      *(r->txt) = len;
      memcpy((r->txt)+1, txt, len);
    }

  r->stat = stat;
  r->name = opt_string_alloc(name);
  r->next = daemon->txt;
  daemon->txt = r;
  r->class = C_CHAOS;
}
#endif

static void do_usage(void)
{
  char buff[100];
  int i, j;

  struct {
    char handle;
    int val;
  } tab[] = {
    { '$', CACHESIZ },
    { '*', EDNS_PKTSZ },
    { '&', MAXLEASES },
    { '!', FTABSIZ },
    { '#', TFTP_MAX_CONNECTIONS },
    { '\0', 0 }
  };

  printf(_("Usage: dnsmasq [options]\n\n"));
#ifndef HAVE_GETOPT_LONG
  printf(_("Use short options only on the command line.\n"));
#endif
  printf(_("Valid options are:\n"));
  
  for (i = 0; usage[i].opt != 0; i++)
    {
      char *desc = usage[i].flagdesc; 
      char *eq = "=";
      
      if (!desc || *desc == '[')
	eq = "";
      
      if (!desc)
	desc = "";

      for ( j = 0; opts[j].name; j++)
	if (opts[j].val == usage[i].opt)
	  break;
      if (usage[i].opt < 256)
	sprintf(buff, "-%c, ", usage[i].opt);
      else
	sprintf(buff, "    ");
      
      sprintf(buff+4, "--%s%s%s", opts[j].name, eq, desc);
      printf("%-55.55s", buff);
	     
      if (usage[i].arg)
	{
	  safe_strncpy(buff, usage[i].arg, sizeof(buff));
	  for (j = 0; tab[j].handle; j++)
	    if (tab[j].handle == *(usage[i].arg))
	      sprintf(buff, "%d", tab[j].val);
	}
      printf(_(usage[i].desc), buff);
      printf("\n");
    }
}

#define ret_err(x) do { strcpy(errstr, (x)); return 0; } while (0)
#define ret_err_free(x,m) do { strcpy(errstr, (x)); free((m)); return 0; } while (0)
#define goto_err(x) do { strcpy(errstr, (x)); goto on_error; } while (0)

static char *parse_mysockaddr(char *arg, union mysockaddr *addr) 
{
  if (inet_pton(AF_INET, arg, &addr->in.sin_addr) > 0)
    addr->sa.sa_family = AF_INET;
  else if (inet_pton(AF_INET6, arg, &addr->in6.sin6_addr) > 0)
    addr->sa.sa_family = AF_INET6;
  else
    return _("bad address");
   
  return NULL;
}

char *parse_server(char *arg, struct server_details *sdetails)
{
  sdetails->serv_port = NAMESERVER_PORT;
  char *portno;
  int ecode = 0;
  struct addrinfo hints;

  memset(&hints, 0, sizeof(struct addrinfo));
  
  *sdetails->interface = 0;
  sdetails->addr_type = AF_UNSPEC;
     
  if (strcmp(arg, "#") == 0)
    {
      if (sdetails->flags)
	*sdetails->flags |= SERV_USE_RESOLV;
      sdetails->addr_type = AF_LOCAL;
      sdetails->valid = 1;
      return NULL;
    }
  
  if ((sdetails->source = split_chr(arg, '@')) && /* is there a source. */
      (portno = split_chr(sdetails->source, '#')) &&
      !atoi_check16(portno, &sdetails->source_port))
    return _("bad port");
  
  if ((portno = split_chr(arg, '#')) && /* is there a port no. */
      !atoi_check16(portno, &sdetails->serv_port))
    return _("bad port");
  
  sdetails->scope_id = split_chr(arg, '%');
  
  if (sdetails->source) {
    sdetails->interface_opt = split_chr(sdetails->source, '@');

    if (sdetails->interface_opt)
      {
#if defined(SO_BINDTODEVICE)
	safe_strncpy(sdetails->interface, sdetails->source, IF_NAMESIZE);
	sdetails->source = sdetails->interface_opt;
#else
	return _("interface binding not supported");
#endif
      }
  }

  if (inet_pton(AF_INET, arg, &sdetails->addr->in.sin_addr) > 0)
      sdetails->addr_type = AF_INET;
  else if (inet_pton(AF_INET6, arg, &sdetails->addr->in6.sin6_addr) > 0)
      sdetails->addr_type = AF_INET6;
  else 
    {
      /* if the argument is neither an IPv4 not an IPv6 address, it might be a
	 hostname and we should try to resolve it to a suitable address. */
      memset(&hints, 0, sizeof(hints));
      /* The AI_ADDRCONFIG flag ensures that then IPv4 addresses are returned in
         the result only if the local system has at least one IPv4 address
         configured, and IPv6 addresses are returned only if the local system
         has at least one IPv6 address configured. The loopback address is not
         considered for this case as valid as a configured address. This flag is
         useful on, for example, IPv4-only systems, to ensure that getaddrinfo()
         does not return IPv6 socket addresses that would always fail in
         subsequent connect() or bind() attempts. */
      hints.ai_flags = AI_ADDRCONFIG;
#if defined(HAVE_IDN) && defined(AI_IDN)
      /* If the AI_IDN flag is specified and we have glibc 2.3.4 or newer, then
         the node name given in node is converted to IDN format if necessary.
         The source encoding is that of the current locale. */
      hints.ai_flags |= AI_IDN;
#endif
      /* The value AF_UNSPEC indicates that getaddrinfo() should return socket
         addresses for any address family (either IPv4 or IPv6, for example)
         that can be used with node <arg> and service "domain". */
      hints.ai_family = AF_UNSPEC;

      /* Get addresses suitable for sending datagrams. We assume that we can use the
	 same addresses for TCP connections. Setting this to zero gets each address
	 threes times, for SOCK_STREAM, SOCK_RAW and SOCK_DGRAM, which is not useful. */
      hints.ai_socktype = SOCK_DGRAM;

      /* Get address associated with this hostname */
      ecode = getaddrinfo(arg, NULL, &hints, &sdetails->hostinfo);
      if (ecode == 0)
	{
	  /* The getaddrinfo() function allocated and initialized a linked list of
	     addrinfo structures, one for each network address that matches node
	     and service, subject to the restrictions imposed by our <hints>
	     above, and returns a pointer to the start of the list in <hostinfo>.
	     The items in the linked list are linked by the <ai_next> field. */
	  sdetails->valid = 1;
	  sdetails->orig_hostinfo = sdetails->hostinfo;
	  return NULL;
	}
      else
	{
	  /* Lookup failed, return human readable error string */
	  if (ecode == EAI_AGAIN)
	    return _("Cannot resolve server name");
	  else
	    return _((char*)gai_strerror(ecode));
	}
    }
  
  sdetails->valid = 1;
  return NULL;
}

char *parse_server_addr(struct server_details *sdetails)
{
  if (sdetails->addr_type == AF_INET)
    {
      sdetails->addr->in.sin_port = htons(sdetails->serv_port);
      sdetails->addr->sa.sa_family = sdetails->source_addr->sa.sa_family = AF_INET;
#ifdef HAVE_SOCKADDR_SA_LEN
      sdetails->source_addr->in.sin_len = sdetails->addr->in.sin_len = sizeof(struct sockaddr_in);
#endif
      sdetails->source_addr->in.sin_addr.s_addr = INADDR_ANY;
      sdetails->source_addr->in.sin_port = htons(daemon->query_port);
      
      if (sdetails->source)
	{
	  if (sdetails->flags)
	    *sdetails->flags |= SERV_HAS_SOURCE;
	  sdetails->source_addr->in.sin_port = htons(sdetails->source_port);
	  if (inet_pton(AF_INET, sdetails->source, &sdetails->source_addr->in.sin_addr) == 0)
	    {
	      if (inet_pton(AF_INET6, sdetails->source, &sdetails->source_addr->in6.sin6_addr) == 1)
		{
		  sdetails->source_addr->sa.sa_family = AF_INET6;
		  /* When resolving a server IP by hostname, we can simply skip mismatching
		     server / source IP pairs. Otherwise, when an IP address is given directly,
		     this is a fatal error. */
		  if (!sdetails->orig_hostinfo)
		    return _("cannot use IPv4 server address with IPv6 source address");
		}
	      else
		{
#if defined(SO_BINDTODEVICE)
		  if (sdetails->interface_opt)
		    return _("interface can only be specified once");

		  sdetails->source_addr->in.sin_addr.s_addr = INADDR_ANY;
		  safe_strncpy(sdetails->interface, sdetails->source, IF_NAMESIZE);
#else
		  return _("interface binding not supported");
#endif
		}
	    }
	}
    }
  else if (sdetails->addr_type == AF_INET6)
    {
      if (sdetails->scope_id && (sdetails->scope_index = if_nametoindex(sdetails->scope_id)) == 0)
	return _("bad interface name");

      sdetails->addr->in6.sin6_port = htons(sdetails->serv_port);
      sdetails->addr->in6.sin6_scope_id = sdetails->scope_index;
      sdetails->source_addr->in6.sin6_addr = in6addr_any;
      sdetails->source_addr->in6.sin6_port = htons(daemon->query_port);
      sdetails->source_addr->in6.sin6_scope_id = 0;
      sdetails->addr->sa.sa_family = sdetails->source_addr->sa.sa_family = AF_INET6;
      sdetails->addr->in6.sin6_flowinfo = sdetails->source_addr->in6.sin6_flowinfo = 0;
#ifdef HAVE_SOCKADDR_SA_LEN
      sdetails->addr->in6.sin6_len = sdetails->source_addr->in6.sin6_len = sizeof(sdetails->addr->in6);
#endif
      if (sdetails->source)
	{
	  if (sdetails->flags)
	    *sdetails->flags |= SERV_HAS_SOURCE;
	  sdetails->source_addr->in6.sin6_port = htons(sdetails->source_port);
	  if (inet_pton(AF_INET6, sdetails->source, &sdetails->source_addr->in6.sin6_addr) == 0)
	    {
	      if (inet_pton(AF_INET, sdetails->source, &sdetails->source_addr->in.sin_addr) == 1)
		{
		  sdetails->source_addr->sa.sa_family = AF_INET;
		  /* When resolving a server IP by hostname, we can simply skip mismatching
		     server / source IP pairs. Otherwise, when an IP address is given directly,
		     this is a fatal error. */
		  if(!sdetails->orig_hostinfo)
		    return _("cannot use IPv6 server address with IPv4 source address");
		}
	      else
		{
#if defined(SO_BINDTODEVICE)
		  if (sdetails->interface_opt)
		  return _("interface can only be specified once");

		  sdetails->source_addr->in6.sin6_addr = in6addr_any;
		  safe_strncpy(sdetails->interface, sdetails->source, IF_NAMESIZE);
#else
		  return _("interface binding not supported");
#endif
		}
	    }
	}
    }
  else if (sdetails->addr_type != AF_LOCAL)
    return _("bad address");
  
  return NULL;
}

int parse_server_next(struct server_details *sdetails)
{
  /* Looping over resolved addresses? */
  if (sdetails->hostinfo)
    {
      /* Get address type */
      sdetails->addr_type = sdetails->hostinfo->ai_family;

      /* Get address */
      if (sdetails->addr_type == AF_INET)
	memcpy(&sdetails->addr->in.sin_addr,
		&((struct sockaddr_in *) sdetails->hostinfo->ai_addr)->sin_addr,
		sizeof(sdetails->addr->in.sin_addr));
      else if (sdetails->addr_type == AF_INET6)
	memcpy(&sdetails->addr->in6.sin6_addr,
		&((struct sockaddr_in6 *) sdetails->hostinfo->ai_addr)->sin6_addr,
		sizeof(sdetails->addr->in6.sin6_addr));

      /* Iterate to the next available address */
      sdetails->valid = sdetails->hostinfo->ai_next != NULL;
      sdetails->hostinfo = sdetails->hostinfo->ai_next;
      return 1;
    }
  else if (sdetails->valid)
    {
      /* When using an IP address, we return the address only once */
      sdetails->valid = 0;
      return 1;
    }
  /* Stop iterating here, we used all available addresses */
  return 0;
}

static char *domain_rev4(int from_file, char *server, struct in_addr *addr4, int size)
{
  int i, j;
  char *string;
  int msize;
  u16 flags = 0;
  char domain[29]; /* strlen("xxx.yyy.zzz.ttt.in-addr.arpa")+1 */
  union mysockaddr serv_addr, source_addr;
  char interface[IF_NAMESIZE+1];
  int count = 1, rem, addrbytes, addrbits;
  struct server_details sdetails;

  memset(&sdetails, 0, sizeof(struct server_details));
  sdetails.addr = &serv_addr;
  sdetails.source_addr = &source_addr;
  sdetails.interface = interface;
  sdetails.flags = &flags;
    
  if (!server)
    flags = SERV_LITERAL_ADDRESS;
  else if ((string = parse_server(server, &sdetails)))
    return string;
  
  if (from_file)
    flags |= SERV_FROM_FILE;
 
  rem = size & 0x7;
  addrbytes = (32 - size) >> 3;
  addrbits = (32 - size) & 7;
  
  if (size > 32 || size < 1)
    return _("bad IPv4 prefix length");
  
  /* Zero out last address bits according to CIDR mask */
  ((u8 *)addr4)[3-addrbytes] &= ~((1 << addrbits)-1);
  
  size = size & ~0x7;
  
  if (rem != 0)
    count = 1 << (8 - rem);
  
  for (i = 0; i < count; i++)
    {
      *domain = 0;
      string = domain;
      msize = size/8;
      
      for (j = (rem == 0) ? msize-1 : msize; j >= 0; j--)
	{ 
	  int dig = ((unsigned char *)addr4)[j];
	  
	  if (j == msize)
	    dig += i;
	  
	  string += sprintf(string, "%d.", dig);
	}
      
      sprintf(string, "in-addr.arpa");

      if (flags & SERV_LITERAL_ADDRESS)
	{
	  if (!add_update_server(flags, &serv_addr, &source_addr, interface, domain, NULL))
	    return  _("error");
	}
      else
	{
	  /* Always reset server as valid here, so we can add the same upstream
	     server address multiple times for each x.y.z.in-addr.arpa  */
	  sdetails.valid = 1;
	  while (parse_server_next(&sdetails))
	    {
	      if ((string = parse_server_addr(&sdetails)))
		return string;
	      
	      if (!add_update_server(flags, &serv_addr, &source_addr, interface, domain, NULL))
		return  _("error");
	    }

	  if (sdetails.orig_hostinfo)
	    freeaddrinfo(sdetails.orig_hostinfo);
	}
    }
  
  return NULL;
}

static char *domain_rev6(int from_file, char *server, struct in6_addr *addr6, int size)
{
  int i, j;
  char *string;
  int msize;
  u16 flags = 0;
  char domain[73]; /* strlen("32*<n.>ip6.arpa")+1 */
  union mysockaddr serv_addr, source_addr;
  char interface[IF_NAMESIZE+1];
  int count = 1, rem, addrbytes, addrbits;
  struct server_details sdetails;
  
  memset(&sdetails, 0, sizeof(struct server_details));
  sdetails.addr = &serv_addr;
  sdetails.source_addr = &source_addr;
  sdetails.interface = interface;
  sdetails.flags = &flags;
   
  if (!server)
    flags = SERV_LITERAL_ADDRESS;
  else if ((string = parse_server(server, &sdetails)))
    return string;

  if (from_file)
    flags |= SERV_FROM_FILE;
  
  rem = size & 0x3;
  addrbytes = (128 - size) >> 3;
  addrbits = (128 - size) & 7;
  
  if (size > 128 || size < 1)
    return _("bad IPv6 prefix length");
  
  /* Zero out last address bits according to CIDR mask */
  addr6->s6_addr[15-addrbytes] &= ~((1 << addrbits) - 1);
  
  size = size & ~0x3;
  
  if (rem != 0)
    count = 1 << (4 - rem);
      
  for (i = 0; i < count; i++)
    {
      *domain = 0;
      string = domain;
      msize = size/4;
  
      for (j = (rem == 0) ? msize-1 : msize; j >= 0; j--)
	{ 
	  int dig = ((unsigned char *)addr6)[j>>1];
	  
	  dig = j & 1 ? dig & 15 : dig >> 4;
	  
	  if (j == msize)
	    dig += i;
	  
	  string += sprintf(string, "%.1x.", dig);
	}
      
      sprintf(string, "ip6.arpa");

      if (flags & SERV_LITERAL_ADDRESS)
	{
	  if (!add_update_server(flags, &serv_addr, &source_addr, interface, domain, NULL))
	    return  _("error");
	}
      else
	{
	  /* Always reset server as valid here, so we can add the same upstream
	     server address multiple times for each x.y.z.ip6.arpa  */
	  sdetails.valid = 1;
	  while (parse_server_next(&sdetails))
	    {
	      if ((string = parse_server_addr(&sdetails)))
		return string;
	      
	      if (!add_update_server(flags, &serv_addr, &source_addr, interface, domain, NULL))
		return  _("error");
	    }

	  if (sdetails.orig_hostinfo)
	    freeaddrinfo(sdetails.orig_hostinfo);
	}
    }
  
  return NULL;
}

static void if_names_add(const char *ifname)
{
  struct iname *new = opt_malloc(sizeof(struct iname));
  new->next = daemon->if_names;
  daemon->if_names = new;
  /* new->name may be NULL if someone does
     "interface=" to disable all interfaces except loop. */
  new->name = opt_string_alloc(ifname);
  new->flags = 0;
}

#ifdef HAVE_DHCP

static int is_tag_prefix(char *arg)
{
  if (arg && (strstr(arg, "net:") == arg || strstr(arg, "tag:") == arg))
    return 1;
  
  return 0;
}

static char *set_prefix(char *arg)
{
   if (strstr(arg, "set:") == arg)
     return arg+4;
   
   return arg;
}

static struct dhcp_netid *dhcp_netid_create(const char *net, struct dhcp_netid *next)
{
  struct dhcp_netid *tt;
  tt = opt_malloc(sizeof (struct dhcp_netid));
  tt->net = opt_string_alloc(net);
  tt->next = next;
  return tt;
}

static void dhcp_netid_free(struct dhcp_netid *nid)
{
  while (nid)
    {
      struct dhcp_netid *tmp = nid;
      nid = nid->next;
      free(tmp->net);
      free(tmp);
    }
}

/* Parse one or more tag:s before parameters.
 * Moves arg to the end of tags. */
static struct dhcp_netid *dhcp_tags(char **arg)
{
  struct dhcp_netid *id = NULL;

  while (is_tag_prefix(*arg))
    {
      char *comma = split(*arg);
      id = dhcp_netid_create((*arg)+4, id);
      *arg = comma;
    };
  if (!*arg)
    {
      dhcp_netid_free(id);
      id = NULL;
    }
  return id;
}

static void dhcp_netid_list_free(struct dhcp_netid_list *netid)
{
  while (netid)
    {
      struct dhcp_netid_list *tmplist = netid;
      netid = netid->next;
      /* Note: don't use dhcp_netid_free() here, since that 
	 frees a list linked on netid->next. Where a netid_list
	 is used that's because the the ->next pointers in the
	 netids are being used to temporarily construct 
	 a list of valid tags. */
      free(tmplist->list->net);
      free(tmplist->list);
      free(tmplist);
    }
}

static void dhcp_config_free(struct dhcp_config *config)
{
  if (config)
    {
      struct hwaddr_config *hwaddr = config->hwaddr;
      
      while (hwaddr)
        {
	  struct hwaddr_config *tmp = hwaddr;
          hwaddr = hwaddr->next;
	  free(tmp);
        }
      
      dhcp_netid_list_free(config->netid);
      dhcp_netid_free(config->filter);
      
      if (config->flags & CONFIG_CLID)
        free(config->clid);
      if (config->flags & CONFIG_NAME)
	free(config->hostname);

#ifdef HAVE_DHCP6
      if (config->flags & CONFIG_ADDR6)
	{
	  struct addrlist *addr, *tmp;
	  
	  for (addr = config->addr6; addr; addr = tmp)
	    {
	      tmp = addr->next;
	      free(addr);
	    }
	}
#endif

      free(config);
    }
}

static void dhcp_context_free(struct dhcp_context *ctx)
{
  if (ctx)
    {
      dhcp_netid_free(ctx->filter);
      free(ctx->netid.net);
#ifdef HAVE_DHCP6
      free(ctx->template_interface);
#endif
      free(ctx);
    }
}

static void dhcp_opt_free(struct dhcp_opt *opt)
{
  if (opt->flags & DHOPT_VENDOR)
    free(opt->u.vendor_class);
  dhcp_netid_free(opt->netid);
  free(opt->val);
  free(opt);
}

/* This is too insanely large to keep in-line in the switch */
static int parse_dhcp_opt(char *errstr, char *arg, int flags)
{
  struct dhcp_opt *new = opt_malloc(sizeof(struct dhcp_opt));
  char lenchar = 0, *cp;
  int addrs, digs, is_addr, is_addr6, is_hex, is_dec, is_string, dots;
  char *comma = NULL;
  u16 opt_len = 0;
  int is6 = 0;
  int option_ok = 0;

  new->len = 0;
  new->flags = flags;
  new->netid = NULL;
  new->val = NULL;
  new->opt = 0;
  
  while (arg)
    {
      comma = split(arg);      

      for (cp = arg; *cp; cp++)
	if (*cp < '0' || *cp > '9')
	  break;
      
      if (!*cp)
	{
	  new->opt = atoi(arg);
	  opt_len = 0;
	  option_ok = 1;
	  break;
	}
      
      if (strstr(arg, "option:") == arg)
	{
	  if ((new->opt = lookup_dhcp_opt(AF_INET, arg+7)) != -1)
	    {
	      opt_len = lookup_dhcp_len(AF_INET, new->opt);
	      /* option:<optname> must follow tag and vendor string. */
	      if (!(opt_len & OT_INTERNAL) || flags == DHOPT_MATCH)
		option_ok = 1;
	    }
	  break;
	}
#ifdef HAVE_DHCP6
      else if (strstr(arg, "option6:") == arg)
	{
	  for (cp = arg+8; *cp; cp++)
	    if (*cp < '0' || *cp > '9')
	      break;
	 
	  if (!*cp)
	    {
	      new->opt = atoi(arg+8);
	      opt_len = 0;
	      option_ok = 1;
	    }
	  else
	    {
	      if ((new->opt = lookup_dhcp_opt(AF_INET6, arg+8)) != -1)
		{
		  opt_len = lookup_dhcp_len(AF_INET6, new->opt);
		  if (!(opt_len & OT_INTERNAL) || flags == DHOPT_MATCH)
		    option_ok = 1;
		}
	    }
	  /* option6:<opt>|<optname> must follow tag and vendor string. */
	  is6 = 1;
	  break;
	}
#endif
      else if (strstr(arg, "vendor:") == arg)
	{
	  new->u.vendor_class = (unsigned char *)opt_string_alloc(arg+7);
	  new->flags |= DHOPT_VENDOR;
	  if ((new->flags & DHOPT_ENCAPSULATE) || flags == DHOPT_MATCH)
	    goto_err(_("inappropriate vendor:"));
	}
      else if (strstr(arg, "encap:") == arg)
	{
	  new->u.encap = atoi(arg+6);
	  new->flags |= DHOPT_ENCAPSULATE;
	  if ((new->flags & DHOPT_VENDOR) || flags == DHOPT_MATCH)
	    goto_err(_("inappropriate encap:"));
	}
      else if (strstr(arg, "vi-encap:") == arg)
	{
	  new->u.encap = atoi(arg+9);
	  new->flags |= DHOPT_RFC3925;
	  if (flags == DHOPT_MATCH)
	    {
	      option_ok = 1;
	      break;
	    }
	}
      else
	{
	  /* allow optional "net:" or "tag:" for consistency */
	  const char *name = (is_tag_prefix(arg)) ? arg+4 : set_prefix(arg);
	  new->netid = dhcp_netid_create(name, new->netid);
	}
      
      arg = comma; 
    }

#ifdef HAVE_DHCP6
  if (is6)
    {
      if (new->flags & (DHOPT_VENDOR | DHOPT_ENCAPSULATE))
	goto_err(_("unsupported encapsulation for IPv6 option"));
      
      if (opt_len == 0 &&
	  !(new->flags & DHOPT_RFC3925))
	opt_len = lookup_dhcp_len(AF_INET6, new->opt);
    }
  else
#endif
    if (opt_len == 0 &&
	!(new->flags & (DHOPT_VENDOR | DHOPT_ENCAPSULATE | DHOPT_RFC3925)))
      opt_len = lookup_dhcp_len(AF_INET, new->opt);
  
  /* option may be missing with rfc3925 match */
  if (!option_ok)
    goto_err(_("bad dhcp-option"));
  
  if (comma)
    {
      /* characterise the value */
      char c;
      int found_dig = 0, found_colon = 0;
      is_addr = is_addr6 = is_hex = is_dec = is_string = 1;
      addrs = digs = 1;
      dots = 0;
      for (cp = comma; (c = *cp); cp++)
	if (c == ',')
	  {
	    addrs++;
	    is_dec = is_hex = 0;
	  }
	else if (c == ':')
	  {
	    digs++;
	    is_dec = is_addr = 0;
	    found_colon = 1;
	  }
	else if (c == '/') 
	  {
	    is_addr6 = is_dec = is_hex = 0;
	    if (cp == comma) /* leading / means a pathname */
	      is_addr = 0;
	  } 
	else if (c == '.')	
	  {
	    is_dec = is_hex = 0;
	    dots++;
	  }
	else if (c == '-')
	  is_hex = is_addr = is_addr6 = 0;
	else if (c == ' ')
	  is_dec = is_hex = 0;
	else if (!(c >='0' && c <= '9'))
	  {
	    is_addr = 0;
	    if (cp[1] == 0 && is_dec &&
		(c == 'b' || c == 's' || c == 'i'))
	      {
		lenchar = c;
		*cp = 0;
	      }
	    else
	      is_dec = 0;
	    if (!((c >='A' && c <= 'F') ||
		  (c >='a' && c <= 'f') || 
		  (c == '*' && (flags & DHOPT_MATCH))))
	      {
		is_hex = 0;
		if (c != '[' && c != ']')
		  is_addr6 = 0;
	      }
	  }
	else
	  found_dig = 1;
     
      if (!found_dig)
	is_dec = is_addr = 0;

      if (!found_colon)
	is_addr6 = 0;

#ifdef HAVE_DHCP6
      /* NTP server option takes hex, addresses or FQDN */
      if (is6 && new->opt == OPTION6_NTP_SERVER && !is_hex)
	opt_len |= is_addr6 ? OT_ADDR_LIST : OT_RFC1035_NAME;
#endif
     
      /* We know that some options take addresses */
      if (opt_len & OT_ADDR_LIST)
	{
	  is_string = is_dec = is_hex = 0;
	  
	  if (!is6 && (!is_addr || dots == 0))
	    goto_err(_("bad IP address"));

	   if (is6 && !is_addr6)
	     goto_err(_("bad IPv6 address"));
	}
      /* or names */
      else if (opt_len & (OT_NAME | OT_RFC1035_NAME | OT_CSTRING))
	is_addr6 = is_addr = is_dec = is_hex = 0;
      
      if (found_dig && (opt_len & OT_TIME) && strlen(comma) > 0)
	{
	  int val, fac = 1;

	  switch (comma[strlen(comma) - 1])
	    {
	    case 'w':
	    case 'W':
	      fac *= 7;
	      /* fall through */
	    case 'd':
	    case 'D':
	      fac *= 24;
	      /* fall through */
	    case 'h':
	    case 'H':
	      fac *= 60;
	      /* fall through */
	    case 'm':
	    case 'M':
	      fac *= 60;
	      /* fall through */
	    case 's':
	    case 'S':
	      comma[strlen(comma) - 1] = 0;
	    }
	  
	  new->len = 4;
	  new->val = opt_malloc(4);
	  val = atoi(comma);
	  *((int *)new->val) = htonl(val * fac);	  
	}  
      else if (is_hex && digs > 1)
	{
	  new->len = digs;
	  new->val = opt_malloc(new->len);
	  parse_hex(comma, new->val, digs, (flags & DHOPT_MATCH) ? &new->u.wildcard_mask : NULL, NULL);
	  new->flags |= DHOPT_HEX;
	}
      else if (is_dec)
	{
	  int i, val = atoi(comma);
	  /* assume numeric arg is 1 byte except for
	     options where it is known otherwise.
	     For vendor class option, we have to hack. */
	  if (opt_len != 0)
	    new->len = opt_len;
	  else if (val & 0xffff0000)
	    new->len = 4;
	  else if (val & 0xff00)
	    new->len = 2;
	  else
	    new->len = 1;

	  if (lenchar == 'b')
	    new->len = 1;
	  else if (lenchar == 's')
	    new->len = 2;
	  else if (lenchar == 'i')
	    new->len = 4;
	  
	  new->val = opt_malloc(new->len);
	  for (i=0; i<new->len; i++)
	    new->val[i] = val>>((new->len - i - 1)*8);
	}
      else if (is_addr && !is6)	
	{
	  struct in_addr in;
	  unsigned char *op;
	  char *slash;
	  /* max length of address/subnet descriptor is five bytes,
	     add one for the option 120 enc byte too */
	  new->val = op = opt_malloc((5 * addrs) + 1);
	  new->flags |= DHOPT_ADDR;

	  if (!(new->flags & (DHOPT_ENCAPSULATE | DHOPT_VENDOR | DHOPT_RFC3925)) && 
	      new->opt == OPTION_SIP_SERVER)
	    {
	      *(op++) = 1; /* RFC 3361 "enc byte" */
	      new->flags &= ~DHOPT_ADDR;
	    }
	  while (addrs--) 
	    {
	      cp = comma;
	      comma = split(cp);
	      slash = split_chr(cp, '/');
	      if (!inet_pton(AF_INET, cp, &in))
		goto_err(_("bad IPv4 address"));
	      if (!slash)
		{
		  memcpy(op, &in, INADDRSZ);
		  op += INADDRSZ;
		}
	      else
		{
		  unsigned char *p = (unsigned char *)&in;
		  int netsize = atoi(slash);
		  *op++ = netsize;
		  if (netsize > 0)
		    *op++ = *p++;
		  if (netsize > 8)
		    *op++ = *p++;
		  if (netsize > 16)
		    *op++ = *p++;
		  if (netsize > 24)
		    *op++ = *p++;
		  new->flags &= ~DHOPT_ADDR; /* cannot re-write descriptor format */
		} 
	    }
	  new->len = op - new->val;
	}
      else if (is_addr6 && is6)
	{
	  unsigned char *op;
	  new->val = op = opt_malloc(16 * addrs);
	  new->flags |= DHOPT_ADDR6;
	  while (addrs--) 
	    {
	      cp = comma;
	      comma = split(cp);
	      
	      /* check for [1234::7] */
	      if (*cp == '[')
		cp++;
	      if (strlen(cp) > 1 && cp[strlen(cp)-1] == ']')
		cp[strlen(cp)-1] = 0;
	      
	      if (inet_pton(AF_INET6, cp, op))
		{
		  op += IN6ADDRSZ;
		  continue;
		}

	      goto_err(_("bad IPv6 address"));
	    } 
	  new->len = op - new->val;
	}
      else if (is_string)
	{
 	  /* text arg */
	  if ((new->opt == OPTION_DOMAIN_SEARCH || new->opt == OPTION_SIP_SERVER) &&
	      !is6 && !(new->flags & (DHOPT_ENCAPSULATE | DHOPT_VENDOR | DHOPT_RFC3925)))
	    {
	      /* dns search, RFC 3397, or SIP, RFC 3361 */
	      unsigned char *q, *r, *tail;
	      unsigned char *p, *m = NULL, *newp;
	      size_t newlen, len = 0;
	      int header_size = (new->opt == OPTION_DOMAIN_SEARCH) ? 0 : 1;
	      
	      arg = comma;
	      comma = split(arg);
	      
	      while (arg && *arg)
		{
		  char *in, *dom = NULL;
		  size_t domlen = 1;
		  /* Allow "." as an empty domain */
		  if (strcmp (arg, ".") != 0)
		    {
		      if (!(dom = canonicalise_opt(arg)))
			goto_err(_("bad domain in dhcp-option"));
			
		      domlen = strlen(dom) + 2;
		    }
		      
		  newp = opt_malloc(len + domlen + header_size);
		  if (m)
		    {
		      memcpy(newp, m, header_size + len);
		      free(m);
		    }
		  m = newp;
		  p = m + header_size;
		  q = p + len;
		  
		  /* add string on the end in RFC1035 format */
		  for (in = dom; in && *in;) 
		    {
		      unsigned char *cp = q++;
		      int j;
		      for (j = 0; *in && (*in != '.'); in++, j++)
			*q++ = *in;
		      *cp = j;
		      if (*in)
			in++;
		    }
		  *q++ = 0;
		  free(dom);
		  
		  /* Now tail-compress using earlier names. */
		  newlen = q - p;
		  for (tail = p + len; *tail; tail += (*tail) + 1)
		    for (r = p; r - p < (int)len; r += (*r) + 1)
		      if (strcmp((char *)r, (char *)tail) == 0)
			{
			  PUTSHORT((r - p) | 0xc000, tail); 
			  newlen = tail - p;
			  goto end;
			}
		end:
		  len = newlen;
		  
		  arg = comma;
		  comma = split(arg);
		}
      
	      /* RFC 3361, enc byte is zero for names */
	      if (new->opt == OPTION_SIP_SERVER && m)
		m[0] = 0;
	      new->len = (int) len + header_size;
	      new->val = m;
	    }
#ifdef HAVE_DHCP6
	  else if (comma && (opt_len & OT_CSTRING))
	    {
	      /* length fields are two bytes so need 16 bits for each string */
	      int i, commas = 1;
	      unsigned char *p, *newp;

	      for (i = 0; comma[i]; i++)
		if (comma[i] == ',')
		  commas++;
	      
	      newp = opt_malloc(strlen(comma)+(2*commas));	  
	      p = newp;
	      arg = comma;
	      comma = split(arg);
	      
	      while (arg && *arg)
		{
		  u16 len = strlen(arg);
		  unhide_metas(arg);
		  PUTSHORT(len, p);
		  memcpy(p, arg, len);
		  p += len; 

		  arg = comma;
		  comma = split(arg);
		}

	      new->val = newp;
	      new->len = p - newp;
	    }
	  else if (comma && (opt_len & OT_RFC1035_NAME))
	    {
	      unsigned char *p = NULL, *q, *newp, *end;
	      int len = 0;
	      int header_size = (is6 && new->opt == OPTION6_NTP_SERVER) ? 4 : 0;
	      arg = comma;
	      comma = split(arg);
	      
	      while (arg && *arg)
		{
		  char *dom = canonicalise_opt(arg);
		  if (!dom)
		    goto_err(_("bad domain in dhcp-option"));
		    		  
		  newp = opt_malloc(len + header_size + strlen(dom) + 2);
		  
		  if (p)
		    {
		      memcpy(newp, p, len);
		      free(p);
		    }
		  
		  p = newp;
		  q = p + len;
		  end = do_rfc1035_name(q + header_size, dom, NULL);
		  *end++ = 0;
		  if (is6 && new->opt == OPTION6_NTP_SERVER)
		    {
		      PUTSHORT(NTP_SUBOPTION_SRV_FQDN, q);
		      PUTSHORT(end - q - 2, q);
		    }
		  len = end - p;
		  free(dom);

		  arg = comma;
		  comma = split(arg);
		}
	      
	      new->val = p;
	      new->len = len;
	    }
#endif
	  else
	    {
	      new->len = strlen(comma);
	      /* keep terminating zero on string */
	      new->val = (unsigned char *)opt_string_alloc(comma);
	      new->flags |= DHOPT_STRING;
	    }
	}
    }

  if (!is6 && 
      ((new->len > 255) || 
      (new->len > 253 && (new->flags & (DHOPT_VENDOR | DHOPT_ENCAPSULATE))) ||
       (new->len > 250 && (new->flags & DHOPT_RFC3925))))
    goto_err(_("dhcp-option too long"));

  if (flags == DHOPT_PXE_OPT &&  (new->flags & DHOPT_VENDOR))
    goto_err(_("No vendor-encap options allowed in dhcp-option-pxe")); 
      
  if (flags == DHOPT_MATCH)
    {
      if ((new->flags & (DHOPT_ENCAPSULATE | DHOPT_VENDOR)) ||
	  !new->netid ||
	  new->netid->next)
	goto_err(_("illegal dhcp-match"));
       
      if (is6)
	{
	  new->next = daemon->dhcp_match6;
	  daemon->dhcp_match6 = new;
	}
      else
	{
	  new->next = daemon->dhcp_match;
	  daemon->dhcp_match = new;
	}
    }
  else if (is6)
    {
      new->next = daemon->dhcp_opts6;
      daemon->dhcp_opts6 = new;
    }
  else
    {
      new->next = daemon->dhcp_opts;
      daemon->dhcp_opts = new;
    }
    
  return 1;
on_error:
  dhcp_opt_free(new);
  return 0;
}

#endif

void set_option_bool(unsigned int opt)
{
  option_var(opt) |= option_val(opt);
}

void reset_option_bool(unsigned int opt)
{
  option_var(opt) &= ~(option_val(opt));
}

static int one_opt(int option, char *arg, char *errstr, char *gen_err, int command_line, int servers_only)
{      
  int i;
  char *comma;

  if (option == '?')
    ret_err(gen_err);
  
  for (i=0; usage[i].opt != 0; i++)
    if (usage[i].opt == option)
      {
	 int rept = usage[i].rept;
	 
	 if (command_line)
	   {
	     /* command line */
	     if (rept == ARG_USED_CL)
	       ret_err(_("illegal repeated flag"));
	     if (rept == ARG_ONE)
	       usage[i].rept = ARG_USED_CL;
	   }
	 else
	   {
	     /* allow file to override command line */
	     if (rept == ARG_USED_FILE)
	       ret_err(_("illegal repeated keyword"));
	     if (rept == ARG_USED_CL || rept == ARG_ONE)
	       usage[i].rept = ARG_USED_FILE;
	   }

	 if (rept != ARG_DUP && rept != ARG_ONE && rept != ARG_USED_CL) 
	   {
	     set_option_bool(rept);
	     return 1;
	   }
       
	 break;
      }
  
  switch (option)
    { 
    case 'C': /* --conf-file */
      {
	char *file = opt_string_alloc(arg);
	if (file)
	  {
	    one_file(file, 0);
	    free(file);
	  }
	break;
      }

    case LOPT_CONF_SCRIPT: /* --conf-script */
      {
	char *file = opt_string_alloc(arg);
	if (file)
	  {
	    one_file(file, LOPT_CONF_SCRIPT);
	    free(file);
	  }
	break;
      }

    case '7': /* --conf-dir */	      
      {
	DIR *dir_stream;
	struct dirent *ent;
	char *directory, *path;
	struct list {
	  char *name;
	  struct list *next;
	} *ignore_suffix = NULL, *match_suffix = NULL, *files = NULL, *li;
	
	comma = split(arg);
	if (!(directory = opt_string_alloc(arg)))
	  break;
	
	for (arg = comma; arg; arg = comma) 
	  {
	    comma = split(arg);
	    if (strlen(arg) != 0)
	      {
		li = opt_malloc(sizeof(struct list));
		if (*arg == '*')
		  {
		    /* "*" with no suffix is a no-op */
		    if (arg[1] == 0)
		      free(li);
		    else
		      {
			li->next = match_suffix;
			match_suffix = li;
			/* Have to copy: buffer is overwritten */
			li->name = opt_string_alloc(arg+1);
		      }
		  }
		else
		  {
		    li->next = ignore_suffix;
		    ignore_suffix = li;
		    /* Have to copy: buffer is overwritten */
		    li->name = opt_string_alloc(arg);
		  }
	      }
	  }
	
	if (!(dir_stream = opendir(directory)))
	  die(_("cannot access directory %s: %s"), directory, EC_FILE);
	
	while ((ent = readdir(dir_stream)))
	  {
	    size_t len = strlen(ent->d_name);
	    struct stat buf;
	    
	    /* ignore emacs backups and dotfiles */
	    if (len == 0 ||
		ent->d_name[len - 1] == '~' ||
		(ent->d_name[0] == '#' && ent->d_name[len - 1] == '#') ||
		ent->d_name[0] == '.')
	      continue;

	    if (match_suffix)
	      {
		for (li = match_suffix; li; li = li->next)
		  {
		    /* check for required suffices */
		    size_t ls = strlen(li->name);
		    if (len > ls &&
			strcmp(li->name, &ent->d_name[len - ls]) == 0)
		      break;
		  }
		if (!li)
		  continue;
	      }
	    
	    for (li = ignore_suffix; li; li = li->next)
	      {
		/* check for proscribed suffices */
		size_t ls = strlen(li->name);
		if (len > ls &&
		    strcmp(li->name, &ent->d_name[len - ls]) == 0)
		  break;
	      }
	    if (li)
	      continue;
	    
	    path = opt_malloc(strlen(directory) + len + 2);
	    strcpy(path, directory);
	    strcat(path, "/");
	    strcat(path, ent->d_name);

	    /* files must be readable */
	    if (stat(path, &buf) == -1)
	      die(_("cannot access %s: %s"), path, EC_FILE);
	    
	    /* only reg files allowed. */
	    if (S_ISREG(buf.st_mode))
	      {
		/* sort files into order. */
		struct list **up, *new = opt_malloc(sizeof(struct list));
		new->name = path;
		
		for (up = &files, li = files; li; up = &li->next, li = li->next)
		  if (strcmp(li->name, path) >=0)
		    break;

		new->next = li;
		*up = new;
	      }
	    else
	      free(path);

	  }

	for (li = files; li; li = li->next)
	  one_file(li->name, 0);
      	
	closedir(dir_stream);
	free(directory);
	for(; ignore_suffix; ignore_suffix = li)
	  {
	    li = ignore_suffix->next;
	    free(ignore_suffix->name);
	    free(ignore_suffix);
	  }
	for(; match_suffix; match_suffix = li)
	  {
	    li = match_suffix->next;
	    free(match_suffix->name);
	    free(match_suffix);
	  }
	for(; files; files = li)
	  {
	    li = files->next;
	    free(files->name);
	    free(files);
	  }
	break;
      }

    case LOPT_ADD_SBNET: /* --add-subnet */
      set_option_bool(OPT_CLIENT_SUBNET);
      if (arg)
	{
          char *err, *end;
	  comma = split(arg);

          struct mysubnet* new = opt_malloc(sizeof(struct mysubnet));
          if ((end = split_chr(arg, '/')))
	    {
	      /* has subnet+len */
	      err = parse_mysockaddr(arg, &new->addr);
	      if (err)
		ret_err_free(err, new);
	      if (!atoi_check(end, &new->mask))
		ret_err_free(gen_err, new);
	      new->addr_used = 1;
	    } 
	  else if (!atoi_check(arg, &new->mask))
	    ret_err_free(gen_err, new);
	    
          daemon->add_subnet4 = new;

          if (comma)
            {
	      new = opt_malloc(sizeof(struct mysubnet));
	      if ((end = split_chr(comma, '/')))
		{
		  /* has subnet+len */
                  err = parse_mysockaddr(comma, &new->addr);
                  if (err)
                    ret_err_free(err, new);
                  if (!atoi_check(end, &new->mask))
                    ret_err_free(gen_err, new);
                  new->addr_used = 1;
                }
              else
                {
                  if (!atoi_check(comma, &new->mask))
                    ret_err_free(gen_err, new);
                }
          
	      daemon->add_subnet6 = new;
	    }
	}
      break;

    case '1': /* --enable-dbus */
      set_option_bool(OPT_DBUS);
      if (arg)
	daemon->dbus_name = opt_string_alloc(arg);
      else
	daemon->dbus_name = DNSMASQ_SERVICE;
      break;

    case LOPT_UBUS: /* --enable-ubus */
      set_option_bool(OPT_UBUS);
      if (arg)
	daemon->ubus_name = opt_string_alloc(arg);
      else
	daemon->ubus_name = DNSMASQ_UBUS_NAME;
      break;

    case '8': /* --log-facility */
      /* may be a filename */
      if (strchr(arg, '/') || strcmp (arg, "-") == 0)
	daemon->log_file = opt_string_alloc(arg);
      else
	{	  
#ifdef __ANDROID__
	  ret_err(_("setting log facility is not possible under Android"));
#else
	  for (i = 0; facilitynames[i].c_name; i++)
	    if (hostname_isequal((char *)facilitynames[i].c_name, arg))
	      break;
	  
	  if (facilitynames[i].c_name)
	    daemon->log_fac = facilitynames[i].c_val;
	  else
	    ret_err(_("bad log facility"));
#endif
	}
      break;

    case 'x': /* --pid-file */
      daemon->runfile = opt_string_alloc(arg);
      break;

    case 'r': /* --resolv-file */
      {
	char *name = opt_string_alloc(arg);
	struct resolvc *new, *list = daemon->resolv_files;
	
	if (list && list->is_default)
	  {
	    /* replace default resolv file - possibly with nothing */
	    if (name)
	      {
		list->is_default = 0;
		list->name = name;
	      }
	    else
	      list = NULL;
	  }
	else if (name)
	  {
	    new = opt_malloc(sizeof(struct resolvc));
	    new->next = list;
	    new->name = name;
	    new->is_default = 0;
	    new->mtime = 0;
	    new->logged = 0;
	    list = new;
	  }
	daemon->resolv_files = list;
	break;
      }

    case LOPT_SERVERS_FILE:
      daemon->servers_file = opt_string_alloc(arg);
      break;
      
    case 'm':  /* --mx-host */
      {
	int pref = 1;
	struct mx_srv_record *new;
	char *name, *target = NULL;

	if ((comma = split(arg)))
	  {
	    char *prefstr;
	    if ((prefstr = split(comma)) && !atoi_check16(prefstr, &pref))
	      ret_err(_("bad MX preference"));
	  }
	
	if (!(name = canonicalise_opt(arg)) || 
	    (comma && !(target = canonicalise_opt(comma))))
	  {
	    free(name);
	    free(target);
	    ret_err(_("bad MX name"));
	  }
	
	new = opt_malloc(sizeof(struct mx_srv_record));
	new->next = daemon->mxnames;
	daemon->mxnames = new;
	new->issrv = 0;
	new->name = name;
	new->target = target; /* may be NULL */
	new->weight = pref;
	break;
      }
      
    case 't': /*  --mx-target */
      if (!(daemon->mxtarget = canonicalise_opt(arg)))
	ret_err(_("bad MX target"));
      break;

    case LOPT_DUMPFILE:  /* --dumpfile */
      daemon->dump_file = opt_string_alloc(arg);
      break;

    case LOPT_DUMPMASK:  /* --dumpmask */
      daemon->dump_mask = strtol(arg, NULL, 0);
      break;
      
#ifdef HAVE_DHCP      
    case 'l':  /* --dhcp-leasefile */
      daemon->lease_file = opt_string_alloc(arg);
      break;
      
      /* Sorry about the gross pre-processor abuse */
    case '6':             /* --dhcp-script */
    case LOPT_LUASCRIPT:  /* --dhcp-luascript */
#  if !defined(HAVE_SCRIPT)
      ret_err(_("recompile with HAVE_SCRIPT defined to enable lease-change scripts"));
#  else
      if (option == LOPT_LUASCRIPT)
#    if !defined(HAVE_LUASCRIPT)
	ret_err(_("recompile with HAVE_LUASCRIPT defined to enable Lua scripts"));
#    else
        daemon->luascript = opt_string_alloc(arg);
#    endif
      else
        daemon->lease_change_command = opt_string_alloc(arg);
#  endif
      break;
#endif /* HAVE_DHCP */

    case LOPT_DHCP_HOST:     /* --dhcp-hostsfile */
    case LOPT_DHCP_OPTS:     /* --dhcp-optsfile */
    case 'H':                /* --addn-hosts */
      {
	struct hostsfile *new = opt_malloc(sizeof(struct hostsfile));
	new->fname = opt_string_alloc(arg);
	new->index = daemon->host_index++;
	new->flags = 0;
	if (option == 'H')
	  {
	    new->next = daemon->addn_hosts;
	    daemon->addn_hosts = new;
	  }
	else if (option == LOPT_DHCP_HOST)
	  {
	    new->next = daemon->dhcp_hosts_file;
	    daemon->dhcp_hosts_file = new;
	  }
	else if (option == LOPT_DHCP_OPTS)
	  {
	    new->next = daemon->dhcp_opts_file;
	    daemon->dhcp_opts_file = new;
	  }
	
	break;
      }

    case LOPT_DHCP_INOTIFY:  /* --dhcp-hostsdir */
    case LOPT_DHOPT_INOTIFY: /* --dhcp-optsdir */
    case LOPT_HOST_INOTIFY:  /* --hostsdir */
      {
	struct dyndir *new = opt_malloc(sizeof(struct dyndir));
	new->dname = opt_string_alloc(arg);
	new->flags = 0;
	new->next = daemon->dynamic_dirs;
	daemon->dynamic_dirs = new; 
	if (option == LOPT_DHCP_INOTIFY)
	new->flags |= AH_DHCP_HST;
	else if (option == LOPT_DHOPT_INOTIFY)
	new->flags |= AH_DHCP_OPT;
	else if (option == LOPT_HOST_INOTIFY)
	new->flags |= AH_HOSTS;

	break;
      }
      
    case LOPT_AUTHSERV: /* --auth-server */
      comma = split(arg);
      
      daemon->authserver = opt_string_alloc(arg);
      
      while ((arg = comma))
	{
	  struct iname *new = opt_malloc(sizeof(struct iname));
	  comma = split(arg);
	  new->name = NULL;
	  unhide_metas(arg);
	  if (inet_pton(AF_INET, arg, &new->addr.in.sin_addr) > 0)
	    new->addr.sa.sa_family = AF_INET;
	  else if (inet_pton(AF_INET6, arg, &new->addr.in6.sin6_addr) > 0)
	    new->addr.sa.sa_family = AF_INET6;
	  else
	    {
	      char *fam = split_chr(arg, '/');
	      new->name = opt_string_alloc(arg);
	      new->addr.sa.sa_family = 0;
	      if (fam)
		{
		  if (strcmp(fam, "4") == 0)
		    new->addr.sa.sa_family = AF_INET;
		  else if (strcmp(fam, "6") == 0)
		    new->addr.sa.sa_family = AF_INET6;
		  else
		  {
		    free(new->name);
		    ret_err_free(gen_err, new);
		  }
		} 
	    }
	  new->next = daemon->authinterface;
	  daemon->authinterface = new;
	};
            
      break;

    case LOPT_AUTHSFS: /* --auth-sec-servers */
      {
	struct name_list *new;

	do {
	  comma = split(arg);
	  new = opt_malloc(sizeof(struct name_list));
	  new->name = opt_string_alloc(arg);
	  new->next = daemon->secondary_forward_server;
	  daemon->secondary_forward_server = new;
	  arg = comma;
	} while (arg);
	break;
      }
	
    case LOPT_AUTHZONE: /* --auth-zone */
      {
	struct auth_zone *new;
	
	comma = split(arg);
		
	new = opt_malloc(sizeof(struct auth_zone));
	new->domain = canonicalise_opt(arg);
	if (!new->domain)
	  ret_err_free(_("invalid auth-zone"), new);
 	new->subnet = NULL;
	new->exclude = NULL;
	new->interface_names = NULL;
	new->next = daemon->auth_zones;
	daemon->auth_zones = new;

	while ((arg = comma))
	  {
	    int prefixlen = 0;
	    int is_exclude = 0;
	    char *prefix;
	    struct addrlist *subnet =  NULL;
	    union all_addr addr;

	    comma = split(arg);
	    prefix = split_chr(arg, '/');
	    
	    if (prefix && !atoi_check(prefix, &prefixlen))
	      ret_err(gen_err);
	    
	    if (strstr(arg, "exclude:") == arg)
	      {
		    is_exclude = 1;
		    arg = arg+8;
	      }

	    if (inet_pton(AF_INET, arg, &addr.addr4))
	      {
		subnet = opt_malloc(sizeof(struct addrlist));
		subnet->prefixlen = (prefixlen == 0) ? 24 : prefixlen;
		subnet->flags = ADDRLIST_LITERAL;
	      }
	    else if (inet_pton(AF_INET6, arg, &addr.addr6))
	      {
		subnet = opt_malloc(sizeof(struct addrlist));
		subnet->prefixlen = (prefixlen == 0) ? 64 : prefixlen;
		subnet->flags = ADDRLIST_LITERAL | ADDRLIST_IPV6;
	      }
	    else 
	      {
		struct auth_name_list *name =  opt_malloc(sizeof(struct auth_name_list));
		name->name = opt_string_alloc(arg);
		name->flags = AUTH4 | AUTH6;
		name->next = new->interface_names;
		new->interface_names = name;
		if (prefix)
		  {
		    if (prefixlen == 4)
		      name->flags &= ~AUTH6;
		    else if (prefixlen == 6)
		      name->flags &= ~AUTH4;
		    else
		      ret_err(gen_err);
		  }
	      }
	    
	    if (subnet)
	      {
		subnet->addr = addr;

		if (is_exclude)
		  {
		    subnet->next = new->exclude;
		    new->exclude = subnet;
		  }
		else
		  {
		    subnet->next = new->subnet;
		    new->subnet = subnet;
		  }
	      }
	  }
	break;
      }
      
    case  LOPT_AUTHSOA: /* --auth-soa */
      comma = split(arg);
      daemon->soa_sn = (u32)atoi(arg);
      if (comma)
	{
	  char *cp;
	  arg = comma;
	  comma = split(arg);
	  daemon->hostmaster = opt_string_alloc(arg);
	  for (cp = daemon->hostmaster; cp && *cp; cp++)
	    if (*cp == '@')
	      *cp = '.';

	  if (comma)
	    {
	      arg = comma;
	      comma = split(arg); 
	      daemon->soa_refresh = (u32)atoi(arg);
	      if (comma)
		{
		  arg = comma;
		  comma = split(arg); 
		  daemon->soa_retry = (u32)atoi(arg);
		  if (comma)
		    daemon->soa_expiry = (u32)atoi(comma);
		}
	    }
	}

      break;

    case 's':         /* --domain */
    case LOPT_SYNTH:  /* --synth-domain */
      {
	char *d, *d_raw = arg;
	comma = split(arg);
	if (!(d = canonicalise_opt(d_raw)))
	  ret_err(gen_err);
	else
	  {
	    free(d); /* allocate this again below. */
	    if (comma)
	      {
		struct cond_domain *new = opt_malloc(sizeof(struct cond_domain));
		char *netpart;
		
		new->prefix = NULL;
		new->indexed = 0;
		new->prefixlen = 0;
		
		unhide_metas(comma);
		if ((netpart = split_chr(comma, '/')))
		  {
		    int msize;
		    
		    arg = split(netpart);
		    if (!atoi_check(netpart, &msize))
		      ret_err_free(gen_err, new);
		    else if (inet_pton(AF_INET, comma, &new->start))
		      {
			int mask;
			
			if (msize > 32)
			  ret_err_free(_("bad prefix length"), new);
			
			mask = (1 << (32 - msize)) - 1;
			new->is6 = 0; 			  
			new->start.s_addr = ntohl(htonl(new->start.s_addr) & ~mask);
			new->end.s_addr = new->start.s_addr | htonl(mask);
			if (arg)
			  {
			    if (option != 's')
			      {
				/* IPv6 address is longest and represented as
				   xxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxx
				   which is 39 chars */
				if (!(new->prefix = canonicalise_opt(arg)) ||
				    strlen(new->prefix) > (MAXLABEL - 39))
				  ret_err_free(_("bad prefix"), new);
			      }
			    else if (strcmp(arg, "local") != 0)
			      ret_err_free(gen_err, new);
			    else
			      {
				/* local=/xxx.yyy.zzz.in-addr.arpa/ */
				domain_rev4(0, NULL, &new->start, msize);
				
				/* local=/<domain>/ */
				/* d_raw can't failed to canonicalise here, checked above. */
				add_update_server(SERV_LITERAL_ADDRESS, NULL, NULL, NULL, d_raw, NULL);
			      }
			  }
		      }
		    else if (inet_pton(AF_INET6, comma, &new->start6))
		      {
			u64 mask, addrpart = addr6part(&new->start6);
			
			if (msize > 128)
			  ret_err_free(_("bad prefix length"), new);
			
			/* prefix==64 overflows the mask calculation */
			if (msize <= 64)
			  mask = (u64)-1LL;
			else
			  mask = (1LLU << (128 - msize)) - 1LLU;
			
			new->is6 = 1;
			new->prefixlen = msize;
			
			new->end6 = new->start6;
			setaddr6part(&new->start6, addrpart & ~mask);
			setaddr6part(&new->end6, addrpart | mask);
			
			if (arg)
			  {
			    if (option != 's')
			      {
				if (!(new->prefix = canonicalise_opt(arg)) ||
				    strlen(new->prefix) > MAXLABEL - INET6_ADDRSTRLEN)
				  ret_err_free(_("bad prefix"), new);
			      }	
			    else if (strcmp(arg, "local") != 0)
			      ret_err_free(gen_err, new);
			    else 
			      {
				/* generate the equivalent of
				   local=/xxx.yyy.zzz.ip6.arpa/ */
				domain_rev6(0, NULL, &new->start6, msize);
				
				/* local=/<domain>/ */
				/* d_raw can't failed to canonicalise here, checked above. */
				add_update_server(SERV_LITERAL_ADDRESS, NULL, NULL, NULL, d_raw, NULL);
			      }
			  }
		      }
		    else
		      ret_err_free(gen_err, new);
		  }
		else
		  {
		    char *prefstr;
		    arg = split(comma);
		    prefstr = split(arg);
		    
		    if (inet_pton(AF_INET, comma, &new->start))
		      {
			new->is6 = 0;
			if (!arg)
			  new->end.s_addr = new->start.s_addr;
			else if (!inet_pton(AF_INET, arg, &new->end))
			  ret_err_free(gen_err, new);
		      }
		    else if (inet_pton(AF_INET6, comma, &new->start6))
		      {
			new->is6 = 1;
			if (!arg)
			  memcpy(&new->end6, &new->start6, IN6ADDRSZ);
			else if (!inet_pton(AF_INET6, arg, &new->end6))
			  ret_err_free(gen_err, new);
		      }
		    else if (option == 's')
		      {
			/* subnet from interface. */
			new->interface = opt_string_alloc(comma);
			new->al = NULL;
		      }
		    else
		      ret_err_free(gen_err, new);
		    
		    if (option != 's' && prefstr)
		      {
			if (!(new->prefix = canonicalise_opt(prefstr)) ||
			    strlen(new->prefix) > MAXLABEL - INET_ADDRSTRLEN)
			  ret_err_free(_("bad prefix"), new);
		      }
		  }
		
		new->domain = canonicalise_opt(d_raw);
		if (option  == 's')
		  {
		    new->next = daemon->cond_domain;
		    daemon->cond_domain = new;
		  }
		else
		  {
		    char *star;
		    if (new->prefix &&
			(star = strrchr(new->prefix, '*'))
			&& *(star+1) == 0)
		      {
			*star = 0;
			new->indexed = 1;
			if (new->is6 && new->prefixlen < 64)
			  ret_err_free(_("prefix length too small"), new);
		      }
		    new->next = daemon->synth_domains;
		    daemon->synth_domains = new;
		  }
	      }
	    else if (option == 's')
	      {
		if (strcmp (arg, "#") == 0)
		  set_option_bool(OPT_RESOLV_DOMAIN);
		else
		  daemon->domain_suffix = canonicalise_opt(d_raw);
	      }
	    else 
	      ret_err(gen_err);
	  }
	
	break;
      }
      
    case LOPT_CPE_ID: /* --add-dns-client */
      if (arg)
	daemon->dns_client_id = opt_string_alloc(arg);
      break;

    case LOPT_UMBRELLA: /* --umbrella */
      set_option_bool(OPT_UMBRELLA);
      while (arg)
	{
	  comma = split(arg);
	  if (strstr(arg, "deviceid:"))
	    {
	      char *p;
	      u8 *u = daemon->umbrella_device;
	      char word[3];
	      
	      arg += 9;
	      if (strlen(arg) != 16)
		ret_err(gen_err);
	      
	      for (p = arg; *p; p++)
		if (!isxdigit((unsigned char)*p))
		  ret_err(gen_err);
	      
	      set_option_bool(OPT_UMBRELLA_DEVID);
	      
	      for (i = 0; i < (int)sizeof(daemon->umbrella_device); i++, arg+=2)
		{
		  memcpy(word, &(arg[0]), 2);
		  *u++ = strtoul(word, NULL, 16);
		}
	    }
	  else if (strstr(arg, "orgid:"))
	    {
	      if (!strtoul_check(arg+6, &daemon->umbrella_org))
		ret_err(gen_err);
	    }
	  else if (strstr(arg, "assetid:"))
	    {
	      if (!strtoul_check(arg+8, &daemon->umbrella_asset))
		ret_err(gen_err);
	    }
	  else
	    ret_err(gen_err);
	  
	  arg = comma;
	}
      break;
      
    case LOPT_ADD_MAC: /* --add-mac */
      if (!arg)
	set_option_bool(OPT_ADD_MAC);
      else
	{
	  unhide_metas(arg);
	  if (strcmp(arg, "base64") == 0)
	    set_option_bool(OPT_MAC_B64);
	  else if (strcmp(arg, "text") == 0)
	    set_option_bool(OPT_MAC_HEX);
	  else
	    ret_err(gen_err);
	}
      break;

    case 'u':  /* --user */
      daemon->username = opt_string_alloc(arg);
      break;
      
    case 'g':  /* --group */
      daemon->groupname = opt_string_alloc(arg);
      daemon->group_set = 1;
      break;

#ifdef HAVE_DHCP
    case LOPT_SCRIPTUSR: /* --scriptuser */
      daemon->scriptuser = opt_string_alloc(arg);
      break;
#endif
      
    case 'i':  /* --interface */
      do {
        comma = split(arg);
	if_names_add(arg);
	arg = comma;
      } while (arg);
      break;
      
    case LOPT_TFTP: /* --enable-tftp */
      set_option_bool(OPT_TFTP);
      if (!arg)
	break;
      /* fall through */

    case 'I':  /* --except-interface */
    case '2':  /* --no-dhcp-interface */
    case LOPT_NO_DHCP6: /* --no-dhcpv6-interface */
    case LOPT_NO_DHCP4: /* --no-dhcpv4-interface */
      do {
	struct iname *new = opt_malloc(sizeof(struct iname));
	comma = split(arg);
	new->name = opt_string_alloc(arg);
	new->flags = INAME_4 | INAME_6;
	if (option == 'I')
	  {
	    new->next = daemon->if_except;
	    daemon->if_except = new;
	  }
	else if (option == LOPT_TFTP)
	   {
	    new->next = daemon->tftp_interfaces;
	    daemon->tftp_interfaces = new;
	  }
	else
	  {
	    if (option == LOPT_NO_DHCP6)
	      new->flags &= ~INAME_4;
	    if (option == LOPT_NO_DHCP4)
	      new->flags &= ~INAME_6;
	    new->next = daemon->dhcp_except;
	    daemon->dhcp_except = new;
	  }
	arg = comma;
      } while (arg);
      break;

#ifdef HAVE_DHCP
# if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
# endif
    case LOPT_LEASEQUERY:
      set_option_bool(OPT_LEASEQUERY);
      if (!arg)
	break;
# if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
# pragma GCC diagnostic pop
# endif
#endif
    case 'B':  /* --bogus-nxdomain */
    case LOPT_IGNORE_ADDR: /* --ignore-address */
     {
	union all_addr addr;
	int prefix, is6 = 0;
	struct bogus_addr *baddr;
	
	unhide_metas(arg);

	if (!arg ||
	    ((comma = split_chr(arg, '/')) && !atoi_check(comma, &prefix)))
	  ret_err(gen_err);

	if (inet_pton(AF_INET6, arg, &addr.addr6) == 1)
	  is6 = 1;
	else if (inet_pton(AF_INET, arg, &addr.addr4) != 1)
	  ret_err(gen_err);

	if (!comma)
	  {
	    if (is6)
	      prefix = 128;
	    else
	      prefix = 32;
	  }

	if (prefix > 128 || (!is6 && prefix > 32))
	  ret_err(gen_err);
	
	baddr = opt_malloc(sizeof(struct bogus_addr));
	if (option == 'B')
	  {
	    baddr->next = daemon->bogus_addr;
	    daemon->bogus_addr = baddr;
	  }
#ifdef HAVE_DHCP
	else if (option == LOPT_LEASEQUERY)
	  {
	    baddr->next = daemon->leasequery_addr;
	    daemon->leasequery_addr = baddr;
	  }
#endif
	else
	  {
	    baddr->next = daemon->ignore_addr;
	    daemon->ignore_addr = baddr;
	  }

	baddr->prefix = prefix;
	baddr->is6 = is6;
	baddr->addr = addr;
	break;
     }
      
    case 'a':  /* --listen-address */
    case LOPT_AUTHPEER: /* --auth-peer */
      do {
	struct iname *new = opt_malloc(sizeof(struct iname));
	comma = split(arg);
	unhide_metas(arg);
	if (arg && (inet_pton(AF_INET, arg, &new->addr.in.sin_addr) > 0))
	  {
	    new->addr.sa.sa_family = AF_INET;
	    new->addr.in.sin_port = 0;
#ifdef HAVE_SOCKADDR_SA_LEN
	    new->addr.in.sin_len = sizeof(new->addr.in);
#endif
	  }
	else if (arg && inet_pton(AF_INET6, arg, &new->addr.in6.sin6_addr) > 0)
	  {
	    new->addr.sa.sa_family = AF_INET6;
	    new->addr.in6.sin6_flowinfo = 0;
	    new->addr.in6.sin6_scope_id = 0;
	    new->addr.in6.sin6_port = 0;
#ifdef HAVE_SOCKADDR_SA_LEN
	    new->addr.in6.sin6_len = sizeof(new->addr.in6);
#endif
	  }
	else
	  ret_err_free(gen_err, new);

	new->flags = 0;
	if (option == 'a')
	  {
	    new->next = daemon->if_addrs;
	    daemon->if_addrs = new;
	  }
	else
	  {
	    new->next = daemon->auth_peers;
	    daemon->auth_peers = new;
	  } 
	arg = comma;
      } while (arg);
      break;
      
    case LOPT_NO_REBIND: /*  --rebind-domain-ok */
      {
	struct rebind_domain *new;

	unhide_metas(arg);

	if (*arg == '/')
	  arg++;
	
	do {
	  comma = split_chr(arg, '/');
	  new = opt_malloc(sizeof(struct  rebind_domain));
	  new->domain = canonicalise_opt(arg);
	  new->next = daemon->no_rebind;
	  daemon->no_rebind = new;
	  arg = comma;
	} while (arg && *arg);

	break;
      }
      
    case 'S':            /*  --server */
    case LOPT_LOCAL:     /*  --local */
    case 'A':            /*  --address */
      {
	char *lastdomain = NULL, *domain = "", *cur_domain;
	u16 flags = 0;
	char *err;
	union all_addr addr;
	union mysockaddr serv_addr, source_addr;
	char interface[IF_NAMESIZE+1];
	struct server_details sdetails;

	memset(&sdetails, 0, sizeof(struct server_details));
	sdetails.addr = &serv_addr;
	sdetails.source_addr = &source_addr;
	sdetails.interface = interface;
	sdetails.flags = &flags;
			
	unhide_metas(arg);
	
	/* split the domain args, if any and skip to the end of them. */
	if (arg && *arg == '/')
	  {
	    char *last;

	    domain = lastdomain = ++arg;
	    
	    while ((last = split_chr(arg, '/')))
	      {
		lastdomain = arg;
		arg = last;
	      }
	  }
	
	if (!arg || !*arg)
	  flags = SERV_LITERAL_ADDRESS;
	else if (option == 'A')
	  {
	    /* # as literal address means return zero address for 4 and 6 */
	    if (strcmp(arg, "#") == 0)
	      flags = SERV_ALL_ZEROS | SERV_LITERAL_ADDRESS;
	    else if (inet_pton(AF_INET, arg, &addr.addr4) > 0)
	      flags = SERV_4ADDR | SERV_LITERAL_ADDRESS;
	    else if (inet_pton(AF_INET6, arg, &addr.addr6) > 0)
	      flags = SERV_6ADDR | SERV_LITERAL_ADDRESS;
	    else
	      ret_err(_("Bad address in --address"));
	  }
	else
	  {
	    if ((err = parse_server(arg, &sdetails)))
	      ret_err(err);
	  }

	if (servers_only && option == 'S')
	  flags |= SERV_FROM_FILE;

	cur_domain = domain;
	while ((flags & SERV_LITERAL_ADDRESS) || parse_server_next(&sdetails))
	  {
	    cur_domain = domain;

	    if (!(flags & SERV_LITERAL_ADDRESS) && (err = parse_server_addr(&sdetails)))
	      ret_err(err);

	    /* When source is set only use DNS records of the same type and skip all others */
	    if (flags & SERV_HAS_SOURCE && sdetails.addr_type != sdetails.source_addr->sa.sa_family)
	      continue;

	    while (1)
	      {
		/* server=//1.2.3.4 is special. */
		if (lastdomain)
		  {
		    if (strlen(cur_domain) == 0)
		      flags |= SERV_FOR_NODOTS;
		    else
		      flags &= ~SERV_FOR_NODOTS;
		    
		    /* address=/#/ matches the same as without domain, as does server=/#/.... for consistency. */
		    if (cur_domain[0] == '#' && cur_domain[1] == 0)
		      cur_domain[0] = 0;
		  }
		
		if (!add_update_server(flags, sdetails.addr, sdetails.source_addr, sdetails.interface, cur_domain, &addr))
		  ret_err(gen_err);
		
		if (!lastdomain || cur_domain == lastdomain)
		  break;

		cur_domain += strlen(cur_domain) + 1;
	      }

	    if (flags & SERV_LITERAL_ADDRESS)
	      break;
	  }

	if (sdetails.orig_hostinfo)
	  freeaddrinfo(sdetails.orig_hostinfo);
	
     	break;
      }

    case LOPT_REV_SERV: /* --rev-server */
      {
	char *string;
	int size;
	struct in_addr addr4;
	struct in6_addr addr6;
 	
	unhide_metas(arg);
	if (!arg)
	  ret_err(gen_err);
	
	comma=split(arg);
	
	if (!(string = split_chr(arg, '/')) || !atoi_check(string, &size))
	  size = -1;

	if (inet_pton(AF_INET, arg, &addr4))
	  {
	   if (size == -1)
	     size = 32;

	   if ((string = domain_rev4(servers_only, comma, &addr4, size)))
	      ret_err(string);
	  }
	else if (inet_pton(AF_INET6, arg, &addr6))
	  {
	     if (size == -1)
	       size = 128;

	     if ((string = domain_rev6(servers_only, comma, &addr6, size)))
	      ret_err(string);
	  }
	else
	  ret_err(gen_err);
	
	break;
      }

    case LOPT_IPSET: /* --ipset */
    case LOPT_NFTSET: /* --nftset */
#ifndef HAVE_IPSET
      if (option == LOPT_IPSET)
        {
          ret_err(_("recompile with HAVE_IPSET defined to enable ipset directives"));
          break;
        }
#endif
#ifndef HAVE_NFTSET
      if (option == LOPT_NFTSET)
        {
          ret_err(_("recompile with HAVE_NFTSET defined to enable nftset directives"));
          break;
        }
#endif

      {
	 struct ipsets ipsets_head;
	 struct ipsets *ipsets = &ipsets_head;
         struct ipsets **daemon_sets =
           (option == LOPT_IPSET) ? &daemon->ipsets : &daemon->nftsets;
	 int size;
	 char *end;
	 char **sets, **sets_pos;
	 memset(ipsets, 0, sizeof(struct ipsets));
	 unhide_metas(arg);
	 if (arg && *arg == '/') 
	   {
	     arg++;
	     while ((end = split_chr(arg, '/'))) 
	       {
		 char *domain = NULL;
		 /* elide leading dots - they are implied in the search algorithm */
		 while (*arg == '.')
		   arg++;
		 /* # matches everything and becomes a zero length domain string */
		 if (strcmp(arg, "#") == 0 || !*arg)
		   domain = "";
		 else if (strlen(arg) != 0 && !(domain = canonicalise_opt(arg)))
		   ret_err(gen_err);
		 ipsets->next = opt_malloc(sizeof(struct ipsets));
		 ipsets = ipsets->next;
		 memset(ipsets, 0, sizeof(struct ipsets));
		 ipsets->domain = domain;
		 arg = end;
	       }
	   } 
	 else 
	   {
	     ipsets->next = opt_malloc(sizeof(struct ipsets));
	     ipsets = ipsets->next;
	     memset(ipsets, 0, sizeof(struct ipsets));
	     ipsets->domain = "";
	   }
	 
	 if (!arg || !*arg)
	   ret_err(gen_err);
	 
	 for (size = 2, end = arg; *end; ++end) 
	   if (*end == ',')
	       ++size;
     
	 sets = sets_pos = opt_malloc(sizeof(char *) * size);
	 
	 do {
	   char *p;
	   end = split(arg);
	   *sets_pos = opt_string_alloc(arg);
	   /* Use '#' to delimit table and set */
	   if (option == LOPT_NFTSET)
	     while ((p = strchr(*sets_pos, '#')))
	       *p = ' ';
	   sets_pos++;
	   arg = end;
	 } while (end);
	 *sets_pos = 0;
	 for (ipsets = &ipsets_head; ipsets->next; ipsets = ipsets->next)
	   ipsets->next->sets = sets;
	 ipsets->next = *daemon_sets;
	 *daemon_sets = ipsets_head.next;
	 
	 break;
      }
      
    case LOPT_CMARK_ALST_EN: /* --connmark-allowlist-enable */
#ifndef HAVE_CONNTRACK
      ret_err(_("recompile with HAVE_CONNTRACK defined to enable connmark-allowlist directives"));
      break;
#else
      {
	u32 mask = UINT32_MAX;
	
	if (arg)
	  if (!strtoul_check(arg, &mask) || mask < 1)
	    ret_err(gen_err);
	
	set_option_bool(OPT_CMARK_ALST_EN);
	daemon->allowlist_mask = mask;
	break;
      }
#endif
      
    case LOPT_CMARK_ALST: /* --connmark-allowlist */
#ifndef HAVE_CONNTRACK
	ret_err(_("recompile with HAVE_CONNTRACK defined to enable connmark-allowlist directives"));
	break;
#else
      {
	struct allowlist *allowlists;
	char **patterns, **patterns_pos;
	u32 mark, mask = UINT32_MAX;
	size_t num_patterns = 0;
	
	char *c, *m = NULL;
	char *separator;
	unhide_metas(arg);
	if (!arg)
	  ret_err(gen_err);
	c = arg;
	if (*c < '0' || *c > '9')
	  ret_err(gen_err);
	while (*c && *c != ',')
	  {
	    if (*c == '/')
	      {
		if (m)
		  ret_err(gen_err);
	        *c = '\0';
		m = ++c;
	      }
	    if (*c < '0' || *c > '9')
	      ret_err(gen_err);
	    c++;
	  }
	separator = c;
	if (!*separator)
	  break;
	while (c && *c)
	  {
	    char *end = strchr(++c, '/');
	    if (end)
	      *end = '\0';
	    if (strcmp(c, "*") && !is_valid_dns_name_pattern(c))
	      ret_err(gen_err);
	    if (end)
	      *end = '/';
	    if (num_patterns >= UINT16_MAX - 1)
	      ret_err(gen_err);
	    num_patterns++;
	    c = end;
	  }
	
	*separator = '\0';
	if (!strtoul_check(arg, &mark) || mark < 1 || mark > UINT32_MAX)
	  ret_err(gen_err);
	if (m)
	  if (!strtoul_check(m, &mask) || mask < 1 || mask > UINT32_MAX || (mark & ~mask))
	    ret_err(gen_err);
	if (num_patterns)
	  *separator = ',';
	for (allowlists = daemon->allowlists; allowlists; allowlists = allowlists->next)
	  if (allowlists->mark == mark && allowlists->mask == mask)
	    ret_err(gen_err);
	
	patterns = opt_malloc((num_patterns + 1) * sizeof(char *));
	if (!patterns)
	  goto fail_cmark_allowlist;
	patterns_pos = patterns;
	c = separator;
	while (c && *c)
	{
	  char *end = strchr(++c, '/');
	  if (end)
	    *end = '\0';
	  if (!(*patterns_pos++ = opt_string_alloc(c)))
	    goto fail_cmark_allowlist;
	  if (end)
	    *end = '/';
	  c = end;
	}
	*patterns_pos++ = NULL;
	
	allowlists = opt_malloc(sizeof(struct allowlist));
	if (!allowlists)
	  goto fail_cmark_allowlist;
	memset(allowlists, 0, sizeof(struct allowlist));
	allowlists->mark = mark;
	allowlists->mask = mask;
	allowlists->patterns = patterns;
	allowlists->next = daemon->allowlists;
	daemon->allowlists = allowlists;
	break;
	
      fail_cmark_allowlist:
	if (patterns)
	  {
	    for (patterns_pos = patterns; *patterns_pos; patterns_pos++)
	      {
		free(*patterns_pos);
		*patterns_pos = NULL;
	      }
	    free(patterns);
	    patterns = NULL;
	  }
	if (allowlists)
	  {
	    free(allowlists);
	    allowlists = NULL;
	  }
	ret_err(gen_err);
      }
#endif
      
    case 'c':  /* --cache-size */
      {
	int size;
	
	if (!atoi_check(arg, &size))
	  ret_err(gen_err);
	else
	  {
	    /* zero is OK, and means no caching. */
	    
	    if (size < 0)
	      size = 0;

	    /* Note that for very large cache sizes, the malloc()
	       will overflow. For the size of the cache record
	       at the time this was noted, the value of "very large"
               was 46684428. Limit to an order of magnitude less than
	       that to be safe from changes to the cache record. */
	    if (size > 5000000)
	      size = 5000000;
	    
	    daemon->cachesize = size;
	  }
	break;
      }
      
    case 'p':  /* --port */
      if (!atoi_check16(arg, &daemon->port))
	ret_err(gen_err);
      break;
    
    case LOPT_MINPORT:  /* --min-port */
      if (!atoi_check16(arg, &daemon->min_port))
	ret_err(gen_err);
      break;

    case LOPT_MAXPORT:  /* --max-port */
      if (!atoi_check16(arg, &daemon->max_port))
	ret_err(gen_err);
      break;

    case '0':  /* --dns-forward-max */
      if (!atoi_check(arg, &daemon->ftabsize))
	ret_err(gen_err);
      break;  
    
    case 'q': /* --log-queries */
      set_option_bool(OPT_LOG);
      if (arg)
	{
	  if (strcmp(arg, "extra") == 0)
	    set_option_bool(OPT_EXTRALOG);
	  else if (strcmp(arg, "proto") == 0)
	    {
	      set_option_bool(OPT_EXTRALOG);
	      set_option_bool(OPT_LOG_PROTO);
	    }
	  else if (strcmp(arg, "auth") == 0)
	    set_option_bool(OPT_AUTH_LOG);
	}
      break;

    case LOPT_MAX_LOGS:  /* --log-async */
      daemon->max_logs = LOG_MAX; /* default */
      if (arg && !atoi_check(arg, &daemon->max_logs))
	ret_err(gen_err);
      else if (daemon->max_logs > 100)
	daemon->max_logs = 100;
      break;

    case LOPT_LOCAL_SERVICE:  /* --local-service */
      if (!arg || !strcmp(arg, "net"))
	set_option_bool(OPT_LOCAL_SERVICE);
      else if (!strcmp(arg, "host"))
	set_option_bool(OPT_LOCALHOST_SERVICE);
      else
	ret_err(gen_err);
      break;  

    case 'P': /* --edns-packet-max */
      {
	int i;
	if (!atoi_check(arg, &i))
	  ret_err(gen_err);
	daemon->edns_pktsz = (unsigned short)i;	
	break;
      }
      
    case 'Q':  /* --query-port */
      if (!atoi_check16(arg, &daemon->query_port))
	ret_err(gen_err);
      /* if explicitly set to zero, use single OS ephemeral port
	 and disable random ports */
      if (daemon->query_port == 0)
	daemon->osport = 1;
      break;

    case LOPT_RANDPORT_LIM: /* --port-limit */
      if (!atoi_check(arg, &daemon->randport_limit) || (daemon->randport_limit < 1))
	ret_err(gen_err);
      break;
      
    case 'T':         /* --local-ttl */
    case LOPT_NEGTTL: /* --neg-ttl */
    case LOPT_MAXTTL: /* --max-ttl */
    case LOPT_MINCTTL: /* --min-cache-ttl */
    case LOPT_MAXCTTL: /* --max-cache-ttl */
    case LOPT_AUTHTTL: /* --auth-ttl */
    case LOPT_DHCPTTL: /* --dhcp-ttl */
      {
	int ttl;
	if (!atoi_check(arg, &ttl))
	  ret_err(gen_err);
	else if (option == LOPT_NEGTTL)
	  daemon->neg_ttl = (unsigned long)ttl;
	else if (option == LOPT_MAXTTL)
	  daemon->max_ttl = (unsigned long)ttl;
	else if (option == LOPT_MINCTTL)
	  {
	    if (ttl > TTL_FLOOR_LIMIT)
	      ttl = TTL_FLOOR_LIMIT;
	    daemon->min_cache_ttl = (unsigned long)ttl;
	  }
	else if (option == LOPT_MAXCTTL)
	  daemon->max_cache_ttl = (unsigned long)ttl;
	else if (option == LOPT_AUTHTTL)
	  daemon->auth_ttl = (unsigned long)ttl;
	else if (option == LOPT_DHCPTTL)
	  {
	    daemon->dhcp_ttl = (unsigned long)ttl;
	    daemon->use_dhcp_ttl = 1;
	  }
	else
	  daemon->local_ttl = (unsigned long)ttl;
	break;
      }

    case LOPT_FAST_RETRY: /* --fast-dns-retry */
      daemon->fast_retry_timeout = TIMEOUT;
      
      if (!arg)
	daemon->fast_retry_time = DEFAULT_FAST_RETRY;
      else
	{
	  int retry;
	  
	  comma = split(arg);
	  if (!atoi_check(arg, &retry) || retry < 50)
	    ret_err(gen_err);
	  daemon->fast_retry_time = retry;
	  
	  if (comma)
	    {
	      if (!atoi_check(comma, &retry))
		ret_err(gen_err);
	      daemon->fast_retry_timeout = retry/1000;
	    }
	}
      break;

    case LOPT_CACHE_RR: /* --cache-rr */
    case LOPT_FILTER_RR: /* --filter-rr */
    case LOPT_FILTER_A: /* --filter-A */
    case LOPT_FILTER_AAAA: /* --filter-AAAA */
      while (1) {
	int type;
	struct rrlist *new;

	comma = NULL;

	if (option == LOPT_FILTER_A)
	  type = T_A;
	else if (option == LOPT_FILTER_AAAA)
	  type = T_AAAA;
	else
	  {
	    comma = split(arg);
	    if (!atoi_check(arg, &type) && (type = rrtype(arg)) == 0)
	      ret_err(_("bad RR type"));
	  }
	
	new = opt_malloc(sizeof(struct rrlist));
	new->rr = type;

	if (option == LOPT_CACHE_RR)
	  {
	    new->next = daemon->cache_rr;
	    daemon->cache_rr = new;
	  }
	else
	  {
	    new->next = daemon->filter_rr;
	    daemon->filter_rr = new;
	  }
	
	if (!comma) break;
	arg = comma;
      }
      break;
      
            
#ifdef HAVE_DHCP
    case 'X': /* --dhcp-lease-max */
      if (!atoi_check(arg, &daemon->dhcp_max))
	ret_err(gen_err);
      break;
#endif
      
#ifdef HAVE_TFTP
    case LOPT_TFTP_MAX:  /*  --tftp-max */
      if (!atoi_check(arg, &daemon->tftp_max))
	ret_err(gen_err);
      break;  

    case LOPT_TFTP_MTU:  /*  --tftp-mtu */
      if (!atoi_check(arg, &daemon->tftp_mtu))
	ret_err(gen_err);
      break;

    case LOPT_PREFIX: /* --tftp-prefix */
      comma = split(arg);
      if (comma)
	{
	  struct tftp_prefix *new = opt_malloc(sizeof(struct tftp_prefix));
	  new->interface = opt_string_alloc(comma);
	  new->prefix = opt_string_alloc(arg);
	  new->next = daemon->if_prefix;
	  daemon->if_prefix = new;
	}
      else
	daemon->tftp_prefix = opt_string_alloc(arg);
      break;

    case LOPT_TFTPPORTS: /* --tftp-port-range */
      if (!(comma = split(arg)) || 
	  !atoi_check16(arg, &daemon->start_tftp_port) ||
	  !atoi_check16(comma, &daemon->end_tftp_port))
	ret_err(_("bad port range"));
      
      if (daemon->start_tftp_port > daemon->end_tftp_port)
	{
	  int tmp = daemon->start_tftp_port;
	  daemon->start_tftp_port = daemon->end_tftp_port;
	  daemon->end_tftp_port = tmp;
	} 
      
      break;

    case LOPT_APREF: /* --tftp-unique-root */
      if (!arg || strcasecmp(arg, "ip") == 0)
        set_option_bool(OPT_TFTP_APREF_IP);
      else if (strcasecmp(arg, "mac") == 0)
        set_option_bool(OPT_TFTP_APREF_MAC);
      else
        ret_err(gen_err);
      break;
#endif
	      
    case LOPT_BRIDGE:   /* --bridge-interface */
      {
	struct dhcp_bridge *new;

	if (!(comma = split(arg)) || strlen(arg) > IF_NAMESIZE - 1 )
	  ret_err(_("bad bridge-interface"));

	for (new = daemon->bridges; new; new = new->next)
	  if (strcmp(new->iface, arg) == 0)
	    break;

	if (!new)
	  {
	     new = opt_malloc(sizeof(struct dhcp_bridge));
	     strcpy(new->iface, arg);
	     new->alias = NULL;
	     new->next = daemon->bridges;
	     daemon->bridges = new;
	  }
	
	do {
	  arg = comma;
	  comma = split(arg);
	  if (strlen(arg) != 0 && strlen(arg) <= IF_NAMESIZE - 1)
	    {
	      struct dhcp_bridge *b = opt_malloc(sizeof(struct dhcp_bridge)); 
	      b->next = new->alias;
	      new->alias = b;
	      strcpy(b->iface, arg);
	    }
	} while (comma);
	
	break;
      }

#ifdef HAVE_DHCP
    case LOPT_SHARED_NET: /* --shared-network */
      {
	struct shared_network *new = opt_malloc(sizeof(struct shared_network));

#ifdef HAVE_DHCP6
	new->shared_addr.s_addr = 0;
#endif
	new->if_index = 0;
	
	if (!(comma = split(arg)))
	  {
	  snerr:
	    free(new);
	    ret_err(_("bad shared-network"));
	  }
	
	if (inet_pton(AF_INET, comma, &new->shared_addr))
	  {
	    if (!inet_pton(AF_INET, arg, &new->match_addr) &&
		!(new->if_index = if_nametoindex(arg)))
	      goto snerr;
	  }
#ifdef HAVE_DHCP6
	else if (inet_pton(AF_INET6, comma, &new->shared_addr6))
	  {
	    if (!inet_pton(AF_INET6, arg, &new->match_addr6) &&
		!(new->if_index = if_nametoindex(arg)))
	      goto snerr;
	  }
#endif
	else
	  goto snerr;

	new->next = daemon->shared_networks;
	daemon->shared_networks = new;
	break;
      }
	  
    case 'F':  /* --dhcp-range */
      {
	int k, leasepos = 2;
	char *cp, *a[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	struct dhcp_context *new = opt_malloc(sizeof(struct dhcp_context));
	
	memset (new, 0, sizeof(*new));
	
	while(1)
	  {
	    for (cp = arg; *cp; cp++)
	      if (!(*cp == ' ' || *cp == '.' || *cp == ':' || 
		    (*cp >= 'a' && *cp <= 'f') || (*cp >= 'A' && *cp <= 'F') ||
		    (*cp >='0' && *cp <= '9')))
		break;
	    
	    if (*cp != ',' && (comma = split(arg)))
	      {
		if (is_tag_prefix(arg))
		  {
		    /* ignore empty tag */
		    if (arg[4])
		      new->filter = dhcp_netid_create(arg+4, new->filter);
		  }
		else
		  {
		    if (new->netid.net)
		      {
			dhcp_context_free(new);
			ret_err(_("only one tag allowed"));
		      }
		    else
		      new->netid.net = opt_string_alloc(set_prefix(arg));
		  }
		arg = comma;
	      }
	    else
	      {
		a[0] = arg;
		break;
	      }
	  }
	
	for (k = 1; k < 8; k++)
	  if (!(a[k] = split(a[k-1])))
	    break;
	
	if (k < 2)
	  {
	    dhcp_context_free(new);
	    ret_err(_("bad dhcp-range"));
	  }
	
	if (inet_pton(AF_INET, a[0], &new->start))
	  {
	    new->next = daemon->dhcp;
	    new->lease_time = DEFLEASE;
	    daemon->dhcp = new;
	    new->end = new->start;
	    if (strcmp(a[1], "static") == 0)
	      new->flags |= CONTEXT_STATIC;
	    else if (strcmp(a[1], "proxy") == 0)
	      new->flags |= CONTEXT_PROXY;
	    else if (!inet_pton(AF_INET, a[1], &new->end))
	      {
		dhcp_context_free(new);
		ret_err(_("bad dhcp-range"));
	      }
	    
	    if (ntohl(new->start.s_addr) > ntohl(new->end.s_addr))
	      {
		struct in_addr tmp = new->start;
		new->start = new->end;
		new->end = tmp;
	      }
	    
	    if (k >= 3 && strchr(a[2], '.') &&  
		(inet_pton(AF_INET, a[2], &new->netmask) > 0))
	      {
		new->flags |= CONTEXT_NETMASK;
		leasepos = 3;
		if (!is_same_net(new->start, new->end, new->netmask))
		  {
		    dhcp_context_free(new);
		    ret_err(_("inconsistent DHCP range"));
		  }
		
	    
		if (k >= 4 && strchr(a[3], '.') &&  
		    (inet_pton(AF_INET, a[3], &new->broadcast) > 0))
		  {
		    new->flags |= CONTEXT_BRDCAST;
		    leasepos = 4;
		  }
	      }
	  }
#ifdef HAVE_DHCP6
	else if (inet_pton(AF_INET6, a[0], &new->start6))
	  {
	    const char *err = NULL;

	    new->flags |= CONTEXT_V6; 
	    new->prefix = 64; /* default */
	    new->end6 = new->start6;
	    new->lease_time = DEFLEASE6;
	    new->next = daemon->dhcp6;
	    daemon->dhcp6 = new;

	    for (leasepos = 1; leasepos < k; leasepos++)
	      {
		if (strcmp(a[leasepos], "static") == 0)
		  new->flags |= CONTEXT_STATIC | CONTEXT_DHCP;
		else if (strcmp(a[leasepos], "ra-only") == 0 || strcmp(a[leasepos], "slaac") == 0 )
		  new->flags |= CONTEXT_RA;
		else if (strcmp(a[leasepos], "ra-names") == 0)
		  new->flags |= CONTEXT_RA_NAME | CONTEXT_RA;
		else if (strcmp(a[leasepos], "ra-advrouter") == 0)
		  new->flags |= CONTEXT_RA_ROUTER | CONTEXT_RA;
		else if (strcmp(a[leasepos], "ra-stateless") == 0)
		  new->flags |= CONTEXT_RA_STATELESS | CONTEXT_DHCP | CONTEXT_RA;
		else if (strcmp(a[leasepos], "off-link") == 0)
		  new->flags |= CONTEXT_RA_OFF_LINK;
		else if (leasepos == 1 && inet_pton(AF_INET6, a[leasepos], &new->end6))
		  new->flags |= CONTEXT_DHCP; 
		else if (strstr(a[leasepos], "constructor:") == a[leasepos])
		  {
		    new->template_interface = opt_string_alloc(a[leasepos] + 12);
		    new->flags |= CONTEXT_TEMPLATE;
		  }
		else  
		  break;
	      }
	   	    	     
	    /* bare integer < 128 is prefix value */
	    if (leasepos < k)
	      {
		int pref;
		for (cp = a[leasepos]; *cp; cp++)
		  if (!(*cp >= '0' && *cp <= '9'))
		    break;
		if (!*cp && (pref = atoi(a[leasepos])) <= 128)
		  {
		    new->prefix = pref;
		    leasepos++;
		  }
	      }
	    
	    if (new->prefix > 64)
	      {
		if (new->flags & CONTEXT_RA)
		  err=(_("prefix length must be exactly 64 for RA subnets"));
		else if (new->flags & CONTEXT_TEMPLATE)
		  err=(_("prefix length must be exactly 64 for subnet constructors"));
	      }
	    else if (new->prefix < 64)
	      err=(_("prefix length must be at least 64"));
	    
	    if (!err && !is_same_net6(&new->start6, &new->end6, new->prefix))
	      err=(_("inconsistent DHCPv6 range"));

	    if (err)
	      {
		dhcp_context_free(new);
		ret_err(err);
	      }

	    /* dhcp-range=:: enables DHCP stateless on any interface */
	    if (IN6_IS_ADDR_UNSPECIFIED(&new->start6) && !(new->flags & CONTEXT_TEMPLATE))
	      new->prefix = 0;
	    
	    if (new->flags & CONTEXT_TEMPLATE)
	      {
		struct in6_addr zero;
		memset(&zero, 0, sizeof(zero));
		if (!is_same_net6(&zero, &new->start6, new->prefix))
		  {
		    dhcp_context_free(new);
		    ret_err(_("prefix must be zero with \"constructor:\" argument"));
		  }
	      }
	    
	    if (addr6part(&new->start6) > addr6part(&new->end6))
	      {
		struct in6_addr tmp = new->start6;
		new->start6 = new->end6;
		new->end6 = tmp;
	      }
	  }
#endif
	else
	  {
	    dhcp_context_free(new);
	    ret_err(_("bad dhcp-range"));
	  }
	
	if (leasepos < k)
	  {
	    if (leasepos != k-1)
	      {
		dhcp_context_free(new);
		ret_err(_("bad dhcp-range"));
	      }
	    
	    if (strcmp(a[leasepos], "infinite") == 0)
	      {
		new->lease_time = 0xffffffff;
		new->flags |= CONTEXT_SETLEASE;
	      }
	    else if (strcmp(a[leasepos], "deprecated") == 0)
	      new->flags |= CONTEXT_DEPRECATE;
	    else
	      {
		int fac = 1;
		if (strlen(a[leasepos]) > 0)
		  {
		    switch (a[leasepos][strlen(a[leasepos]) - 1])
		      {
		      case 'w':
		      case 'W':
			fac *= 7;
			/* fall through */
		      case 'd':
		      case 'D':
			fac *= 24;
			/* fall through */
		      case 'h':
		      case 'H':
			fac *= 60;
			/* fall through */
		      case 'm':
		      case 'M':
			fac *= 60;
			/* fall through */
		      case 's':
		      case 'S':
			a[leasepos][strlen(a[leasepos]) - 1] = 0;
		      }
		    
		    for (cp = a[leasepos]; *cp; cp++)
		      if (!(*cp >= '0' && *cp <= '9'))
			break;

		    if (*cp || (leasepos+1 < k))
		      ret_err_free(_("bad dhcp-range"), new);
		    
		    new->lease_time = atoi(a[leasepos]) * fac;
		    new->flags |= CONTEXT_SETLEASE;
		    /* Leases of a minute or less confuse
		       some clients, notably Apple's */
		    if (new->lease_time < 120)
		      new->lease_time = 120;
		  }
	      }
	  }

	break;
      }

    case LOPT_BANK:
    case 'G':  /* --dhcp-host */
      {
	struct dhcp_config *new;
	struct in_addr in;
	
	new = opt_malloc(sizeof(struct dhcp_config));
	
	new->next = daemon->dhcp_conf;
	new->flags = (option == LOPT_BANK) ? CONFIG_BANK : 0;
	new->hwaddr = NULL;
	new->netid = NULL;
	new->filter = NULL;
	new->clid = NULL;
#ifdef HAVE_DHCP6
	new->addr6 = NULL;
#endif

	while (arg)
	  {
	    comma = split(arg);
	    if (strchr(arg, ':')) /* Ethernet address, netid or binary CLID */
	      {
		if ((arg[0] == 'i' || arg[0] == 'I') &&
		    (arg[1] == 'd' || arg[1] == 'D') &&
		    arg[2] == ':')
		  {
		    if (arg[3] == '*')
		      new->flags |= CONFIG_NOCLID;
		    else
		      {
			int len;
			arg += 3; /* dump id: */
			if (strchr(arg, ':'))
			  len = parse_hex(arg, (unsigned char *)arg, -1, NULL, NULL);
			else
			  {
			    unhide_metas(arg);
			    len = (int) strlen(arg);
			  }
			
			if (len == -1)
			  {
			    dhcp_config_free(new);
			    ret_err(_("bad hex constant"));
			  }
			else if ((new->clid = opt_malloc(len)))
			  {
			    new->flags |= CONFIG_CLID;
			    new->clid_len = len;
			    memcpy(new->clid, arg, len);
			  }
		      }
		  }
		/* dhcp-host has strange backwards-compat needs. */
		else if (strstr(arg, "net:") == arg || strstr(arg, "set:") == arg)
		  {
		    struct dhcp_netid_list *newlist = opt_malloc(sizeof(struct dhcp_netid_list));
		    newlist->next = new->netid;
		    new->netid = newlist;
		    newlist->list = dhcp_netid_create(arg+4, NULL);
		  }
		else if (strstr(arg, "tag:") == arg)
		  new->filter = dhcp_netid_create(arg+4, new->filter);
		  
#ifdef HAVE_DHCP6
		else if (arg[0] == '[' && arg[strlen(arg)-1] == ']')
		  {
		    char *pref;
		    struct in6_addr in6;
		    struct addrlist *new_addr;
		    
		    arg[strlen(arg)-1] = 0;
		    arg++;
		    pref = split_chr(arg, '/');
		    
		    if (!inet_pton(AF_INET6, arg, &in6))
		      {
			dhcp_config_free(new);
			ret_err(_("bad IPv6 address"));
		      }

		    new_addr = opt_malloc(sizeof(struct addrlist));
		    new_addr->flags = 0;
		    new_addr->addr.addr6 = in6;
		    
		    if (pref)
		      {
			u64 addrpart = addr6part(&in6);
			
			if (!atoi_check(pref, &new_addr->prefixlen) ||
			    new_addr->prefixlen > 128 ||
			    ((((u64)1<<(128-new_addr->prefixlen))-1) & addrpart) != 0)
			  {
			    dhcp_config_free(new);
			    ret_err_free(_("bad IPv6 prefix"), new_addr);
			  }
			
			new_addr->flags |= ADDRLIST_PREFIX;
		      }
		  
		    for (i= 0; i < 8; i++)
		      if (in6.s6_addr[i] != 0)
			break;
		    
		    /* set WILDCARD if network part all zeros */
		    if (i == 8)
		      new_addr->flags |= ADDRLIST_WILDCARD;
		    
		    new_addr->next = new->addr6;
		    new->addr6 = new_addr;
		    new->flags |= CONFIG_ADDR6;
		  }
#endif
		else
		  {
		    struct hwaddr_config *newhw = opt_malloc(sizeof(struct hwaddr_config));
		    if ((newhw->hwaddr_len = parse_hex(arg, newhw->hwaddr, DHCP_CHADDR_MAX, 
						       &newhw->wildcard_mask, &newhw->hwaddr_type)) == -1)
		      {
			free(newhw);
			dhcp_config_free(new);
			ret_err(_("bad hex constant"));
		      }
		    else
		      {
			newhw->next = new->hwaddr;
			new->hwaddr = newhw;
		      }		    
		  }
	      }
	    else if (strchr(arg, '.') && (inet_pton(AF_INET, arg, &in) > 0))
	      {
		struct dhcp_config *configs;
		
		new->addr = in;
		new->flags |= CONFIG_ADDR;
		
		/* If the same IP appears in more than one host config, then DISCOVER
		   for one of the hosts will get the address, but REQUEST will be NAKed,
		   since the address is reserved by the other one -> protocol loop. */
		for (configs = daemon->dhcp_conf; configs; configs = configs->next) 
		  if ((configs->flags & CONFIG_ADDR) && configs->addr.s_addr == in.s_addr)
		    {
		      inet_ntop(AF_INET, &in, daemon->addrbuff, ADDRSTRLEN);
		      sprintf(errstr, _("duplicate dhcp-host IP address %s"),
			      daemon->addrbuff);
		      dhcp_config_free(new);
		      return 0;
		    }	      
	      }
	    else
	      {
		char *cp, *lastp = NULL, last = 0;
		int fac = 1, isdig = 0;
		
		if (strlen(arg) > 1)
		  {
		    lastp = arg + strlen(arg) - 1;
		    last = *lastp;
		    switch (last)
		      {
		      case 'w':
		      case 'W':
			fac *= 7;
			/* fall through */
		      case 'd':
		      case 'D':
			fac *= 24;
			/* fall through */
		      case 'h':
		      case 'H':
			fac *= 60;
			/* fall through */
		      case 'm':
		      case 'M':
			fac *= 60;
			/* fall through */
		      case 's':
		      case 'S':
			*lastp = 0;
		      }
		  }
		
		for (cp = arg; *cp; cp++)
		  if (isdigit((unsigned char)*cp))
		    isdig = 1;
		  else if (*cp != ' ')
		    break;

		if (*cp)
		  {
		    if (lastp)
		      *lastp = last;
		    if (strcmp(arg, "infinite") == 0)
		      {
			new->lease_time = 0xffffffff;
			new->flags |= CONFIG_TIME;
		      }
		    else if (strcmp(arg, "ignore") == 0)
		      new->flags |= CONFIG_DISABLE;
		    else if (new->hostname)
		      {
			dhcp_config_free(new);
			ret_err(_("DHCP host has multiple names"));
		      }
 		    else
		      {
			if (!(new->hostname = canonicalise_opt(arg)) ||
			    !legal_hostname(new->hostname))
			  {
			    dhcp_config_free(new);
			    ret_err(_("bad DHCP host name"));
			  }
			
			new->flags |= CONFIG_NAME;
			new->domain = strip_hostname(new->hostname);			
		      }
		  }
		else if (isdig)
		  {
		    new->lease_time = atoi(arg) * fac; 
		    /* Leases of a minute or less confuse
		       some clients, notably Apple's */
		    if (new->lease_time < 120)
		      new->lease_time = 120;
		    new->flags |= CONFIG_TIME;
		  }
	      }

	    arg = comma;
	  }

	daemon->dhcp_conf = new;
	break;
      }
      
    case LOPT_TAG_IF:  /* --tag-if */
      {
	struct tag_if *new = opt_malloc(sizeof(struct tag_if));
		
	new->tag = NULL;
	new->set = NULL;
	new->next = NULL;
	
	/* preserve order */
	if (!daemon->tag_if)
	  daemon->tag_if = new;
	else
	  {
	    struct tag_if *tmp;
	    for (tmp = daemon->tag_if; tmp->next; tmp = tmp->next);
	    tmp->next = new;
	  }

	while (arg)
	  {
	    size_t len;

	    comma = split(arg);
	    len = strlen(arg);

	    if (len < 5)
	      {
		new->set = NULL;
		break;
	      }
	    else
	      {
		struct dhcp_netid *newtag = dhcp_netid_create(arg+4, NULL);

		if (strstr(arg, "set:") == arg)
		  {
		    struct dhcp_netid_list *newlist = opt_malloc(sizeof(struct dhcp_netid_list));
		    newlist->next = new->set;
		    new->set = newlist;
		    newlist->list = newtag;
		  }
		else if (strstr(arg, "tag:") == arg)
		  {
		    newtag->next = new->tag;
		    new->tag = newtag;
		  }
		else 
		  {
		    new->set = NULL;
		    dhcp_netid_free(newtag);
		    break;
		  }
	      }
	    
	    arg = comma;
	  }

	if (!new->set)
	  {
	    dhcp_netid_free(new->tag);
	    dhcp_netid_list_free(new->set);
	    ret_err_free(_("bad tag-if"), new);
	  }
	  
	break;
      }

      
    case 'O':           /* --dhcp-option */
    case LOPT_FORCE:    /* --dhcp-option-force */
    case LOPT_PXE_OPT:  /* --dhcp-option-pxe */
    case LOPT_OPTS:
    case LOPT_MATCH:    /* --dhcp-match */
      return parse_dhcp_opt(errstr, arg, 
			    option == LOPT_FORCE ? DHOPT_FORCE : 
			    (option == LOPT_MATCH ? DHOPT_MATCH :
			     (option == LOPT_OPTS ? DHOPT_BANK :
			      (option == LOPT_PXE_OPT ? DHOPT_PXE_OPT : 0))));

    case LOPT_NAME_MATCH: /* --dhcp-name-match */
      {
	struct dhcp_match_name *new;
	ssize_t len;
	
	if (!(comma = split(arg)) || (len = strlen(comma)) == 0)
	  ret_err(gen_err);

	new = opt_malloc(sizeof(struct dhcp_match_name));
	new->wildcard = 0;
	new->netid = opt_malloc(sizeof(struct dhcp_netid));
	new->netid->net = opt_string_alloc(set_prefix(arg));

	if (comma[len-1] == '*')
	  {
	    comma[len-1] = 0;
	    new->wildcard = 1;
	  }
	new->name = opt_string_alloc(comma);

	new->next = daemon->dhcp_name_match;
	daemon->dhcp_name_match = new;

	break;
      }
      
    case 'M': /* --dhcp-boot */
      {
	struct dhcp_netid *id = dhcp_tags(&arg);
	
	if (!arg)
	  {
	    ret_err(gen_err);
	  }
	else 
	  {
	    char *dhcp_file, *dhcp_sname = NULL, *tftp_sname = NULL;
	    struct in_addr dhcp_next_server;
	    struct dhcp_boot *new;
	    comma = split(arg);
	    dhcp_file = opt_string_alloc(arg);
	    dhcp_next_server.s_addr = 0;
	    if (comma)
	      {
		arg = comma;
		comma = split(arg);
		dhcp_sname = opt_string_alloc(arg);
		if (comma)
		  {
		    unhide_metas(comma);
		    if (!(inet_pton(AF_INET, comma, &dhcp_next_server) > 0))
		      {
			/*
			 * The user may have specified the tftp hostname here.
			 * save it so that it can be resolved/looked up during
			 * actual dhcp_reply().
			 */	
			
			tftp_sname = opt_string_alloc(comma);
			dhcp_next_server.s_addr = 0;
		      }
		  }
	      }
	    
	    new = opt_malloc(sizeof(struct dhcp_boot));
	    new->file = dhcp_file;
	    new->sname = dhcp_sname;
	    new->tftp_sname = tftp_sname;
	    new->next_server = dhcp_next_server;
	    new->netid = id;
	    new->next = daemon->boot_config;
	    daemon->boot_config = new;
	  }
      
	break;
      }

    case LOPT_REPLY_DELAY: /* --dhcp-reply-delay */
      {
	struct dhcp_netid *id = dhcp_tags(&arg);
	
	if (!arg)
	  {
	    ret_err(gen_err);
	  }
	else
	  {
	    struct delay_config *new;
	    int delay;
	    if (!atoi_check(arg, &delay))
              ret_err(gen_err);
	    
	    new = opt_malloc(sizeof(struct delay_config));
	    new->delay = delay;
	    new->netid = id;
            new->next = daemon->delay_conf;
            daemon->delay_conf = new;
	  }
	
	break;
      }
      
    case LOPT_PXE_PROMT:  /* --pxe-prompt */
       {
	 struct dhcp_opt *new = opt_malloc(sizeof(struct dhcp_opt));
	 int timeout;
	 
	 new->netid = NULL;
	 new->opt = 10; /* PXE_MENU_PROMPT */
	 new->netid = dhcp_tags(&arg);
	 
	 if (!arg)
	   {
	     dhcp_opt_free(new);
	     ret_err(gen_err);
	   }
	 else
	   {
	     comma = split(arg);
	     unhide_metas(arg);
	     new->len = strlen(arg) + 1;
	     new->val = opt_malloc(new->len);
	     memcpy(new->val + 1, arg, new->len - 1);
	     
	     new->u.vendor_class = NULL;
	     new->flags = DHOPT_VENDOR | DHOPT_VENDOR_PXE;
	     
	     if (comma && atoi_check(comma, &timeout))
	       *(new->val) = timeout;
	     else
	       *(new->val) = 255;

	     new->next = daemon->dhcp_opts;
	     daemon->dhcp_opts = new;
	     daemon->enable_pxe = 1;
	   }
	 
	 break;
       }
       
    case LOPT_PXE_SERV:  /* --pxe-service */
       {
	 struct pxe_service *new = opt_malloc(sizeof(struct pxe_service));
	 char *CSA[] = { "x86PC", "PC98", "IA64_EFI", "Alpha", "Arc_x86", "Intel_Lean_Client",
			 "IA32_EFI", "x86-64_EFI", "Xscale_EFI", "BC_EFI",
			 "ARM32_EFI", "ARM64_EFI", NULL };  
	 static int boottype = 32768;
	 
	 new->netid = NULL;
	 new->sname = NULL;
	 new->server.s_addr = 0;
	 new->netid = dhcp_tags(&arg);

	 if (arg && (comma = split(arg)))
	   {
	     for (i = 0; CSA[i]; i++)
	       if (strcasecmp(CSA[i], arg) == 0)
		 break;
	     
	     if (CSA[i] || atoi_check(arg, &i))
	       {
		 arg = comma;
		 comma = split(arg);
		 
		 new->CSA = i;
		 new->menu = opt_string_alloc(arg);
		 
		 if (!comma)
		   {
		     new->type = 0; /* local boot */
		     new->basename = NULL;
		   }
		 else
		   {
		     arg = comma;
		     comma = split(arg);
		     if (atoi_check(arg, &i))
		       {
			 new->type = i;
			 new->basename = NULL;
		       }
		     else
		       {
			 new->type = boottype++;
			 new->basename = opt_string_alloc(arg);
		       }
		     
		     if (comma)
		       {
			 if (!inet_pton(AF_INET, comma, &new->server))
			   {
			     new->server.s_addr = 0;
			     new->sname = opt_string_alloc(comma);
			   }
		       
		       }
		   }
		 
		 /* Order matters */
		 new->next = NULL;
		 if (!daemon->pxe_services)
		   daemon->pxe_services = new; 
		 else
		   {
		     struct pxe_service *s;
		     for (s = daemon->pxe_services; s->next; s = s->next);
		     s->next = new;
		   }
		 
		 daemon->enable_pxe = 1;
		 break;
		
	       }
	   }
	 
	 dhcp_netid_free(new->netid);
	 free(new);
	 ret_err(gen_err);
       }
	 
    case '4':  /* --dhcp-mac */
      {
	if (!(comma = split(arg)))
	  ret_err(gen_err);
	else
	  {
	    struct dhcp_mac *new = opt_malloc(sizeof(struct dhcp_mac));
	    new->netid.net = opt_string_alloc(set_prefix(arg));
	    unhide_metas(comma);
	    new->hwaddr_len = parse_hex(comma, new->hwaddr, DHCP_CHADDR_MAX, &new->mask, &new->hwaddr_type);
	    if (new->hwaddr_len == -1)
	      {
		free(new->netid.net);
		ret_err_free(gen_err, new);
	      }
	    else
	      {
		new->next = daemon->dhcp_macs;
		daemon->dhcp_macs = new;
	      }
	  }
      }
      break;

    case 'U':           /* --dhcp-vendorclass */
    case 'j':           /* --dhcp-userclass */
    case LOPT_CIRCUIT:  /* --dhcp-circuitid */
    case LOPT_REMOTE:   /* --dhcp-remoteid */
    case LOPT_SUBSCR:   /* --dhcp-subscrid */
      {
	 unsigned char *p;
	 int dig, colon;
	 struct dhcp_vendor *new = opt_malloc(sizeof(struct dhcp_vendor));
	 
	 if (!(comma = split(arg)))
	   ret_err_free(gen_err, new);
	
	 new->netid.net = opt_string_alloc(set_prefix(arg));
	 /* check for hex string - must digits may include : must not have nothing else, 
	    only allowed for agent-options. */
	 
	 arg = comma;
	 if ((comma = split(arg)))
	   {
	     if (option  != 'U' || strstr(arg, "enterprise:") != arg)
	       {
	         free(new->netid.net);
	         ret_err_free(gen_err, new);
	       }
	     else
	       new->enterprise = atoi(arg+11);
	   }
	 else
	   comma = arg;
	 
	 for (dig = 0, colon = 0, p = (unsigned char *)comma; *p; p++)
	   if (isxdigit(*p))
	     dig = 1;
	   else if (*p == ':')
	     colon = 1;
	   else
	     break;
	 
	 unhide_metas(comma);
	 if (option == 'U' || option == 'j' || *p || !dig || !colon)
	   {
	     new->len = strlen(comma);  
	     new->data = opt_malloc(new->len);
	     memcpy(new->data, comma, new->len);
	   }
	 else
	   {
	     new->len = parse_hex(comma, (unsigned char *)comma, strlen(comma), NULL, NULL);
	     new->data = opt_malloc(new->len);
	     memcpy(new->data, comma, new->len);
	   }
	 
	 switch (option)
	   {
	   case 'j':
	     new->match_type = MATCH_USER;
	     break;
	   case 'U':
	     new->match_type = MATCH_VENDOR;
	     break; 
	   case LOPT_CIRCUIT:
	     new->match_type = MATCH_CIRCUIT;
	     break;
	   case LOPT_REMOTE:
	     new->match_type = MATCH_REMOTE;
	     break;
	   case LOPT_SUBSCR:
	     new->match_type = MATCH_SUBSCRIBER;
	     break;
	   }
	 new->next = daemon->dhcp_vendors;
	 daemon->dhcp_vendors = new;

	 break;
      }
      
    case LOPT_ALTPORT:   /* --dhcp-alternate-port */
      if (!arg)
	{
	  daemon->dhcp_server_port = DHCP_SERVER_ALTPORT;
	  daemon->dhcp_client_port = DHCP_CLIENT_ALTPORT;
	}
      else
	{
	  comma = split(arg);
	  if (!atoi_check16(arg, &daemon->dhcp_server_port) || 
	      (comma && !atoi_check16(comma, &daemon->dhcp_client_port)))
	    ret_err(_("invalid port number"));
	  if (!comma)
	    daemon->dhcp_client_port = daemon->dhcp_server_port+1; 
	}
      break;

    case 'J':            /* --dhcp-ignore */
    case LOPT_NO_NAMES:  /* --dhcp-ignore-names */
    case LOPT_BROADCAST: /* --dhcp-broadcast */
    case '3':            /* --bootp-dynamic */
    case LOPT_GEN_NAMES: /* --dhcp-generate-names */
      {
	struct dhcp_netid_list *new = opt_malloc(sizeof(struct dhcp_netid_list));
	struct dhcp_netid *list = NULL;
	if (option == 'J')
	  {
	    new->next = daemon->dhcp_ignore;
	    daemon->dhcp_ignore = new;
	  }
	else if (option == LOPT_BROADCAST)
	  {
	    new->next = daemon->force_broadcast;
	    daemon->force_broadcast = new;
	  }
	else if (option == '3')
	  {
	    new->next = daemon->bootp_dynamic;
	    daemon->bootp_dynamic = new;
	  }
	else if (option == LOPT_GEN_NAMES)
	  {
	    new->next = daemon->dhcp_gen_names;
	    daemon->dhcp_gen_names = new;
	  }
	else
	  {
	    new->next = daemon->dhcp_ignore_names;
	    daemon->dhcp_ignore_names = new;
	  }
	
	while (arg) {
	  comma = split(arg);
	  list = dhcp_netid_create(is_tag_prefix(arg) ? arg+4 :arg, list);
	  arg = comma;
	}
	
	new->list = list;
	break;
      }

    case LOPT_PROXY: /* --dhcp-proxy */
      daemon->override = 1;
      while (arg) {
	struct addr_list *new = opt_malloc(sizeof(struct addr_list));
	comma = split(arg);
	if (!(inet_pton(AF_INET, arg, &new->addr) > 0))
	  ret_err_free(_("bad dhcp-proxy address"), new);
	new->next = daemon->override_relays;
	daemon->override_relays = new;
	arg = comma;
	}
      break;
      
    case LOPT_PXE_VENDOR: /* --dhcp-pxe-vendor */
      {
        while (arg) {
	  struct dhcp_pxe_vendor *new = opt_malloc(sizeof(struct dhcp_pxe_vendor));
	  comma = split(arg);
          new->data = opt_string_alloc(arg);
	  new->next = daemon->dhcp_pxe_vendors;
	  daemon->dhcp_pxe_vendors = new;
	  arg = comma;
	}
      }
      break;
      
    case LOPT_RELAY: /* --dhcp-relay */
    case LOPT_SPLIT_RELAY: /* --dhcp-splt-relay */
      {
	struct dhcp_relay *new = opt_malloc(sizeof(struct dhcp_relay));
	char *two = split(arg);
	char *three = split(two);

	if (option == LOPT_SPLIT_RELAY)
	  {
	    new->split_mode = 1;
	    
	    /* split mode must have two addresses and a non-wildcard interface name. */
	    if (!three || strchr(three, '*'))
	      two = NULL;
	  }
		    
	new->iface_index = 0;

	if (two)
	  {
	    if (inet_pton(AF_INET, arg, &new->local))
	      {
		char *hash = split_chr(two, '#');

		if (!hash || !atoi_check16(hash, &new->port))
		  new->port = DHCP_SERVER_PORT;
		
		if (!inet_pton(AF_INET, two, &new->server))
		  {
		    new->server.addr4.s_addr = 0;
		    		    
		    /* Fail for three arg version where there are not two addresses. 
		       Also fail when broadcasting to wildcard address. */
		    if (three || strchr(two, '*'))
		      two = NULL;
		    else
		      three = two;
		  }
		
		new->next = daemon->relay4;
		daemon->relay4 = new;
	      }
#ifdef HAVE_DHCP6
	    else if (inet_pton(AF_INET6, arg, &new->local) && !new->split_mode)
	      {
		char *hash = split_chr(two, '#');

		if (!hash || !atoi_check16(hash, &new->port))
		  new->port = DHCPV6_SERVER_PORT;

		if (!inet_pton(AF_INET6, two, &new->server))
		  {
		    inet_pton(AF_INET6, ALL_SERVERS, &new->server.addr6);
		    /* Fail for three arg version where there are not two addresses.
		       Also fail when multicasting to wildcard address. */
		    if (three || strchr(two, '*'))
		      two = NULL;
		    else
		      three = two;
		  }
		new->next = daemon->relay6;
		daemon->relay6 = new;
	      }
#endif
	    else
	      two = NULL;
	    
	    new->interface = opt_string_alloc(three);
	  }
	
	if (!two)
	  {
	    free(new->interface);
	    ret_err_free(_("Bad dhcp-relay"), new);
	  }
	
	break;
      }

#endif
      
#ifdef HAVE_DHCP6
    case LOPT_RA_PARAM: /* --ra-param */
      if ((comma = split(arg)))
	{
	  struct ra_interface *new = opt_malloc(sizeof(struct ra_interface));
	  new->lifetime = -1;
	  new->prio = 0;
	  new->mtu = 0;
	  new->mtu_name = NULL;
	  new->name = opt_string_alloc(arg);
	  if (strcasestr(comma, "mtu:") == comma)
	    {
	      arg = comma + 4;
	      if (!(comma = split(comma)))
	        goto err;
	      if (!strcasecmp(arg, "off"))
	        new->mtu = -1;
	      else if (!atoi_check(arg, &new->mtu))
	        new->mtu_name = opt_string_alloc(arg);
	      else if (new->mtu < 1280)
	        goto err;
	    }
	  if (strcasestr(comma, "high") == comma || strcasestr(comma, "low") == comma)
	    {
	      if (*comma == 'l' || *comma == 'L')
		new->prio = 0x18;
	      else
		new->prio = 0x08;
	      comma = split(comma);
	    }
	   arg = split(comma);
	   if (!atoi_check(comma, &new->interval) || 
	      (arg && !atoi_check(arg, &new->lifetime)))
             {
err:
	       free(new->name);
	       ret_err_free(_("bad RA-params"), new);
             }
	  
	  new->next = daemon->ra_interfaces;
	  daemon->ra_interfaces = new;
	}
      break;
      
    case LOPT_DUID: /* --dhcp-duid */
      if (!(comma = split(arg)) || !atoi_check(arg, (int *)&daemon->duid_enterprise))
	ret_err(_("bad DUID"));
      else
	{
	  daemon->duid_config_len = parse_hex(comma,(unsigned char *)comma, strlen(comma), NULL, NULL);
	  daemon->duid_config = opt_malloc(daemon->duid_config_len);
	  memcpy(daemon->duid_config, comma, daemon->duid_config_len);
	}
      break;
#endif

    case 'V':  /* --alias */
      {
	char *dash, *a[3] = { NULL, NULL, NULL };
	int k = 0;
	struct doctor *new = opt_malloc(sizeof(struct doctor));
	new->next = daemon->doctors;
	daemon->doctors = new;
	new->mask.s_addr = 0xffffffff;
	new->end.s_addr = 0;

	if ((a[0] = arg))
	  for (k = 1; k < 3; k++)
	    {
	      if (!(a[k] = split(a[k-1])))
		break;
	      unhide_metas(a[k]);
	    }
	
	dash = split_chr(a[0], '-');

	if ((k < 2) || 
	    (!(inet_pton(AF_INET, a[0], &new->in) > 0)) ||
	    (!(inet_pton(AF_INET, a[1], &new->out) > 0)) ||
	    (k == 3 && !inet_pton(AF_INET, a[2], &new->mask)))
	  ret_err(_("missing address in alias"));
	
	if (dash && 
	    (!(inet_pton(AF_INET, dash, &new->end) > 0) ||
	     !is_same_net(new->in, new->end, new->mask) ||
	     ntohl(new->in.s_addr) > ntohl(new->end.s_addr)))
	  ret_err_free(_("invalid alias range"), new);
	
	break;
      }
      
    case LOPT_INTNAME:  /* --interface-name */
    case LOPT_DYNHOST:  /* --dynamic-host */
      {
	struct interface_name *new, **up;
	char *domain = arg;
	
	arg = split(arg);
	
	new = opt_malloc(sizeof(struct interface_name));
	memset(new, 0, sizeof(struct interface_name));
	new->flags = IN4 | IN6;
	
	/* Add to the end of the list, so that first name
	   of an interface is used for PTR lookups. */
	for (up = &daemon->int_names; *up; up = &((*up)->next));
	*up = new;
	
	while ((comma = split(arg)))
	  {
	    if (inet_pton(AF_INET, arg, &new->proto4))
	      new->flags |= INP4;
	    else if (inet_pton(AF_INET6, arg, &new->proto6))
	      new->flags |= INP6;
	    else
	      break;
	    
	    arg = comma;
	  }

	if ((comma = split_chr(arg, '/')))
	  {
	    if (strcmp(comma, "4") == 0)
	      new->flags &= ~IN6;
	    else if (strcmp(comma, "6") == 0)
	      new->flags &= ~IN4;
	    else
	      ret_err_free(gen_err, new);
	  }

	new->intr = opt_string_alloc(arg);

	if (option == LOPT_DYNHOST)
	  {
	    if (!(new->flags & (INP4 | INP6)))
	      ret_err(_("missing address in dynamic host"));

	    if (!(new->flags & IN4) || !(new->flags & IN6))
	      arg = NULL; /* provoke error below */

	    new->flags &= ~(IN4 | IN6);
	  }
	else
	  {
	    if (new->flags & (INP4 | INP6))
	      arg = NULL; /* provoke error below */
	  }
	
	if (!domain || !arg || !(new->name = canonicalise_opt(domain)))
	  ret_err(option == LOPT_DYNHOST ?
		  _("bad dynamic host") : _("bad interface name"));
	
	break;
      }
      
    case LOPT_CNAME: /* --cname */
      {
	struct cname *new;
	char *alias, *target=NULL, *last, *pen;
	int ttl = -1;

	for (last = pen = NULL, comma = arg; comma; comma = split(comma))
	  {
	    pen = last;
	    last = comma;
	  }

	if (!pen)
	  ret_err(_("bad CNAME"));
	
	if (pen != arg && atoi_check(last, &ttl))
	  last = pen;
	  	
	while (arg != last)
	  {
	    int arglen = strlen(arg);
	    alias = canonicalise_opt(arg);

	    if (!target)
	      target = canonicalise_opt(last);
	    if (!alias || !target)
	      {
		free(target);
		free(alias);
		ret_err(_("bad CNAME"));
	      }
	    
	    for (new = daemon->cnames; new; new = new->next)
	      if (hostname_isequal(new->alias, alias))
		{
		  free(target);
		  free(alias);
		  ret_err(_("duplicate CNAME"));
		}
	    new = opt_malloc(sizeof(struct cname));
	    new->next = daemon->cnames;
	    daemon->cnames = new;
	    new->alias = alias;
	    new->target = target;
	    new->ttl = ttl;

	    for (arg += arglen+1; *arg && isspace((unsigned char)*arg); arg++);
	  }
      
	break;
      }

    case LOPT_PTR:  /* --ptr-record */
      {
	struct ptr_record *new;
	char *dom, *target = NULL;

	comma = split(arg);
	
	if (!(dom = canonicalise_opt(arg)) ||
	    (comma && !(target = canonicalise_opt(comma))))
	  {
	    free(dom);
	    free(target);
	    ret_err(_("bad PTR record"));
	  }
	else
	  {
	    new = opt_malloc(sizeof(struct ptr_record));
	    new->next = daemon->ptr;
	    daemon->ptr = new;
	    new->name = dom;
	    new->ptr = target;
	  }
	break;
      }

    case LOPT_NAPTR: /* --naptr-record */
      {
	char *a[7] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	int k = 0;
	struct naptr *new;
	int order, pref;
	char *name=NULL, *replace = NULL;

	if ((a[0] = arg))
	  for (k = 1; k < 7; k++)
	    if (!(a[k] = split(a[k-1])))
	      break;
	
	
	if (k < 6 || 
	    !(name = canonicalise_opt(a[0])) ||
	    !atoi_check16(a[1], &order) || 
	    !atoi_check16(a[2], &pref) ||
	    (k == 7 && !(replace = canonicalise_opt(a[6]))))
          {
	    free(name);
	    free(replace);
	    ret_err(_("bad NAPTR record"));
          }
	else
	  {
	    new = opt_malloc(sizeof(struct naptr));
	    new->next = daemon->naptr;
	    daemon->naptr = new;
	    new->name = name;
	    new->flags = opt_string_alloc(a[3]);
	    new->services = opt_string_alloc(a[4]);
	    new->regexp = opt_string_alloc(a[5]);
	    new->replace = replace;
	    new->order = order;
	    new->pref = pref;
	  }
	break;
      }

    case LOPT_RR: /* dns-rr */
      {
       	struct txt_record *new;
	size_t len = 0;
	char *data;
	int class;

	comma = split(arg);
	data = split(comma);
		
	new = opt_malloc(sizeof(struct txt_record));
	new->name = NULL;
	
	if (!atoi_check(comma, &class) || 
	    !(new->name = canonicalise_opt(arg)) ||
	    (data && (len = parse_hex(data, (unsigned char *)data, -1, NULL, NULL)) == -1U))
          {
            free(new->name);
	    ret_err_free(_("bad RR record"), new);
          }

	new->len = 0;
	new->class = class;
	new->next = daemon->rr;
	daemon->rr = new;
	
	if (data)
	  {
	    new->txt = opt_malloc(len);
	    new->len = len;
	    memcpy(new->txt, data, len);
	  }
	
	break;
      }

    case LOPT_CAA: /* --caa-record */
      {
       	struct txt_record *new;
	char *tag, *value;
	int flags;
	
	comma = split(arg);
	tag = split(comma);
	value = split(tag);
	
	new = opt_malloc(sizeof(struct txt_record));
	new->next = daemon->rr;
	daemon->rr = new;

	if (!atoi_check(comma, &flags) || !tag || !value || !(new->name = canonicalise_opt(arg)))
	  ret_err(_("bad CAA record"));
	
	unhide_metas(tag);
	unhide_metas(value);

	new->len = strlen(tag) + strlen(value) + 2;
	new->txt = opt_malloc(new->len);
	new->txt[0] = flags;
	new->txt[1] = strlen(tag);
	memcpy(&new->txt[2], tag, strlen(tag));
	memcpy(&new->txt[2 + strlen(tag)], value, strlen(value));
	new->class = T_CAA;
	
	break;
      }
	
    case 'Y':  /* --txt-record */
      {
	struct txt_record *new;
	unsigned char *p, *cnt;
	size_t len;

	comma = split(arg);
		
	new = opt_malloc(sizeof(struct txt_record));
	new->class = C_IN;
	new->stat = 0;

	if (!(new->name = canonicalise_opt(arg)))
	  ret_err_free(_("bad TXT record"), new);
	
	new->next = daemon->txt;
	daemon->txt = new;
	len = comma ? strlen(comma) : 0;
	len += (len/255) + 1; /* room for extra counts */
	new->txt = p = opt_malloc(len);

	cnt = p++;
	*cnt = 0;
	
	while (comma && *comma)
	  {
	    unsigned char c = (unsigned char)*comma++;

	    if (c == ',' || *cnt == 255)
	      {
		if (c != ',')
		  comma--;
		cnt = p++;
		*cnt = 0;
	      }
	    else
	      {
		*p++ = unhide_meta(c);
		(*cnt)++;
	      }
	  }

	new->len = p - new->txt;

	break;
      }
      
    case 'W':  /* --srv-host */
      {
	int port = 1, priority = 0, weight = 0;
	char *name, *target = NULL;
	struct mx_srv_record *new;
	
	comma = split(arg);
	
	if (!(name = canonicalise_opt(arg)))
	  ret_err(_("bad SRV record"));
	
	if (comma)
	  {
	    arg = comma;
	    comma = split(arg);
	    if (!(target = canonicalise_opt(arg)))
	      ret_err_free(_("bad SRV target"), name);
		
	    if (comma)
	      {
		arg = comma;
		comma = split(arg);
		if (!atoi_check16(arg, &port))
                  {
                    free(name);
		    ret_err_free(_("invalid port number"), target);
                  }
		
		if (comma)
		  {
		    arg = comma;
		    comma = split(arg);
		    if (!atoi_check16(arg, &priority))
                      {
                        free(name);
		        ret_err_free(_("invalid priority"), target);
		      }
		    if (comma && !atoi_check16(comma, &weight))
                      {
                        free(name);
		        ret_err_free(_("invalid weight"), target);
                      }
		  }
	      }
	  }
	
	new = opt_malloc(sizeof(struct mx_srv_record));
	new->next = daemon->mxnames;
	daemon->mxnames = new;
	new->issrv = 1;
	new->name = name;
	new->target = target;
	new->srvport = port;
	new->priority = priority;
	new->weight = weight;
	break;
      }
      
    case LOPT_HOST_REC: /* --host-record */
      {
	struct host_record *new;

	if (!arg || !(comma = split(arg)))
	  ret_err(_("Bad host-record"));
	
	new = opt_malloc(sizeof(struct host_record));
	memset(new, 0, sizeof(struct host_record));
	new->ttl = -1;
	new->flags = 0;

	while (arg)
	  {
	    union all_addr addr;
	    char *dig;

	    for (dig = arg; *dig != 0; dig++)
	      if (*dig < '0' || *dig > '9')
		break;
	    if (*dig == 0)
	      new->ttl = atoi(arg);
	    else if (inet_pton(AF_INET, arg, &addr.addr4))
	      {
		new->addr = addr.addr4;
		new->flags |= HR_4;
	      }
	    else if (inet_pton(AF_INET6, arg, &addr.addr6))
	      {
		new->addr6 = addr.addr6;
		new->flags |= HR_6;
	      }
	    else
	      {
		char *canon = canonicalise_opt(arg);
		struct name_list *nl;
		if (!canon)
                  {
		    struct name_list *tmp, *next;
		    for (tmp = new->names; tmp; tmp = next)
		      {
			next = tmp->next;
			free(tmp);
		      }
		    ret_err_free(_("Bad name in host-record"), new);
                  }

		nl = opt_malloc(sizeof(struct name_list));
		nl->name = canon;
		/* keep order, so that PTR record goes to first name */
		nl->next = NULL;
		if (!new->names)
		  new->names = nl;
		else
		  { 
		    struct name_list *tmp;
		    for (tmp = new->names; tmp->next; tmp = tmp->next);
		    tmp->next = nl;
		  }
	      }
	    
	    arg = comma;
	    comma = split(arg);
	  }

	/* Keep list order */
	if (!daemon->host_records_tail)
	  daemon->host_records = new;
	else
	  daemon->host_records_tail->next = new;
	new->next = NULL;
	daemon->host_records_tail = new;
	break;
      }

    case LOPT_STALE_CACHE: /* --use-stale-cache */
      {
	int max_expiry = STALE_CACHE_EXPIRY;
	if (arg)
	  {
	    /* Don't accept negative TTLs here, they'd have the counter-intuitive
	       side-effect of evicting cache records before they expire */
	    if (!atoi_check(arg, &max_expiry) || max_expiry < 0)
	      ret_err(gen_err);
	    /* Store "serve expired forever" as -1 internally, the option isn't
	       active for daemon->cache_max_expiry == 0 */
	    if (max_expiry == 0)
	      max_expiry = -1;
	  }
	daemon->cache_max_expiry = max_expiry;
	break;
      }

#ifdef HAVE_DNSSEC
    case LOPT_DNSSEC_LIMITS:
      {
	int lim, val;

	for (lim = LIMIT_SIG_FAIL; arg && lim < LIMIT_MAX ;  lim++, arg = comma)
	  {
	    comma = split(arg);

	    if (!atoi_check(arg, &val))
	      ret_err(gen_err);

	    if (val != 0)
	      daemon->limit[lim] = val;
	  }
	
	break;
      }
      
    case LOPT_DNSSEC_STAMP: /* --dnssec-timestamp */
      daemon->timestamp_file = opt_string_alloc(arg); 
      break;

    case LOPT_DNSSEC_CHECK: /* --dnssec-check-unsigned */
      if (arg)
	{
	  if (strcmp(arg, "no") == 0)
	    set_option_bool(OPT_DNSSEC_IGN_NS);
	  else
	    ret_err(_("bad value for dnssec-check-unsigned"));
	}
      break;
      
    case LOPT_TRUST_ANCHOR: /* --trust-anchor */
      {
	struct ds_config *new = opt_malloc(sizeof(struct ds_config));
      	char *cp, *cp1, *keyhex, *digest, *algo = NULL;
	int len;
	
	new->class = C_IN;
	new->name = NULL;
	new->digestlen = 0;
	
	if ((comma = split(arg)) && (algo = split(comma)))
	  {
	    int class = 0;
	    if (strcmp(comma, "IN") == 0)
	      class = C_IN;
	    else if (strcmp(comma, "CH") == 0)
	      class = C_CHAOS;
	    else if (strcmp(comma, "HS") == 0)
	      class = C_HESIOD;
	    
	    if (class != 0)
	      {
		new->class = class;
		comma = algo;
		algo = split(comma);
	      }
	  }
	
	if (!(new->name = canonicalise_opt(arg)))
	  ret_err_free(_("bad trust anchor"), new);

	if (comma)
	  {
	    if (!algo || !(digest = split(algo)) || !(keyhex = split(digest)) ||
		!atoi_check16(comma, &new->keytag) || 
		!atoi_check8(algo, &new->algo) ||
		!atoi_check8(digest, &new->digest_type))
	      {
		free(new->name);
		ret_err_free(_("bad trust anchor"), new);
	      }
	    
	    /* Upper bound on length */
	    len = (2*strlen(keyhex))+1;
	    new->digest = opt_malloc(len);
	    unhide_metas(keyhex);
	    /* 4034: "Whitespace is allowed within digits" */
	    for (cp = keyhex; *cp; )
	      if (isspace((unsigned char)*cp))
		for (cp1 = cp; *cp1; cp1++)
		  *cp1 = *(cp1+1);
	      else
		cp++;
	    if ((new->digestlen = parse_hex(keyhex, (unsigned char *)new->digest, len, NULL, NULL)) == -1)
	      {
		free(new->name);
		ret_err_free(_("bad HEX in trust anchor"), new);
	      }
	  }
	
	new->next = daemon->ds;
	daemon->ds = new;
	
	break;
      }
#endif

    case LOPT_MAX_PROCS: /* --max-tcp-connections */
      {
	int max_procs;
	/* Don't accept numbers less than 1. */
	if (!atoi_check(arg, &max_procs) || max_procs < 1)
	  ret_err(gen_err);
	daemon->max_procs = max_procs;
	break;
      }

    default:
      ret_err(_("unsupported option (check that dnsmasq was compiled with DHCP/TFTP/DNSSEC/DBus support)"));
      
    }
  
  return 1;
}

static void read_file(char *file, FILE *f, int hard_opt, int from_script)	
{
  volatile int lineno = 0;
  char *buff = daemon->namebuff;
  
  while (fgets(buff, MAXDNAME, f))
    {
      int white, i;
      volatile int option;
      char *errmess, *p, *arg, *start;
      size_t len;

      option = (hard_opt == LOPT_REV_SERV) ? 0 : hard_opt;

      /* Memory allocation failure longjmps here if mem_recover == 1 */ 
      if (option != 0 || hard_opt == LOPT_REV_SERV)
	{
	  if (setjmp(mem_jmp))
	    continue;
	  mem_recover = 1;
	}

      arg = NULL;
      lineno++;
      errmess = NULL;
      
      /* Implement quotes, inside quotes we allow \\ \" \n and \t 
	 metacharacters get hidden also strip comments */
      for (white = 1, p = buff; *p; p++)
	{
	  if (*p == '"')
	    {
	      memmove(p, p+1, strlen(p+1)+1);

	      for(; *p && *p != '"'; p++)
		{
		  if (*p == '\\' && strchr("\"tnebr\\", p[1]))
		    {
		      if (p[1] == 't')
			p[1] = '\t';
		      else if (p[1] == 'n')
			p[1] = '\n';
		      else if (p[1] == 'b')
			p[1] = '\b';
		      else if (p[1] == 'r')
			p[1] = '\r';
		      else if (p[1] == 'e') /* escape */
			p[1] = '\033';
		      memmove(p, p+1, strlen(p+1)+1);
		    }
		  *p = hide_meta(*p);
		}

	      if (*p == 0) 
		{
		  errmess = _("missing \"");
		  goto oops; 
		}

	      memmove(p, p+1, strlen(p+1)+1);
	    }

	  if (isspace((unsigned char)*p))
	    {
	      *p = ' ';
	      white = 1;
	    }
	  else 
	    {
	      if (white && *p == '#')
		{ 
		  *p = 0;
		  break;
		}
	      white = 0;
	    } 
	}

      
      /* strip leading spaces */
      for (start = buff; *start && *start == ' '; start++);
      
      /* strip trailing spaces */
      for (len = strlen(start); (len != 0) && (start[len-1] == ' '); len--);
      
      if (len == 0)
	continue; 
      else
	start[len] = 0;
      
      if (option != 0)
	arg = start;
      else if ((p=strchr(start, '=')))
	{
	  /* allow spaces around "=" */
	  for (arg = p+1; *arg == ' '; arg++);
	  for (; p >= start && (*p == ' ' || *p == '='); p--)
	    *p = 0;
	}
      else
	arg = NULL;

      if (option == 0)
	{
	  for (option = 0, i = 0; opts[i].name; i++) 
	    if (strcmp(opts[i].name, start) == 0)
	      {
		option = opts[i].val;
		break;
	      }
	  
	  if (!option)
	    errmess = _("bad option");
	  else if (opts[i].has_arg == 0 && arg)
	    errmess = _("extraneous parameter");
	  else if (opts[i].has_arg == 1 && !arg)
	    errmess = _("missing parameter");
	  else if (hard_opt == LOPT_REV_SERV && option != 'S' && option != LOPT_REV_SERV)
	    errmess = _("illegal option");
	}

    oops:
      if (errmess)
	strcpy(daemon->namebuff, errmess);
	  
      if (errmess || !one_opt(option, arg, daemon->namebuff, _("error"), 0, hard_opt == LOPT_REV_SERV))
	{
	  if (from_script)
	    sprintf(daemon->namebuff + strlen(daemon->namebuff), _(" in output from %s"), file);
	  else
	    sprintf(daemon->namebuff + strlen(daemon->namebuff), _(" at line %d of %s"), lineno, file);
	  
	  if (hard_opt != 0)
	    my_syslog(LOG_ERR, "%s", daemon->namebuff);
	  else
	    die("%s", daemon->namebuff, EC_BADCONF);
	}
    }

  mem_recover = 0;
}

#if defined(HAVE_DHCP) && defined(HAVE_INOTIFY)
int option_read_dynfile(char *file, int flags)
{
  my_syslog(MS_DHCP | LOG_INFO, _("read %s"), file);
  
  if (flags & AH_DHCP_HST)
    return one_file(file, LOPT_BANK);
  else if (flags & AH_DHCP_OPT)
    return one_file(file, LOPT_OPTS);
  
  return 0;
}
#endif

static int one_file(char *file, int hard_opt)
{
  FILE *f;
  int nofile_ok = 0, do_popen = 0;
  static int read_stdin = 0;
  static struct fileread {
    dev_t dev;
    ino_t ino;
    struct fileread *next;
  } *filesread = NULL;
  
  if (hard_opt == LOPT_CONF_OPT)
    {
      /* default conf-file reading */
      hard_opt = 0;
      nofile_ok = 1;
    }

   if (hard_opt == LOPT_CONF_SCRIPT)
     {
       hard_opt = 0;
       do_popen = 1;
     }
   
   if (hard_opt == 0 && !do_popen && strcmp(file, "-") == 0)
    {
      if (read_stdin == 1)
	return 1;
      read_stdin = 1;
      file = "stdin";
      f = stdin;
    }
  else
    {
      /* ignore repeated files. */
      struct stat statbuf;
    
      if (hard_opt == 0 && stat(file, &statbuf) == 0)
	{
	  struct fileread *r;
	  
	  for (r = filesread; r; r = r->next)
	    if (r->dev == statbuf.st_dev && r->ino == statbuf.st_ino)
	      return 1;
	  
	  r = safe_malloc(sizeof(struct fileread));
	  r->next = filesread;
	  filesread = r;
	  r->dev = statbuf.st_dev;
	  r->ino = statbuf.st_ino;
	}

      if (do_popen)
	{
	  if (!(f = popen(file, "r")))
	    die(_("cannot execute %s: %s"), file, EC_FILE);
	}
      else if (!(f = fopen(file, "r")))
	{   
	  if (errno == ENOENT && nofile_ok)
	    return 1; /* No conffile, all done. */
	  else
	    {
	      char *str = _("cannot read %s: %s");
	      if (hard_opt != 0)
		{
		  my_syslog(LOG_ERR, str, file, strerror(errno));
		  return 0;
		}
	      else
		die(str, file, EC_FILE);
	    }
	} 
    }
  
   read_file(file, f, hard_opt, do_popen);

  if (do_popen)
    {
      int rc;

      if ((rc = pclose(f)) == -1)
	die(_("error executing %s: %s"), file, EC_MISC);

      if (rc != 0)
	die(_("%s returns non-zero error code"), file, rc+10);
    }
  else
    fclose(f);
	
  return 1;
}

static int file_filter(const struct dirent *ent)
{
  size_t lenfile = strlen(ent->d_name);

  /* ignore emacs backups and dotfiles */

  if (lenfile == 0 || 
      ent->d_name[lenfile - 1] == '~' ||
      (ent->d_name[0] == '#' && ent->d_name[lenfile - 1] == '#') ||
      ent->d_name[0] == '.')
    return 0;

  return 1;
}
/* expand any name which is a directory */
struct hostsfile *expand_filelist(struct hostsfile *list)
{
  unsigned int i;
  int entcnt, n;
  struct hostsfile *ah, *last, *next, **up;
  struct dirent **namelist;

  /* find largest used index */
  for (i = SRC_AH, ah = list; ah; ah = ah->next)
    {
      last = ah;
      
      if (i <= ah->index)
	i = ah->index + 1;

      if (ah->flags & AH_DIR)
	ah->flags |= AH_INACTIVE;
      else
	ah->flags &= ~AH_INACTIVE;
    }

  for (ah = list; ah; ah = ah->next)
    if (!(ah->flags & AH_INACTIVE))
      {
	struct stat buf;
	if (stat(ah->fname, &buf) != -1 && S_ISDIR(buf.st_mode))
	  {
	    struct dirent *ent;
	    
	    /* don't read this as a file */
	    ah->flags |= AH_INACTIVE;
	    
	    entcnt = scandir(ah->fname, &namelist, file_filter, alphasort);
	    if (entcnt < 0)
	      my_syslog(LOG_ERR, _("cannot access directory %s: %s"), 
			ah->fname, strerror(errno));
	    else
	      {
		for (n = 0; n < entcnt; n++)
		  {
		    ent = namelist[n];
		    size_t lendir = strlen(ah->fname);
		    size_t lenfile = strlen(ent->d_name);
		    struct hostsfile *ah1;
		    char *path;
		    
		    /* see if we have an existing record.
		       dir is ah->fname 
		       file is ent->d_name
		       path to match is ah1->fname */
		    
		    for (up = &list, ah1 = list; ah1; ah1 = next)
		      {
			next = ah1->next;

			if (lendir < strlen(ah1->fname) &&
			    strstr(ah1->fname, ah->fname) == ah1->fname &&
			    ah1->fname[lendir] == '/' &&
			    strcmp(ah1->fname + lendir + 1, ent->d_name) == 0)
			  {
			    ah1->flags &= ~AH_INACTIVE;
			    /* If found, remove from list to re-insert at the end.
			       Unless it's already at the end. */
			    if (last != ah1)
			      *up = next;
			    break;
			  }

			up = &ah1->next;
		      }
		    
		    /* make new record */
		    if (!ah1)
		      {
			if (!(ah1 = whine_malloc(sizeof(struct hostsfile))))
			  continue;
			
			if (!(path = whine_malloc(lendir + lenfile + 2)))
			  {
			    free(ah1);
			    continue;
			  }
		      	
			strcpy(path, ah->fname);
			strcat(path, "/");
			strcat(path, ent->d_name);
			ah1->fname = path;
			ah1->index = i++;
			ah1->flags = AH_DIR;
		      }

		    /* Edge case, may be the last in the list anyway */
		    if (last != ah1)
		      last->next = ah1;
		    ah1->next = NULL;
		    last = ah1;
		    
		    /* inactivate record if not regular file */
		    if ((ah1->flags & AH_DIR) && stat(ah1->fname, &buf) != -1 && !S_ISREG(buf.st_mode))
		      ah1->flags |= AH_INACTIVE; 
		    
		  }
	      }
	    free(namelist);
	  }
      }
  
  return list;
}

void read_servers_file(void)
{
  FILE *f;

  if (!(f = fopen(daemon->servers_file, "r")))
    {
       my_syslog(LOG_ERR, _("cannot read %s: %s"), daemon->servers_file, strerror(errno));
       return;
    }
  
  mark_servers(SERV_FROM_FILE);
  read_file(daemon->servers_file, f, LOPT_REV_SERV, 0);
  fclose(f);
  cleanup_servers();
  check_servers(0);
}
 

#ifdef HAVE_DHCP
static void clear_dynamic_conf(void)
{
  struct dhcp_config *configs, *cp, **up;
  
  /* remove existing... */
  for (up = &daemon->dhcp_conf, configs = daemon->dhcp_conf; configs; configs = cp)
    {
      cp = configs->next;
      
      if (configs->flags & CONFIG_BANK)
	{
	  *up = cp;
	  dhcp_config_free(configs);
	}
      else
	up = &configs->next;
    }
}

static void clear_dhcp_opt(struct dhcp_opt **dhcp_opts)
{
  struct dhcp_opt *opts, *cp, **up;

  for (up = dhcp_opts, opts = *dhcp_opts; opts; opts = cp)
    {
      cp = opts->next;
      
      if (opts->flags & DHOPT_BANK)
	{
	  *up = cp;
	  dhcp_opt_free(opts);
	}
      else
	up = &opts->next;
    }
}

static void clear_dynamic_opt(void)
{
  clear_dhcp_opt(&daemon->dhcp_opts);
#ifdef HAVE_DHCP6
  clear_dhcp_opt(&daemon->dhcp_opts6);
#endif
}

void reread_dhcp(void)
{
   struct hostsfile *hf;

   /* Do these even if there is no daemon->dhcp_hosts_file or
      daemon->dhcp_opts_file since entries may have been created by the
      inotify dynamic file reading system. */
   
   clear_dynamic_conf();
   clear_dynamic_opt();

   if (daemon->dhcp_hosts_file)
    {
      daemon->dhcp_hosts_file = expand_filelist(daemon->dhcp_hosts_file);
      for (hf = daemon->dhcp_hosts_file; hf; hf = hf->next)
	if (!(hf->flags & AH_INACTIVE))
	  {
	    if (one_file(hf->fname, LOPT_BANK))  
	      my_syslog(MS_DHCP | LOG_INFO, _("read %s"), hf->fname);
	  }
    }

  if (daemon->dhcp_opts_file)
    {
      daemon->dhcp_opts_file = expand_filelist(daemon->dhcp_opts_file);
      for (hf = daemon->dhcp_opts_file; hf; hf = hf->next)
	if (!(hf->flags & AH_INACTIVE))
	  {
	    if (one_file(hf->fname, LOPT_OPTS))  
	      my_syslog(MS_DHCP | LOG_INFO, _("read %s"), hf->fname);
	  }
    }

#  ifdef HAVE_INOTIFY
  /* Setup notify and read pre-existing files. */
  set_dynamic_inotify(AH_DHCP_HST | AH_DHCP_OPT, 0, NULL, 0);
#  endif
}
#endif

void read_opts(int argc, char **argv, char *compile_opts)
{
  size_t argbuf_size = MAXDNAME;
  char *argbuf = opt_malloc(argbuf_size);
  /* Note that both /000 and '.' are allowed within labels. These get
     represented in presentation format using NAME_ESCAPE as an escape
     character. In theory, if all the characters in a name were /000 or
     '.' or NAME_ESCAPE then all would have to be escaped, so the 
     presentation format would be twice as long as the spec. */
  char *buff = opt_malloc((MAXDNAME * 2) + 1);
  int option, testmode = 0;
  char *arg, *conffile = NULL;
  
  opterr = 0;

  daemon = opt_malloc(sizeof(struct daemon));
  memset(daemon, 0, sizeof(struct daemon));
  daemon->namebuff = buff;
  daemon->workspacename = safe_malloc((MAXDNAME * 2) + 1);
  daemon->addrbuff = safe_malloc(ADDRSTRLEN);
  
  /* Set defaults - everything else is zero or NULL */
  daemon->cachesize = CACHESIZ;
  daemon->ftabsize = FTABSIZ;
  daemon->port = NAMESERVER_PORT;
  daemon->dhcp_client_port = DHCP_CLIENT_PORT;
  daemon->dhcp_server_port = DHCP_SERVER_PORT;
  daemon->default_resolv.is_default = 1;
  daemon->default_resolv.name = RESOLVFILE;
  daemon->resolv_files = &daemon->default_resolv;
  daemon->username = CHUSER;
  daemon->runfile =  RUNFILE;
  daemon->dhcp_max = MAXLEASES;
  daemon->tftp_max = TFTP_MAX_CONNECTIONS;
  daemon->edns_pktsz = EDNS_PKTSZ;
  daemon->log_fac = -1;
  daemon->auth_ttl = AUTH_TTL; 
  daemon->soa_refresh = SOA_REFRESH;
  daemon->soa_retry = SOA_RETRY;
  daemon->soa_expiry = SOA_EXPIRY;
  daemon->randport_limit = 1;
  daemon->host_index = SRC_AH;
  daemon->max_procs = MAX_PROCS;
#ifdef HAVE_DUMPFILE
  daemon->dump_mask = 0xffffffff;
#endif
#ifdef HAVE_DNSSEC
  daemon->limit[LIMIT_SIG_FAIL] = DNSSEC_LIMIT_SIG_FAIL;
  daemon->limit[LIMIT_CRYPTO] = DNSSEC_LIMIT_CRYPTO;
  daemon->limit[LIMIT_WORK] = DNSSEC_LIMIT_WORK;
  daemon->limit[LIMIT_NSEC3_ITERS] = DNSSEC_LIMIT_NSEC3_ITERS;
#endif
  
  /* See comment above make_servers(). Optimises server-read code. */
  mark_servers(0);
  
  while (1) 
    {
#ifdef HAVE_GETOPT_LONG
      option = getopt_long(argc, argv, OPTSTRING, opts, NULL);
#else
      option = getopt(argc, argv, OPTSTRING);
#endif
      
      if (option == -1)
	{
	  for (; optind < argc; optind++)
	    {
	      unsigned char *c = (unsigned char *)argv[optind];
	      for (; *c != 0; c++)
		if (!isspace(*c))
		  die(_("junk found in command line"), NULL, EC_BADCONF);
	    }
	  break;
	}

      /* Copy optarg so that argv doesn't get changed */
      if (optarg)
	{
	  if (strlen(optarg) >= argbuf_size)
	    {
	      free(argbuf);
	      argbuf_size = strlen(optarg) + 1;
	      argbuf = opt_malloc(argbuf_size);
	    }
	  safe_strncpy(argbuf, optarg, argbuf_size);
	  arg = argbuf;
	}
      else
	arg = NULL;
      
      /* command-line only stuff */
      if (option == LOPT_TEST)
	testmode = 1;
      else if (option == 'w')
	{
#ifdef HAVE_DHCP
	  if (argc == 3 && strcmp(argv[2], "dhcp") == 0)
	    display_opts();
#ifdef HAVE_DHCP6
	  else if (argc == 3 && strcmp(argv[2], "dhcp6") == 0)
	    display_opts6();
#endif
	  else
#endif
	    do_usage();

	  exit(0);
	}
      else if (option == 'v')
	{
	  printf(_("Dnsmasq version %s  %s\n"), VERSION, COPYRIGHT);
	  printf(_("Compile time options: %s\n\n"), compile_opts); 
	  printf(_("This software comes with ABSOLUTELY NO WARRANTY.\n"));
	  printf(_("Dnsmasq is free software, and you are welcome to redistribute it\n"));
	  printf(_("under the terms of the GNU General Public License, version 2 or 3.\n"));
          exit(0);
        }
      else if (option == 'C')
	{
          if (!conffile)
	    conffile = opt_string_alloc(arg);
	  else
	    {
	      char *extra = opt_string_alloc(arg);
	      one_file(extra, 0);
	      free(extra);
	    }
	}
      else
	{
#ifdef HAVE_GETOPT_LONG
	  if (!one_opt(option, arg, daemon->namebuff, _("try --help"), 1, 0))
#else 
	    if (!one_opt(option, arg, daemon->namebuff, _("try -w"), 1, 0)) 
#endif  
	    die(_("bad command line options: %s"), daemon->namebuff, EC_BADCONF);
	}
    }

  free(argbuf);

  if (conffile)
    {
      one_file(conffile, 0);
      free(conffile);
    }
  else
    one_file(CONFFILE, LOPT_CONF_OPT);

  /* Add TXT records if wanted */
#ifndef NO_ID
  if (!option_bool(OPT_NO_IDENT))
    {
      add_txt("version.bind", "dnsmasq-" VERSION, 0 );
      add_txt("authors.bind", "Simon Kelley", 0);
      add_txt("copyright.bind", COPYRIGHT, 0);
      add_txt("cachesize.bind", NULL, TXT_STAT_CACHESIZE);
      add_txt("insertions.bind", NULL, TXT_STAT_INSERTS);
      add_txt("evictions.bind", NULL, TXT_STAT_EVICTIONS);
      add_txt("misses.bind", NULL, TXT_STAT_MISSES);
      add_txt("hits.bind", NULL, TXT_STAT_HITS);
#ifdef HAVE_AUTH
      add_txt("auth.bind", NULL, TXT_STAT_AUTH);
#endif
      add_txt("servers.bind", NULL, TXT_STAT_SERVERS);
    }
#endif

#ifdef HAVE_DNSSEC
  /* Default fast retry on when doing DNSSEC */
  if (option_bool(OPT_DNSSEC_VALID) && daemon->fast_retry_time == 0)
    {
      daemon->fast_retry_timeout = TIMEOUT;
      daemon->fast_retry_time = DEFAULT_FAST_RETRY;
    }
#endif
  
  /* port might not be known when the address is parsed - fill in here */
  if (daemon->servers)
    {
      struct server *tmp;
      for (tmp = daemon->servers; tmp; tmp = tmp->next)
	if (!(tmp->flags & SERV_HAS_SOURCE))
	  {
	    if (tmp->source_addr.sa.sa_family == AF_INET)
	      tmp->source_addr.in.sin_port = htons(daemon->query_port);
	    else if (tmp->source_addr.sa.sa_family == AF_INET6)
	      tmp->source_addr.in6.sin6_port = htons(daemon->query_port);
	  }
    } 
  
  if (daemon->host_records)
    {
      struct host_record *hr;
      
      for (hr = daemon->host_records; hr; hr = hr->next)
	if (hr->ttl == -1)
	  hr->ttl = daemon->local_ttl;
    }

  if (daemon->cnames)
    {
      struct cname *cn, *cn2, *cn3;

#define NOLOOP 1
#define TESTLOOP 2      

      /* Fill in TTL for CNAMES now we have local_ttl.
	 Also prepare to do loop detection. */
      for (cn = daemon->cnames; cn; cn = cn->next)
	{
	  if (cn->ttl == -1)
	    cn->ttl = daemon->local_ttl;
	  cn->flag = 0;
	  cn->targetp = NULL;
	  for (cn2 = daemon->cnames; cn2; cn2 = cn2->next)
	    if (hostname_isequal(cn->target, cn2->alias))
	      {
		cn->targetp = cn2;
		break;
	      }
	}
      
      /* Find any CNAME loops.*/
      for (cn = daemon->cnames; cn; cn = cn->next)
	{
	  for (cn2 = cn->targetp; cn2; cn2 = cn2->targetp)
	    {
	      if (cn2->flag == NOLOOP)
		break;
	      
	      if (cn2->flag == TESTLOOP)
		die(_("CNAME loop involving %s"), cn->alias, EC_BADCONF);
	      
	      cn2->flag = TESTLOOP;
	    }
	  
	  for (cn3 = cn->targetp; cn3 != cn2; cn3 = cn3->targetp)
	    cn3->flag = NOLOOP;
	}
    }

  if (daemon->if_addrs)
    {  
      struct iname *tmp;
      for(tmp = daemon->if_addrs; tmp; tmp = tmp->next)
	if (tmp->addr.sa.sa_family == AF_INET)
	  tmp->addr.in.sin_port = htons(daemon->port);
	else if (tmp->addr.sa.sa_family == AF_INET6)
	  tmp->addr.in6.sin6_port = htons(daemon->port);
    }
	
  /* create default, if not specified */
  if (daemon->authserver && !daemon->hostmaster)
    {
      strcpy(buff, "hostmaster.");
      strcat(buff, daemon->authserver);
      daemon->hostmaster = opt_string_alloc(buff);
    }

  if (!daemon->dhcp_pxe_vendors)
    {
      daemon->dhcp_pxe_vendors = opt_malloc(sizeof(struct dhcp_pxe_vendor));
      daemon->dhcp_pxe_vendors->data = opt_string_alloc(DHCP_PXE_DEF_VENDOR);
      daemon->dhcp_pxe_vendors->next = NULL;
    }
  
  /* only one of these need be specified: the other defaults to the host-name */
  if (option_bool(OPT_LOCALMX) || daemon->mxnames || daemon->mxtarget)
    {
      struct mx_srv_record *mx;
      
      if (gethostname(buff, MAXDNAME) == -1)
	die(_("cannot get host-name: %s"), NULL, EC_MISC);
      
      for (mx = daemon->mxnames; mx; mx = mx->next)
	if (!mx->issrv && hostname_isequal(mx->name, buff))
	  break;
      
      if ((daemon->mxtarget || option_bool(OPT_LOCALMX)) && !mx)
	{
	  mx = opt_malloc(sizeof(struct mx_srv_record));
	  mx->next = daemon->mxnames;
	  mx->issrv = 0;
	  mx->target = NULL;
	  mx->name = opt_string_alloc(buff);
	  daemon->mxnames = mx;
	}
      
      if (!daemon->mxtarget)
	daemon->mxtarget = opt_string_alloc(buff);

      for (mx = daemon->mxnames; mx; mx = mx->next)
	if (!mx->issrv && !mx->target)
	  mx->target = daemon->mxtarget;
    }

  if (!option_bool(OPT_NO_RESOLV) &&
      daemon->resolv_files && 
      daemon->resolv_files->next && 
      option_bool(OPT_NO_POLL))
    die(_("only one resolv.conf file allowed in no-poll mode."), NULL, EC_BADCONF);
  
  if (option_bool(OPT_RESOLV_DOMAIN))
    {
      char *line;
      FILE *f;

      if (option_bool(OPT_NO_RESOLV) ||
	  !daemon->resolv_files || 
	  (daemon->resolv_files)->next)
	die(_("must have exactly one resolv.conf to read domain from."), NULL, EC_BADCONF);
      
      if (!(f = fopen((daemon->resolv_files)->name, "r")))
	die(_("failed to read %s: %s"), (daemon->resolv_files)->name, EC_FILE);
      
      while ((line = fgets(buff, MAXDNAME, f)))
	{
	  char *token = strtok(line, " \t\n\r");
	  
	  if (!token || strcmp(token, "search") != 0)
	    continue;
	  
	  if ((token = strtok(NULL, " \t\n\r")) &&  
	      (daemon->domain_suffix = canonicalise_opt(token)))
	    break;
	}

      fclose(f);

      if (!daemon->domain_suffix)
	die(_("no search directive found in %s"), (daemon->resolv_files)->name, EC_MISC);
    }

  if (daemon->domain_suffix)
    {
       /* add domain for any srv record without one. */
      struct mx_srv_record *srv;
      
      for (srv = daemon->mxnames; srv; srv = srv->next)
	if (srv->issrv &&
	    strchr(srv->name, '.') && 
	    strchr(srv->name, '.') == strrchr(srv->name, '.'))
	  {
	    if (strlen(srv->name) + 1 + strlen(daemon->domain_suffix) > MAXDNAME)
	      die(_("srv-host name %s too long after domain appended"), srv->name, EC_MISC);
	    strcpy(buff, srv->name);
	    strcat(buff, ".");
	    strcat(buff, daemon->domain_suffix);
	    free(srv->name);
	    srv->name = opt_string_alloc(buff);
	  }
    }
  else if (option_bool(OPT_DHCP_FQDN))
    die(_("there must be a default domain when --dhcp-fqdn is set"), NULL, EC_BADCONF);

  /* If there's access-control config, then ignore --local-service, it's intended
     as a system default to keep otherwise unconfigured installations safe. */
  if (daemon->if_names || daemon->if_except || daemon->if_addrs || daemon->authserver)
    {
      reset_option_bool(OPT_LOCAL_SERVICE);
      reset_option_bool(OPT_LOCALHOST_SERVICE);
    }
  else if (option_bool(OPT_LOCALHOST_SERVICE) && !option_bool(OPT_LOCAL_SERVICE))
    {
      /* listen only on localhost, emulate --interface=lo --bind-interfaces */
      if_names_add(NULL);
      set_option_bool(OPT_NOWILD);
    }

  if (testmode)
    {
      fprintf(stderr, "dnsmasq: %s.\n", _("syntax check OK"));
      exit(0);
    }
}  
