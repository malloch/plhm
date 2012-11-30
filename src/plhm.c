/*
 * "plhm" and "libplhm" are copyright 2009, Stephen Sinclair and
 * authors listed in file AUTHORS.
 *
 * written at:
 *   Input Devices and Music Interaction Laboratory
 *   McGill University, Montreal, Canada
 *
 * This code is licensed under the GNU General Public License v2.1 or
 * later.  See COPYING for more information.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

#include "config.h"

#ifdef HAVE_LIBLO
#include <lo/lo.h>
#endif

#ifdef HAVE_LIBMAPPER
#include <mapper/mapper.h>
#endif

#include <plhm.h>

double starttime;
double curtime;
struct timeval temp;
struct timeval prev;

int listen_port=0;
int started = 0;
int device_found = 0;
int data_good = 0;
int poll_period = 0;

#ifdef HAVE_LIBLO
lo_address addr = 0;

void liblo_error(int num, const char *msg, const char *path);
int start_handler(const char *path, const char *types, lo_arg **argv, int argc,
                  void *data, void *user_data);
int stop_handler(const char *path, const char *types, lo_arg **argv, int argc,
                 void *data, void *user_data);
int status_handler(const char *path, const char *types, lo_arg **argv, int argc,
                   void *data, void *user_data);
#else
int addr = 1;
#endif

#ifdef HAVE_LIBMAPPER
mapper_timetag_t timetag;
mapper_device map_dev = 0;
mapper_signal sig_pos[8][3];
mapper_signal sig_eul[8][3];
#endif

int read_stations_and_send(plhm_t *pol, int poll);

typedef union {
    const int *i;
    const unsigned int *ui;
    const short *s;
    const unsigned short *us;
    const float *f;
    const char *c;
    const unsigned char *uc;
} multiptr;

/* macros */
#define CHECKBRK(m,x) if (x) { printf("[plhm] error: " m "\n"); break; }
#define LOG(...) if (outfile) { fprintf(outfile, __VA_ARGS__); }

/* option flags */
static int daemon_flag = 0;
static int hex_flag = 0;
static int euler_flag = 0;
static int position_flag = 0;
static int timestamp_flag = 0;
static int reset_flag = 0;
static int mapper_flag = 0;

const char *device_name = "/dev/ttyUSB0";
const char *osc_url = 0;
const char *mapper_alias = 0;

FILE *outfile = 0;

void ctrlc_handler(int sig) {
    started = 0;
}

