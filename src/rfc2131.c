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

#ifdef HAVE_DHCP

#define option_len(opt) ((int)(((unsigned char *)(opt))[1]))
#define option_ptr(opt, i) ((void *)&(((unsigned char *)(opt))[2u+(unsigned int)(i)]))

#ifdef HAVE_SCRIPT
static void add_extradata_opt(struct dhcp_lease *lease, unsigned char *opt);
#endif

static int sanitise(unsigned char *opt, char *buf);
static struct in_addr server_id(struct dhcp_context *context, struct in_addr override, struct in_addr fallback);
static unsigned int calc_time(struct dhcp_context *context, struct dhcp_config *config, unsigned char *opt);
static void option_put(struct dhcp_packet *mess, unsigned char *end, int opt, int len, unsigned int val);
static void option_put_string(struct dhcp_packet *mess, unsigned char *end, 
			      int opt, const char *string, int null_term);
static struct in_addr option_addr(unsigned char *opt);
static unsigned int option_uint(unsigned char *opt, int offset, int size);
static void log_packet(char *type, void *addr, unsigned char *ext_mac, 
		       int mac_len, char *interface, char *string, char *err, u32 xid);
static unsigned char *option_find(struct dhcp_packet *mess, size_t size, int opt_type, int minsize);
static unsigned char *option_find1(unsigned char *p, unsigned char *end, int opt, int minsize);
static size_t dhcp_packet_size(struct dhcp_packet *mess, unsigned char *agent_id, unsigned char *real_end);
static void clear_packet(struct dhcp_packet *mess, unsigned char *end);
static int in_list(unsigned char *list, int opt);
static unsigned char *free_space(struct dhcp_packet *mess, unsigned char *end, int opt, int len);
static void do_options(struct dhcp_context *context,
		       struct dhcp_packet *mess,
		       unsigned char *end,
		       unsigned char *req_options,
		       char *hostname, 
		       char *domain,
		       struct dhcp_netid *netid,
		       struct in_addr subnet_addr, 
		       unsigned char fqdn_flags,
		       int null_term, int pxe_arch,
		       unsigned char *uuid,
		       int vendor_class_len,
		       time_t now,
		       unsigned int lease_time,
		       unsigned short fuzz,
		       const char *pxevendor,
		       int leasequery);


static void match_vendor_opts(unsigned char *opt, struct dhcp_opt *dopt); 
static int do_encap_opts(struct dhcp_opt *opt, int encap, int flag, struct dhcp_packet *mess, unsigned char *end, int null_term);
static void pxe_misc(struct dhcp_packet *mess, unsigned char *end, unsigned char *uuid, const char *pxevendor);
static int prune_vendor_opts(struct dhcp_netid *netid);
static struct dhcp_opt *pxe_opts(int pxe_arch, struct dhcp_netid *netid, struct in_addr local, time_t now);
struct dhcp_boot *find_boot(struct dhcp_netid *netid);
static int pxe_uefi_workaround(int pxe_arch, struct dhcp_netid *netid, struct dhcp_packet *mess, struct in_addr local, time_t now, int pxe);
static void apply_delay(u32 xid, time_t recvtime, struct dhcp_netid *netid);
static int is_pxe_client(struct dhcp_packet *mess, size_t sz, const char **pxe_vendor);
static int do_opt(struct dhcp_opt *opt, unsigned char *p, struct dhcp_context *context, int null_term);
static void handle_encap(struct dhcp_packet *mess, unsigned char *end, unsigned char *req_options, int null_term, struct dhcp_netid *tagif, int pxemode);

