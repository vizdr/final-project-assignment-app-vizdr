// can_send_detection.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>   // <-- needed for ioctl()
#include <net/if.h>      // <-- needed for struct ifreq

#include <linux/can.h>
#include <linux/can/raw.h>


#define FILE_PATH "/var/tmp/audio_detection"
#define CAN_INTERFACE "can0"
#define CAN_ID 0x123          // CAN ID to send the detection count
#define READ_INTERVAL_SEC 5   // Read file every 5 seconds

int main()
{
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;

    // --- Open CAN socket ---
    if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Error while opening CAN socket");
        return 1;
    }

    strcpy(ifr.ifr_name, CAN_INTERFACE);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX failed");
        close(sock);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("CAN socket bind failed");
        close(sock);
        return 1;
    }

    printf("CAN socket opened on interface %s\n", CAN_INTERFACE);

    // --- Main loop ---
    while (1) {
        FILE *fp = fopen(FILE_PATH, "r");
        if (!fp) {
            perror("Error opening detection file");
            sleep(READ_INTERVAL_SEC);
            continue;
        }

        int detection_count = 0;
        if (fscanf(fp, "%d", &detection_count) != 1) {
            fprintf(stderr, "Failed to read integer from file\n");
            fclose(fp);
            sleep(READ_INTERVAL_SEC);
            continue;
        }
        fclose(fp);

        // --- Prepare CAN frame ---
        frame.can_id = CAN_ID;
        frame.can_dlc = sizeof(int);
        memcpy(frame.data, &detection_count, sizeof(int));

        // --- Send CAN frame ---
        if (write(sock, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
            perror("CAN write failed");
        } else {
            printf("Sent detection_count=%d over CAN ID=0x%X\n", detection_count, CAN_ID);
        }

        sleep(READ_INTERVAL_SEC);
    }

    close(sock);
    return 0;
}
