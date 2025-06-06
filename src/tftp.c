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

#include "dnsmasq.h"

#ifdef HAVE_TFTP

static void handle_tftp(time_t now, struct tftp_transfer *transfer, ssize_t len);
static struct tftp_file *check_tftp_fileperm(ssize_t *len, char *prefix, char *client);
static void free_transfer(struct tftp_transfer *transfer);
static ssize_t tftp_err(int err, char *packet, char *message, char *file, char *arg2);
static ssize_t tftp_err_oops(char *packet, const char *file);
static ssize_t get_block(char *packet, struct tftp_transfer *transfer);
static char *next(char **p, char *end);
static void sanitise(char *buf);

#define OP_RRQ  1
#define OP_WRQ  2
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERR  5
#define OP_OACK 6

#define ERR_NOTDEF 0
#define ERR_FNF    1
#define ERR_PERM   2
#define ERR_FULL   3
#define ERR_ILL    4
#define ERR_TID    5

static void tftp_request(struct listener *listen, time_t now)
{
  ssize_t len;
  char *packet = daemon->packet;
  char *filename, *mode, *p, *end;
  union mysockaddr addr, peer;
  struct msghdr msg;
  struct iovec iov;
  struct ifreq ifr;
  int is_err = 1, if_index = 0, mtu = 0;
  struct iname *tmp;
  struct tftp_transfer *transfer = NULL, **up;
  int port = daemon->start_tftp_port; /* may be zero to use ephemeral port */
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
  int mtuflag = IP_PMTUDISC_DONT;
#endif
  char namebuff[IF_NAMESIZE];
  char *name = NULL;
  char *prefix = daemon->tftp_prefix;
  struct tftp_prefix *pref;
  union all_addr addra;
  int family = listen->addr.sa.sa_family;
  /* Can always get recvd interface for IPv6 */
  int check_dest = !option_bool(OPT_NOWILD) || family == AF_INET6;
  union {
    struct cmsghdr align; /* this ensures alignment */
    char control6[CMSG_SPACE(sizeof(struct in6_pktinfo))];
#if defined(HAVE_LINUX_NETWORK)
    char control[CMSG_SPACE(sizeof(struct in_pktinfo))];
#elif defined(HAVE_SOLARIS_NETWORK)
    char control[CMSG_SPACE(sizeof(struct in_addr)) +
		 CMSG_SPACE(sizeof(unsigned int))];
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
    char control[CMSG_SPACE(sizeof(struct in_addr)) +
		 CMSG_SPACE(sizeof(struct sockaddr_dl))];
#endif
  } control_u; 

  msg.msg_controllen = sizeof(control_u);
  msg.msg_control = control_u.control;
  msg.msg_flags = 0;
  msg.msg_name = &peer;
  msg.msg_namelen = sizeof(peer);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  iov.iov_base = packet;
  iov.iov_len = daemon->packet_buff_sz;

  /* we overwrote the buffer... */
  daemon->srv_save = NULL;

  if ((len = recvmsg(listen->tftpfd, &msg, 0)) < 2)
    return;

#ifdef HAVE_DUMPFILE
  dump_packet_udp(DUMP_TFTP, (void *)packet, len, (union mysockaddr *)&peer, NULL, listen->tftpfd);
#endif
  
  /* Can always get recvd interface for IPv6 */
  if (!check_dest)
    {
      if (listen->iface)
	{
	  addr = listen->iface->addr;
	  name = listen->iface->name;
	  mtu = listen->iface->mtu;
	  if (daemon->tftp_mtu != 0 && daemon->tftp_mtu < mtu)
	    mtu = daemon->tftp_mtu;
	}
      else
	{
	  /* we're listening on an address that doesn't appear on an interface,
	     ask the kernel what the socket is bound to */
	  socklen_t tcp_len = sizeof(union mysockaddr);
	  if (getsockname(listen->tftpfd, (struct sockaddr *)&addr, &tcp_len) == -1)
	    return;
	}
    }
  else
    {
      struct cmsghdr *cmptr;

      if (msg.msg_controllen < sizeof(struct cmsghdr))
        return;
      
      addr.sa.sa_family = family;
      
#if defined(HAVE_LINUX_NETWORK)
      if (family == AF_INET)
	for (cmptr = CMSG_FIRSTHDR(&msg); cmptr; cmptr = CMSG_NXTHDR(&msg, cmptr))
	  if (cmptr->cmsg_level == IPPROTO_IP && cmptr->cmsg_type == IP_PKTINFO)
	    {
	      union {
		unsigned char *c;
		struct in_pktinfo *p;
	      } p;
	      p.c = CMSG_DATA(cmptr);
	      addr.in.sin_addr = p.p->ipi_spec_dst;
	      if_index = p.p->ipi_ifindex;
	    }
      
#elif defined(HAVE_SOLARIS_NETWORK)
      if (family == AF_INET)
	for (cmptr = CMSG_FIRSTHDR(&msg); cmptr; cmptr = CMSG_NXTHDR(&msg, cmptr))
	  {
	    union {
	      unsigned char *c;
	      struct in_addr *a;
	      unsigned int *i;
	    } p;
	    p.c = CMSG_DATA(cmptr);
	    if (cmptr->cmsg_level == IPPROTO_IP && cmptr->cmsg_type == IP_RECVDSTADDR)
	    addr.in.sin_addr = *(p.a);
	    else if (cmptr->cmsg_level == IPPROTO_IP && cmptr->cmsg_type == IP_RECVIF)
	    if_index = *(p.i);
	  }
      
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
      if (family == AF_INET)
	for (cmptr = CMSG_FIRSTHDR(&msg); cmptr; cmptr = CMSG_NXTHDR(&msg, cmptr))
	  {
	    union {
	      unsigned char *c;
	      struct in_addr *a;
	      struct sockaddr_dl *s;
	    } p;
	    p.c = CMSG_DATA(cmptr);
	    if (cmptr->cmsg_level == IPPROTO_IP && cmptr->cmsg_type == IP_RECVDSTADDR)
	      addr.in.sin_addr = *(p.a);
	    else if (cmptr->cmsg_level == IPPROTO_IP && cmptr->cmsg_type == IP_RECVIF)
	      if_index = p.s->sdl_index;
	  }
	  
#endif

      if (family == AF_INET6)
        {
          for (cmptr = CMSG_FIRSTHDR(&msg); cmptr; cmptr = CMSG_NXTHDR(&msg, cmptr))
            if (cmptr->cmsg_level == IPPROTO_IPV6 && cmptr->cmsg_type == daemon->v6pktinfo)
              {
                union {
                  unsigned char *c;
                  struct in6_pktinfo *p;
                } p;
                p.c = CMSG_DATA(cmptr);
                  
                addr.in6.sin6_addr = p.p->ipi6_addr;
                if_index = p.p->ipi6_ifindex;
              }
        }
      
      if (!indextoname(listen->tftpfd, if_index, namebuff))
	return;

      name = namebuff;
      
      if (family == AF_INET6)
	addra.addr6 = addr.in6.sin6_addr;
      else
	addra.addr4 = addr.in.sin_addr;
      
      if (daemon->tftp_interfaces)
	{
	  /* dedicated tftp interface list */
	  for (tmp = daemon->tftp_interfaces; tmp; tmp = tmp->next)
	    if (tmp->name && wildcard_match(tmp->name, name))
	      break;

	  if (!tmp)
	    return;
	}
      else
	{
	  /* Do the same as DHCP */
	  if (!iface_check(family, &addra, name, NULL))
	    {
	      if (!option_bool(OPT_CLEVERBIND))
		enumerate_interfaces(0); 
	      if (!loopback_exception(listen->tftpfd, family, &addra, name) &&
		  !label_exception(if_index, family, &addra))
		return;
	    }
	  
#ifdef HAVE_DHCP      
	  /* allowed interfaces are the same as for DHCP */
	  for (tmp = daemon->dhcp_except; tmp; tmp = tmp->next)
	    if (tmp->name && (tmp->flags & INAME_4) && (tmp->flags & INAME_6) &&
		wildcard_match(tmp->name, name))
	      return;
#endif
	}

      safe_strncpy(ifr.ifr_name, name, IF_NAMESIZE);
      if (ioctl(listen->tftpfd, SIOCGIFMTU, &ifr) != -1)
	{
	  mtu = ifr.ifr_mtu;  
	  if (daemon->tftp_mtu != 0 && daemon->tftp_mtu < mtu)
	    mtu = daemon->tftp_mtu;    
	}
    }

  /* Failed to get interface mtu - can use configured value. */
  if (mtu == 0)
    mtu = daemon->tftp_mtu;

  /* data transfer via server listening socket */
  if (option_bool(OPT_SINGLE_PORT))
    {
      int tftp_cnt;

      for (tftp_cnt = 0, transfer = daemon->tftp_trans, up = &daemon->tftp_trans; transfer; up = &transfer->next, transfer = transfer->next)
	{
	  tftp_cnt++;

	  if (sockaddr_isequal(&peer, &transfer->peer))
	    {
	      if (ntohs(*((unsigned short *)packet)) == OP_RRQ)
		{
		  /* Handle repeated RRQ or abandoned transfer from same host and port 
		     by unlinking and reusing the struct transfer. */
		  *up = transfer->next;
		  break;
		}
	      else
		{
		  handle_tftp(now, transfer, len);
		  return;
		}
	    }
	}
      
      /* Enforce simultaneous transfer limit. In non-single-port mode
	 this is done by not listening on the server socket when
	 too many transfers are in progress. */
      if (!transfer && tftp_cnt >= daemon->tftp_max)
	return;
    }
  
  if (name)
    {
      /* check for per-interface prefix */ 
      for (pref = daemon->if_prefix; pref; pref = pref->next)
	if (strcmp(pref->interface, name) == 0)
	  prefix = pref->prefix;  
    }

  if (family == AF_INET)
    {
      addr.in.sin_port = htons(port);
#ifdef HAVE_SOCKADDR_SA_LEN
      addr.in.sin_len = sizeof(addr.in);
#endif
    }
  else
    {
      addr.in6.sin6_port = htons(port);
      addr.in6.sin6_flowinfo = 0;
      addr.in6.sin6_scope_id = 0;
#ifdef HAVE_SOCKADDR_SA_LEN
      addr.in6.sin6_len = sizeof(addr.in6);
#endif
    }

  /* May reuse struct transfer from abandoned transfer in single port mode. */
  if (!transfer && !(transfer = whine_malloc(sizeof(struct tftp_transfer))))
    return;

  memset(transfer, 0, sizeof(struct tftp_transfer));
	 
  if (option_bool(OPT_SINGLE_PORT))
    transfer->sockfd = listen->tftpfd;
  else if ((transfer->sockfd = socket(family, SOCK_DGRAM, 0)) == -1)
    {
      free(transfer);
      return;
    }
  
  transfer->peer = peer;
  transfer->source = addra;
  transfer->if_index = if_index;
  transfer->timeout = 2;
  transfer->start = now;
  transfer->backoff = 1;
  transfer->block = 1;
  transfer->blocksize = 512;
  transfer->windowsize = 1;
  
  (void)prettyprint_addr(&peer, daemon->addrbuff);
  
  /* if we have a nailed-down range, iterate until we find a free one. */
  while (!option_bool(OPT_SINGLE_PORT))
    {
      if (bind(transfer->sockfd, &addr.sa, sa_len(&addr)) == -1 ||
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
	  setsockopt(transfer->sockfd, IPPROTO_IP, IP_MTU_DISCOVER, &mtuflag, sizeof(mtuflag)) == -1 ||
#endif
	  !fix_fd(transfer->sockfd))
	{
	  if (errno == EADDRINUSE && daemon->start_tftp_port != 0)
	    {
	      if (++port <= daemon->end_tftp_port)
		{ 
		  if (family == AF_INET)
		    addr.in.sin_port = htons(port);
		  else
		    addr.in6.sin6_port = htons(port);
		  
		  continue;
		}
	      my_syslog(MS_TFTP | LOG_ERR, _("unable to get free port for TFTP"));
	    }
	  free_transfer(transfer);
	  return;
	}
      break;
    }
  
  p = packet + 2;
  end = packet + len;

  len = 0;
  
  if (ntohs(*((unsigned short *)packet)) == OP_WRQ)
    len = tftp_err(ERR_ILL, packet, _("unsupported write request from %s"),daemon->addrbuff, NULL);
  else if (ntohs(*((unsigned short *)packet)) == OP_RRQ)
    {
      if (!(filename = next(&p, end)))
	len = tftp_err(ERR_ILL, packet, _("empty filename in request from %s"), daemon->addrbuff, NULL);
      else if (!(mode = next(&p, end)) || (strcasecmp(mode, "octet") != 0 && strcasecmp(mode, "netascii") != 0))
	len = tftp_err(ERR_ILL, packet, _("unsupported request from %s"),daemon->addrbuff, NULL);
      else
	{
	  char *opt, *arg;
	  
	  if (strcasecmp(mode, "netascii") == 0)
	    transfer->netascii = 1;
	  
	  while ((opt = next(&p, end)) && (arg = next(&p, end)))
	    {
	      unsigned int val = atoi(arg);
	      
	      if (strcasecmp(opt, "blksize") == 0 && !option_bool(OPT_TFTP_NOBLOCK))
		{
		  /* 32 bytes for IP, UDP and TFTP headers, 52 bytes for IPv6 */
		  int overhead = (family == AF_INET) ? 32 : 52;
		  if (val < 1)
		    val  = 1;
		  if (val > (unsigned)daemon->packet_buff_sz - 4)
		    val  = (unsigned)daemon->packet_buff_sz - 4;
		  if (mtu != 0 && val > (unsigned)mtu - overhead)
		    val  = (unsigned)mtu - overhead;
		  transfer->blocksize = val;
		  transfer->opt_blocksize = 1;
		  transfer->block = 0;
		}
	      else if (strcasecmp(opt, "tsize") == 0 && !transfer->netascii)
		{
		  transfer->opt_transize = 1;
		  transfer->block = 0;
		}
	      else if (strcasecmp(opt, "timeout") == 0)
		{
		  if (val > 255)
		    val = 255;
		  transfer->timeout = val;
		  transfer->opt_timeout = 1;
		  transfer->block = 0;
		}
	      else if (strcasecmp(opt, "windowsize") == 0 && !transfer->netascii)
		{
		  /* windowsize option only supported for binary transfers. */
		  if (val < 1)
		    val = 1;
		  if (val > TFTP_MAX_WINDOW)
		    val = TFTP_MAX_WINDOW;
		  transfer->windowsize = val;
		  transfer->opt_windowsize = 1;
		  transfer->block = 0;
		}
	    }
	  
	  /* cope with backslashes from windows boxen. */
	  for (p = filename; *p; p++)
	    if (*p == '\\')
	      *p = '/';
	    else if (option_bool(OPT_TFTP_LC))
	      *p = tolower((unsigned char)*p);
	  
	  strcpy(daemon->namebuff, "/");
	  if (prefix)
	    {
	      if (prefix[0] == '/')
		daemon->namebuff[0] = 0;
	      strncat(daemon->namebuff, prefix, (MAXDNAME-1) - strlen(daemon->namebuff));
	      if (prefix[strlen(prefix)-1] != '/')
		strncat(daemon->namebuff, "/", (MAXDNAME-1) - strlen(daemon->namebuff));
	      
	      if (option_bool(OPT_TFTP_APREF_IP))
		{
		  size_t oldlen = strlen(daemon->namebuff);
		  struct stat statbuf;
		  
		  strncat(daemon->namebuff, daemon->addrbuff, (MAXDNAME-1) - strlen(daemon->namebuff));
		  strncat(daemon->namebuff, "/", (MAXDNAME-1) - strlen(daemon->namebuff));
		  
		  /* remove unique-directory if it doesn't exist */
		  if (stat(daemon->namebuff, &statbuf) == -1 || !S_ISDIR(statbuf.st_mode))
		    daemon->namebuff[oldlen] = 0;
		}
	      
	      if (option_bool(OPT_TFTP_APREF_MAC))
		{
		  unsigned char *macaddr = NULL;
		  unsigned char macbuf[DHCP_CHADDR_MAX];
		  
#ifdef HAVE_DHCP
		  if (daemon->dhcp && peer.sa.sa_family == AF_INET)
		    {
		      /* Check if the client IP is in our lease database */
		      struct dhcp_lease *lease = lease_find_by_addr(peer.in.sin_addr);
		      if (lease && lease->hwaddr_type == ARPHRD_ETHER && lease->hwaddr_len == ETHER_ADDR_LEN)
			macaddr = lease->hwaddr;
		    }
#endif
		  
		  /* If no luck, try to find in ARP table. This only works if client is in same (V)LAN */
		  if (!macaddr && find_mac(&peer, macbuf, 1, now) > 0)
		    macaddr = macbuf;
		  
		  if (macaddr)
		    {
		      size_t oldlen = strlen(daemon->namebuff);
		      struct stat statbuf;
		      
		      snprintf(daemon->namebuff + oldlen, (MAXDNAME-1) - oldlen, "%.2x-%.2x-%.2x-%.2x-%.2x-%.2x/",
			       macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5]);
		      
		      /* remove unique-directory if it doesn't exist */
		      if (stat(daemon->namebuff, &statbuf) == -1 || !S_ISDIR(statbuf.st_mode))
			daemon->namebuff[oldlen] = 0;
		    }
		}
	      
	      /* Absolute pathnames OK if they match prefix */
	      if (filename[0] == '/')
		{
		  if (strstr(filename, daemon->namebuff) == filename)
		    daemon->namebuff[0] = 0;
		  else
		    filename++;
		}
	    }
	  else if (filename[0] == '/')
	    daemon->namebuff[0] = 0;
	  strncat(daemon->namebuff, filename, (MAXDNAME-1) - strlen(daemon->namebuff));
	  
	  /* check permissions and open file */
	  if ((transfer->file = check_tftp_fileperm(&len, prefix, daemon->addrbuff)))
	    {
	      transfer->lastack = transfer->block;
	      transfer->retransmit = now + transfer->timeout;
	      /* This packet is may be the first data packet, but only if windowsize == 1
		 To get windowsize greater then one requires an option negotiation,
		 in which case this packet is the OACK. */
	      if ((len = get_block(packet, transfer)) == -1)
		len = tftp_err_oops(packet, daemon->namebuff);
	      else
		is_err = 0;
	    }
	}
    }
  
  if (len)
    {
      send_from(transfer->sockfd, !option_bool(OPT_SINGLE_PORT), packet, len, &peer, &addra, if_index);
      
#ifdef HAVE_DUMPFILE
      dump_packet_udp(DUMP_TFTP, (void *)packet, len, NULL, (union mysockaddr *)&peer, transfer->sockfd);
#endif
    }
  
  if (is_err)
    free_transfer(transfer);
  else
    {
      transfer->next = daemon->tftp_trans;
      daemon->tftp_trans = transfer;
    }
}
 
