/*
	set up a TUN NIC
	also provided some routines to send packet to the NIC
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <err.h>
#include <linux/netlink.h>
#include <arpa/inet.h>
#include <linux/if_addr.h>
#include <linux/rtnetlink.h>
#ifndef __USE_MISC
#define __USE_MISC   // this is need for some struc definition, or VSCode will report error
#endif
#include <net/if.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#include "tap.h"
// #include "util.h"

/***************************************************************
 * low level netlink routines, copied from Syzkaller
*/
struct nlmsg {
	char* pos;
	int nesting;
	struct nlattr* nested[8];
	char buf[4096];
};

typedef struct tun_dev {
	int fd;
	char name[100];
	char local_mac_s[18];

} tun_dev_t;

static int netlink_sock_fd;
static struct nlmsg nlmsg;

static int netlink_send_ext(struct nlmsg* nlmsg, int sock, unsigned short reply_type, int* reply_len, bool dofail)
{
	if (nlmsg->pos > nlmsg->buf + sizeof(nlmsg->buf) || nlmsg->nesting) {
		WARNFD_ERR("nlmsg overflow/bad nesting");
		return -1;
	}
	struct nlmsghdr* hdr = (struct nlmsghdr*)nlmsg->buf;
	hdr->nlmsg_len = nlmsg->pos - nlmsg->buf;
	struct sockaddr_nl addr;
	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	ssize_t n = sendto(sock, nlmsg->buf, hdr->nlmsg_len, 0, (struct sockaddr*)&addr, sizeof(addr));
	if (n != (ssize_t)hdr->nlmsg_len) {
		WARNFD_ERR("netlink_send_ext: short netlink write: %zd/%d errno=%d\n", n, hdr->nlmsg_len, errno);
		return -1;
	}
	n = recv(sock, nlmsg->buf, sizeof(nlmsg->buf), 0);
	if (reply_len)
		*reply_len = 0;
	if (n < 0) {
		WARNFD_ERR("netlink_send_ext: netlink read failed: errno=%d\n", errno);
		return -1;
	}
	if (n < (ssize_t)sizeof(struct nlmsghdr)) {
		errno = EINVAL;
		WARNFD_ERR("netlink_send_ext: short netlink read: %zd\n", n);
		return -1;
	}
	if (hdr->nlmsg_type == NLMSG_DONE)
		return 0;
	if (reply_len && hdr->nlmsg_type == reply_type) {
		*reply_len = n;
		return 0;
	}
	if (n < (ssize_t)(sizeof(struct nlmsghdr) + sizeof(struct nlmsgerr))) {
		errno = EINVAL;
		WARNFD_ERR("netlink_send_ext: short netlink read: %zd\n", n);
		return -1;
	}
	if (hdr->nlmsg_type != NLMSG_ERROR) {
		errno = EINVAL;
		WARNFD_ERR("netlink_send_ext: short netlink ack: %d\n", hdr->nlmsg_type);
		return -1;
	}
	errno = -((struct nlmsgerr*)(hdr + 1))->error;
  if (errno != 0 && dofail)
    WARNFD_ERR("netlink_send_ext: netlink error: %d\n", errno);
	return -errno;
}

static int netlink_send(struct nlmsg* nlmsg, int sock)
{
	return netlink_send_ext(nlmsg, sock, 0, NULL, true);
}

static void netlink_init(struct nlmsg* nlmsg, int typ, int flags, const void* data, int size)
{
	struct nlmsghdr* hdr;

	memset(nlmsg, 0, sizeof(*nlmsg));
	hdr = (struct nlmsghdr*)nlmsg->buf;
	hdr->nlmsg_type = typ;
	hdr->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	memcpy(hdr + 1, data, size);
	nlmsg->pos = (char*)(hdr + 1) + NLMSG_ALIGN(size);
}

static void netlink_attr(struct nlmsg* nlmsg, int typ, const void* data, int size)
{
	struct nlattr* attr = (struct nlattr*)nlmsg->pos;
	attr->nla_len = sizeof(*attr) + size;
	attr->nla_type = typ;
	if (size > 0)
		memcpy(attr + 1, data, size);
	nlmsg->pos += NLMSG_ALIGN(attr->nla_len);
}

