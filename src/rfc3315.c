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

#ifdef HAVE_DHCP6

struct state {
  unsigned char *clid;
  int multicast_dest, clid_len, ia_type, interface, hostname_auth, lease_allocate;
  char *client_hostname, *hostname, *domain, *send_domain;
  struct dhcp_context *context;
  struct in6_addr *link_address, *fallback, *ll_addr, *ula_addr;
  unsigned int xid, fqdn_flags, iaid;
  char *iface_name;
  void *packet_options, *end;
  struct dhcp_netid *tags, *context_tags;
  unsigned char mac[DHCP_CHADDR_MAX];
  unsigned int mac_len, mac_type;
};

static int dhcp6_maybe_relay(struct state *state, unsigned char *inbuff, size_t sz, 
			     struct in6_addr *client_addr, int is_unicast, time_t now);
static int dhcp6_no_relay(struct state *state, int msg_type, unsigned char *inbuff, size_t sz, int is_unicast, time_t now);
static void log6_opts(int nest, unsigned int xid, void *start_opts, void *end_opts);
static void log6_packet(struct state *state, char *type, struct in6_addr *addr, char *string);
static void log6_quiet(struct state *state, char *type, struct in6_addr *addr, char *string);
static void *opt6_find (uint8_t *opts, uint8_t *end, unsigned int search, unsigned int minsize);
static void *opt6_next(uint8_t *opts, uint8_t *end);
static unsigned int opt6_uint(unsigned char *opt, int offset, int size);
static void get_context_tag(struct state *state, struct dhcp_context *context);
static int check_ia(struct state *state, void *opt, void **endp, void **ia_option);
static int build_ia(struct state *state, int *t1cntr);
static void end_ia(int t1cntr, unsigned int min_time, int do_fuzz);
static void mark_context_used(struct state *state, struct in6_addr *addr);
static void mark_config_used(struct dhcp_context *context, struct in6_addr *addr);
static int check_address(struct state *state, struct in6_addr *addr);
static int config_valid(struct dhcp_config *config, struct dhcp_context *context, struct in6_addr *addr, struct state *state, time_t now);
static struct addrlist *config_implies(struct dhcp_config *config, struct dhcp_context *context, struct in6_addr *addr);
static void add_address(struct state *state, struct dhcp_context *context, unsigned int lease_time, void *ia_option, 
			unsigned int *min_time, struct in6_addr *addr, time_t now);
static void update_leases(struct state *state, struct dhcp_context *context, struct in6_addr *addr, unsigned int lease_time, time_t now);
static int add_local_addrs(struct dhcp_context *context);
static struct dhcp_netid *add_options(struct state *state, int do_refresh);
static void calculate_times(struct dhcp_context *context, unsigned int *min_time, unsigned int *valid_timep, 
			    unsigned int *preferred_timep, unsigned int lease_time);

#define opt6_len(opt) ((int)(opt6_uint(opt, -2, 2)))
#define opt6_type(opt) (opt6_uint(opt, -4, 2))
#define opt6_ptr(opt, i) ((void *)&(((uint8_t *)(opt))[4+(i)]))

#define opt6_user_vendor_ptr(opt, i) ((void *)&(((uint8_t *)(opt))[2+(i)]))
#define opt6_user_vendor_len(opt) ((int)(opt6_uint(opt, -4, 2)))
#define opt6_user_vendor_next(opt, end) (opt6_next(((uint8_t *) opt) - 2, end))
 

unsigned short dhcp6_reply(struct dhcp_context *context, int multicast_dest, int interface, char *iface_name,
			   struct in6_addr *fallback,  struct in6_addr *ll_addr, struct in6_addr *ula_addr,
			   size_t sz, struct in6_addr *client_addr, time_t now)
{
  struct dhcp_vendor *vendor;
  int msg_type;
  struct state state;
  
  if (sz <= 4)
    return 0;
  
  msg_type = *((unsigned char *)daemon->dhcp_packet.iov_base);
  
  /* Mark these so we only match each at most once, to avoid tangled linked lists */
  for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
    vendor->netid.next = &vendor->netid;
  
  reset_counter();
  state.context = context;
  state.multicast_dest = multicast_dest;
  state.interface = interface;
  state.iface_name = iface_name;
  state.fallback = fallback;
  state.ll_addr = ll_addr;
  state.ula_addr = ula_addr;
  state.mac_len = 0;
  state.tags = NULL;
  state.link_address = NULL;

  if (dhcp6_maybe_relay(&state, daemon->dhcp_packet.iov_base, sz, client_addr, 
			IN6_IS_ADDR_MULTICAST(client_addr), now))
    return msg_type == DHCP6RELAYFORW ? DHCPV6_SERVER_PORT : DHCPV6_CLIENT_PORT;

  return 0;
}

/* This cost me blood to write, it will probably cost you blood to understand - srk. */
static int dhcp6_maybe_relay(struct state *state, unsigned char *inbuff, size_t sz, 
			     struct in6_addr *client_addr, int is_unicast, time_t now)
{
  uint8_t *end = inbuff + sz;
  uint8_t *opts = inbuff + 34;
  int msg_type = *inbuff;
  unsigned char *outmsgtypep;
  uint8_t *opt;
  struct dhcp_vendor *vendor;

  /* if not an encapsulated relayed message, just do the stuff */
  if (msg_type != DHCP6RELAYFORW)
    {
      /* if link_address != NULL if points to the link address field of the 
	 innermost nested RELAYFORW message, which is where we find the
	 address of the network on which we can allocate an address.
	 Recalculate the available contexts using that information. 

      link_address == NULL means there's no relay in use, so we try and find the client's 
      MAC address from the local ND cache. */
      
      if (!state->link_address)
	get_client_mac(client_addr, state->interface, state->mac, &state->mac_len, &state->mac_type, now);
      else
	{
	  struct dhcp_context *c;
	  struct shared_network *share = NULL;
	  state->context = NULL;

	  if (!IN6_IS_ADDR_LOOPBACK(state->link_address) &&
	      !IN6_IS_ADDR_LINKLOCAL(state->link_address) &&
	      !IN6_IS_ADDR_MULTICAST(state->link_address))
	    for (c = daemon->dhcp6; c; c = c->next)
	      {
		for (share = daemon->shared_networks; share; share = share->next)
		  {
		    if (share->shared_addr.s_addr != 0)
		      continue;
		    
		    if (share->if_index != 0 ||
			!IN6_ARE_ADDR_EQUAL(state->link_address, &share->match_addr6))
		      continue;
		    
		    if ((c->flags & CONTEXT_DHCP) &&
			!(c->flags & (CONTEXT_TEMPLATE | CONTEXT_OLD)) &&
			is_same_net6(&share->shared_addr6, &c->start6, c->prefix) &&
			is_same_net6(&share->shared_addr6, &c->end6, c->prefix))
		      break;
		  }
		
		if (share ||
		    ((c->flags & CONTEXT_DHCP) &&
		     !(c->flags & (CONTEXT_TEMPLATE | CONTEXT_OLD)) &&
		     is_same_net6(state->link_address, &c->start6, c->prefix) &&
		     is_same_net6(state->link_address, &c->end6, c->prefix)))
		  {
		    c->preferred = c->valid = 0xffffffff;
		    c->current = state->context;
		    state->context = c;
		  }
	      }
	  
	  if (!state->context)
	    {
	      inet_ntop(AF_INET6, state->link_address, daemon->addrbuff, ADDRSTRLEN); 
	      my_syslog(MS_DHCP | LOG_WARNING, 
			_("no address range available for DHCPv6 request from relay at %s"),
			daemon->addrbuff);
	      return 0;
	    }
	}
	  
      if (!state->context)
	{
	  my_syslog(MS_DHCP | LOG_WARNING, 
		    _("no address range available for DHCPv6 request via %s"), state->iface_name);
	  return 0;
	}

      return dhcp6_no_relay(state, msg_type, inbuff, sz, is_unicast, now);
    }

  /* must have at least msg_type+hopcount+link_address+peer_address+minimal size option
     which is               1   +    1   +    16      +     16     + 2 + 2 = 38 */
  if (sz < 38)
    return 0;
  
  /* copy header stuff into reply message and set type to reply */
  if (!(outmsgtypep = put_opt6(inbuff, 34)))
    return 0;
  *outmsgtypep = DHCP6RELAYREPL;

  /* look for relay options and set tags if found. */
  for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
    {
      int mopt;
      
      if (vendor->match_type == MATCH_SUBSCRIBER)
	mopt = OPTION6_SUBSCRIBER_ID;
      else if (vendor->match_type == MATCH_REMOTE)
	mopt = OPTION6_REMOTE_ID; 
      else
	continue;

      if ((opt = opt6_find(opts, end, mopt, 1)) &&
	  vendor->len == opt6_len(opt) &&
	  memcmp(vendor->data, opt6_ptr(opt, 0), vendor->len) == 0 &&
	  vendor->netid.next != &vendor->netid)
	{
	  vendor->netid.next = state->tags;
	  state->tags = &vendor->netid;
	  break;
	}
    }
  
  /* RFC-6939 */
  if ((opt = opt6_find(opts, end, OPTION6_CLIENT_MAC, 3)))
    {
      if (opt6_len(opt) - 2 > DHCP_CHADDR_MAX) {
        return 0;
      }
      state->mac_type = opt6_uint(opt, 0, 2);
      state->mac_len = opt6_len(opt) - 2;
      memcpy(&state->mac[0], opt6_ptr(opt, 2), state->mac_len);
    }
  
  for (opt = opts; opt; opt = opt6_next(opt, end))
    {
      if ((uint8_t *)opt6_ptr(opt, 0) + opt6_len(opt) > end)
        return 0;
     
      /* Don't copy MAC address into reply. */
      if (opt6_type(opt) != OPTION6_CLIENT_MAC)
	{
	  int o = new_opt6(opt6_type(opt));
	  if (opt6_type(opt) == OPTION6_RELAY_MSG)
	    {
	      struct in6_addr align;
	      /* the packet data is unaligned, copy to aligned storage */
	      memcpy(&align, inbuff + 2, IN6ADDRSZ); 


	      /* RFC6221 para 4 */
	      if (!IN6_IS_ADDR_UNSPECIFIED(&align))
		state->link_address = &align;
	      /* zero is_unicast since that is now known to refer to the 
		 relayed packet, not the original sent by the client */
	      if (!dhcp6_maybe_relay(state, opt6_ptr(opt, 0), opt6_len(opt), client_addr, 0, now))
		return 0;
	    }
	  else
	    put_opt6(opt6_ptr(opt, 0), opt6_len(opt));
	  end_opt6(o);
	}
    }
  