static struct tftp_file *check_tftp_fileperm(ssize_t *len, char *prefix, char *client)
{
  char *packet = daemon->packet, *namebuff = daemon->namebuff;
  struct tftp_file *file;
  struct tftp_transfer *t;
  uid_t uid = geteuid();
  struct stat statbuf;
  int fd = -1;

  /* trick to ban moving out of the subtree */
  if (prefix && strstr(namebuff, "/../"))
    goto perm;
  
  if ((fd = open(namebuff, O_RDONLY)) == -1)
    {
      if (errno == ENOENT)
	{
	  *len = tftp_err(ERR_FNF, packet, _("file %s not found for %s"), namebuff, client);
	  return NULL;
	}
      else if (errno == EACCES)
	goto perm;
      else
	goto oops;
    }
  
  /* stat the file descriptor to avoid stat->open races */
  if (fstat(fd, &statbuf) == -1)
    goto oops;
  
  /* running as root, must be world-readable */
  if (uid == 0)
    {
      if (!(statbuf.st_mode & S_IROTH))
	goto perm;
    }
  /* in secure mode, must be owned by user running dnsmasq */
  else if (option_bool(OPT_TFTP_SECURE) && uid != statbuf.st_uid)
    goto perm;
      
  /* If we're doing many transfers from the same file, only 
     open it once this saves lots of file descriptors 
     when mass-booting a big cluster, for instance. 
     Be conservative and only share when inode and name match
     this keeps error messages sane. */
  for (t = daemon->tftp_trans; t; t = t->next)
    if (t->file->dev == statbuf.st_dev && 
	t->file->inode == statbuf.st_ino &&
	strcmp(t->file->filename, namebuff) == 0)
      {
	close(fd);
	t->file->refcount++;
	return t->file;
      }
  
