/*
Copyright (c) 2013 Katja Rohloff <Katja.Rohloff@uni-jena.de>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <errno.h>
#include <signal.h>

#include <pcap/pcap.h>
#include <sndfile.h>

#include "listener_mrp_client.h"

#define DEBUG 0
#define PCAP 1
#define LIBSND 1

#define VERSION_STR "1.1"

#define ETHERNET_HEADER_SIZE (18)
#define SEVENTEEN22_HEADER_PART1_SIZE (4)
#define STREAM_ID_SIZE (8)
#define SEVENTEEN22_HEADER_PART2_SIZE (10)
#define SIX1883_HEADER_SIZE (10)
#define HEADER_SIZE (ETHERNET_HEADER_SIZE		\
			+ SEVENTEEN22_HEADER_PART1_SIZE \
			+ STREAM_ID_SIZE		\
			+ SEVENTEEN22_HEADER_PART2_SIZE \
			+ SIX1883_HEADER_SIZE)
#define HEADER_SIZE_AAF (36)
#define SAMPLES_PER_SECOND (48000)
#define SAMPLES_PER_FRAME (6)
#define SAMPLES_PER_FRAME_AAF (64)
#define CHANNELS (2)

struct mrp_listener_ctx *ctx_sig;//Context pointer for signal handler

struct ethernet_header{
	u_char dst[6];
	u_char src[6];
	u_char stuff[4];
	u_char type[2];
};

/* globals */

static const char *version_str = "simple_listener v" VERSION_STR "\n"
    "Copyright (c) 2012, Intel Corporation\n";

static const char *static_dest_mac = "91:E0:F0:00:FE:01";
static u_char static_stream_id[] = { 0x91, 0xE0, 0xF0, 0x00, 0xFE, 0x00, 0x00, 0x01 };

pcap_t* glob_pcap_handle;

u_char glob_ether_type[] = { 0x22, 0xf0 };
SNDFILE* glob_snd_file = NULL;
u_char* glob_target_stream_id = NULL;

u_char glob_no_srp = 0;
u_char glob_use_aaf = 0;

static void help()
{
	fprintf(stderr, "\n"
		"Usage: listener [-h] -i interface -f file_name.wav"
		"\n"
		"Options:\n"
		"    -h  show this message\n"
		"    -i  specify interface for AVB connection\n"
		"    -n  no SRP\n"
		"    -a  use AAF\n"
		"    -f  set the name of the output wav-file\n"
		"\n" "%s" "\n", version_str);
	exit(EXIT_FAILURE);
}

void pcap_callback(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet)
{
	unsigned char* test_stream_id;
	struct ethernet_header* eth_header;
	uint32_t* buf;
	uint32_t frame[2] = { 0 , 0 };

	int i;
	(void) args; /* unused */
	(void) packet_header; /* unused */

#if DEBUG
	fprintf(stdout,"Got packet.\n");
#endif /* DEBUG*/

	eth_header = (struct ethernet_header*)(packet);

#if DEBUG
	fprintf(stdout,"Ether Type: 0x%02x%02x\n", eth_header->type[0], eth_header->type[1]);
#endif /* DEBUG*/

	if (0 == memcmp(glob_ether_type,eth_header->type,sizeof(eth_header->type)))
	{
		test_stream_id = (unsigned char*)(packet + ETHERNET_HEADER_SIZE + SEVENTEEN22_HEADER_PART1_SIZE);

#if DEBUG
		fprintf(stderr, "Received stream id: %02x%02x%02x%02x%02x%02x%02x%02x\n ",
			     test_stream_id[0], test_stream_id[1],
			     test_stream_id[2], test_stream_id[3],
			     test_stream_id[4], test_stream_id[5],
			     test_stream_id[6], test_stream_id[7]);
#endif /* DEBUG*/

		if (0 == memcmp(test_stream_id, glob_target_stream_id, sizeof(STREAM_ID_SIZE)))
		{

#if DEBUG
			fprintf(stdout,"Stream ids matched.\n");
#endif /* DEBUG*/
			buf = (uint32_t*)(packet + HEADER_SIZE);
			for(i = 0; i < SAMPLES_PER_FRAME * CHANNELS; i += 2)
			{
				memcpy(&frame[0], &buf[i], sizeof(frame));

				frame[0] = ntohl(frame[0]);   /* convert to host-byte order */
				frame[1] = ntohl(frame[1]);
				frame[0] &= 0x00ffffff;       /* ignore leading label */
				frame[1] &= 0x00ffffff;
				frame[0] <<= 8;               /* left-align remaining PCM-24 sample */
				frame[1] <<= 8;

				sf_writef_int(glob_snd_file, (const int *)frame, 1);
			}
		}
	}
}

