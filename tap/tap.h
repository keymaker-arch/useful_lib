#ifndef __LIB_TUN
#define __LIB_TUN
#include <stdbool.h>

/**
 * initialize a TAP device
 * @tap_dev_name: name of the TAP device
 * @local_mac: MAC address of the TAP device
 * @return: file descriptor of the TAP device
 * 
 * create a TAP deivce and assign IP address, add neighbor info and bring it online.
 * the created TAP device will have an IPv4 address, an IPv6 address, and a MAC address, with a neighbor info.
*/
int tap_initialize(const char* tap_dev_name, const char* local_mac);

/**
 * flush the TAP device
 * @tapfd: fd of the TAP device
*/
void tap_flush(int tapfd);

/**
 * add a neighbor info to TAP device
 * @name: name of the TAP device
 * @addr: IP address of the neighbor
 * @mac: MAC address of the neighbor
 * @return: 0 on success, -1 on error
 * 
 * mac address and ip address must be in string format
*/
int tap_add_neigh(const char* name, const char* addr, const char* mac);

/**
 * add IPv6 addr to TAP device
 * @name: name of the TAP device
 * @addr: IPv6 addr to be added
 * @return: 0 on success, -1 on error
*/
int tap_add_ipv6_addr(const char* name, const char* addr);

/**
 * add IPv4 addr to TAP device
 * @name: name of the TAP device
 * @addr: IPv4 addr to be added
 * @return: 0 on success, -1 on error
*/
int tap_add_ipv4_addr(const char* name, const char* addr);

/**
 * change state of the TAP device
 * @name: name of the TAP device
 * @up: true for up, false for down
 * @mac: MAC address to be assigned to the TAP device, given in string format
 * @new_name: new name of the TAP device
 * @return: 0 on success, -1 on error
*/
int tap_state_change(const char* name, bool up, const char* mac, const char* new_name);

#endif