/*
	IP4/6 addr assigning function
*/
static int netlink_add_addr(struct nlmsg* nlmsg, int sock, const char* dev, const void* addr, int addrsize)
{
	struct ifaddrmsg hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.ifa_family = addrsize == 4 ? AF_INET : AF_INET6;
	hdr.ifa_prefixlen = addrsize == 4 ? 24 : 120;
	hdr.ifa_scope = RT_SCOPE_UNIVERSE;
	hdr.ifa_index = if_nametoindex(dev);
	netlink_init(nlmsg, RTM_NEWADDR, NLM_F_CREATE | NLM_F_REPLACE, &hdr, sizeof(hdr));
	netlink_attr(nlmsg, IFA_LOCAL, addr, addrsize);
	netlink_attr(nlmsg, IFA_ADDRESS, addr, addrsize);
	return netlink_send(nlmsg, sock);
}

/*
	add IPv4 addr to an interface
*/
int netlink_add_addr4(struct nlmsg* nlmsg, int sock, const char* dev, const char* addr)
{
	struct in_addr in_addr;

	inet_pton(AF_INET, addr, &in_addr);

	return netlink_add_addr(nlmsg, sock, dev, &in_addr, sizeof(in_addr));
}

/*
	add IPv6 addr to an interface by netlink
*/
int netlink_add_addr6(struct nlmsg* nlmsg, int sock, const char* dev, const char* addr)
{
	struct in6_addr in6_addr;

	inet_pton(AF_INET6, addr, &in6_addr);

	return netlink_add_addr(nlmsg, sock, dev, &in6_addr, sizeof(in6_addr));
}

/*
	add neighbor info by netlink
*/
int netlink_add_neigh(struct nlmsg* nlmsg, int sock, const char* name, const void* addr, int addrsize, const void* mac, int macsize)
{
	struct ndmsg hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.ndm_family = addrsize == 4 ? AF_INET : AF_INET6;
	hdr.ndm_ifindex = if_nametoindex(name);
	hdr.ndm_state = NUD_PERMANENT;
	netlink_init(nlmsg, RTM_NEWNEIGH, NLM_F_EXCL | NLM_F_CREATE, &hdr, sizeof(hdr));
	netlink_attr(nlmsg, NDA_DST, addr, addrsize);
	netlink_attr(nlmsg, NDA_LLADDR, mac, macsize);

	return netlink_send(nlmsg, sock);
}

/*
	change interface name, state, master, mac, by netlink
*/
int netlink_device_change(struct nlmsg* nlmsg, int sock, const char* name, bool up, const char* master, const void* mac, int macsize, const char* new_name)
{
	struct ifinfomsg hdr;

	memset(&hdr, 0, sizeof(hdr));
	if (up)
		hdr.ifi_flags = hdr.ifi_change = IFF_UP;
	hdr.ifi_index = if_nametoindex(name);
	netlink_init(nlmsg, RTM_NEWLINK, 0, &hdr, sizeof(hdr));
	if (new_name)
		netlink_attr(nlmsg, IFLA_IFNAME, new_name, strlen(new_name));
	if (master) {
		int ifindex = if_nametoindex(master);
		netlink_attr(nlmsg, IFLA_MASTER, &ifindex, sizeof(ifindex));
	}
	if (macsize)
		netlink_attr(nlmsg, IFLA_ADDRESS, mac, macsize);

	return netlink_send(nlmsg, sock);
}

static void mac2bytes(const char* mac_str, char* dst)
{
  sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &dst[0], &dst[1], &dst[2], &dst[3], &dst[4], &dst[5]);
}


/**
 * creating TUN device
 * @return: file descriptor of the TUN device
*/
static int tap_create(const char* tun_dev_name)
{
	int tapfd;
	struct ifreq ifr;

	tapfd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
	if (tapfd == -1) {
		WARNFD_ERR("open /dev/net/tun failed");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, tun_dev_name, strlen(tun_dev_name));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	if (ioctl(tapfd, TUNSETIFF, (void*)&ifr) < 0) {
		WARNFD_ERR("ioctl(TUNSETIFF) failed");
		return -1;
	}

	return tapfd;
}


/**************************************************************
 * Exported Functions: TAP device management routines
*/