  if (!(file = whine_malloc(sizeof(struct tftp_file) + strlen(namebuff) + 1)))
    {
      errno = ENOMEM;
      goto oops;
    }

  file->fd = fd;
  file->size = statbuf.st_size;
  file->dev = statbuf.st_dev;
  file->inode = statbuf.st_ino;
  file->refcount = 1;
  strcpy(file->filename, namebuff);
  return file;
  
 perm:
  *len =  tftp_err(ERR_PERM, packet, _("cannot access %s: %s"), namebuff, strerror(EACCES));
  if (fd != -1)
    close(fd);
  return NULL;

 oops:
  *len =  tftp_err_oops(packet, namebuff);
  if (fd != -1)
    close(fd);
  return NULL;
}

void check_tftp_listeners(time_t now)
{
  struct listener *listener;
  struct tftp_transfer *transfer, *tmp, **up;
  
  for (listener = daemon->listeners; listener; listener = listener->next)
    if (listener->tftpfd != -1 && poll_check(listener->tftpfd, POLLIN))
      tftp_request(listener, now);
    
  /* In single port mode, all packets come via port 69 and tftp_request() */
  if (!option_bool(OPT_SINGLE_PORT))
    for (transfer = daemon->tftp_trans; transfer; transfer = transfer->next)
      if (poll_check(transfer->sockfd, POLLIN))
	{
	  union mysockaddr peer;
	  socklen_t addr_len = sizeof(union mysockaddr);
	  ssize_t len;
	  
	  /* we overwrote the buffer... */
	  daemon->srv_save = NULL;

	  if ((len = recvfrom(transfer->sockfd, daemon->packet, daemon->packet_buff_sz, 0, &peer.sa, &addr_len)) > 0)
	    {
#ifdef HAVE_DUMPFILE
	      dump_packet_udp(DUMP_TFTP, (void *)daemon->packet, len, (union mysockaddr *)&peer, NULL, transfer->sockfd);
#endif	      

	      if (sockaddr_isequal(&peer, &transfer->peer)) 
		handle_tftp(now, transfer, len);
	      else
		{
		  /* Wrong source address. See rfc1350 para 4. */
		  prettyprint_addr(&peer, daemon->addrbuff);
		  len = tftp_err(ERR_TID, daemon->packet, _("ignoring packet from %s (TID mismatch)"), daemon->addrbuff, NULL);
		  while(retry_send(sendto(transfer->sockfd, daemon->packet, len, 0, &peer.sa, sa_len(&peer))));

#ifdef HAVE_DUMPFILE
		  dump_packet_udp(DUMP_TFTP, (void *)daemon->packet, len, NULL, (union mysockaddr *)&peer, transfer->sockfd);
#endif
		}
	    }
	}
	  
  for (transfer = daemon->tftp_trans, up = &daemon->tftp_trans; transfer; transfer = tmp)
    {
      int endcon = 0, error = 0, timeout = 0;
      
      tmp = transfer->next;
            
      /* ->start set to zero in handle_tftp() when we recv an error packet. */
      if (transfer->start == 0)
	endcon = error = 1;
      else if (difftime(now, transfer->start) > TFTP_TRANSFER_TIME)  
	{
	  endcon = 1;
	  /* don't complain about timeout when we're awaiting the last
	     ACK, some clients never send it */
	  if (get_block(daemon->packet, transfer) > 0)
	    error = timeout = 1;
	}
      else if (difftime(now, transfer->retransmit) >= 0.0)
	{
	  /* Do transmission or re-transmission. When we get an ACK, the call to handle_tftp()
	     bumps transfer->lastack and trips the retransmit timer so that we send the next block(s)
	     here. */
	  ssize_t len;
	  
	  transfer->retransmit += transfer->timeout + (1<<(transfer->backoff/2));
	  transfer->backoff++;
	  transfer->block = transfer->lastack;
	  
	  if ((len = get_block(daemon->packet, transfer)) == 0)
	    endcon = 1; /* got last ACK */
	  else
	    {
	      /* send a window'a worth of blocks unless we're retransmitting OACK */
	      unsigned int i, winsize = transfer->block ? transfer->windowsize : 1;
	      
	      for (i = 0; i < winsize && !endcon; i++, transfer->block++)
		{
		  if (i != 0)
		    len = get_block(daemon->packet, transfer);

		  if (len == 0)
		    break;
		  
		  if (len == -1)
		    {
		      len = tftp_err_oops(daemon->packet, transfer->file->filename);
		      endcon = error = 1;
		    }
		  
		  send_from(transfer->sockfd, !option_bool(OPT_SINGLE_PORT), daemon->packet, len,
			    &transfer->peer, &transfer->source, transfer->if_index);
#ifdef HAVE_DUMPFILE
		  dump_packet_udp(DUMP_TFTP, (void *)daemon->packet, len, NULL, (union mysockaddr *)&transfer->peer, transfer->sockfd);
#endif
		}
	    }
	}
	      
      if (endcon)
	{
	  strcpy(daemon->namebuff, transfer->file->filename);
	  sanitise(daemon->namebuff);
	  (void)prettyprint_addr(&transfer->peer, daemon->addrbuff);
	  if (timeout)
	    my_syslog(MS_TFTP | LOG_ERR, _("timeout sending %s to %s"), daemon->namebuff, daemon->addrbuff);
	  else if (error)
	    my_syslog(MS_TFTP | LOG_ERR, _("failed sending %s to %s"), daemon->namebuff, daemon->addrbuff);
	  else
	    my_syslog(MS_TFTP | LOG_INFO, _("sent %s to %s"), daemon->namebuff, daemon->addrbuff);
	  
	  /* unlink */
	  *up = tmp;
	  if (error)
	    free_transfer(transfer);
	  else
	    {
	      /* put on queue to be sent to script and deleted */
	      transfer->next = daemon->tftp_done_trans;
	      daemon->tftp_done_trans = transfer;
	    }
	}
      else
	up = &transfer->next;
    }
}

