**Timiac: update all five boards on a Windows PC**

You need internet, two FAT32 microSD cards and a card reader, a USB data cable for the ESP32, and the supplied **3.3 V ESP-12 programming adapter**. Label the ESP-12 modules **1** and **2** before removing them. Switch off Timiac and unplug its USB cables before changing connections.

**Before running:** this package requests 1,000 mA RMS while moving/homing and approximately 500 mA while holding. Fit the planned **24 V / 400 W supply** and cooling fans first. Update all five boards before powering the complete system.

**1. Install the software**

Install [VS Code for Windows](https://code.visualstudio.com/docs/setup/windows) using **User Setup**, then [Python 3.13 for Windows](https://www.python.org/downloads/windows/) using **Windows installer (64-bit)**. In the Python installer, tick **Add python.exe to PATH** and choose **Install Now**. Restart VS Code afterward.

Extract the received Timiac ZIP. In VS Code, choose **File → Open Folder** and select the folder containing `run.py`. Choose **Terminal → New Terminal**. Paste these commands **one at a time**, waiting for each to finish:

```powershell
python --version
python -m pip install --user -r requirements.txt "esptool>=5,<6"
python run.py verify
```

Continue only if verification says **Verified TIMIAC2-03b3b6e145e4**. No VS Code extensions are needed.

**2. Update both Octopus boards using their microSD cards**

Insert the first card into the PC. Open **File Explorer → This PC** and note its drive letter. Replace `I:` below with that card's letter:

```powershell
python run.py octopus_release --sd I:\
```

Safely eject the card, insert it into the **powered-off first Octopus**, then power that board on. Keep the master off. Wait one minute, switch the board off, and check the card in the PC: `firmware.bin` should have disappeared and become `FIRMWARE.CUR`. After confirming this, remove the card; it is not needed during operation.

**Repeat the same procedure with the second card and second Octopus.** Both use the same command. Leave both boards off afterward.

**3. Update ESP-12 number 1, then number 2**

Disconnect the ESP-12 from its Octopus and use the supplied **3.3 V programming adapter**, one module at a time. Do not connect it to Octopus power/UART while programming. Use the adapter's FLASH/PROGRAM setting; [ESP-12 flashing mode requires GPIO0 low during reset](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html). Return it to RUN afterward.

Right-click Windows **Start → Device Manager → Ports (COM & LPT)**. Note the port that appears when the adapter is plugged in. Replace `COM5` with that port:

```powershell
python run.py esp1_release --port COM5
```

Wait for successful writing and verification, then unplug the adapter. **Physically replace module 1 with module 2**, check its COM port, and run:

```powershell
python run.py esp2_release --port COM5
```

Unplug the adapter and reinstall each module on its original Octopus.

**4. Update the master ESP32**

Connect only the master to the PC using its USB data cable. Check its COM port again, then run:

```powershell
python run.py master_release --port COM5
```

Wait for successful writing and verification. This also installs the dashboard. Unplug USB afterward.

For this cloud-connected clock, the same command installs the paired firmware
included in the repository. No credentials need copying. The terminal should
say **Flashing cloud-enabled release master**.
Reconnect the master to its Wi-Fi router, open **https://control.timiac.it/**,
and sign in with the account supplied separately.

**5. Start and check**

Restore all connections and power on Timiac. **Keep clear of the arms: they home automatically.** Connect your phone to Wi-Fi **Timiac**, password **12345678**, and open **http://192.168.4.1**. Check both boards are ready and the time is correct. Leave it running for at least **30 minutes** to confirm it keeps updating.

If any step fails, stop and send the terminal error to Davoud. If no COM port appears, check the USB data cable and install the programming adapter manufacturer's driver. Do not guess wiring or overwrite another drive.

**For Davoud: files to include in the ZIP**

Keep these paths inside one `Timiac` folder:

```text
instruction.md
run.py
requirements.txt
machine.json
tools/machine_config.py
Marlin/profiles/lumiac-reference/  (entire folder)
firmware/two-motor/               (entire folder, including JSON files)
ESP_Timiac/include/cloud_config.h  (included clock pairing defaults)
```

At **50% speed**, check that Struggle stays between **15 and 35 mm** and finishes within **15 seconds**. Next Hour should open all arms, close all, then show time within **40 seconds** (within **20 seconds** at 100%). Stop if steps are lost or overheating warnings appear. A thermal fault requires cooling and an Octopus reset before homing again.