  return 1;
}

static int dhcp6_no_relay(struct state *state, int msg_type, unsigned char *inbuff, size_t sz, int is_unicast, time_t now)
{
  void *opt;
  int i, o, o1, start_opts, start_msg;
  struct dhcp_opt *opt_cfg;
  struct dhcp_netid *tagif;
  struct dhcp_config *config = NULL;
  struct dhcp_netid known_id, iface_id, v6_id;
  unsigned char outmsgtype;
  struct dhcp_vendor *vendor;
  struct dhcp_context *context_tmp;
  struct dhcp_mac *mac_opt;
  unsigned int ignore = 0;

  state->packet_options = inbuff + 4;
  state->end = inbuff + sz;
  state->clid = NULL;
  state->clid_len = 0;
  state->lease_allocate = 0;
  state->context_tags = NULL;
  state->domain = NULL;
  state->send_domain = NULL;
  state->hostname_auth = 0;
  state->hostname = NULL;
  state->client_hostname = NULL;
  state->fqdn_flags = 0x01; /* default to send if we receive no FQDN option */

  /* set tag with name == interface */
  iface_id.net = state->iface_name;
  iface_id.next = state->tags;
  state->tags = &iface_id; 

  /* set tag "dhcpv6" */
  v6_id.net = "dhcpv6";
  v6_id.next = state->tags;
  state->tags = &v6_id;

  start_msg = save_counter(-1);
  /* copy over transaction-id */
  if (!put_opt6(inbuff, 4))
    return 0;
  start_opts = save_counter(-1);
  state->xid = inbuff[3] | inbuff[2] << 8 | inbuff[1] << 16;
    
  /* We're going to be linking tags from all context we use. 
     mark them as unused so we don't link one twice and break the list */
  for (context_tmp = state->context; context_tmp; context_tmp = context_tmp->current)
    {
      context_tmp->netid.next = &context_tmp->netid;

      if (option_bool(OPT_LOG_OPTS))
	{
	   inet_ntop(AF_INET6, &context_tmp->start6, daemon->dhcp_buff, ADDRSTRLEN); 
	   inet_ntop(AF_INET6, &context_tmp->end6, daemon->dhcp_buff2, ADDRSTRLEN); 
	   if (context_tmp->flags & (CONTEXT_STATIC))
	     my_syslog(MS_DHCP | LOG_INFO, _("%u available DHCPv6 subnet: %s/%d"),
		       state->xid, daemon->dhcp_buff, context_tmp->prefix);
	   else
	     my_syslog(MS_DHCP | LOG_INFO, _("%u available DHCP range: %s -- %s"), 
		       state->xid, daemon->dhcp_buff, daemon->dhcp_buff2);
	}
    }

  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_CLIENT_ID, 1)))
    {
      state->clid = opt6_ptr(opt, 0);
      state->clid_len = opt6_len(opt);
      o = new_opt6(OPTION6_CLIENT_ID);
      put_opt6(state->clid, state->clid_len);
      end_opt6(o);
    }
  else if (msg_type != DHCP6IREQ)
    return 0;

  opt = opt6_find(state->packet_options, state->end, OPTION6_SERVER_ID, 1);
  
  if (msg_type == DHCP6SOLICIT || msg_type == DHCP6CONFIRM || msg_type == DHCP6REBIND || msg_type == DHCP6IREQ)
    {
      /* Above message types must be multicast 3315 Section 15. */
      if (!state->multicast_dest)
	return 0;

      /* server-id must match except for SOLICIT, CONFIRM and REBIND messages, which MUST NOT
	 have a server-id.  3315 para 15.x */
      if (msg_type == DHCP6IREQ)
	{
	  /* If server-id provided in IREQ, it must match. */
	  if (opt && (opt6_len(opt) != daemon->duid_len ||
		      memcmp(opt6_ptr(opt, 0), daemon->duid, daemon->duid_len) != 0))
	    return 0;
	}
      else if (opt) 
	return 0;
    }
  else
    {
      /* Everything else MUST have a server-id that matches ours. */
      if (!opt || opt6_len(opt) != daemon->duid_len ||
	  memcmp(opt6_ptr(opt, 0), daemon->duid, daemon->duid_len) != 0)
	return 0;
    }
  
  o = new_opt6(OPTION6_SERVER_ID);
  put_opt6(daemon->duid, daemon->duid_len);
  end_opt6(o);

  if (is_unicast &&
      (msg_type == DHCP6REQUEST || msg_type == DHCP6RENEW || msg_type == DHCP6RELEASE || msg_type == DHCP6DECLINE))
    
    {  
      outmsgtype = DHCP6REPLY;
      o1 = new_opt6(OPTION6_STATUS_CODE);
      put_opt6_short(DHCP6USEMULTI);
      put_opt6_string("Use multicast");
      end_opt6(o1);
      goto done;
    }

  /* match vendor and user class options */
  for (vendor = daemon->dhcp_vendors; vendor; vendor = vendor->next)
    {
      int mopt;
      
      if (vendor->match_type == MATCH_VENDOR)
	mopt = OPTION6_VENDOR_CLASS;
      else if (vendor->match_type == MATCH_USER)
	mopt = OPTION6_USER_CLASS; 
      else
	continue;

      if ((opt = opt6_find(state->packet_options, state->end, mopt, 2)))
	{
	  void *enc_opt, *enc_end = opt6_ptr(opt, opt6_len(opt));
	  int offset = 0;
	  
	  if (mopt == OPTION6_VENDOR_CLASS)
	    {
	      if (opt6_len(opt) < 4)
		continue;
	      
	      if (vendor->enterprise != opt6_uint(opt, 0, 4))
		continue;
	    
	      offset = 4;
	    }
 
	  /* Note that format if user/vendor classes is different to DHCP options - no option types. */
	  for (enc_opt = opt6_ptr(opt, offset); enc_opt; enc_opt = opt6_user_vendor_next(enc_opt, enc_end))
	    for (i = 0; i <= (opt6_user_vendor_len(enc_opt) - vendor->len); i++)
	      if (memcmp(vendor->data, opt6_user_vendor_ptr(enc_opt, i), vendor->len) == 0)
		{
		  vendor->netid.next = state->tags;
		  state->tags = &vendor->netid;
		  break;
		}
	}
    }

  if (option_bool(OPT_LOG_OPTS) && (opt = opt6_find(state->packet_options, state->end, OPTION6_VENDOR_CLASS, 4)))
    my_syslog(MS_DHCP | LOG_INFO, _("%u vendor class: %u"), state->xid, opt6_uint(opt, 0, 4));
  
  /* dhcp-match. If we have hex-and-wildcards, look for a left-anchored match.
     Otherwise assume the option is an array, and look for a matching element. 
     If no data given, existence of the option is enough. This code handles 
     V-I opts too. */
  for (opt_cfg = daemon->dhcp_match6; opt_cfg; opt_cfg = opt_cfg->next)
    {
      int match = 0;
      
      if (opt_cfg->flags & DHOPT_RFC3925)
	{
	  for (opt = opt6_find(state->packet_options, state->end, OPTION6_VENDOR_OPTS, 4);
	       opt;
	       opt = opt6_find(opt6_next(opt, state->end), state->end, OPTION6_VENDOR_OPTS, 4))
	    {
	      void *vopt;
	      void *vend = opt6_ptr(opt, opt6_len(opt));
	      
	      for (vopt = opt6_find(opt6_ptr(opt, 4), vend, opt_cfg->opt, 0);
		   vopt;
		   vopt = opt6_find(opt6_next(vopt, vend), vend, opt_cfg->opt, 0))
		if ((match = match_bytes(opt_cfg, opt6_ptr(vopt, 0), opt6_len(vopt))))
		  break;
	    }
	  if (match)
	    break;
	}
      else
	{
	  if (!(opt = opt6_find(state->packet_options, state->end, opt_cfg->opt, 1)))
	    continue;
	  
	  match = match_bytes(opt_cfg, opt6_ptr(opt, 0), opt6_len(opt));
	} 
  
      if (match)
	{
	  opt_cfg->netid->next = state->tags;
	  state->tags = opt_cfg->netid;
	}
    }

  if (state->mac_len != 0)
    {
      if (option_bool(OPT_LOG_OPTS))
	{
	  print_mac(daemon->dhcp_buff, state->mac, state->mac_len);
	  my_syslog(MS_DHCP | LOG_INFO, _("%u client MAC address: %s"), state->xid, daemon->dhcp_buff);
	}

      for (mac_opt = daemon->dhcp_macs; mac_opt; mac_opt = mac_opt->next)
	if ((unsigned)mac_opt->hwaddr_len == state->mac_len &&
	    ((unsigned)mac_opt->hwaddr_type == state->mac_type || mac_opt->hwaddr_type == 0) &&
	    memcmp_masked(mac_opt->hwaddr, state->mac, state->mac_len, mac_opt->mask))
	  {
	    mac_opt->netid.next = state->tags;
	    state->tags = &mac_opt->netid;
	  }
    }
  else if (option_bool(OPT_LOG_OPTS))
    my_syslog(MS_DHCP | LOG_INFO, _("%u cannot determine client MAC address"), state->xid);
  
  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_FQDN, 1)))
    {
      /* RFC4704 refers */
       int len = opt6_len(opt) - 1;
       
       state->fqdn_flags = opt6_uint(opt, 0, 1);
       
       /* Always force update, since the client has no way to do it itself. */
       if (!option_bool(OPT_FQDN_UPDATE) && !(state->fqdn_flags & 0x01))
	 state->fqdn_flags |= 0x03;
 
       state->fqdn_flags &= ~0x04;

       if (len != 0 && len < 255)
	 {
	   unsigned char *pp, *op = opt6_ptr(opt, 1);
	   char *pq = daemon->dhcp_buff;
	   
	   pp = op;
	   while (*op != 0 && ((op + (*op)) - pp) < len)
	     {
	       memcpy(pq, op+1, *op);
	       pq += *op;
	       op += (*op)+1;
	       *(pq++) = '.';
	     }
	   
	   if (pq != daemon->dhcp_buff)
	     pq--;
	   *pq = 0;
	   
	   if (legal_hostname(daemon->dhcp_buff))
	     {
	       struct dhcp_match_name *m;
	       size_t nl = strlen(daemon->dhcp_buff);
	       
	       state->client_hostname = daemon->dhcp_buff;
	       
	       if (option_bool(OPT_LOG_OPTS))
		 my_syslog(MS_DHCP | LOG_INFO, _("%u client provides name: %s"), state->xid, state->client_hostname);
	       
	       for (m = daemon->dhcp_name_match; m; m = m->next)
		 {
		   size_t ml = strlen(m->name);
		   char save = 0;
		   
		   if (nl < ml)
		     continue;
		   if (nl > ml)
		     {
		       save = state->client_hostname[ml];
		       state->client_hostname[ml] = 0;
		     }
		   
		   if (hostname_isequal(state->client_hostname, m->name) &&
		       (save == 0 || m->wildcard))
		     {
		       m->netid->next = state->tags;
		       state->tags = m->netid;
		     }
		   
		   if (save != 0)
		     state->client_hostname[ml] = save;
		 }
	     }
	 }
    }	 
  
  if (state->clid &&
      (config = find_config(daemon->dhcp_conf, state->context, state->clid, state->clid_len,
			    state->mac, state->mac_len, state->mac_type, NULL, run_tag_if(state->tags))) &&
      have_config(config, CONFIG_NAME))
    {
      state->hostname = config->hostname;
      state->domain = config->domain;
      state->hostname_auth = 1;
    }
  else if (state->client_hostname)
    {
      state->domain = strip_hostname(state->client_hostname);
      
      if (strlen(state->client_hostname) != 0)
	{
	  state->hostname = state->client_hostname;
	  
	  if (!config)
	    {
	      /* Search again now we have a hostname. 
		 Only accept configs without CLID here, (it won't match)
		 to avoid impersonation by name. */
	      struct dhcp_config *new = find_config(daemon->dhcp_conf, state->context, NULL, 0, NULL, 0, 0, state->hostname, run_tag_if(state->tags));
	      if (new && !have_config(new, CONFIG_CLID) && !new->hwaddr)
		config = new;
	    }
	}
    }

  if (config)
    {
      struct dhcp_netid_list *list;
      
      for (list = config->netid; list; list = list->next)
        {
          list->list->next = state->tags;
          state->tags = list->list;
        }

      /* set "known" tag for known hosts */
      known_id.net = "known";
      known_id.next = state->tags;
      state->tags = &known_id;

      if (have_config(config, CONFIG_DISABLE))
	ignore = 1;
    }
  else if (state->clid &&
	   find_config(daemon->dhcp_conf, NULL, state->clid, state->clid_len,
		       state->mac, state->mac_len, state->mac_type, NULL, run_tag_if(state->tags)))
    {
      known_id.net = "known-othernet";
      known_id.next = state->tags;
      state->tags = &known_id;
    }
  
  tagif = run_tag_if(state->tags);
  
  /* if all the netids in the ignore list are present, ignore this client */
  if (daemon->dhcp_ignore)
    {
      struct dhcp_netid_list *id_list;
     
      for (id_list = daemon->dhcp_ignore; id_list; id_list = id_list->next)
	if (match_netid(id_list->list, tagif, 0))
	  ignore = 1;
    }
  
  /* if all the netids in the ignore_name list are present, ignore client-supplied name */
  if (!state->hostname_auth)
    {
       struct dhcp_netid_list *id_list;
       
       for (id_list = daemon->dhcp_ignore_names; id_list; id_list = id_list->next)
	 if ((!id_list->list) || match_netid(id_list->list, tagif, 0))
	   break;
       if (id_list)
	 state->hostname = NULL;
    }
  

  switch (msg_type)
    {
    default:
      return 0;
      
      
    case DHCP6SOLICIT:
      {
      	int address_assigned;
	/* tags without all prefix-class tags */
	struct dhcp_netid *solicit_tags;
	struct dhcp_context *c;
	
	outmsgtype = DHCP6ADVERTISE;
	
	if (opt6_find(state->packet_options, state->end, OPTION6_RAPID_COMMIT, 0))
	  {
	    outmsgtype = DHCP6REPLY;
	    state->lease_allocate = 1;
	    o = new_opt6(OPTION6_RAPID_COMMIT);
	    end_opt6(o);
	  }
	
  	log6_quiet(state, "DHCPSOLICIT", NULL, ignore ? _("ignored") : NULL);

      request_no_address:
	solicit_tags = tagif;
	address_assigned = 0;
	
	if (ignore)
	  return 0;
	
	/* reset USED bits in leases */
	lease6_reset();

	/* Can use configured address max once per prefix */
	for (c = state->context; c; c = c->current)
	  c->flags &= ~CONTEXT_CONF_USED;

	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {   
	    void *ia_option, *ia_end;
	    unsigned int min_time = 0xffffffff;
	    int t1cntr;
	    int ia_counter;
	    /* set unless we're sending a particular prefix-class, when we
	       want only dhcp-ranges with the correct tags set and not those without any tags. */
	    int plain_range = 1;
	    u32 lease_time;
	    struct dhcp_lease *ltmp;
	    struct in6_addr req_addr, addr;
	    
	    if (!check_ia(state, opt, &ia_end, &ia_option))
	      continue;
	    
	    /* reset USED bits in contexts - one address per prefix per IAID */
	    for (c = state->context; c; c = c->current)
	      c->flags &= ~CONTEXT_USED;

	    o = build_ia(state, &t1cntr);
	    if (address_assigned)
		address_assigned = 2;

	    for (ia_counter = 0; ia_option; ia_counter++, ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24))
	      {
		/* worry about alignment here. */
		memcpy(&req_addr, opt6_ptr(ia_option, 0), IN6ADDRSZ);
				
		if ((c = address6_valid(state->context, &req_addr, solicit_tags, plain_range)))
		  {
		    lease_time = c->lease_time;
		    /* If the client asks for an address on the same network as a configured address, 
		       offer the configured address instead, to make moving to newly-configured
		       addresses automatic. */
		    if (!(c->flags & CONTEXT_CONF_USED) && config_valid(config, c, &addr, state, now))
		      {
			req_addr = addr;
			mark_config_used(c, &addr);
			if (have_config(config, CONFIG_TIME))
			  lease_time = config->lease_time;
		      }
		    else if (!(c = address6_available(state->context, &req_addr, solicit_tags, plain_range)))
		      continue; /* not an address we're allowed */
		    else if (!check_address(state, &req_addr))
		      continue; /* address leased elsewhere */
		    
		    /* add address to output packet */
		    add_address(state, c, lease_time, ia_option, &min_time, &req_addr, now);
		    mark_context_used(state, &req_addr);
		    get_context_tag(state, c);
		    address_assigned = 1;
		  }
	      }
	    
	    /* Suggest configured address(es) */
	    for (c = state->context; c; c = c->current) 
	      if (!(c->flags & CONTEXT_CONF_USED) &&
		  match_netid(c->filter, solicit_tags, plain_range) &&
		  config_valid(config, c, &addr, state, now))
		{
		  mark_config_used(state->context, &addr);
		  if (have_config(config, CONFIG_TIME))
		    lease_time = config->lease_time;
		  else
		    lease_time = c->lease_time;

		  /* add address to output packet */
		  add_address(state, c, lease_time, NULL, &min_time, &addr, now);
		  mark_context_used(state, &addr);
		  get_context_tag(state, c);
		  address_assigned = 1;
		}
	    
	    /* return addresses for existing leases */
	    ltmp = NULL;
	    while ((ltmp = lease6_find_by_client(ltmp, state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA, state->clid, state->clid_len, state->iaid)))
	      {
		req_addr = ltmp->addr6;
		if ((c = address6_available(state->context, &req_addr, solicit_tags, plain_range)))
		  {
		    add_address(state, c, c->lease_time, NULL, &min_time, &req_addr, now);
		    mark_context_used(state, &req_addr);
		    get_context_tag(state, c);
		    address_assigned = 1;
		  }
	      }
		 	   
	    /* Return addresses for all valid contexts which don't yet have one */
	    while ((c = address6_allocate(state->context, state->clid, state->clid_len, state->ia_type == OPTION6_IA_TA,
					  state->iaid, ia_counter, solicit_tags, plain_range, &addr)))
	      {
		add_address(state, c, c->lease_time, NULL, &min_time, &addr, now);
		mark_context_used(state, &addr);
		get_context_tag(state, c);
		address_assigned = 1;
	      }
	    
	    if (address_assigned != 1)
	      {
		/* If the server cannot assign any addresses to an IA in the message
		   from the client, the server MUST include the IA in the Reply message
		   with no addresses in the IA and a Status Code option in the IA
		   containing status code NoAddrsAvail. */
		o1 = new_opt6(OPTION6_STATUS_CODE);
		put_opt6_short(DHCP6NOADDRS);
		put_opt6_string(_("address unavailable"));
		end_opt6(o1);
	      }
	    
	    end_ia(t1cntr, min_time, 0);
	    end_opt6(o);	
	  }

	if (address_assigned) 
	  {
	    o1 = new_opt6(OPTION6_STATUS_CODE);
	    put_opt6_short(DHCP6SUCCESS);
	    put_opt6_string(_("success"));
	    end_opt6(o1);
	    
	    /* If --dhcp-authoritative is set, we can tell client not to wait for
	       other possible servers */
	    o = new_opt6(OPTION6_PREFERENCE);
	    put_opt6_char(option_bool(OPT_AUTHORITATIVE) ? 255 : 0);
	    end_opt6(o);
	  }
	else
	  { 
	    /* no address, return error */
	    o1 = new_opt6(OPTION6_STATUS_CODE);
	    put_opt6_short(DHCP6NOADDRS);
	    put_opt6_string(_("no addresses available"));
	    end_opt6(o1);

	    /* Some clients will ask repeatedly when we're not giving
	       out addresses because we're in stateless mode. Avoid spamming
	       the log in that case. */
	    for (c = state->context; c; c = c->current)
	      if (!(c->flags & CONTEXT_RA_STATELESS))
		{
		  log6_packet(state, state->lease_allocate ? "DHCPREPLY" : "DHCPADVERTISE", NULL, _("no addresses available"));
		  break;
		}
	  }
	
	tagif = add_options(state, 0);
	break;
      }
      
    case DHCP6REQUEST:
      {
	int address_assigned = 0;
	int start = save_counter(-1);

	/* set reply message type */
	outmsgtype = DHCP6REPLY;
	state->lease_allocate = 1;

	log6_quiet(state, "DHCPREQUEST", NULL, ignore ? _("ignored") : NULL);
	
	if (ignore)
	  return 0;
	
	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {   
	    void *ia_option, *ia_end;
	    unsigned int min_time = 0xffffffff;
	    int t1cntr;
	    
	     if (!check_ia(state, opt, &ia_end, &ia_option))
	       continue;

	     if (!ia_option)
	       {
		 /* If we get a request with an IA_*A without addresses, treat it exactly like
		    a SOLICT with rapid commit set. */
		 save_counter(start);
		 goto request_no_address; 
	       }

	    o = build_ia(state, &t1cntr);
	      
	    for (; ia_option; ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24))
	      {
		struct in6_addr req_addr;
		struct dhcp_context *dynamic, *c;
		unsigned int lease_time;
		int config_ok = 0;

		/* align. */
		memcpy(&req_addr, opt6_ptr(ia_option, 0), IN6ADDRSZ);
		
		if ((c = address6_valid(state->context, &req_addr, tagif, 1)))
		  config_ok = (config_implies(config, c, &req_addr) != NULL);
		
		if ((dynamic = address6_available(state->context, &req_addr, tagif, 1)) || c)
		  {
		    if (!dynamic && !config_ok)
		      {
			/* Static range, not configured. */
			o1 = new_opt6(OPTION6_STATUS_CODE);
			put_opt6_short(DHCP6NOADDRS);
			put_opt6_string(_("address unavailable"));
			end_opt6(o1);
		      }
		    else if (!check_address(state, &req_addr))
		      {
			/* Address leased to another DUID/IAID */
			o1 = new_opt6(OPTION6_STATUS_CODE);
			put_opt6_short(DHCP6UNSPEC);
			put_opt6_string(_("address in use"));
			end_opt6(o1);
		      } 
		    else 
		      {
			if (!dynamic)
			  dynamic = c;

			lease_time = dynamic->lease_time;
			
			if (config_ok && have_config(config, CONFIG_TIME))
			  lease_time = config->lease_time;

			add_address(state, dynamic, lease_time, ia_option, &min_time, &req_addr, now);
			get_context_tag(state, dynamic);
			address_assigned = 1;
		      }
		  }
		else 
		  {
		    /* requested address not on the correct link */
		    o1 = new_opt6(OPTION6_STATUS_CODE);
		    put_opt6_short(DHCP6NOTONLINK);
		    put_opt6_string(_("not on link"));
		    end_opt6(o1);
		  }
	      }
	 
	    end_ia(t1cntr, min_time, 0);
	    end_opt6(o);	
	  }

	if (address_assigned) 
	  {
	    o1 = new_opt6(OPTION6_STATUS_CODE);
	    put_opt6_short(DHCP6SUCCESS);
	    put_opt6_string(_("success"));
	    end_opt6(o1);
	  }
	else
	  { 
	    /* no address, return error */
	    o1 = new_opt6(OPTION6_STATUS_CODE);
	    put_opt6_short(DHCP6NOADDRS);
	    put_opt6_string(_("no addresses available"));
	    end_opt6(o1);
	    log6_packet(state, "DHCPREPLY", NULL, _("no addresses available"));
	  }

	tagif = add_options(state, 0);
	break;
      }
      
  
    case DHCP6RENEW:
    case DHCP6REBIND:
      {
	int address_assigned = 0;

	/* set reply message type */
	outmsgtype = DHCP6REPLY;
	
	log6_quiet(state, msg_type == DHCP6RENEW ? "DHCPRENEW" : "DHCPREBIND", NULL, NULL);

	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {
	    void *ia_option, *ia_end;
	    unsigned int min_time = 0xffffffff;
	    int t1cntr, iacntr;
	    
	    if (!check_ia(state, opt, &ia_end, &ia_option))
	      continue;
	    
	    o = build_ia(state, &t1cntr);
	    iacntr = save_counter(-1); 
	    
	    for (; ia_option; ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24))
	      {
		struct dhcp_lease *lease = NULL;
		struct in6_addr req_addr;
		unsigned int preferred_time =  opt6_uint(ia_option, 16, 4);
		unsigned int valid_time =  opt6_uint(ia_option, 20, 4);
		char *message = NULL;
		struct dhcp_context *this_context;

		memcpy(&req_addr, opt6_ptr(ia_option, 0), IN6ADDRSZ); 
		
		if (!(lease = lease6_find(state->clid, state->clid_len,
					  state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA, 
					  state->iaid, &req_addr)))
		  {
		    if (msg_type == DHCP6REBIND)
		      {
			/* When rebinding, we can create a lease if it doesn't exist, as long
			   as --dhcp-authoritative is set. */
			if (option_bool(OPT_AUTHORITATIVE))
			  lease = lease6_allocate(&req_addr, state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA);
			if (lease)
			  lease_set_iaid(lease, state->iaid);
			else
			  break;
		      }
		    else
		      {
			/* If the server cannot find a client entry for the IA the server
			   returns the IA containing no addresses with a Status Code option set
			   to NoBinding in the Reply message. */
			save_counter(iacntr);
			t1cntr = 0;
			
			log6_packet(state, "DHCPREPLY", &req_addr, _("lease not found"));
			
			o1 = new_opt6(OPTION6_STATUS_CODE);
			put_opt6_short(DHCP6NOBINDING);
			put_opt6_string(_("no binding found"));
			end_opt6(o1);
			
			preferred_time = valid_time = 0;
			break;
		      }
		  }
		
		if ((this_context = address6_available(state->context, &req_addr, tagif, 1)) ||
		    (this_context = address6_valid(state->context, &req_addr, tagif, 1)))
		  {
		    unsigned int lease_time;

		    get_context_tag(state, this_context);
		    
		    if (config_implies(config, this_context, &req_addr) && have_config(config, CONFIG_TIME))
		      lease_time = config->lease_time;
		    else 
		      lease_time = this_context->lease_time;
		    
		    calculate_times(this_context, &min_time, &valid_time, &preferred_time, lease_time); 
		    
		    lease_set_expires(lease, valid_time, now);
		    /* Update MAC record in case it's new information. */
		    if (state->mac_len != 0)
		      lease_set_hwaddr(lease, state->mac, state->clid, state->mac_len, state->mac_type, state->clid_len, now, 0);
		    if (state->ia_type == OPTION6_IA_NA && state->hostname)
		      {
			char *addr_domain = get_domain6(&req_addr);
			if (!state->send_domain)
			  state->send_domain = addr_domain;
			lease_set_hostname(lease, state->hostname, state->hostname_auth, addr_domain, state->domain); 
			message = state->hostname;
		      }
		    
		    
		    if (preferred_time == 0)
		      message = _("deprecated");

		    address_assigned = 1;
		  }
		else
		  {
		    preferred_time = valid_time = 0;
		    message = _("address invalid");
		  } 

		if (message && (message != state->hostname))
		  log6_packet(state, "DHCPREPLY", &req_addr, message);	
		else
		  log6_quiet(state, "DHCPREPLY", &req_addr, message);
	
		o1 =  new_opt6(OPTION6_IAADDR);
		put_opt6(&req_addr, sizeof(req_addr));
		put_opt6_long(preferred_time);
		put_opt6_long(valid_time);
		end_opt6(o1);
	      }
	    
	    end_ia(t1cntr, min_time, 1);
	    end_opt6(o);
	  }

	if (!address_assigned && msg_type == DHCP6REBIND)
	  { 
	    /* can't create lease for any address, return error */
	    o1 = new_opt6(OPTION6_STATUS_CODE);
	    put_opt6_short(DHCP6NOADDRS);
	    put_opt6_string(_("no addresses available"));
	    end_opt6(o1);
	  }
	
	tagif = add_options(state, 0);
	break;
      }
      
    case DHCP6CONFIRM:
      {
	int good_addr = 0, bad_addr = 0;

	/* set reply message type */
	outmsgtype = DHCP6REPLY;
	
	log6_quiet(state, "DHCPCONFIRM", NULL, NULL);
	
	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {
	    void *ia_option, *ia_end;
	    
	    for (check_ia(state, opt, &ia_end, &ia_option);
		 ia_option;
		 ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24))
	      {
		struct in6_addr req_addr;

		/* alignment */
		memcpy(&req_addr, opt6_ptr(ia_option, 0), IN6ADDRSZ);
		
		if (!address6_valid(state->context, &req_addr, tagif, 1))
		  {
		    bad_addr = 1;
		    log6_quiet(state, "DHCPREPLY", &req_addr, _("confirm failed"));
		  }
		else
		  {
		    good_addr = 1;
		    log6_quiet(state, "DHCPREPLY", &req_addr, state->hostname);
		  }
	      }
	  }	 
	
	/* No addresses, no reply: RFC 3315 18.2.2 */
	if (!good_addr && !bad_addr)
	  return 0;

	o1 = new_opt6(OPTION6_STATUS_CODE);
	put_opt6_short(bad_addr ? DHCP6NOTONLINK : DHCP6SUCCESS);
	put_opt6_string(bad_addr ? (_("confirm failed")) : (_("all addresses still on link")));
	end_opt6(o1);
	break;
    }
      
    case DHCP6IREQ:
      {
	/* 3315 para 15.12 */
	if (opt6_find(state->packet_options, state->end, OPTION6_IA_NA, 1) ||
	    opt6_find(state->packet_options, state->end, OPTION6_IA_TA, 1))
	  return 0;
	
	/* We can't discriminate contexts based on address, as we don't know it.
	   If there is only one possible context, we can use its tags */
	if (state->context && state->context->netid.net && !state->context->current)
	  {
	    state->context->netid.next = NULL;
	    state->context_tags =  &state->context->netid;
	  }

	/* Similarly, we can't determine domain from address, but if the FQDN is
	   given in --dhcp-host, we can use that, and failing that we can use the 
	   unqualified configured domain, if any. */
	if (state->hostname_auth)
	  state->send_domain = state->domain;
	else
	  state->send_domain = get_domain6(NULL);

	log6_quiet(state, "DHCPINFORMATION-REQUEST", NULL, ignore ? _("ignored") : state->hostname);
	if (ignore)
	  return 0;
	outmsgtype = DHCP6REPLY;
	tagif = add_options(state, 1);
	break;
      }
      
      
    case DHCP6RELEASE:
      {
	/* set reply message type */
	outmsgtype = DHCP6REPLY;

	log6_quiet(state, "DHCPRELEASE", NULL, NULL);

	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {
	    void *ia_option, *ia_end;
	    int made_ia = 0;
	    	    
	    for (check_ia(state, opt, &ia_end, &ia_option);
		 ia_option;
		 ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24)) 
	      {
		struct dhcp_lease *lease;
		struct in6_addr addr;

		/* align */
		memcpy(&addr, opt6_ptr(ia_option, 0), IN6ADDRSZ);
		if ((lease = lease6_find(state->clid, state->clid_len, state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA,
					 state->iaid, &addr)))
		  lease_prune(lease, now);
		else
		  {
		    if (!made_ia)
		      {
			o = new_opt6(state->ia_type);
			put_opt6_long(state->iaid);
			if (state->ia_type == OPTION6_IA_NA)
			  {
			    put_opt6_long(0);
			    put_opt6_long(0); 
			  }
			made_ia = 1;
		      }
		    
		    o1 = new_opt6(OPTION6_IAADDR);
		    put_opt6(&addr, IN6ADDRSZ);
		    put_opt6_long(0);
		    put_opt6_long(0);
		    end_opt6(o1);
		  }
	      }
	    
	    if (made_ia)
	      {
		o1 = new_opt6(OPTION6_STATUS_CODE);
		put_opt6_short(DHCP6NOBINDING);
		put_opt6_string(_("no binding found"));
		end_opt6(o1);
		
		end_opt6(o);
	      }
	  }
	
	o1 = new_opt6(OPTION6_STATUS_CODE);
	put_opt6_short(DHCP6SUCCESS);
	put_opt6_string(_("release received"));
	end_opt6(o1);
	
	break;
      }

    case DHCP6DECLINE:
      {
	/* set reply message type */
	outmsgtype = DHCP6REPLY;
	
	log6_quiet(state, "DHCPDECLINE", NULL, NULL);

	for (opt = state->packet_options; opt; opt = opt6_next(opt, state->end))
	  {
	    void *ia_option, *ia_end;
	    int made_ia = 0;
	    	    
	    for (check_ia(state, opt, &ia_end, &ia_option);
		 ia_option;
		 ia_option = opt6_find(opt6_next(ia_option, ia_end), ia_end, OPTION6_IAADDR, 24)) 
	      {
		struct dhcp_lease *lease;
		struct in6_addr addr;
		struct addrlist *addr_list;
		
		/* align */
		memcpy(&addr, opt6_ptr(ia_option, 0), IN6ADDRSZ);

		if ((addr_list = config_implies(config, state->context, &addr)))
		  {
		    prettyprint_time(daemon->dhcp_buff3, DECLINE_BACKOFF);
		    inet_ntop(AF_INET6, &addr, daemon->addrbuff, ADDRSTRLEN);
		    my_syslog(MS_DHCP | LOG_WARNING, _("disabling DHCP static address %s for %s"), 
			      daemon->addrbuff, daemon->dhcp_buff3);
		    addr_list->flags |= ADDRLIST_DECLINED;
		    addr_list->decline_time = now;
		  }
		else
		  /* make sure this host gets a different address next time. */
		  for (context_tmp = state->context; context_tmp; context_tmp = context_tmp->current)
		    context_tmp->addr_epoch++;
		
		if ((lease = lease6_find(state->clid, state->clid_len, state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA,
					 state->iaid, &addr)))
		  lease_prune(lease, now);
		else
		  {
		    if (!made_ia)
		      {
			o = new_opt6(state->ia_type);
			put_opt6_long(state->iaid);
			if (state->ia_type == OPTION6_IA_NA)
			  {
			    put_opt6_long(0);
			    put_opt6_long(0); 
			  }
			made_ia = 1;
		      }
		    
		    o1 = new_opt6(OPTION6_IAADDR);
		    put_opt6(&addr, IN6ADDRSZ);
		    put_opt6_long(0);
		    put_opt6_long(0);
		    end_opt6(o1);
		  }
	      }
	    
	    if (made_ia)
	      {
		o1 = new_opt6(OPTION6_STATUS_CODE);
		put_opt6_short(DHCP6NOBINDING);
		put_opt6_string(_("no binding found"));
		end_opt6(o1);
		
		end_opt6(o);
	      }
	    
	  }

	/* We must answer with 'success' in global section anyway */
	o1 = new_opt6(OPTION6_STATUS_CODE);
	put_opt6_short(DHCP6SUCCESS);
	put_opt6_string(_("success"));
	end_opt6(o1);
	break;
      }

    }

  log_tags(tagif, state->xid);

 done:
  /* Fill in the message type. Note that we store the offset,
     not a direct pointer, since the packet memory may have been 
     reallocated. */
  ((unsigned char *)(daemon->outpacket.iov_base))[start_msg] = outmsgtype;

  log6_opts(0, state->xid, (uint8_t *)daemon->outpacket.iov_base + start_opts, (uint8_t *)daemon->outpacket.iov_base + save_counter(-1));
  
  return 1;

}

