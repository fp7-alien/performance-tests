#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <net/ethernet.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <limits.h>
#include <math.h>
#include <gsl/gsl_statistics.h>
#include <test_module.h>

#include "log.h"
#include "msg.h"
#include "traffic_generator.h"

/** String for scheduling events
 */
#define BYESTR "bye bye"
#define WRITEPACKET "write packet"
#define PRINTCOUNT "print"

/** 
 * String for scheduling events
 */
#define BYESTR "bye bye"
#define SNMPGET "snmp get"

/**
 * packet size limits
 */
#define MIN_PKT_SIZE 64
#define MAX_PKT_SIZE 1500

/**
 *
 */
uint64_t proberate = 100; 
/**
 * calculated sending time interval (measured in usec). 
 */
uint64_t probe_snd_interval;

/**
 * Number of flows to send. 
 */
int flows = 1000;
char *cli_param;
char *network = "192.168.3.0";
int pkt_size = 1500;
int finished = 0;
int count = 0;

/**
 * Some constants to help me with conversions
 */
const uint64_t sec_to_usec = 1000000;
const uint64_t byte_to_bits = 8, mbits_to_bits = 1024*1024;

struct entry {
  struct timeval snd,rcv;
  int ch, id;
  TAILQ_ENTRY(entry) entries;         /* Tail queue. */
}; 
TAILQ_HEAD(tailhead, entry) head;

/**@ingroup modules
 * Packet in module.
 * The module sends packet into a port to generate packet-in events.
 * The rate, count and delay then determined.
 *
 * Copyright (C) Stanford University, 2009
 * @author ykk
 * @date September, 2009
 * 
 * @return name of module
 */
char * name()
{
	return "Pkt_in_module";
}

/** Initialization
 * @param ctx pointer to opaque context
 */
int start(struct oflops_context * ctx) {
  struct timeval now;
  gettimeofday(&now, NULL);
  void *b;
  char msg[1024];
  int res;

  //init measurement queue
  TAILQ_INIT(&head); 

  //Initialize pap-based  tcp flow reassembler for the communication 
  //channel
  msg_init();
  snprintf(msg, 1024,  "Intializing module %s", name());

  //log when I start module
  gettimeofday(&now, NULL);
  oflops_log(now, GENERIC_MSG, msg);
  oflops_log(now,GENERIC_MSG , cli_param);

  //start openflow session with switch
  make_ofp_hello(&b);
  res = oflops_send_of_mesgs(ctx, b, sizeof(struct ofp_hello));
  free(b);  

  //send a message to clean up flow tables. 
  printf("cleaning up flow table...\n");
  res = make_ofp_flow_del(&b);
  res = oflops_send_of_mesg(ctx, b);  
  free(b);

  //get port and cpu status from switch 
  gettimeofday(&now, NULL);
  add_time(&now, 1, 0);
  oflops_schedule_timer_event(ctx,&now, SNMPGET);
 
  //Schedule end
  gettimeofday(&now, NULL);
  add_time(&now, 30, 0);
  oflops_schedule_timer_event(ctx,&now, BYESTR);

  return 0;
}

/** Handle timer  * @param ctx pointer to opaque context
 * @param te pointer to timer event
 */
int handle_timer_event(struct oflops_context * ctx, struct timer_event *te)
{
  struct timeval now;
  char * str;

  gettimeofday(&now,NULL);
  str = (char *) te->arg;
 
  if(!strcmp(str,SNMPGET)) {
    oflops_snmp_get(ctx, ctx->cpuOID, ctx->cpuOID_len);
    int i;
    for(i=0;i<ctx->n_channels;i++) {
      oflops_snmp_get(ctx, ctx->channels[i].inOID, ctx->channels[i].inOID_len);
      oflops_snmp_get(ctx, ctx->channels[i].outOID, ctx->channels[i].outOID_len);
    }  
    gettimeofday(&now, NULL);
    add_time(&now, 1, 0);
    oflops_schedule_timer_event(ctx,&now, SNMPGET);
  } else if(!strcmp(str,BYESTR)) {
    oflops_end_test(ctx,1);
  } else
    fprintf(stderr, "Unknown timer event: %s", str);
  return 0;
}

