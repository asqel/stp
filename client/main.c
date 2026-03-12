#include <sys/socket.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

// client setings (editable)
#define DEFAULT_IP "asqel.ddns.net:42024"
#define RECV_TIMEOUT_MS 1000  // stop waiting for a server response after this delay
#define MAX_RETRY_COUNT 4     // give up after this many retries (after a timeout)
#define FAST_DL_ONCE    16    // ask for 16 parts at once in download_pkg_fast

// protocol types
#define GET_ID          0x01
#define GET_ID_RSP      0x02
#define GET_INFO        0x03
#define GET_INFO_RSP    0x04
#define READ_PART       0x07
#define READ_PART_RSP   0x08
#define GET_DEP         0x09
#define GET_DEP_RSP     0x0A

// protocol error codes
#define ERR_UNKNOWN_REQUEST 0xFFFF
#define ERR_UPDATING        0xFF00
#define ERR_INV_ID          0xFF01
#define ERR_OUT_RANGE       0xFF03
#define ERR_TOO_LONG        0xFF04
#define ERR_FAIL            0xFF05

// protocol limits
#define STP_PKT_SIZE        1300
#define STP_MAX_DESC_SIZE   1000
#define STP_MAX_DEPS        (STP_PKT_SIZE - 8) / 8
#define STP_MAX_PART_SIZE   (STP_PKT_SIZE - 8)
#define STP_MAX_NAME_SIZE   64 // TODO verify after the new get_info

// XID and TYPE decoding macros
#define R64_TO_XID(r) ((r) & 0xFFFFFFFFFF)
#define R64_TO_TYPE(r) (((r) >> 48) & 0xFFFF)
#define TYPE_IS_ERR(t) (((t) & 0xFF00) == 0xFF00)

int G_FD;

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
 *             PROTOCOL UTILS             *
 *                                        *
********************************************/

static uint64_t get_random_xid(void) {
    return (((uint64_t) rand() << 32) | rand()) & 0xFFFFFFFFFF;
}

static void purge_receive_buffer(void) {
    // [probably useless] purge the buffer to read only the new response

    struct pollfd pfd = { .fd = G_FD, .events = POLLIN };
    uint8_t tmp[STP_PKT_SIZE];

    while (poll(&pfd, 1, 0) > 0) {
        recv(G_FD, tmp, sizeof(tmp), 0);
    }
}

static int timeout_recv(uint8_t *buf, size_t buf_size) {
    // returns -2 on timeout or the normal recv return value

    struct pollfd pfd = { .fd = G_FD, .events = POLLIN };

    int poll_ret = poll(&pfd, 1, RECV_TIMEOUT_MS);

    if (poll_ret == -1)
        return -1;
    else if (poll_ret == 0)
        return -2;

    return recv(G_FD, buf, buf_size, 0);
}

#define RETERR(...) return (fprintf(stderr, __VA_ARGS__), -1)
#define GOTOERR(section, ...) do {fprintf(stderr, __VA_ARGS__); goto section;} while(0)

static int stp_sarap(uint8_t *buf, int buf_len, uint16_t type) {
    // sarap: send and receive and process

    int retry_count = 0;

    send_and_wait:
    uint64_t xid = get_random_xid();
    uint64_t r = type;

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);

    purge_receive_buffer();

    if (send(G_FD, buf, buf_len, 0) == -1)
        RETERR("[protocol err] send error %d\n", errno);

    wait_for_response:
    int rlen = timeout_recv(buf, 1300);

    if (rlen == -2) {
        if (retry_count >= MAX_RETRY_COUNT)
            RETERR("[protocol err] recv timeout, max retry count reached\n");
        fprintf(stderr, "[protocol warn] recv timeout, retrying... (%d/%d)\n",
                    retry_count + 1, MAX_RETRY_COUNT);
        retry_count++;
        goto send_and_wait;
    }

    if (rlen < 0)
        RETERR("[protocol err] recv error %d\n", errno);

    if (rlen < 8)
        RETERR("[protocol err] recv too short\n");

    memcpy(&r, buf, 8);

    if (R64_TO_XID(r) != xid) {
        fprintf(stderr, "[protocol warn] wrong xid, waiting for response again\n");
        goto wait_for_response;
    }

    uint16_t resp_type = R64_TO_TYPE(r);

    if (TYPE_IS_ERR(resp_type))
        RETERR("[protocol err] error response received: %s (0x%04x)\n", error_to_str(resp_type), resp_type);

    if (resp_type != type + 1)
        RETERR("[protocol err] unexpected response type %x\n", resp_type);

    return rlen;
}