int main(int argc, char *argv[])
{
    static struct option long_options[] =
    {
        {"daemon",   no_argument,       &daemon_flag,   1},
        {"device",   required_argument, 0,              'd'},
        {"hex",      no_argument,       &hex_flag,      1},
        {"euler",    no_argument,       &euler_flag,    1},
        {"position", no_argument,       &position_flag, 1},
        {"timestamp",no_argument,       &timestamp_flag,1},
        {"output",   optional_argument, 0,              'o'},
#ifdef HAVE_LIBLO
        {"send",     required_argument, 0,              's'},
        {"listen",   required_argument, 0,              'l'},
#endif
#ifdef HAVE_LIBMAPPER
        {"mapper",   no_argument,       &mapper_flag,   1},
#endif
        {"poll",     optional_argument, 0,              'p'},
        {"help",     no_argument,       0,              0},
        {"version",  no_argument,       0,              'V'},
        {"reset",    no_argument,       &reset_flag,    1},
        {0, 0, 0, 0}
    };

    while (1)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "Dd:HEPTo::s:l:mhVp::",
                            long_options, &option_index);
        if (c==-1)
            break;

        switch (c)
        {
        case 0:
            if (long_options[option_index].flag != 0)
                break;
            break;

        case 'D':
            daemon_flag = 1;
            break;

        case 'H':
            hex_flag = 1;
            break;

        case 'P':
            position_flag = 1;
            break;

        case 'E':
            euler_flag = 1;
            break;

        case 'T':
            timestamp_flag = 1;
            break;

        case 'd':
            // serial device name
            device_name = optarg;
            break;

#ifdef HAVE_LIBLO
        case 'u':
            // handle OSC url (liblo)
            osc_url = optarg;
            break;

        case 'l':
            listen_port = atoi(optarg);
            break;
#endif

#ifdef HAVE_LIBMAPPER
        case 'm':
            mapper_flag = 1;
            break;
#endif

        case 'p':
            poll_period = -1;
            if (optarg)
                poll_period = (int)(atof(optarg)*1000);
            if (!poll_period) {
                printf("[plhm] Please specify a poll period in milliseconds.\n");
                exit(1);
            }
            break;

        case 'o':
            // output file name, if specified
            // otherwise, stdout
            if (optarg) {
                outfile = fopen(optarg, "w");
            }
            else
                outfile = stdout;
            break;

        case 'V':
            printf(PACKAGE_STRING "  (" __DATE__ ")\n");
            exit(0);
            break;

        case '?':
            break;

        default:
        case 'h':
            printf("Usage: %s [options]\n"
"  where options are:\n"
"  -D --daemon           wait indefinitely for device\n"
"  -d --device=<device>  specify the serial device to use\n"
"  -P --position         request position data\n"
"  -E --euler            request euler angle data\n"
"  -T --timestamp        request timestamp data\n"
"  -o --output=[path]    write data to stdout, or to a file\n"
"                        if path is specified\n"
"  -H --hex              write float values as hexidecimal\n"
#ifdef HAVE_LIBLO
"  -s --send=<url>       provide a URL for OSC destination\n"
"                        this URL must be liblo-compatible,\n"
"                        e.g., osc.udp://localhost:9999\n"
"                        this option is required to enable\n"
"                        the Open Sound Control interface\n"
"  -l --listen=<port>    port on which to listen for OSC messages\n"
#endif
#ifdef HAVE_LIBMAPPER
"  -m --mapper           enable ad-hoc mapping with libmapper\n"
#endif
"  -p --poll=[period]    poll instead of requesting continuous data\n"
"                        optional period is in milliseconds, or as\n"
"                        fast as possible if unspecified.\n"
"     --reset            reset the device before starting acquisition\n"
"                        (takes 10 seconds)\n"
"  -V --version          print the version string and exit\n"
"  -h --help             show this help\n"
                   , argv[0]);
            exit(c!='h');
            break;
        }
    }

    int slp = 0;

    // sanity check: ensure user requested something
    if (!(euler_flag || position_flag || timestamp_flag)) {
        printf("[plhm] No data requested.  Try option '-h' for help.\n");
        exit(1);
    }

    plhm_t pol;
    memset((void*)&pol, 0, sizeof(plhm_t));

#ifdef HAVE_LIBLO
    // setup OSC server
    lo_server_thread st = 0;
    if (listen_port > 0)
    {
        char str[256];
        sprintf(str, "%d", listen_port);
        st = lo_server_thread_new(str, liblo_error);
        lo_server_thread_add_method(st, "/liberty/start", "si",
                                    start_handler, &pol);
        lo_server_thread_add_method(st, "/liberty/start", "i",
                                    start_handler, &pol);
        lo_server_thread_add_method(st, "/liberty/stop", "",
                                    stop_handler, &pol);
        lo_server_thread_add_method(st, "/liberty/status", "si",
                                    status_handler, &pol);
        lo_server_thread_add_method(st, "/liberty/status", "i",
                                    status_handler, &pol);
        lo_server_thread_start(st);
    }

    if (osc_url != 0) {
        addr = lo_address_new_from_url(osc_url);
        if (!addr) {
            printf("[plhm] Couldn't open OSC address %s\n", osc_url);
            exit(1);
        }
    }
#endif

#ifdef HAVE_LIBMAPPER
    // setup mapper device
    if (!mapper_alias)
        mapper_alias = strdup("polhemus");
    map_dev = mdev_new(mapper_alias, 0, 0);

    // initialize signal pointers
    int i, j;
    for (i=0; i<8; i++) {
        for (j=0; j<3; j++) {
            sig_pos[i][j] = 0;
            sig_eul[i][j] = 0;
        }
    }
