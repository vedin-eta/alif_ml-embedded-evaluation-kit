# Setup for Alif Semiconductor Ensemble E7 Development Kit (DK-E7)

## Currently Supported Boards

* Alif Ensemble E7 Development Kit (DK-E7)
    * Dual Cortex-M55 cores with Arm Ethos-U55 NPU
    * MRAM-based flash storage
    * Dual-channel USB serial interface

## Requirements List

1. Alif SE Tools (SETOOLS) for device flashing and management
2. ARM ML Embedded Evaluation Kit (Alif fork)
3. ARM GNU Toolchain for Cortex-M55
4. Python 3.8+ with required dependencies (pexpect, pyserial)
5. USB access and udev rules configuration

____
## 1. Install Alif SE Tools (SETOOLS)

Alif SETOOLS provides utilities for flashing firmware, device reset, and device management operations.

Download the latest SETOOLS release from Alif Semiconductor and extract it to your project directory:

```bash
$ cd ~/Project
$ # Extract SETOOLS package (obtain from Alif Semiconductor)
$ tar -xzf app-release-exec-linux-<version>.tar.gz
$ mv app-release-exec-linux app-release-exec-linux
```

The SETOOLS package includes:
* `maintenance` - Interactive maintenance utility for device control
* `app-gen-toc` - Application table of contents generator
* `app-write-mram` - MRAM flash programming utility
* `isp_config_data.cfg` - Serial port configuration file

____
## 2. Install ARM ML Embedded Evaluation Kit (Alif Fork)

Clone the Alif-specific fork of ARM's ML Embedded Evaluation Kit:

```bash
$ cd ~/Project
$ git clone git@github.com:vedin-eta/alif_ml-embedded-evaluation-kit.git
$ cd alif_ml-embedded-evaluation-kit
$ git checkout v1.0.0
```

Follow the detailed build and setup instructions in the repository's `ML_Embedded_Evaluation_Kit.md` file.

The evaluation kit provides:
* Inference runner application for model benchmarking
* Integration with Arm Ethos-U55 NPU
* TensorFlow Lite Micro runtime
* Performance profiling and cycle counting

____
## 3. Install ARM GNU Toolchain

Download and install the ARM GNU Toolchain for Cortex-M processors:

```bash
$ cd <download_directory>
$ wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2
$ sudo mkdir -p /opt/arm-none-eabi/
$ sudo tar xf gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 -C /opt/arm-none-eabi/
```

Add the toolchain to your PATH in `~/.bashrc`:

```bash
export PATH="/opt/arm-none-eabi/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH"
```

____
## 4. Setup USB Access and Udev Rules

The Alif DK-E7 board uses a Cypress Semiconductor USB-Serial dual-channel interface. When connected, Linux creates two serial ports:

* **Channel 0** (`-if00`) → `/dev/ttyACM0` - **Control port** for SETOOLS maintenance
* **Channel 1** (`-if02`) → `/dev/ttyACM1` - **Communications port** for UART logs

### 4-a. Identify the Board's Unique ID

Each Alif E7 chip has a unique ECC key that serves as its hardware identifier. To read the device ID:

```bash
$ cd ~/Project/app-release-exec-linux
$ ./maintenance -opt getecckey
[INFO] /dev/ttyACM0 open Serial port success
[INFO] baud rate 55000
[INFO] Connecting to target...Device connected
ECC key (HEX):  18499D89A2B1CDE5301824A7F76A862CD2D426C8DBE8FB2FB857F2BAE2D5B10C009BFE1762517D44B83B0651682F491C0C0B90AA8B696A888D06C8AF2F69D0A0
```

To convert the ECC key to a board serial number (SHA256, first 8 bytes), use the Python utility:

```python
from modeltesting.boards.Alif.alif_setools import AlifSetools
sn = AlifSetools.convert_ecc_key_to_sn("18499D89A2B1CDE5301824A7F76A862CD2D426C8DBE8FB2FB857F2BAE2D5B10C009BFE1762517D44B83B0651682F491C0C0B90AA8B696A888D06C8AF2F69D0A0")
print(sn)  # Output: CC8FBF54A705E9C7
```

**Note:** The barcode serial number printed on the DK-E7 PCB is a manufacturing/logistics identifier. It is NOT stored in the SoC and is NOT exposed over USB.

### 4-b. Create Persistent Symlinks with Udev Rules

To reliably identify the correct serial ports, create custom udev rules based on the board's serial number.

First, find the USB VID/PID:

```bash
$ lsusb
Bus 001 Device 004: ID 04b4:0005 Cypress Semiconductor Corp. USB-Serial (Dual Channel)
```

Then, verify the USB interface details:

```bash
$ ls -l /dev/serial/by-id/
lrwxrwxrwx 1 root root 13 Jan 15 10:00 usb-Cypress_Semiconductor_USB-Serial__Dual_Channel_-if00 -> ../../ttyACM0
lrwxrwxrwx 1 root root 13 Jan 15 10:00 usb-Cypress_Semiconductor_USB-Serial__Dual_Channel_-if02 -> ../../ttyACM1
```

Create a udev rule file named according to your board's serial number:

```bash
$ sudo nano /etc/udev/rules.d/99-alif-e7-CC8FBF54A705E9C7.rules
```

