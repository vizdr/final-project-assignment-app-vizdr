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
#include <sys/ioctl.h> // <-- needed for ioctl()
#include <net/if.h>    // <-- needed for struct ifreq
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>
#include <syslog.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_sig(int sig)
{
    (void)sig;
    g_stop = 1;
}

#define FILE_PATH "/var/tmp/audio_detection"
#define CAN_INTERFACE "can0"
#define CAN_ID 0x123        // CAN ID to send the detection count
#define READ_INTERVAL_SEC 5 // Read file every 5 seconds
int main()
{
    openlog("CAN_Detection_Sender", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "CAN Detection Sender started.");
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
            syslog(LOG_INFO, "[INIT] INPUT_FILE not found. Creating %s with value 0...", FILE_PATH);
            printf("Input file %s not found. Creating with initial value 0.\n", FILE_PATH);
            int fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                perror("open (creating INPUT_FILE)");
                exit(EXIT_FAILURE);
            }
            syslog(LOG_INFO, "[INIT] INPUT_FILE created successfully.");
            printf("Input file %s created with initial value 0.\n", FILE_PATH);

            const char *init_value = "0\n";
            if (write(fd, init_value, strlen(init_value)) != (ssize_t)strlen(init_value))
            {
                syslog(LOG_ERR, "Failed to write initial value to %s: %s", FILE_PATH, strerror(errno));
                printf("Failed to write initial value to %s\n", FILE_PATH);
                perror("write (initializing INPUT_FILE)");
                close(fd);
                exit(EXIT_FAILURE);
            }

            close(fd);
            syslog(LOG_INFO, "[INIT] INPUT_FILE created successfully.");
            printf("Input file %s created with initial value 0.\n", FILE_PATH);
        }
        else
        {
            perror("stat INPUT_FILE");
            exit(EXIT_FAILURE);
        }
    }

    // --- Open CAN socket ---
    if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        printf("Error while opening CAN socket\n");
        syslog(LOG_ERR, "Error while opening CAN socket: %s", strerror(errno));
        perror("Error while opening CAN socket");
        return 1;
    }

    // Increase TX buffer to reduce ENOBUFS at startup
    int txbuf_size = 100 * sizeof(struct can_frame);
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &txbuf_size, sizeof(txbuf_size)) < 0)
    {
        perror("setsockopt SO_SNDBUF failed");
        syslog(LOG_WARNING, "Failed to set CAN TX buffer size: %s", strerror(errno));
    }

    strcpy(ifr.ifr_name, CAN_INTERFACE);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0)
    {
        printf("Error in ioctl SIOCGIFINDEX\n");
        syslog(LOG_ERR, "ioctl SIOCGIFINDEX failed: %s", strerror(errno));
        perror("ioctl SIOCGIFINDEX failed");
        close(sock);
        return 1;
    }

    printf("CAN socket opened on interface %s\n", CAN_INTERFACE);

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("CAN socket bind failed");
        syslog(LOG_ERR, "CAN socket bind failed: %s", strerror(errno));
        printf("CAN socket bind failed\n");
        close(sock);
        return 1;
    }

    printf("CAN socket bound on interface %s\n", CAN_INTERFACE);

    printf("Waiting 100 ms for CAN interface to initialize...\n");
    syslog(LOG_INFO, "Waiting 100 ms for CAN interface to initialize...");
    usleep(100000);

    // --- Wait for interface to be fully up ---
    int retry = 10;
    while (retry--)
    {
        struct ifreq ifr_check;
        strcpy(ifr_check.ifr_name, CAN_INTERFACE);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr_check) == 0 &&
            (ifr_check.ifr_flags & IFF_UP))
        {
            break; // interface ready
        }
        syslog(LOG_WARNING, "CAN interface %s not ready, waiting 100ms...", CAN_INTERFACE);
        usleep(100000); // 100ms
    }

    // --- Main loop ---
    while (!g_stop)
    {
        FILE *fp = fopen(FILE_PATH, "r");
        if (!fp)
        {
            syslog(LOG_ERR, "Error opening detection file %s: %s", FILE_PATH, strerror(errno));
            perror("Error opening detection file");
            printf("Retrying in %d seconds...\n", READ_INTERVAL_SEC);
            sleep(READ_INTERVAL_SEC);
            continue;
        }

        int detection_count = 0;
        if (fscanf(fp, "%d", &detection_count) != 1)
        {
            printf("Failed to read integer from file %s\n", FILE_PATH);
            syslog(LOG_ERR, "Failed to read integer from file %s", FILE_PATH);
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
        if (write(sock, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
        {
            printf("CAN write failed\n");
            syslog(LOG_ERR, "CAN write failed: %s", strerror(errno));
            perror("CAN write failed");
        }
        else
        {
            printf("Sent detection_count=%d over CAN ID=0x%X\n", detection_count, CAN_ID);
        }

        sleep(READ_INTERVAL_SEC);
    }

    close(sock);
    syslog(LOG_INFO, "CAN Detection Sender stopped.");
    closelog();
    return 0;
}
