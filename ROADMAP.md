# ApexPyro Roadmap

## Hardware Development

- Build BOM and detailed wiring instructions for ADC/MUX continuity scanning to detect good igniter, open or shorted circuits in the connected zones. Referance completed work in `docs/CONTINUITY_128_ROADMAP.md` and `docs/ESP32_DEVKITC_V4_WIRING.md` as well as the datasheets for the ADS1115 and CD74HCx4067 in `docs/datasheets`.

- Develop detailed assembly instructions for the ADC/MUX continuity scanning setup, including step-by-step guidance for connecting the ADS1115 and CD74HCx4067 to the ESP32 DevKitC V4, ensuring proper signal routing and power connections. Include diagrams and photos where necessary.

- Design schematic diagrams for the ApexPyro system, including the controller, ADC/MUX continuity scanning setup, relay boards, physical kill switch, and WiFi reset button. Ensure that the schematics clearly indicate all connections, component values, and any necessary annotations for assembly.

- Build a complete BOM for the entire ApexPyro system, including all necessary components for the controller, ADC/MUX continuity scanning, and any additional peripherals. Include part numbers, quantities, and recommended suppliers for each component.

## New Features and Improvements

- Improve controller role transfer so a Viewer can explicitly take the Controller role without relying on a page refresh. After the 'Controller' user clicks the role badge to unlock it, add a `data/control.svg` button to the left of the Viewer roles that when clicked shows a required confirmation modal before takeover, and show a toast to the previous controller when another user takes the role. Prevent multiple viewers from taking the Controller role at the same time (collision detection).

- Add an active-high output GPIO for lighting an LED whenever any zone or group is firing. The LED should be on (GPIO HIGH) while any zone or group is firing, and off when no zones or groups are firing.

- Sound an alarm on the device and in the app when the master arm is enabled and an enabled, unfired zone has a continuity error. Show which zone is alarming on the show page, show a toast message, allow the alarm to be silenced, and do not block firing while the alarm is active.

- When starting an Auto Show, display the Approx. duration in the confirmation modal as MM:SS instead of seconds only.

- When an Auto Show starts, display an overall show timer with both count-up and count-down timing.

- In manual mode, when the first zone or group is fired, also display the same show timers for reference if any configured zone or group has associated timing.

- On the builder page, only show zones that exist based on the boards that are actually detected. For example, if 2 boards are found show zones 1-32, if 4 boards are found show zones 1-64, and if only the second (I2C address) board is found show only zones 17-32.

- On the builder page, add drag-and-drop reordering for zones and groups in addition to the existing up/down buttons. When a zone or group is moved, update the order and reflect the change in the UI without requiring a page refresh.

- Explore adding HTTPS support, taking into consideration performance/latency trade-offs and resource constraints.
