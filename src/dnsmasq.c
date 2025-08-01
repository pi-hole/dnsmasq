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

/* Declare static char *compiler_opts  in config.h */
#define DNSMASQ_COMPILE_OPTS

/* dnsmasq.h has to be included first as it sources config.h */
#include "dnsmasq.h"

#if defined(HAVE_IDN) || defined(HAVE_LIBIDN2) || defined(LOCALEDIR)
#include <locale.h>
#endif

struct daemon *daemon;

static volatile pid_t pid = 0;
static volatile int pipewrite;

static void set_dns_listeners(void);
#ifdef HAVE_TFTP
static void set_tftp_listeners(void);
#endif
static void check_dns_listeners(time_t now);
static void do_tcp_connection(struct listener *listener, time_t now, int slot);
static void sig_handler(int sig);
static void async_event(int pipe, time_t now);
static void fatal_event(struct event_desc *ev, char *msg);
static int read_event(int fd, struct event_desc *evp, char **msg);
static void poll_resolv(int force, int do_reload, time_t now);

int main (int argc, char **argv)
{
  time_t now;
  struct sigaction sigact;
  struct iname *if_tmp;
  int piperead, pipefd[2], err_pipe[2];
  struct passwd *ent_pw = NULL;
#if defined(HAVE_SCRIPT)
  uid_t script_uid = 0;
  gid_t script_gid = 0;
#endif
  struct group *gp = NULL;
  long i, max_fd = sysconf(_SC_OPEN_MAX);
  char *baduser = NULL;
  int log_err;
  int chown_warn = 0;
#if defined(HAVE_LINUX_NETWORK)
  cap_user_header_t hdr = NULL;
  cap_user_data_t data = NULL;
  int need_cap_net_admin = 0;
  int need_cap_net_raw = 0;
  int need_cap_net_bind_service = 0;
  int have_cap_chown = 0;
#  ifdef HAVE_DHCP
  char *bound_device = NULL;
  int did_bind = 0;
#  endif
  struct server *serv;
  char *netlink_warn;
#else
  int bind_fallback = 0;
#endif 
#if defined(HAVE_DHCP)
  struct dhcp_context *context;
  struct dhcp_relay *relay;
#endif
#ifdef HAVE_TFTP
  int tftp_prefix_missing = 0;
#endif

#ifdef HAVE_LINUX_NETWORK
  (void)netlink_warn;
#endif
  
#if defined(HAVE_IDN) || defined(HAVE_LIBIDN2) || defined(LOCALEDIR)
  setlocale(LC_ALL, "");
#endif
#ifdef LOCALEDIR
  bindtextdomain("dnsmasq", LOCALEDIR); 
  textdomain("dnsmasq");
#endif

  sigact.sa_handler = sig_handler;
  sigact.sa_flags = 0;
  sigemptyset(&sigact.sa_mask);
  sigaction(SIGUSR1, &sigact, NULL);
  sigaction(SIGUSR2, &sigact, NULL);
  sigaction(SIGHUP, &sigact, NULL);
  sigaction(SIGTERM, &sigact, NULL);
  sigaction(SIGALRM, &sigact, NULL);
  sigaction(SIGCHLD, &sigact, NULL);
  sigaction(SIGINT, &sigact, NULL);
  
  /* ignore SIGPIPE */
  sigact.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sigact, NULL);

  umask(022); /* known umask, create leases and pid files as 0644 */

  rand_init(); /* Must precede read_opts() */
  
  read_opts(argc, argv, compile_opts);
 
#ifdef HAVE_LINUX_NETWORK
  daemon->kernel_version = kernel_version();
#endif

  if (daemon->edns_pktsz < PACKETSZ)
    daemon->edns_pktsz = PACKETSZ;

  /* Min buffer size: we check after adding each record, so there must be 
     memory for the largest packet, and the largest record so the
     min for DNS is PACKETSZ+MAXDNAME+RRFIXEDSZ which is < 1000.
     This might be increased is EDNS packet size if greater than the minimum. */ 
  daemon->packet_buff_sz = daemon->edns_pktsz + MAXDNAME + RRFIXEDSZ;
  daemon->packet = safe_malloc(daemon->packet_buff_sz);
  
  if (option_bool(OPT_EXTRALOG))
    daemon->addrbuff2 = safe_malloc(ADDRSTRLEN);
  
#ifdef HAVE_DNSSEC
  if (option_bool(OPT_DNSSEC_VALID))
    {
      /* Note that both /000 and '.' are allowed within labels. These get
	 represented in presentation format using NAME_ESCAPE as an escape
	 character. In theory, if all the characters in a name were /000 or
	 '.' or NAME_ESCAPE then all would have to be escaped, so the 
	 presentation format would be twice as long as the spec. */
      daemon->keyname = safe_malloc((MAXDNAME * 2) + 1);
      daemon->cname = safe_malloc((MAXDNAME * 2) + 1);
      /* one char flag per possible RR in answer section (may get extended). */
      daemon->rr_status_sz = 64;
      daemon->rr_status = safe_malloc(sizeof(*daemon->rr_status) * daemon->rr_status_sz);
    }
#endif
  
#ifdef HAVE_DHCP
  if (!daemon->lease_file)
    {
      if (daemon->dhcp || daemon->dhcp6)
	daemon->lease_file = LEASEFILE;
    }
#endif
  
  /* Ensure that at least stdin, stdout and stderr (fd 0, 1, 2) exist,
     otherwise file descriptors we create can end up being 0, 1, or 2 
     and then get accidentally closed later when we make 0, 1, and 2 
     open to /dev/null. Normally we'll be started with 0, 1 and 2 open, 
     but it's not guaranteed. By opening /dev/null three times, we 
     ensure that we're not using those fds for real stuff. */
  for (i = 0; i < 3; i++)
    open("/dev/null", O_RDWR); 
  
  /* Close any file descriptors we inherited apart from std{in|out|err} */
  close_fds(max_fd, -1, -1, -1);
  
#ifndef HAVE_LINUX_NETWORK
#  if !(defined(IP_RECVDSTADDR) && defined(IP_RECVIF) && defined(IP_SENDSRCADDR))
  if (!option_bool(OPT_NOWILD))
    {
      bind_fallback = 1;
      set_option_bool(OPT_NOWILD);
    }
#  endif
  
  /* -- bind-dynamic not supported on !Linux, fall back to --bind-interfaces */
  if (option_bool(OPT_CLEVERBIND))
    {
      bind_fallback = 1;
      set_option_bool(OPT_NOWILD);
      reset_option_bool(OPT_CLEVERBIND);
    }
#endif

#ifndef HAVE_INOTIFY
  if (daemon->dynamic_dirs)
    die(_("dhcp-hostsdir, dhcp-optsdir and hostsdir are not supported on this platform"), NULL, EC_BADCONF);
#endif
  
  if (option_bool(OPT_DNSSEC_VALID))
    {
#ifdef HAVE_DNSSEC
      struct ds_config *ds;

      /* Must have at least a root trust anchor, or the DNSSEC code
	 can loop forever. */
      for (ds = daemon->ds; ds; ds = ds->next)
	if (ds->name[0] == 0)
	  break;

      if (!ds)
	die(_("no root trust anchor provided for DNSSEC"), NULL, EC_BADCONF);
      
      if (daemon->cachesize < CACHESIZ)
	die(_("cannot reduce cache size from default when DNSSEC enabled"), NULL, EC_BADCONF);
#else 
      die(_("DNSSEC not available: set HAVE_DNSSEC in src/config.h"), NULL, EC_BADCONF);
#endif
    }

#ifndef HAVE_TFTP
  if (option_bool(OPT_TFTP))
    die(_("TFTP server not available: set HAVE_TFTP in src/config.h"), NULL, EC_BADCONF);
#endif

#ifdef HAVE_CONNTRACK
  if (option_bool(OPT_CONNTRACK))
    {
      if (daemon->query_port != 0 || daemon->osport)
	die (_("cannot use --conntrack AND --query-port"), NULL, EC_BADCONF);

      need_cap_net_admin = 1;
    }
#else
  if (option_bool(OPT_CONNTRACK))
    die(_("conntrack support not available: set HAVE_CONNTRACK in src/config.h"), NULL, EC_BADCONF);
#endif

#ifdef HAVE_SOLARIS_NETWORK
  if (daemon->max_logs != 0)
    die(_("asynchronous logging is not available under Solaris"), NULL, EC_BADCONF);
#endif
  
#ifdef __ANDROID__
  if (daemon->max_logs != 0)
    die(_("asynchronous logging is not available under Android"), NULL, EC_BADCONF);
#endif

#ifndef HAVE_AUTH
  if (daemon->auth_zones)
    die(_("authoritative DNS not available: set HAVE_AUTH in src/config.h"), NULL, EC_BADCONF);
#endif

#ifndef HAVE_LOOP
  if (option_bool(OPT_LOOP_DETECT))
    die(_("loop detection not available: set HAVE_LOOP in src/config.h"), NULL, EC_BADCONF);
#endif

#ifndef HAVE_UBUS
  if (option_bool(OPT_UBUS))
    die(_("Ubus not available: set HAVE_UBUS in src/config.h"), NULL, EC_BADCONF);
#endif
  
  /* Handle only one of min_port/max_port being set. */
  if (daemon->min_port != 0 && daemon->max_port == 0)
    daemon->max_port = MAX_PORT;
  
  if (daemon->max_port != 0 && daemon->min_port == 0)
    daemon->min_port = MIN_PORT;
   
  if (daemon->max_port < daemon->min_port)
    die(_("max_port cannot be smaller than min_port"), NULL, EC_BADCONF);

  if (daemon->max_port != 0 &&
      daemon->max_port - daemon->min_port + 1 < daemon->randport_limit)
    die(_("port_limit must not be larger than available port range"), NULL, EC_BADCONF);
  
  now = dnsmasq_time();

  if (daemon->auth_zones)
    {
      if (!daemon->authserver)
	die(_("--auth-server required when an auth zone is defined."), NULL, EC_BADCONF);

      /* Create a serial at startup if not configured. */
#ifdef HAVE_BROKEN_RTC
      if (daemon->soa_sn == 0)
	die(_("zone serial must be configured in --auth-soa"), NULL, EC_BADCONF);
#else
      if (daemon->soa_sn == 0)
	daemon->soa_sn = now;
#endif
    }
  