static struct dhcp_netid *add_options(struct state *state, int do_refresh)  
{
  void *oro;
  /* filter options based on tags, those we want get DHOPT_TAGOK bit set */
  struct dhcp_netid *tagif = option_filter(state->tags, state->context_tags, daemon->dhcp_opts6, 0);
  struct dhcp_opt *opt_cfg;
  int done_dns = 0, done_refresh = !do_refresh, do_encap = 0;
  int i, o, o1;

  oro = opt6_find(state->packet_options, state->end, OPTION6_ORO, 0);
  
  for (opt_cfg = daemon->dhcp_opts6; opt_cfg; opt_cfg = opt_cfg->next)
    {
      /* netids match and not encapsulated? */
      if (!(opt_cfg->flags & DHOPT_TAGOK))
	continue;
      
      if (!(opt_cfg->flags & DHOPT_FORCE) && oro)
	{
	  for (i = 0; i <  opt6_len(oro) - 1; i += 2)
	    if (opt6_uint(oro, i, 2) == (unsigned)opt_cfg->opt)
	      break;
	  
	  /* option not requested */
	  if (i >=  opt6_len(oro) - 1)
	    continue;
	}
      
      if (opt_cfg->opt == OPTION6_REFRESH_TIME)
	done_refresh = 1;
       
      if (opt_cfg->opt == OPTION6_DNS_SERVER)
	done_dns = 1;
      
      if (opt_cfg->flags & DHOPT_ADDR6)
	{
	  int len, j;
	  struct in6_addr *a;
	  
	  for (a = (struct in6_addr *)opt_cfg->val, len = opt_cfg->len, j = 0; 
	       j < opt_cfg->len; j += IN6ADDRSZ, a++)
	    if ((IN6_IS_ADDR_ULA_ZERO(a) && IN6_IS_ADDR_UNSPECIFIED(state->ula_addr)) ||
		(IN6_IS_ADDR_LINK_LOCAL_ZERO(a) && IN6_IS_ADDR_UNSPECIFIED(state->ll_addr)))
	      len -= IN6ADDRSZ;
	  
	  if (len != 0)
	    {
	      
	      o = new_opt6(opt_cfg->opt);
	      	  
	      for (a = (struct in6_addr *)opt_cfg->val, j = 0; j < opt_cfg->len; j+=IN6ADDRSZ, a++)
		{
		  struct in6_addr *p = NULL;

		  if (IN6_IS_ADDR_UNSPECIFIED(a))
		    {
		      if (!add_local_addrs(state->context))
			p = state->fallback;
		    }
		  else if (IN6_IS_ADDR_ULA_ZERO(a))
		    {
		      if (!IN6_IS_ADDR_UNSPECIFIED(state->ula_addr))
			p = state->ula_addr;
		    }
		  else if (IN6_IS_ADDR_LINK_LOCAL_ZERO(a))
		    {
		      if (!IN6_IS_ADDR_UNSPECIFIED(state->ll_addr))
			p = state->ll_addr;
		    }
		  else
		    p = a;

		  if (!p)
		    continue;
		  else if (opt_cfg->opt == OPTION6_NTP_SERVER)
		    {
		      if (IN6_IS_ADDR_MULTICAST(p))
			o1 = new_opt6(NTP_SUBOPTION_MC_ADDR);
		      else
			o1 = new_opt6(NTP_SUBOPTION_SRV_ADDR);
		      put_opt6(p, IN6ADDRSZ);
		      end_opt6(o1);
		    }
		  else
		    put_opt6(p, IN6ADDRSZ);
		}

	      end_opt6(o);
	    }
	}
      else
	{
	  o = new_opt6(opt_cfg->opt);
	  if (opt_cfg->val)
	    put_opt6(opt_cfg->val, opt_cfg->len);
	  end_opt6(o);
	}
    }
  