int 
destroy(oflops_context *ctx) {
  struct entry *np;
  long double t = 0, t_sq = 0;
  int ix = 0, min_id =  INT_MAX, max_id =  INT_MIN, delay;
  float loss;
  double *data;
  char msg[1024];	
  char msg1[1024];
  uint32_t mean, std, median;
  struct timeval now;
  gettimeofday(&now, NULL);
   data = xmalloc(count*sizeof(double));

  for (np = head.tqh_first; np != NULL; np = np->entries.tqe_next) {
    min_id = (np->id < min_id)?np->id:min_id;
    max_id = (np->id > max_id)?np->id:max_id;
    data[ix ++] = time_diff(&np->snd, &np->rcv);
    //data[ix ++] = (double)delay;
if(1)
      if(printf("%lu;%lu.%06lu;%lu.%06lu:%d;\n", 
		 (long unsigned int)np->id,  
		 (long unsigned int)np->snd.tv_sec, 
		 (long unsigned int)np->snd.tv_usec,
		 (long unsigned int)np->rcv.tv_sec, 
		 (long unsigned int)np->rcv.tv_usec,ix-1) < 0)  
	perror_and_exit("fprintf fail", 1); 
    printf("Probe packet: %d, delay: %f\n",ix, data[ix-1]);
    free(np);
  }
  if(ix > 0) {
      gsl_sort (data, 1, ix);
      mean = (uint32_t)gsl_stats_mean(data, 1, ix);
      std = (uint32_t)sqrt(gsl_stats_variance(data, 1, ix));
      median = (uint32_t)gsl_stats_median_from_sorted_data (data, 1, ix);
      loss = (float)ix/(float)(max_id - min_id+1);
      
      snprintf(msg, 1024, "statistics:port:%d:%u:%u:%u:%.4f:%d", 
	       ctx->channels[1].of_port, mean, median, std, loss, count);
      printf("statistics:port:%u:%u:%u:%.4f:%d\n", mean, median, std, loss, count);
      oflops_log(now, GENERIC_MSG, msg);
  }
  return 0;
}

/** Register pcap filter.
 * @param ctx pointer to opaque context
 * @param ofc enumeration of channel that filter is being asked for
 * @param filter filter string for pcap * @param buflen length of buffer
 */
int 
get_pcap_filter(struct oflops_context *ctx, oflops_channel_name ofc, 
		char * filter, int buflen) {
  if (ofc == OFLOPS_CONTROL) {
    return snprintf(filter, buflen, "port %d",  ctx->listen_port);
  }
  return 0;
}

/** Handle pcap event.
 * @param ctx pointer to opaque context
 * @param pe pcap event
 * @param ch enumeration of channel that pcap event is triggered
 */