#endif

    started = 1;

    signal(SIGINT, ctrlc_handler);

    while (started || daemon_flag || mapper_flag) {

#ifdef HAVE_LIBMAPPER
        mdev_poll(map_dev, 1000);
#else
        sleep(slp);
        slp = 1;
#endif

        // Loop until device is available.
        if (plhm_find_device(device_name)) {
            if (daemon_flag) {
                device_found = 0;
                continue;
            } else {
                printf("[plhm] Could not find device at %s\n", device_name);
                break;
            }
        }
        device_found = 1;

        // Don't open device if nobody is listening
        if (!(started && (addr || outfile || mapper_flag)) && daemon_flag)
            continue;

        if (plhm_open_device(&pol, device_name))
        {
            if (daemon_flag)
                continue;
            else {
                printf("[plhm] Could not open device %s\n", device_name);
                break;
            }
        }

        // stop any incoming continuous data just in case
        // ignore the response
        CHECKBRK("data_request",plhm_data_request(&pol));

        while (!plhm_read_until_timeout(&pol, 500)) {}

        // reset the device if requested (waits 10 seconds)
        if (reset_flag)
            plhm_reset(&pol);

        CHECKBRK("text_mode",plhm_text_mode(&pol));

        // determine tracker type
        CHECKBRK("get_version",plhm_get_version(&pol));
        if (pol.device_type == PLHM_UNKNOWN)
            printf("[plhm] Warning: Device type unknown.\n");

        // check for initialization errors
        CHECKBRK("read_bits",plhm_read_bits(&pol));

        // check what stations are available
        CHECKBRK("get_stations",plhm_get_stations(&pol));

        CHECKBRK("set_hemisphere",plhm_set_hemisphere(&pol));

        CHECKBRK("set_units",plhm_set_units(&pol, PLHM_UNITS_METRIC));

        CHECKBRK("set_rate",plhm_set_rate(&pol, PLHM_RATE_240));

        CHECKBRK("set_data_fields",
                 plhm_set_data_fields(&pol,
                                      (position_flag ? PLHM_DATA_POSITION : 0)
                                      | (euler_flag ? PLHM_DATA_EULER : 0)
                                      | (timestamp_flag ? PLHM_DATA_TIMESTAMP : 0)));

        gettimeofday(&temp, NULL);
        starttime = (temp.tv_sec * 1000.0) + (temp.tv_usec / 1000.0);

        CHECKBRK("binary_mode",plhm_binary_mode(&pol));

        if (!poll_period)
            CHECKBRK("data_request_continuous",plhm_data_request_continuous(&pol));

        /* loop getting data until stop is requested or error occurs */
        while (started && !read_stations_and_send(&pol,poll_period!=0)) {
            if (poll_period > 0)
                usleep(poll_period);
        }

        // stop any incoming continuous data
        CHECKBRK("data_request",plhm_data_request(&pol));

        plhm_read_until_timeout(&pol, 500);
        plhm_read_until_timeout(&pol, 500);
        plhm_read_until_timeout(&pol, 500);

        CHECKBRK("text_mode",plhm_text_mode(&pol));

        plhm_close_device(&pol);

        if (!daemon_flag)
            break;
    }

    plhm_close_device(&pol);

#ifdef HAVE_LIBLO
    if (st)
        lo_server_thread_free(st);
    if (addr)
        lo_address_free(addr);
#endif
#ifdef HAVE_LIBMAPPER
    if (map_dev) {
        mdev_free(map_dev);
    }
#endif
    if (outfile && (outfile != stdout))
        fclose(outfile);

    return 0;
}



/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */

