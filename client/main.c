#include <sys/socket.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>

#define ERR_UNKNOWN_REQUEST 0xFFFF
#define ERR_UPDATING        0xFF00
#define ERR_INV_ID          0xFF01
#define ERR_OUT_RANGE       0xFF03
#define ERR_TOO_LONG        0xFF04

#define GET_ID          0x01
#define GET_ID_RSP      0x02

#define GET_INFO        0x03
#define GET_INFO_RSP    0x04

#define READ_PART       0x07
#define READ_PART_RSP   0x08

#define GET_DEP         0x09
#define GET_DEP_RSP     0x0A

#define STP_PKG_SIZE    1300

#define DEFAULT_IP "asqel.ddns.net:42024"

int G_FD;

#define R64_TO_XID(r) ((r) & 0xFFFFFFFFFF)
#define R64_TO_TYPE(r) (((r) >> 48) & 0xFFFF)
#define TYPE_IS_ERR(t) (((t) & 0xFF00) == 0xFF00)

/*******************************************
 *                                        *
 *   STP CLIENT SIDE PROTOCOL FUNCTIONS   *
 *                                        *
********************************************/

#define RETERR(...) return (fprintf(stderr, __VA_ARGS__), -1)

long get_pkg_id(uint64_t xid, const char *name) {
    if (strlen(name) > STP_PKG_SIZE - 8)
        RETERR("[get_pkg_id] name too long\n");

    uint8_t buf[STP_PKG_SIZE];
    uint64_t r = GET_ID;

    int s = strlen(name);

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);
    memcpy(buf + 8, name, s);

    if (send(G_FD, buf, 8 + s, 0) == -1)
        RETERR("[get_pkg_id] send error %d\n", errno);

    int rlen = recv(G_FD, buf, 1300, 0);

    if (rlen < 0)
        RETERR("[get_pkg_id] recv error %d\n", errno);

    if (rlen < 8)
        RETERR("[get_pkg_id] recv too short\n");

    memcpy(&r, buf, 8);

    if (R64_TO_XID(r) != xid)
        RETERR("[get_pkg_id] wrong xid, expected %"PRId64" got %"PRId64"\n", xid, R64_TO_XID(r));

    uint16_t resp_type = R64_TO_TYPE(r);

    if (TYPE_IS_ERR(resp_type))
        RETERR("[get_pkg_id] error response %x\n", resp_type);

    if (resp_type != GET_ID_RSP)
        RETERR("[get_pkg_id] unexpected response type %x\n", resp_type);

    memcpy(&r, buf + 8, 8);
    return r;
}

/*******************************************
 *                                        *
 *           COMMAND LINE STUFF           *
 *                                        *
********************************************/

int parse_ipandport(char *exe, const char *str, struct sockaddr_in *addr) {
    // truc.ddns.net:1234 or 127.0.0.1:1234

    char ip[256];
    int port;

    const char *p = strchr(str, ':');
    if (!p) {
        fprintf(stderr, "%s: expected ip:port format\n", exe);
        return -1;
    }

    size_t ip_len = p - str;
    if (ip_len >= sizeof(ip)) {
        fprintf(stderr, "%s: ip too long\n", exe);
        return -1;
    }

    memcpy(ip, str, ip_len);
    ip[ip_len] = '\0';

    port = atoi(p + 1);
    if (port == 0 || port > 65535) {
        fprintf(stderr, "%s: '%s' invalid port\n", exe, p + 1);
        return -1;
    }

    struct hostent *info = gethostbyname(ip);
    if (!info) {
        fprintf(stderr, "%s: %s: host non trouve\n", exe, str);
        return -1;
    }

    addr->sin_family = AF_INET;
    memcpy(&addr->sin_addr, info->h_addr_list[0], 4);
    addr->sin_port = htons(port);

    printf("// ip: %d.%d.%d.%d:%d\n",
        (uint8_t)info->h_addr_list[0][0],
        (uint8_t)info->h_addr_list[0][1],
        (uint8_t)info->h_addr_list[0][2],
        (uint8_t)info->h_addr_list[0][3],
        port
    );

    return 0;
}

int main(int argc, char **argv) {
    struct sockaddr_in addr;
    char *ip_str;

    if (argc > 2) {
        fprintf(stderr, "usage: %s ip:port\n", argv[0]);
        return 1;
    }

    if (argc == 2)
        ip_str = argv[1];
    else
        ip_str = DEFAULT_IP;

    if (parse_ipandport(argv[0], ip_str, &addr))
        return 1;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    G_FD = fd;

    if (fd < 0) {
        return 1;
    }

    if (connect(fd, (void *)&addr, sizeof(addr))) {
        fprintf(stderr, "erreur de connection ta mere\n");
        close(fd);
        return 1;
    }

    printf("aaaa %ld\n", get_pkg_id(99, "tcc"));

    close(fd);
    return 0;
}
