#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define UVC_SET_CUR 0x01
#define XU_UNIT 3
#define XU_DFU_SELECTOR 1
#define FH_XU_SIZE 0x0b

static void usage(const char *name)
{
    printf("Usage: %s --device /dev/cam_left|/dev/cam_right\n"
           "\nSends the vendor partial-upgrade command to exactly one UVC camera.\n",
           name);
}

static int is_uvc_device(int fd, const char *device)
{
    struct v4l2_capability capability;

    memset(&capability, 0, sizeof(capability));
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        fprintf(stderr, "VIDIOC_QUERYCAP failed for %s: %s\n", device, strerror(errno));
        return 0;
    }
    if (strcmp((const char *)capability.driver, "uvcvideo") != 0) {
        fprintf(stderr, "%s is not handled by uvcvideo (driver=%s)\n",
                device, capability.driver);
        return 0;
    }
    return 1;
}

static int enter_partial_dfu(int fd)
{
    unsigned char command[FH_XU_SIZE] = {0x83, 0x00};
    struct uvc_xu_control_query control;

    memset(&control, 0, sizeof(control));
    control.unit = XU_UNIT;
    control.selector = XU_DFU_SELECTOR;
    control.query = UVC_SET_CUR;
    control.size = FH_XU_SIZE;
    control.data = command;
    return ioctl(fd, UVCIOC_CTRL_QUERY, &control);
}

int main(int argc, char **argv)
{
    const char *device = NULL;
    int fd;
    int i;

    for (i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "-d") || !strcmp(argv[i], "--device")) && i + 1 < argc) {
            device = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!device) {
        usage(argv[0]);
        return 2;
    }

    fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open failed for %s: %s\n", device, strerror(errno));
        return 1;
    }
    if (!is_uvc_device(fd, device)) {
        close(fd);
        return 1;
    }
    if (enter_partial_dfu(fd) < 0) {
        fprintf(stderr, "UVC DFU trigger failed for %s: %s\n", device, strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    printf("DFU trigger sent to %s\n", device);
    return 0;
}