  if (daemon->port == NAMESERVER_PORT && !done_dns)
    {
      o = new_opt6(OPTION6_DNS_SERVER);
      if (!add_local_addrs(state->context))
	put_opt6(state->fallback, IN6ADDRSZ);
      end_opt6(o); 
    }

  if (state->context && !done_refresh)
    {
      struct dhcp_context *c;
      unsigned int lease_time = 0xffffffff;
      
      /* Find the smallest lease tie of all contexts,
	 subject to the RFC-4242 stipulation that this must not 
	 be less than 600. */
      for (c = state->context; c; c = c->next)
	if (c->lease_time < lease_time)
	  {
	    if (c->lease_time < 600)
	      lease_time = 600;
	    else
	      lease_time = c->lease_time;
	  }

      o = new_opt6(OPTION6_REFRESH_TIME);
      put_opt6_long(lease_time);
      end_opt6(o); 
    }
   
    /* handle vendor-identifying vendor-encapsulated options,
       dhcp-option = vi-encap:13,17,....... */
  for (opt_cfg = daemon->dhcp_opts6; opt_cfg; opt_cfg = opt_cfg->next)
    opt_cfg->flags &= ~DHOPT_ENCAP_DONE;
    
  if (oro)
    for (i = 0; i <  opt6_len(oro) - 1; i += 2)
      if (opt6_uint(oro, i, 2) == OPTION6_VENDOR_OPTS)
	do_encap = 1;
  
