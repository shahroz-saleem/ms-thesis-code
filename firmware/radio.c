#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "net/gnrc/netif.h"
#include "net/ipv6/addr.h"
#include "net/netif.h"
#include "net/sock/async/event.h"
#include "net/sock/udp.h"
#include "ztimer.h"

#include "radio.h"

#define PORT            4242
#define MAX_PAYLOAD     16
#define MAX_HEARD       128
#define PROBE_FRAMES    5
#define PROBE_GAP_MS    20

enum { KIND_PROBE = 1, KIND_DATA = 2 };

typedef struct {
    uint16_t id;
    int32_t rssi_sum;
    uint16_t count;
} heard_t;

static netif_t *netif;
static sock_udp_t sock;
static uint16_t my_id;
static radio_receive_t on_receive;

static heard_t heard[MAX_HEARD];
static unsigned heard_count;

/* Node ids are FIT IoT-LAB board numbers and every node's address is
 * fe80::<id>, so a neighbour is reachable from its id alone. */
static void address_of(uint16_t id, ipv6_addr_t *addr)
{
    ipv6_addr_set_link_local_prefix(addr);
    memset(&addr->u8[8], 0, 8);
    addr->u8[14] = id >> 8;
    addr->u8[15] = id & 0xff;
}

static uint16_t id_of(const sock_udp_ep_t *remote)
{
    return (remote->addr.ipv6[14] << 8) | remote->addr.ipv6[15];
}

static void record_rssi(uint16_t sender, int16_t rssi)
{
    for (unsigned i = 0; i < heard_count; i++) {
        if (heard[i].id == sender) {
            heard[i].rssi_sum += rssi;
            heard[i].count++;
            return;
        }
    }
    if (heard_count < MAX_HEARD) {
        heard[heard_count++] = (heard_t){ .id = sender, .rssi_sum = rssi, .count = 1 };
    }
}

static void on_udp(sock_udp_t *s, sock_async_flags_t flags, void *arg)
{
    (void)arg;

    if (!(flags & SOCK_ASYNC_MSG_RECV)) {
        return;
    }

    uint8_t buf[MAX_PAYLOAD];
    sock_udp_ep_t remote;
    sock_udp_aux_rx_t aux = { .flags = SOCK_AUX_GET_RSSI };
    ssize_t len;

    /* One event can stand for several queued datagrams, so drain them all. */
    while ((len = sock_udp_recv_aux(s, buf, sizeof(buf), 0, &remote, &aux)) >= 0) {
        uint16_t sender = id_of(&remote);

        /* GNRC clears the flag when it did fill in the RSSI. */
        if (len >= 1 && buf[0] == KIND_PROBE && !(aux.flags & SOCK_AUX_GET_RSSI)) {
            record_rssi(sender, aux.rssi);
        }
        else if (len >= 1 && buf[0] == KIND_DATA && on_receive) {
            on_receive(sender, buf + 1, len - 1);
        }
        aux.flags = SOCK_AUX_GET_RSSI;
    }
}

int radio_init(event_queue_t *queue, radio_receive_t receive)
{
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;

    netif = netif_iter(NULL);
    if (netif == NULL) {
        return -ENODEV;
    }

    local.port = PORT;
    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        return -EIO;
    }
    on_receive = receive;
    sock_udp_event_init(&sock, queue, on_udp, NULL);
    return 0;
}

int radio_set_id(uint16_t id)
{
    uint8_t short_addr[2] = { id >> 8, id & 0xff };
    uint8_t long_addr[8] = { 0x02, 0, 0, 0, 0, 0, id >> 8, id & 0xff };
    ipv6_addr_t addrs[CONFIG_GNRC_NETIF_IPV6_ADDRS_NUMOF];
    ipv6_addr_t link_local;

    if (netif_set_opt(netif, NETOPT_ADDRESS, 0, short_addr, sizeof(short_addr)) < 0 ||
        netif_set_opt(netif, NETOPT_ADDRESS_LONG, 0, long_addr, sizeof(long_addr)) < 0) {
        return -EIO;
    }

    int count = netif_get_opt(netif, NETOPT_IPV6_ADDR, 0, addrs, sizeof(addrs))
                / (int)sizeof(ipv6_addr_t);
    for (int i = 0; i < count; i++) {
        if (ipv6_addr_is_link_local(&addrs[i])) {
            netif_set_opt(netif, NETOPT_IPV6_ADDR_REMOVE, 0, &addrs[i], sizeof(addrs[i]));
        }
    }
    address_of(id, &link_local);
    if (netif_set_opt(netif, NETOPT_IPV6_ADDR, 64 << 8, &link_local, sizeof(link_local)) < 0) {
        return -EIO;
    }

    my_id = id;
    return 0;
}

uint16_t radio_id(void)
{
    return my_id;
}

static int send_packet(const ipv6_addr_t *to, uint8_t kind, const void *data, size_t len)
{
    uint8_t buf[MAX_PAYLOAD];
    sock_udp_ep_t remote = { .family = AF_INET6, .port = PORT };

    if (len + 1 > sizeof(buf)) {
        return -EMSGSIZE;
    }
    buf[0] = kind;
    if (len > 0) {
        memcpy(buf + 1, data, len);
    }
    memcpy(remote.addr.ipv6, to, sizeof(*to));
    remote.netif = netif_get_id(netif);
    return sock_udp_send(&sock, buf, len + 1, &remote);
}

int radio_send(uint16_t to, const void *data, size_t len)
{
    ipv6_addr_t addr;

    address_of(to, &addr);
    return send_packet(&addr, KIND_DATA, data, len);
}

void radio_probe(void)
{
    ipv6_addr_t all_nodes;

    ipv6_addr_set_all_nodes_multicast(&all_nodes, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);
    for (unsigned i = 0; i < PROBE_FRAMES; i++) {
        send_packet(&all_nodes, KIND_PROBE, NULL, 0);
        ztimer_sleep(ZTIMER_MSEC, PROBE_GAP_MS);
    }
}

void radio_print_heard(void)
{
    printf("heard");
    for (unsigned i = 0; i < heard_count; i++) {
        printf(" %u:%ld", heard[i].id, (long)(heard[i].rssi_sum / heard[i].count));
    }
    printf("\n");
}
