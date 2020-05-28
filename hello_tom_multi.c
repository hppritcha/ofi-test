#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_errno.h>

#define MSG_TAG		(0xFFFFFFFFFFFFFFFFULL)
#define MSG_TAG1	(0xFFFF0000FFFF0000ULL)
#define MSG_TAG2	(0x0000FFFF0000FFFFULL)
#define MSG_TAG3	(0x00FF00FF00FF00FFULL)
#define MSG_TAG4	(0xFF00FF00FF00FF00ULL)
#define SV_MSG_TAG	(0x0F0F0F0F0F0F0F0FULL)
#define IGNORE		(0x0ULL)

#define CHK_ERR(name, cond, err)							\
	do {										\
		if (cond) {								\
			fprintf(stderr,"%s: %s\n", name, strerror(-(err)));		\
			exit(1);							\
		}									\
	} while (0)

static struct fi_info		*fi;
static struct fid_fabric	*fabric;
static struct fid_domain	*domain;
static struct fid_av		*av;
static struct fid_ep		*ep;
static struct fid_cq		*cq;
static fi_addr_t		    *peer_addr;
static struct fi_context	sctxt;
static struct fi_context	rctxt;
static char			sbuf[64];
static char			rbuf[64];
static char			tempbuf[64];
static int          count=0;

static void get_peer_addr(void *peer_name)
{
	int err;
	char buf[64];
	size_t len = 64;

	buf[0] = '\0';
	fi_av_straddr(av, peer_name, buf, &len);
	printf("Translating peer address: %s\n", buf);

	err = fi_av_insert(av, peer_name, 1, &peer_addr[0], 0, NULL);
	printf("fi_av_insert returns: %d\n", err);
	/*CHK_ERR("fi_av_insert", (err!=1), err);*/
}

static void init_fabric(char *server)
{
	struct fi_info		*hints;
	struct fi_cq_attr	cq_attr;
	struct fi_av_attr	av_attr;
	int 				err;
	int					version;
	char				name[64], buf[64];
	size_t				len = 64;

	hints = fi_allocinfo();
	CHK_ERR("fi_allocinfo", (!hints), -ENOMEM);

	memset(&cq_attr, 0, sizeof(cq_attr));
	memset(&av_attr, 0, sizeof(av_attr));

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG | FI_TAGGED;
	hints->mode = FI_CONTEXT;
#if 0
        hints->fabric_attr->prov_name = strdup("UDP-IP");
#endif
#if 1
        /*hints->fabric_attr->prov_name = strdup("UDP"); TODO: Test with other providers*/
#endif

	version = FI_VERSION(1, 5);
	err = fi_getinfo(version, server, "12345", server ? 0 : FI_SOURCE, hints, &fi);
	CHK_ERR("fi_getinfo", (err<0), err);

	fi_freeinfo(hints);

	printf("Using OFI device: %s\n", fi->fabric_attr->name);

	err = fi_fabric(fi->fabric_attr, &fabric, NULL);
	CHK_ERR("fi_fabric", (err<0), err);

	err = fi_domain(fabric, fi, &domain, NULL);
	CHK_ERR("fi_domain", (err<0), err);

	av_attr.type = FI_AV_UNSPEC;

	err = fi_av_open(domain, &av_attr, &av, NULL);
	CHK_ERR("fi_av_open", (err<0), err);

	cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cq_attr.size = 100;

	err = fi_cq_open(domain, &cq_attr, &cq, NULL);
	CHK_ERR("fi_cq_open", (err<0), err);

	err = fi_endpoint(domain, fi, &ep, NULL);
	CHK_ERR("fi_endpoint", (err<0), err);

	err = fi_ep_bind(ep, (fid_t)cq, FI_SEND|FI_RECV);
	CHK_ERR("fi_ep_bind cq", (err<0), err);

	err = fi_ep_bind(ep, (fid_t)av, 0);
	CHK_ERR("fi_ep_bind av", (err<0), err);

	err = fi_enable(ep);
	CHK_ERR("fi_enable", (err<0), err);

	err = fi_getname((fid_t)ep, name, &len);
	CHK_ERR("fi_getname", (err<0), err);

	buf[0] = '\0';
	len = 64;
	fi_av_straddr(av, name, buf, &len);
	printf("My address is %s\n", buf);

	if (server)
		get_peer_addr(fi->dest_addr);
}

static void finalize_fabric(void)
{
	fi_close((fid_t)ep);
	fi_close((fid_t)cq);
	fi_close((fid_t)av);
	fi_close((fid_t)domain);
	fi_close((fid_t)fabric);
	fi_freeinfo(fi);
	free(peer_addr);
}

static void wait_cq(void)	/*TODO: Make a wait method that waits for all receives to finish instead of just one */
{
	struct fi_cq_err_entry entry;
	int ret, completed = 0;
	fi_addr_t from;

	while (!completed) {
		ret = fi_cq_readfrom(cq, &entry, 1, &from);

        if (ret == -FI_EAGAIN)
            continue;

		if (ret == -FI_EAVAIL) {
			ret = fi_cq_readerr(cq, &entry, 1);
			CHK_ERR("fi_cq_readerr", (ret!=1), ret);

			printf("Completion with error: %d\n", entry.err);
			if (entry.err == FI_EADDRNOTAVAIL)
				get_peer_addr(entry.err_data);
		}

		CHK_ERR("fi_cq_read", (ret<0), ret);
		completed += ret;
	}
}