#ifdef HAVE_DHCP6
  if (daemon->dhcp6)
    {
      daemon->doing_ra = option_bool(OPT_RA);
      
      for (context = daemon->dhcp6; context; context = context->next)
	{
	  if (context->flags & CONTEXT_DHCP)
	    daemon->doing_dhcp6 = 1;
	  if (context->flags & CONTEXT_RA)
	    daemon->doing_ra = 1;
#if !defined(HAVE_LINUX_NETWORK) && !defined(HAVE_BSD_NETWORK)
	  if (context->flags & CONTEXT_TEMPLATE)
	    die (_("dhcp-range constructor not available on this platform"), NULL, EC_BADCONF);
#endif 
	}
    }
#endif
  
#ifdef HAVE_DHCP
  /* Note that order matters here, we must call lease_init before
     creating any file descriptors which shouldn't be leaked
     to the lease-script init process. We need to call common_init
     before lease_init to allocate buffers it uses.
     The script subsystem relies on DHCP buffers, hence the last two
     conditions below. */  
  if (daemon->dhcp || daemon->doing_dhcp6 || daemon->relay4 || 
      daemon->relay6 || option_bool(OPT_TFTP) || option_bool(OPT_SCRIPT_ARP))
    {
      dhcp_common_init();
      if (daemon->dhcp || daemon->doing_dhcp6)
	lease_init(now);
    }
  
  if (daemon->dhcp || daemon->relay4)
    {
      dhcp_init();
#   ifdef HAVE_LINUX_NETWORK
      /* Need NET_RAW to send ping. */
      if (!option_bool(OPT_NO_PING))
	need_cap_net_raw = 1;
      /* Need NET_ADMIN to change ARP cache if not always broadcasting. */
      if (daemon->force_broadcast == NULL || daemon->force_broadcast->list != NULL)
        need_cap_net_admin = 1;
#   endif
    }
  
#  ifdef HAVE_DHCP6
  if (daemon->doing_ra || daemon->doing_dhcp6 || daemon->relay6)
    {
      ra_init(now);
#   ifdef HAVE_LINUX_NETWORK
      need_cap_net_raw = 1;
      need_cap_net_admin = 1;
#   endif
    }
  
  if (daemon->doing_dhcp6 || daemon->relay6)
    dhcp6_init();
#  endif

#endif

#ifdef HAVE_IPSET
  if (daemon->ipsets)
    {
      ipset_init();
#  ifdef HAVE_LINUX_NETWORK
      need_cap_net_admin = 1;
#  endif
    }
#endif

#ifdef HAVE_NFTSET
  if (daemon->nftsets)
    {
      nftset_init();
#  ifdef HAVE_LINUX_NETWORK
      need_cap_net_admin = 1;
#  endif
    }
#endif

#if  defined(HAVE_LINUX_NETWORK)
  netlink_warn = netlink_init();
#elif defined(HAVE_BSD_NETWORK)
  route_init();
#endif

  if (option_bool(OPT_NOWILD) && option_bool(OPT_CLEVERBIND))
    die(_("cannot set --bind-interfaces and --bind-dynamic"), NULL, EC_BADCONF);
  
  if (!enumerate_interfaces(1) || !enumerate_interfaces(0))
    die(_("failed to find list of interfaces: %s"), NULL, EC_MISC);

#ifdef HAVE_DHCP
  /* Determine lease FQDNs after enumerate_interfaces() call, since it needs
     to call get_domain and that's only valid for some domain configs once we
     have interface addresses. */
  lease_calc_fqdns();
#endif
  
  if (option_bool(OPT_NOWILD) || option_bool(OPT_CLEVERBIND)) 
    {
      create_bound_listeners(1);
      
      if (!option_bool(OPT_CLEVERBIND))
	for (if_tmp = daemon->if_names; if_tmp; if_tmp = if_tmp->next)
	  if (if_tmp->name && !(if_tmp->flags & INAME_USED))
	    die(_("unknown interface %s"), if_tmp->name, EC_BADNET);

#if defined(HAVE_LINUX_NETWORK) && defined(HAVE_DHCP)
      /* after enumerate_interfaces()  */
      bound_device = whichdevice();

      if ((did_bind = bind_dhcp_devices(bound_device)) & 2)
	die(_("failed to set SO_BINDTODEVICE on DHCP socket: %s"), NULL, EC_BADNET);	
#endif
    }
  else 
    create_wildcard_listeners();
 
#ifdef HAVE_DHCP6
  /* after enumerate_interfaces() */
  if (daemon->doing_dhcp6 || daemon->relay6 || daemon->doing_ra)
    join_multicast(1);

  /* After netlink_init() and before create_helper() */
  lease_make_duid(now);
#endif
  
  if (daemon->port != 0)
    {
      cache_init();
      blockdata_init();

      /* Scale random socket pool by ftabsize, but
	 limit it based on available fds. */
      daemon->numrrand = daemon->ftabsize/2;
      if (daemon->numrrand > max_fd/3)
	daemon->numrrand = max_fd/3;
      /* safe_malloc returns zero'd memory */
      daemon->randomsocks = safe_malloc(daemon->numrrand * sizeof(struct randfd));

      daemon->tcp_pids = safe_malloc(daemon->max_procs*sizeof(pid_t));
      daemon->tcp_pipes = safe_malloc(daemon->max_procs*sizeof(int));

      for (i = 0; i < daemon->max_procs; i++)
	daemon->tcp_pipes[i] = -1;
    }

#ifdef HAVE_INOTIFY
  if ((daemon->port != 0 && !option_bool(OPT_NO_RESOLV)) ||
      daemon->dynamic_dirs)
    inotify_dnsmasq_init();
  else
    daemon->inotifyfd = -1;
#endif

  if (daemon->dump_file)
#ifdef HAVE_DUMPFILE
    dump_init();
  else 
    daemon->dumpfd = -1;
#else
  die(_("Packet dumps not available: set HAVE_DUMP in src/config.h"), NULL, EC_BADCONF);
#endif
  
  if (option_bool(OPT_DBUS))
#ifdef HAVE_DBUS
    {
      char *err;
      if ((err = dbus_init()))
	die(_("DBus error: %s"), err, EC_MISC);
    }
#else
  die(_("DBus not available: set HAVE_DBUS in src/config.h"), NULL, EC_BADCONF);
#endif

  if (option_bool(OPT_UBUS))
#ifdef HAVE_UBUS
    {
      char *err;
      if ((err = ubus_init()))
	die(_("UBus error: %s"), err, EC_MISC);
    }
#else
  die(_("UBus not available: set HAVE_UBUS in src/config.h"), NULL, EC_BADCONF);
#endif

  if (daemon->port != 0)
    pre_allocate_sfds();

#if defined(HAVE_SCRIPT)
  /* Note getpwnam returns static storage */
  if ((daemon->dhcp || daemon->dhcp6) && 
      daemon->scriptuser && 
      (daemon->lease_change_command || daemon->luascript))
    {
      struct passwd *scr_pw;
      
      if ((scr_pw = getpwnam(daemon->scriptuser)))
	{
	  script_uid = scr_pw->pw_uid;
	  script_gid = scr_pw->pw_gid;
	 }
      else
	baduser = daemon->scriptuser;
    }
#endif
  
  if (daemon->username && !(ent_pw = getpwnam(daemon->username)))
    baduser = daemon->username;
  else if (daemon->groupname && !(gp = getgrnam(daemon->groupname)))
    baduser = daemon->groupname;

  if (baduser)
    die(_("unknown user or group: %s"), baduser, EC_BADCONF);

  /* implement group defaults, "dip" if available, or group associated with uid */
  if (!daemon->group_set && !gp)
    {
      if (!(gp = getgrnam(CHGRP)) && ent_pw)
	gp = getgrgid(ent_pw->pw_gid);
      
      /* for error message */
      if (gp)
	daemon->groupname = gp->gr_name; 
    }

#if defined(HAVE_LINUX_NETWORK)
  /* We keep CAP_NETADMIN (for ARP-injection) and
     CAP_NET_RAW (for icmp) if we're doing dhcp,
     if we have yet to bind ports because of DAD, 
     or we're doing it dynamically, we need CAP_NET_BIND_SERVICE. */
  if ((is_dad_listeners() || option_bool(OPT_CLEVERBIND)) &&
      (option_bool(OPT_TFTP) || (daemon->port != 0 && daemon->port <= 1024)))
    need_cap_net_bind_service = 1;

  /* usptream servers which bind to an interface call SO_BINDTODEVICE
     for each TCP connection, so need CAP_NET_RAW */
  for (serv = daemon->servers; serv; serv = serv->next)
    if (serv->interface[0] != 0)
      need_cap_net_raw = 1;

  /* If we're doing Dbus or UBus, the above can be set dynamically,
     (as can ports) so always (potentially) needed. */
#ifdef HAVE_DBUS
  if (option_bool(OPT_DBUS))
    {
      need_cap_net_bind_service = 1;
      need_cap_net_raw = 1;
    }
#endif

#ifdef HAVE_UBUS
  if (option_bool(OPT_UBUS))
    {
      need_cap_net_bind_service = 1;
      need_cap_net_raw = 1;
    }
