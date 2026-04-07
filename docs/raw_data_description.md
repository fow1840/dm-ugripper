# Ugripper Raw Data Description

## 1. Scope
- Data format version: `1`
- This document applies to dual-device synchronized acquisition, including the master device and the slave device.

## 2. Storage Location
Each device stores its own raw data locally:

```text
/mnt/data_disk/<device_sn>/data/
```

Each recording session is stored in a directory named:

```text
episode_YYYYMMDD_XXXX
```

Example:

```text
/mnt/data_disk/<device_sn>/data/episode_20260326_0001
```

## 3. Per-Episode Directory Contents
Each `episode` directory typically contains:

```text
episode_YYYYMMDD_XXXX/
├── metadata.json
├── calibration.json
├── info.json
├── cam.mkv
├── tact_left.mkv
├── tact_right.mkv
├── sensor_data.mcap
├── fays_stereo_output.mkv
├── fays_data.mcap
├── audio_pre.wav                 # May be generated on the master device only
└── audio_post.wav                # May be generated on the master device only
```

## 4. Master and Slave Device Behavior
### Master Device
- Controls recording start and stop.
- Creates the local `episode` directory and sends synchronized start/stop commands to the peer device.
- May additionally generate `audio_pre.wav` and `audio_post.wav`.

### Slave Device
- Starts and stops recording after receiving synchronization commands from the master device.
- Uses the same `episode` directory name as the master device for pairing convenience.
- Does not generate `audio_pre.wav` or `audio_post.wav`.
- Writes `paired_master_sn` into `info.json` after recording stops.

## 5. Role Rules
- The `Right` device always runs as `Master`.
- The `Left` device determines its role automatically.
- When the network cable is connected and the master device `192.168.1.100` is reachable, the `Left` device runs as `Slave`.
- Otherwise, the `Left` device runs as `Master`.

## 6. Key File Description
### `metadata.json`
Contains runtime metadata for the current recording, including:
- `device_id`: device serial number
- `device_side`: physical side of the device (`left` or `right`)
- `device_role`: runtime role (`master` or `slave`)
- `camera_codec`: video codec (`h264` or `h265`)
- `ugripper_version`: acquisition software version
- `ugripper_usb_updater_version`: USB updater version
- `data_format_version`: data format version

### `calibration.json`
Contains a snapshot of the calibration parameters used for this recording, for downstream processing consistency.

### `info.json`
Contains timing metadata for the recorded video streams. The primary fields for recovering absolute frame system time are:
- `boot_time_offset`
- `boot_time_offset_us`
- `cam_record_time_offset_us`
- `tact_left_record_time_offset_us`
- `tact_right_record_time_offset_us`

In downstream processing, the `*_record_time_offset_us` fields should be used as the primary offsets for frame-level timestamp reconstruction.

The slave device may additionally write:
- `paired_master_sn`

If the recording is a reset/re-record operation, `info.json` may also include:
- `tags: ["reset"]`
- `reset_info`

### `sensor_data.mcap`
This file stores robot-side sensor data in MCAP format.

Recorded topics include:
- `imu_raw`: body IMU
- `encoder`: encoder position

Payload definitions:
- `imu_raw`: little-endian `float32[10]`
- Field order: `qx`, `qy`, `qz`, `qw`, `gx`, `gy`, `gz`, `ax`, `ay`, `az`
- `encoder`: little-endian `int32 + float32`
- Field order: `raw`, `rad`

Time fields:
- `logTime`: host system time in nanoseconds
- `publishTime`: host system time in nanoseconds

For customer-side parsing, `py_script/mcap_sdk.py` exposes the same topics as:
- `imu_raw`
- `encoder`

### `fays_data.mcap`
This file stores Fays-related synchronized metadata in MCAP format.

Recorded topics include:
- `i`: Fays IMU
- `c`: Fays video frame index and timestamp mapping

Payload definitions:
- `i`: little-endian `float64[6]`
- Field order: `gx`, `gy`, `gz`, `ax`, `ay`, `az`
- `c`: little-endian `uint32`
- Field meaning: `frameIndex`

Time fields:
- `logTime`: native Fays device timestamp
- `publishTime`: system-aligned timestamp

For customer-side parsing, `py_script/mcap_sdk.py` exposes these topics as:
- `fays_imu` for raw topic `i`
- `fays_frame_index` for raw topic `c`

### `fays_stereo_output.mkv`
This file stores the encoded Fays stereo video stream.

Recommended usage:
- Read video frames from `fays_stereo_output.mkv`
- Read per-frame timing from topic `c` in `fays_data.mcap`
- Match video frame order with `frameIndex`
- Use `publishTime` as the recommended system-aligned frame timestamp

Detailed MCAP topic definitions, recommended time field usage, and CSV export examples are provided in:
- `docs/mcap_sdk_usage.md`
- `py_script/mcap_sdk.py`

## 7. Sensor Time Synchronization
### Cross-Device Time Base
- The two devices are synchronized at the system clock level through PTP (`ptp4l` + `phc2sys`).
- A monitoring service continuously writes PTP status to `/dev/shm/umi_ptp_status`.
- An NTP synchronization service is only used when the device is idle and not recording.
- As a result, timestamps expressed in system time can be aligned directly across the master device and the slave device.

### `cam.mkv`, `tact_left.mkv`, `tact_right.mkv`
- These video files store frame timing as relative video `PTS`.
- Absolute system time is restored through `info.json`.
- The following fields are provided:
- `cam_record_time_offset_us`
- `tact_left_record_time_offset_us`
- `tact_right_record_time_offset_us`
- The reconstruction formula is:

```text
frame_system_time_us = frame_pts_us + <camera>_record_time_offset_us
```

- Here `<camera>` can be `cam`, `tact_left`, or `tact_right`.

### `sensor_data.mcap`
- This file contains the robot-side sensor streams, including:
- `imu_raw`
- `encoder`
- For both topics, `logTime` and `publishTime` are written directly with the host system time in nanoseconds at write time.
- Therefore, `sensor_data.mcap` can be aligned directly with other system-time-based data across both devices.

### `fays_data.mcap`
- This file contains two Fays topics:
- `i`: Fays IMU
- `c`: Fays camera timestamp stream
- For both topics:
- `logTime` uses the native Fays hardware timestamp.
- `publishTime` uses the aligned host system timestamp.
- The alignment is computed from IMU samples first:
- For each IMU sample, the recorder calculates `system_time - imu_device_time`.
- This offset is then applied to Fays camera timestamps to obtain aligned system time.
- For downstream fusion across devices or with other sensors, `publishTime` should be used as the primary timestamp.
- If native Fays device time is needed for debugging or low-level analysis, `logTime` should be used.

### `fays_stereo_output.mkv`
- This file stores the encoded Fays stereo video stream.
- For precise timestamp alignment, it should be used together with topic `c` in `fays_data.mcap`.
- Topic `c` records one timestamp entry per encoded frame and provides:
- `frameIndex`
- `logTime`: native Fays timestamp
- `publishTime`: aligned system timestamp
- The frame order in `fays_stereo_output.mkv` corresponds to `frameIndex` order in topic `c`.
- Therefore, the recommended way to recover Fays video timing is to match video frame order to `frameIndex`, then use `publishTime` as the system-aligned frame timestamp.

### Audio Files
- `audio_pre.wav` and `audio_post.wav` are operator voice annotations used for data tagging during acquisition.
- `audio_pre.wav` is used for pre-annotation before recording.
- `audio_post.wav` is used for post-recording remarks after recording.
- They are associated with the episode directory, but no per-frame or per-sample synchronized timestamp field is written for downstream sensor fusion.