void pcap_aaf_callback(u_char* args, const struct pcap_pkthdr* packet_header, const u_char* packet)
{
	unsigned char* test_stream_id;
	struct ethernet_header* eth_header;
	uint16_t* buf;
	uint16_t frame[2] = { 0 , 0 };

	int i;
	(void) args; /* unused */
	(void) packet_header; /* unused */

#if DEBUG
	fprintf(stdout,"Got packet.\n");
#endif /* DEBUG*/

	eth_header = (struct ethernet_header*)(packet);

#if DEBUG
	fprintf(stdout,"Ether Type: 0x%02x%02x\n", eth_header->type[0], eth_header->type[1]);
#endif /* DEBUG*/

	if (0 == memcmp(glob_ether_type,eth_header->type,sizeof(eth_header->type)))
	{
		test_stream_id = (unsigned char*)(packet + ETHERNET_HEADER_SIZE + SEVENTEEN22_HEADER_PART1_SIZE);

#if DEBUG
		fprintf(stderr, "Received stream id: %02x%02x%02x%02x%02x%02x%02x%02x\n ",
			     test_stream_id[0], test_stream_id[1],
			     test_stream_id[2], test_stream_id[3],
			     test_stream_id[4], test_stream_id[5],
			     test_stream_id[6], test_stream_id[7]);
#endif /* DEBUG*/

		if (0 == memcmp(test_stream_id, glob_target_stream_id, sizeof(STREAM_ID_SIZE)))
		{

#if DEBUG
			fprintf(stdout,"Stream ids matched.\n");
#endif /* DEBUG*/
			buf = (uint16_t*)(packet + HEADER_SIZE_AAF);
			for(i = 0; i < SAMPLES_PER_FRAME_AAF * CHANNELS; i += 2)
			{
				memcpy(&frame[0], &buf[i], sizeof(frame));
				frame[0] = ntohs(frame[0]);   /* convert to host-byte order */
				frame[1] = ntohs(frame[1]);

				sf_writef_short(glob_snd_file, (const short *)frame, 1);
			}
		}
	}
}

void sigint_handler(int signum)
{
	int ret;

	fprintf(stdout,"Received signal %d:leaving...\n", signum);

	if (0 != ctx_sig->talker) {
		ret = send_leave(ctx_sig);
		if (ret)
			printf("send_leave failed\n");
	}

	if (2 > ctx_sig->control_socket)
	{
		close(ctx_sig->control_socket);
		ret = mrp_disconnect(ctx_sig);
		if (ret)
			printf("mrp_disconnect failed\n");
	}

#if PCAP
	if (NULL != glob_pcap_handle)
	{
		pcap_breakloop(glob_pcap_handle);
		pcap_close(glob_pcap_handle);
	}
#endif /* PCAP */

#if LIBSND
	sf_write_sync(glob_snd_file);
	sf_close(glob_snd_file);
#endif /* LIBSND */
}