size_t dhcp_reply(struct dhcp_context *context, char *iface_name, int int_index,
		  size_t sz, time_t now, int unicast_dest, int loopback,
		  int *is_inform, int pxe, struct in_addr fallback, time_t recvtime, struct in_addr leasequery_source)
{
  unsigned char *opt, *clid = NULL;
  struct dhcp_lease *ltmp, *lease = NULL;
  struct dhcp_vendor *vendor;
  struct dhcp_mac *mac;
  struct dhcp_netid_list *id_list;
  int clid_len = 0, ignore = 0, do_classes = 0, rapid_commit = 0, selecting = 0, pxearch = -1;
  const char *pxevendor = NULL;
  struct dhcp_packet *mess = (struct dhcp_packet *)daemon->dhcp_packet.iov_base;
  unsigned char *end = (unsigned char *)(mess + 1); 
  unsigned char *real_end = (unsigned char *)(mess + 1); 
  char *hostname = NULL, *offer_hostname = NULL, *client_hostname = NULL, *domain = NULL;
  int hostname_auth = 0, borken_opt = 0;
  unsigned char *req_options = NULL;
  char *message = NULL;
  unsigned int time;
  struct dhcp_config *config;
  struct dhcp_netid *netid, *tagif_netid;
  struct in_addr subnet_addr, override;
  unsigned short fuzz = 0;
  unsigned int mess_type = 0;
  unsigned char fqdn_flags = 0;
  unsigned char *agent_id = NULL, *uuid = NULL;
  unsigned char *emac = NULL;
  int vendor_class_len = 0, emac_len = 0;
  struct dhcp_netid known_id, iface_id, cpewan_id;
  struct dhcp_opt *o;
  unsigned char pxe_uuid[17];
  unsigned char *oui = NULL, *serial = NULL;
#ifdef HAVE_SCRIPT
  unsigned char *class = NULL;
#endif

  subnet_addr.s_addr = override.s_addr = 0;
          
  /* set tag with name == interface */
  iface_id.net = iface_name;
  iface_id.next = NULL;
  netid = &iface_id; 
  
  if (mess->op != BOOTREQUEST || mess->hlen > DHCP_CHADDR_MAX)
    return 0;
   
  if (mess->htype == 0 && mess->hlen != 0)
    return 0;

  /* check for DHCP rather than BOOTP */
  if ((opt = option_find(mess, sz, OPTION_MESSAGE_TYPE, 1)))
    {
      u32 cookie = htonl(DHCP_COOKIE);
      
      /* only insist on a cookie for DHCP. */
      if (memcmp(mess->options, &cookie, sizeof(u32)) != 0)
	return 0;
      
      mess_type = option_uint(opt, 0, 1);
      
      /* two things to note here: expand_buf may move the packet,
	 so reassign mess from daemon->packet. Also, the size
	 sent includes the IP and UDP headers, hence the magic "-28" */
      if ((opt = option_find(mess, sz, OPTION_MAXMESSAGE, 2)))
	{
	  size_t size = (size_t)option_uint(opt, 0, 2) - 28;
	  
	  if (size > DHCP_PACKET_MAX)
	    size = DHCP_PACKET_MAX;
	  else if (size < sizeof(struct dhcp_packet))
	    size = sizeof(struct dhcp_packet);
	  
	  if (expand_buf(&daemon->dhcp_packet, size))
	    {
	      mess = (struct dhcp_packet *)daemon->dhcp_packet.iov_base;
	      real_end = end = ((unsigned char *)mess) + size;
	    }
	}

      /* Some buggy clients set ciaddr when they shouldn't, so clear that here since
	 it can affect the context-determination code. */
      if ((option_find(mess, sz, OPTION_REQUESTED_IP, INADDRSZ) || mess_type == DHCPDISCOVER))
	mess->ciaddr.s_addr = 0;

      /* search for device identity from CPEWAN devices, we pass this through to the script */
      if ((opt = option_find(mess, sz, OPTION_VENDOR_IDENT_OPT, 5)))
	{
	  unsigned  int elen, offset, len = option_len(opt);
	  
	  for (offset = 0; offset < (len - 5); offset += elen + 5)
	    {
	      elen = option_uint(opt, offset + 4 , 1);
	      if (option_uint(opt, offset, 4) == BRDBAND_FORUM_IANA && offset + elen + 5 <= len)
		{
		  unsigned char *x = option_ptr(opt, offset + 5);
		  unsigned char *y = option_ptr(opt, offset + elen + 5);
		  oui = option_find1(x, y, 1, 1);
		  serial = option_find1(x, y, 2, 1);
#ifdef HAVE_SCRIPT
		  class = option_find1(x, y, 3, 1);		  
#endif
		  /* If TR069-id is present set the tag "cpewan-id" to facilitate echoing 
		     the gateway id back. Note that the device class is optional */
		  if (oui && serial)
		    {
		      cpewan_id.net = "cpewan-id";
		      cpewan_id.next = netid;
		      netid = &cpewan_id;
		    }
		  break;
		}
	    }
	}
      
      if (mess_type != DHCPLEASEQUERY && (opt = option_find(mess, sz, OPTION_AGENT_ID, 1)))
	{
	  /* Any agent-id needs to be copied back out, verbatim, as the last option
	     in the packet. Here, we shift it to the very end of the buffer, if it doesn't
	     get overwritten, then it will be shuffled back at the end of processing.
	     Note that the incoming options must not be overwritten here, so there has to 
	     be enough free space at the end of the packet to copy the option. */
	  unsigned char *sopt;
	  unsigned int total = option_len(opt) + 2;
	  unsigned char *last_opt = option_find1(&mess->options[0] + sizeof(u32), ((unsigned char *)mess) + sz,
						 OPTION_END, 0);
	  if (last_opt && last_opt < end - total)
	    {
	      end -= total;
	      agent_id = end;
	      memcpy(agent_id, opt, total);
	    }

	  /* look for RFC5010 flags sub-option */
	  if ((sopt = option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_FLAGS, INADDRSZ)))
	    unicast_dest = !!(option_uint(opt, 0, 1) & 0x80);
	      
	  /* look for RFC3527 Link selection sub-option */
	  if ((sopt = option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_SUBNET_SELECT, INADDRSZ)))
	    subnet_addr = option_addr(sopt);

	  /* look for RFC5107 server-identifier-override */
	  if ((sopt = option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_SERVER_OR, INADDRSZ)))
	    override = option_addr(sopt);
	  
	  /* if a circuit-id or remote-is option is provided, exact-match to options. */
	  for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
	    {
	      int search;
	      
	      if (vendor->match_type == MATCH_CIRCUIT)
		search = SUBOPT_CIRCUIT_ID;
	      else if (vendor->match_type == MATCH_REMOTE)
		search = SUBOPT_REMOTE_ID;
	      else if (vendor->match_type == MATCH_SUBSCRIBER)
		search = SUBOPT_SUBSCR_ID;
	      else 
		continue;
	      
	      if ((sopt = option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), search, 1)) &&
		  vendor->len == option_len(sopt) &&
		  memcmp(option_ptr(sopt, 0), vendor->data, vendor->len) == 0)
		{
		  vendor->netid.next = netid;
		  netid = &vendor->netid;
		} 
	    }
	}
      
      /* Check for RFC3011 subnet selector - only if RFC3527 one not present */
      if (subnet_addr.s_addr == 0 && (opt = option_find(mess, sz, OPTION_SUBNET_SELECT, INADDRSZ)))
	subnet_addr = option_addr(opt);
      
      /* If there is no client identifier option, use the hardware address */
      if (!option_bool(OPT_IGNORE_CLID) && (opt = option_find(mess, sz, OPTION_CLIENT_ID, 1)))
	{
	  clid_len = option_len(opt);
	  clid = option_ptr(opt, 0);
	}

      /* do we have a lease in store? */
      lease = lease_find_by_client(mess->chaddr, mess->hlen, mess->htype, clid, clid_len);

      /* If this request is missing a clid, but we've seen one before, 
	 use it again for option matching etc. */
      if (lease && !clid && lease->clid)
	{
	  clid_len = lease->clid_len;
	  clid = lease->clid;
	}

      /* find mac to use for logging and hashing */
      emac = extended_hwaddr(mess->htype, mess->hlen, mess->chaddr, clid_len, clid, &emac_len);
    }
  
  for (mac = daemon->dhcp_macs; mac; mac = mac->next)
    if (mac->hwaddr_len == mess->hlen &&
	(mac->hwaddr_type == mess->htype || mac->hwaddr_type == 0) &&
	memcmp_masked(mac->hwaddr, mess->chaddr, mess->hlen, mac->mask))
      {
	mac->netid.next = netid;
	netid = &mac->netid;
      }
  
  /* Determine network for this packet. Our caller will have already linked all the 
     contexts which match the addresses of the receiving interface but if the 
     machine has an address already, or came via a relay, or we have a subnet selector, 
     we search again. If we don't have have a giaddr or explicit subnet selector, 
     use the ciaddr. This is necessary because a  machine which got a lease via a 
     relay won't use the relay to renew. If matching a ciaddr fails but we have a context 
     from the physical network, continue using that to allow correct DHCPNAK generation later. */
  if (mess->giaddr.s_addr || subnet_addr.s_addr || mess->ciaddr.s_addr)
    {
      struct dhcp_context *context_tmp, *context_new = NULL;
      struct shared_network *share = NULL;
      struct in_addr addr;
      int force = 0, via_relay = 0;
      
      if (subnet_addr.s_addr)
	{
	  addr = subnet_addr;
	  force = 1;
	  if (mess->giaddr.s_addr)
	    via_relay = 1;
	}
      else if (mess->giaddr.s_addr)
	{
	  addr = mess->giaddr;
	  force = 1;
	  via_relay = 1;
	}
      else
	{
	  /* If ciaddr is in the hardware derived set of contexts, leave that unchanged */
	  addr = mess->ciaddr;
	  for (context_tmp = context; context_tmp; context_tmp = context_tmp->current)
	    if (context_tmp->netmask.s_addr && 
		is_same_net(addr, context_tmp->start, context_tmp->netmask) &&
		is_same_net(addr, context_tmp->end, context_tmp->netmask))
	      {
		context_new = context;
		break;
	      }
	} 
		
      if (!context_new)
	{
	  for (context_tmp = daemon->dhcp; context_tmp; context_tmp = context_tmp->next)
	    {
	      struct in_addr netmask = context_tmp->netmask;
	      
	      /* guess the netmask for relayed networks */
	      if (!(context_tmp->flags & CONTEXT_NETMASK) && context_tmp->netmask.s_addr == 0)
		{
		  if (IN_CLASSA(ntohl(context_tmp->start.s_addr)) && IN_CLASSA(ntohl(context_tmp->end.s_addr)))
		    netmask.s_addr = htonl(0xff000000);
		  else if (IN_CLASSB(ntohl(context_tmp->start.s_addr)) && IN_CLASSB(ntohl(context_tmp->end.s_addr)))
		    netmask.s_addr = htonl(0xffff0000);
		  else if (IN_CLASSC(ntohl(context_tmp->start.s_addr)) && IN_CLASSC(ntohl(context_tmp->end.s_addr)))
		    netmask.s_addr = htonl(0xffffff00); 
		}

	      /* check to see is a context is OK because of a shared address on
		 the relayed subnet. */
	      if (via_relay)
		for (share = daemon->shared_networks; share; share = share->next)
		  {
#ifdef HAVE_DHCP6
		    if (share->shared_addr.s_addr == 0)
		      continue;
#endif
		    if (share->if_index != 0 ||
			share->match_addr.s_addr != mess->giaddr.s_addr)
		      continue;
		    
		    if (netmask.s_addr != 0  && 
			is_same_net(share->shared_addr, context_tmp->start, netmask) &&
			is_same_net(share->shared_addr, context_tmp->end, netmask))
		      break;
		  }
	      
	      /* This section fills in context mainly when a client which is on a remote (relayed)
		 network renews a lease without using the relay, after dnsmasq has restarted. */
	      if (share ||
		  (netmask.s_addr != 0  && 
		   is_same_net(addr, context_tmp->start, netmask) &&
		   is_same_net(addr, context_tmp->end, netmask)))
		{
		  context_tmp->netmask = netmask;
		  if (context_tmp->local.s_addr == 0)
		    context_tmp->local = fallback;
		  if (context_tmp->router.s_addr == 0 && !share)
		    {
		      if (override.s_addr)
			context_tmp->router = override;
		      else
			context_tmp->router = mess->giaddr;
		    }
		  
		  /* fill in missing broadcast addresses for relayed ranges */
		  if (!(context_tmp->flags & CONTEXT_BRDCAST) && context_tmp->broadcast.s_addr == 0 )
		    context_tmp->broadcast.s_addr = context_tmp->start.s_addr | ~context_tmp->netmask.s_addr;
		  
		  context_tmp->current = context_new;
		  context_new = context_tmp;
		}
	      
	    }
	}
	  
      if (context_new || force)
	context = context_new; 
    }
  
  if (mess_type != DHCPLEASEQUERY)
    {
      if  (!context)
	{
	  const char *via;
	  if (subnet_addr.s_addr)
	    {
	      via = _("with subnet selector");
	      inet_ntop(AF_INET, &subnet_addr, daemon->addrbuff, ADDRSTRLEN);
	    }
	  else
	    {
	      via = _("via");
	      if (mess->giaddr.s_addr)
		inet_ntop(AF_INET, &mess->giaddr, daemon->addrbuff, ADDRSTRLEN);
	      else
		safe_strncpy(daemon->addrbuff, iface_name, ADDRSTRLEN);
	    }
	  my_syslog(MS_DHCP | LOG_WARNING, _("no address range available for DHCP request %s %s"),
		    via, daemon->addrbuff);
	  return 0;
	}
      
      if (option_bool(OPT_LOG_OPTS))
	{
	  struct dhcp_context *context_tmp;
	  for (context_tmp = context; context_tmp; context_tmp = context_tmp->current)
	    {
	      inet_ntop(AF_INET, &context_tmp->start, daemon->namebuff, MAXDNAME);
	      if (context_tmp->flags & (CONTEXT_STATIC | CONTEXT_PROXY))
		{
		  inet_ntop(AF_INET, &context_tmp->netmask, daemon->addrbuff, ADDRSTRLEN);
		  my_syslog(MS_DHCP | LOG_INFO, _("%u available DHCP subnet: %s/%s"),
			    ntohl(mess->xid), daemon->namebuff, daemon->addrbuff);
		}
	      else
		{
		  inet_ntop(AF_INET, &context_tmp->end, daemon->addrbuff, ADDRSTRLEN);
		  my_syslog(MS_DHCP | LOG_INFO, _("%u available DHCP range: %s -- %s"),
			    ntohl(mess->xid), daemon->namebuff, daemon->addrbuff);
		}
	    }
	}
    }
  
  /* dhcp-match. If we have hex-and-wildcards, look for a left-anchored match.
     Otherwise assume the option is an array, and look for a matching element. 
     If no data given, existence of the option is enough. This code handles 
     rfc3925 V-I classes too. */
  for (o = daemon->dhcp_match; o; o = o->next)
    {
      unsigned int len, elen, match = 0;
      size_t offset, o2;

      if (o->flags & DHOPT_RFC3925)
	{
	  if (!(opt = option_find(mess, sz, OPTION_VENDOR_IDENT, 5)))
	    continue;
	  
	  for (offset = 0; offset < (option_len(opt) - 5u); offset += len + 5)
	    {
	      len = option_uint(opt, offset + 4 , 1);
	      /* Need to take care that bad data can't run us off the end of the packet */
	      if ((offset + len + 5 <= (unsigned)(option_len(opt))) &&
		  (option_uint(opt, offset, 4) == (unsigned int)o->u.encap))
		for (o2 = offset + 5; o2 < offset + len + 5; o2 += elen + 1)
		  { 
		    elen = option_uint(opt, o2, 1);
		    if ((o2 + elen + 1 <= (unsigned)option_len(opt)) &&
			(match = match_bytes(o, option_ptr(opt, o2 + 1), elen)))
		      break;
		  }
	      if (match) 
		break;
	    }	  
	}
      else
	{
	  if (!(opt = option_find(mess, sz, o->opt, 1)))
	    continue;
	  
	  match = match_bytes(o, option_ptr(opt, 0), option_len(opt));
	} 

      if (match)
	{
	  o->netid->next = netid;
	  netid = o->netid;
	}
    }
	
  /* user-class options are, according to RFC3004, supposed to contain
     a set of counted strings. Here we check that this is so (by seeing
     if the counts are consistent with the overall option length) and if
     so zero the counts so that we don't get spurious matches between 
     the vendor string and the counts. If the lengths don't add up, we
     assume that the option is a single string and non RFC3004 compliant 
     and just do the substring match. dhclient provides these broken options.
     The code, later, which sends user-class data to the lease-change script
     relies on the transformation done here.
  */

  if ((opt = option_find(mess, sz, OPTION_USER_CLASS, 1)))
    {
      unsigned char *ucp = option_ptr(opt, 0);
      int tmp, j;
      for (j = 0; j < option_len(opt); j += ucp[j] + 1);
      if (j == option_len(opt))
	for (j = 0; j < option_len(opt); j = tmp)
	  {
	    tmp = j + ucp[j] + 1;
	    ucp[j] = 0;
	  }
    }
    
  for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
    {
      int mopt;
      
      if (vendor->match_type == MATCH_VENDOR)
	mopt = OPTION_VENDOR_ID;
      else if (vendor->match_type == MATCH_USER)
	mopt = OPTION_USER_CLASS; 
      else
	continue;

      if ((opt = option_find(mess, sz, mopt, 1)))
	{
	  int i;
	  for (i = 0; i <= (option_len(opt) - vendor->len); i++)
	    if (memcmp(vendor->data, option_ptr(opt, i), vendor->len) == 0)
	      {
		vendor->netid.next = netid;
		netid = &vendor->netid;
		break;
	      }
	}
    }

  /* mark vendor-encapsulated options which match the client-supplied vendor class,
     save client-supplied vendor class */
  if ((opt = option_find(mess, sz, OPTION_VENDOR_ID, 1)))
    {
      memcpy(daemon->dhcp_buff3, option_ptr(opt, 0), option_len(opt));
      vendor_class_len = option_len(opt);
    }
  match_vendor_opts(opt, daemon->dhcp_opts);
  
  if (option_bool(OPT_LOG_OPTS))
    {
      if (sanitise(opt, daemon->namebuff))
	my_syslog(MS_DHCP | LOG_INFO, _("%u vendor class: %s"), ntohl(mess->xid), daemon->namebuff);
      if (sanitise(option_find(mess, sz, OPTION_USER_CLASS, 1), daemon->namebuff))
	my_syslog(MS_DHCP | LOG_INFO, _("%u user class: %s"), ntohl(mess->xid), daemon->namebuff);
    }

  mess->op = BOOTREPLY;
  
  config = find_config(daemon->dhcp_conf, context, clid, clid_len, 
		       mess->chaddr, mess->hlen, mess->htype, NULL, run_tag_if(netid));

  /* set "known" tag for known hosts */
  if (config)
    {
      known_id.net = "known";
      known_id.next = netid;
      netid = &known_id;
    }
  else if (find_config(daemon->dhcp_conf, NULL, clid, clid_len, 
		       mess->chaddr, mess->hlen, mess->htype, NULL, run_tag_if(netid)))
    {
      known_id.net = "known-othernet";
      known_id.next = netid;
      netid = &known_id;
    }
  
  if (mess_type == 0 && !pxe)
    {
      /* BOOTP request */
      struct dhcp_netid id, bootp_id;
      struct in_addr *logaddr = NULL;

      /* must have a MAC addr for bootp */
      if (mess->htype == 0 || mess->hlen == 0 || (context->flags & CONTEXT_PROXY))
	return 0;
      
      if (have_config(config, CONFIG_DISABLE))
	message = _("disabled");

      end = mess->options + 64; /* BOOTP vend area is only 64 bytes */
            
      if (have_config(config, CONFIG_NAME))
	{
	  hostname = config->hostname;
	  domain = config->domain;
	}

      if (config)
	{
	  struct dhcp_netid_list *list;

	  for (list = config->netid; list; list = list->next)
	    {
	      list->list->next = netid;
	      netid = list->list;
	    }
	}

      /* Match incoming filename field as a netid. */
      if (mess->file[0])
	{
	  memcpy(daemon->dhcp_buff2, mess->file, sizeof(mess->file));
	  daemon->dhcp_buff2[sizeof(mess->file) + 1] = 0; /* ensure zero term. */
	  id.net = (char *)daemon->dhcp_buff2;
	  id.next = netid;
	  netid = &id;
	}

      /* Add "bootp" as a tag to allow different options, address ranges etc
	 for BOOTP clients */
      bootp_id.net = "bootp";
      bootp_id.next = netid;
      netid = &bootp_id;
      
      tagif_netid = run_tag_if(netid);

      for (id_list = daemon->dhcp_ignore; id_list; id_list = id_list->next)
	if (match_netid(id_list->list, tagif_netid, 0))
	  message = _("ignored");
      
      if (!message)
	{
	  int nailed = 0;

	  if (have_config(config, CONFIG_ADDR))
	    {
	      nailed = 1;
	      logaddr = &config->addr;
	      mess->yiaddr = config->addr;
	      if ((lease = lease_find_by_addr(config->addr)) &&
		  (lease->hwaddr_len != mess->hlen ||
		   lease->hwaddr_type != mess->htype ||
		   memcmp(lease->hwaddr, mess->chaddr, lease->hwaddr_len) != 0))
		message = _("address in use");
	    }
	  else
	    {
	      if (!(lease = lease_find_by_client(mess->chaddr, mess->hlen, mess->htype, NULL, 0)) ||
		  !address_available(context, lease->addr, tagif_netid))
		{
		   if (lease)
		     {
		       /* lease exists, wrong network. */
		       lease_prune(lease, now);
		       lease = NULL;
		     }
		   if (!address_allocate(context, &mess->yiaddr, mess->chaddr, mess->hlen, tagif_netid, now, loopback))
		     message = _("no address available");
		}
	      else
		mess->yiaddr = lease->addr;
	    }
	  
	  if (!message && !(context = narrow_context(context, mess->yiaddr, netid)))
	    message = _("wrong network");
	  else if (context->netid.net)
	    {
	      context->netid.next = netid;
	      tagif_netid = run_tag_if(&context->netid);
	    }

	  log_tags(tagif_netid, ntohl(mess->xid));
	    
	  if (!message && !nailed)
	    {
	      for (id_list = daemon->bootp_dynamic; id_list; id_list = id_list->next)
		if ((!id_list->list) || match_netid(id_list->list, tagif_netid, 0))
		  break;
	      if (!id_list)
		message = _("no address configured");
	    }

	  if (!message && 
	      !lease && 
	      (!(lease = lease4_allocate(mess->yiaddr))))
	    message = _("no leases left");
	  
	  if (!message)
	    {
	      logaddr = &mess->yiaddr;
		
	      lease_set_hwaddr(lease, mess->chaddr, NULL, mess->hlen, mess->htype, 0, now, 1);
	      if (hostname)
		lease_set_hostname(lease, hostname, 1, get_domain(lease->addr), domain); 
	      /* infinite lease unless nailed in dhcp-host line. */
	      lease_set_expires(lease,  
				have_config(config, CONFIG_TIME) ? config->lease_time : 0xffffffff, 
				now); 
	      lease_set_interface(lease, int_index, now);
	      
	      clear_packet(mess, end);
	      do_options(context, mess, end, NULL, hostname, get_domain(mess->yiaddr), 
			 netid, subnet_addr, 0, 0, -1, NULL, vendor_class_len, now, 0xffffffff, 0, NULL, 0);
	    }
	}
      
      daemon->metrics[METRIC_BOOTP]++;
      log_packet("BOOTP", logaddr, mess->chaddr, mess->hlen, iface_name, NULL, message, mess->xid);
      
      return message ? 0 : dhcp_packet_size(mess, agent_id, real_end);
    }
      
  if ((opt = option_find(mess, sz, OPTION_CLIENT_FQDN, 3)))
    {
      /* http://tools.ietf.org/wg/dhc/draft-ietf-dhc-fqdn-option/draft-ietf-dhc-fqdn-option-10.txt */
      int len = option_len(opt);
      char *pq = daemon->dhcp_buff;
      unsigned char *pp, *op = option_ptr(opt, 0);
      
      fqdn_flags = *op;
      len -= 3;
      op += 3;
      pp = op;
      
      /* NB, the following always sets at least one bit */
      if (option_bool(OPT_FQDN_UPDATE))
	{
	  if (fqdn_flags & 0x01)
	    {
	      fqdn_flags |= 0x02; /* set O */
	      fqdn_flags &= ~0x01; /* clear S */
	    }
	  fqdn_flags |= 0x08; /* set N */
	}
      else 
	{
	  if (!(fqdn_flags & 0x01))
	    fqdn_flags |= 0x03; /* set S and O */
	  fqdn_flags &= ~0x08; /* clear N */
	}
      
      if (fqdn_flags & 0x04)
	while (*op != 0 && ((op + (*op)) - pp) < len)
	  {
	    memcpy(pq, op+1, *op);
	    pq += *op;
	    op += (*op)+1;
	    *(pq++) = '.';
	  }
      else
	{
	  memcpy(pq, op, len);
	  if (len > 0 && op[len-1] == 0)
	    borken_opt = 1;
	  pq += len + 1;
	}
      
      if (pq != daemon->dhcp_buff)
	pq--;
      
      *pq = 0;
      
      if (legal_hostname(daemon->dhcp_buff))
	offer_hostname = client_hostname = daemon->dhcp_buff;
    }
  else if ((opt = option_find(mess, sz, OPTION_HOSTNAME, 1)))
    {
      int len = option_len(opt);
      memcpy(daemon->dhcp_buff, option_ptr(opt, 0), len);
      /* Microsoft clients are broken, and need zero-terminated strings
	 in options. We detect this state here, and do the same in
	 any options we send */
      if (len > 0 && daemon->dhcp_buff[len-1] == 0)
	borken_opt = 1;
      else
	daemon->dhcp_buff[len] = 0;
      if (legal_hostname(daemon->dhcp_buff))
	client_hostname = daemon->dhcp_buff;
    }

  if (client_hostname)
    {
      struct dhcp_match_name *m;
      size_t nl = strlen(client_hostname);
      
      if (option_bool(OPT_LOG_OPTS))
	my_syslog(MS_DHCP | LOG_INFO, _("%u client provides name: %s"), ntohl(mess->xid), client_hostname);
      for (m = daemon->dhcp_name_match; m; m = m->next)
	{
	  size_t ml = strlen(m->name);
	  char save = 0;
	  
	  if (nl < ml)
	    continue;
	  if (nl > ml)
	    {
	      save = client_hostname[ml];
	      client_hostname[ml] = 0;
	    }
	  
	  if (hostname_isequal(client_hostname, m->name) &&
	      (save == 0 || m->wildcard))
	    {
	      m->netid->next = netid;
	      netid = m->netid;
	    }
	  
	  if (save != 0)
	    client_hostname[ml] = save;
	}
    }
  
  if (have_config(config, CONFIG_NAME))
    {
      hostname = config->hostname;
      domain = config->domain;
      hostname_auth = 1;
      /* be careful not to send an OFFER with a hostname not matching the DISCOVER. */
      if (fqdn_flags != 0 || !client_hostname || hostname_isequal(hostname, client_hostname))
        offer_hostname = hostname;
    }
  else if (client_hostname)
    {
      domain = strip_hostname(client_hostname);
      
      if (strlen(client_hostname) != 0)
	{
	  hostname = client_hostname;
	  
	  if (!config)
	    {
	      /* Search again now we have a hostname. 
		 Only accept configs without CLID and HWADDR here, (they won't match)
		 to avoid impersonation by name. */
	      struct dhcp_config *new = find_config(daemon->dhcp_conf, context, NULL, 0,
						    mess->chaddr, mess->hlen, 
						    mess->htype, hostname, run_tag_if(netid));
	      if (new && !have_config(new, CONFIG_CLID) && !new->hwaddr)
		{
		  config = new;
		  /* set "known" tag for known hosts */
		  known_id.net = "known";
		  known_id.next = netid;
		  netid = &known_id;
		}
	    }
	}
    }

  if (mess_type != DHCPLEASEQUERY && config)
    {
      struct dhcp_netid_list *list;
      
      for (list = config->netid; list; list = list->next)
	{
	  list->list->next = netid;
	  netid = list->list;
	}
    }
  
  tagif_netid = run_tag_if(netid);
  
  /* if all the netids in the ignore list are present, ignore this client */
  for (id_list = daemon->dhcp_ignore; id_list; id_list = id_list->next)
    if (match_netid(id_list->list, tagif_netid, 0))
      ignore = 1;

  /* If configured, we can override the server-id to be the address of the relay, 
     so that all traffic goes via the relay and can pick up agent-id info. This can be
     configured for all relays, or by address. */
  if (daemon->override && mess->giaddr.s_addr != 0 && override.s_addr == 0)
    {
      if (!daemon->override_relays)
	override = mess->giaddr;
      else
	{
	  struct addr_list *l;
	  for (l = daemon->override_relays; l; l = l->next)
	    if (l->addr.s_addr == mess->giaddr.s_addr)
	      break;
	  if (l)
	    override = mess->giaddr;
	}
    }

  /* Can have setting to ignore the client ID for a particular MAC address or hostname */
  if (have_config(config, CONFIG_NOCLID))
    clid = NULL;
          
  /* Check if client is PXE client. */
  if (mess_type != DHCPLEASEQUERY &&
      daemon->enable_pxe &&
      is_pxe_client(mess, sz, &pxevendor))
    {
      if ((opt = option_find(mess, sz, OPTION_PXE_UUID, 17)))
	{
	  memcpy(pxe_uuid, option_ptr(opt, 0), 17);
	  uuid = pxe_uuid;
	}

      /* Check if this is really a PXE bootserver request, and handle specially if so. */
      if ((mess_type == DHCPREQUEST || mess_type == DHCPINFORM) &&
	  (opt = option_find(mess, sz, OPTION_VENDOR_CLASS_OPT, 1)) &&
	  (opt = option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_PXE_BOOT_ITEM, 4)))
	{
	  struct pxe_service *service;
	  int type = option_uint(opt, 0, 2);
	  int layer = option_uint(opt, 2, 2);
	  unsigned char save71[4];
	  struct dhcp_opt opt71;

	  if (ignore)
	    return 0;

	  if (layer & 0x8000)
	    {
	      my_syslog(MS_DHCP | LOG_ERR, _("PXE BIS not supported"));
	      return 0;
	    }

	  memcpy(save71, option_ptr(opt, 0), 4);
	  
	  for (service = daemon->pxe_services; service; service = service->next)
	    if (service->type == type)
	      break;
	  
	  for (; context; context = context->current)
	    if (match_netid(context->filter, tagif_netid, 1) &&
		is_same_net(mess->ciaddr, context->start, context->netmask))
	      break;
	  
	  if (!service || !service->basename || !context)
	    return 0;
	  	  
	  clear_packet(mess, end);
	  
	  mess->yiaddr = mess->ciaddr;
	  mess->ciaddr.s_addr = 0;
	  if (service->sname)
	    mess->siaddr = a_record_from_hosts(service->sname, now);
	  else if (service->server.s_addr != 0)
	    mess->siaddr = service->server; 
	  else
	    mess->siaddr = context->local; 
	  
	  if (strchr(service->basename, '.'))
	    snprintf((char *)mess->file, sizeof(mess->file),
		"%s", service->basename);
	  else
	    snprintf((char *)mess->file, sizeof(mess->file),
		"%s.%d", service->basename, layer);
	  
	  option_put(mess, end, OPTION_MESSAGE_TYPE, 1, DHCPACK);
	  option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, htonl(context->local.s_addr));
	  pxe_misc(mess, end, uuid, pxevendor);
	  
	  prune_vendor_opts(tagif_netid);
	  opt71.val = save71;
	  opt71.opt = SUBOPT_PXE_BOOT_ITEM;
	  opt71.len = 4;
	  opt71.flags = DHOPT_VENDOR_MATCH;
	  opt71.netid = NULL;
	  opt71.next = daemon->dhcp_opts;
	  do_encap_opts(&opt71, OPTION_VENDOR_CLASS_OPT, DHOPT_VENDOR_MATCH, mess, end, 0);
	  
	  daemon->metrics[METRIC_PXE]++;
	  log_packet("PXE", &mess->yiaddr, emac, emac_len, iface_name, (char *)mess->file, NULL, mess->xid);
	  log_tags(tagif_netid, ntohl(mess->xid));
	  return dhcp_packet_size(mess, agent_id, real_end);	  
	}
      
      if ((opt = option_find(mess, sz, OPTION_ARCH, 2)))
	{
	  pxearch = option_uint(opt, 0, 2);

	  /* proxy DHCP here. */
	  if ((mess_type == DHCPDISCOVER || (pxe && mess_type == DHCPREQUEST)))
	    {
	      struct dhcp_context *tmp;
	      int workaround = 0;
	      
	      for (tmp = context; tmp; tmp = tmp->current)
		if ((tmp->flags & CONTEXT_PROXY) &&
		    match_netid(tmp->filter, tagif_netid, 1))
		  break;
	      
	      if (tmp)
		{
		  struct dhcp_boot *boot;
		  int redirect4011 = 0;
		  struct dhcp_opt *option;

		  /* OK only dhcp-option-pxe options. */
		  tagif_netid = option_filter(netid, tmp->netid.net ? &tmp->netid : NULL, daemon->dhcp_opts, 2);
		  boot = find_boot(tagif_netid);
		  
		  mess->yiaddr.s_addr = 0;
		  if  (mess_type == DHCPDISCOVER || mess->ciaddr.s_addr == 0)
		    {
		      mess->ciaddr.s_addr = 0;
		      mess->flags |= htons(0x8000); /* broadcast */
		    }
		  
		  clear_packet(mess, end);
		  
		  /* Redirect EFI clients to port 4011 */
		  if (pxearch >= 6)
		    {
		      redirect4011 = 1;
		      mess->siaddr = tmp->local;
		    }
		  
		  /* Returns true if only one matching service is available. On port 4011, 
		     it also inserts the boot file and server name. */
		  workaround = pxe_uefi_workaround(pxearch, tagif_netid, mess, tmp->local, now, pxe);
		  
		  if (!workaround && boot)
		    {
		      /* Provide the bootfile here, for iPXE, and in case we have no menu items
			 and set discovery_control = 8 */
		      if (boot->next_server.s_addr) 
			mess->siaddr = boot->next_server;
		      else if (boot->tftp_sname) 
			mess->siaddr = a_record_from_hosts(boot->tftp_sname, now);
		      
		      if (boot->file)
			safe_strncpy((char *)mess->file, boot->file, sizeof(mess->file));
		    }
		  
		  option_put(mess, end, OPTION_MESSAGE_TYPE, 1, 
			     mess_type == DHCPDISCOVER ? DHCPOFFER : DHCPACK);
		  option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, htonl(tmp->local.s_addr));
		  pxe_misc(mess, end, uuid, pxevendor);
		  prune_vendor_opts(tagif_netid);
		  if ((pxe && !workaround) || !redirect4011)
		    do_encap_opts(pxe_opts(pxearch, tagif_netid, tmp->local, now), OPTION_VENDOR_CLASS_OPT, DHOPT_VENDOR_MATCH, mess, end, 0);

		  /* dhcp-option-pxe ONLY */
		  for (option = daemon->dhcp_opts; option; option = option->next)
		    {
		      int len;
		      unsigned char *p;
		      
		      if (!(option->flags & DHOPT_TAGOK))
			continue;
		      
		      len = do_opt(option, NULL, tmp, borken_opt);

		      if ((p = free_space(mess, end, option->opt, len)))
			do_opt(option, p, tmp, borken_opt);
		    }

		  handle_encap(mess, end, req_options, borken_opt, tagif_netid, 2);
		  
		  daemon->metrics[METRIC_PXE]++;
		  log_packet("PXE", NULL, emac, emac_len, iface_name, ignore ? "proxy-ignored" : "proxy", NULL, mess->xid);
		  log_tags(tagif_netid, ntohl(mess->xid));
		  if (!ignore)
		    apply_delay(mess->xid, recvtime, tagif_netid);
		  return ignore ? 0 : dhcp_packet_size(mess, agent_id, real_end);	  
		}
	    }
	}
    }

  /* if we're just a proxy server, go no further */
  if (mess_type != DHCPLEASEQUERY &&
      ((context->flags & CONTEXT_PROXY) || pxe))
    return 0;
  
  if ((opt = option_find(mess, sz, OPTION_REQUESTED_OPTIONS, 0)))
    {
      req_options = (unsigned char *)daemon->dhcp_buff2;
      memcpy(req_options, option_ptr(opt, 0), option_len(opt));
      req_options[option_len(opt)] = OPTION_END;
    }
  
  switch (mess_type)
    {
    case DHCPLEASEQUERY:
      mess_type = DHCPLEASEUNKNOWN;
      
      if (!option_bool(OPT_LEASEQUERY))
	return 0;
      
      if (leasequery_source.s_addr == 0)
	return 0;

      inet_ntop(AF_INET, &leasequery_source, daemon->workspacename, ADDRSTRLEN);

      if (daemon->leasequery_addr)
	{
	  struct bogus_addr *baddrp;

	  for (baddrp = daemon->leasequery_addr; baddrp; baddrp = baddrp->next)
	    if (!baddrp->is6 && is_same_net_prefix(leasequery_source, baddrp->addr.addr4, baddrp->prefix))
	      break;
	  
	  if (!baddrp)
	    {
	      my_syslog(MS_DHCP | LOG_WARNING, _("leasequery from %s not permitted"), daemon->workspacename);
	      return 0;
	    }
	}
      
      daemon->metrics[METRIC_DHCPLEASEQUERY]++;
      log_packet("DHCPLEASEQUERY", mess->ciaddr.s_addr ? &mess->ciaddr : NULL, emac_len != 0 ? emac : NULL, emac_len,
		 iface_name, "from ", daemon->workspacename, mess->xid);

      /* Put all the contexts on the ->current list for the next stages. */
      for (context = daemon->dhcp; context; context = context->next)
	context->current = context->next;
      
      /* Have maybe already found the lease by MAC or clid. */
      if (mess->ciaddr.s_addr != 0 &&
	  !(lease = lease_find_by_addr(mess->ciaddr)) &&
	  address_available(daemon->dhcp, mess->ciaddr, tagif_netid))
	{
	  mess_type = DHCPLEASEUNASSIGNED;
	  daemon->metrics[METRIC_DHCPLEASEUNASSIGNED]++;
	}
      
      if (lease)
	{
	  /* RFC4388 para 6.4.2 */
	  if (lease->agent_id)
	    {
	      unsigned char *sopt ;
	      
	      for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
		{
		  int search;
		  
		  if (vendor->match_type == MATCH_CIRCUIT)
		    search = SUBOPT_CIRCUIT_ID;
		  else if (vendor->match_type == MATCH_REMOTE)
		    search = SUBOPT_REMOTE_ID;
		  else if (vendor->match_type == MATCH_SUBSCRIBER)
		    search = SUBOPT_SUBSCR_ID;
		  else 
		    continue;
		  
		  if ((sopt = option_find1(lease->agent_id, lease->agent_id + lease->agent_id_len, search, 1)) &&
		      vendor->len == option_len(sopt) &&
		      memcmp(option_ptr(sopt, 0), vendor->data, vendor->len) == 0)
		    {
		      vendor->netid.next = netid;
		      netid = &vendor->netid;
		    }
		}
	      
	      tagif_netid = run_tag_if(netid);
	    }
	  
	  /* Now find the context for this lease and config for this host. */
	  if ((context = narrow_context(daemon->dhcp, lease->addr, tagif_netid)))
	    {
	      if ((config = find_config(daemon->dhcp_conf, context, lease->clid, lease->clid_len, 
					lease->hwaddr, lease->hwaddr_len, lease->hwaddr_type, lease->hostname, tagif_netid)))
		{
		  struct dhcp_netid_list *list;
		  
		  for (list = config->netid; list; list = list->next)
		    {
		      list->list->next = netid;
		      netid = list->list;
		    }

		  tagif_netid = run_tag_if(netid);
		}
	      
	      if (context->netid.net)
		{
		  context->netid.next = netid;
		  tagif_netid = run_tag_if(&context->netid);
		}

	      log_tags(tagif_netid, ntohl(mess->xid));
	      emac = extended_hwaddr(lease->hwaddr_type, lease->hwaddr_len, lease->hwaddr, lease->clid_len, lease->clid, &emac_len);
	      mess_type = DHCPLEASEACTIVE;
	      daemon->metrics[METRIC_DHCPLEASEACTIVE]++;
	    }
	}
      
      log_packet(mess_type == DHCPLEASEACTIVE ? "DHCPLEASEACTIVE" : (mess_type == DHCPLEASEUNASSIGNED ? "DHCPLEASEUNASSIGNED" : "DHCPLEASEUNKNOWN"),
		 mess_type == DHCPLEASEACTIVE ? &lease->addr : (mess->ciaddr.s_addr != 0 ? &mess->ciaddr : NULL),
		 emac_len != 0 ? emac : NULL, emac_len,
		 iface_name, mess_type == DHCPLEASEACTIVE ? lease->hostname : NULL, NULL, mess->xid);
      
      clear_packet(mess, end);
      option_put(mess, end, OPTION_MESSAGE_TYPE, 1, mess_type);
      
      if (mess_type == DHCPLEASEUNKNOWN)
	{
	  daemon->metrics[METRIC_DHCPLEASEUNKNOWN]++;
	  mess->ciaddr.s_addr = 0;
	}
      
      if (mess_type == DHCPLEASEACTIVE)
	{
	  unsigned char *p;
	  
	  mess->ciaddr = lease->addr;
	  mess->hlen = lease->hwaddr_len;
	  mess->htype = lease->hwaddr_type;
	  memcpy(mess->chaddr, lease->hwaddr, lease->hwaddr_len);
	  
	  if (lease->clid && in_list(req_options, OPTION_CLIENT_ID) &&
	      (p = free_space(mess, end, OPTION_CLIENT_ID, lease->clid_len)))
	    memcpy(p, lease->clid, lease->clid_len);
	  
	  if (in_list(req_options, OPTION_LEASE_TIME))
	    {
	      if (lease->expires == 0) /* infinite lease */
		option_put(mess, end, OPTION_LEASE_TIME, 4, 0xffffffff);
	      else
		option_put(mess, end, OPTION_LEASE_TIME, 4, (unsigned int)(lease->expires - now));
	    }
	  
	  if (lease->expires != 0)
	    {
	      time = calc_time(context, config, NULL);

	      if (in_list(req_options, OPTION_T1) && (lease->expires - now) > time/2)
		option_put(mess, end, OPTION_T1, 4, ((unsigned int)(lease->expires - now)) - time/2);
	      if (in_list(req_options, OPTION_T2) && (lease->expires - now) > time/8)
		option_put(mess, end, OPTION_T2, 4, ((unsigned int)(lease->expires - now)) - time/8);
	      if (in_list(req_options, OPTION_LAST_TRANSACTION) && (lease->expires - now) < time)
		option_put(mess, end, OPTION_LAST_TRANSACTION, 4, time - ((unsigned int)(lease->expires - now)));
	    }
	  
	  if (lease->vendorclass)
	    {
	      memcpy(daemon->dhcp_buff3, lease->vendorclass, lease->vendorclass_len);
	      vendor_class_len = lease->vendorclass_len;
	    }
	  
	  subnet_addr.s_addr = 0;
	  do_options(context, mess, end, req_options, lease->hostname, get_domain(lease->addr), netid, subnet_addr,
		     0, 0, -1, NULL, vendor_class_len, now, 0xffffffff, 0, NULL, 1);
	  
	  /* Does this have to be last for leasequery replies also? RFC 4388 is silent on the subject. */
	  if (lease->agent_id && in_list(req_options, OPTION_AGENT_ID) &&
	      (p = free_space(mess, end, OPTION_AGENT_ID, lease->agent_id_len)))
	    memcpy(p, lease->agent_id, lease->agent_id_len);
	}
      
      return dhcp_packet_size(mess, NULL, real_end);
            
    case DHCPDECLINE:
      if (!(opt = option_find(mess, sz, OPTION_SERVER_IDENTIFIER, INADDRSZ)) ||
	  option_addr(opt).s_addr != server_id(context, override, fallback).s_addr)
	return 0;
      
      /* sanitise any message. Paranoid? Moi? */
      sanitise(option_find(mess, sz, OPTION_MESSAGE, 1), daemon->dhcp_buff);
      
      if (!(opt = option_find(mess, sz, OPTION_REQUESTED_IP, INADDRSZ)))
	return 0;
      
      daemon->metrics[METRIC_DHCPDECLINE]++;
      log_packet("DHCPDECLINE", option_ptr(opt, 0), emac, emac_len, iface_name, NULL, daemon->dhcp_buff, mess->xid);
      
      if (lease && lease->addr.s_addr == option_addr(opt).s_addr)
	lease_prune(lease, now);
      
      if (have_config(config, CONFIG_ADDR) && 
	  config->addr.s_addr == option_addr(opt).s_addr)
	{
	  prettyprint_time(daemon->dhcp_buff, DECLINE_BACKOFF);
	  inet_ntop(AF_INET, &config->addr, daemon->addrbuff, ADDRSTRLEN);
	  my_syslog(MS_DHCP | LOG_WARNING, _("disabling DHCP static address %s for %s"), 
		    daemon->addrbuff, daemon->dhcp_buff);
	  config->flags |= CONFIG_DECLINED;
	  config->decline_time = now;
	}
      else
	/* make sure this host gets a different address next time. */
	for (; context; context = context->current)
	  context->addr_epoch++;
      
      return 0;

    case DHCPRELEASE:
      if (!(context = narrow_context(context, mess->ciaddr, tagif_netid)) ||
	  !(opt = option_find(mess, sz, OPTION_SERVER_IDENTIFIER, INADDRSZ)) ||
	  option_addr(opt).s_addr != server_id(context, override, fallback).s_addr)
	return 0;
      
      if (lease && lease->addr.s_addr == mess->ciaddr.s_addr)
	lease_prune(lease, now);
      else
	message = _("unknown lease");

      daemon->metrics[METRIC_DHCPRELEASE]++;
      log_packet("DHCPRELEASE", &mess->ciaddr, emac, emac_len, iface_name, NULL, message, mess->xid);
	
      return 0;
      
    case DHCPDISCOVER:
      if (ignore || have_config(config, CONFIG_DISABLE))
	{
	  if (option_bool(OPT_QUIET_DHCP))
	    return 0;
	  message = _("ignored");
	  opt = NULL;
	}
      else 
	{
	  struct in_addr addr, conf;
	  
	  addr.s_addr = conf.s_addr = 0;

	  if ((opt = option_find(mess, sz, OPTION_REQUESTED_IP, INADDRSZ)))	 
	    addr = option_addr(opt);
	  
	  if (have_config(config, CONFIG_ADDR))
	    {
	      inet_ntop(AF_INET, &config->addr, daemon->addrbuff, ADDRSTRLEN);
	      
	      if ((ltmp = lease_find_by_addr(config->addr)) && 
		  ltmp != lease &&
		  !config_has_mac(config, ltmp->hwaddr, ltmp->hwaddr_len, ltmp->hwaddr_type))
		{
		  int len;
		  unsigned char *mac = extended_hwaddr(ltmp->hwaddr_type, ltmp->hwaddr_len,
						       ltmp->hwaddr, ltmp->clid_len, ltmp->clid, &len);
		  my_syslog(MS_DHCP | LOG_WARNING, _("not using configured address %s because it is leased to %s"),
			    daemon->addrbuff, print_mac(daemon->namebuff, mac, len));
		}
	      else
		{
		  struct dhcp_context *tmp;
		  for (tmp = context; tmp; tmp = tmp->current)
		    if (context->router.s_addr == config->addr.s_addr)
		      break;
		  if (tmp)
		    my_syslog(MS_DHCP | LOG_WARNING, _("not using configured address %s because it is in use by the server or relay"), daemon->addrbuff);
		  else if (have_config(config, CONFIG_DECLINED) &&
			   difftime(now, config->decline_time) < (float)DECLINE_BACKOFF)
		    my_syslog(MS_DHCP | LOG_WARNING, _("not using configured address %s because it was previously declined"), daemon->addrbuff);
		  else
		    conf = config->addr;
		}
	    }
	  
	  if (conf.s_addr)
	    mess->yiaddr = conf;
	  else if (lease && 
		   address_available(context, lease->addr, tagif_netid) && 
		   !config_find_by_address(daemon->dhcp_conf, lease->addr))
	    mess->yiaddr = lease->addr;
	  else if (opt && address_available(context, addr, tagif_netid) && !lease_find_by_addr(addr) && 
		   !config_find_by_address(daemon->dhcp_conf, addr) && do_icmp_ping(now, addr, 0, loopback))
	    mess->yiaddr = addr;
	  else if (emac_len == 0)
	    message = _("no unique-id");
	  else if (!address_allocate(context, &mess->yiaddr, emac, emac_len, tagif_netid, now, loopback))
	    message = _("no address available");      
	}
      
      daemon->metrics[METRIC_DHCPDISCOVER]++;
      log_packet("DHCPDISCOVER", opt ? option_ptr(opt, 0) : NULL, emac, emac_len, iface_name, NULL, message, mess->xid); 

      if (message || !(context = narrow_context(context, mess->yiaddr, tagif_netid)))
	return 0;

      if (context->netid.net)
	{
	  context->netid.next = netid;
	  tagif_netid = run_tag_if(&context->netid);
	}

      apply_delay(mess->xid, recvtime, tagif_netid);

      if (option_bool(OPT_RAPID_COMMIT) && option_find(mess, sz, OPTION_RAPID_COMMIT, 0))
	{
	  rapid_commit = 1;
	  /* If a lease exists for this host and another address, squash it. */
	  if (lease && lease->addr.s_addr != mess->yiaddr.s_addr)
	    {
	      lease_prune(lease, now);
	      lease = NULL;
	    }
	  goto rapid_commit;
	}
      
      log_tags(tagif_netid, ntohl(mess->xid));

      daemon->metrics[METRIC_DHCPOFFER]++;
      log_packet("DHCPOFFER" , &mess->yiaddr, emac, emac_len, iface_name, NULL, NULL, mess->xid);
      
      time = calc_time(context, config, option_find(mess, sz, OPTION_LEASE_TIME, 4));
      clear_packet(mess, end);
      option_put(mess, end, OPTION_MESSAGE_TYPE, 1, DHCPOFFER);
      option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, ntohl(server_id(context, override, fallback).s_addr));
      option_put(mess, end, OPTION_LEASE_TIME, 4, time);
      /* T1 and T2 are required in DHCPOFFER by HP's wacky Jetdirect client. */
      do_options(context, mess, end, req_options, offer_hostname, get_domain(mess->yiaddr), 
		 netid, subnet_addr, fqdn_flags, borken_opt, pxearch, uuid, vendor_class_len, now, time, fuzz, pxevendor, 0);
      
      return dhcp_packet_size(mess, agent_id, real_end);
	

    case DHCPREQUEST:
      if (ignore || have_config(config, CONFIG_DISABLE))
	return 0;
      if ((opt = option_find(mess, sz, OPTION_REQUESTED_IP, INADDRSZ)))
	{
	  /* SELECTING  or INIT_REBOOT */
	  mess->yiaddr = option_addr(opt);
	  
	  /* send vendor and user class info for new or recreated lease */
	  do_classes = 1;
	  
	  if ((opt = option_find(mess, sz, OPTION_SERVER_IDENTIFIER, INADDRSZ)))
	    {
	      /* SELECTING */
	      selecting = 1;
	      
	      if (override.s_addr != 0)
		{
		  if (option_addr(opt).s_addr != override.s_addr)
		    return 0;
		}
	      else 
		{
		  for (; context; context = context->current)
		    if (context->local.s_addr == option_addr(opt).s_addr)
		      break;
		  
		  if (!context)
		    {
		      /* Handle very strange configs where clients have more than one route to the server.
			 If a clients idea of its server-id matches any of our DHCP interfaces, we let it pass.
			 Have to set override to make sure we echo back the correct server-id */
		      struct irec *intr;
		      
		      enumerate_interfaces(0);

		      for (intr = daemon->interfaces; intr; intr = intr->next)
			if (intr->addr.sa.sa_family == AF_INET &&
			    intr->addr.in.sin_addr.s_addr == option_addr(opt).s_addr &&
			    intr->tftp_ok)
			  break;

		      if (intr)
			override = intr->addr.in.sin_addr;
		      else
			{
			  /* In auth mode, a REQUEST sent to the wrong server
			     should be faulted, so that the client establishes 
			     communication with us, otherwise, silently ignore. */
			  if (!option_bool(OPT_AUTHORITATIVE))
			    return 0;
			  message = _("wrong server-ID");
			}
		    }
		}

	      /* If a lease exists for this host and another address, squash it. */
	      if (lease && lease->addr.s_addr != mess->yiaddr.s_addr)
		{
		  lease_prune(lease, now);
		  lease = NULL;
		}
	    }
	  else
	    {
	      /* INIT-REBOOT */
	      if (!lease && !option_bool(OPT_AUTHORITATIVE))
		return 0;
	      
	      if (lease && lease->addr.s_addr != mess->yiaddr.s_addr)
		message = _("wrong address");
	    }
	}
      else
	{
	  /* RENEWING or REBINDING */ 
	  /* Check existing lease for this address.
	     We allow it to be missing if dhcp-authoritative mode
	     as long as we can allocate the lease now - checked below.
	     This makes for a smooth recovery from a lost lease DB */
	  if ((lease && mess->ciaddr.s_addr != lease->addr.s_addr) ||
	      (!lease && !option_bool(OPT_AUTHORITATIVE)))
	    {
	      /* A client rebinding will broadcast the request, so we may see it even 
		 if the lease is held by another server. Just ignore it in that case. 
		 If the request is unicast to us, then somethings wrong, NAK */
	      if (!unicast_dest)
		return 0;
	      message = _("lease not found");
	      /* ensure we broadcast NAK */
	      unicast_dest = 0;
	    }

	  /* desynchronise renewals */
	  fuzz = rand16();
	  mess->yiaddr = mess->ciaddr;
	}

      daemon->metrics[METRIC_DHCPREQUEST]++;
      log_packet("DHCPREQUEST", &mess->yiaddr, emac, emac_len, iface_name, NULL, NULL, mess->xid);
      
    rapid_commit:
      if (!message)
	{
	  struct dhcp_config *addr_config;
	  struct dhcp_context *tmp = NULL;
	  
	  if (have_config(config, CONFIG_ADDR))
	    for (tmp = context; tmp; tmp = tmp->current)
	      if (context->router.s_addr == config->addr.s_addr)
		break;
	  
	  if (!(context = narrow_context(context, mess->yiaddr, tagif_netid)))
	    {
	      /* If a machine moves networks whilst it has a lease, we catch that here. */
	      message = _("wrong network");
	      /* ensure we broadcast NAK */
	      unicast_dest = 0;
	    }
	  
	  /* Check for renewal of a lease which is outside the allowed range. */
	  else if (!address_available(context, mess->yiaddr, tagif_netid) &&
		   (!have_config(config, CONFIG_ADDR) || config->addr.s_addr != mess->yiaddr.s_addr))
	    message = _("address not available");
	  
	  /* Check if a new static address has been configured. Be very sure that
	     when the client does DISCOVER, it will get the static address, otherwise
	     an endless protocol loop will ensue. */
	  else if (!tmp && !selecting &&
		   have_config(config, CONFIG_ADDR) && 
		   (!have_config(config, CONFIG_DECLINED) ||
		    difftime(now, config->decline_time) > (float)DECLINE_BACKOFF) &&
		   config->addr.s_addr != mess->yiaddr.s_addr &&
		   (!(ltmp = lease_find_by_addr(config->addr)) || ltmp == lease))
	    message = _("static lease available");

	  /* Check to see if the address is reserved as a static address for another host */
	  else if ((addr_config = config_find_by_address(daemon->dhcp_conf, mess->yiaddr)) && addr_config != config)
	    message = _("address reserved");

	  else if (!lease && (ltmp = lease_find_by_addr(mess->yiaddr)))
	    {
	      /* If a host is configured with more than one MAC address, it's OK to 'nix 
		 a lease from one of its MACs to give the address to another. */
	      if (config && config_has_mac(config, ltmp->hwaddr, ltmp->hwaddr_len, ltmp->hwaddr_type))
		{
		  inet_ntop(AF_INET, &ltmp->addr, daemon->addrbuff, ADDRSTRLEN);
		  my_syslog(MS_DHCP | LOG_INFO, _("abandoning lease to %s of %s"),
			    print_mac(daemon->namebuff, ltmp->hwaddr, ltmp->hwaddr_len), 
			    daemon->addrbuff);
		  lease = ltmp;
		}
	      else
		message = _("address in use");
	    }

	  if (!message)
	    {
	      if (emac_len == 0)
		message = _("no unique-id");
	      
	      else if (!lease)
		{	     
		  if ((lease = lease4_allocate(mess->yiaddr)))
		    do_classes = 1;
		  else
		    message = _("no leases left");
		}
	    }
	}

      if (message)
	{
	  daemon->metrics[rapid_commit ? METRIC_NOANSWER : METRIC_DHCPNAK]++;
	  log_packet(rapid_commit ? "NOANSWER" : "DHCPNAK", &mess->yiaddr, emac, emac_len, iface_name, NULL, message, mess->xid);

	  /* rapid commit case: lease allocate failed but don't send DHCPNAK */
	  if (rapid_commit)
	    return 0;
	  
	  mess->yiaddr.s_addr = 0;
	  clear_packet(mess, end);
	  option_put(mess, end, OPTION_MESSAGE_TYPE, 1, DHCPNAK);
	  option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, ntohl(server_id(context, override, fallback).s_addr));
	  option_put_string(mess, end, OPTION_MESSAGE, message, borken_opt);
	  /* This fixes a problem with the DHCP spec, broadcasting a NAK to a host on 
	     a distant subnet which unicast a REQ to us won't work. */
	  if (!unicast_dest || mess->giaddr.s_addr != 0 || 
	      mess->ciaddr.s_addr == 0 || is_same_net(context->local, mess->ciaddr, context->netmask))
	    {
	      mess->flags |= htons(0x8000); /* broadcast */
	      mess->ciaddr.s_addr = 0;
	    }
	}
      else
	{
	  if (context->netid.net)
	    {
	      context->netid.next = netid;
	      tagif_netid = run_tag_if( &context->netid);
	    }

	  log_tags(tagif_netid, ntohl(mess->xid));
	  
	  if (do_classes)
	    {
	      /* pick up INIT-REBOOT events. */
	      lease->flags |= LEASE_CHANGED;

#ifdef HAVE_SCRIPT
	      if (daemon->lease_change_command)
		{
		  struct dhcp_netid *n;
		  
		  if (mess->giaddr.s_addr)
		    lease->giaddr = mess->giaddr;
		  
		  free(lease->extradata);
		  lease->extradata = NULL;
		  lease->extradata_size = lease->extradata_len = 0;
		  
		  add_extradata_opt(lease, option_find(mess, sz, OPTION_VENDOR_ID, 1));
		  add_extradata_opt(lease, option_find(mess, sz, OPTION_HOSTNAME, 1));
		  add_extradata_opt(lease, oui);
		  add_extradata_opt(lease, serial);
		  add_extradata_opt(lease, class);

		  if ((opt = option_find(mess, sz, OPTION_AGENT_ID, 1)))
		    {
		      add_extradata_opt(lease, option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_CIRCUIT_ID, 1));
		      add_extradata_opt(lease, option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_SUBSCR_ID, 1));
		      add_extradata_opt(lease, option_find1(option_ptr(opt, 0), option_ptr(opt, option_len(opt)), SUBOPT_REMOTE_ID, 1));
		    }
		  else
		    {
		      add_extradata_opt(lease, NULL);
		      add_extradata_opt(lease, NULL);
		      add_extradata_opt(lease, NULL);
		    }

		  /* DNSMASQ_REQUESTED_OPTIONS */
		  if ((opt = option_find(mess, sz, OPTION_REQUESTED_OPTIONS, 1)))
		    {
		      int i, len = option_len(opt);
		      unsigned char *rop = option_ptr(opt, 0);
		      
		      for (i = 0; i < len; i++)
			lease_add_extradata(lease, (unsigned char *)daemon->namebuff,
					    sprintf(daemon->namebuff, "%u", rop[i]), (i + 1) == len ? 0 : ',');
		    }
		  else
		    lease_add_extradata(lease, NULL, 0, 0);
		  
		  add_extradata_opt(lease, option_find(mess, sz, OPTION_MUD_URL_V4, 1));
		  
		  /* space-concat tag set */
		  if (!tagif_netid)
		    add_extradata_opt(lease, NULL);
		  else
		    for (n = tagif_netid; n; n = n->next)
		      {
			struct dhcp_netid *n1;
			/* kill dupes */
			for (n1 = n->next; n1; n1 = n1->next)
			  if (strcmp(n->net, n1->net) == 0)
			    break;
			if (!n1)
			  lease_add_extradata(lease, (unsigned char *)n->net, strlen(n->net), n->next ? ' ' : 0); 
		      }
		  
		  if ((opt = option_find(mess, sz, OPTION_USER_CLASS, 1)))
		    {
		      int len = option_len(opt);
		      unsigned char *ucp = option_ptr(opt, 0);
		      /* If the user-class option started as counted strings, the first byte will be zero. */
		      if (len != 0 && ucp[0] == 0)
			ucp++, len--;
		      lease_add_extradata(lease, ucp, len, -1);
		    }
		}