/* packet in daemon->packet as this is called. */
static void handle_tftp(time_t now, struct tftp_transfer *transfer, ssize_t len)
{
  struct ack {
    unsigned short op, block;
  } *mess = (struct ack *)daemon->packet;
  
  if (len >= (ssize_t)sizeof(struct ack))
    {
      if (ntohs(mess->op) == OP_ACK)
	{
	  /* try and handle 16-bit blockno wrap-around */
	  unsigned int block = (unsigned short)ntohs(mess->block);
	  if (block < transfer->lastack)
	    block |= transfer->block & 0xffff0000;
	  
	  /* ignore duplicate ACKs and ACKs for blocks we've not yet sent. */
	  if (block >= transfer->lastack &&
	      block <= transfer->block) 
	    {
	      /* Got ack, move forward and ensure we take the (re)transmit path */
	      transfer->retransmit = transfer->start = now;
	      transfer->backoff = 0;
	      transfer->lastack = block + 1;
	      
	      /* We have no easy function from block no. to file offset when
		 expanding line breaks in netascii mode, so we update the offset here
		 as each block is acknowledged. This explains why the window size must be
		 one for a netascii transfer; to avoid  the block no. doing anything
		 other than incrementing by one. */
	      if (transfer->netascii && block != 0)
		transfer->offset += transfer->blocksize - transfer->expansion;
	    }
	}
      else if (ntohs(mess->op) == OP_ERR)
	{
	  char *p = daemon->packet + sizeof(struct ack);
	  char *end = daemon->packet + len;
	  char *err = next(&p, end);
	  
	  (void)prettyprint_addr(&transfer->peer, daemon->addrbuff);
	  
	  /* Sanitise error message */
	  if (!err)
	    err = "";
	  else
	    sanitise(err);
	  
	  my_syslog(MS_TFTP | LOG_ERR, _("error %d %s received from %s"),
		    (int)ntohs(mess->block), err, 
		    daemon->addrbuff);	
	  
	  /* Got err, ensure we take abort */
	  transfer->start = 0;
	}
    }
}

