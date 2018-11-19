/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#include <iostream>
#include <trace.h>
#include <tools/random.h>
#include "ns-agent.h"

int hdr_dsr::offset_;

static class DSRUUHeaderClass:public PacketHeaderClass {
	
public:
	DSRUUHeaderClass():PacketHeaderClass("PacketHeader/DSRUU",
					     DSR_OPTS_MAX_SIZE) {
		bind_offset(&hdr_dsr::offset_);
	}
} class_rtProtoDSRUU_hdr;

/* Link to OTcl name space */
static class DSRUUClass : public TclClass {
public:
	DSRUUClass() : TclClass("Agent/DSRUU") {}
	TclObject* create(int, const char*const*) {
		return (new DSRUU);
	}
} class_DSRUU;


int DSRUU::confvals[CONFVAL_MAX];

DSRUU::DSRUU() : Agent(PT_DSR), 
		 ack_timer(this, "ACKTimer"), 
		 grat_rrep_tbl_timer(this, "GratRREPTimer"), 
		 send_buf_timer(this, "SendBufTimer"), 
		 neigh_tbl_timer(this, "NeighTblTimer"), 
		 lc_timer(this, "LinkCacheTimer")
{
	int i;
	
	DEBUG("Initializing DSR Agent\n");
	/* Initialize Link Cache */
	
	trace_ = NULL; 
	mac_ = NULL;
	ll_ = NULL;
	ifq_ = NULL;
	node_ = NULL;
	
	for (i = 0; i < CONFVAL_MAX; i++) {
		char name[40];
		/* Override the default values in the ns-default.tcl file */
		confvals[i] = confvals_def[i].val;
		sprintf(name, "%s_", confvals_def[i].name);
		bind(name,  &confvals[i]);
	}
	
	/* Default values specific to simulation */
	set_confval(UseNetworkLayerAck, 0);
	set_confval(PrintDebug, 1);
	
	/* Initilize tables */
	lc_init();
	neigh_tbl_init();
	rreq_tbl_init();
	grat_rrep_tbl_init();
	maint_buf_init();
	send_buf_init();
	
	myaddr_.s_addr = 0;

	return;
}

DSRUU::~DSRUU()
{
	lc_cleanup();
	neigh_tbl_cleanup();
	rreq_tbl_cleanup();
	grat_rrep_tbl_cleanup();
	send_buf_cleanup();
 	maint_buf_cleanup();

	exit(-1);
}

int DSRUU::trace(const char *func, const char *fmt, ...)
{
	va_list args;
	int len;
#define BUF_LEN 1024
	static char buf[BUF_LEN];

	if (!trace_)
		return 0;
	
	va_start(args, fmt);
	
	len = sprintf(buf, "# %.5f:_%d_:%s: ", Scheduler::instance().clock(), 
		      myaddr_.s_addr, func);
	
	/* Emit the output into the temporary buffer */
	len += vsnprintf(buf+len, BUF_LEN - len, fmt, args);
	
	va_end(args);
	
	buf[len-1] = '\0';

/* Undefine to LOG to trace file */
#define DBG_TO_STDOUT

#ifdef DBG_TO_STDOUT
	printf("%s\n", buf);
	       
	fflush(stdout);
#else	
 	sprintf(trace_->pt_->buffer(), "%s", buf);

	trace_->pt_->dump();
#endif
	return len;
}

/* Should probably find a way to set the arp table entry manually... However,
 * sending a fake arp will do for now. */
int DSRUU::arpset(struct in_addr addr, unsigned int mac_addr)
{
	//create a new packet
	Packet *p = Packet::alloc();

	//access all the headers needed for ARP
	hdr_cmn *ch = HDR_CMN(p);
	char	*mh = (char*)HDR_MAC(p);
	hdr_ll  *lh = HDR_LL(p);
	hdr_arp *ah = HDR_ARP(p);
	
	ch->size() = ARP_HDR_LEN;
	ch->error() = 0;
	ch->direction() = hdr_cmn::UP; // send this pkt UP to LL
	ch->ptype() = PT_ARP;

	mac_->hdr_dst(mh, this->macaddr_);
	mac_->hdr_src(mh, mac_addr);
	mac_->hdr_type(mh, ETHERTYPE_ARP);

	lh->seqno() = 0;
	lh->lltype() = LL_DATA;

	ah->arp_op  = ARPOP_REPLY;
	ah->arp_tha = this->macaddr_;
	ah->arp_sha = mac_addr;
		
	ah->arp_spa = (nsaddr_t)addr.s_addr;
	ah->arp_tpa = (nsaddr_t)myaddr_.s_addr;

	ll_->recv(p, 0);

	return 1;
}