#endif
	    }
	  
	  if (!hostname_auth && (client_hostname = host_from_dns(mess->yiaddr)))
	    {
	      domain = get_domain(mess->yiaddr);
	      hostname = client_hostname;
	      hostname_auth = 1;
	    }
	  
	  time = calc_time(context, config, option_find(mess, sz, OPTION_LEASE_TIME, 4));
	  lease_set_hwaddr(lease, mess->chaddr, clid, mess->hlen, mess->htype, clid_len, now, do_classes);
	  
	  /* if all the netids in the ignore_name list are present, ignore client-supplied name */
	  if (!hostname_auth)
	    {
	      for (id_list = daemon->dhcp_ignore_names; id_list; id_list = id_list->next)
		if ((!id_list->list) || match_netid(id_list->list, tagif_netid, 0))
		  break;
	      if (id_list)
		hostname = NULL;
	    }
	  
	  /* Last ditch, if configured, generate hostname from mac address */
	  if (!hostname && emac_len != 0)
	    {
	      for (id_list = daemon->dhcp_gen_names; id_list; id_list = id_list->next)
		if ((!id_list->list) || match_netid(id_list->list, tagif_netid, 0))
		  break;
	      if (id_list)
		{
		  int i;

		  hostname = daemon->dhcp_buff;
		  /* buffer is 256 bytes, 3 bytes per octet */
		  for (i = 0; (i < emac_len) && (i < 80); i++)
		    hostname += sprintf(hostname, "%.2x%s", emac[i], (i == emac_len - 1) ? "" : "-");
		  hostname = daemon->dhcp_buff;
		}
	    }

	  if (hostname)
	    lease_set_hostname(lease, hostname, hostname_auth, get_domain(lease->addr), domain);
	  
	  lease_set_expires(lease, time, now);
	  lease_set_interface(lease, int_index, now);
	  
	  if (option_bool(OPT_LEASEQUERY))
	    {
	      if (agent_id)
		lease_set_agent_id(lease, option_ptr(agent_id, 0), option_len(agent_id));
	      if (vendor_class_len != 0)
		lease_set_vendorclass(lease, (unsigned char *)daemon->dhcp_buff3, vendor_class_len);
	    }
	  else
	    {
	      /* if leasequery no longer enabled, remove stuff that may have been stored when it was. */
	      lease_set_agent_id(lease, NULL, 0);
	      lease_set_vendorclass(lease, NULL, 0);
	    }
	  
	  if (override.s_addr != 0)
	    lease->override = override;
	  else
	    override = lease->override;

	  daemon->metrics[METRIC_DHCPACK]++;
	  log_packet("DHCPACK", &mess->yiaddr, emac, emac_len, iface_name, hostname, NULL, mess->xid);  

	  clear_packet(mess, end);
	  option_put(mess, end, OPTION_MESSAGE_TYPE, 1, DHCPACK);
	  option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, ntohl(server_id(context, override, fallback).s_addr));
	  option_put(mess, end, OPTION_LEASE_TIME, 4, time);
	  if (rapid_commit)
	     option_put(mess, end, OPTION_RAPID_COMMIT, 0, 0);
	   do_options(context, mess, end, req_options, hostname, get_domain(mess->yiaddr), 
		      netid, subnet_addr, fqdn_flags, borken_opt, pxearch, uuid, vendor_class_len, now, time, fuzz, pxevendor, 0);
	}

      return dhcp_packet_size(mess, agent_id, real_end); 
      
    case DHCPINFORM:
      if (ignore || have_config(config, CONFIG_DISABLE))
	message = _("ignored");
      
      daemon->metrics[METRIC_DHCPINFORM]++;
      log_packet("DHCPINFORM", &mess->ciaddr, emac, emac_len, iface_name, message, NULL, mess->xid);
     
      if (message || mess->ciaddr.s_addr == 0)
	return 0;

      /* For DHCPINFORM only, cope without a valid context */
      context = narrow_context(context, mess->ciaddr, tagif_netid);
      
      /* Find a least based on IP address if we didn't
	 get one from MAC address/client-d */
      if (!lease &&
	  (lease = lease_find_by_addr(mess->ciaddr)) && 
	  lease->hostname)
	hostname = lease->hostname;
      
      if (!hostname)
	hostname = host_from_dns(mess->ciaddr);
      
      if (context && context->netid.net)
	{
	  context->netid.next = netid;
	  tagif_netid = run_tag_if(&context->netid);
	}

      log_tags(tagif_netid, ntohl(mess->xid));
      
      daemon->metrics[METRIC_DHCPACK]++;
      log_packet("DHCPACK", &mess->ciaddr, emac, emac_len, iface_name, hostname, NULL, mess->xid);
      
      if (lease)
	{
	  lease_set_interface(lease, int_index, now);
	  if (override.s_addr != 0)
	    lease->override = override;
	  else
	    override = lease->override;
	}

      clear_packet(mess, end);
      option_put(mess, end, OPTION_MESSAGE_TYPE, 1, DHCPACK);
      option_put(mess, end, OPTION_SERVER_IDENTIFIER, INADDRSZ, ntohl(server_id(context, override, fallback).s_addr));
     
      /* RFC 2131 says that DHCPINFORM shouldn't include lease-time parameters, but 
	 we supply a utility which makes DHCPINFORM requests to get this information.
	 Only include lease time if OPTION_LEASE_TIME is in the parameter request list,
	 which won't be true for ordinary clients, but will be true for the 
	 dhcp_lease_time utility. */
      if (lease && in_list(req_options, OPTION_LEASE_TIME))
	{
	  if (lease->expires == 0)
	    time = 0xffffffff;
	  else
	    time = (unsigned int)difftime(lease->expires, now);
	  option_put(mess, end, OPTION_LEASE_TIME, 4, time);
	}

      do_options(context, mess, end, req_options, hostname, get_domain(mess->ciaddr),
		 netid, subnet_addr, fqdn_flags, borken_opt, pxearch, uuid, vendor_class_len, now, 0xffffffff, 0, pxevendor, 0);
      
      *is_inform = 1; /* handle reply differently */
      return dhcp_packet_size(mess, agent_id, real_end); 
    }
  
  return 0;
}