#endif
  
  /* determine capability API version here, while we can still
     call safe_malloc */
  int capsize = 1; /* for header version 1 */
  char *fail = NULL;
  
  hdr = safe_malloc(sizeof(*hdr));
  
  /* find version supported by kernel */
  memset(hdr, 0, sizeof(*hdr));
  capget(hdr, NULL);
  
  if (hdr->version != LINUX_CAPABILITY_VERSION_1)
    {
      /* if unknown version, use largest supported version (3) */
      if (hdr->version != LINUX_CAPABILITY_VERSION_2)
	hdr->version = LINUX_CAPABILITY_VERSION_3;
      capsize = 2;
    }
  
  data = safe_malloc(sizeof(*data) * capsize);
  capget(hdr, data); /* Get current values, for verification */

  have_cap_chown = data->permitted & (1 << CAP_CHOWN);

  if (need_cap_net_admin && !(data->permitted & (1 << CAP_NET_ADMIN)))
    fail = "NET_ADMIN";
  else if (need_cap_net_raw && !(data->permitted & (1 << CAP_NET_RAW)))
    fail = "NET_RAW";
  else if (need_cap_net_bind_service && !(data->permitted & (1 << CAP_NET_BIND_SERVICE)))
    fail = "NET_BIND_SERVICE";
  
  if (fail)
    die(_("process is missing required capability %s"), fail, EC_MISC);

  /* Now set bitmaps to set caps after daemonising */
  memset(data, 0, sizeof(*data) * capsize);
  
  if (need_cap_net_admin)
    data->effective |= (1 << CAP_NET_ADMIN);
  if (need_cap_net_raw)
    data->effective |= (1 << CAP_NET_RAW);
  if (need_cap_net_bind_service)
    data->effective |= (1 << CAP_NET_BIND_SERVICE);
  
  data->permitted = data->effective;  
#endif

  /* Use a pipe to carry signals and other events back to the event loop 
     in a race-free manner and another to carry errors to daemon-invoking process */
  safe_pipe(pipefd, 1);
  
  piperead = pipefd[0];
  pipewrite = pipefd[1];
  /* prime the pipe to load stuff first time. */
  send_event(pipewrite, EVENT_INIT, 0, NULL); 

  err_pipe[1] = -1;
  
  if (!option_bool(OPT_DEBUG))   
    {
      /* The following code "daemonizes" the process. 
	 See Stevens section 12.4 */
      
      if (chdir("/") != 0)
	die(_("cannot chdir to filesystem root: %s"), NULL, EC_MISC); 

      if (!option_bool(OPT_NO_FORK))
	{
	  pid_t pid;
	  
	  /* pipe to carry errors back to original process.
	     When startup is complete we close this and the process terminates. */
	  safe_pipe(err_pipe, 0);
	  
	  if ((pid = fork()) == -1)
	    /* fd == -1 since we've not forked, never returns. */
	    send_event(-1, EVENT_FORK_ERR, errno, NULL);
	   
	  if (pid != 0)
	    {
	      struct event_desc ev;
	      char *msg;

	      /* close our copy of write-end */
	      close(err_pipe[1]);
	      
	      /* check for errors after the fork */
	      if (read_event(err_pipe[0], &ev, &msg))
		fatal_event(&ev, msg);
	      
	      _exit(EC_GOOD);
	    } 
	  
	  close(err_pipe[0]);

	  /* NO calls to die() from here on. */
	  
	  setsid();
	 
	  if ((pid = fork()) == -1)
	    send_event(err_pipe[1], EVENT_FORK_ERR, errno, NULL);
	 
	  if (pid != 0)
	    _exit(0);
	}
            
      /* write pidfile _after_ forking ! */
      if (daemon->runfile)
	{
	  int fd, err = 0;

	  sprintf(daemon->namebuff, "%d\n", (int) getpid());

	  /* Explanation: Some installations of dnsmasq (eg Debian/Ubuntu) locate the pid-file
	     in a directory which is writable by the non-privileged user that dnsmasq runs as. This
	     allows the daemon to delete the file as part of its shutdown. This is a security hole to the 
	     extent that an attacker running as the unprivileged  user could replace the pidfile with a 
	     symlink, and have the target of that symlink overwritten as root next time dnsmasq starts. 

	     The following code first deletes any existing file, and then opens it with the O_EXCL flag,
	     ensuring that the open() fails should there be any existing file (because the unlink() failed, 
	     or an attacker exploited the race between unlink() and open()). This ensures that no symlink
	     attack can succeed. 

	     Any compromise of the non-privileged user still theoretically allows the pid-file to be
	     replaced whilst dnsmasq is running. The worst that could allow is that the usual 
	     "shutdown dnsmasq" shell command could be tricked into stopping any other process.

	     Note that if dnsmasq is started as non-root (eg for testing) it silently ignores 
	     failure to write the pid-file.
	  */

	  unlink(daemon->runfile); 
	  
	  if ((fd = open(daemon->runfile, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH)) == -1)
	    {
	      /* only complain if started as root */
	      if (getuid() == 0)
		err = 1;
	    }
	  else
	    {
	      /* We're still running as root here. Change the ownership of the PID file
		 to the user we will be running as. Note that this is not to allow
		 us to delete the file, since that depends on the permissions 
		 of the directory containing the file. That directory will
		 need to by owned by the dnsmasq user, and the ownership of the
		 file has to match, to keep systemd >273 happy. */
	      if (getuid() == 0 && ent_pw && ent_pw->pw_uid != 0 && fchown(fd, ent_pw->pw_uid, ent_pw->pw_gid) == -1)
		chown_warn = errno;

	      if (!read_write(fd, (unsigned char *)daemon->namebuff, strlen(daemon->namebuff), RW_WRITE))
		err = 1;
	      else
		{
		  if (close(fd) == -1)
		    err = 1;
		}
	    }

	  if (err)
	    {
	      send_event(err_pipe[1], EVENT_PIDFILE, errno, daemon->runfile);
	      _exit(0);
	    }
	}
    }
  
   log_err = log_start(ent_pw, err_pipe[1]);

   if (!option_bool(OPT_DEBUG)) 
     {       
       /* open  stdout etc to /dev/null */
       int nullfd = open("/dev/null", O_RDWR);
       if (nullfd != -1)
	 {
	   dup2(nullfd, STDOUT_FILENO);
	   dup2(nullfd, STDERR_FILENO);
	   dup2(nullfd, STDIN_FILENO);
	   close(nullfd);
	 }
     }
   
   /* if we are to run scripts, we need to fork a helper before dropping root. */
  daemon->helperfd = -1;
#ifdef HAVE_SCRIPT 
  if ((daemon->dhcp ||
       daemon->dhcp6 ||
       daemon->relay6 ||
       option_bool(OPT_TFTP) ||
       option_bool(OPT_SCRIPT_ARP)) && 
      (daemon->lease_change_command || daemon->luascript))
      daemon->helperfd = create_helper(pipewrite, err_pipe[1], script_uid, script_gid, max_fd);
#endif

  if (!option_bool(OPT_DEBUG) && getuid() == 0)   
    {
      int bad_capabilities = 0;
      gid_t dummy;
      
      /* remove all supplementary groups */
      if (gp && 
	  (setgroups(0, &dummy) == -1 ||
	   setgid(gp->gr_gid) == -1))
	{
	  send_event(err_pipe[1], EVENT_GROUP_ERR, errno, daemon->groupname);
	  _exit(0);
	}
  
      if (ent_pw && ent_pw->pw_uid != 0)
	{     
#if defined(HAVE_LINUX_NETWORK)	  
	  /* Need to be able to drop root. */
	  data->effective |= (1 << CAP_SETUID);
	  data->permitted |= (1 << CAP_SETUID);
	  /* Tell kernel to not clear capabilities when dropping root */
	  if (capset(hdr, data) == -1 || prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1)
	    bad_capabilities = errno;
			  
#elif defined(HAVE_SOLARIS_NETWORK)
	  /* http://developers.sun.com/solaris/articles/program_privileges.html */
	  priv_set_t *priv_set;
	  
	  if (!(priv_set = priv_str_to_set("basic", ",", NULL)) ||
	      priv_addset(priv_set, PRIV_NET_ICMPACCESS) == -1 ||
	      priv_addset(priv_set, PRIV_SYS_NET_CONFIG) == -1)
	    bad_capabilities = errno;

	  if (priv_set && bad_capabilities == 0)
	    {
	      priv_inverse(priv_set);
	  
	      if (setppriv(PRIV_OFF, PRIV_LIMIT, priv_set) == -1)
		bad_capabilities = errno;
	    }

	  if (priv_set)
	    priv_freeset(priv_set);

#endif    

	  if (bad_capabilities != 0)
	    {
	      send_event(err_pipe[1], EVENT_CAP_ERR, bad_capabilities, NULL);
	      _exit(0);
	    }
	  
	  /* finally drop root */
	  if (setuid(ent_pw->pw_uid) == -1)
	    {
	      send_event(err_pipe[1], EVENT_USER_ERR, errno, daemon->username);
	      _exit(0);
	    }     

#ifdef HAVE_LINUX_NETWORK
	  data->effective &= ~(1 << CAP_SETUID);
	  data->permitted &= ~(1 << CAP_SETUID);
	  
	  /* lose the setuid capability */
	  if (capset(hdr, data) == -1)
	    {
	      send_event(err_pipe[1], EVENT_CAP_ERR, errno, NULL);
	      _exit(0);
	    }
#endif
	  
	}
    }
  
#ifdef HAVE_LINUX_NETWORK
  free(hdr);
  free(data);
  if (option_bool(OPT_DEBUG)) 
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif

#ifdef HAVE_TFTP
  if (option_bool(OPT_TFTP))
    {
      DIR *dir;
      struct tftp_prefix *p;
      
      if (daemon->tftp_prefix)
	{
	  if (!((dir = opendir(daemon->tftp_prefix))))
	    {
	      tftp_prefix_missing = 1;
	      if (!option_bool(OPT_TFTP_NO_FAIL))
	        {
	          send_event(err_pipe[1], EVENT_TFTP_ERR, errno, daemon->tftp_prefix);
	          _exit(0);
	        }
	    }
	  else
	    closedir(dir);
	}

      for (p = daemon->if_prefix; p; p = p->next)
	{
	  p->missing = 0;
	  if (!((dir = opendir(p->prefix))))
	    {
	      p->missing = 1;
	      if (!option_bool(OPT_TFTP_NO_FAIL))
		{
		  send_event(err_pipe[1], EVENT_TFTP_ERR, errno, p->prefix);
		  _exit(0);
		}
	    }
	  else
	    closedir(dir);
	}
    }