  for (opt_cfg = daemon->dhcp_opts6; opt_cfg; opt_cfg = opt_cfg->next)
    { 
      if (opt_cfg->flags & DHOPT_RFC3925)
	{
	  int found = 0;
	  struct dhcp_opt *oc;
	  
	  if (opt_cfg->flags & DHOPT_ENCAP_DONE)
	    continue;
	  
	  for (oc = daemon->dhcp_opts6; oc; oc = oc->next)
	    {
	      oc->flags &= ~DHOPT_ENCAP_MATCH;
	      
	      if (!(oc->flags & DHOPT_RFC3925) || opt_cfg->u.encap != oc->u.encap)
		continue;
	      
	      oc->flags |= DHOPT_ENCAP_DONE;
	      if (match_netid(oc->netid, tagif, 1))
		{
		  /* option requested/forced? */
		  if (!oro || do_encap || (oc->flags & DHOPT_FORCE))
		    {
		      oc->flags |= DHOPT_ENCAP_MATCH;
		      found = 1;
		    }
		} 
	    }
	  
	  if (found)
	    { 
	      o = new_opt6(OPTION6_VENDOR_OPTS);	      
	      put_opt6_long(opt_cfg->u.encap);	
	     
	      for (oc = daemon->dhcp_opts6; oc; oc = oc->next)
		if (oc->flags & DHOPT_ENCAP_MATCH)
		  {
		    o1 = new_opt6(oc->opt);
		    put_opt6(oc->val, oc->len);
		    end_opt6(o1);
		  }
	      end_opt6(o);
	    }
	}
    }      