int timeval_subtract (struct timeval *result,
                      const struct timeval *x,
                      const struct timeval *y)
{
    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

void log_float(float f)
{
    multiptr p;
    p.f = &f;
    if (hex_flag) {
        LOG(", 0x%02x%02x%02x%02x",
            p.uc[0] & 0xFF,
            p.uc[1] & 0xFF,
            p.uc[2] & 0xFF,
            p.uc[3] & 0xFF);
    } else
        LOG(", %.4f", f);
}

int read_stations_and_send(plhm_t *pol, int poll)
{
    struct timeval now, diff;

    static int c=0;
    if (c++ > 30) {
        gettimeofday(&now, NULL);
        timeval_subtract(&diff, &now, &prev);
        prev.tv_sec = now.tv_sec;
        prev.tv_usec = now.tv_usec;

        fprintf(stderr, "Update frequency: %0.2f Hz           \r",
                30.0 / (diff.tv_usec/1000000.0 + diff.tv_sec));
        c=0;
    }

    if (poll)
        plhm_data_request(pol);

    plhm_record_t rec;
    int s;

    for (s = 0; s < pol->stations; s++)
    {
        if (plhm_read_data_record(pol, &rec)) {
            data_good = 0;
            return 1;
        }
        data_good = 1;

        curtime = ((rec.readtime.tv_sec * 1000.0)
                   + (rec.readtime.tv_usec / 1000.0));

        LOG("%d", rec.station);

        if (rec.fields & PLHM_DATA_POSITION)
        {
            log_float(rec.position[0]);
            log_float(rec.position[1]);
            log_float(rec.position[2]);
        }

        if (rec.fields & PLHM_DATA_EULER)
        {
            log_float(rec.euler[0]);
            log_float(rec.euler[1]);
            log_float(rec.euler[2]);
        }

        if (rec.fields & PLHM_DATA_TIMESTAMP)
            LOG(", %u", rec.timestamp);

        LOG(", %f\n", curtime);

        if (!addr)
            continue;

#ifdef HAVE_LIBMAPPER
        // check if signals exist
        if (!sig_pos[rec.station][0]) {
            char str[30];
            snprintf(str, 30, "/marker.%d/x", rec.station);
            sig_pos[rec.station][0] = mdev_add_output(map_dev, str, 1,
                                                      'f', "cm", 0, 0);
            snprintf(str, 30, "/marker.%d/y", rec.station);
            sig_pos[rec.station][1] = mdev_add_output(map_dev, str, 1,
                                                      'f', "cm", 0, 0);
            snprintf(str, 30, "/marker.%d/z", rec.station);
            sig_pos[rec.station][2] = mdev_add_output(map_dev, str, 1,
                                                      'f', "cm", 0, 0);
            snprintf(str, 30, "/marker.%d/azimuth", rec.station);
            sig_eul[rec.station][0] = mdev_add_output(map_dev, str, 1,
                                                      'f', 0, 0, 0);
            snprintf(str, 30, "/marker.%d/elevation", rec.station);
            sig_eul[rec.station][1] = mdev_add_output(map_dev, str, 1,
                                                      'f', 0, 0, 0);
            snprintf(str, 30, "/marker.%d/roll", rec.station);
            sig_eul[rec.station][2] = mdev_add_output(map_dev, str, 1,
                                                      'f', 0, 0, 0);
        }
        // calculate timestamp
        // TODO: should convert Polhemus framecount or timestamp into NTP stamp
        mdev_timetag_now(map_dev, &timetag);
        mdev_start_queue(map_dev, timetag);
        /*if (rec.fields & PLHM_DATA_TIMESTAMP)
        {
            sprintf(path, "/liberty/marker/%d/timestamp", rec.station);
            lo_send(addr, path, "i", rec.timestamp);
        }*/
        if (rec.fields & PLHM_DATA_POSITION)
        {
            msig_update(sig_pos[rec.station][0], &rec.position[0], 1, timetag);
            msig_update(sig_pos[rec.station][1], &rec.position[1], 1, timetag);
            msig_update(sig_pos[rec.station][2], &rec.position[2], 1, timetag);
        }
        if (rec.fields & PLHM_DATA_EULER)
        {
            msig_update(sig_eul[rec.station][0], &rec.euler[0], 1, timetag);
            msig_update(sig_eul[rec.station][1], &rec.euler[1], 1, timetag);
            msig_update(sig_eul[rec.station][2], &rec.euler[2], 1, timetag);
        }
#endif // HAVE_LIBMAPPER

#ifdef HAVE_LIBLO
        char path[30];
        if (rec.fields & PLHM_DATA_POSITION)
        {
            sprintf(path, "/liberty/marker/%d/x", rec.station);
            lo_send(addr, path, "f", rec.position[0]);

            sprintf(path, "/liberty/marker/%d/y", rec.station);
            lo_send(addr, path, "f", rec.position[1]);

            sprintf(path, "/liberty/marker/%d/z", rec.station);
            lo_send(addr, path, "f", rec.position[2]);
        }

        if (rec.fields & PLHM_DATA_EULER)
        {
            sprintf(path, "/liberty/marker/%d/azimuth", rec.station);
            lo_send(addr, path, "f", rec.euler[0]);

            sprintf(path, "/liberty/marker/%d/elevation", rec.station);
            lo_send(addr, path, "f", rec.euler[1]);

            sprintf(path, "/liberty/marker/%d/roll", rec.station);
            lo_send(addr, path, "f", rec.euler[2]);
        }

        if (rec.fields & PLHM_DATA_TIMESTAMP)
        {
            sprintf(path, "/liberty/marker/%d/timestamp", rec.station);
            lo_send(addr, path, "i", rec.timestamp);
        }

        sprintf(path, "/liberty/marker/%d/readtime", rec.station);
        lo_send(addr, path, "f", curtime);
#endif // HAVE_LIBLO
    }

    return 0;
}

#ifdef HAVE_LIBLO
void liblo_error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stdout);
}