#endif

  if (daemon->port == 0)
    my_syslog(LOG_INFO, _("started, version %s DNS disabled"), VERSION);
  else 
    {
      if (daemon->cachesize != 0)
	{
	  my_syslog(LOG_INFO, _("started, version %s cachesize %d"), VERSION, daemon->cachesize);
	  if (daemon->cachesize > 10000)
	    my_syslog(LOG_WARNING, _("cache size greater than 10000 may cause performance issues, and is unlikely to be useful."));
	}
      else
	my_syslog(LOG_INFO, _("started, version %s cache disabled"), VERSION);

      if (option_bool(OPT_LOCAL_SERVICE))
	my_syslog(LOG_INFO, _("DNS service limited to local subnets"));
      else if (option_bool(OPT_LOCALHOST_SERVICE))
	my_syslog(LOG_INFO, _("DNS service limited to localhost"));
    }
  
  my_syslog(LOG_INFO, _("compile time options: %s"), compile_opts);

  if (chown_warn != 0)
    {
#if defined(HAVE_LINUX_NETWORK)
      if (chown_warn == EPERM && !have_cap_chown)
        my_syslog(LOG_INFO, "chown of PID file %s failed: please add capability CAP_CHOWN", daemon->runfile);
      else
#endif
      my_syslog(LOG_WARNING, "chown of PID file %s failed: %s", daemon->runfile, strerror(chown_warn));
    }
  
#ifdef HAVE_DBUS
  if (option_bool(OPT_DBUS))
    {
      if (daemon->dbus)
	my_syslog(LOG_INFO, _("DBus support enabled: connected to system bus"));
      else
	my_syslog(LOG_INFO, _("DBus support enabled: bus connection pending"));
    }
#endif

#ifdef HAVE_UBUS
  if (option_bool(OPT_UBUS))
    {
      if (daemon->ubus)
        my_syslog(LOG_INFO, _("UBus support enabled: connected to system bus"));
      else
        my_syslog(LOG_INFO, _("UBus support enabled: bus connection pending"));
    }
#endif

#ifdef HAVE_DNSSEC
  if (option_bool(OPT_DNSSEC_VALID))
    {
      int rc;
      struct ds_config *ds;
      
      /* Delay creating the timestamp file until here, after we've changed user, so that
	 it has the correct owner to allow updating the mtime later. 
	 This means we have to report fatal errors via the pipe. */
      if ((rc = setup_timestamp()) == -1)
	{
	  send_event(err_pipe[1], EVENT_TIME_ERR, errno, daemon->timestamp_file);
	  _exit(0);
	}
      
      if (option_bool(OPT_DNSSEC_IGN_NS))
	my_syslog(LOG_INFO, _("DNSSEC validation enabled but all unsigned answers are trusted"));
      else
	my_syslog(LOG_INFO, _("DNSSEC validation enabled"));
      
      daemon->dnssec_no_time_check = option_bool(OPT_DNSSEC_TIME);
      if (option_bool(OPT_DNSSEC_TIME) && !daemon->back_to_the_future)
	my_syslog(LOG_INFO, _("DNSSEC signature timestamps not checked until receipt of SIGINT"));
      
      if (rc == 1)
	my_syslog(LOG_INFO, _("DNSSEC signature timestamps not checked until system time valid"));

      for (ds = daemon->ds; ds; ds = ds->next)
	my_syslog(LOG_INFO,
		  ds->digestlen == 0 ? _("configured with negative trust anchor for %s") : _("configured with trust anchor for %s keytag %u"),
		  ds->name[0] == 0 ? "<root>" : ds->name, ds->keytag);
    }
#endif

  if (log_err != 0)
    my_syslog(LOG_WARNING, _("warning: failed to change owner of %s: %s"), 
	      daemon->log_file, strerror(log_err));
  
#ifndef HAVE_LINUX_NETWORK
  if (bind_fallback)
    my_syslog(LOG_WARNING, _("setting --bind-interfaces option because of OS limitations"));
#endif

  if (option_bool(OPT_NOWILD))
    warn_bound_listeners();
  else if (!option_bool(OPT_CLEVERBIND))
    warn_wild_labels();

  warn_int_names();
  
  if (!option_bool(OPT_NOWILD)) 
    for (if_tmp = daemon->if_names; if_tmp; if_tmp = if_tmp->next)
      if (if_tmp->name && !(if_tmp->flags & INAME_USED))
	my_syslog(LOG_WARNING, _("warning: interface %s does not currently exist"), if_tmp->name);
   
  if (daemon->port != 0 && option_bool(OPT_NO_RESOLV))
    {
      if (daemon->resolv_files && !daemon->resolv_files->is_default)
	my_syslog(LOG_WARNING, _("warning: ignoring resolv-file flag because no-resolv is set"));
      daemon->resolv_files = NULL;
      if (!daemon->servers)
	{
#ifdef HAVE_DBUS
	  if (option_bool(OPT_DBUS))
	    my_syslog(LOG_INFO, _("no upstream servers configured - please set them from DBus"));
	  else
#endif
	  my_syslog(LOG_WARNING, _("warning: no upstream servers configured"));
	}
    } 

  if (daemon->max_logs != 0)
    my_syslog(LOG_INFO, _("asynchronous logging enabled, queue limit is %d messages"), daemon->max_logs);
  

#ifdef HAVE_DHCP
  for (context = daemon->dhcp; context; context = context->next)
    log_context(AF_INET, context);

  for (relay = daemon->relay4; relay; relay = relay->next)
    log_relay(AF_INET, relay);

#  ifdef HAVE_DHCP6
  for (context = daemon->dhcp6; context; context = context->next)
    log_context(AF_INET6, context);

  for (relay = daemon->relay6; relay; relay = relay->next)
    log_relay(AF_INET6, relay);
  
  if (daemon->doing_dhcp6 || daemon->doing_ra)
    dhcp_construct_contexts(now);
  
  if (option_bool(OPT_RA))
    my_syslog(MS_DHCP | LOG_INFO, _("IPv6 router advertisement enabled"));
#  endif

#  ifdef HAVE_LINUX_NETWORK
  if (did_bind)
    my_syslog(MS_DHCP | LOG_INFO, _("DHCP, sockets bound exclusively to interface %s"), bound_device);

  if (netlink_warn)
    my_syslog(LOG_WARNING, netlink_warn);
#  endif

  /* after dhcp_construct_contexts */
  if (daemon->dhcp || daemon->doing_dhcp6)
    lease_find_interfaces(now);
#endif

#ifdef HAVE_TFTP
  if (option_bool(OPT_TFTP))
    {
      struct tftp_prefix *p;

      my_syslog(MS_TFTP | LOG_INFO, "TFTP %s%s %s %s", 
		daemon->tftp_prefix ? _("root is ") : _("enabled"),
		daemon->tftp_prefix ? daemon->tftp_prefix : "",
		option_bool(OPT_TFTP_SECURE) ? _("secure mode") : "",
		option_bool(OPT_SINGLE_PORT) ? _("single port mode") : "");

      if (tftp_prefix_missing)
	my_syslog(MS_TFTP | LOG_WARNING, _("warning: %s inaccessible"), daemon->tftp_prefix);

      for (p = daemon->if_prefix; p; p = p->next)
	if (p->missing)
	   my_syslog(MS_TFTP | LOG_WARNING, _("warning: TFTP directory %s inaccessible"), p->prefix);

      /* This is a guess, it assumes that for small limits, 
	 disjoint files might be served, but for large limits, 
	 a single file will be sent to may clients (the file only needs
	 one fd). */

      max_fd -= 30 + daemon->numrrand; /* use other than TFTP */
      
      if (max_fd < 0)
	max_fd = 5;
      else if (max_fd < 100 && !option_bool(OPT_SINGLE_PORT))
	max_fd = max_fd/2;
      else
	max_fd = max_fd - 20;
      
      /* if we have to use a limited range of ports, 
	 that will limit the number of transfers */
      if (daemon->start_tftp_port != 0 &&
	  daemon->end_tftp_port - daemon->start_tftp_port + 1 < max_fd)
	max_fd = daemon->end_tftp_port - daemon->start_tftp_port + 1;

      if (daemon->tftp_max > max_fd)
	{
	  daemon->tftp_max = max_fd;
	  my_syslog(MS_TFTP | LOG_WARNING, 
		    _("restricting maximum simultaneous TFTP transfers to %d"), 
		    daemon->tftp_max);
	}
    }
#endif

  /* finished start-up - release original process */
  if (err_pipe[1] != -1)
    close(err_pipe[1]);
  
  if (daemon->port != 0)
    check_servers(0);
  
  pid = getpid();

  daemon->pipe_to_parent = -1;

#ifdef HAVE_INOTIFY
  /* Using inotify, have to select a resolv file at startup */
  poll_resolv(1, 0, now);