/* find a good value to use as MAC address for logging and address-allocation hashing.
   This is normally just the chaddr field from the DHCP packet,
   but eg Firewire will have hlen == 0 and use the client-id instead. 
   This could be anything, but will normally be EUI64 for Firewire.
   We assume that if the first byte of the client-id equals the htype byte
   then the client-id is using the usual encoding and use the rest of the 
   client-id: if not we can use the whole client-id. This should give
   sane MAC address logs. */
unsigned char *extended_hwaddr(int hwtype, int hwlen, unsigned char *hwaddr, 
				      int clid_len, unsigned char *clid, int *len_out)
{
  if (hwlen == 0 && clid && clid_len > 3)
    {
      if (clid[0]  == hwtype)
	{
	  *len_out = clid_len - 1 ;
	  return clid + 1;
	}

#if defined(ARPHRD_EUI64) && defined(ARPHRD_IEEE1394)
      if (clid[0] ==  ARPHRD_EUI64 && hwtype == ARPHRD_IEEE1394)
	{
	  *len_out = clid_len - 1 ;
	  return clid + 1;
	}
#endif
      
      *len_out = clid_len;
      return clid;
    }
  
  *len_out = hwlen;
  return hwaddr;
}

static unsigned int calc_time(struct dhcp_context *context, struct dhcp_config *config, unsigned char *opt)
{
  unsigned int time = have_config(config, CONFIG_TIME) ? config->lease_time : context->lease_time;
  
  if (opt)
    { 
      unsigned int req_time = option_uint(opt, 0, 4);
      if (req_time < 120 )
	req_time = 120; /* sanity */
      if (time == 0xffffffff || (req_time != 0xffffffff && req_time < time))
	time = req_time;
    }

  return time;
}