/* Xmit failure - salvage packets. With link layer feedback we do not really use
 * the DSR mainenance buffer since we can grab the packets from the interface
 * queue.
 */
void DSRUU::xmit_failed(Packet *p)
{
	Packet *qp;
	struct dsr_pkt *dp;
	struct in_addr dst, nxt_hop;
	hdr_mac *mh;
	hdr_cmn *cmh;
	hdr_ip *iph;
	
	mh = HDR_MAC(p);
	cmh = HDR_CMN(p);
	iph = HDR_IP(p);

	dst.s_addr = iph->daddr();
	nxt_hop.s_addr = cmh->next_hop();

	DEBUG("Xmit failure for %s nxt_hop=%s\n", 
	      print_ip(dst), print_ip(nxt_hop));
 		
	lc_link_del(my_addr(), nxt_hop);
	
	dp = dsr_pkt_alloc(p);
		
	if (!dp) {
		DEBUG("Could not allocate DSR packet\n");
		drop(p, DROP_RTR_NO_ROUTE);
	} else {
		dsr_rerr_send(dp, nxt_hop);

		dp->nxt_hop = nxt_hop;
		
		maint_buf_salvage(dp);
	}
	/* Salvage the other packets still in the interface queue with the same
	 * next hop */
	while ((qp = ifq_->prq_get_nexthop(cmh->next_hop()))) {
		
		dp = dsr_pkt_alloc(qp);
		
		if (!dp) {
			drop(qp, DROP_RTR_NO_ROUTE);
			continue;
		}
		
		dp->nxt_hop = nxt_hop;
		
		maint_buf_salvage(dp);			
	}
	return;
}


void xmit_failure(Packet *p, void *data)
{
	DSRUU *agent = (DSRUU *)data;
	agent->xmit_failed(p);
}


struct hdr_ip *DSRUU::dsr_build_ip(struct dsr_pkt *dp, struct in_addr src, 
				    struct in_addr dst, int ip_len, 
				    int tot_len, int protocol, int ttl)
{
	hdr_cmn *cmh;
	
	dp->nh.iph = &dp->ip_data;
	
	// Set IP header fields
	dp->nh.iph->saddr() = (nsaddr_t)dp->src.s_addr;
	dp->nh.iph->daddr() = (nsaddr_t)dp->dst.s_addr;
	dp->nh.iph->ttl() = ttl;
	
	if (dp->p) {
		cmh = HDR_CMN(dp->p);
		cmh->ptype() = (packet_t)protocol;
	}
	return dp->nh.iph;
}