#endif
  
  while (1)
    {
      int timeout = fast_retry(now);
      
      poll_reset();
      
      /* Whilst polling for the dbus, or doing a tftp transfer, wake every quarter second */
      if ((daemon->tftp_trans || (option_bool(OPT_DBUS) && !daemon->dbus)) &&
	  (timeout == -1 || timeout > 250))
	timeout = 250;
      
      /* Wake every second whilst waiting for DAD to complete */
      else if (is_dad_listeners() &&
	       (timeout == -1 || timeout > 1000))
	timeout = 1000;
      
      if (daemon->port != 0)
	set_dns_listeners();
      
#ifdef HAVE_TFTP
      set_tftp_listeners();
#endif

#ifdef HAVE_DBUS
      if (option_bool(OPT_DBUS))
	set_dbus_listeners();
#endif
      
#ifdef HAVE_UBUS
      if (option_bool(OPT_UBUS))
        set_ubus_listeners();
#endif
      
#ifdef HAVE_DHCP
#  if defined(HAVE_LINUX_NETWORK)
      if (bind_dhcp_devices(bound_device) & 2)
	{
	  static int warned = 0;
	  if (!warned)
	    {
	      my_syslog(LOG_ERR, _("error binding DHCP socket to device %s"), bound_device);
	      warned = 1;
	    }
	}
# endif
      if (daemon->dhcp || daemon->relay4)
	{
	  poll_listen(daemon->dhcpfd, POLLIN);
	  if (daemon->pxefd != -1)
	    poll_listen(daemon->pxefd, POLLIN);
	}
#endif

#ifdef HAVE_DHCP6
      if (daemon->doing_dhcp6 || daemon->relay6)
	poll_listen(daemon->dhcp6fd, POLLIN);
	
      if (daemon->doing_ra)
	poll_listen(daemon->icmp6fd, POLLIN); 
#endif
    
#ifdef HAVE_INOTIFY
      if (daemon->inotifyfd != -1)
	poll_listen(daemon->inotifyfd, POLLIN);
#endif

#if defined(HAVE_LINUX_NETWORK)
      poll_listen(daemon->netlinkfd, POLLIN);
#elif defined(HAVE_BSD_NETWORK)
      poll_listen(daemon->routefd, POLLIN);
#endif
      
      poll_listen(piperead, POLLIN);

#ifdef HAVE_SCRIPT
#    ifdef HAVE_DHCP
      while (helper_buf_empty() && do_script_run(now)); 
#    endif

      /* Refresh cache */
      if (option_bool(OPT_SCRIPT_ARP))
	find_mac(NULL, NULL, 0, now);
      while (helper_buf_empty() && do_arp_script_run());

#    ifdef HAVE_TFTP
      while (helper_buf_empty() && do_tftp_script_run());
#    endif

#    ifdef HAVE_DHCP6
      while (helper_buf_empty() && do_snoop_script_run());
#    endif
      
      if (!helper_buf_empty())
	poll_listen(daemon->helperfd, POLLOUT);
#else
      /* need this for other side-effects */
#    ifdef HAVE_DHCP
      while (do_script_run(now));
#    endif

      while (do_arp_script_run());

#    ifdef HAVE_TFTP 
      while (do_tftp_script_run());
#    endif

#endif

   
      /* must do this just before do_poll(), when we know no
	 more calls to my_syslog() can occur */
      set_log_writer();
      
      if (do_poll(timeout) < 0)
	continue;
      
      now = dnsmasq_time();

      check_log_writer(0);

      /* prime. */
      enumerate_interfaces(1);

      /* Check the interfaces to see if any have exited DAD state
	 and if so, bind the address. */
      if (is_dad_listeners())
	{
	  enumerate_interfaces(0);
	  /* NB, is_dad_listeners() == 1 --> we're binding interfaces */
	  create_bound_listeners(0);
	  warn_bound_listeners();
	}

#if defined(HAVE_LINUX_NETWORK)
      if (poll_check(daemon->netlinkfd, POLLIN))
	netlink_multicast();
#elif defined(HAVE_BSD_NETWORK)
      if (poll_check(daemon->routefd, POLLIN))
	route_sock();
#endif

#ifdef HAVE_INOTIFY
      if  (daemon->inotifyfd != -1 && poll_check(daemon->inotifyfd, POLLIN) && inotify_check(now))
	{
	  if (daemon->port != 0 && !option_bool(OPT_NO_POLL))
	    poll_resolv(1, 1, now);
	} 	  
#else
      /* Check for changes to resolv files once per second max. */
      /* Don't go silent for long periods if the clock goes backwards. */
      if (daemon->last_resolv == 0 || 
	  difftime(now, daemon->last_resolv) > 1.0 || 
	  difftime(now, daemon->last_resolv) < -1.0)
	{
	  /* poll_resolv doesn't need to reload first time through, since 
	     that's queued anyway. */

	  poll_resolv(0, daemon->last_resolv != 0, now); 	  
	  daemon->last_resolv = now;
	}
#endif

      if (poll_check(piperead, POLLIN))
	async_event(piperead, now);
      
#ifdef HAVE_DBUS
      /* if we didn't create a DBus connection, retry now. */ 
      if (option_bool(OPT_DBUS))
	{
	  if (!daemon->dbus)
	    {
	      char *err  = dbus_init();

	      if (daemon->dbus)
		my_syslog(LOG_INFO, _("connected to system DBus"));
	      else if (err)
		{
		  my_syslog(LOG_ERR, _("DBus error: %s"), err);
		  reset_option_bool(OPT_DBUS); /* fatal error, stop trying. */
		}
	    }
	  
	  check_dbus_listeners();
	}
#endif

#ifdef HAVE_UBUS
      /* if we didn't create a UBus connection, retry now. */
      if (option_bool(OPT_UBUS))
	{
	  if (!daemon->ubus)
	    {
	      char *err = ubus_init();

	      if (daemon->ubus)
		my_syslog(LOG_INFO, _("connected to system UBus"));
	      else if (err)
		{
		  my_syslog(LOG_ERR, _("UBus error: %s"), err);
		  reset_option_bool(OPT_UBUS); /* fatal error, stop trying. */
		}
	    }
	  
	  check_ubus_listeners();
	}
#endif
      
      if (daemon->port != 0)
	check_dns_listeners(now);

#ifdef HAVE_TFTP
      check_tftp_listeners(now);
#endif      

#ifdef HAVE_DHCP
      if (daemon->dhcp || daemon->relay4)
	{
	  if (poll_check(daemon->dhcpfd, POLLIN))
	    dhcp_packet(now, 0);
	  if (daemon->pxefd != -1 && poll_check(daemon->pxefd, POLLIN))
	    dhcp_packet(now, 1);
	}

#ifdef HAVE_DHCP6
      if ((daemon->doing_dhcp6 || daemon->relay6) && poll_check(daemon->dhcp6fd, POLLIN))
	dhcp6_packet(now);

      if (daemon->doing_ra && poll_check(daemon->icmp6fd, POLLIN))
	icmp6_packet(now);
#endif

#  ifdef HAVE_SCRIPT
      if (daemon->helperfd != -1 && poll_check(daemon->helperfd, POLLOUT))
	helper_write();
#  endif
#endif

    }
}

static void sig_handler(int sig)
{
  if (pid == 0)
    {
      /* ignore anything other than TERM during startup
	 and in helper proc. (helper ignore TERM too) */
      if (sig == SIGTERM || sig == SIGINT)
	exit(EC_MISC);
    }
  else if (pid != getpid())
    {
      /* alarm is used to kill TCP children after a fixed time. */
      if (sig == SIGALRM)
	_exit(0);
    }
  else
    {
      /* master process */
      int event, errsave = errno;
      
      if (sig == SIGHUP)
	event = EVENT_RELOAD;
      else if (sig == SIGCHLD)
	event = EVENT_CHILD;
      else if (sig == SIGALRM)
	event = EVENT_ALARM;
      else if (sig == SIGTERM)
	event = EVENT_TERM;
      else if (sig == SIGUSR1)
	event = EVENT_DUMP;
      else if (sig == SIGUSR2)
	event = EVENT_REOPEN;
      else if (sig == SIGINT)
	{
	  /* Handle SIGINT normally in debug mode, so
	     ctrl-c continues to operate. */
	  if (option_bool(OPT_DEBUG))
	    exit(EC_MISC);
	  else
	    event = EVENT_TIME;
	}
      else
	return;

      send_event(pipewrite, event, 0, NULL); 
      errno = errsave;
    }
}

/* now == 0 -> queue immediate callback */
void send_alarm(time_t event, time_t now)
{
  if (now == 0 || event != 0)
    {
      /* alarm(0) or alarm(-ve) doesn't do what we want.... */
      if ((now == 0 || difftime(event, now) <= 0.0))
	send_event(pipewrite, EVENT_ALARM, 0, NULL);
      else 
	alarm((unsigned)difftime(event, now)); 
    }
}

void queue_event(int event)
{
  send_event(pipewrite, event, 0, NULL);
}

void send_event(int fd, int event, int data, char *msg)
{
  struct event_desc ev;
  struct iovec iov[2];

  ev.event = event;
  ev.data = data;
  ev.msg_sz = msg ? strlen(msg) : 0;
  
  iov[0].iov_base = &ev;
  iov[0].iov_len = sizeof(ev);
  iov[1].iov_base = msg;
  iov[1].iov_len = ev.msg_sz;
  
  /* error pipe, debug mode. */
  if (fd == -1)
    fatal_event(&ev, msg);
  else
    /* pipe is non-blocking and struct event_desc is smaller than
       PIPE_BUF, so this either fails or writes everything */
    while (writev(fd, iov, msg ? 2 : 1) == -1 && errno == EINTR);
}

/* NOTE: the memory used to return msg is leaked: use msgs in events only
   to describe fatal errors. */
static int read_event(int fd, struct event_desc *evp, char **msg)
{
  char *buf;

  if (!read_write(fd, (unsigned char *)evp, sizeof(struct event_desc), RW_READ))
    return 0;
  
  *msg = NULL;
  
  if (evp->msg_sz != 0 && 
      (buf = malloc(evp->msg_sz + 1)) &&
      read_write(fd, (unsigned char *)buf, evp->msg_sz, RW_READ))
    {
      buf[evp->msg_sz] = 0;
      *msg = buf;
    }

  return 1;
}
    
static void fatal_event(struct event_desc *ev, char *msg)
{
  errno = ev->data;
  
  switch (ev->event)
    {
    case EVENT_DIE:
      exit(0);

    case EVENT_FORK_ERR:
      die(_("cannot fork into background: %s"), NULL, EC_MISC);

      /* fall through */
    case EVENT_PIPE_ERR:
      die(_("failed to create helper: %s"), NULL, EC_MISC);

      /* fall through */
    case EVENT_CAP_ERR:
      die(_("setting capabilities failed: %s"), NULL, EC_MISC);

      /* fall through */
    case EVENT_USER_ERR:
      die(_("failed to change user-id to %s: %s"), msg, EC_MISC);

      /* fall through */
    case EVENT_GROUP_ERR:
      die(_("failed to change group-id to %s: %s"), msg, EC_MISC);

      /* fall through */
    case EVENT_PIDFILE:
      die(_("failed to open pidfile %s: %s"), msg, EC_FILE);

      /* fall through */
    case EVENT_LOG_ERR:
      die(_("cannot open log %s: %s"), msg, EC_FILE);

      /* fall through */
    case EVENT_LUA_ERR:
      die(_("failed to load Lua script: %s"), msg, EC_MISC);

      /* fall through */
    case EVENT_TFTP_ERR:
      die(_("TFTP directory %s inaccessible: %s"), msg, EC_FILE);

      /* fall through */
    case EVENT_TIME_ERR:
      die(_("cannot create timestamp file %s: %s" ), msg, EC_BADCONF);
    }
}	
      