static struct in_addr server_id(struct dhcp_context *context, struct in_addr override, struct in_addr fallback)
{
  if (override.s_addr != 0)
    return override;
  else if (context && context->local.s_addr != 0)
    return context->local;
  else
    return fallback;
}

static int sanitise(unsigned char *opt, char *buf)
{
  char *p;
  int i;
  
  *buf = 0;
  
  if (!opt)
    return 0;

  p = option_ptr(opt, 0);

  for (i = option_len(opt); i > 0; i--)
    {
      char c = *p++;
      if (isprint((unsigned char)c))
	*buf++ = c;
    }
  *buf = 0; /* add terminator */
  
  return 1;
}

#ifdef HAVE_SCRIPT
static void add_extradata_opt(struct dhcp_lease *lease, unsigned char *opt)
{
  if (!opt)
    lease_add_extradata(lease, NULL, 0, 0);
  else
    lease_add_extradata(lease, option_ptr(opt, 0), option_len(opt), 0); 
}
#endif

static void log_packet(char *type, void *addr, unsigned char *ext_mac, 
		       int mac_len, char *interface, char *string, char *err, u32 xid)
{
  if (!err && !option_bool(OPT_LOG_OPTS) && option_bool(OPT_QUIET_DHCP))
    return;
  
  daemon->addrbuff[0] = daemon->namebuff[0] = 0;
  
  if (addr)
    inet_ntop(AF_INET, addr, daemon->addrbuff, ADDRSTRLEN);
  
  if (ext_mac)
    print_mac(daemon->namebuff, ext_mac, mac_len);
  
  if (option_bool(OPT_LOG_OPTS))
    my_syslog(MS_DHCP | LOG_INFO, "%u %s(%s) %s%s%s%s%s%s",
	      ntohl(xid), 
	      type,
	      interface, 
	      daemon->addrbuff,
	      addr ? " " : "",
	      daemon->namebuff,
	      ext_mac ? " " : "",
	      string ? string : "",
	      err ? err : "");
  else
    my_syslog(MS_DHCP | LOG_INFO, "%s(%s) %s%s%s%s%s%s",
	      type,
	      interface, 
	      daemon->addrbuff,
	      addr ? " " : "",
	      daemon->namebuff,
	      ext_mac ? " " : "",
	      string ? string : "",
	      err ? err : "");
  
#ifdef HAVE_UBUS
  if (!strcmp(type, "DHCPACK"))
    ubus_event_bcast("dhcp.ack", daemon->namebuff, addr ? daemon->addrbuff : NULL, string, interface);
  else if (!strcmp(type, "DHCPRELEASE"))
    ubus_event_bcast("dhcp.release", daemon->namebuff, addr ? daemon->addrbuff : NULL, string, interface);
#endif
}