  if (state->hostname)
    {
      unsigned char *p;
      size_t len = strlen(state->hostname);
      
      if (state->send_domain)
	len += strlen(state->send_domain) + 2;

      o = new_opt6(OPTION6_FQDN);
      if ((p = expand(len + 2)))
	{
	  *(p++) = state->fqdn_flags;
	  p = do_rfc1035_name(p, state->hostname, NULL);
	  if (state->send_domain)
	    {
	      p = do_rfc1035_name(p, state->send_domain, NULL);
	      *p = 0;
	    }
	}
      end_opt6(o);
    }


  /* logging */
  if (option_bool(OPT_LOG_OPTS) && oro)
    {
      char *q = daemon->namebuff;
      for (i = 0; i <  opt6_len(oro) - 1; i += 2)
	{
	  char *s = option_string(AF_INET6, opt6_uint(oro, i, 2), NULL, 0, NULL, 0);
	  q += snprintf(q, MAXDNAME - (q - daemon->namebuff),
			"%d%s%s%s", 
			opt6_uint(oro, i, 2),
			strlen(s) != 0 ? ":" : "",
			s, 
			(i > opt6_len(oro) - 3) ? "" : ", ");
	  if ( i >  opt6_len(oro) - 3 || (q - daemon->namebuff) > 40)
	    {
	      q = daemon->namebuff;
	      my_syslog(MS_DHCP | LOG_INFO, _("%u requested options: %s"), state->xid, daemon->namebuff);
	    }
	}
    } 

  return tagif;
}
 
static int add_local_addrs(struct dhcp_context *context)
{
  int done = 0;
  
  for (; context; context = context->current)
    if ((context->flags & CONTEXT_USED) && !IN6_IS_ADDR_UNSPECIFIED(&context->local6))
      {
	/* squash duplicates */
	struct dhcp_context *c;
	for (c = context->current; c; c = c->current)
	  if ((c->flags & CONTEXT_USED) &&
	      IN6_ARE_ADDR_EQUAL(&context->local6, &c->local6))
	    break;
	
	if (!c)
	  { 
	    done = 1;
	    put_opt6(&context->local6, IN6ADDRSZ);
	  }
      }

  return done;
}


static void get_context_tag(struct state *state, struct dhcp_context *context)
{
  /* get tags from context if we've not used it before */
  if (context->netid.next == &context->netid && context->netid.net)
    {
      context->netid.next = state->context_tags;
      state->context_tags = &context->netid;
      if (!state->hostname_auth)
	{
	  struct dhcp_netid_list *id_list;
	  
	  for (id_list = daemon->dhcp_ignore_names; id_list; id_list = id_list->next)
	    if ((!id_list->list) || match_netid(id_list->list, &context->netid, 0))
	      break;
	  if (id_list)
	    state->hostname = NULL;
	}
    }
} 

static int check_ia(struct state *state, void *opt, void **endp, void **ia_option)
{
  state->ia_type = opt6_type(opt);
  *ia_option = NULL;

  if (state->ia_type != OPTION6_IA_NA && state->ia_type != OPTION6_IA_TA)
    return 0;
  
  if (state->ia_type == OPTION6_IA_NA && opt6_len(opt) < 12)
    return 0;
	    
  if (state->ia_type == OPTION6_IA_TA && opt6_len(opt) < 4)
    return 0;
  
  *endp = opt6_ptr(opt, opt6_len(opt));
  state->iaid = opt6_uint(opt, 0, 4);
  *ia_option = opt6_find(opt6_ptr(opt, state->ia_type == OPTION6_IA_NA ? 12 : 4), *endp, OPTION6_IAADDR, 24);

  return 1;
}


static int build_ia(struct state *state, int *t1cntr)
{
  int  o = new_opt6(state->ia_type);
 
  put_opt6_long(state->iaid);
  *t1cntr = 0;
	    
  if (state->ia_type == OPTION6_IA_NA)
    {
      /* save pointer */
      *t1cntr = save_counter(-1);
      /* so we can fill these in later */
      put_opt6_long(0);
      put_opt6_long(0); 
    }

  return o;
}

static void end_ia(int t1cntr, unsigned int min_time, int do_fuzz)
{
  if (t1cntr != 0)
    {
      /* go back and fill in fields in IA_NA option */
      int sav = save_counter(t1cntr);
      unsigned int t1, t2, fuzz = 0;

      if (do_fuzz)
	{
	  fuzz = rand16();
      
	  while (fuzz > (min_time/16))
	    fuzz = fuzz/2;
	}
      
      t1 = (min_time == 0xffffffff) ? 0xffffffff : min_time/2 - fuzz;
      t2 = (min_time == 0xffffffff) ? 0xffffffff : ((min_time/8)*7) - fuzz;
      put_opt6_long(t1);
      put_opt6_long(t2);
      save_counter(sav);
    }	
}

static void add_address(struct state *state, struct dhcp_context *context, unsigned int lease_time, void *ia_option, 
			unsigned int *min_time, struct in6_addr *addr, time_t now)
{
  unsigned int valid_time = 0, preferred_time = 0;
  int o = new_opt6(OPTION6_IAADDR);
  struct dhcp_lease *lease;

  /* get client requested times */
  if (ia_option)
    {
      preferred_time =  opt6_uint(ia_option, 16, 4);
      valid_time =  opt6_uint(ia_option, 20, 4);
    }

  calculate_times(context, min_time, &valid_time, &preferred_time, lease_time); 
  
  put_opt6(addr, sizeof(*addr));
  put_opt6_long(preferred_time);
  put_opt6_long(valid_time); 		    
  end_opt6(o);
  
  if (state->lease_allocate)
    update_leases(state, context, addr, valid_time, now);

  if ((lease = lease6_find_by_addr(addr, 128, 0)))
    lease->flags |= LEASE_USED;

  /* get tags from context if we've not used it before */
  if (context->netid.next == &context->netid && context->netid.net)
    {
      context->netid.next = state->context_tags;
      state->context_tags = &context->netid;
      
      if (!state->hostname_auth)
	{
	  struct dhcp_netid_list *id_list;
	  
	  for (id_list = daemon->dhcp_ignore_names; id_list; id_list = id_list->next)
	    if ((!id_list->list) || match_netid(id_list->list, &context->netid, 0))
	      break;
	  if (id_list)
	    state->hostname = NULL;
	}
    }

  log6_quiet(state, state->lease_allocate ? "DHCPREPLY" : "DHCPADVERTISE", addr, state->hostname);

}

static void mark_context_used(struct state *state, struct in6_addr *addr)
{
  struct dhcp_context *context;

  /* Mark that we have an address for this prefix. */
  for (context = state->context; context; context = context->current)
    if (is_same_net6(addr, &context->start6, context->prefix))
      context->flags |= CONTEXT_USED;
}

static void mark_config_used(struct dhcp_context *context, struct in6_addr *addr)
{
  for (; context; context = context->current)
    if (is_same_net6(addr, &context->start6, context->prefix))
      context->flags |= CONTEXT_CONF_USED;
}

/* make sure address not leased to another CLID/IAID */
static int check_address(struct state *state, struct in6_addr *addr)
{ 
  struct dhcp_lease *lease;

  if (!(lease = lease6_find_by_addr(addr, 128, 0)))
    return 1;

  if (lease->clid_len != state->clid_len || 
      memcmp(lease->clid, state->clid, state->clid_len) != 0 ||
      lease->iaid != state->iaid)
    return 0;

  return 1;
}


/* return true of *addr could have been generated from config. */
static struct addrlist *config_implies(struct dhcp_config *config, struct dhcp_context *context, struct in6_addr *addr)
{
  int prefix;
  struct in6_addr wild_addr;
  struct addrlist *addr_list;
  
  if (!config || !(config->flags & CONFIG_ADDR6))
    return NULL;
  
  for (addr_list = config->addr6; addr_list; addr_list = addr_list->next)
    {
      prefix = (addr_list->flags & ADDRLIST_PREFIX) ? addr_list->prefixlen : 128;
      wild_addr = addr_list->addr.addr6;
      
      if ((addr_list->flags & ADDRLIST_WILDCARD) && context->prefix == 64)
	{
	  wild_addr = context->start6;
	  setaddr6part(&wild_addr, addr6part(&addr_list->addr.addr6));
	}
      else if (!is_same_net6(&context->start6, addr, context->prefix))
	continue;
      
      if (is_same_net6(&wild_addr, addr, prefix))
	return addr_list;
    }
  
  return NULL;
}

