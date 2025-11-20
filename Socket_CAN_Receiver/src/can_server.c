// can_server.c
// Minimal CAN receiver for Raspberry Pi (Waveshare RS485 CAN HAT)
// Compatible with your can_send_detection.c sender.
//
// - Listens on CAN ID 0x123
// - Extracts 4-byte integer from frame.data
// - Writes value to /var/tmp/can_received_value
// - Logs all received messages to /var/log/can-server.log
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>
#include <syslog.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

#define CAN_INTERFACE "can0"
#define CAN_ID        0x123

#define OUTPUT_FILE   "/var/tmp/audio_detection"
#define LOG_FILE      "/var/log/can-server.log"

// Write timestamp in YYYY-MM-DD HH:MM:SS format
static void write_timestamp(FILE *logf)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(logf, "%04d-%02d-%02d %02d:%02d:%02d ",
            t->tm_year + 1900,
            t->tm_mon + 1,
            t->tm_mday,
            t->tm_hour,
            t->tm_min,
            t->tm_sec);
    fflush(logf);
}

int main()
{
    openlog("CAN_Receiver_Server", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "CAN Receiver app started.");
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

    // --- Open log file ---
    FILE *logf = fopen(LOG_FILE, "a");
    if (!logf) {
        syslog(LOG_ERR, "Failed to open log file %s: %s", LOG_FILE, strerror(errno));
        perror("Failed to open log file, logging to stdout");
        logf = stdout;  // use stdout fallback
    } else {
        fprintf(logf, "\n--- CAN server started ---\n");
        syslog(LOG_INFO, "CAN Server started, Logging to %s", LOG_FILE);
        fflush(logf);
    }

    // --- Open CAN socket ---
    if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        syslog(LOG_ERR, "Error opening CAN socket: %s", strerror(errno));
        perror("Error opening CAN socket");
        return 1;
    }

    strcpy(ifr.ifr_name, CAN_INTERFACE);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        syslog(LOG_ERR, "ioctl SIOCGIFINDEX failed: %s", strerror(errno));
        perror("ioctl failed");
        close(sock);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "CAN socket bind failed: %s", strerror(errno));
        perror("CAN socket bind failed");
        close(sock);
        return 1;
    }

    printf("Waiting 100 ms for CAN interface to initialize...\n");
    syslog(LOG_INFO, "Waiting 100 ms for CAN interface to initialize...");
    usleep(100000);

    fprintf(logf, "Listening on %s for CAN ID 0x%X\n",
            CAN_INTERFACE, CAN_ID);
    fflush(logf);

    // Apply filter for CAN ID 0x123
    struct can_filter rfilter[1];
    rfilter[0].can_id   = CAN_ID;
    rfilter[0].can_mask = CAN_SFF_MASK;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    // --- Main receive loop ---
    while (!g_stop) {
        ssize_t nbytes = read(sock, &frame, sizeof(frame));

        if (nbytes < 0) {
            write_timestamp(logf);
            syslog(LOG_ERR, "CAN read error: %s", strerror(errno));
            fflush(logf);
            continue;
        }

        if (nbytes < sizeof(struct can_frame)) {
            write_timestamp(logf);
            syslog(LOG_WARNING, "Short CAN frame received");
            fflush(logf);
            continue;
        }

        // Extract integer exactly as sent by sender
        if (frame.can_dlc < sizeof(int)) {
            write_timestamp(logf);
            syslog(LOG_WARNING, "Invalid DLC %d (expected >= 4)", frame.can_dlc);
            fflush(logf);
            continue;
        }

        int value = 0;
        memcpy(&value, frame.data, sizeof(int));

        // Write to output file
        FILE *fp = fopen(OUTPUT_FILE, "w+");
        if (!fp) {
            write_timestamp(logf);
            syslog(LOG_ERR, "Error opening %s: %s", OUTPUT_FILE, strerror(errno));
            fflush(logf);
        } else {
            fprintf(fp, "%d\n", value);
            fclose(fp);
        }

        // Log the received value
        write_timestamp(logf);
        syslog(LOG_INFO, "Received CAN ID=0x%X value=%d", frame.can_id, value);
        fflush(logf);

        usleep(10000); // 10ms small delay to avoid busy looping
    }

    close(sock);
    syslog(LOG_INFO, "CAN Receiver stopped.");
    closelog();

    if (logf != stdout)
        fclose(logf);

    return 0;
}