/**
 * change state of the TAP device
 * @name: name of the TAP device
 * @up: true for up, false for down
 * @mac: MAC address to be assigned to the TAP device, given in string format
 * @new_name: new name of the TAP device
 * @return: 0 on success, -1 on error
*/
int tap_state_change(const char* name, bool up, const char* mac, const char* new_name)
{
    char macbytes[ETH_ALEN];

	/* now we only support ether, the mac address must be in string format*/
	if (strlen(mac) != 17) {
		ERRORFD("invalid MAC address: %s", mac);
		return -1;
	}

    mac2bytes(mac, macbytes);

	if (netlink_device_change(&nlmsg, netlink_sock_fd, name, up, NULL, macbytes, ETH_ALEN, new_name) != 0) {
		return -1;
	}

	return 0;
}

/**
 * initialize a TAP device
 * @tap_dev_name: name of the TAP device
 * @local_mac: MAC address of the TAP device
 * @return: file descriptor of the TAP device
 * 
 * create a TAP deivce and assign IP address, add neighbor info and bring it online.
 * the created TAP device will have an IPv4 address, an IPv6 address, and a MAC address, with a neighbor info.
*/
int tap_initialize(const char* tap_dev_name, const char* local_mac)
{
	int tapfd;
	
	tapfd = tap_create(tap_dev_name);
	if (tapfd == -1) {
		ERRORFD("tun_create failed");
		return -1;
	}

	netlink_sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (netlink_sock_fd == -1) {
		ERRORFD("socket(AF_NETLINK) failed");
		return -1;
	}

	if (tap_state_change(tap_dev_name, true, local_mac, NULL) != 0) {
		ERRORFD("tun_state_change failed");
		return -1;
	}

	tap_flush(tapfd);
	return tapfd;
}

/**
 * flush the TAP device
 * @tapfd: fd of the TAP device
*/
void tap_flush(int tapfd)
{
	char buf[0x100];
	while (read(tapfd, buf, sizeof(buf)) != -1);
}

/**
 * add IPv4 addr to TAP device
 * @name: name of the TAP device
 * @addr: IPv4 addr to be added
 * @return: 0 on success, -1 on error
*/
int tap_add_ipv4_addr(const char* name, const char* addr)
{
	/* check if ipv4 address legal */
	struct in_addr in_addr;
	if (inet_pton(AF_INET, addr, &in_addr) != 1) {
		WARNFD("invalid IPv4 address");
		return -1;
	}

	if (netlink_add_addr4(&nlmsg, netlink_sock_fd, name, addr) != 0) {
		return -1;
	}

	return 0;
}

/**
 * add IPv6 addr to TAP device
 * @name: name of the TAP device
 * @addr: IPv6 addr to be added
 * @return: 0 on success, -1 on error
*/
int tap_add_ipv6_addr(const char* name, const char* addr)
{
	/* check if ipv6 address legal */
	struct in6_addr in6_addr;
	if (inet_pton(AF_INET6, addr, &in6_addr) != 1) {
		ERRORFD("invalid IPv6 address");
		return -1;
	}

	if (netlink_add_addr6(&nlmsg, netlink_sock_fd, name, addr) != 0) {
		return -1;
	}

	return 0;
}

/**
 * add a neighbor info to TAP device
 * @name: name of the TAP device
 * @addr: IP address of the neighbor
 * @mac: MAC address of the neighbor
 * 
 * mac address and ip address must be in string format
*/
int tap_add_neigh(const char* name, const char* addr, const char* mac)
{
	struct in_addr in_addr;
	struct in6_addr in6_addr;

	char macbytes[ETH_ALEN];

	/* now we only support ether */
	if (strlen(mac) != 17) {
		WARNFD("invalid MAC address");
		return -1;
	}

	mac2bytes(mac, macbytes);

	/* now we only support IPv4 and IPv6 */
	if (inet_pton(AF_INET, addr, &in_addr) == 1) {
		if (netlink_add_neigh(&nlmsg, netlink_sock_fd, name, &in_addr, sizeof(in_addr), macbytes, ETH_ALEN) != 0) {
			return -1;
		}
	} else if (inet_pton(AF_INET6, addr, &in6_addr) == 1) {
		if (netlink_add_neigh(&nlmsg, netlink_sock_fd, name, &in6_addr, sizeof(in6_addr), macbytes, ETH_ALEN) != 0) {
			return -1;
		}
	} else {
		WARNFD("invalid IP address");
		return -1;
	}

	return 0;
}