static int config_valid(struct dhcp_config *config, struct dhcp_context *context, struct in6_addr *addr, struct state *state, time_t now)
{
  u64 addrpart, i, addresses;
  struct addrlist *addr_list;
  
  if (!config || !(config->flags & CONFIG_ADDR6))
    return 0;

  for (addr_list = config->addr6; addr_list; addr_list = addr_list->next)
    if (!(addr_list->flags & ADDRLIST_DECLINED) ||
	difftime(now, addr_list->decline_time) >= (float)DECLINE_BACKOFF)
      {
	addrpart = addr6part(&addr_list->addr.addr6);
	addresses = 1;
	
	if (addr_list->flags & ADDRLIST_PREFIX)
	  addresses = (u64)1<<(128-addr_list->prefixlen);
	
	if ((addr_list->flags & ADDRLIST_WILDCARD))
	  {
	    if (context->prefix != 64)
	      continue;
	    
	    *addr = context->start6;
	  }
	else if (is_same_net6(&context->start6, &addr_list->addr.addr6, context->prefix))
	  *addr = addr_list->addr.addr6;
	else
	  continue;
	
	for (i = 0 ; i < addresses; i++)
	  {
	    setaddr6part(addr, addrpart+i);
	    
	    if (check_address(state, addr))
	      return 1;
	  }
      }
  
  return 0;
}

/* Calculate valid and preferred times to send in leases/renewals. 

   Inputs are:

   *valid_timep, *preferred_timep - requested times from IAADDR options.
   context->valid, context->preferred - times associated with subnet address on local interface.
   context->flags | CONTEXT_DEPRECATE - "deprecated" flag in dhcp-range.
   lease_time - configured time for context for individual client.
   *min_time - smallest valid time sent so far.

   Outputs are :
   
   *valid_timep, *preferred_timep - times to be send in IAADDR option.
   *min_time - smallest valid time sent so far, to calculate T1 and T2.
   
   */
static void calculate_times(struct dhcp_context *context, unsigned int *min_time, unsigned int *valid_timep, 
			    unsigned int *preferred_timep, unsigned int lease_time)
{
  unsigned int req_preferred = *preferred_timep, req_valid = *valid_timep;
  unsigned int valid_time = lease_time, preferred_time = lease_time;
  
  /* RFC 3315: "A server ignores the lifetimes set
     by the client if the preferred lifetime is greater than the valid
     lifetime. */
  if (req_preferred <= req_valid)
    {
      if (req_preferred != 0)
	{
	  /* 0 == "no preference from client" */
	  if (req_preferred < 120u)
	    req_preferred = 120u; /* sanity */
	  
	  if (req_preferred < preferred_time)
	    preferred_time = req_preferred;
	}
      
      if (req_valid != 0)
	/* 0 == "no preference from client" */
	{
	  if (req_valid < 120u)
	    req_valid = 120u; /* sanity */
	  
	  if (req_valid < valid_time)
	    valid_time = req_valid;
	}
    }

  /* deprecate (preferred == 0) which configured, or when local address 
     is deprecated */
  if ((context->flags & CONTEXT_DEPRECATE) || context->preferred == 0)
    preferred_time = 0;
  
  if (preferred_time != 0 && preferred_time < *min_time)
    *min_time = preferred_time;
  
  if (valid_time != 0 && valid_time < *min_time)
    *min_time = valid_time;
  
  *valid_timep = valid_time;
  *preferred_timep = preferred_time;
}

static void update_leases(struct state *state, struct dhcp_context *context, struct in6_addr *addr, unsigned int lease_time, time_t now)
{
  struct dhcp_lease *lease = lease6_find_by_addr(addr, 128, 0);
#ifdef HAVE_SCRIPT
  struct dhcp_netid *tagif = run_tag_if(state->tags);
#endif

  (void)context;

  if (!lease)
    lease = lease6_allocate(addr, state->ia_type == OPTION6_IA_NA ? LEASE_NA : LEASE_TA);
  
  if (lease)
    {
      lease_set_expires(lease, lease_time, now);
      lease_set_iaid(lease, state->iaid); 
      lease_set_hwaddr(lease, state->mac, state->clid, state->mac_len, state->mac_type, state->clid_len, now, 0);
      lease_set_interface(lease, state->interface, now);
      if (state->hostname && state->ia_type == OPTION6_IA_NA)
	{
	  char *addr_domain = get_domain6(addr);
	  if (!state->send_domain)
	    state->send_domain = addr_domain;
	  lease_set_hostname(lease, state->hostname, state->hostname_auth, addr_domain, state->domain);
	}
      
#ifdef HAVE_SCRIPT
      if (daemon->lease_change_command)
	{
	  void *opt;
	  
	  lease->flags |= LEASE_CHANGED;
	  free(lease->extradata);
	  lease->extradata = NULL;
	  lease->extradata_size = lease->extradata_len = 0;
	  lease->vendorclass_count = 0; 
	  
	  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_VENDOR_CLASS, 4)))
	    {
	      void *enc_opt, *enc_end = opt6_ptr(opt, opt6_len(opt));
	      lease->vendorclass_count++;
	      /* send enterprise number first  */
	      sprintf(daemon->dhcp_buff2, "%u", opt6_uint(opt, 0, 4));
	      lease_add_extradata(lease, (unsigned char *)daemon->dhcp_buff2, strlen(daemon->dhcp_buff2), 0);
	      
	      if (opt6_len(opt) >= 6) 
		for (enc_opt = opt6_ptr(opt, 4); enc_opt; enc_opt = opt6_next(enc_opt, enc_end))
		  {
		    lease->vendorclass_count++;
		    lease_add_extradata(lease, opt6_ptr(enc_opt, 0), opt6_len(enc_opt), 0);
		  }
	    }
	  
	  lease_add_extradata(lease, (unsigned char *)state->client_hostname, 
			      state->client_hostname ? strlen(state->client_hostname) : 0, 0);				
	  
	  /* DNSMASQ_REQUESTED_OPTIONS */
	  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_ORO, 2)))
	    {
	      int i, len = opt6_len(opt)/2;
	      u16 *rop = opt6_ptr(opt, 0);
	      
	      for (i = 0; i < len; i++)
		lease_add_extradata(lease, (unsigned char *)daemon->namebuff,
				    sprintf(daemon->namebuff, "%u", ntohs(rop[i])), (i + 1) == len ? 0 : ',');
	    }
	  else
	    lease_add_extradata(lease, NULL, 0, 0);

	  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_MUD_URL, 1)))
	    lease_add_extradata(lease, opt6_ptr(opt, 0), opt6_len(opt), 0);
	  else
	    lease_add_extradata(lease, NULL, 0, 0);

	  /* space-concat tag set */
	  if (!tagif && !context->netid.net)
	    lease_add_extradata(lease, NULL, 0, 0);
	  else
	    {
	      if (context->netid.net)
		lease_add_extradata(lease, (unsigned char *)context->netid.net, strlen(context->netid.net), tagif ? ' ' : 0);
	      
	      if (tagif)
		{
		  struct dhcp_netid *n;
		  for (n = tagif; n; n = n->next)
		    {
		      struct dhcp_netid *n1;
		      /* kill dupes */
		      for (n1 = n->next; n1; n1 = n1->next)
			if (strcmp(n->net, n1->net) == 0)
			  break;
		      if (!n1)
			lease_add_extradata(lease, (unsigned char *)n->net, strlen(n->net), n->next ? ' ' : 0); 
		    }
		}
	    }
	  
	  if (state->link_address)
	    inet_ntop(AF_INET6, state->link_address, daemon->addrbuff, ADDRSTRLEN);
	  
	  lease_add_extradata(lease, (unsigned char *)daemon->addrbuff, state->link_address ? strlen(daemon->addrbuff) : 0, 0);
	  
	  if ((opt = opt6_find(state->packet_options, state->end, OPTION6_USER_CLASS, 2)))
	    {
	      void *enc_opt, *enc_end = opt6_ptr(opt, opt6_len(opt));
	      for (enc_opt = opt6_ptr(opt, 0); enc_opt; enc_opt = opt6_next(enc_opt, enc_end))
		lease_add_extradata(lease, opt6_ptr(enc_opt, 0), opt6_len(enc_opt), 0);
	    }
	}
#endif	
      
    }
}
			  
			
	
static void log6_opts(int nest, unsigned int xid, void *start_opts, void *end_opts)
{
  void *opt;
  char *desc = nest ? "nest" : "sent";
  
  if (!option_bool(OPT_LOG_OPTS) || start_opts == end_opts)
    return;
  
  for (opt = start_opts; opt; opt = opt6_next(opt, end_opts))
    {
      int type = opt6_type(opt);
      void *ia_options = NULL;
      char *optname;
      
      if (type == OPTION6_IA_NA)
	{
	  sprintf(daemon->namebuff, "IAID=%u T1=%u T2=%u",
		  opt6_uint(opt, 0, 4), opt6_uint(opt, 4, 4), opt6_uint(opt, 8, 4));
	  optname = "ia-na";
	  ia_options = opt6_ptr(opt, 12);
	}
      else if (type == OPTION6_IA_TA)
	{
	  sprintf(daemon->namebuff, "IAID=%u", opt6_uint(opt, 0, 4));
	  optname = "ia-ta";
	  ia_options = opt6_ptr(opt, 4);
	}
      else if (type == OPTION6_IAADDR)
	{
	  struct in6_addr addr;

	  /* align */
	  memcpy(&addr, opt6_ptr(opt, 0), IN6ADDRSZ);
	  inet_ntop(AF_INET6, &addr, daemon->addrbuff, ADDRSTRLEN);
	  sprintf(daemon->namebuff, "%s PL=%u VL=%u", 
		  daemon->addrbuff, opt6_uint(opt, 16, 4), opt6_uint(opt, 20, 4));
	  optname = "iaaddr";
	  ia_options = opt6_ptr(opt, 24);
	}
      else if (type == OPTION6_STATUS_CODE)
	{
	  int len = sprintf(daemon->namebuff, "%u ", opt6_uint(opt, 0, 2));
	  memcpy(daemon->namebuff + len, opt6_ptr(opt, 2), opt6_len(opt)-2);
	  daemon->namebuff[len + opt6_len(opt) - 2] = 0;
	  optname = "status";
	}
      else
	{
	  /* account for flag byte on FQDN */
	  int offset = type == OPTION6_FQDN ? 1 : 0;
	  optname = option_string(AF_INET6, type, opt6_ptr(opt, offset), opt6_len(opt) - offset, daemon->namebuff, MAXDNAME);
	}
      
      my_syslog(MS_DHCP | LOG_INFO, "%u %s size:%3d option:%3d %s  %s", 
		xid, desc, opt6_len(opt), type, optname, daemon->namebuff);
      
      if (ia_options)
	log6_opts(1, xid, ia_options, opt6_ptr(opt, opt6_len(opt)));
    }
}		 
 