int start_handler(const char *path, const char *types, lo_arg **argv, int argc,
                  void *data, void *user_data)
{
    int port;
    const char *hostname;
    started = 0;

    if (argc == 1) {
        hostname = lo_address_get_hostname(lo_message_get_source(data));

        // SS: this is obviously not the right way to handle IPV6 hosts, but
        //     for a reason I don't understand simply passing hostname to
        //     gethostbyname() fails with error code Success.  This is a (bad)
        //     work-around for now.
        if (hostname[0]==':' && hostname[1]==':') {
            hostname += 7;
        }
        port = argv[0]->i;
    }
    else {
        hostname = &argv[0]->s;
        port = argv[1]->i;
    }

    char url[256];
    sprintf(url, "osc.udp://%s:%d", hostname, port);
    lo_address a = lo_address_new_from_url(url);
    if (a && addr) lo_address_free(addr);
    addr = a;
    printf("starting... %s\n", url);

    started = 1;
    return 0;
}

int stop_handler(const char *path, const char *types, lo_arg **argv, int argc,
                 void *data, void *user_data)
{
    printf("stopping..\n");
    started = 0;
    return 0;
}

void send_status(plhm_t *pol, const char* hostname, int port)
{
    char port_s[30];
    char *status;

    if (started) {
        status = "sending";
        if (!device_found)
            status = "device_not_found";
        else if (!pol->device_open)
            status = "device_found_but_not_open";
        else if (!data_good)
            status = "data_stream_error";
    }
    else {
        status = "waiting";
    }

    sprintf(port_s, "%d", port);
    lo_address t = lo_address_new(hostname, port_s);
    if (t) {
        lo_send(t, "/liberty/status","s", status);
        lo_address_free(t);
    }
}

int status_handler(const char *path, const char *types, lo_arg **argv, int argc,
                   void *data, void *user_data)
{
    plhm_t *pol = (plhm_t*)user_data;

    int port;
    const char *hostname;
    if (argc == 1) {
        hostname = lo_address_get_hostname(lo_message_get_source(data));
        port = argv[0]->i;
    }
    else {
        hostname = &argv[0]->s;
        port = argv[1]->i;
    }

    send_status(pol, hostname, port);

    return 0;
}
#endif // HAVE_LIBLO

#ifdef HAVE_LIBMAPPER
// TODO: declare mapper handler for input signal /pollrate
#endif // HAVE_LIBMAPPER