static void log_options(unsigned char *start, u32 xid)
{
  while (*start != OPTION_END)
    {
      char *optname = option_string(AF_INET, start[0], option_ptr(start, 0), option_len(start), daemon->namebuff, MAXDNAME);
      
      my_syslog(MS_DHCP | LOG_INFO, "%u sent size:%3d option:%3d %s  %s", 
		ntohl(xid), option_len(start), start[0], optname, daemon->namebuff);
      start += start[1] + 2;
    }
}

static unsigned char *option_find1(unsigned char *p, unsigned char *end, int opt, int minsize)
{
  while (1) 
    {
      if (p >= end)
	return NULL;
      else if (*p == OPTION_END)
	return opt == OPTION_END ? p : NULL;
      else if (*p == OPTION_PAD)
	p++;
      else 
	{ 
	  int opt_len;
	  if (p > end - 2)
	    return NULL; /* malformed packet */
	  opt_len = option_len(p);
	  if (p > end - (2 + opt_len))
	    return NULL; /* malformed packet */
	  if (*p == opt && opt_len >= minsize)
	    return p;
	  p += opt_len + 2;
	}
    }
}
 
static unsigned char *option_find(struct dhcp_packet *mess, size_t size, int opt_type, int minsize)
{
  unsigned char *ret, *overload;
  
  /* skip over DHCP cookie; */
  if ((ret = option_find1(&mess->options[0] + sizeof(u32), ((unsigned char *)mess) + size, opt_type, minsize)))
    return ret;

  /* look for overload option. */
  if (!(overload = option_find1(&mess->options[0] + sizeof(u32), ((unsigned char *)mess) + size, OPTION_OVERLOAD, 1)))
    return NULL;
  
  /* Can we look in filename area ? */
  if ((overload[2] & 1) &&
      (ret = option_find1(&mess->file[0], &mess->file[128], opt_type, minsize)))
    return ret;

  /* finally try sname area */
  if ((overload[2] & 2) &&
      (ret = option_find1(&mess->sname[0], &mess->sname[64], opt_type, minsize)))
    return ret;

  return NULL;
}

static struct in_addr option_addr(unsigned char *opt)
{
   /* this worries about unaligned data in the option. */
  /* struct in_addr is network byte order */
  struct in_addr ret;

  memcpy(&ret, option_ptr(opt, 0), INADDRSZ);

  return ret;
}

static unsigned int option_uint(unsigned char *opt, int offset, int size)
{
  /* this worries about unaligned data and byte order */
  unsigned int ret = 0;
  int i;
  unsigned char *p = option_ptr(opt, offset);
  
  for (i = 0; i < size; i++)
    ret = (ret << 8) | *p++;

  return ret;
}

static unsigned char *dhcp_skip_opts(unsigned char *start)
{
  while (*start != 0)
    start += start[1] + 2;
  return start;
}

/* only for use when building packet: doesn't check for bad data. */ 
static unsigned char *find_overload(struct dhcp_packet *mess)
{
  unsigned char *p = &mess->options[0] + sizeof(u32);
  
  while (*p != 0)
    {
      if (*p == OPTION_OVERLOAD)
	return p;
      p += p[1] + 2;
    }
  return NULL;
}

static size_t dhcp_packet_size(struct dhcp_packet *mess, unsigned char *agent_id, unsigned char *real_end)
{
  unsigned char *p = dhcp_skip_opts(&mess->options[0] + sizeof(u32));
  unsigned char *overload;
  size_t ret;
  
  /* move agent_id back down to the end of the packet */
  if (agent_id)
    {
      memmove(p, agent_id, real_end - agent_id);
      p += real_end - agent_id;
      memset(p, 0, real_end - p); /* in case of overlap */
    }
  
  /* add END options to the regions. */
  overload = find_overload(mess);
  
  if (overload && (option_uint(overload, 0, 1) & 1))
    {
      *dhcp_skip_opts(mess->file) = OPTION_END;
      if (option_bool(OPT_LOG_OPTS))
	log_options(mess->file, mess->xid);
    }
  else if (option_bool(OPT_LOG_OPTS) && strlen((char *)mess->file) != 0)
    my_syslog(MS_DHCP | LOG_INFO, _("%u bootfile name: %s"), ntohl(mess->xid), (char *)mess->file);
  
  if (overload && (option_uint(overload, 0, 1) & 2))
    {
      *dhcp_skip_opts(mess->sname) = OPTION_END;
      if (option_bool(OPT_LOG_OPTS))
	log_options(mess->sname, mess->xid);
    }
  else if (option_bool(OPT_LOG_OPTS) && strlen((char *)mess->sname) != 0)
    my_syslog(MS_DHCP | LOG_INFO, _("%u server name: %s"), ntohl(mess->xid), (char *)mess->sname);


  *p++ = OPTION_END;
  
  if (option_bool(OPT_LOG_OPTS))
    {
      if (mess->siaddr.s_addr != 0)
	{
	  inet_ntop(AF_INET, &mess->siaddr, daemon->addrbuff, ADDRSTRLEN);
	  my_syslog(MS_DHCP | LOG_INFO, _("%u next server: %s"), ntohl(mess->xid), daemon->addrbuff);
	}
      
      if ((mess->flags & htons(0x8000)) && mess->ciaddr.s_addr == 0)
	my_syslog(MS_DHCP | LOG_INFO, _("%u broadcast response"), ntohl(mess->xid));
      
      log_options(&mess->options[0] + sizeof(u32), mess->xid);
    } 
  
  ret = (size_t)(p - (unsigned char *)mess);
  
  if (ret < MIN_PACKETSZ)
    ret = MIN_PACKETSZ;
  
  return ret;
}

static unsigned char *free_space(struct dhcp_packet *mess, unsigned char *end, int opt, int len)
{
  unsigned char *p = dhcp_skip_opts(&mess->options[0] + sizeof(u32));
  
  if (p + len + 3 >= end)
    /* not enough space in options area, try and use overload, if poss */
    {
      unsigned char *overload;
      
      if (!(overload = find_overload(mess)) &&
	  (mess->file[0] == 0 || mess->sname[0] == 0))
	{
	  /* attempt to overload fname and sname areas, we've reserved space for the
	     overflow option previuously. */
	  overload = p;
	  *(p++) = OPTION_OVERLOAD;
	  *(p++) = 1;
	}
      
      p = NULL;
      
      /* using filename field ? */
      if (overload)
	{
	  if (mess->file[0] == 0)
	    overload[2] |= 1;
	  
	  if (overload[2] & 1)
	    {
	      p = dhcp_skip_opts(mess->file);
	      if (p + len + 3 >= mess->file + sizeof(mess->file))
		p = NULL;
	    }
	  
	  if (!p)
	    {
	      /* try to bring sname into play (it may be already) */
	      if (mess->sname[0] == 0)
		overload[2] |= 2;
	      
	      if (overload[2] & 2)
		{
		  p = dhcp_skip_opts(mess->sname);
		  if (p + len + 3 >= mess->sname + sizeof(mess->sname))
		    p = NULL;
		}
	    }
	}
      
      if (!p)
	my_syslog(MS_DHCP | LOG_WARNING, _("cannot send DHCP/BOOTP option %d: no space left in packet"), opt);
    }
 
  if (p)
    {
      *(p++) = opt;
      *(p++) = len;
    }

  return p;
}
	      
static void option_put(struct dhcp_packet *mess, unsigned char *end, int opt, int len, unsigned int val)
{
  int i;
  unsigned char *p = free_space(mess, end, opt, len);
  
  if (p) 
    for (i = 0; i < len; i++)
      *(p++) = val >> (8 * (len - (i + 1)));
}

static void option_put_string(struct dhcp_packet *mess, unsigned char *end, int opt, 
			      const char *string, int null_term)
{
  unsigned char *p;
  size_t len = strlen(string);

  if (null_term && len != 255)
    len++;

  if ((p = free_space(mess, end, opt, len)))
    memcpy(p, string, len);
}

/* return length, note this only does the data part */
static int do_opt(struct dhcp_opt *opt, unsigned char *p, struct dhcp_context *context, int null_term)
{
  int len = opt->len;
  
  if ((opt->flags & DHOPT_STRING) && null_term && len != 255)
    len++;

  if (p && len != 0)
    {
      if (context && (opt->flags & DHOPT_ADDR))
	{
	  int j;
	  struct in_addr *a = (struct in_addr *)opt->val;
	  for (j = 0; j < opt->len; j+=INADDRSZ, a++)
	    {
	      /* zero means "self" (but not in vendorclass options.) */
	      if (a->s_addr == 0)
		memcpy(p, &context->local, INADDRSZ);
	      else
		memcpy(p, a, INADDRSZ);
	      p += INADDRSZ;
	    }
	}
      else
	/* empty string may be extended to "\0" by null_term */
	memcpy(p, opt->val ? opt->val : (unsigned char *)"", len);
    }  
  return len;
}

static int in_list(unsigned char *list, int opt)
{
  int i;

   /* If no requested options, send everything, not nothing. */
  if (!list)
    return 1;
  
  for (i = 0; list[i] != OPTION_END; i++)
    if (opt == list[i])
      return 1;

  return 0;
}

static struct dhcp_opt *option_find2(int opt)
{
  struct dhcp_opt *opts;
  
  for (opts = daemon->dhcp_opts; opts; opts = opts->next)
    if (opts->opt == opt && (opts->flags & DHOPT_TAGOK))
      return opts;
  
  return NULL;
}

/* mark vendor-encapsulated options which match the client-supplied  or
   config-supplied vendor class */
static void match_vendor_opts(unsigned char *opt, struct dhcp_opt *dopt)
{
  for (; dopt; dopt = dopt->next)
    {
      dopt->flags &= ~DHOPT_VENDOR_MATCH;
      if (opt && (dopt->flags & DHOPT_VENDOR))
	{
	  const struct dhcp_pxe_vendor *pv;
	  struct dhcp_pxe_vendor dummy_vendor = {
	    .data = (char *)dopt->u.vendor_class,
	    .next = NULL,
	  };
	  if (dopt->flags & DHOPT_VENDOR_PXE)
	    pv = daemon->dhcp_pxe_vendors;
	  else
	    pv = &dummy_vendor;
	  for (; pv; pv = pv->next)
	    {
	      int i, len = 0, matched = 0;
	      if (pv->data)
	        len = strlen(pv->data);
	      for (i = 0; i <= (option_len(opt) - len); i++)
	        if (len == 0 || memcmp(pv->data, option_ptr(opt, i), len) == 0)
	          {
		    matched = 1;
	            break;
	          }
	      if (matched)
		{
	          dopt->flags |= DHOPT_VENDOR_MATCH;
		  break;
		}
	    }
	}
    }
}

static int do_encap_opts(struct dhcp_opt *opt, int encap, int flag,  
			 struct dhcp_packet *mess, unsigned char *end, int null_term)
{
  int len, enc_len, ret = 0;
  struct dhcp_opt *start;
  unsigned char *p;
    
  /* find size in advance */
  for (enc_len = 0, start = opt; opt; opt = opt->next)
    if (opt->flags & flag)
      {
	int new = do_opt(opt, NULL, NULL, null_term) + 2;
	ret  = 1;
	if (enc_len + new <= 255)
	  enc_len += new;
	else
	  {
	    p = free_space(mess, end, encap, enc_len);
	    for (; start && start != opt; start = start->next)
	      if (p && (start->flags & flag))
		{
		  len = do_opt(start, p + 2, NULL, null_term);
		  *(p++) = start->opt;
		  *(p++) = len;
		  p += len;
		}
	    enc_len = new;
	    start = opt;
	  }
      }
  
  if (enc_len != 0 &&
      (p = free_space(mess, end, encap, enc_len + 1)))
    {
      for (; start; start = start->next)
	if (start->flags & flag)
	  {
	    len = do_opt(start, p + 2, NULL, null_term);
	    *(p++) = start->opt;
	    *(p++) = len;
	    p += len;
	  }
      *p = OPTION_END;
    }

  return ret;
}

static void pxe_misc(struct dhcp_packet *mess, unsigned char *end, unsigned char *uuid, const char *pxevendor)
{
  unsigned char *p;

  if (!pxevendor)
    pxevendor="PXEClient";
  option_put_string(mess, end, OPTION_VENDOR_ID, pxevendor, 0);
  if (uuid && (p = free_space(mess, end, OPTION_PXE_UUID, 17)))
    memcpy(p, uuid, 17);
}

static int prune_vendor_opts(struct dhcp_netid *netid)
{
  int force = 0;
  struct dhcp_opt *opt;

  /* prune vendor-encapsulated options based on netid, and look if we're forcing them to be sent */
  for (opt = daemon->dhcp_opts; opt; opt = opt->next)
    if (opt->flags & DHOPT_VENDOR_MATCH)
      {
	if (!match_netid(opt->netid, netid, 1))
	  opt->flags &= ~DHOPT_VENDOR_MATCH;
	else if (opt->flags & DHOPT_FORCE)
	  force = 1;
      }
  return force;
}


/* Many UEFI PXE implementations have badly broken menu code.
   If there's exactly one relevant menu item, we abandon the menu system,
   and jamb the data direct into the DHCP file, siaddr and sname fields.
   Note that in this case, we have to assume that layer zero would be requested
   by the client PXE stack. */
static int pxe_uefi_workaround(int pxe_arch, struct dhcp_netid *netid, struct dhcp_packet *mess, struct in_addr local, time_t now, int pxe)
{
  struct pxe_service *service, *found;

  /* Only workaround UEFI archs. */
  if (pxe_arch < 6)
    return 0;
  
  for (found = NULL, service = daemon->pxe_services; service; service = service->next)
    if (pxe_arch == service->CSA && service->basename && match_netid(service->netid, netid, 1))
      {
	if (found)
	  return 0; /* More than one relevant menu item */
	  
	found = service;
      }

  if (!found)
    return 0; /* No relevant menu items. */
  
  if (!pxe)
     return 1;
  
  if (found->sname)
    {
      mess->siaddr = a_record_from_hosts(found->sname, now);
      snprintf((char *)mess->sname, sizeof(mess->sname), "%s", found->sname);
    }
  else 
    {
      if (found->server.s_addr != 0)
	mess->siaddr = found->server; 
      else
	mess->siaddr = local;
  
      inet_ntop(AF_INET, &mess->siaddr, (char *)mess->sname, INET_ADDRSTRLEN);
    }
  
  if (found->basename)
    snprintf((char *)mess->file, sizeof(mess->file), 
	     strchr(found->basename, '.') ? "%s" : "%s.0", found->basename);
  
  return 1;
}

