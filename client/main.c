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
#define ERR_FAIL            0xFF05

#define GET_ID          0x01
#define GET_ID_RSP      0x02

#define GET_INFO        0x03
#define GET_INFO_RSP    0x04

#define READ_PART       0x07
#define READ_PART_RSP   0x08

#define GET_DEP         0x09
#define GET_DEP_RSP     0x0A


#define STP_PKT_SIZE        1300
#define STP_MAX_DESC_SIZE   1000
#define STP_MAX_DEPS        (STP_PKT_SIZE - 16) / 8

#define DEFAULT_IP "asqel.ddns.net:42024"

int G_FD;

#define R64_TO_XID(r) ((r) & 0xFFFFFFFFFF)
#define R64_TO_TYPE(r) (((r) >> 48) & 0xFFFF)
#define TYPE_IS_ERR(t) (((t) & 0xFF00) == 0xFF00)

/*******************************************
 *                                        *
 *             ERROR MESSAGES             *
 *                                        *
********************************************/

char *error_to_str(uint16_t error) {
	if (error >> 8 != 0xFF)
		return "Success";
	switch (error) {
		case ERR_UNKNOWN_REQUEST:
			return "Invalid request";
		case ERR_UPDATING:
			return "Package is being updated";
		case ERR_INV_ID:
			return "Invalide package id";
		case ERR_OUT_RANGE:
			return "Offset out of range";
		case ERR_TOO_LONG:
			return "Package part too long";
		case ERR_FAIL:
			return "Internal server failure";
		default:
			return "Unknown error";
	}
}

/*******************************************
 *                                        *
 *   STP CLIENT SIDE PROTOCOL FUNCTIONS   *
 *                                        *
********************************************/

static uint64_t get_random_xid(void) {
    return (((uint64_t) rand() << 32) | rand()) & 0xFFFFFFFFFF;
}

static void purge_receive_buffer(void) {
    // [probably useless] purge the buffer to read only the new response
    uint8_t buf[STP_PKT_SIZE];
    while (recv(G_FD, buf, sizeof(buf), MSG_DONTWAIT) > 0);
}

#define RETERR(...) return (fprintf(stderr, __VA_ARGS__), -1)

static int check_response(uint8_t *buf, int buf_len, uint64_t expected_xid, uint16_t expected_type) {
    uint64_t r;

    if (buf_len < 0)
        RETERR("[protocol err] recv error %d\n", errno);

    if (buf_len < 8)
        RETERR("[protocol err] recv too short\n");

    memcpy(&r, buf, 8);

    if (R64_TO_XID(r) != expected_xid)
        RETERR("[protocol err] wrong xid\n");

    uint16_t resp_type = R64_TO_TYPE(r);

    if (TYPE_IS_ERR(resp_type))
        RETERR("[protocol err] error response received: %s (0x%04x)\n", error_to_str(resp_type), resp_type);

    if (resp_type != expected_type)
        RETERR("[protocol err] unexpected response type %x\n", resp_type);

    return 0;   
}

int64_t get_pkg_id(const char *name) {
    if (strlen(name) > STP_PKT_SIZE - 8)
        RETERR("[protocol err] name too long\n");

    uint8_t buf[STP_PKT_SIZE];
    uint64_t r = GET_ID;

    uint64_t xid = get_random_xid();

    int s = strlen(name);

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);
    memcpy(buf + 8, name, s);

    purge_receive_buffer();

    if (send(G_FD, buf, 8 + s, 0) == -1)
        RETERR("[protocol err] send error %d\n", errno);

    int rlen = recv(G_FD, buf, 1300, 0);

    if (check_response(buf, rlen, xid, GET_ID_RSP))
        return -1;

    if (rlen != 16)
        RETERR("[protocol err] recv wrong length\n");

    memcpy(&r, buf + 8, 8);
    return r;
}

int64_t get_pkg_info(int64_t id, char *info_buf, size_t buf_size) { // returns file size
    uint8_t buf[STP_PKT_SIZE];
    uint64_t r = GET_INFO;

    uint64_t xid = get_random_xid();

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);
    memcpy(buf + 8, &id, 8);

    purge_receive_buffer();

    if (send(G_FD, buf, 16, 0) == -1)
        RETERR("[protocol err] send error %d\n", errno);

    int rlen = recv(G_FD, buf, 1300, 0);

    if (check_response(buf, rlen, xid, GET_INFO_RSP))
        return -1;

    if (rlen < 16)
        RETERR("[protocol err] recv too short\n");

    uint64_t file_size;
    memcpy(&file_size, buf + 8, 8);

    int max_copy_size = rlen - 16;
    if (max_copy_size > (int) buf_size - 1) // -1 for the null terminator
        max_copy_size = (int) buf_size - 1;

    memcpy(info_buf, buf + 16, max_copy_size);
    info_buf[max_copy_size] = '\0';

    return file_size;
}

int get_pkg_deps(int64_t id, int64_t *dep_buf, size_t buf_size) { // returns number of deps
    uint8_t buf[STP_PKT_SIZE];
    uint64_t r = GET_DEP;

    uint64_t xid = get_random_xid();

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);
    memcpy(buf + 8, &id, 8);

    purge_receive_buffer();

    if (send(G_FD, buf, 16, 0) == -1)
        RETERR("[protocol err] send error %d\n", errno);

    int rlen = recv(G_FD, buf, 1300, 0);

    if (check_response(buf, rlen, xid, GET_DEP_RSP))
        return -1;

    int num_deps = (rlen - 8) / 8;

    if (num_deps > (int) buf_size)
        num_deps = (int) buf_size;

    for (int i = 0; i < num_deps; i++) {
        int64_t dep_id;
        memcpy(&dep_id, buf + 8 + i * 8, 8);
        dep_buf[i] = dep_id;
    }

    return num_deps;
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
    if (port <= 0 || port > 65535) {
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
    
    int64_t id = get_pkg_id("tcc");

    printf("id = %"PRId64"\n", id);

    if (id == -1)
        return 1;

    char info_buf[STP_MAX_DESC_SIZE + 1];
    int64_t file_size = get_pkg_info(id, info_buf, sizeof(info_buf));

    printf("file size = %"PRId64"\n", file_size);

    if (file_size == -1)
        return 1;

    printf("info = %s\n", info_buf);

    int64_t deps[STP_MAX_DEPS];
    int num_deps = get_pkg_deps(id, deps, STP_MAX_DEPS);

    printf("num_deps = %d\n", num_deps);

    if (num_deps == -1)
        return 1;

    for (int i = 0; i < num_deps; i++) {
        printf("dep %d: %"PRId64"\n", i, deps[i]);
    }

    close(fd);
    return 0;
}