int handle_pcap_event(struct oflops_context *ctx, struct pcap_event * pe, oflops_channel_name ch) {
  struct pktgen_hdr *pktgen;
  int dir, len;
  struct ofp_header *ofp;
  struct pcap_event *ofp_msg;
  struct ofp_packet_in *pkt_in = NULL;
  char msg[1024];
  struct flow fl;
  struct in_addr addr;
  struct ofp_error_msg *err_p = NULL;

  if (ch == OFLOPS_CONTROL) {
    dir = append_data_to_flow(pe->data,pe->pcaphdr);
    while(contains_next_msg(dir) > 0) {
      len = get_next_msg(dir, &ofp_msg);
      ofp = (struct ofp_header *)ofp_msg->data;
      switch(ofp->type) {
      case  OFPT_ECHO_REQUEST:
	err_p = (struct ofp_error_msg *)ofp;
	printf("%ld.%06ld: OFPT_ECHO_REQUEST %d\n", pe->pcaphdr.ts.tv_sec, pe->pcaphdr.ts.tv_usec, ntohl(ofp->xid));
//	if((ntohl(ofp->xid) < 100) && (ntohl(ofp->xid) > 0))
//	  memcpy(&echo_data[ntohl(ofp->xid)].snd, &pe->pcaphdr.ts, sizeof(struct timeval));
	break;   
      case  OFPT_ECHO_REPLY:
	err_p = (struct ofp_error_msg *)ofp;
	printf("%ld.%06ld: OFPT_ECHO_REPLY %d\n", pe->pcaphdr.ts.tv_sec, pe->pcaphdr.ts.tv_usec, ntohl(ofp->xid));
	//if( (ntohl(ofp->xid) < 100) && (ntohl(ofp->xid) > 0))
	  //memcpy(&echo_data[ntohl(ofp->xid)].rcv, &pe->pcaphdr.ts, sizeof(struct timeval));
	break;  
      }
      if(ofp->type ==  OFPT_PACKET_IN) {
	pkt_in = (struct ofp_error_msg *)ofp;
	//printf("pcaphdr.caplen : %d, sizeof(struct ofp_packet_in)): %d \n", pe->pcaphdr.caplen, sizeof(struct ofp_packet_in));
	pktgen = extract_pktgen_pkt(pkt_in->data, pe->pcaphdr.caplen - sizeof(struct ofp_packet_in), &fl);
	//int counter;
	//unsigned char * b = (unsigned char *) pktgen;
	//printf("Address depacket %p \n", pktgen);
	//for(counter = 0; counter < sizeof(struct pktgen_hdr); counter ++)
	//	printf("%02x ", *(b + counter));
	//printf("\n");
	if(pktgen == NULL) 
	  return 0;
		addr.s_addr = fl.nw_dst;
	struct entry *n1 = malloc(sizeof(struct entry));
	n1->snd.tv_sec = htonl(pktgen->time.tv_sec);
	n1->snd.tv_usec = htonl(pktgen->time.tv_usec);
	//printf("Generated host : %d s, %d us\n", pktgen->tv_sec, pktgen->tv_usec);
	//printf("Generated: %d s, %d us,Received : %d s, %d us\n",(int) n1->snd.tv_sec,(int) n1->snd.tv_usec, pe->pcaphdr.ts.tv_sec, pe->pcaphdr.ts.tv_usec);
	memcpy(&n1->rcv, &pe->pcaphdr.ts, sizeof(struct timeval));
	n1->id = htonl(pktgen->seq_num);
	n1->ch = ch;
	count++;
	TAILQ_INSERT_TAIL(&head, n1, entries);

	snprintf(msg, 1024, 
		 "%llu.%llu:%d:%s:%d:%d", 
		 (unsigned long long)ntohl(pktgen->tv_sec),
		 (unsigned long long)ntohl(pktgen->tv_usec),
		 ntohl(pkt_in->buffer_id),
		 inet_ntoa(addr),
		 ntohl(pktgen->seq_num),
		 ntohs(ofp->length));
	//printf("%s\n", msg);
	oflops_log(pe->pcaphdr.ts, OFPT_PACKET_IN_MSG, msg);
	//fprintf(stderr, "%s\n", msg);
      }
    }
  }
  else
    fprintf(stderr, "wtf! why channel %u?", ch);

  return 0;
}

int 
handle_snmp_event(struct oflops_context * ctx, struct snmp_event * se) {
  netsnmp_variable_list *vars;
  int len = 1024, i;
  char msg[1024], log[1024];
  struct timeval now;

  for(vars = se->pdu->variables; vars; vars = vars->next_variable)  {
    snprint_value(msg, len, vars->name, vars->name_length, vars);
    if((vars->name_length == ctx->cpuOID_len) &&
       (memcmp(vars->name, ctx->cpuOID,  ctx->cpuOID_len * sizeof(oid)) == 0) ) {
      snprintf(log, len, "cpu : %s %%", msg);
      oflops_log(now, SNMP_MSG, log);
    } else {
      for(i=0;i<ctx->n_channels;i++) {
	if((vars->name_length == ctx->channels[i].inOID_len) &&
	   (memcmp(vars->name, ctx->channels[i].inOID, ctx->channels[i].inOID_len * sizeof(oid)) == 0) ) {
	  snprintf(log, len, "port %d : rx %s pkts",  
		   (int)ctx->channels[i].outOID[ctx->channels[i].outOID_len-1], msg);
	  oflops_log(now, SNMP_MSG, log);
	  break;
	}
	
	if((vars->name_length == ctx->channels[i].outOID_len) &&
	   (memcmp(vars->name, ctx->channels[i].outOID, ctx->channels[i].outOID_len * sizeof(oid))==0) ) {
	  snprintf(log, len, "port %d : tx %s pkts",  
		   (int)ctx->channels[i].outOID[ctx->channels[i].outOID_len-1], msg);
	  oflops_log(now, SNMP_MSG, log);
	  break;
	}
      } //for
    }// if cpu
  }
  return 0;
}

