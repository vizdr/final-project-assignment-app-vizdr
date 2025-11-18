// can_send_detection.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>   // <-- needed for ioctl()
#include <net/if.h>      // <-- needed for struct ifreq
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

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
    struct sigaction sa;

     /* Setup signal handling */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // -------------------------------------------------------------
    // Ensure the input file exists; if not, create it with "0"
    // -------------------------------------------------------------
    struct stat st;
    if (stat(FILE_PATH, &st) == -1)
    {
        if (errno == ENOENT)
        {
            fprintf(stderr, "[INIT] INPUT_FILE not found. Creating %s with value 0...\n", FILE_PATH);
            int fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                perror("open (creating INPUT_FILE)");
                exit(EXIT_FAILURE);
            }

            const char *init_value = "0\n";
            if (write(fd, init_value, strlen(init_value)) != (ssize_t)strlen(init_value))
            {
                perror("write (initializing INPUT_FILE)");
                close(fd);
                exit(EXIT_FAILURE);
            }

            close(fd);
            fprintf(stderr, "[INIT] INPUT_FILE created successfully.\n");
        }
        else
        {
            perror("stat INPUT_FILE");
            exit(EXIT_FAILURE);
        }
    }

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
    while (!g_stop) {
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