static void async_event(int pipe, time_t now)
{
  pid_t p;
  struct event_desc ev;
  int wstatus, i, check = 0;
  char *msg;
  
  /* NOTE: the memory used to return msg is leaked: use msgs in events only
     to describe fatal errors. */
  
  if (read_event(pipe, &ev, &msg))
    switch (ev.event)
      {
      case EVENT_RELOAD:
	daemon->soa_sn++; /* Bump zone serial, as it may have changed. */
	
	/* fall through */
	
      case EVENT_INIT:
	clear_cache_and_reload(now);
	
	if (daemon->port != 0)
	  {
	    if (daemon->resolv_files && option_bool(OPT_NO_POLL))
	      {
		reload_servers(daemon->resolv_files->name);
		check = 1;
	      }

	    if (daemon->servers_file)
	      {
		read_servers_file();
		check = 1;
	      }

	    if (check)
	      check_servers(0);
	  }

#ifdef HAVE_DHCP
	rerun_scripts();
#endif
	break;
	
      case EVENT_DUMP:
	if (daemon->port != 0)
	  dump_cache(now);
	break;
	
      case EVENT_ALARM:
#ifdef HAVE_DHCP
	if (daemon->dhcp || daemon->doing_dhcp6)
	  {
	    lease_prune(NULL, now);
	    lease_update_file(now);
	    lease_update_dns(0);
	  }
#ifdef HAVE_DHCP6
	else if (daemon->doing_ra)
	  /* Not doing DHCP, so no lease system, manage alarms for ra only */
	    send_alarm(periodic_ra(now), now);
#endif
#endif
	break;
		
      case EVENT_CHILD:
	/* See Stevens 5.10 */
	while ((p = waitpid(-1, &wstatus, WNOHANG)) != 0)
	  if (p == -1)
	    {
	      if (errno != EINTR)
		break;
	    }      
	  else if (daemon->port != 0)
	    for (i = 0 ; i < daemon->max_procs; i++)
	      if (daemon->tcp_pids[i] == p)
		{
		  daemon->tcp_pids[i] = 0;

		  if (!WIFEXITED(wstatus))
		    {
		      /* If a helper process dies, (eg with SIGSEV)
			 log that and attempt to patch things up so that the 
			 parent can continue to function. */
		      my_syslog(LOG_WARNING, _("TCP helper process %u died unexpectedly"), (unsigned int)p);
		      if (daemon->tcp_pipes[i] != -1)
			{
			  close(daemon->tcp_pipes[i]);
			  daemon->tcp_pipes[i] = -1;
			}
		    }
		  
		  /* tcp_pipes == -1 && tcp_pids == 0 required to free slot */
		  if (daemon->tcp_pipes[i] == -1)
		    daemon->metrics[METRIC_TCP_CONNECTIONS]--;
		}
	break;
	
#if defined(HAVE_SCRIPT)	
      case EVENT_KILLED:
	my_syslog(LOG_WARNING, _("script process killed by signal %d"), ev.data);
	break;

      case EVENT_EXITED:
	my_syslog(LOG_WARNING, _("script process exited with status %d"), ev.data);
	break;

      case EVENT_EXEC_ERR:
	my_syslog(LOG_ERR, _("failed to execute %s: %s"), 
		  daemon->lease_change_command, strerror(ev.data));
	break;

      case EVENT_SCRIPT_LOG:
	my_syslog(MS_SCRIPT | LOG_DEBUG, "%s", msg ? msg : "");
        free(msg);
	msg = NULL;
	break;

	/* necessary for fatal errors in helper */
      case EVENT_USER_ERR:
      case EVENT_DIE:
      case EVENT_LUA_ERR:
	fatal_event(&ev, msg);
	break;
#endif

      case EVENT_REOPEN:
	/* Note: this may leave TCP-handling processes with the old file still open.
	   Since any such process will die in CHILD_LIFETIME or probably much sooner,
	   we leave them logging to the old file. */
	if (daemon->log_file != NULL)
	  log_reopen(daemon->log_file);
	break;

      case EVENT_NEWADDR:
	newaddress(now);
	break;

      case EVENT_NEWROUTE:
	resend_query();
	/* Force re-reading resolv file right now, for luck. */
	poll_resolv(0, 1, now);
	break;

      case EVENT_TIME:
#ifdef HAVE_DNSSEC
	if (daemon->dnssec_no_time_check && option_bool(OPT_DNSSEC_VALID) && option_bool(OPT_DNSSEC_TIME))
	  {
	    my_syslog(LOG_INFO, _("now checking DNSSEC signature timestamps"));
	    daemon->dnssec_no_time_check = 0;
	    clear_cache_and_reload(now);
	  }
#endif
	break;
	
      case EVENT_TERM:
	/* Knock all our children on the head. */
	if (daemon->port != 0)
	  for (i = 0; i < daemon->max_procs; i++)
	    if (daemon->tcp_pids[i] != 0)
	      kill(daemon->tcp_pids[i], SIGALRM);
	
#if defined(HAVE_SCRIPT) && defined(HAVE_DHCP)
	/* handle pending lease transitions */
	if (daemon->helperfd != -1)
	  {
	    /* block in writes until all done */
	    if ((i = fcntl(daemon->helperfd, F_GETFL)) != -1)
	      while(retry_send(fcntl(daemon->helperfd, F_SETFL, i & ~O_NONBLOCK)));
	    do {
	      helper_write();
	    } while (!helper_buf_empty() || do_script_run(now));
	    close(daemon->helperfd);
	  }
#endif
	
	if (daemon->lease_stream)
	  fclose(daemon->lease_stream);

#ifdef HAVE_DNSSEC
	/* update timestamp file on TERM if time is considered valid */
	if (daemon->back_to_the_future)
	  {
	     if (utimes(daemon->timestamp_file, NULL) == -1)
		my_syslog(LOG_ERR, _("failed to update mtime on %s: %s"), daemon->timestamp_file, strerror(errno));
	  }
#endif

	if (daemon->runfile)
	  unlink(daemon->runfile);

#ifdef HAVE_DUMPFILE
	if (daemon->dumpfd != -1)
	  close(daemon->dumpfd);
#endif
	
	my_syslog(LOG_INFO, _("exiting on receipt of SIGTERM"));
	flush_log();
	exit(EC_GOOD);
      }
}

static void poll_resolv(int force, int do_reload, time_t now)
{
  struct resolvc *res, *latest;
  struct stat statbuf;
  time_t last_change = 0;
  /* There may be more than one possible file. 
     Go through and find the one which changed _last_.
     Warn of any which can't be read. */

  if (daemon->port == 0 || option_bool(OPT_NO_POLL))
    return;
  
  for (latest = NULL, res = daemon->resolv_files; res; res = res->next)
    if (stat(res->name, &statbuf) == -1)
      {
	if (force)
	  {
	    res->mtime = 0; 
	    continue;
	  }

	if (!res->logged)
	  my_syslog(LOG_WARNING, _("failed to access %s: %s"), res->name, strerror(errno));
	res->logged = 1;
	
	if (res->mtime != 0)
	  { 
	    /* existing file evaporated, force selection of the latest
	       file even if its mtime hasn't changed since we last looked */
	    poll_resolv(1, do_reload, now);
	    return;
	  }
      }
    else
      {
	res->logged = 0;
	if (force || (statbuf.st_mtime != res->mtime || statbuf.st_ino != res->ino))
          {
            res->mtime = statbuf.st_mtime;
	    res->ino = statbuf.st_ino;
	    if (difftime(statbuf.st_mtime, last_change) > 0.0)
	      {
		last_change = statbuf.st_mtime;
		latest = res;
	      }
	  }
      }
  
  if (latest)
    {
      static int warned = 0;
      if (reload_servers(latest->name))
	{
	  my_syslog(LOG_INFO, _("reading %s"), latest->name);
	  warned = 0;
	  check_servers(0);
	  if (option_bool(OPT_RELOAD) && do_reload)
	    clear_cache_and_reload(now);
	}
      else 
	{
	  /* If we're delaying things, we don't call check_servers(), but 
	     reload_servers() may have deleted some servers, rendering the server_array
	     invalid, so just rebuild that here. Once reload_servers() succeeds,
	     we call check_servers() above, which calls build_server_array itself. */
	  build_server_array();
	  latest->mtime = 0;
	  if (!warned)
	    {
	      my_syslog(LOG_WARNING, _("no servers found in %s, will retry"), latest->name);
	      warned = 1;
	    }
	}
    }
}       

void clear_cache_and_reload(time_t now)
{
  (void)now;

  if (daemon->port != 0)
    cache_reload();
  
#ifdef HAVE_DHCP
  if (daemon->dhcp || daemon->doing_dhcp6)
    {
      reread_dhcp();
      if (option_bool(OPT_ETHERS))
	dhcp_read_ethers();
      dhcp_update_configs(daemon->dhcp_conf);
      lease_update_from_configs(); 
      lease_update_file(now); 
      lease_update_dns(1);
    }
#ifdef HAVE_DHCP6
  else if (daemon->doing_ra)
    /* Not doing DHCP, so no lease system, manage 
       alarms for ra only */
    send_alarm(periodic_ra(now), now);
#endif
#endif
}

#ifdef HAVE_TFTP
static void set_tftp_listeners(void)
{
  int  tftp = 0;
  struct tftp_transfer *transfer;
  struct listener *listener;
  
  if (!option_bool(OPT_SINGLE_PORT))
    for (transfer = daemon->tftp_trans; transfer; transfer = transfer->next)
      {
	tftp++;
	poll_listen(transfer->sockfd, POLLIN);
      }

  for (listener = daemon->listeners; listener; listener = listener->next)
    /* tftp == 0 in single-port mode. */
    if (tftp <= daemon->tftp_max && listener->tftpfd != -1)
      poll_listen(listener->tftpfd, POLLIN);
}
#endif

