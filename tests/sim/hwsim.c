/* LD_PRELOAD hardware simulator.
 *
 * Lets the real emb-bus-diag-v2 binary run its I2C, SPI and CAN code paths on a
 * machine with no hardware (and no kernel drivers) by emulating the system
 * calls the HAL makes:
 *
 *   I2C  /dev/i2c-1      INA219 @0x40 (reg 0x02), TMP102 @0x48 (reg 0x00)
 *   SPI  /dev/spidev0.0  one register device
 *   CAN  vcan0 / can0    noise frames, an error frame, then the target frame
 *
 * Values are 16-bit raw words, overridable with SIM_INA219, SIM_TMP102,
 * SIM_SPI and SIM_CAN (e.g. SIM_TMP102=0xF600 -> -10 C).
 * Build: see Makefile target `sim`.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <net/if.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

enum { K_NONE = 0, K_I2C, K_SPI, K_CAN };
#define MAXFD 1024
static unsigned char g_kind[MAXFD];
static unsigned g_can_reads[MAXFD];

static uint16_t env16(const char *name, uint16_t def) {
    const char *e = getenv(name);
    return e ? (uint16_t)strtoul(e, NULL, 0) : def;
}

static int new_fd(int kind) {
    int fd = open("/dev/null", O_RDWR);   /* real fd so close() works */
    if (fd >= 0 && fd < MAXFD) { g_kind[fd] = (unsigned char)kind; g_can_reads[fd] = 0; }
    return fd;
}
static int kind_of(int fd) { return (fd >= 0 && fd < MAXFD) ? g_kind[fd] : K_NONE; }

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    if (path && strcmp(path, "/dev/i2c-1") == 0) return new_fd(K_I2C);
    if (path && strcmp(path, "/dev/spidev0.0") == 0) return new_fd(K_SPI);
    int (*real)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    return real(path, flags, mode);
}

int close(int fd) {
    int (*real)(int) = dlsym(RTLD_NEXT, "close");
    if (fd >= 0 && fd < MAXFD) g_kind[fd] = K_NONE;
    return real(fd);
}

int socket(int domain, int type, int protocol) {
    if (domain == PF_CAN) return new_fd(K_CAN);
    int (*real)(int, int, int) = dlsym(RTLD_NEXT, "socket");
    return real(domain, type, protocol);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    if (kind_of(fd) == K_CAN) return 0;
    int (*real)(int, const struct sockaddr *, socklen_t) = dlsym(RTLD_NEXT, "bind");
    return real(fd, addr, len);
}

static int i2c_lookup(uint8_t addr, uint8_t reg, uint16_t *out) {
    if (addr == 0x40 && reg == 0x02) { *out = env16("SIM_INA219", 0x5DC0); return 0; } /* 12.0 V  */
    if (addr == 0x48 && reg == 0x00) { *out = env16("SIM_TMP102", 0x1900); return 0; } /* 25.0 C  */
    return -1;
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    switch (kind_of(fd)) {
    case K_I2C:
        if (req == I2C_SLAVE) return 0;
        if (req == I2C_RDWR) {
            struct i2c_rdwr_ioctl_data *d = arg;
            if (d->nmsgs != 2 || d->msgs[0].len != 1 || !(d->msgs[1].flags & I2C_M_RD) || d->msgs[1].len < 2) {
                errno = EINVAL; return -1;
            }
            uint16_t v;
            if (i2c_lookup((uint8_t)d->msgs[0].addr, d->msgs[0].buf[0], &v) != 0) {
                errno = EREMOTEIO; return -1;      /* NACK */
            }
            d->msgs[1].buf[0] = (uint8_t)(v >> 8);
            d->msgs[1].buf[1] = (uint8_t)(v & 0xFF);
            return 2;
        }
        errno = ENOTTY; return -1;
    case K_SPI:
        if (req == SPI_IOC_WR_MODE || req == SPI_IOC_WR_BITS_PER_WORD || req == SPI_IOC_WR_MAX_SPEED_HZ) return 0;
        if (_IOC_TYPE(req) == SPI_IOC_MAGIC && _IOC_NR(req) == 0) {
            struct spi_ioc_transfer *tr = arg;
            uint8_t *rx = (uint8_t *)(uintptr_t)tr->rx_buf;
            uint16_t v = env16("SIM_SPI", 0x1388);   /* 5000 * 0.001 = 5.0 V */
            memset(rx, 0, tr->len);
            if (tr->len >= 3) { rx[1] = (uint8_t)(v >> 8); rx[2] = (uint8_t)(v & 0xFF); }
            return (int)tr->len;
        }
        errno = ENOTTY; return -1;
    case K_CAN:
        if (req == SIOCGIFINDEX) {
            struct ifreq *ifr = arg;
            if (strcmp(ifr->ifr_name, "vcan0") == 0 || strcmp(ifr->ifr_name, "can0") == 0) {
                ifr->ifr_ifindex = 1; return 0;
            }
            errno = ENODEV; return -1;
        }
        errno = ENOTTY; return -1;
    default: {
        int (*real)(int, unsigned long, ...) = dlsym(RTLD_NEXT, "ioctl");
        return real(fd, req, arg);
    }
    }
}

int poll(struct pollfd *fds, nfds_t n, int timeout) {
    if (n == 1 && kind_of(((volatile struct pollfd *)fds)[0].fd) == K_CAN) {
        ((volatile struct pollfd *)fds)[0].revents = POLLIN;
        return 1;
    }
    int (*real)(struct pollfd *, nfds_t, int) = dlsym(RTLD_NEXT, "poll");
    return real(fds, n, timeout);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (kind_of(fd) == K_CAN) {
        if (count < sizeof(struct can_frame)) { errno = EINVAL; return -1; }
        struct can_frame f;
        memset(&f, 0, sizeof f);
        unsigned n = g_can_reads[fd]++;
        uint16_t v = env16("SIM_CAN", 0x5DC0);
        switch (n) {
        case 0:  f.can_id = CAN_ERR_FLAG | 0x004; f.can_dlc = 8; break;               /* error frame  */
        case 1:  f.can_id = 0x200; f.can_dlc = 2; f.data[0] = 0xFF; f.data[1] = 0xFF; break; /* other node */
        case 2:  f.can_id = 0x123 | CAN_RTR_FLAG; f.can_dlc = 0; break;               /* RTR request  */
        case 3:  if (getenv("SIM_CAN_EXT")) {   /* standard frame with the same low bits must be ignored */
                     f.can_id = 0x1ABCDE & CAN_SFF_MASK; f.can_dlc = 2; f.data[0] = 0xFF; f.data[1] = 0xFF; break;
                 }
                 /* fall through */
        default: f.can_id = getenv("SIM_CAN_EXT") ? (0x1ABCDE | CAN_EFF_FLAG) : 0x123; f.can_dlc = 2; f.data[0] = (uint8_t)(v >> 8); f.data[1] = (uint8_t)(v & 0xFF);
        }
        memcpy(buf, &f, sizeof f);
        return (ssize_t)sizeof f;
    }
    ssize_t (*real)(int, void *, size_t) = dlsym(RTLD_NEXT, "read");
    return real(fd, buf, count);
}
