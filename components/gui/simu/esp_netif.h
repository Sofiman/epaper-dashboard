#pragma once

typedef struct {
} esp_netif_t;

struct esp_ip4_addr {
    uint32_t addr;  /*!< IPv4 address */
};

typedef struct esp_ip4_addr esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;      /**< Interface IPV4 address */
} esp_netif_ip_info_t;

static void esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info)
{
    ip_info->ip.addr = 0xc0a80069;
}

#define IPSTR "%d.%d.%d.%d"
#define esp_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t*)(&(ipaddr)->addr))[idx])
#define esp_ip4_addr1(ipaddr) esp_ip4_addr_get_byte(ipaddr, 0)
#define esp_ip4_addr2(ipaddr) esp_ip4_addr_get_byte(ipaddr, 1)
#define esp_ip4_addr3(ipaddr) esp_ip4_addr_get_byte(ipaddr, 2)
#define esp_ip4_addr4(ipaddr) esp_ip4_addr_get_byte(ipaddr, 3)


#define esp_ip4_addr1_16(ipaddr) ((uint16_t)esp_ip4_addr1(ipaddr))
#define esp_ip4_addr2_16(ipaddr) ((uint16_t)esp_ip4_addr2(ipaddr))
#define esp_ip4_addr3_16(ipaddr) ((uint16_t)esp_ip4_addr3(ipaddr))
#define esp_ip4_addr4_16(ipaddr) ((uint16_t)esp_ip4_addr4(ipaddr))

#define IP2STR(ipaddr) esp_ip4_addr1_16(ipaddr), \
    esp_ip4_addr2_16(ipaddr), \
    esp_ip4_addr3_16(ipaddr), \
    esp_ip4_addr4_16(ipaddr)