static void log6_quiet(struct state *state, char *type, struct in6_addr *addr, char *string)
{
  if (option_bool(OPT_LOG_OPTS) || !option_bool(OPT_QUIET_DHCP6))
    log6_packet(state, type, addr, string);
}

static void log6_packet(struct state *state, char *type, struct in6_addr *addr, char *string)
{
  int clid_len = state->clid_len;

  /* avoid buffer overflow */
  if (clid_len > 100)
    clid_len = 100;
  
  print_mac(daemon->namebuff, state->clid, clid_len);

  if (addr)
    {
      inet_ntop(AF_INET6, addr, daemon->dhcp_buff2, DHCP_BUFF_SZ - 1);
      strcat(daemon->dhcp_buff2, " ");
    }
  else
    daemon->dhcp_buff2[0] = 0;

  if(option_bool(OPT_LOG_OPTS))
    my_syslog(MS_DHCP | LOG_INFO, "%u %s(%s) %s%s %s",
	      state->xid, 
	      type,
	      state->iface_name, 
	      daemon->dhcp_buff2,
	      daemon->namebuff,
	      string ? string : "");
  else
    my_syslog(MS_DHCP | LOG_INFO, "%s(%s) %s%s %s",
	      type,
	      state->iface_name, 
	      daemon->dhcp_buff2,
	      daemon->namebuff,
	      string ? string : "");
}

static void *opt6_find (uint8_t *opts, uint8_t *end, unsigned int search, unsigned int minsize)
{
  u16 opt, opt_len;
  void *start;
  
  if (!opts)
    return NULL;
    
  while (1)
    {
      if (end - opts < 4) 
	return NULL;
      
      start = opts;
      GETSHORT(opt, opts);
      GETSHORT(opt_len, opts);
      
      if (opt_len > (end - opts))
	return NULL;
      
      if (opt == search && (opt_len >= minsize))
	return start;
      
      opts += opt_len;
    }
}

static void *opt6_next(uint8_t *opts, uint8_t *end)
{
  u16 opt_len;
  
  if (end - opts < 4) 
    return NULL;
  
  opts += 2;
  GETSHORT(opt_len, opts);
  
  if (opt_len >= (end - opts))
    return NULL;
  
  return opts + opt_len;
}

static unsigned int opt6_uint(unsigned char *opt, int offset, int size)
{
  /* this worries about unaligned data and byte order */
  unsigned int ret = 0;
  int i;
  unsigned char *p = opt6_ptr(opt, offset);
  
  for (i = 0; i < size; i++)
    ret = (ret << 8) | *p++;
  
  return ret;
} 

int relay_upstream6(int iface_index, ssize_t sz, 
		    struct in6_addr *peer_address, u32 scope_id, time_t now)
{
  unsigned char *header;
  unsigned char *inbuff = daemon->dhcp_packet.iov_base;
  int msg_type = *inbuff;
  int hopcount, o;
  struct in6_addr multicast;
  unsigned int maclen, mactype;
  unsigned char mac[DHCP_CHADDR_MAX];
  struct dhcp_relay *relay;
  
  for (relay = daemon->relay6; relay; relay = relay->next)
    if (relay->iface_index != 0 && relay->iface_index == iface_index)
      break;

  /* No relay config. */
  if (!relay)
    return 0;
  
  inet_pton(AF_INET6, ALL_SERVERS, &multicast);
  get_client_mac(peer_address, scope_id, mac, &maclen, &mactype, now);
  
  /* Get hop count from nested relayed message */ 
  if (msg_type == DHCP6RELAYFORW)
    hopcount = *((unsigned char *)inbuff+1) + 1;
  else
    hopcount = 0;

  reset_counter();

  /* RFC 3315 HOP_COUNT_LIMIT */
  if (hopcount > 32 || !(header = put_opt6(NULL, 34)))
    return 1;
  
  header[0] = DHCP6RELAYFORW;
  header[1] = hopcount;
  memcpy(&header[18], peer_address, IN6ADDRSZ);
  
  /* RFC-6939 */
  if (maclen != 0)
    {
      o = new_opt6(OPTION6_CLIENT_MAC);
      put_opt6_short(mactype);
      put_opt6(mac, maclen);
      end_opt6(o);
    }
  
  o = new_opt6(OPTION6_RELAY_MSG);
  put_opt6(inbuff, sz);
  end_opt6(o);
  
  for (; relay; relay = relay->next)
    if (relay->iface_index != 0 && relay->iface_index == iface_index)
      {
	union mysockaddr to;

	memcpy(&header[2], &relay->local.addr6, IN6ADDRSZ);
	
	to.sa.sa_family = AF_INET6;
	to.in6.sin6_addr = relay->server.addr6;
#ifdef HAVE_SOCKADDR_SA_LEN
	to.in6.sin6_len = sizeof(struct sockaddr_in6);
#endif 
	to.in6.sin6_port = htons(relay->port);
	to.in6.sin6_flowinfo = 0;
	to.in6.sin6_scope_id = 0;
	
	if (IN6_ARE_ADDR_EQUAL(&relay->server.addr6, &multicast))
	  {
	    int multicast_iface;
	    if (!relay->interface || strchr(relay->interface, '*') ||
		(multicast_iface = if_nametoindex(relay->interface)) == 0 ||
		setsockopt(daemon->dhcp6fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &multicast_iface, sizeof(multicast_iface)) == -1)
	      {
		my_syslog(MS_DHCP | LOG_ERR, _("Cannot multicast DHCP relay via interface %s"), relay->interface);
		continue;
	      }
	  }
	
#ifdef HAVE_DUMPFILE
	dump_packet_udp(DUMP_DHCPV6, (void *)daemon->outpacket.iov_base, save_counter(-1), NULL, &to, daemon->dhcp6fd);
#endif

	while (retry_send(sendto(daemon->dhcp6fd, (void *)daemon->outpacket.iov_base, save_counter(-1),
				 0, (struct sockaddr *)&to, sa_len(&to))));
	
	if (option_bool(OPT_LOG_OPTS))
	  {
	    inet_ntop(AF_INET6, &relay->local, daemon->addrbuff, ADDRSTRLEN);
	    if (IN6_ARE_ADDR_EQUAL(&relay->server.addr6, &multicast))
	      snprintf(daemon->namebuff, MAXDNAME, _("multicast via %s"), relay->interface);
	    else
	      inet_ntop(AF_INET6, &relay->server, daemon->namebuff, ADDRSTRLEN);
	    my_syslog(MS_DHCP | LOG_INFO, _("DHCP relay at %s -> %s"), daemon->addrbuff, daemon->namebuff);
	  }
	
      }
  
  return 1;
}

int relay_reply6(struct sockaddr_in6 *peer, ssize_t sz, char *arrival_interface)
{
  struct dhcp_relay *relay;
  struct in6_addr link;
  unsigned char *inbuff = daemon->dhcp_packet.iov_base;
  
  /* must have at least msg_type+hopcount+link_address+peer_address+minimal size option
     which is               1   +    1   +    16      +     16     + 2 + 2 = 38 */
  
  if (sz < 38 || *inbuff != DHCP6RELAYREPL)
    return 0;
  
  memcpy(&link, &inbuff[2], IN6ADDRSZ); 
  
  for (relay = daemon->relay6; relay; relay = relay->next)
    if (IN6_ARE_ADDR_EQUAL(&link, &relay->local.addr6) &&
	(!relay->interface || wildcard_match(relay->interface, arrival_interface)))
      break;
      
  reset_counter();

  if (relay)
    {
      void *opt, *opts = inbuff + 34;
      void *end = inbuff + sz;
      
      if ((opt = opt6_find(opts, end, OPTION6_RELAY_MSG, 4)))
	{
	  int encap_type = opt6_uint(opt, 0, 1);
	  put_opt6(opt6_ptr(opt, 0), opt6_len(opt));
	  memcpy(&peer->sin6_addr, &inbuff[18], IN6ADDRSZ); 
	  peer->sin6_scope_id = relay->iface_index;

	  if (encap_type == DHCP6RELAYREPL)
	    {
	      peer->sin6_port = ntohs(DHCPV6_SERVER_PORT);
	      return 1;
	    }
	  
	  peer->sin6_port = ntohs(DHCPV6_CLIENT_PORT);
	  
#ifdef HAVE_SCRIPT
	  if (daemon->lease_change_command && encap_type == DHCP6REPLY)
	    {
	      /* skip over message type and transaction-id. to get to options. */
	      opts = opt6_ptr(opt, 4);
	      end = opt6_ptr(opt, opt6_len(opt));

	      if ((opt = opt6_find(opts, end, OPTION6_IA_PD, 12)))
		{
		  opts = opt6_ptr(opt, 12);
		  end = opt6_ptr(opt, opt6_len(opt));
		  
		  for (opt = opt6_find(opts, end, OPTION6_IAPREFIX, 25); opt; opt = opt6_find(opt6_next(opt, end), end, OPTION6_IAPREFIX, 25))
		    /* valid lifetime must not be zero. */
		    if (opt6_uint(opt, 4, 4) != 0)
		      {
			if (daemon->free_snoops ||
			    (daemon->free_snoops = whine_malloc(sizeof(struct snoop_record))))
			  {
			    struct snoop_record *snoop = daemon->free_snoops;
			    
			    daemon->free_snoops = snoop->next;
			    snoop->client = peer->sin6_addr;
			    snoop->prefix_len = opt6_uint(opt, 8, 1); 
			    memcpy(&snoop->prefix, opt6_ptr(opt, 9), IN6ADDRSZ); 
			    snoop->next = relay->snoop_records;
			    relay->snoop_records = snoop;
			  }
		      }
		}	      
	    }
#endif
	  return 1;
	}
    }
  
  return 0;
}
  
#ifdef HAVE_SCRIPT
int do_snoop_script_run(void)
{
  struct dhcp_relay *relay;
  struct snoop_record *snoop;
  
  for (relay = daemon->relay6; relay; relay = relay->next)
    if ((snoop = relay->snoop_records))
      {
	relay->snoop_records = snoop->next;
	snoop->next = daemon->free_snoops;
	daemon->free_snoops = snoop;
	
	queue_relay_snoop(&snoop->client, relay->iface_index, &snoop->prefix, snoop->prefix_len);
	return 1;
      }
  
  return 0;
}
#endif

#endif
