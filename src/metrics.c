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

const char * metric_names[] = {
    "dns_cache_inserted",
    "dns_cache_live_freed",
    "dns_queries_forwarded",
    "dns_auth_answered",
    "dns_local_answered",
    "dns_stale_answered",
    "dns_unanswered",
    "dnssec_max_crypto_use",
    "dnssec_max_sig_fail",
    "dnssec_max_work",
    "bootp",
    "pxe",
    "dhcp_ack",
    "dhcp_decline",
    "dhcp_discover",
    "dhcp_inform",
    "dhcp_nak",
    "dhcp_offer",
    "dhcp_release",
    "dhcp_request",
    "noanswer",
    "leases_allocated_4",
    "leases_pruned_4",
    "leases_allocated_6",
    "leases_pruned_6",
    "tcp_connections",
    "dhcp_leasequery",
    "dhcp_lease_unassigned",
    "dhcp_lease_actve",
    "dhcp_lease_unknown"
};

const char* get_metric_name(int i) {
    return metric_names[i];
}

void clear_metrics(void)
{
  int i;
  struct server *serv;
  
  for (i = 0; i < __METRIC_MAX; i++)
    daemon->metrics[i] = 0;

  for (serv = daemon->servers; serv; serv = serv->next)
    {
      serv->queries = 0;
      serv->failed_queries = 0;
      serv->failed_queries = 0;
      serv->retrys = 0;
      serv->nxdomain_replies = 0;
      serv->query_latency = 0;
    }
}
	