static void set_dns_listeners(void)
{
  struct serverfd *serverfdp;
  struct listener *listener;
  struct randfd_list *rfl;
  int i;
  
  for (serverfdp = daemon->sfds; serverfdp; serverfdp = serverfdp->next)
    poll_listen(serverfdp->fd, POLLIN);
    
  for (i = 0; i < daemon->numrrand; i++)
    if (daemon->randomsocks[i].refcount != 0)
      poll_listen(daemon->randomsocks[i].fd, POLLIN);

  /* Check overflow random sockets too. */
  for (rfl = daemon->rfl_poll; rfl; rfl = rfl->next)
    poll_listen(rfl->rfd->fd, POLLIN);
  
  /* check to see if we have free tcp process slots. */
  for (i = daemon->max_procs - 1; i >= 0; i--)
    if (daemon->tcp_pids[i] == 0 && daemon->tcp_pipes[i] == -1)
      break;

  for (listener = daemon->listeners; listener; listener = listener->next)
    {
      if (listener->fd != -1)
	poll_listen(listener->fd, POLLIN);
      
      /* Only listen for TCP connections when a process slot
	 is available. Death of a child goes through the select loop, so
	 we don't need to explicitly arrange to wake up here,
	 we'll be called again when a slot becomes available. */
      if  (listener->tcpfd != -1 && i >= 0)
	poll_listen(listener->tcpfd, POLLIN);
    }
  
  if (!option_bool(OPT_DEBUG))
    for (i = 0; i < daemon->max_procs; i++)
      if (daemon->tcp_pipes[i] != -1)
	poll_listen(daemon->tcp_pipes[i], POLLIN);
}

static void check_dns_listeners(time_t now)
{
  struct serverfd *serverfdp;
  struct listener *listener;
  struct randfd_list *rfl;
  int i;
  
  /* Note that handling events here can create or destroy fds and
     render the result of the last poll() call invalid. Once
     we find an fd that needs service, do it, then return to go around the
     poll() loop again. This avoid really, really, wierd bugs. */

  if (!option_bool(OPT_DEBUG))
    for (i = 0; i < daemon->max_procs; i++)
      if (daemon->tcp_pipes[i] != -1 &&
	  poll_check(daemon->tcp_pipes[i], POLLIN | POLLHUP))
	{
	   /* Races. The child process can die before we read all of the data from the
	      pipe, or vice versa. Therefore send tcp_pids to zero when we wait() the 
	      process, and tcp_pipes to -1 and close the FD when we read the last
	      of the data - indicated by cache_recv_insert returning zero.
	      The order of these events is indeterminate, and both are needed
	      to free the process slot. Once the child process has gone, poll()
	      returns POLLHUP, not POLLIN, so have to check for both here. */
	  if (!cache_recv_insert(now, daemon->tcp_pipes[i]))
	    {
	      close(daemon->tcp_pipes[i]);
	      daemon->tcp_pipes[i] = -1;	
	      /* tcp_pipes == -1 && tcp_pids == 0 required to free slot */
	      if (daemon->tcp_pids[i] == 0)
		daemon->metrics[METRIC_TCP_CONNECTIONS]--;
	    }
	  return;
	}

  for (serverfdp = daemon->sfds; serverfdp; serverfdp = serverfdp->next)
    if (poll_check(serverfdp->fd, POLLIN))
      {
	reply_query(serverfdp->fd, now);
	return;
      }
  
  for (i = 0; i < daemon->numrrand; i++)
    if (daemon->randomsocks[i].refcount != 0 && 
	poll_check(daemon->randomsocks[i].fd, POLLIN))
      {
	reply_query(daemon->randomsocks[i].fd, now);
	return;
      }
  
  /* Check overflow random sockets too. */
  for (rfl = daemon->rfl_poll; rfl; rfl = rfl->next)
    if (poll_check(rfl->rfd->fd, POLLIN))
      {
	reply_query(rfl->rfd->fd, now);
	return;
      }
  
  for (listener = daemon->listeners; listener; listener = listener->next)
    if (listener->fd != -1 && poll_check(listener->fd, POLLIN))
      {
	receive_query(listener, now); 
	return;
      }
  
  /* check to see if we have a free tcp process slot.
     Note that we can't assume that because we had
     at least one a poll() time, that we still do.
     There may be more waiting connections after
     poll() returns then free process slots. */
  for (i = daemon->max_procs - 1; i >= 0; i--)
    if (daemon->tcp_pids[i] == 0 && daemon->tcp_pipes[i] == -1)
      break;

  if (i >= 0)
    for (listener = daemon->listeners; listener; listener = listener->next)
      if (listener->tcpfd != -1 && poll_check(listener->tcpfd, POLLIN))
	{
	  do_tcp_connection(listener, now, i);
	  return;
	}
}

static void do_tcp_connection(struct listener *listener, time_t now, int slot)
{
  int confd, client_ok = 1;
  struct irec *iface = NULL;
  pid_t p;
  union mysockaddr tcp_addr;
  socklen_t tcp_len = sizeof(union mysockaddr);
  unsigned char *buff;
  struct server *s; 
  int flags, auth_dns;
  struct in_addr netmask;
  int pipefd[2];
#ifdef HAVE_LINUX_NETWORK
  unsigned char a = 0;
#endif

  while ((confd = accept(listener->tcpfd, NULL, NULL)) == -1 && errno == EINTR);
  
  if (confd == -1)
    return;
  
  if (getsockname(confd, (struct sockaddr *)&tcp_addr, &tcp_len) == -1)
    {
    closeconandreturn:
      shutdown(confd, SHUT_RDWR);
      close(confd);
      return;
    }
  
  /* Make sure that the interface list is up-to-date.
     
     We do this here as we may need the results below, and
     the DNS code needs them for --interface-name stuff.
     
     Multiple calls to enumerate_interfaces() per select loop are
     inhibited, so calls to it in the child process (which doesn't select())
     have no effect. This avoids two processes reading from the same
     netlink fd and screwing the pooch entirely.
  */
  
  enumerate_interfaces(0);
  
  if (option_bool(OPT_NOWILD))
    iface = listener->iface; /* May be NULL */
  else 
    {
      int if_index;
      char intr_name[IF_NAMESIZE];
      
      /* if we can find the arrival interface, check it's one that's allowed */
      if ((if_index = tcp_interface(confd, tcp_addr.sa.sa_family)) != 0 &&
	  indextoname(listener->tcpfd, if_index, intr_name))
	{
	  union all_addr addr;
	  
	  if (tcp_addr.sa.sa_family == AF_INET6)
	    addr.addr6 = tcp_addr.in6.sin6_addr;
	  else
	    addr.addr4 = tcp_addr.in.sin_addr;
	  
	  for (iface = daemon->interfaces; iface; iface = iface->next)
	    if (iface->index == if_index &&
		iface->addr.sa.sa_family == tcp_addr.sa.sa_family)
	      break;
	  
	  if (!iface && !loopback_exception(listener->tcpfd, tcp_addr.sa.sa_family, &addr, intr_name))
	    client_ok = 0;
	}
      
      if (option_bool(OPT_CLEVERBIND))
	iface = listener->iface; /* May be NULL */
      else
	{
	  /* Check for allowed interfaces when binding the wildcard address:
	     we do this by looking for an interface with the same address as 
	     the local address of the TCP connection, then looking to see if that's
	     an allowed interface. As a side effect, we get the netmask of the
	     interface too, for localisation. */
	  
	  for (iface = daemon->interfaces; iface; iface = iface->next)
	    if (sockaddr_isequal(&iface->addr, &tcp_addr))
	      break;
	  
	  if (!iface)
	    client_ok = 0;
	}
    }
  
  if (!client_ok)
    goto closeconandreturn;
  
  if (!option_bool(OPT_DEBUG))
    {
      if (pipe(pipefd) == -1)
	goto closeconandreturn; /* pipe failed */
            
      if ((p = fork()) == -1)
	{
	  /* fork failed */
	  close(pipefd[0]);
	  close(pipefd[1]);
	  goto closeconandreturn;
	}

      if (p != 0)
	{
	  /* fork() done: parent side */
	  close(pipefd[1]); /* parent needs read pipe end. */
      
#ifdef HAVE_LINUX_NETWORK
	  /* The child process inherits the netlink socket, 
	     which it never uses, but when the parent (us) 
	     uses it in the future, the answer may go to the 
	     child, resulting in the parent blocking
	     forever awaiting the result. To avoid this
	     the child closes the netlink socket, but there's
	     a nasty race, since the parent may use netlink
	     before the child has done the close.
	     
	     To avoid this, the parent blocks here until a 
	     single byte comes back up the pipe, which
	     is sent by the child after it has closed the
	     netlink socket. */

	  read_write(pipefd[0], &a, 1, RW_READ);
#endif
	  

	  daemon->tcp_pids[slot] = p;
	  daemon->tcp_pipes[slot] = pipefd[0];
	  daemon->metrics[METRIC_TCP_CONNECTIONS]++;
	  if (daemon->metrics[METRIC_TCP_CONNECTIONS] > daemon->max_procs_used)
	    daemon->max_procs_used = daemon->metrics[METRIC_TCP_CONNECTIONS];
	
	  close(confd);
	  
	  /* The child can use up to TCP_MAX_QUERIES ids, so skip that many. */
	  daemon->log_id += TCP_MAX_QUERIES;
#ifdef HAVE_DNSSEC
	  /* It can do more if making DNSSEC queries too. */
	  if (option_bool(OPT_DNSSEC_VALID))
	    daemon->log_id += daemon->limit[LIMIT_WORK];
#endif
	  
	  return;
	}
    }
         
  if (iface)
    {
      netmask = iface->netmask;
      auth_dns = iface->dns_auth;
    }
  else
    {
      netmask.s_addr = 0;
      auth_dns = 0;
    }
  
  /* Arrange for SIGALRM after CHILD_LIFETIME seconds to
     terminate the process. */
  if (!option_bool(OPT_DEBUG))
    {
#ifdef HAVE_LINUX_NETWORK
      /* See comment above re: netlink socket. */
      close(daemon->netlinkfd);
      read_write(pipefd[1], &a, 1, RW_WRITE);
#endif		  
      alarm(CHILD_LIFETIME);
      close(pipefd[0]); /* close read end in child. */
      daemon->pipe_to_parent = pipefd[1];
    }

  /* The connected socket inherits non-blocking
     attribute from the listening socket. 
     Reset that here. */
  if ((flags = fcntl(confd, F_GETFL, 0)) != -1)
    while(retry_send(fcntl(confd, F_SETFL, flags & ~O_NONBLOCK)));

  buff = tcp_request(confd, now, &tcp_addr, netmask, auth_dns);
	      
  if (buff)
    free(buff);
  
  for (s = daemon->servers; s; s = s->next)
    if (s->tcpfd != -1)
      {
	shutdown(s->tcpfd, SHUT_RDWR);
	close(s->tcpfd);
	s->tcpfd = -1;
      }
  
  if (!option_bool(OPT_DEBUG))
    {
#ifdef HAVE_DNSSEC
       cache_update_hwm(); /* Sneak out possibly updated crypto HWM values. */
#endif

      close(daemon->pipe_to_parent);
      flush_log();
      _exit(0);
    }
}


