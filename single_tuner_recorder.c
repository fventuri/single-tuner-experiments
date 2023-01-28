/* simple C program to record to file the I/Q stream from a RSP
 * in single tuner mode
 * Franco Venturi - Fri Jan 27 08:35:55 AM EST 2023
 */

/*
 * Copyright 2023 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include <sdrplay_api.h>

#define UNUSED(x) (void)(x)
#define MAX_PATH_SIZE 1024

typedef struct {
    struct timeval earliest_callback;
    struct timeval latest_callback;
    unsigned long long total_samples;
    unsigned int next_sample_num;
    int output_fd;
    short imin, imax;
    short qmin, qmax;
} RXContextRecord;

typedef struct {
    struct timespec prev_time;
    long callback_count;
    long diff_threshold;
} RXContextMeasureTimeDiff;

static void usage(const char* progname);
static void rx_callback_record(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void rx_callback_measure_time_diff(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext);


int main(int argc, char *argv[])
{
    const char *serial_number = NULL;
    double rsp_sample_rate = 0.0;
    int decimation = 1;
    sdrplay_api_If_kHzT if_frequency = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT if_bandwidth = sdrplay_api_BW_0_200;
    sdrplay_api_AgcControlT agc = sdrplay_api_AGC_DISABLE;
    int gRdB = 40;
    int LNAstate = 0;
    int DCenable = 1;
    int IQenable = 1;
    int dcCal = 3;
    int speedUp = 0;
    int trackTime = 1;
    int refreshRateTime = 2048;
    double frequency = 100e6;
    int streaming_time = 10;  /* streaming time in seconds */
    const char *output_file = NULL;
    int debug_enable = 0;
    int measure_time_diff_only = 0;

    int c;
    while ((c = getopt(argc, argv, "s:r:d:i:b:g:l:DIy:f:x:o:LTh")) != -1) {
        switch (c) {
            case 's':
                serial_number = optarg;
                break;
            case 'r':
                if (sscanf(optarg, "%lg", &rsp_sample_rate) != 1) {
                    fprintf(stderr, "invalid RSP sample rate: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'd':
                if (sscanf(optarg, "%d", &decimation) != 1) {
                    fprintf(stderr, "invalid decimation: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'i':
                if (sscanf(optarg, "%d", (int *)(&if_frequency)) != 1) {
                    fprintf(stderr, "invalid IF frequency: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'b':
                if (sscanf(optarg, "%d", (int *)(&if_bandwidth)) != 1) {
                    fprintf(stderr, "invalid IF bandwidth: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'g':
                if (strcmp(optarg, "AGC") == 0) {
                    agc = sdrplay_api_AGC_50HZ;
                } else if (sscanf(optarg, "%d", &gRdB) == 1) {
                    agc = sdrplay_api_AGC_DISABLE;
                } else {
                    fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'l':
                if (sscanf(optarg, "%d", &LNAstate) != 1) {
                    fprintf(stderr, "invalid LNA state: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'D':
                DCenable = 0;
                break;
            case 'I':
                IQenable = 0;
                break;
            case 'y':
                if (sscanf(optarg, "%d,%d,%d,%d", &dcCal, &speedUp, &trackTime, &refreshRateTime) != 4) {
                    fprintf(stderr, "invalid tuner DC offset compensation parameters: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'f':
                if (sscanf(optarg, "%lg", &frequency) != 1) {
                    fprintf(stderr, "invalid frequency: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'x':
                if (sscanf(optarg, "%d", &streaming_time) != 1) {
                    fprintf(stderr, "invalid streaming time: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'L':
                debug_enable = 1;
                break;
            case 'T':
                measure_time_diff_only = 1;
                break;

            // help
            case 'h':
                usage(argv[0]);
                exit(0);
            case '?':
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    /* open SDRplay API and check version */
    sdrplay_api_ErrT err;
    err = sdrplay_api_Open();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Open() failed: %s\n", sdrplay_api_GetErrorString(err));
        exit(1);
    }
    float ver;
    err = sdrplay_api_ApiVersion(&ver);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_ApiVersion() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }       
    if (ver != SDRPLAY_API_VERSION) {
        fprintf(stderr, "SDRplay API version mismatch - expected=%.2f found=%.2f\n", SDRPLAY_API_VERSION, ver);
        sdrplay_api_Close();
        exit(1);
    }

    /* select device */
    err = sdrplay_api_LockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_LockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }
#ifdef SDRPLAY_MAX_DEVICES
#undef SDRPLAY_MAX_DEVICES
#endif
#define SDRPLAY_MAX_DEVICES 4
    unsigned int ndevices = SDRPLAY_MAX_DEVICES;
    sdrplay_api_DeviceT devices[SDRPLAY_MAX_DEVICES];
    err = sdrplay_api_GetDevices(devices, &ndevices, ndevices);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDevices() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    int device_index = -1;
    for (unsigned int i = 0; i < ndevices; i++) {
        if (serial_number == NULL || strcmp(devices[i].SerNo, serial_number) == 0) {
            device_index = i;
            break;
        }
    }
    if (device_index == -1) {
        fprintf(stderr, "SDRplay RSP not found or not available\n");
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    sdrplay_api_DeviceT device = devices[device_index];

    /* for RSPduo make sure single tuner mode is available */
    if (device.hwVer == SDRPLAY_RSPduo_ID) {
        if ((device.rspDuoMode & sdrplay_api_RspDuoMode_Single_Tuner) != sdrplay_api_RspDuoMode_Single_Tuner) {
            fprintf(stderr, "SDRplay RSPduo single tuner mode not available\n");
            sdrplay_api_UnlockDeviceApi();
            sdrplay_api_Close();
            exit(1);
        } else {
            device.rspDuoMode = sdrplay_api_RspDuoMode_Single_Tuner;
            device.tuner = sdrplay_api_Tuner_A;
            device.rspDuoSampleFreq = 0;
        }
    }

    err = sdrplay_api_SelectDevice(&device);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_SelectDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }

    err = sdrplay_api_UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    if (debug_enable) {
        err = sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Verbose);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_DebugEnable() failed: %s\n", sdrplay_api_GetErrorString(err));
            sdrplay_api_ReleaseDevice(&device);
            sdrplay_api_Close();
            exit(1);
        }
    }

    // select device settings
    sdrplay_api_DeviceParamsT *device_params;
    err = sdrplay_api_GetDeviceParams(device.dev, &device_params);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDeviceParams() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }
    sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA ;
    device_params->devParams->fsFreq.fsHz = rsp_sample_rate;
    rx_channel_params->ctrlParams.decimation.enable = decimation > 1;
    rx_channel_params->ctrlParams.decimation.decimationFactor = decimation;
    rx_channel_params->tunerParams.ifType = if_frequency;
    rx_channel_params->tunerParams.bwType = if_bandwidth;
    rx_channel_params->ctrlParams.agc.enable = agc;
    if (agc == sdrplay_api_AGC_DISABLE) {
        rx_channel_params->tunerParams.gain.gRdB = gRdB;
    }
    rx_channel_params->tunerParams.gain.LNAstate = LNAstate;
    rx_channel_params->ctrlParams.dcOffset.DCenable = DCenable;
    rx_channel_params->ctrlParams.dcOffset.IQenable = IQenable;
    rx_channel_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
    rx_channel_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
    rx_channel_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
    rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
    rx_channel_params->tunerParams.rfFreq.rfHz = frequency;

    /* quick check */
    sdrplay_api_CallbackFnsT callbackNullFns = { NULL, NULL, NULL };
    err = sdrplay_api_Init(device.dev, &callbackNullFns, NULL);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* print settings */
    fprintf(stdout, "SerNo=%s hwVer=%d tuner=0x%02x\n", device.SerNo, device.hwVer, device.tuner);
    fprintf(stdout, "LO=%.0lf BW=%d If=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channel_params->tunerParams.rfFreq.rfHz, rx_channel_params->tunerParams.bwType, rx_channel_params->tunerParams.ifType, rx_channel_params->ctrlParams.decimation.decimationFactor, rx_channel_params->ctrlParams.agc.enable, rx_channel_params->tunerParams.gain.gRdB, rx_channel_params->tunerParams.gain.LNAstate);
    fprintf(stdout, "DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channel_params->ctrlParams.dcOffset.DCenable), (int)(rx_channel_params->ctrlParams.dcOffset.IQenable), (int)(rx_channel_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channel_params->tunerParams.dcOffsetTuner.speedUp), rx_channel_params->tunerParams.dcOffsetTuner.trackTime, rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime);

    int init_ok = 1;
    if (device.tuner != sdrplay_api_Tuner_A) {
        fprintf(stderr, "unexpected change - tuner: 0x%02x -> 0x%02x\n", sdrplay_api_Tuner_A, device.tuner);
        init_ok = 0;
    }
    if (device.rspDuoMode != sdrplay_api_RspDuoMode_Single_Tuner) {
        fprintf(stderr, "unexpected change - rspDuoMode: 0x%02x -> 0x%02x\n", sdrplay_api_RspDuoMode_Single_Tuner, device.rspDuoMode);
        init_ok = 0;
    }
    if (device.rspDuoSampleFreq != 0) {
        fprintf(stderr, "unexpected change - rspDuoSampleFreq: %.0lf -> %.0lf\n", 0.0, device.rspDuoSampleFreq);
        init_ok = 0;
    }
    if (device_params->devParams->fsFreq.fsHz != rsp_sample_rate) {
        fprintf(stderr, "unexpected change - fsHz: %.0lf -> %.0lf\n", rsp_sample_rate, device_params->devParams->fsFreq.fsHz);
        init_ok = 0;
    }
    if (rx_channel_params->ctrlParams.decimation.enable != (decimation > 1)) {
        fprintf(stderr, "unexpected change - decimation.enable: %d -> %d\n", decimation > 1, rx_channel_params->ctrlParams.decimation.enable);
        init_ok = 0;
    }
    if (rx_channel_params->ctrlParams.decimation.decimationFactor != decimation) {
        fprintf(stderr, "unexpected change - decimation.decimationFactor: %d -> %d\n", decimation, rx_channel_params->ctrlParams.decimation.decimationFactor);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.ifType != if_frequency) {
        fprintf(stderr, "unexpected change - ifType: %d -> %d\n", if_frequency, rx_channel_params->tunerParams.ifType);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.bwType != if_bandwidth) {
        fprintf(stderr, "unexpected change - bwType: %d -> %d\n", if_bandwidth, rx_channel_params->tunerParams.bwType);
        init_ok = 0;
    }
    if (rx_channel_params->ctrlParams.agc.enable != agc) {
        fprintf(stderr, "unexpected change - agc.enable: %d -> %d\n", agc, rx_channel_params->ctrlParams.agc.enable);
        init_ok = 0;
    }
    if (agc == sdrplay_api_AGC_DISABLE) {
        if (rx_channel_params->tunerParams.gain.gRdB != gRdB) {
            fprintf(stderr, "unexpected change - gain.gRdB: %d -> %d\n", gRdB, rx_channel_params->tunerParams.gain.gRdB);
            init_ok = 0;
        }
    }
    if (rx_channel_params->tunerParams.gain.LNAstate != LNAstate) {
        fprintf(stderr, "unexpected change - gain.LNAstate: %d -> %d\n", LNAstate, rx_channel_params->tunerParams.gain.LNAstate);
        init_ok = 0;
    }
    if (rx_channel_params->ctrlParams.dcOffset.DCenable != DCenable) {
        fprintf(stderr, "unexpected change - dcOffset.DCenable: %d -> %d\n", DCenable, rx_channel_params->ctrlParams.dcOffset.DCenable);
        init_ok = 0;
    }
    if (rx_channel_params->ctrlParams.dcOffset.IQenable != IQenable) {
        fprintf(stderr, "unexpected change - dcOffset.IQenable: %d -> %d\n", IQenable, rx_channel_params->ctrlParams.dcOffset.IQenable);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.dcOffsetTuner.dcCal != dcCal) {
        fprintf(stderr, "unexpected change - dcOffsetTuner.dcCal: %d -> %d\n", dcCal, rx_channel_params->tunerParams.dcOffsetTuner.dcCal);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.dcOffsetTuner.speedUp != speedUp) {
        fprintf(stderr, "unexpected change - dcOffsetTuner.speedUp: %d -> %d\n", speedUp, rx_channel_params->tunerParams.dcOffsetTuner.speedUp);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.dcOffsetTuner.trackTime != trackTime) {
        fprintf(stderr, "unexpected change - dcOffsetTuner.trackTime: %d -> %d\n", trackTime, rx_channel_params->tunerParams.dcOffsetTuner.trackTime);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime != refreshRateTime) {
        fprintf(stderr, "unexpected change - dcOffsetTuner.refreshRateTime: %d -> %d\n", refreshRateTime, rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.rfFreq.rfHz != frequency) {
        fprintf(stderr, "unexpected change - rfHz: %.0lf -> %.0lf\n", frequency, rx_channel_params->tunerParams.rfFreq.rfHz);
        init_ok = 0;
    }

    if (!init_ok) {
        sdrplay_api_Uninit(device.dev);
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    err = sdrplay_api_Uninit(device.dev);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* now for the real thing */
    RXContextRecord rx_context_record = {
        .earliest_callback = {0, 0},
        .latest_callback = {0, 0},
        .total_samples = 0,
        .next_sample_num = 0xffffffff,
        .output_fd = -1,
        .imin = SHRT_MAX,
        .imax = SHRT_MIN,
        .qmin = SHRT_MAX,
        .qmax = SHRT_MIN,
    };

    RXContextMeasureTimeDiff rx_context_measure_time_diff = {
        .prev_time = {0, 0},
        .callback_count = 0,
        .diff_threshold = 5000000,   /* 5ms */
    };

    sdrplay_api_CallbackFnsT callbackFns = {
        !measure_time_diff_only ? rx_callback_record : rx_callback_measure_time_diff,
        NULL,
        event_callback
    };

    if (!measure_time_diff_only) {
        if (output_file != NULL) {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fprintf(stderr, "open(%s) for writing failed: %s\n", output_file, strerror(errno));
                sdrplay_api_ReleaseDevice(&device);
                sdrplay_api_Close();
                exit(1);
            }
            rx_context_record.output_fd = fd;
        }
    }

    err = sdrplay_api_Init(device.dev, &callbackFns, !measure_time_diff_only ? (void *)&rx_context_record : (void *)&rx_context_measure_time_diff);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    fprintf(stderr, "streaming for %d seconds\n", streaming_time);
    sleep(streaming_time);

    err = sdrplay_api_Uninit(device.dev);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* wait one second after sdrplay_api_Uninit() before closing the files */
    sleep(1);

    if (!measure_time_diff_only) {
        if (rx_context_record.output_fd > 0) {
            if (close(rx_context_record.output_fd) == -1) {
                fprintf(stderr, "close(%d) failed: %s\n", rx_context_record.output_fd, strerror(errno));
            }
        }
    }

    /* estimate actual sample rate */
    if (!measure_time_diff_only) {
        double elapsed_sec = (rx_context_record.latest_callback.tv_sec - rx_context_record.earliest_callback.tv_sec) + 1e-6 * (rx_context_record.latest_callback.tv_usec - rx_context_record.earliest_callback.tv_usec);
        double actual_sample_rate = (double)(rx_context_record.total_samples) / elapsed_sec;
        int rounded_sample_rate_kHz = (int)(actual_sample_rate / 1000.0 + 0.5);
        fprintf(stderr, "total_samples=%llu actual_sample_rate=%.0lf rounded_sample_rate_kHz=%d\n", rx_context_record.total_samples, actual_sample_rate, rounded_sample_rate_kHz);
        fprintf(stderr, "I_range=[%hd,%hd] Q_range=[%hd,%hd]\n", rx_context_record.imin, rx_context_record.imax, rx_context_record.qmin, rx_context_record.qmax);
        const char *samplerate_string = "SAMPLERATE";
        if (output_file != NULL && strstr(output_file, samplerate_string)) {
            char *p = strstr(output_file, samplerate_string);
            int from = p - output_file;
            int to = from + strlen(samplerate_string);
            char new_filename[MAX_PATH_SIZE];
            snprintf(new_filename, MAX_PATH_SIZE, "%.*s%d%s", from, output_file, rounded_sample_rate_kHz, output_file + to);
            if (rename(output_file, new_filename) == -1) {
                fprintf(stderr, "rename(%s, %s) failed: %s\n", output_file, new_filename, strerror(errno));
            }
        }
    }

    err = sdrplay_api_LockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_LockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }
    err = sdrplay_api_ReleaseDevice(&device);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_ReleaseDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    err = sdrplay_api_UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }

    /* all done: close SDRplay API */
    err = sdrplay_api_Close();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Close() failed: %s\n", sdrplay_api_GetErrorString(err));
        exit(1);
    }

    return 0;
}

static void usage(const char* progname)
{
    fprintf(stderr, "usage: %s [options...]\n", progname);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "    -s <serial number>\n");
    fprintf(stderr, "    -r <RSPduo sample rate>\n");
    fprintf(stderr, "    -d <decimation>\n");
    fprintf(stderr, "    -i <IF frequency>\n");
    fprintf(stderr, "    -b <IF bandwidth>\n");
    fprintf(stderr, "    -g <IF gain reduction> (\"AGC\" to enable AGC)\n");
    fprintf(stderr, "    -l <LNA state>\n");
    fprintf(stderr, "    -D disable post tuner DC offset compensation (default: enabled)\n");
    fprintf(stderr, "    -I disable post tuner I/Q balance compensation (default: enabled)\n");
    fprintf(stderr, "    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)\n");
    fprintf(stderr, "    -f <center frequency>\n");
    fprintf(stderr, "    -x <streaming time (s)> (default: 10s)\n");
    fprintf(stderr, "    -o <output file> (''SAMPLERATE' will be replaced by the estimated sample rate in kHz)\n");
    fprintf(stderr, "    -L enable SDRplay API debug log level (default: disabled)\n");
    fprintf(stderr, "    -T measure callback time difference only (no output) (default: disabled)\n");
    fprintf(stderr, "    -h show usage\n");
}

static void rx_callback_record(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    UNUSED(reset);

    RXContextRecord *rxContext = (RXContextRecord *)cbContext;

    /* track callback timestamp */
    gettimeofday(&rxContext->latest_callback, NULL);
    if (rxContext->earliest_callback.tv_sec == 0) {
        rxContext->earliest_callback.tv_sec = rxContext->latest_callback.tv_sec;
        rxContext->earliest_callback.tv_usec = rxContext->latest_callback.tv_usec;
    }
    rxContext->total_samples += numSamples;

    /* check for dropped samples */
    if (rxContext->next_sample_num != 0xffffffff && params->firstSampleNum != rxContext->next_sample_num) {
        unsigned int dropped_samples;
        if (rxContext->next_sample_num < params->firstSampleNum) {
            dropped_samples = params->firstSampleNum - rxContext->next_sample_num;
        } else {
            dropped_samples = UINT_MAX - (params->firstSampleNum - rxContext->next_sample_num) + 1;
        }
        fprintf(stderr, "dropped %d samples\n", dropped_samples);
    }
    rxContext->next_sample_num = params->firstSampleNum + numSamples;

    short imin = SHRT_MAX;
    short imax = SHRT_MIN;
    short qmin = SHRT_MAX;
    short qmax = SHRT_MIN;
    for (unsigned int i = 0; i < numSamples; i++) {
        imin = imin < xi[i] ? imin : xi[i];
        imax = imax > xi[i] ? imax : xi[i];
    }
    for (unsigned int i = 0; i < numSamples; i++) {
        qmin = qmin < xq[i] ? qmin : xq[i];
        qmax = qmax > xq[i] ? qmax : xq[i];
    }
    rxContext->imin = rxContext->imin < imin ? rxContext->imin : imin;
    rxContext->imax = rxContext->imax > imax ? rxContext->imax : imax;
    rxContext->qmin = rxContext->qmin < qmin ? rxContext->qmin : qmin;
    rxContext->qmax = rxContext->qmax > qmax ? rxContext->qmax : qmax;

    /* write samples to output file */
    if (rxContext->output_fd > 0) {
        short samples[4096];
        for (unsigned int i = 0; i < numSamples; i++) {
            samples[2*i] = xi[i];
        }
        for (unsigned int i = 0; i < numSamples; i++) {
            samples[2*i+1] = xq[i];
        }
        size_t count = numSamples * 2 * sizeof(short);
        ssize_t nwritten = write(rxContext->output_fd, samples, count);
        if (nwritten == -1) {
            fprintf(stderr, "write() failed: %s\n", strerror(errno));
        } else if ((size_t)nwritten != count) {
            fprintf(stderr, "incomplete write() - expected: %ld bytes - actual: %ld bytes\n", count, nwritten);
        }
    }
}

static void rx_callback_measure_time_diff(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    UNUSED(xi);
    UNUSED(xq);
    UNUSED(params);
    UNUSED(reset);

    RXContextMeasureTimeDiff *rxContext = (RXContextMeasureTimeDiff *)cbContext;

    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
    if (rxContext->prev_time.tv_sec > 0) {
        long diff = (current_time.tv_sec - rxContext->prev_time.tv_sec) * 1000000000 + (current_time.tv_nsec - rxContext->prev_time.tv_nsec);
        if (diff > rxContext->diff_threshold) {
            fprintf(stderr, "%ld %u %ld\n", rxContext->callback_count, numSamples, diff);
        }
    }
    rxContext->prev_time.tv_sec = current_time.tv_sec;
    rxContext->prev_time.tv_nsec = current_time.tv_nsec;
    rxContext->callback_count++;
}

static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext)
{
    UNUSED(eventId);
    UNUSED(tuner);
    UNUSED(params);
    UNUSED(cbContext);
    /* do nothing for now */
    return;
}