/*******************************************
 *                                        *
 *   STP CLIENT SIDE PROTOCOL FUNCTIONS   *
 *                                        *
********************************************/

int64_t get_pkg_id(const char *name) {
    if (strlen(name) > STP_MAX_NAME_SIZE)
        return 0; // too long to exist, 0 == not found

    uint8_t buf[STP_PKT_SIZE];
    uint64_t r;

    int s = strlen(name);
    memcpy(buf + 8, name, s);

    int rlen = stp_sarap(buf, 8 + s, GET_ID);
    if (rlen < 0)
        return -1;

    if (rlen != 16)
        RETERR("[protocol err] recv wrong length\n");

    memcpy(&r, buf + 8, 8);
    return r;
}

int64_t get_pkg_info(int64_t id, char *info_buf, size_t buf_size) {
    // returns file size

    uint8_t buf[STP_PKT_SIZE];
    memcpy(buf + 8, &id, 8);

    int rlen = stp_sarap(buf, 16, GET_INFO);
    if (rlen < 0)
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

int get_pkg_deps(int64_t id, int64_t *dep_buf, size_t buf_size) {
    // returns number of dependencies

    uint8_t buf[STP_PKT_SIZE];
    memcpy(buf + 8, &id, 8);

    int rlen = stp_sarap(buf, 16, GET_DEP);
    if (rlen < 0)
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

int download_pkg(int64_t id, const char *dest_path, int64_t file_size) {
    uint8_t buf[STP_PKT_SIZE];
    int64_t offset = 0;

    // open the local file for writing
    FILE *f = fopen(dest_path, "wb");
    if (!f)
        RETERR("failed to open local file for writing\n");

    while (offset < file_size) {
        int part_size = (int) (file_size - offset);
        if (part_size > STP_MAX_PART_SIZE)
            part_size = STP_MAX_PART_SIZE;

        memcpy(buf + 8, &id, 8);
        memcpy(buf + 16, &offset, 8);
        memcpy(buf + 24, &part_size, 2);

        int rlen = stp_sarap(buf, 26, READ_PART);

        if (rlen < 0)
            goto error;

        if (rlen < 8 + part_size)
            GOTOERR(error, "[protocol err] recv too short\n");

        size_t written = fwrite(buf + 8, 1, part_size, f);
        if (written != (size_t) part_size)
            GOTOERR(error, "failed to write to local file\n");

        offset += part_size;

        printf("downloaded %"PRId64"/%"PRId64" bytes\r", offset, file_size);
        fflush(stdout);
    }

    printf("\n");
    fclose(f);
    return 0;

    error:
    fclose(f);
    remove(dest_path);
    return -1;
}

int fast_download_send(uint64_t xid, int64_t id, int64_t offset, int64_t file_size) {
    uint8_t buf[STP_PKT_SIZE];
    uint64_t r = READ_PART;

    int part_size = (int) file_size - offset;
    if (part_size > STP_MAX_PART_SIZE)
        part_size = STP_MAX_PART_SIZE;

    memcpy(buf, &xid, 6);
    memcpy(buf + 6, &r, 2);
    memcpy(buf + 8, &id, 8);
    memcpy(buf + 16, &offset, 8);
    memcpy(buf + 24, &part_size, 2);

    if (send(G_FD, buf, 26, 0) == -1)
        RETERR("[protocol err] send error %d\n", errno);

    return 0;
}

int fast_download_pkg(int64_t id, const char *dest_path, int64_t file_size) {
    uint8_t buf[STP_PKT_SIZE];
    int64_t received_bytes = 0, offset = 0;

    if (FAST_DL_ONCE > 255)
        RETERR("FAST_DL_ONCE must be < 256\n");
 
    // open the local file for writing
    FILE *f = fopen(dest_path, "wb");
    if (!f)
        RETERR("failed to open local file for writing\n");

    char received_parts[FAST_DL_ONCE]; // bitmap to track received parts

    while (offset < file_size) {
        int64_t part_offset = offset;
        int to_wait;
    
        memset(received_parts, 0, sizeof(received_parts));

        // we use the last bytes of the XID to identify the part in the response
        uint64_t xid = get_random_xid() & 0xFFFFFFFF00;

        // receive responses
        int retry_count = 0;

        for (to_wait = 0; to_wait < FAST_DL_ONCE && offset + to_wait * STP_MAX_PART_SIZE < file_size; to_wait++);
        send_and_wait:

        // send requests for the parts
        for (int i = 0; i < FAST_DL_ONCE && offset < file_size; i++) {
            if (!received_parts[i] && fast_download_send(xid + i, id, offset, file_size))
                goto error;

            offset += STP_MAX_PART_SIZE;
        }

        while (to_wait > 0) {
            int rlen = timeout_recv(buf, sizeof(buf));

            if (rlen == -2) {
                if (retry_count >= MAX_RETRY_COUNT)
                    GOTOERR(error, "[protocol err] recv timeout, max retry count reached\n");
                retry_count++;
                offset = part_offset; // reset offset to resend the same parts
                goto send_and_wait;
            }

            if (rlen < 0)
                GOTOERR(error, "[protocol err] recv error %d\n", errno);

            if (rlen < 8)
                GOTOERR(error, "[protocol err] recv too short\n");

            uint64_t r;
            memcpy(&r, buf, 8);
            uint16_t resp_type = R64_TO_TYPE(r);

            if (TYPE_IS_ERR(resp_type))
                GOTOERR(error, "[protocol err] error response received: %s (0x%04x)\n",
                            error_to_str(resp_type), resp_type);

            if (resp_type != READ_PART_RSP)
                GOTOERR(error, "[protocol err] unexpected response type 0x%04x\n", resp_type);

            int part_index = R64_TO_XID(r) & 0xFF;

            if ((R64_TO_XID(r) & 0xFFFFFFFF00) != xid || part_index >= FAST_DL_ONCE || received_parts[part_index])
                continue; // probably an old response timed out

            int expected_len = file_size - (part_offset + part_index * STP_MAX_PART_SIZE);
            if (expected_len > STP_MAX_PART_SIZE)
                expected_len = STP_MAX_PART_SIZE;
            expected_len += 8; // +8 for the header

            if (rlen != expected_len)
                GOTOERR(error, "[protocol err] recv wrong length %d (expected %d)\n", rlen, expected_len);

            // write the data to the file at the correct offset
            if (fseek(f, part_offset + part_index * STP_MAX_PART_SIZE, SEEK_SET) != 0)
                GOTOERR(error, "failed to seek in local file\n");

            rlen -= 8;

            if (fwrite(buf + 8, 1, rlen, f) != (size_t) rlen)
                GOTOERR(error, "failed to write to local file\n");

            received_bytes += rlen;
            printf("downloaded %"PRId64"/%"PRId64" bytes\r", received_bytes, file_size);
            fflush(stdout);

            received_parts[part_index] = 1;
            to_wait--;
        }
    }

    fclose(f);
    return 0;

    error:
    fclose(f);
    remove(dest_path);
    return -1;
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

    for (int i = 0; i < num_deps; i++)
        printf("dep %d: %"PRId64"\n", i, deps[i]);


    if (fast_download_pkg(id, "tcc.txt", file_size))
        return 1;

    close(fd);
    return 0;
}