static void twait_cq(void)
{
	struct fi_cq_tagged_entry entry;
	int ret, completed = 0;

	while (!completed) {
		ret = fi_cq_read(cq, &entry, 1);

        if (ret == -FI_EAGAIN)
            continue;

		CHK_ERR("fi_cq_read", (ret<0), ret);
		completed += ret;
	}
}

static void wait(int n)
{
    struct fi_cq_tagged_entry entry;
    int ret, completed = 0;

	while (!completed) {
		ret = fi_cq_read(cq, &entry, n);

        if (ret == -FI_EAGAIN)
            continue;

		CHK_ERR("fi_cq_read", (ret<0), ret);
		completed += ret;
	}
    
}

static void send_one(int size, int dest)
{
	int err;

	err = fi_send(ep, sbuf, size, NULL, peer_addr[dest], &sctxt);

	wait_cq();
}

static void recv_one(int size)
{
	int err;

	err = fi_recv(ep, rbuf, size, NULL, FI_ADDR_UNSPEC, &rctxt);

	wait_cq();
	printf("Received '%s'\n", rbuf);
}

static void recv_multi(int size, int n)
{
	int err;

	for (int i = 0; i < n; i++)
	{
		err = fi_recv(ep, rbuf, size, NULL, FI_ADDR_UNSPEC, &rctxt);
		wait(n);

		printf("Received '%s'\n", rbuf);
	}
}

static void trecv_multi(int size, uint64_t tag, int n)
{
	int err;

	for (int i = 0; i < n; i++)
	{
		err = fi_trecv(ep, rbuf, size, NULL, FI_ADDR_UNSPEC, tag, IGNORE, &rctxt);
		twait_cq();

		printf("Received '%s' (tagged)\n", rbuf);
	}

}

static void tsend_one(int size, uint64_t tag, int dest)
{
    printf("Sending %s to tag %lu\n", sbuf, tag);
	int err;

	err = fi_tsend(ep, sbuf, size, NULL, peer_addr[dest], tag, &sctxt);

	twait_cq();
}

static void trecv_one(int size, uint64_t tag)
{
	int err;

	err = fi_trecv(ep, rbuf, size, NULL, FI_ADDR_UNSPEC, tag, IGNORE, &rctxt);

	twait_cq();
	printf("Received '%s' (tagged)\n", rbuf);
}

int main(int argc, char *argv[])	/*TODO: use tagged and untagged messages and see if the messages get to the right buffers, make sure data goes to right buffers*/
{
	int is_client = 0;
	char *server = NULL;
	int size = 64;
	int terms = 0;
	int client_num, num_clients;

	if (argc > 2) {
		is_client = 1;
		server = argv[1];
		client_num = (int) strtol(argv[2], (char **)NULL, 10);
	} else {
		num_clients = (int) strtol(argv[1], (char **)NULL, 10);
	}

	peer_addr = malloc((num_clients+1)*sizeof(fi_addr_t));
	init_fabric(server);
	size_t addrlen = 64;
	size_t len = 64;
	int err;
	uint64_t tags[] = {MSG_TAG1, MSG_TAG2, MSG_TAG3, MSG_TAG4};
	char *ptr;
	long n;

	if (is_client) {
		fi_getname(&ep->fid, sbuf, &addrlen);
		tsend_one(size, tags[client_num-1], 0);
	} else {
		for (int i = 1; i <= num_clients; i++)
		{
			char buf[64];
			printf("Waiting for client %d to connect\n", i);
			trecv_one(size, tags[i-1]);
			buf[0] = '\0';
			fi_av_straddr(av, rbuf, buf, &len);
			printf("Translating peer address: %s\n", buf);
			err = fi_av_insert(av, rbuf, 1, &peer_addr[i], 0, NULL);
			printf("fi_av_insert returns: %d\n", err);

		}
	}

	while (1) {
		if (is_client) {
			printf("Sending client number to server\n");
            sprintf(sbuf, "%d", client_num);
            tsend_one(size, SV_MSG_TAG, 0);
            printf("Enter message to server: ");
            scanf("%s", sbuf);
            tsend_one(size, MSG_TAG, 0);
			printf("Waiting for server, tag %lu\n", tags[client_num-1]);
            trecv_one(size, tags[client_num-1]);
			printf("Received '%s' from server\n", rbuf);
		} else {
            trecv_one(size, SV_MSG_TAG);
            client_num = strtol(rbuf, &ptr, 10);
            printf("Received client number %d\n", client_num);
            trecv_one(size, MSG_TAG);
            printf("Received message %s from client %d\n", rbuf, client_num);
            printf("Enter reply to client %d, tag %lu: ", client_num, tags[client_num-1]);
            scanf("%s", sbuf);
            tsend_one(size, tags[client_num-1], client_num);
		}
	}
	

	finalize_fabric();

	return 0;
}