int init_mrp(struct mrp_listener_ctx *ctx, struct mrp_domain_attr *class_a, struct mrp_domain_attr *class_b)// struct mrp_domain_attr *class_a, struct mrp_domain_attr *class_a)
{
	int rc;

	rc = mrp_listener_client_init(ctx);
	if (rc)
	{
		printf("failed to initialize global variables\n");
		return -1;
	}

	if (create_socket(ctx))
	{
		fprintf(stderr, "Socket creation failed.\n");
		return -1;
	}

	rc = mrp_monitor(ctx);
	if (rc)
	{
		printf("failed creating MRP monitor thread\n");
		return -1;
	}
	rc = mrp_get_domain(ctx, class_a, class_b);
	if (rc)
	{
		printf("failed calling mrp_get_domain()\n");
		return -1;
	}

	printf("detected domain Class A PRIO=%d VID=%04x...\n",class_a->priority,class_a->vid);

	rc = report_domain_status(class_a,ctx);
	if (rc) {
		printf("report_domain_status failed\n");
		return -1;
	}

	rc = join_vlan(class_a, ctx);
	if (rc) {
		printf("join_vlan failed\n");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char* file_name = NULL;
	char* dev = NULL;
	int sf_pcm_format = SF_FORMAT_PCM_24;
	char errbuf[PCAP_ERRBUF_SIZE];
	struct bpf_program comp_filter_exp;		/* The compiled filter expression */
	char filter_exp[100];				/* The filter expression */
	char dest_mac[30];
	pcap_handler callback = pcap_callback;
	struct mrp_listener_ctx *ctx = malloc(sizeof(struct mrp_listener_ctx));
	struct mrp_domain_attr *class_a = malloc(sizeof(struct mrp_domain_attr));
	struct mrp_domain_attr *class_b = malloc(sizeof(struct mrp_domain_attr));
	ctx_sig = ctx;
	signal(SIGINT, sigint_handler);
	ctx->MRPD_PORT = MRPD_PORT_DEFAULT;
	int c, rc;
	while((c = getopt(argc, argv, "anhi:f:P:")) > 0)
	{
		switch (c)
		{
		case 'h':
			help();
			break;
		case 'i':
			dev = strdup(optarg);
			break;
		case 'f':
			file_name = strdup(optarg);
			break;
		case 'n':
			glob_no_srp = 1;
			sprintf(dest_mac,"%s", static_dest_mac);
			glob_target_stream_id = static_stream_id;
			break;
		case 'a':
			glob_use_aaf = 1;
			sf_pcm_format = SF_FORMAT_PCM_16;
			callback = pcap_aaf_callback;
		case 'P':
			ctx->MRPD_PORT = (int) strtoul(optarg,NULL,10);
			break;
		break;
		default:
    	fprintf(stderr, "Unrecognized option!\n");
		}
	}

	if ((NULL == dev) || (NULL == file_name))
		help();

	if (0 == glob_no_srp)
	{
		if (-1 == init_mrp(ctx, class_a, class_b))
		{
			printf("init_mrp failed\n");
			return EXIT_FAILURE;
		}

		fprintf(stdout,"Waiting for talker...\n");
		await_talker(ctx);

#if DEBUG
		fprintf(stdout,"Send ready-msg...\n");
#endif /* DEBUG */
		rc = send_ready(ctx);
		if (rc) {
			printf("send_ready failed\n");
			return EXIT_FAILURE;
		}

		glob_target_stream_id = ctx->stream_id;
		sprintf(dest_mac,"%02x:%02x:%02x:%02x:%02x:%02x",
		ctx->dst_mac[0], ctx->dst_mac[1], ctx->dst_mac[2],
		ctx->dst_mac[3], ctx->dst_mac[4], ctx->dst_mac[5]);
	}

	if (NULL == glob_target_stream_id)
	{
		fprintf(stderr, "Target Stream ID not set");
		return EXIT_FAILURE;
	}

#if LIBSND
	SF_INFO* sf_info = (SF_INFO*)malloc(sizeof(SF_INFO));

	memset(sf_info, 0, sizeof(SF_INFO));

	sf_info->samplerate = SAMPLES_PER_SECOND;
	sf_info->channels = CHANNELS;
	sf_info->format = SF_FORMAT_WAV | sf_pcm_format;

	if (0 == sf_format_check(sf_info))
	{
		fprintf(stderr, "Wrong format.");
		return EXIT_FAILURE;
	}

	fprintf(stdout,"Output SampleRate:%d, Channels:%d, Format0x%X\n",
	sf_info->samplerate, sf_info->channels, sf_info->format);
	if (NULL == (glob_snd_file = sf_open(file_name, SFM_WRITE, sf_info)))
	{
		fprintf(stderr, "Could not create file.");
		return EXIT_FAILURE;
	}

	fprintf(stdout,"Created file called %s\n", file_name);
#endif /* LIBSND */

#if PCAP
	/** session, get session handler */
	/* take promiscuous vs. non-promiscuous sniffing? (0 or 1) */
	glob_pcap_handle = pcap_open_live(dev, BUFSIZ, 1, -1, errbuf);
	if (NULL == glob_pcap_handle)
	{
		fprintf(stderr, "Could not open device %s: %s\n", dev, errbuf);
		return EXIT_FAILURE;
	}

#if DEBUG
	fprintf(stdout,"Got session pcap handler.\n");
#endif /* DEBUG */
	
	/* compile and apply filter */
	fprintf(stdout,"Create packet filter ether dst %s\n", dest_mac);
	sprintf(filter_exp,"ether dst %s", dest_mac);
	if (-1 == pcap_compile(glob_pcap_handle, &comp_filter_exp, filter_exp, 0, PCAP_NETMASK_UNKNOWN))
	{
		fprintf(stderr, "Could not parse filter %s: %s\n", filter_exp, pcap_geterr(glob_pcap_handle));
		return EXIT_FAILURE;
	}

	if (-1 == pcap_setfilter(glob_pcap_handle, &comp_filter_exp))
	{
		fprintf(stderr, "Could not install filter %s: %s\n", filter_exp, pcap_geterr(glob_pcap_handle));
		return EXIT_FAILURE;
	}

#if DEBUG
	fprintf(stdout,"Compiled and applied filter.\n");
#endif /* DEBUG */
	printf("Target Stream ID: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	glob_target_stream_id[0], glob_target_stream_id[1], glob_target_stream_id[2], glob_target_stream_id[3],
	glob_target_stream_id[4], glob_target_stream_id[5], glob_target_stream_id[6], glob_target_stream_id[7]);
	/** loop forever and call callback-function for every received packet */
	pcap_loop(glob_pcap_handle, -1, callback, (u_char*)ctx);
#endif /* PCAP */
	free(ctx);
	free(class_a);
	free(class_b);

	return EXIT_SUCCESS;
}
