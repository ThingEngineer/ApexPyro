---
PW535 Address Selection:

0x20: A0 = GND, A1 = GND, A2 = GND
0x21: A0 = VCC, A1 = GND, A2 = GND
0x22: A0 = GND, A1 = VCC, A2 = GND
0x23: A0 = VCC, A1 = VCC, A2 = GND
0x24: A0 = GND, A1 = GND, A2 = VCC
0x25: A0 = VCC, A1 = GND, A2 = VCC
0x26: A0 = GND, A1 = VCC, A2 = VCC
0x27: A0 = VCC, A1 = VCC, A2 = VCC

0x20: - - -
0x22: + - -
0x21: - + -
0x23: + + -
0x24: - - +
0x25: + - +
0x26: - + +
0x27: + + +

---

ADS1115 Address Selection:

ADDR to GND: 0x48 (default)
ADDR to VDD (VCC): 0x49
ADDR to SDA: 0x4A
ADDR to SCL: 0x4B
