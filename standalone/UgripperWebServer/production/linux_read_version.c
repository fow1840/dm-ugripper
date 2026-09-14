#define _GNU_SOURCE

#include <fcntl.h>
#include <linux/uvcvideo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define UVC_SET_CUR 0x01
#define UVC_GET_CUR 0x81
#define XU_UNIT 3
#define XU_VERSION_SELECTOR 2
#define FH_XU_SIZE 0x0b

static int version_xu_query(int fd, unsigned int query, unsigned char *data)
{
    struct uvc_xu_control_query control;

    memset(&control, 0, sizeof(control));
    control.unit = XU_UNIT;
    control.selector = XU_VERSION_SELECTOR;
    control.query = (uint8_t)query;
    control.size = FH_XU_SIZE;
    control.data = data;
    return ioctl(fd, UVCIOC_CTRL_QUERY, &control);
}

static int format_version(uint32_t packed, char *text, size_t text_size)
{
    unsigned int year = 2000U + (packed & 0xffU);
    unsigned int month = (packed >> 8) & 0x3fU;
    unsigned int day = (packed >> 14) & 0x3fU;
    unsigned int hour = (packed >> 20) & 0x3fU;
    unsigned int minute = (packed >> 26) & 0x3fU;

    if (!text || text_size < 17 || packed <= 0x1000U || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute > 59) {
        return -1;
    }
    snprintf(text, text_size, "%04u-%02u-%02u-%02u%02u", year, month, day, hour, minute);
    return 0;
}

static int read_device_version(const char *device, char *text, size_t text_size)
{
    unsigned char data[FH_XU_SIZE] = {0};
    uint32_t packed;
    uint32_t reserved;
    int fd;

    if (!device || !text) {
        return -1;
    }
    fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(device);
        return -1;
    }
    memcpy(data, "VERSI", 5);
    if (version_xu_query(fd, UVC_SET_CUR, data) < 0) {
        perror("UVC_SET_CUR VERSI");
        close(fd);
        return -1;
    }
    memset(data, 0, sizeof(data));
    if (version_xu_query(fd, UVC_GET_CUR, data) < 0) {
        perror("UVC_GET_CUR VERSI");
        close(fd);
        return -1;
    }
    close(fd);
    memcpy(&packed, data, sizeof(packed));
    memcpy(&reserved, data + sizeof(packed), sizeof(reserved));
    return reserved == 0 ? format_version(packed, text, text_size) : -1;
}

static int read_image_version(const char *image, char *text, size_t text_size)
{
    struct stat image_stat;
    unsigned char data[8];
    off_t offset;
    uint32_t packed;
    uint32_t inverse;
    int fd;

    if (!image || stat(image, &image_stat) < 0 || image_stat.st_size <= 0) {
        return -1;
    }
    offset = image_stat.st_size > 0x300000 ? 0x74fff0 : 0xafff0;
    if (image_stat.st_size < offset + (off_t)sizeof(data)) {
        return -1;
    }
    fd = open(image, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(image);
        return -1;
    }
    if (pread(fd, data, sizeof(data), offset) != (ssize_t)sizeof(data)) {
        perror("pread firmware version");
        close(fd);
        return -1;
    }
    close(fd);
    memcpy(&packed, data, sizeof(packed));
    memcpy(&inverse, data + sizeof(packed), sizeof(inverse));
    return inverse == ~packed ? format_version(packed, text, text_size) : -1;
}

static void usage(const char *name)
{
    printf("Usage: %s [--image FILE] [--device /dev/cam_left|/dev/cam_right]\n"
           "\nReads the firmware image version, one UVC camera version, or both.\n"
           "At least one of --image and --device is required.\n",
           name);
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    const char *device = NULL;
    char image_version[32] = {0};
    char device_version[32] = {0};
    int failed = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "-i") || !strcmp(argv[i], "--image")) && i + 1 < argc) {
            image = argv[++i];
        } else if ((!strcmp(argv[i], "-d") || !strcmp(argv[i], "--device")) && i + 1 < argc) {
            device = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!image && !device) {
        usage(argv[0]);
        return 2;
    }

    if (image) {
        if (read_image_version(image, image_version, sizeof(image_version)) == 0) {
            printf("待升级固件版本: %s\n", image_version);
        } else {
            fprintf(stderr, "待升级固件版本读取失败: %s\n", image);
            failed = 1;
        }
    }
    if (device) {
        if (read_device_version(device, device_version, sizeof(device_version)) == 0) {
            printf("当前 UVC 摄像头版本 (%s): %s\n", device, device_version);
        } else {
            fprintf(stderr, "当前 UVC 摄像头版本读取失败: %s\n", device);
            failed = 1;
        }
    }
    return failed;
}