#ifdef HAVE_DNSSEC
/* If a DNSSEC query over UDP returns a truncated answer,
   we swap to the TCP path. This routine is responsible for forking
   the required process, the child then calls tcp_key_recurse() and
   returns the result of the validation through the pipe to the parent
   (which has also primed the cache with the relevant DS and DNSKEY records).
   If we're in debug mode, don't fork and return the result directly, otherwise
   return  STAT_ASYNC. The UDP validation process will restart when 
   cache_recv_insert() calls pop_and_retry_query() after the result 
   arrives via the pipe to the parent. */
int swap_to_tcp(struct frec *forward, time_t now, int status, struct dns_header *header,
		ssize_t *plen, char *name, int class, struct server *server, int *keycount, int *validatecount)
{
  struct server *s;

  if (!option_bool(OPT_DEBUG))
    {
      pid_t p;
      int i, pipefd[2];
#ifdef HAVE_LINUX_NETWORK
      unsigned char a = 0;
#endif
      
      /* check to see if we have a free tcp process slot. */
      for (i = daemon->max_procs - 1; i >= 0; i--)
	if (daemon->tcp_pids[i] == 0 && daemon->tcp_pipes[i] == -1)
	  break;
      
      /* No slots or no pipe */
      if (i < 0 || pipe(pipefd) != 0)
	return STAT_ABANDONED;
				
      if ((p = fork()) != 0)
	{
	  close(pipefd[1]); /* parent needs read pipe end. */
	  if (p == -1)
	    {
	      /* fork() failed */
	      close(pipefd[0]);
	      return STAT_ABANDONED;
	    }

#ifdef HAVE_LINUX_NETWORK
	  /* The child process inherits the netlink socket, 
	     which it never uses, but when the parent (us) 
	     uses it in the future, the answer may go to the 
	     child, resulting in the parent blocking
	     forever awaiting the result. To avoid this
	     the child closes the netlink socket, but there's
	     a nasty race, since the parent may use netlink
	     before the child has done the close.
	     
	     To avoid this, the parent blocks here until a 
	     single byte comes back up the pipe, which
	     is sent by the child after it has closed the
	     netlink socket. */
	  read_write(pipefd[0], &a, 1, RW_READ);
#endif
	  
	  /* i holds index of free slot */
	  daemon->tcp_pids[i] = p;
	  daemon->tcp_pipes[i] = pipefd[0];
	  daemon->metrics[METRIC_TCP_CONNECTIONS]++;
	  if (daemon->metrics[METRIC_TCP_CONNECTIONS] > daemon->max_procs_used)
	    daemon->max_procs_used = daemon->metrics[METRIC_TCP_CONNECTIONS];

	  /* child can use a maximum of this many log serials. */
	  daemon->log_id += daemon->limit[LIMIT_WORK];

	  /* tell the caller we've forked. */
	  return STAT_ASYNC;
	}
      else
	{
	  /* child starts here. */
#ifdef HAVE_LINUX_NETWORK
	  /* See comment above re: netlink socket. */
	  close(daemon->netlinkfd);
	  read_write(pipefd[1], &a, 1, RW_WRITE);
#endif		  
	  close(pipefd[0]); /* close read end in child. */
	  daemon->pipe_to_parent = pipefd[1];	  
	}
    }
  
  status = tcp_from_udp(now, status, header, plen, class, name, server, keycount, validatecount);
  
  /* close upstream connections. */
  for (s = daemon->servers; s; s = s->next)
    if (s->tcpfd != -1)
      {
	shutdown(s->tcpfd, SHUT_RDWR);
	close(s->tcpfd);
	s->tcpfd = -1;
      }
  
   if (!option_bool(OPT_DEBUG))
     {
       unsigned char op = PIPE_OP_RESULT;

       /* tell our parent we're done, and what the result was then exit. */
       read_write(daemon->pipe_to_parent, &op, sizeof(op), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)&status, sizeof(status), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)plen, sizeof(*plen), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)header, *plen, RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)&forward, sizeof(forward), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)&forward->uid, sizeof(forward->uid), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)keycount, sizeof(*keycount), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)&keycount, sizeof(keycount), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)validatecount, sizeof(*validatecount), RW_WRITE);
       read_write(daemon->pipe_to_parent, (unsigned char *)&validatecount, sizeof(validatecount), RW_WRITE);
      
       cache_update_hwm(); /* Sneak out possibly updated crypto HWM values. */
              
       close(daemon->pipe_to_parent);
       
       flush_log();
       _exit(0);
     }
   
   /* path for debug mode. */
   return status;
}
#endif


#ifdef HAVE_DHCP
int make_icmp_sock(void)
{
  int fd;
  int zeroopt = 0;

  if ((fd = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP)) != -1)
    {
      if (!fix_fd(fd) ||
	  setsockopt(fd, SOL_SOCKET, SO_DONTROUTE, &zeroopt, sizeof(zeroopt)) == -1)
	{
	  close(fd);
	  fd = -1;
	}
    }

  return fd;
}

int icmp_ping(struct in_addr addr)
{
  /* Try and get an ICMP echo from a machine. */

  int fd;
  struct sockaddr_in saddr;
  struct { 
    struct ip ip;
    struct icmp icmp;
  } packet;
  unsigned short id = rand16();
  unsigned int i, j;
  int gotreply = 0;

#if defined(HAVE_LINUX_NETWORK) || defined (HAVE_SOLARIS_NETWORK)
  if ((fd = make_icmp_sock()) == -1)
    return 0;
#else
  int opt = 2000;
  fd = daemon->dhcp_icmp_fd;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
#endif

  saddr.sin_family = AF_INET;
  saddr.sin_port = 0;
  saddr.sin_addr = addr;
#ifdef HAVE_SOCKADDR_SA_LEN
  saddr.sin_len = sizeof(struct sockaddr_in);
#endif
  
  memset(&packet.icmp, 0, sizeof(packet.icmp));
  packet.icmp.icmp_type = ICMP_ECHO;
  packet.icmp.icmp_id = id;
  for (j = 0, i = 0; i < sizeof(struct icmp) / 2; i++)
    j += ((u16 *)&packet.icmp)[i];
  while (j>>16)
    j = (j & 0xffff) + (j >> 16);  
  packet.icmp.icmp_cksum = (j == 0xffff) ? j : ~j;
  
  while (retry_send(sendto(fd, (char *)&packet.icmp, sizeof(struct icmp), 0, 
			   (struct sockaddr *)&saddr, sizeof(saddr))));
  
  gotreply = delay_dhcp(dnsmasq_time(), PING_WAIT, fd, addr.s_addr, id);

#if defined(HAVE_LINUX_NETWORK) || defined(HAVE_SOLARIS_NETWORK)
  close(fd);
#else
  opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
#endif

  return gotreply;
}

int delay_dhcp(time_t start, int sec, int fd, uint32_t addr, unsigned short id)
{
  /* Delay processing DHCP packets for "sec" seconds counting from "start".
     If "fd" is not -1 it will stop waiting if an ICMP echo reply is received
     from "addr" with ICMP ID "id" and return 1 */

  /* Note that whilst waiting, we check for
     (and service) events on the DNS and TFTP  sockets, (so doing that
     better not use any resources our caller has in use...)
     but we remain deaf to signals or further DHCP packets. */

  /* There can be a problem using dnsmasq_time() to end the loop, since
     it's not monotonic, and can go backwards if the system clock is
     tweaked, leading to the code getting stuck in this loop and
     ignoring DHCP requests. To fix this, we check to see if select returned
     as a result of a timeout rather than a socket becoming available. We
     only allow this to happen as many times as it takes to get to the wait time
     in quarter-second chunks. This provides a fallback way to end loop. */

  int rc, timeout_count;
  time_t now;

  for (now = dnsmasq_time(), timeout_count = 0;
       (difftime(now, start) <= (float)sec) && (timeout_count < sec * 4);)
    {
      poll_reset();
      if (fd != -1)
        poll_listen(fd, POLLIN);
      if (daemon->port != 0)
	set_dns_listeners();
#ifdef HAVE_TFTP
      set_tftp_listeners();
#endif
      set_log_writer();
      
#ifdef HAVE_DHCP6
      if (daemon->doing_ra)
	poll_listen(daemon->icmp6fd, POLLIN); 
#endif
      
      rc = do_poll(250);
      
      if (rc < 0)
	continue;
      else if (rc == 0)
	timeout_count++;

      now = dnsmasq_time();
      
      check_log_writer(0);
      if (daemon->port != 0)
	check_dns_listeners(now);
      
#ifdef HAVE_DHCP6
      if (daemon->doing_ra && poll_check(daemon->icmp6fd, POLLIN))
	icmp6_packet(now);
#endif
      
#ifdef HAVE_TFTP
      check_tftp_listeners(now);
#endif

      if (fd != -1)
        {
          struct {
            struct ip ip;
            struct icmp icmp;
          } packet;
          struct sockaddr_in faddr;
          socklen_t len = sizeof(faddr);
	  
          if (poll_check(fd, POLLIN) &&
	      recvfrom(fd, &packet, sizeof(packet), 0, (struct sockaddr *)&faddr, &len) == sizeof(packet) &&
	      addr == faddr.sin_addr.s_addr &&
	      packet.icmp.icmp_type == ICMP_ECHOREPLY &&
	      packet.icmp.icmp_seq == 0 &&
	      packet.icmp.icmp_id == id)
	    return 1;
	}
    }

  return 0;
}
#endif /* HAVE_DHCP */


