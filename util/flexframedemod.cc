#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <liquid/liquid.h>

int callback(unsigned char *  header_,
             int              header_valid_,
             int              header_test_,
             unsigned char *  payload_,
             unsigned int     payload_len_,
             int              payload_valid_,
             framesyncstats_s stats_,
             void *           userdata_)
{
    if (header_test_)
        return 1;

    if (!header_valid_)
        printf("INVALID HEADER: ");
    else if (!payload_valid_)
        printf("INVALID PAYLOAD: ");
    else
        printf("Valid packet: ");

    printf("rssi=%7.2fdB evm=%7.2fdB\n", stats_.rssi, stats_.evm);

    return 0;
}

int main(int argc, char** argv)
{
    flexframesync fs;
    struct timespec t_start, t_end;

    fs = flexframesync_create(callback, NULL);

    for (int i = 1; i < argc; ++i) {
        FILE *fp;
        long sz;

        if ((fp = fopen(argv[i], "rb")) == NULL) {
            perror("fopen");
            exit(1);
        }

        // Get file's size
        fseek(fp, 0L, SEEK_END);
        sz = ftell(fp);
        rewind(fp);

        // Read file into a buffer
        void *buf = malloc(sz);

        assert(buf != NULL);

        ssize_t count = fread(buf, 1, sz, fp);

        // Run data through the synchronizer
        flexframesync_reset(fs);

        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_start);

        flexframesync_execute(fs,
                              (liquid_float_complex*) buf,
                              count / sizeof(liquid_float_complex));

        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_end);

        // Print elapsed time
        double t = (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_nsec - t_start.tv_nsec)/1e9;
        printf("Elapsed time: %lf (sec)\n", t);

        // Free the buffer and close the file
        free(buf);
        fclose(fp);
    }

    flexframesync_destroy(fs);

    return 0;
}