Add the following rules (replace `ATTRS{serial}` with your board's actual USB serial if available):

```bash
# Channel 0 (Control port - for SETOOLS maintenance)
SUBSYSTEM=="tty", ENV{ID_VENDOR_ID}=="04b4", ENV{ID_MODEL_ID}=="0005", ENV{ID_USB_INTERFACE_NUM}=="00", \
  SYMLINK+="alif-e7-CC8FBF54A705E9C7-control"

# Channel 1 (Communications port - for UART logs)
SUBSYSTEM=="tty", ENV{ID_VENDOR_ID}=="04b4", ENV{ID_MODEL_ID}=="0005", ENV{ID_USB_INTERFACE_NUM}=="02", \
  SYMLINK+="alif-e7-CC8FBF54A705E9C7-comms"
```

Reload the udev rules:

```bash
$ sudo udevadm control --reload-rules
$ sudo udevadm trigger
```

Verify the symlinks are created:

```bash
$ ls -l /dev/alif-e7-*
lrwxrwxrwx 1 root root 7 Jan 15 01:40 /dev/alif-e7-CC8FBF54A705E9C7-control -> ttyACM0
lrwxrwxrwx 1 root root 7 Jan 15 01:40 /dev/alif-e7-CC8FBF54A705E9C7-comms -> ttyACM1
```

### 4-c. Setup USB Permissions

Add your user to the `dialout` group for serial port access:

```bash
$ sudo usermod -a -G dialout $USER
$ sudo usermod -a -G plugdev $USER
```

Create USB access rules:

```bash
$ rules='SUBSYSTEM=="usb", MODE="0666", GROUP="plugdev"'
$ echo -e $rules | sudo tee /etc/udev/rules.d/99-alif.rules
$ sudo udevadm control --reload-rules
$ sudo udevadm trigger
```

**Log out and log back in** for group membership changes to take effect.

____
## 5. Configure Board in config.json

Add the Alif board to your `~/Project/benchmarking-platform/AutomatedModelBenchmarking/src/modeltesting/config.json`. Some notable entries:

* `control_serial_port` - Serial port for SETOOLS maintenance operations (device reset, flashing)
* `comms_serial_port` - Serial port for UART communication and inference logs
* `uart_serial_no` - Serial NO calculated form the ECC key
* `serial_no` - Serial NO calculated form the ECC key
* `setools_path` - Path to the Alif SETOOLS directory
* `alif_project_path` - Path to the ARM ML Embedded Evaluation Kit repository
____
## 6. Verify Setup

### 6-a. Test SETOOLS Connection

```bash
$ cd ~/Project/app-release-exec-linux
$ ./maintenance -c /dev/ttyACM0 -b 55000
# You should see the maintenance menu if the connection is successful
```

### 6-b. Test Device Reset

```python
from modeltesting.boards.Alif.alif_setools import AlifSetools

setools = AlifSetools(
    root_dir="/home/eta_lab/Project/app-release-exec-linux",
    serial_port="/dev/ttyACM0",
    baudrate=55000
)

# Perform device reset
setools.reset_device()
print("Device reset successful!")
```

### 6-c. Test Device ID Retrieval

```python
from modeltesting.boards.Alif.alif_setools import AlifSetools

setools = AlifSetools(
    root_dir="/home/eta_lab/Project/app-release-exec-linux",
    serial_port="/dev/ttyACM0",
    baudrate=55000
)

# Get ECC key (device ID)
ecc_key = setools.get_device_id()
print(f"Device ECC Key: {ecc_key}")
```

### 6-d. Monitor UART Output

Use `minicom` or `screen` to monitor the communications port:

```bash
$ minicom -D /dev/ttyACM1 -b 115200
# or
$ screen /dev/ttyACM1 115200
```

____
## 7. Troubleshooting

### Issue: "Permission denied" when accessing serial ports

**Solution:**
```bash
$ sudo usermod -a -G dialout $USER
$ sudo usermod -a -G plugdev $USER
# Log out and log back in
```

### Issue: SETOOLS cannot connect to device

**Solution:**
* Verify the correct control port is being used (usually `-if00`, `/dev/ttyACM0`)
* Check that baudrate is set to 55000
* Ensure no other process is using the serial port
* Try unplugging and replugging the USB cable

### Issue: Symlinks not created after adding udev rules

**Solution:**
```bash
$ sudo udevadm control --reload-rules
$ sudo udevadm trigger
# Unplug and replug the board
```

### Issue: Cannot find board's USB serial

**Solution:**
```bash
$ lsusb | grep Cypress
$ ls -l /dev/serial/by-id/
$ dmesg | tail -n 50  # Check for USB connection messages
```

### Issue: Wrong serial port being used after reboot

**Solution:**
* Use the persistent symlinks created via udev rules instead of `/dev/ttyACMx`
* Update `config.json` to use the symlink paths

____
## 8. Additional Resources

* https://drive.google.com/drive/u/2/folders/1ubhgWbV6fXokKVcZeY_GUOpW3IB6QAYp
* https://github.com/vedin-eta/alif_ml-embedded-evaluation-kit

____
## Tips and Tricks

### Debugging Serial Communication

Monitor the control port in one terminal:

```bash
$ minicom -D /dev/alif-e7-CC8FBF54A705E9C7-control -b 55000
```

Monitor the comms port in another terminal:

```bash
$ minicom -D /dev/alif-e7-CC8FBF54A705E9C7-comms -b 115200
```

### Resolving Symlink Paths Programmatically

```python
from modeltesting.boards.Alif.alif_utils import resolve_serial_port_symlink

real_port = resolve_serial_port_symlink("/dev/alif-e7-CC8FBF54A705E9C7-comms")
print(f"Symlink resolves to: {real_port}")  # Output: /dev/ttyACM1
```

### Batch Device Operations

```python
from modeltesting.boards.Alif.alif_setools import AlifSetools

setools = AlifSetools(
    root_dir="/home/eta_lab/Project/app-release-exec-linux",
    serial_port="/dev/alif-e7-CC8FBF54A705E9C7-control",
    baudrate=55000
)

# Get device info
device_id = setools.get_device_id()
print(f"Device ID: {device_id}")

# Reset device
setools.reset_device()

# Flash firmware
setools.flash_device("/path/to/firmware.bin")
```