int
handle_traffic_generation (oflops_context *ctx) {
  struct traf_gen_det det;
  struct in_addr ip;
  char *str_ip;
  init_traf_gen(ctx);
  strcpy(det.src_ip,"10.1.1.1");
  strcpy(det.dst_ip_min,"192.168.3.1");
  ip.s_addr = ntohl(inet_addr("192.168.3.1"));
  ip.s_addr += flows;
  ip.s_addr = htonl(ip.s_addr);
  str_ip = inet_ntoa(ip);
  strcpy(det.dst_ip_max, str_ip);
  strcpy(det.mac_dst,"08:00:27:be:63:fe");
  strcpy(det.mac_src,"08:00:27:5c:c0:40");
  det.vlan = 0xffff;
  det.vlan_p = 0;
  det.vlan_cfi = 0;
  det.udp_src_port = 8080;
  det.udp_dst_port = 8080;
  det.pkt_size = pkt_size;
  det.delay = probe_snd_interval*1000;
  add_traffic_generator(ctx, OFLOPS_DATA1, &det);  
  
  start_traffic_generator(ctx);
  return 1;
}

/**
 * Initialization code with parameters
 * @param ctx 
 */
int init(struct oflops_context *ctx, char * config_str) {
  char *pos = NULL;
  char *param = config_str;
  char *value = NULL;
  struct timeval now;

  //init counters
  finished = 0;
  gettimeofday(&now, NULL);
  cli_param = strdup(config_str);

  while(*config_str == ' ') {
    config_str++;
  }
  param = config_str;
  while(1) {
    pos = index(param, ' ');

    if((pos == NULL)) {
      if (*param != '\0') {
        pos = param + strlen(param) + 1;
      } else
        break;
    }
    *pos='\0';
    pos++;
    value = index(param,'=');
    *value = '\0';
    value++;
    //fprintf(stderr, "param = %s, value = %s\n", param, value);
    if(value != NULL) {
      if(strcmp(param, "pkt_size") == 0) {
        //parse int to get pkt size
        pkt_size = strtol(value, NULL, 0);
        if((pkt_size < MIN_PKT_SIZE) && (pkt_size > MAX_PKT_SIZE))
          perror_and_exit("Invalid packet size value", 1);
      }  else if(strcmp(param, "probe_rate") == 0) {
        //parse int to get measurement probe rate
        proberate = strtol(value, NULL, 0);
        if((proberate <= 0) || (proberate >= 1010)) 
          perror_and_exit("Invalid probe rate param(Value between 1 and 1010)", 1);
      } else if(strcmp(param, "flows") == 0) {
	//parse int to get pkt size
        flows = strtol(value, NULL, 0);
        if(flows <= 0)  
          perror_and_exit("Invalid flow number", 1);
      } else 
        fprintf(stderr, "Invalid parameter:%s\n", param);
      param = pos;
    }
  } 

  //calculate sendind interval
  probe_snd_interval = (pkt_size * byte_to_bits * sec_to_usec) / (proberate * mbits_to_bits);
  fprintf(stderr, "Sending probe interval : %u usec (pkt_size: %u bytes, rate: %u Mbits/sec )\n", 
	  (uint32_t)probe_snd_interval, (uint32_t)pkt_size, (uint32_t)proberate);
  return 0;
}
