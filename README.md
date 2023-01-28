# RSP{1,1A,2,duo,dx} single tuner mode experiments

A collection of tools, programs, and scripts to experiment with the SDRplay RSPs in single tuner mode

## single_tuner_recorder

A simple C program that records to file the I/Q stream from an RSP in single tuner mode; sample rate, decimation, IF frequency, IF bandwidth, gains, and center frequency are provided via command line arguments (see below).


To build it run these commands:
```
mkdir build
cd build
cmake ..
make (or ninja)
```


These are the command line options for `single_tuner_recorder`:

    -s <serial number>
    -r <RSP sample rate>
    -d <decimation>
    -i <IF frequency>
    -b <IF bandwidth>
    -g <IF gain reduction> ("AGC" to enable AGC)
    -l <LNA state>
    -D disable post tuner DC offset compensation (default: enabled)
    -I disable post tuner I/Q balance compensation (default: enabled)
    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)
    -f <center frequency>
    -x <streaming time (s)> (default: 10s)
    -o <output file> ('SAMPLERATE' will be replaced by the estimated sample rate in kHz)


Here are some usage examples:

- record local NOAA weather radio on 162.55MHz using a sample rate of 6MHz and IF=1620kHz:
```
./single_tuner_recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000 -o noaa-6M-SAMPLERATE.iq16
```

- record local NOAA weather radio on 162.55MHz using a sample rate of 8MHz and IF=2048kHz:
```
./single_tuner_recorder -r 8000000 -i 2048 -b 1536 -l 3 -f 162550000 -o noaa-8M-SAMPLERATEk.iq16
```


## Copyright

(C) 2023 Franco Venturi - Licensed under the GNU GPL V3 (see [LICENSE](LICENSE))