static struct dhcp_opt *pxe_opts(int pxe_arch, struct dhcp_netid *netid, struct in_addr local, time_t now)
{
#define NUM_OPTS 4  

  unsigned  char *p, *q;
  struct pxe_service *service;
  static struct dhcp_opt *o, *ret;
  int i, j = NUM_OPTS - 1;
  struct in_addr boot_server;
  
  /* We pass back references to these, hence they are declared static */
  static unsigned char discovery_control;
  static unsigned char fake_prompt[] = { 0, 'P', 'X', 'E' }; 
  static struct dhcp_opt *fake_opts = NULL;
  
  /* Disable multicast, since we don't support it, and broadcast
     unless we need it */
  discovery_control = 3;
  
  ret = daemon->dhcp_opts;
  
  if (!fake_opts && !(fake_opts = whine_malloc(NUM_OPTS * sizeof(struct dhcp_opt))))
    return ret;

  for (i = 0; i < NUM_OPTS; i++)
    {
      fake_opts[i].flags = DHOPT_VENDOR_MATCH;
      fake_opts[i].netid = NULL;
      fake_opts[i].next = i == (NUM_OPTS - 1) ? ret : &fake_opts[i+1];
    }
  
  /* create the data for the PXE_MENU and PXE_SERVERS options. */
  p = (unsigned char *)daemon->dhcp_buff;
  q = (unsigned char *)daemon->dhcp_buff3;

  for (i = 0, service = daemon->pxe_services; service; service = service->next)
    if (pxe_arch == service->CSA && match_netid(service->netid, netid, 1))
      {
	size_t len = strlen(service->menu);
	/* opt 43 max size is 255. encapsulated option has type and length
	   bytes, so its max size is 253. */
	if (p - (unsigned char *)daemon->dhcp_buff + len + 3 < 253)
	  {
	    *(p++) = service->type >> 8;
	    *(p++) = service->type;
	    *(p++) = len;
	    memcpy(p, service->menu, len);
	    p += len;
	    i++;
	  }
	else
	  {
	  toobig:
	    my_syslog(MS_DHCP | LOG_ERR, _("PXE menu too large"));
	    return daemon->dhcp_opts;
	  }
	
	boot_server = service->basename ? local : 
	  (service->sname ? a_record_from_hosts(service->sname, now) : service->server);
	
	if (boot_server.s_addr != 0)
	  {
	    if (q - (unsigned char *)daemon->dhcp_buff3 + 3 + INADDRSZ >= 253)
	      goto toobig;
	    
	    /* Boot service with known address - give it */
	    *(q++) = service->type >> 8;
	    *(q++) = service->type;
	    *(q++) = 1;
	    /* dest misaligned */
	    memcpy(q, &boot_server.s_addr, INADDRSZ);
	    q += INADDRSZ;
	  }
	else if (service->type != 0)
	  /* We don't know the server for a service type, so we'll
	     allow the client to broadcast for it */
	  discovery_control = 2;
      }

  /* if no prompt, wait forever if there's a choice */
  fake_prompt[0] = (i > 1) ? 255 : 0;
  
  if (i == 0)
    discovery_control = 8; /* no menu - just use use mess->filename */
  else
    {
      ret = &fake_opts[j--];
      ret->len = p - (unsigned char *)daemon->dhcp_buff;
      ret->val = (unsigned char *)daemon->dhcp_buff;
      ret->opt = SUBOPT_PXE_MENU;

      if (q - (unsigned char *)daemon->dhcp_buff3 != 0)
	{
	  ret = &fake_opts[j--]; 
	  ret->len = q - (unsigned char *)daemon->dhcp_buff3;
	  ret->val = (unsigned char *)daemon->dhcp_buff3;
	  ret->opt = SUBOPT_PXE_SERVERS;
	}
    }

  for (o = daemon->dhcp_opts; o; o = o->next)
    if ((o->flags & DHOPT_VENDOR_MATCH) && o->opt == SUBOPT_PXE_MENU_PROMPT)
      break;
  
  if (!o)
    {
      ret = &fake_opts[j--]; 
      ret->len = sizeof(fake_prompt);
      ret->val = fake_prompt;
      ret->opt = SUBOPT_PXE_MENU_PROMPT;
    }
  
  ret = &fake_opts[j--]; 
  ret->len = 1;
  ret->opt = SUBOPT_PXE_DISCOVERY;
  ret->val= &discovery_control;
 
  return ret;
}
  
static void clear_packet(struct dhcp_packet *mess, unsigned char *end)
{
  memset(mess->sname, 0, sizeof(mess->sname));
  memset(mess->file, 0, sizeof(mess->file));
  memset(&mess->options[0] + sizeof(u32), 0, end - (&mess->options[0] + sizeof(u32)));
  mess->siaddr.s_addr = 0;
}

struct dhcp_boot *find_boot(struct dhcp_netid *netid)
{
  struct dhcp_boot *boot;

  /* decide which dhcp-boot option we're using */
  for (boot = daemon->boot_config; boot; boot = boot->next)
    if (match_netid(boot->netid, netid, 0))
      break;
  if (!boot)
    /* No match, look for one without a netid */
    for (boot = daemon->boot_config; boot; boot = boot->next)
      if (match_netid(boot->netid, netid, 1))
	break;

  return boot;
}

static int is_pxe_client(struct dhcp_packet *mess, size_t sz, const char **pxe_vendor)
{
  const unsigned char *opt = NULL;
  ssize_t conf_len = 0;
  const struct dhcp_pxe_vendor *conf = daemon->dhcp_pxe_vendors;
  opt = option_find(mess, sz, OPTION_VENDOR_ID, 0);
  if (!opt) 
    return 0;
  for (; conf; conf = conf->next)
    {
      conf_len = strlen(conf->data);
      if (option_len(opt) < conf_len)
        continue;
      if (strncmp(option_ptr(opt, 0), conf->data, conf_len) == 0)
        {
          if (pxe_vendor)
            *pxe_vendor = conf->data;
          return 1;
        }
    }
  return 0;
}

static void do_options(struct dhcp_context *context,
		       struct dhcp_packet *mess,
		       unsigned char *end, 
		       unsigned char *req_options,
		       char *hostname, 
		       char *domain,
		       struct dhcp_netid *netid,
		       struct in_addr subnet_addr,
		       unsigned char fqdn_flags,
		       int null_term, int pxe_arch,
		       unsigned char *uuid,
		       int vendor_class_len,
		       time_t now,
		       unsigned int lease_time,
		       unsigned short fuzz,
		       const char *pxevendor,
		       int leasequery)
{
  struct dhcp_opt *opt, *config_opts = daemon->dhcp_opts;
  struct dhcp_boot *boot;
  unsigned char *p;
  int i, len, force_encap = 0;
  unsigned char f0 = 0, s0 = 0;
  int done_file = 0, done_server = 0;
  int done_vendor_class = 0;
  struct dhcp_netid *tagif;
  struct dhcp_netid_list *id_list;

  /* filter options based on tags, those we want get DHOPT_TAGOK bit set */
  if (context)
    context->netid.next = NULL;
  tagif = option_filter(netid, context && context->netid.net ? &context->netid : NULL, config_opts, pxe_arch != -1);
	
  /* logging */
  if (option_bool(OPT_LOG_OPTS) && req_options)
    {
      char *q = daemon->namebuff;
      for (i = 0; req_options[i] != OPTION_END; i++)
	{
	  char *s = option_string(AF_INET, req_options[i], NULL, 0, NULL, 0);
	  q += snprintf(q, MAXDNAME - (q - daemon->namebuff),
			"%d%s%s%s", 
			req_options[i],
			strlen(s) != 0 ? ":" : "",
			s, 
			req_options[i+1] == OPTION_END ? "" : ", ");
	  if (req_options[i+1] == OPTION_END || (q - daemon->namebuff) > 40)
	    {
	      q = daemon->namebuff;
	      my_syslog(MS_DHCP | LOG_INFO, _("%u requested options: %s"), ntohl(mess->xid), daemon->namebuff);
	    }
	}
    }
      
  if (!leasequery)
    {
      for (id_list = daemon->force_broadcast; id_list; id_list = id_list->next)
	if ((!id_list->list) || match_netid(id_list->list, netid, 0))
	  break;
      if (id_list)
	mess->flags |= htons(0x8000); /* force broadcast */
      
      if (context)
	mess->siaddr = context->local;
     
      /* See if we can send the boot stuff as options.
	 To do this we need a requested option list, BOOTP
	 and very old DHCP clients won't have this, we also 
	 provide a manual option to disable it.
	 Some PXE ROMs have bugs (surprise!) and need zero-terminated 
	 names, so we always send those.  */
      if ((boot = find_boot(tagif)))
	{
	  if (boot->sname)
	    {	  
	      if (!option_bool(OPT_NO_OVERRIDE) &&
		  req_options && 
		  in_list(req_options, OPTION_SNAME))
		option_put_string(mess, end, OPTION_SNAME, boot->sname, 1);
	      else
		safe_strncpy((char *)mess->sname, boot->sname, sizeof(mess->sname));
	    }
	  
	  if (boot->file)
	    {
	      if (!option_bool(OPT_NO_OVERRIDE) &&
		  req_options && 
		  in_list(req_options, OPTION_FILENAME))
		option_put_string(mess, end, OPTION_FILENAME, boot->file, 1);
	      else
		safe_strncpy((char *)mess->file, boot->file, sizeof(mess->file));
	    }
	  
	  if (boot->next_server.s_addr) 
	    mess->siaddr = boot->next_server;
	  else if (boot->tftp_sname)
	    mess->siaddr = a_record_from_hosts(boot->tftp_sname, now);
	}
      else
	/* Use the values of the relevant options if no dhcp-boot given and
	   they're not explicitly asked for as options. OPTION_END is used
	   as an internal way to specify siaddr without using dhcp-boot, for use in
	   dhcp-optsfile. */
	{
	  if ((!req_options || !in_list(req_options, OPTION_FILENAME)) &&
	      (opt = option_find2(OPTION_FILENAME)) && !(opt->flags & DHOPT_FORCE))
	    {
	      safe_strncpy((char *)mess->file, (char *)opt->val, sizeof(mess->file));
	      done_file = 1;
	    }
	  
	  if ((!req_options || !in_list(req_options, OPTION_SNAME)) &&
	      (opt = option_find2(OPTION_SNAME)) && !(opt->flags & DHOPT_FORCE))
	    {
	      safe_strncpy((char *)mess->sname, (char *)opt->val, sizeof(mess->sname));
	      done_server = 1;
	    }
	  
	  if ((opt = option_find2(OPTION_END)))
	    mess->siaddr.s_addr = ((struct in_addr *)opt->val)->s_addr;	
	}
    }
        
  /* We don't want to do option-overload for BOOTP, so make the file and sname
     fields look like they are in use, even when they aren't. This gets restored
     at the end of this function. */

  if (!req_options || option_bool(OPT_NO_OVERRIDE))
    {
      f0 = mess->file[0];
      mess->file[0] = 1;
      s0 = mess->sname[0];
      mess->sname[0] = 1;
    }
      
  /* At this point, if mess->sname or mess->file are zeroed, they are available
     for option overload, reserve space for the overload option. */
  if (mess->file[0] == 0 || mess->sname[0] == 0)
    end -= 3;

  /* rfc3011 says this doesn't need to be in the requested options list. */
  if (!leasequery && subnet_addr.s_addr)
    option_put(mess, end, OPTION_SUBNET_SELECT, INADDRSZ, ntohl(subnet_addr.s_addr));
   
  if (lease_time != 0xffffffff)
    { 
      unsigned int t1val = lease_time/2; 
      unsigned int t2val = (lease_time*7)/8;
      unsigned int hval;
      
      /* If set by user, sanity check, so not longer than lease. */
      if ((opt = option_find2(OPTION_T1)))
	{
	  hval = ntohl(*((unsigned int *)opt->val));
	  if (hval < lease_time && hval > 2)
	    t1val = hval;
	}

       if ((opt = option_find2(OPTION_T2)))
	{
	  hval = ntohl(*((unsigned int *)opt->val));
	  if (hval < lease_time && hval > 2)
	    t2val = hval;
	}
       	  
       /* ensure T1 is still < T2 */
       if (t2val <= t1val)
	 t1val = t2val - 1; 

       while (fuzz > (t1val/8))
	 fuzz = fuzz/2;
	 
       t1val -= fuzz;
       t2val -= fuzz;
       
       option_put(mess, end, OPTION_T1, 4, t1val);
       option_put(mess, end, OPTION_T2, 4, t2val);
    }

  /* replies to DHCPINFORM may not have a valid context */
  if (context)
    {
      /* Netmask and broadcast always sent, except leasequery. */
      if (!option_find2(OPTION_NETMASK) &&
	  (!leasequery || in_list(req_options, OPTION_NETMASK)))
	option_put(mess, end, OPTION_NETMASK, INADDRSZ, ntohl(context->netmask.s_addr));
      
      /* May not have a "guessed" broadcast address if we got no packets via a relay
	 from this net yet (ie just unicast renewals after a restart */
      if (context->broadcast.s_addr &&
	  !option_find2(OPTION_BROADCAST) &&
	  (!leasequery || in_list(req_options, OPTION_BROADCAST)))
    	option_put(mess, end, OPTION_BROADCAST, INADDRSZ, ntohl(context->broadcast.s_addr));
      
      /* Same comments as broadcast apply, and also may not be able to get a sensible
	 default when using subnet select.  User must configure by steam in that case. */
      if (context->router.s_addr &&
	  in_list(req_options, OPTION_ROUTER) &&
	  !option_find2(OPTION_ROUTER))
	option_put(mess, end, OPTION_ROUTER, INADDRSZ, ntohl(context->router.s_addr));
      
      if (daemon->port == NAMESERVER_PORT &&
	  in_list(req_options, OPTION_DNSSERVER) &&
	  !option_find2(OPTION_DNSSERVER))
	option_put(mess, end, OPTION_DNSSERVER, INADDRSZ, ntohl(context->local.s_addr));
    }

  if (domain && in_list(req_options, OPTION_DOMAINNAME) && 
      !option_find2(OPTION_DOMAINNAME))
    option_put_string(mess, end, OPTION_DOMAINNAME, domain, null_term);
 
  /* Note that we ignore attempts to set the fqdn using --dhc-option=81,<name> */
  if (hostname)
    {
      if (in_list(req_options, OPTION_HOSTNAME) &&
	  !option_find2(OPTION_HOSTNAME))
	option_put_string(mess, end, OPTION_HOSTNAME, hostname, null_term);
      
      if (fqdn_flags != 0)
	{
	  len = strlen(hostname) + 3;
	  
	  if (fqdn_flags & 0x04)
	    len += 2;
	  else if (null_term)
	    len++;

	  if (domain)
	    len += strlen(domain) + 1;
	  else if (fqdn_flags & 0x04)
	    len--;

	  if ((p = free_space(mess, end, OPTION_CLIENT_FQDN, len)))
	    {
	      *(p++) = fqdn_flags & 0x0f; /* MBZ bits to zero */ 
	      *(p++) = 255;
	      *(p++) = 255;

	      if (fqdn_flags & 0x04)
		{
		  p = do_rfc1035_name(p, hostname, NULL);
		  if (domain)
		    {
		      p = do_rfc1035_name(p, domain, NULL);
		      *p++ = 0;
		    }
		}
	      else
		{
		  memcpy(p, hostname, strlen(hostname));
		  p += strlen(hostname);
		  if (domain)
		    {
		      *(p++) = '.';
		      memcpy(p, domain, strlen(domain));
		      p += strlen(domain);
		    }
		  if (null_term)
		    *(p++) = 0;
		}
	    }
	}
    }      

  for (opt = config_opts; opt; opt = opt->next)
    {
      int optno = opt->opt;

      /* netids match and not encapsulated? */
      if (!(opt->flags & DHOPT_TAGOK))
	continue;
      
      /* was it asked for, or are we sending it anyway? */
      if ((!(opt->flags & DHOPT_FORCE) || leasequery) && !in_list(req_options, optno))
	continue;
      
      /* prohibit some used-internally options. T1 and T2 already handled. */
      if (optno == OPTION_CLIENT_FQDN ||
	  optno == OPTION_MAXMESSAGE ||
	  optno == OPTION_OVERLOAD ||
	  optno == OPTION_PAD ||
	  optno == OPTION_END ||
	  optno == OPTION_T1 ||
	  optno == OPTION_T2)
	continue;

      if (optno == OPTION_SNAME && done_server)
	continue;

      if (optno == OPTION_FILENAME && done_file)
	continue;
      
      /* For the options we have default values on
	 dhc-option=<optionno> means "don't include this option"
	 not "include a zero-length option" */
      if (opt->len == 0 && 
	  (optno == OPTION_NETMASK ||
	   optno == OPTION_BROADCAST ||
	   optno == OPTION_ROUTER ||
	   optno == OPTION_DNSSERVER || 
	   optno == OPTION_DOMAINNAME ||
	   optno == OPTION_HOSTNAME))
	continue;

      /* vendor-class comes from elsewhere for PXE */
      if (pxe_arch != -1 && optno == OPTION_VENDOR_ID)
	continue;
      
      /* always force null-term for filename and servername - buggy PXE again. */
      len = do_opt(opt, NULL, context, 
		   (optno == OPTION_SNAME || optno == OPTION_FILENAME) ? 1 : null_term);

      if ((p = free_space(mess, end, optno, len)))
	{
	  do_opt(opt, p, context, 
		 (optno == OPTION_SNAME || optno == OPTION_FILENAME) ? 1 : null_term);
	  
	  /* If we send a vendor-id, revisit which vendor-ops we consider 
	     it appropriate to send. */
	  if (optno == OPTION_VENDOR_ID)
	    {
	      match_vendor_opts(p - 2, config_opts);
	      done_vendor_class = 1;
	    }
	}  
    }

  /* encapsulated options. */
  handle_encap(mess, end, req_options, null_term, tagif, pxe_arch != 1);
  
  force_encap = prune_vendor_opts(tagif);
    
  if (context && pxe_arch != -1)
    {
      pxe_misc(mess, end, uuid, pxevendor);
      if (!pxe_uefi_workaround(pxe_arch, tagif, mess, context->local, now, 0))
	config_opts = pxe_opts(pxe_arch, tagif, context->local, now);
    }

  if ((force_encap || in_list(req_options, OPTION_VENDOR_CLASS_OPT) || in_list(req_options, OPTION_VENDOR_ID)) &&
      (leasequery || do_encap_opts(config_opts, OPTION_VENDOR_CLASS_OPT, DHOPT_VENDOR_MATCH, mess, end, null_term)) && 
      pxe_arch == -1 && !done_vendor_class && vendor_class_len != 0 &&
      (p = free_space(mess, end, OPTION_VENDOR_ID, vendor_class_len)))
    /* If we send vendor encapsulated options, and haven't already sent option 60,
       echo back the value we got from the client. */
    memcpy(p, daemon->dhcp_buff3, vendor_class_len);	    
   
   /* restore BOOTP anti-overload hack */
  if (!req_options || option_bool(OPT_NO_OVERRIDE))
    {
      mess->file[0] = f0;
      mess->sname[0] = s0;
    }
}