static void free_transfer(struct tftp_transfer *transfer)
{
  if (!option_bool(OPT_SINGLE_PORT))
    close(transfer->sockfd);

  if (transfer->file && (--transfer->file->refcount) == 0)
    {
      close(transfer->file->fd);
      free(transfer->file);
    }
  
  free(transfer);
}

static char *next(char **p, char *end)
{
  char *n, *ret = *p;
  
  /* Look for end of string, without running off the end of the packet. */
  for (n = *p; n < end && *n != 0; n++);

  /* ran off the end or zero length string - failed */
  if (n == end || n == ret)
    return NULL;
  
  *p = n + 1;
  return ret;
}

/* If we don't do anything, don't write the the input/ouptut
   buffer. This allows us to pass in safe read-only strings constants. */
static void sanitise(char *buf)
{
  unsigned char *q, *r;

  for (q = r = (unsigned char *)buf; *r; r++)
    if (isprint((int)*r))
      {
	if (q != r)
	  *q = *r;
	q++;
      }
  
  if (q != r)
    *q = 0;
}

#define MAXMESSAGE 500 /* limit to make packet < 512 bytes and definitely smaller than buffer */ 
static ssize_t tftp_err(int err, char *packet, char *message, char *file, char *arg2)
{
  struct errmess {
    unsigned short op, err;
    char message[];
  } *mess = (struct errmess *)packet;
  ssize_t len, ret = 4;

  /* we overwrote the buffer... */
  daemon->srv_save = NULL;

  memset(packet, 0, daemon->packet_buff_sz);
  if (file)
    sanitise(file);
  
  mess->op = htons(OP_ERR);
  mess->err = htons(err);
  len = snprintf(mess->message, MAXMESSAGE,  message, file, arg2);
  ret += (len < MAXMESSAGE) ? len + 1 : MAXMESSAGE; /* include terminating zero */
  
  if (err != ERR_FNF || !option_bool(OPT_QUIET_TFTP))
    my_syslog(MS_TFTP | LOG_ERR, "%s", mess->message);
  
  return  ret;
}