Packet *DSRUU::ns_packet_create(struct dsr_pkt *dp)
{
	hdr_mac *mh;
	hdr_cmn *cmh;
	hdr_ip *iph;	
	int tot_len;

	if (!dp)
		return NULL;
	
	if (!dp->p) 
		dp->p = allocpkt();

	tot_len = IP_HDR_LEN + dsr_pkt_opts_len(dp) + dp->payload_len;
	
	mh = HDR_MAC(dp->p);
	cmh = HDR_CMN(dp->p);
	iph = HDR_IP(dp->p);
	
	if (dp->dst.s_addr == DSR_BROADCAST) {
		cmh->addr_type() = NS_AF_NONE;
	} else {
		struct neighbor_info neigh_info;
		int mac_dst;
		
		if (!neigh_tbl_query(dp->nxt_hop, &neigh_info)) {
			DEBUG("No next hop MAC address in neigh_tbl\n");
			Packet::free(dp->p);
			return NULL;
		}

		ethtoint((char *)&neigh_info.hw_addr, &mac_dst);

		/* Broadcast packet */
		mac_->hdr_dst((char*) HDR_MAC(dp->p), mac_dst);
		cmh->addr_type() = NS_AF_INET;

		// Generate fake ARP 
		arpset(dp->nxt_hop, mac_dst);
	}

	// Copy message contents into packet
	if (dsr_pkt_opts_len(dp)) {
		dsr_opt_hdr *opth = HDR_DSRUU(dp->p);

		memset(opth, 0, dsr_pkt_opts_len(dp));
		DEBUG("Building packet dsr_opts_len=%d p_len=%d opth->size=%d\n", 
		      dsr_pkt_opts_len(dp), ntohs(dp->dh.opth->p_len), opth->size());
		
		// Clear DSR part of packet		
		memcpy(opth, dp->dh.raw, dsr_pkt_opts_len(dp));
	}
	/* Add payload */
// 	if (dp->payload_len && dp->payload)
// 		memcpy(dp->p->userdata(), dp->payload, dp->payload_len);
	
	// Set common header fields
	cmh->ptype() = PT_DSR;
	cmh->direction() = hdr_cmn::DOWN;
	cmh->size() = tot_len;
	cmh->iface() = -2;
	cmh->error() = 0;
	cmh->prev_hop_ = (nsaddr_t)myaddr_.s_addr;
	cmh->next_hop_ = (nsaddr_t)dp->nxt_hop.s_addr;
	
	memcpy(iph, dp->nh.iph, sizeof(hdr_ip));
	
	return dp->p;
}
	    
void DSRUU::ns_xmit(struct dsr_pkt *dp)
{
	Packet *p;
	
	struct hdr_cmn *cmh;
	struct hdr_ip *iph; 
	double jitter = 0;

 	if (dp->flags & PKT_REQUEST_ACK)	
 		maint_buf_add(dp);
	
	p = ns_packet_create(dp);

	if (!p) {
		DEBUG("Could not create packet\n");
		if (dp->p) 
			drop(dp->p, DROP_RTR_NO_ROUTE);
		goto out;
	}	

	iph = HDR_IP(p);
	cmh = HDR_CMN(p);
    
	/* If TTL = 0, drop packet */
	if (iph->ttl() == 0) {
		DEBUG("Dropping packet with TTL = 0.");
		drop(p, DROP_RTR_TTL);
		goto out;
	}

	DEBUG("xmitting pkt src=%d dst=%d nxt_hop=%d\n", 
	      iph->saddr(), iph->daddr(), cmh->next_hop_);
    
	/* Set packet fields depending on packet type */
	if (dp->flags & PKT_XMIT_JITTER) {
		/* Broadcast packet */
		jitter = Random::uniform() * ConfVal(BroadCastJitter) / 1000;
	
		DEBUG("xmit jitter=%f s\n", jitter);
	}		

	if (!ConfVal(UseNetworkLayerAck)) {
		cmh->xmit_failure_ = xmit_failure;
		cmh->xmit_failure_data_ = (void *) this;		
	}

	Scheduler::instance().schedule(ll_, p, jitter);
 out:
	dp->p = NULL;

	dsr_pkt_free(dp);
}

void DSRUU::ns_deliver(struct dsr_pkt *dp)
{
	int len, dsr_len = 0;
	
	struct hdr_cmn *cmh= hdr_cmn::access(dp->p);
	
	if (dp->dh.raw)
		len = dsr_opt_remove(dp);
	
	if (len) {
		dsr_len = len; //- sizeof(struct dsr_opt_hdr) + DSR_FIXED_HDR_LEN;
		DEBUG("Removed %d (%d real) bytes DSR options\n", dsr_len, len);	}
	cmh->size() -= IP_HDR_LEN - dsr_len;

	target_->recv(dp->p, (Handler*)0);
	
	dp->p = NULL;
	dsr_pkt_free(dp);
}


void DSRUU::recv(Packet* p, Handler*)
{
	struct dsr_pkt *dp;
	struct hdr_cmn *cmh = hdr_cmn::access(p);

	DEBUG("##########\n");

	dp = dsr_pkt_alloc(p);
	
	switch(cmh->ptype()) {
	case PT_DSR:
		if (dp->src.s_addr != myaddr_.s_addr) {
			dsr_recv(dp);
		} else {
			
// 			DEBUG("Locally generated DSR packet\n");
		}
		break;
	default:
		if (dp->src.s_addr == myaddr_.s_addr) {
			dp->payload_len += IP_HDR_LEN;
			DEBUG("Local packet without DSR header\n");
			dsr_start_xmit(dp);
		} else {
			// This shouldn't really happen ?
			DEBUG("Data packet from %s without DSR header!n", 
			      print_ip(dp->src));
			dsr_pkt_free(dp);
		}
	}
	return;
}

