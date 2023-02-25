/* simple C program to run many gain changes one after another
 * Franco Venturi - Fri Feb 24 10:14:55 AM EST 2023
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

static const int UpdateTimeout = 10000;    /* wait up to 10k x usleep() for updates */
static const int ProgressEveryNGainChanges = 1000;   /* show progress every N gain changes */


#define UNUSED(x) (void)(x)

typedef struct {
    unsigned long long total_samples;
    unsigned int next_sample_num;
    int gain_reduction_changed;
    int verbose;
} RXContext;

static void usage(const char* progname);
static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext);


int main(int argc, char *argv[])
{
    const char *serial_number = NULL;
    double rsp_sample_rate = 2e6;
    int decimation = 1;
    sdrplay_api_If_kHzT if_frequency = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT if_bandwidth = sdrplay_api_BW_0_200;
    int default_gRdBs[] = { 40 };
    int ngRdBs = sizeof(default_gRdBs) / sizeof(default_gRdBs[0]);
    int *gRdBs = default_gRdBs;
    int default_LNAstates[] = { 0 };
    int nLNAstates = sizeof(default_LNAstates) / sizeof(default_LNAstates[0]);
    int *LNAstates = default_LNAstates;
    int DCenable = 1;
    int IQenable = 1;
    int dcCal = 3;
    int speedUp = 0;
    int trackTime = 1;
    int refreshRateTime = 2048;
    double frequency = 100e6;
    unsigned int num_gain_changes = UINT_MAX;
    useconds_t wait_time = 0;
    int debug_enable = 0;
    int verbose = 0;

    int c;
    while ((c = getopt(argc, argv, "s:r:d:i:b:g:l:DIy:f:n:w:LVh")) != -1) {
        char *p;
        int n;
        int consumed;
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
                for (n = 1, p = optarg; *p; p++) {
                    if (*p == ',')
                        n++;
                }
                ngRdBs = n;
                gRdBs = (int *) malloc(ngRdBs * sizeof(int));
                consumed = 0;
                for (n = 0, p = optarg; *p && n < ngRdBs; n++, p += consumed+1) {
                    if (sscanf(p, "%d%n", &gRdBs[n], &consumed) != 1) {
                        fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                        exit(1);
                    }
                }
                if (n < ngRdBs) {
                    fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'l':
                for (n = 1, p = optarg; *p; p++) {
                    if (*p == ',')
                        n++;
                }
                nLNAstates = n;
                LNAstates = (int *) malloc(nLNAstates * sizeof(int));
                consumed = 0;
                for (n = 0, p = optarg; *p && n < nLNAstates; n++, p += consumed+1) {
                    if (sscanf(p, "%d%n", &LNAstates[n], &consumed) != 1) {
                        fprintf(stderr, "invalid LNA state: %s\n", optarg);
                        exit(1);
                    }
                }
                if (n < nLNAstates) {
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
            case 'n':
                if (sscanf(optarg, "%u", &num_gain_changes) != 1) {
                    fprintf(stderr, "invalid number of gain changes: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'w':
                if (sscanf(optarg, "%u", &wait_time) != 1) {
                    fprintf(stderr, "invalid wait time: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'L':
                debug_enable = 1;
                break;
            case 'V':
                verbose = 1;
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
    rx_channel_params->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;
    rx_channel_params->tunerParams.gain.gRdB = gRdBs[0];
    rx_channel_params->tunerParams.gain.LNAstate = LNAstates[0];
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
    fprintf(stdout, "SR=%.0lf LO=%.0lf BW=%d If=%d Dec=%d IFgain=%d LNAstate=%d\n", device_params->devParams->fsFreq.fsHz, rx_channel_params->tunerParams.rfFreq.rfHz, rx_channel_params->tunerParams.bwType, rx_channel_params->tunerParams.ifType, rx_channel_params->ctrlParams.decimation.decimationFactor, rx_channel_params->tunerParams.gain.gRdB, rx_channel_params->tunerParams.gain.LNAstate);
    fprintf(stdout, "DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channel_params->ctrlParams.dcOffset.DCenable), (int)(rx_channel_params->ctrlParams.dcOffset.IQenable), (int)(rx_channel_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channel_params->tunerParams.dcOffsetTuner.speedUp), rx_channel_params->tunerParams.dcOffsetTuner.trackTime, rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime);

    int init_ok = 1;
    if (device.tuner != sdrplay_api_Tuner_A) {
        fprintf(stderr, "unexpected change - tuner: 0x%02x -> 0x%02x\n", sdrplay_api_Tuner_A, device.tuner);
        init_ok = 0;
    }
    if (device.hwVer == SDRPLAY_RSPduo_ID) {
        if (device.rspDuoMode != sdrplay_api_RspDuoMode_Single_Tuner) {
            fprintf(stderr, "unexpected change - rspDuoMode: 0x%02x -> 0x%02x\n", sdrplay_api_RspDuoMode_Single_Tuner, device.rspDuoMode);
            init_ok = 0;
        }
    } else {
        if (device.rspDuoMode != sdrplay_api_RspDuoMode_Unknown) {
            fprintf(stderr, "unexpected change - rspDuoMode: 0x%02x -> 0x%02x\n", sdrplay_api_RspDuoMode_Unknown, device.rspDuoMode);
            init_ok = 0;
        }
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
    if (rx_channel_params->tunerParams.gain.gRdB != gRdBs[0]) {
        fprintf(stderr, "unexpected change - gain.gRdB: %d -> %d\n", gRdBs[0], rx_channel_params->tunerParams.gain.gRdB);
        init_ok = 0;
    }
    if (rx_channel_params->tunerParams.gain.LNAstate != LNAstates[0]) {
        fprintf(stderr, "unexpected change - gain.LNAstate: %d -> %d\n", LNAstates[0], rx_channel_params->tunerParams.gain.LNAstate);
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
    RXContext rx_context = {
        .total_samples = 0,
        .next_sample_num = 0xffffffff,
        .gain_reduction_changed = 0,
        .verbose = verbose,
    };

    sdrplay_api_CallbackFnsT callbackFns = {
        rx_callback,
        NULL,
        event_callback
    };

    err = sdrplay_api_Init(device.dev, &callbackFns, (void *)&rx_context);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    fprintf(stderr, "\n");
    time_t now = time(NULL);
    fprintf(stderr, "%.24s - changing gains - wait time=%dus\n", ctime(&now), wait_time);
    for (unsigned int ngc = 1; ngc < num_gain_changes; ngc++) {
        usleep(wait_time);
        rx_channel_params->tunerParams.gain.gRdB = gRdBs[ngc % ngRdBs];
        rx_channel_params->tunerParams.gain.LNAstate = LNAstates[ngc % nLNAstates];
        rx_context.gain_reduction_changed = 0;
        err = sdrplay_api_Update(device.dev, device.tuner,
                                 sdrplay_api_Update_Tuner_Gr,
                                 sdrplay_api_Update_Ext1_None);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_Update(Tuner_Gr) failed: %s - ngc=%d\n", sdrplay_api_GetErrorString(err), ngc);
            sdrplay_api_ReleaseDevice(&device);
            sdrplay_api_Close();
            exit(1);
        }
        if (ngc % ProgressEveryNGainChanges == 0) {
            time_t now = time(NULL);
            fprintf(stderr, "%.24s - gain change #%d\n", ctime(&now), ngc);
        }

        /* wait for the gain change */
        int elapsed;
        int prev_gain_reduction_changed = -1;
        for (elapsed = 0; elapsed < UpdateTimeout; elapsed++) {
            int curr_gain_reduction_changed = rx_context.gain_reduction_changed;
            if (prev_gain_reduction_changed == 0 && curr_gain_reduction_changed != 0) {
                break;
            }
            prev_gain_reduction_changed = curr_gain_reduction_changed;
            usleep(1);
        }
        if (verbose) {
            fprintf(stderr, "> ngc=%u elapsed=%d\n", ngc, elapsed);
        }
        if (rx_context.gain_reduction_changed == 0) {
            fprintf(stderr, "gain change update timeout\n");
        }
    }

    err = sdrplay_api_Uninit(device.dev);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
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
    fprintf(stderr, "    -g <IF gain reduction>[,<IF gain reduction>[,...]]\n");
    fprintf(stderr, "    -l <LNA state>[,<LNA state>[,...]]\n");
    fprintf(stderr, "    -D disable post tuner DC offset compensation (default: enabled)\n");
    fprintf(stderr, "    -I disable post tuner I/Q balance compensation (default: enabled)\n");
    fprintf(stderr, "    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)\n");
    fprintf(stderr, "    -f <center frequency>\n");
    fprintf(stderr, "    -w <wait time between gain changes (in microseconds)>\n");
    fprintf(stderr, "    -L enable SDRplay API debug log level (default: disabled)\n");
    fprintf(stderr, "    -V verbose (shows elapsed usleep for each gain change) (default: disabled)\n");
    fprintf(stderr, "    -h show usage\n");
}

static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    UNUSED(xi);
    UNUSED(xq);

    RXContext *rx_context = (RXContext *)cbContext;
    if (rx_context->verbose && params->grChanged != 0) {
        fprintf(stderr, "> params->grChanged=%d\n", params->grChanged);
    }
    rx_context->gain_reduction_changed |= params->grChanged;

    /* check for reset */
    if (reset != 0) {
        fprintf(stderr, "reset=%d\n", reset);
    }

    /* check for jumps in sample sequence numbers */
    if (rx_context->next_sample_num != 0xffffffff && params->firstSampleNum != rx_context->next_sample_num) {
        fprintf(stderr, "jump in sample sequence number - from %u to %u\n", rx_context->next_sample_num, params->firstSampleNum);
    }
    rx_context->next_sample_num = params->firstSampleNum + numSamples;
    rx_context->total_samples += numSamples;
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