static ssize_t tftp_err_oops(char *packet, const char *file)
{
  /* May have >1 refs to file, so potentially mangle a copy of the name */
  if (file != daemon->namebuff)
    strcpy(daemon->namebuff, file);
  return tftp_err(ERR_NOTDEF, packet, _("cannot read %s: %s"), daemon->namebuff, strerror(errno));
}

/* return -1 for error, zero for done. */
static ssize_t get_block(char *packet, struct tftp_transfer *transfer)
{
  memset(packet, 0, daemon->packet_buff_sz);

  /* we overwrote the buffer... */
  daemon->srv_save = NULL;

  if (transfer->block == 0)
    {
      /* send OACK */
      char *p;
      struct oackmess {
	unsigned short op;
	char data[];
      } *mess = (struct oackmess *)packet;
      
      p = mess->data;
      mess->op = htons(OP_OACK);
      if (transfer->opt_blocksize)
	{
	  p += (sprintf(p, "blksize") + 1);
	  p += (sprintf(p, "%u", transfer->blocksize) + 1);
	}
      if (transfer->opt_transize)
	{
	  p += (sprintf(p,"tsize") + 1);
	  p += (sprintf(p, "%u", (unsigned int)transfer->file->size) + 1);
	}
      if (transfer->opt_timeout)
	{
	  p += (sprintf(p,"timeout") + 1);
	  p += (sprintf(p, "%u", transfer->timeout) + 1);
	}
      if (transfer->opt_windowsize)
	{
	  p += (sprintf(p,"windowsize") + 1);
	  p += (sprintf(p, "%u", (unsigned int)transfer->windowsize) + 1);
	}
 
      return p - packet;
    }
  else
    {
      /* send data packet */
      struct datamess {
	unsigned short op, block;
	unsigned char data[];
      } *mess = (struct datamess *)packet;
      
      size_t size;
      
      if (!transfer->netascii)
	transfer->offset = (transfer->block - 1) * transfer->blocksize;
      
      if (transfer->offset > transfer->file->size)
	return 0; /* finished */
      
      if ((size = transfer->file->size - transfer->offset) > transfer->blocksize)
	size = transfer->blocksize;
      
      mess->op = htons(OP_DATA);
      mess->block = htons((unsigned short)(transfer->block));

      if (size != 0 &&
	  (lseek(transfer->file->fd, transfer->offset, SEEK_SET) == (off_t)-1 ||
	   !read_write(transfer->file->fd, mess->data, size, RW_READ)))
	return -1;
      
      /* Map '\n' to CR-LF in netascii mode */
      if (transfer->netascii)
	{
	  size_t i;
	  int newcarrylf;
	  
	  transfer->expansion = 0;
	  
	  for (i = 0, newcarrylf = 0; i < size; i++)
	    if (mess->data[i] == '\n' && (i != 0 || !transfer->carrylf))
	      {
		transfer->expansion++;

		if (size != transfer->blocksize)
		  size++; /* room in this block */
		else  if (i == size - 1)
		  newcarrylf = 1; /* don't expand LF again if it moves to the next block */
		  
		/* make space and insert CR */
		memmove(&mess->data[i+1], &mess->data[i], size - (i + 1));
		mess->data[i] = '\r';
		
		i++;
	      }

	  transfer->carrylf = newcarrylf;
	}

      return size + 4;
    }
}


int do_tftp_script_run(void)
{
  struct tftp_transfer *transfer;

  if ((transfer = daemon->tftp_done_trans))
    {
      daemon->tftp_done_trans = transfer->next;
#ifdef HAVE_SCRIPT
      queue_tftp(transfer->file->size, transfer->file->filename, &transfer->peer);
#endif
      free_transfer(transfer);
      return 1;
    }

  return 0;
}
#endif