void DSRUU::tap(const Packet *p_in)
{
	struct dsr_pkt *dp;
	hdr_cmn *cmh = hdr_cmn::access(p_in);
	hdr_ip *iph = hdr_ip::access(p_in);
	struct in_addr next_hop, prev_hop;

	/* Cast the packet so that we can touch it */
	Packet *p = (Packet *)p_in;

	/* Do nothing for my own packets... */
	if ((unsigned int)iph->saddr() == myaddr_.s_addr)
		return;

	next_hop.s_addr = cmh->next_hop_;
	prev_hop.s_addr = cmh->prev_hop_;

	/* Do nothing for packets I am going to receive anyway */
	if (next_hop.s_addr == myaddr_.s_addr)
		return;
	    
	do {
		struct in_addr src, dst, next_hop;

		src.s_addr = iph->saddr();
		dst.s_addr = iph->daddr();
		
		next_hop.s_addr = cmh->next_hop_;
		DEBUG("###### Tap packet src=%s dst=%s prev_hop=%s next_hop=%s\n", 
		      print_ip(src), print_ip(dst), print_ip(prev_hop), print_ip(next_hop));
	} while (0);
	
	dp = dsr_pkt_alloc(p);
	dp->flags |= PKT_PROMISC_RECV;

	/* TODO: See if this node is the next hop. In that case do nothing */

	switch(cmh->ptype()) {
	case PT_DSR:
		if (dp->src.s_addr != myaddr_.s_addr) {
			//DEBUG("DSR packet from %s\n", print_ip(dp->src));
			dsr_recv(dp);
		} else {
// 			DEBUG("Locally gernerated DSR packet\n");
			dsr_pkt_free(dp);
		}
		break;
	default:
		// This shouldn't really happen ?
		DEBUG("Data packet from %s without DSR header!n", 
		      print_ip(dp->src));
		
		dsr_pkt_free(dp);
	}

	return;
}

enum {
	SET_ADDR,
	SET_MAC_ADDR,
	SET_NODE,
	SET_LL,
	SET_TAP,
	SET_DMUX,
	SET_TRACE_TARGET,
	START_DSR,
	MAX_CMD
};

char *cmd[MAX_CMD] = {
	"addr",
	"mac-addr",
	"node",
	"add-ll",
	"install-tap",
	"port-dmux",
	"tracetarget",
	"startdsr"
};

static int name2cmd(const char *name)
{
	int i;

	for (i = 0; i < MAX_CMD; i++) {
		if (strcasecmp(cmd[i], name) == 0) 
			return i;
	}
	return -1;
}

int DSRUU::command(int argc, const char* const* argv)
{	

	//cerr << "cmd=" << argv[1] << endl;

	switch (name2cmd(argv[1])) {
	case SET_ADDR:
		myaddr_.s_addr = Address::instance().str2addr(argv[2]);
		break;
	case SET_MAC_ADDR:
		macaddr_ = Address::instance().str2addr(argv[2]);
		break;
	case SET_NODE:
		node_ = (MobileNode *)TclObject::lookup(argv[2]);
		break;
	case SET_LL:
		ll_ = (LL *)TclObject::lookup(argv[2]);
		ifq_ = (CMUPriQueue *)TclObject::lookup(argv[3]);
		break;
	case SET_DMUX:
		break;
	case SET_TAP:
		mac_ = (Mac *)TclObject::lookup(argv[2]);
		macaddr_ = mac_->addr();
		mac_->installTap(this);
		break;
	case SET_TRACE_TARGET:
		trace_ = (Trace *)TclObject::lookup(argv[2]);
		break;
	case START_DSR:
		break;
	default:
		//cerr << "Unknown command " << argv[1] << endl;
		return Agent::command(argc, argv);
	}
	return TCL_OK;
}

void DSRUUTimer::expire (Event *e)
{
	if (a_)
		(a_->*function)(data);
}
