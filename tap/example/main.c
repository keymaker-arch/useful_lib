#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/if_ether.h>

#include "../tap.h"

#define TUNNAME "TestTun"
#define TUN_MAC "aa:aa:aa:bb:bb:bb"
#define TUN_IP4 "192.168.233.1"
#define TUN_NEIGH_IP4 "192.168.233.2"
#define TUN_NEIGH_MAC "aa:aa:aa:bb:bb:cc"

void print_hex_buffer_fd(int fd, const char *buffer, unsigned long length)
{
	unsigned long i;

	for (i = 0; i < length; i++) {
		dprintf(fd, "%02x ", (unsigned char)buffer[i]);
	}
	dprintf(fd, "\n");
}

int filter_mac_multi_broad_cast(struct ethhdr* eth)
{
    // filter IPv4 milticast by dest begin with 01-00-5E
    if (eth->h_dest[0] == 1 && eth->h_dest[1] == 0 && eth->h_dest[2] == 0x5E) {
        return 1;
    }

    // filter IPv6 milticast by dest begin with 33-33
    if (eth->h_dest[0] == 0x33 && eth->h_dest[1] == 0x33) {
        return 1;
    }

    // filter broadcast
    if (eth->h_dest[0] == 0xff && eth->h_dest[1] == 0xff && eth->h_dest[2] == 0xff && eth->h_dest[3] == 0xff && eth->h_dest[4] == 0xff && eth->h_dest[5] == 0xff) {
        return 1;
    }

    return 0;
}


int main(int argc, char** argv)
{
    int fd, n, fd_agent;
    char buf[0x100];
    struct ethhdr *eth;

    fd = tap_initialize(TUNNAME, TUN_MAC);
    if (fd == -1) {
        return 0;
    }
    tap_flush(fd);
    puts("[+] TUN device initialize done");

    if (tap_add_ipv4_addr(TUNNAME, TUN_IP4) != 0) {
        return 0;
    }
    puts("[+] TUN device address adding done");

    if (tap_add_neigh(TUNNAME, TUN_NEIGH_IP4, TUN_NEIGH_MAC) != 0) {
        return 0;
    }

    while (1) {
        n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            eth = (struct ethhdr*)buf;

            if (filter_mac_multi_broad_cast(eth)) {
                continue;
            }

            print_hex_buffer_fd(1, buf, n);
        } else {
            sleep(1);
        }
    }

    return 0;
}