static void handle_encap(struct dhcp_packet *mess, unsigned char *end, unsigned char *req_options,
			 int null_term, struct dhcp_netid *tagif, int pxemode)
{
  /* Send options to be encapsulated in arbitrary options, 
     eg dhcp-option=encap:172,17,.......
     Also handle vendor-identifying vendor-encapsulated options,
     dhcp-option = vi-encap:13,17,.......
     The may be more that one "outer" to do, so group
     all the options which match each outer in turn. */

  struct dhcp_opt *opt, *config_opts = daemon->dhcp_opts;
  unsigned char *p;
  int len;

  for (opt = config_opts; opt; opt = opt->next)
    opt->flags &= ~DHOPT_ENCAP_DONE;
  
  for (opt = config_opts; opt; opt = opt->next)
    {
      int flags;

      if ((flags = (opt->flags & (DHOPT_ENCAPSULATE | DHOPT_RFC3925))))
	{
	  int found = 0;
	  struct dhcp_opt *o;

	  if (opt->flags & DHOPT_ENCAP_DONE)
	    continue;

	  for (len = 0, o = config_opts; o; o = o->next)
	    {
	      int outer = flags & DHOPT_ENCAPSULATE ? o->u.encap : OPTION_VENDOR_IDENT_OPT;

	      o->flags &= ~DHOPT_ENCAP_MATCH;
	      
	      if (!(o->flags & flags) || opt->u.encap != o->u.encap)
		continue;
	      
	      o->flags |= DHOPT_ENCAP_DONE;
	      if (match_netid(o->netid, tagif, 1) &&
		  pxe_ok(o, pxemode) &&
		  ((o->flags & DHOPT_FORCE) || in_list(req_options, outer)))
		{
		  o->flags |= DHOPT_ENCAP_MATCH;
		  found = 1;
		  len += do_opt(o, NULL, NULL, 0) + 2;
		}
	    } 
	  
	  if (found)
	    { 
	      if (flags & DHOPT_ENCAPSULATE)
		do_encap_opts(config_opts, opt->u.encap, DHOPT_ENCAP_MATCH, mess, end, null_term);
	      else if (len > 250)
		my_syslog(MS_DHCP | LOG_WARNING, _("cannot send RFC3925 option: too many options for enterprise number %d"), opt->u.encap);
	      else if ((p = free_space(mess, end,  OPTION_VENDOR_IDENT_OPT, len + 5)))
		{
		  int swap_ent = htonl(opt->u.encap);
		  memcpy(p, &swap_ent, 4);
		  p += 4;
		  *(p++) = len;
		  for (o = config_opts; o; o = o->next)
		    if (o->flags & DHOPT_ENCAP_MATCH)
		      {
			len = do_opt(o, p + 2, NULL, 0);
			*(p++) = o->opt;
			*(p++) = len;
			p += len;
		      }     
		}
	    }
	}
    }      
}

static void apply_delay(u32 xid, time_t recvtime, struct dhcp_netid *netid)
{
  struct delay_config *delay_conf;
  
  /* Decide which delay_config option we're using */
  for (delay_conf = daemon->delay_conf; delay_conf; delay_conf = delay_conf->next)
    if (match_netid(delay_conf->netid, netid, 0))
      break;
  
  if (!delay_conf)
    /* No match, look for one without a netid */
    for (delay_conf = daemon->delay_conf; delay_conf; delay_conf = delay_conf->next)
      if (match_netid(delay_conf->netid, netid, 1))
        break;

  if (delay_conf)
    {
      if (!option_bool(OPT_QUIET_DHCP))
	my_syslog(MS_DHCP | LOG_INFO, _("%u reply delay: %d"), ntohl(xid), delay_conf->delay);
      delay_dhcp(recvtime, delay_conf->delay, -1, 0, 0);
    }
}

void relay_upstream4(int iface_index, struct dhcp_packet *mess, size_t sz, int unicast)
{
  struct in_addr giaddr = mess->giaddr;
  u8 hops = mess->hops;
  struct dhcp_relay *relay;
  size_t orig_sz = sz;
  unsigned char *endopt = NULL;
    
  if (mess->op != BOOTREQUEST)
    return;

  for (relay = daemon->relay4; relay; relay = relay->next)
    if (relay->iface_index != 0 && relay->iface_index == iface_index)
      {
	union mysockaddr to;
	union all_addr from;
	struct ifreq ifr;
	
	/* restore orig packet */
	mess->hops = hops;
	mess->giaddr = giaddr;
	if (endopt)
	  *endopt = OPTION_END;
	sz = orig_sz;
	
	if ((mess->hops++) > 20)
	  continue;

	if (relay->interface)
	  {
	    safe_strncpy(ifr.ifr_name, relay->interface, IF_NAMESIZE);
	    ifr.ifr_addr.sa_family = AF_INET;
	  }
	
	if (!relay->split_mode)
	  {
	    /* already gatewayed ? */
	    if (giaddr.s_addr)
	      {
		/* if so check if by us, to stomp on loops. */
		if (giaddr.s_addr == relay->local.addr4.s_addr)
		  continue;
	      }

	    /* plug in our address */
	    from.addr4 = mess->giaddr = relay->local.addr4;
	  }
	else
	  {
	    /* Split mode. We put our address on the server-facing interface
	       into giaddr for the server to talk back to us on.

	       Our address on client-facing interface goes into agent-id
	       subnet-selector subopt, so that the server allocates the correct address. */
	    
	    /* get our address on the server-facing interface. */
	    if (ioctl(daemon->dhcpfd, SIOCGIFADDR, &ifr) == -1)
	      continue;
	    
	    /* already gatewayed ? */
	    if (giaddr.s_addr)
	      {
		/* if so check if by us, to stomp on loops. */
		if (giaddr.s_addr == ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr.s_addr)
		  continue;
	      }
	    
	    /* giaddr is our address on the outgoing interface in split mode. */
	    from.addr4 = mess->giaddr = ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
	    
	    if (!endopt)
	      {
		/* Add an RFC3026 relay agent information option (2 bytes) at the very end of the options.
		   Said option to contain a RFC 3527 link selection sub option (6 bytes) and
		   RFC 5017 serverid-override option (6 bytes) and RFC5010 (3 bytes).
		   New END option is a 18th byte, so we need 18 bytes free.
		   We only need to do this once, and poke the address into the same place each time. */
		
		if (!(endopt = option_find1((&mess->options[0] + sizeof(u32)), ((unsigned char *)mess) + sz, OPTION_END, 0)) ||
		    (endopt + 18 > (unsigned char *)(mess + 1)))
		  continue;
		
		endopt[1] = 15; /* length */
		endopt[2] = SUBOPT_SUBNET_SELECT;
		endopt[3] = 4; /* length */
		endopt[8] = SUBOPT_SERVER_OR;
		endopt[9] = 4;
		endopt[14] = SUBOPT_FLAGS;
		endopt[15] = 1; /* length */
		endopt[17] = OPTION_END;
		sz = (endopt - (unsigned char *)mess) + 18;
	      }
	    
	    /* IP address is already in network byte order */
	    memcpy(&endopt[4], &relay->local.addr4.s_addr, INADDRSZ);
	    memcpy(&endopt[10], &relay->local.addr4.s_addr, INADDRSZ);
	    endopt[16] = unicast ? 0x80 : 0x00;
	    endopt[0] = OPTION_AGENT_ID;
	  }
	
	to.sa.sa_family = AF_INET;
	to.in.sin_addr = relay->server.addr4;
	to.in.sin_port = htons(relay->port);
#ifdef HAVE_SOCKADDR_SA_LEN
	to.in.sin_len = sizeof(struct sockaddr_in);
#endif
	
	/* Broadcasting to server. */
	if (relay->server.addr4.s_addr == 0)
	  {
	    if (!relay->interface || strchr(relay->interface, '*') ||
		ioctl(daemon->dhcpfd, SIOCGIFBRDADDR, &ifr) == -1)
	      {
		my_syslog(MS_DHCP | LOG_ERR, _("Cannot broadcast DHCP relay via interface %s"), relay->interface);
		continue;
	      }
	    
	    to.in.sin_addr = ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
	  }
	
#ifdef HAVE_DUMPFILE
	{
	  union mysockaddr fromsock;
	  fromsock.in.sin_port = htons(daemon->dhcp_server_port);
	  fromsock.in.sin_addr = from.addr4;
	  fromsock.sa.sa_family = AF_INET;

	  dump_packet_udp(DUMP_DHCP, (void *)mess, sz, &fromsock, &to, -1);
	}
#endif
	
	 send_from(daemon->dhcpfd, 0, (char *)mess, sz, &to, &from, 0);
	 
	 if (option_bool(OPT_LOG_OPTS))
	   {
	     inet_ntop(AF_INET, &relay->local, daemon->addrbuff, ADDRSTRLEN);
	     if (relay->server.addr4.s_addr == 0)
	       snprintf(daemon->dhcp_buff2, DHCP_BUFF_SZ, _("broadcast via %s"), relay->interface);
	     else
	       inet_ntop(AF_INET, &relay->server.addr4, daemon->dhcp_buff2, DHCP_BUFF_SZ);
	     my_syslog(MS_DHCP | LOG_INFO, _("DHCP relay at %s -> %s"), daemon->addrbuff, daemon->dhcp_buff2);
	   }
      }
  
  /* restore in case of a local reply. */
  mess->hops = hops;
  mess->giaddr = giaddr;
  if (endopt)
    *endopt = OPTION_END;
}

struct dhcp_relay *relay_reply4(struct dhcp_packet *mess, char *arrival_interface)
{
  struct dhcp_relay *relay;

  if (mess->giaddr.s_addr == 0 || mess->op != BOOTREPLY)
    return NULL;

  for (relay = daemon->relay4; relay; relay = relay->next)
    {
      if (relay->split_mode)
	{
	  struct ifreq ifr;

	  safe_strncpy(ifr.ifr_name, arrival_interface, IF_NAMESIZE);
	  ifr.ifr_addr.sa_family = AF_INET;

	  /* giaddr is our address on the returning interface in split mode. */
	  if (ioctl(daemon->dhcpfd, SIOCGIFADDR, &ifr) == -1 ||
	      mess->giaddr.s_addr != ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr.s_addr)
	    continue;
	}
      else if (mess->giaddr.s_addr != relay->local.addr4.s_addr)
	continue;
      
      if (!relay->interface || wildcard_match(relay->interface, arrival_interface))
	return relay->iface_index != 0 ? relay : NULL;
    }
  
  return NULL;	 
}     


#endif /* HAVE_DHCP */
