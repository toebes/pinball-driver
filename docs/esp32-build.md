# ESP32 coordinator build

The ESP32-8048S070C V1.4 coordinator uses the Arduino ESP32 core 2.0.17 and the panel, touch, and LVGL libraries from Guition's 7-inch sample package. The coordinator screen and ring code live in the project repository.

1. Obtain `7.0inch_ESP32-8048S070.zip` from the [Guition model list](https://www.guition.com/model-selection). Extract it into a local folder. Set `$demoArduino` to its `1-Demo/Demo_Arduino` folder.
2. Install Arduino CLI and Espressif's Arduino core `esp32:esp32@2.0.17` using the [Espressif package index](https://espressif.github.io/arduino-esp32/package_esp32_index.json).
3. From the repository root, prepare the sketch and build it.

```powershell
$sketch = .\tools\prepare-esp32-demo.ps1 -DemoArduinoRoot $demoArduino
$board = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app'
arduino-cli compile --fqbn $board --libraries "$demoArduino/Libraries" --build-path build/esp32 $sketch
```

If Windows reports that the command line is too long, use short paths for the Arduino data directory, demo archive, and build directory. The local development setup used a temporary drive letter for these paths.

Back up the development board's factory flash before the first upload. Then run `arduino-cli upload --fqbn $board -p COM4 --input-dir build/esp32 $sketch` with the board's actual serial port. Open a serial monitor at 115200 baud to see transmitted frames and Node_Config replies.

After resetting the PY32s, tap **Assign Addresses** once. Select a node and use the Port button to cycle through ports 1–6. Set LED sends the three RGB slider values. Start Monitoring streams Hall readings for that node and port until Stop Monitoring is tapped. The full ring was verified by receiving Node_Config replies from nodes 1 through 4.

For ring updates, build the relocated `py32_app` target and supply its build
directory to `prepare-esp32-demo.ps1 -Py32Build`. The script embeds the binary
and reads its major/minor version from the CMake cache. Omitting `-Py32Build`
builds the coordinator without an update image. Assign all four addresses
before updating, select the target node, and tap **Update 0.4**. The coordinator
confirms the version after reboot and address reassignment.
