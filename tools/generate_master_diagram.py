"""Draw the master PCB connections, and the installed analog sensor interface."""
from pathlib import Path
import re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / 'ESP_Timiac/include/config.h').read_text(encoding='utf-8')
def pin(name):
    return int(re.search(r'#define\s+' + name + r'\s+(\d+)', config).group(1))
sensor, sda, scl = (pin(name) for name in ('TIMIAC_PROXIMITY_PIN', 'TIMIAC_RTC_SDA', 'TIMIAC_RTC_SCL'))
assert sensor not in (18, 19, sda, scl)

parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1700" height="1300" viewBox="0 0 1700 1300" role="img" aria-labelledby="title desc">',
         '<title id="title">Timiac master wiring: existing power and analog hand sensor</title>',
         f'<desc id="desc">DS3231 uses GPIO21 and GPIO22. Move only the installed sensor signal from GPIO27 to ADC1 GPIO{sensor}; keep its resistors. GPIO18 and GPIO19 remain unused.</desc>',
         '<style>text{font-family:Arial,sans-serif;fill:#172b36} .wire{fill:none;stroke-linejoin:round;stroke-linecap:round}</style>',
         '<rect width="1700" height="1300" fill="#f7f9fb"/>']
RED, GND, V33, SIGNAL, SDA, SCL = '#bb352e', '#344b60', '#28804c', '#7751b9', '#147b8d', '#b96b13'

def text(x, y, value, size=20, color='#172b36', weight='normal', anchor='start'):
    parts.append(f'<text x="{x}" y="{y}" font-size="{size}" style="fill:{color}" font-weight="{weight}" text-anchor="{anchor}">{escape(value)}</text>')
def box(x, y, w, h, fill='#fff', stroke='#c4ced7', radius=12):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
def wire(points, color, net=''):
    coords = ' '.join(f'{x},{y}' for x, y in points)
    parts.append(f'<polyline class="wire" points="{coords}" stroke="{color}" stroke-width="4" data-net="{escape(net)}"/>')
def dot(x, y, color):
    parts.append(f'<circle cx="{x}" cy="{y}" r="6" fill="{color}"/>')

text(45, 48, 'TIMIAC  /  MASTER BOARD WIRING', 31, weight='bold')
text(45, 83, f'Existing 24 V → 3.3 V converter · New sensor on GPIO{sensor} · GPIO18/19 stay unused', 22)
box(40, 115, 1620, 385)
text(65, 145, '1   EXISTING POWER + CLOCK CONNECTIONS', 21, weight='bold')

box(65, 175, 195, 220, '#fff3f1')
text(85, 208, '24 V DC INPUT', 21, weight='bold')
text(85, 240, 'From your PSU', 18)
text(240, 281, '+24 V', 20, RED, anchor='end')
text(240, 341, '0 V', 20, GND, anchor='end')
box(365, 175, 265, 220, '#eef8f0')
text(385, 208, 'EXISTING DC–DC', 21, weight='bold')
text(385, 240, 'Keep output at 3.3 V', 19)
text(385, 281, 'IN+', 19, RED)
text(610, 281, 'OUT+  3.3 V', 19, V33, anchor='end')
text(385, 341, 'IN−', 19, GND)
text(610, 341, 'OUT− / GND', 19, GND, anchor='end')
wire([(260,275),(365,275)], RED, '+24V')
wire([(260,335),(365,335)], GND, 'GND')

box(805, 175, 315, 220, '#f0edfa')
text(825, 208, 'MASTER ESP32', 23, weight='bold')
text(825, 240, 'GPIO labels, not header numbers', 16)
text(825, 281, '3V3', 19, V33)
text(825, 341, 'GND', 19, GND)
wire([(630,275),(805,275)], V33, '+3V3')
wire([(630,335),(805,335)], GND, 'GND')
text(1098, 273, '3V3', 18, V33, anchor='end')
text(1098, 313, 'GND', 18, GND, anchor='end')
text(1098, 353, f'GPIO{sda}', 18, SDA, anchor='end')
text(1098, 383, f'GPIO{scl}', 18, SCL, anchor='end')

box(1350, 175, 270, 220, '#eef7fb')
text(1370, 208, 'DS3231 CLOCK', 22, weight='bold')
for y, color, label, net in [(267,V33,'VCC: 3.3 V','+3V3'),(307,GND,'GND','GND'),(347,SDA,'SDA','SDA'),(377,SCL,'SCL','SCL')]:
    wire([(1120,y),(1350,y)], color, net)
    text(1370,y+6,label,19,color)
text(1370, 428, 'SQW and 32K: not connected', 17)
text(65, 438, 'Keep existing PCB power traces. Remove the ultrasonic module; leave GPIO18/19 untouched.', 19)
text(65, 472, 'Motor boards communicate by Wi-Fi: no master UART wires. Disconnect 24 V before USB flashing.', 18)

dot(980,395,SIGNAL)
text(995, 410, f'GPIO{sensor}', 20, SIGNAL, weight='bold')
text(1300, 543, f'New signal wire to GPIO{sensor}', 19, SIGNAL)

box(40, 555, 1620, 530)
text(65, 590, f'2   INSTALLED SENSOR: MOVE ONLY GPIO27 SIGNAL WIRE TO GPIO{sensor}', 24, weight='bold')
box(80, 680, 310, 230)
text(105, 720, 'EXISTING HAND SENSOR', 22, weight='bold')
text(105, 760, 'Keep its power wiring', 20)
text(105, 795, 'Keep its ground wiring', 20)
text(365, 857, 'Existing output', 20, SIGNAL, anchor='end')
box(590, 680, 430, 230, '#fff6e8')
text(615, 720, 'EXISTING RESISTOR INTERFACE', 21, weight='bold')
text(615, 763, 'Leave every resistor as installed.', 21)
text(615, 800, 'No new pull-up or divider changes.', 20)
text(995, 857, 'Low-voltage junction', 19, SIGNAL, anchor='end')
wire([(390,850),(590,850)], SIGNAL, 'SENSOR_OUT')
wire([(1020,850),(1250,850)], SIGNAL, f'GPIO{sensor}')
dot(1250,850,SIGNAL)
text(1250, 785, f'GPIO{sensor} / ADC1', 24, SIGNAL, weight='bold', anchor='middle')
text(1250, 819, 'Analog input, no pull-up', 19, SIGNAL, anchor='middle')
wire([(980,395),(980,515),(1640,515),(1640,850),(1250,850)], SIGNAL, f'GPIO{sensor}')
text(100, 962, 'Measured at the signal junction relative to ESP32 GND:', 22, weight='bold')
text(100, 1003, 'Clear: 0.095 V (95 mV). Hand: 0.0025 V (2.5 mV). Confirm ADC readings after the move.', 22)
text(100, 1046, 'ESP32 GND, sensor 0 V and the resistor ground remain connected exactly as before.', 20, GND)
box(40, 1110, 1620, 155, '#edf5fa')
text(65, 1145, 'FIRMWARE 2026-10-20.3: ANALOG PRESENCE DETECTION', 21, weight='bold')
text(65, 1180, 'Detect below 40 mV, clear above 60 mV. Zero ADC counts count as low; voltage is below the ADC floor.', 20)
text(65, 1214, 'Below 100 mV the ESP32 ADC is outside its specified accurate range. Check both hand states on the dashboard.', 19)
text(65, 1247, 'Only the existing low-voltage junction connects to the GPIO. Never connect a raw 24 V sensor wire to it.', 19, RED)
parts.append('</svg>')
output = ROOT / 'Docs/master-wiring.svg'
output.write_text('\n'.join(parts) + '\n', encoding='utf-8')
print(output)

parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="680" viewBox="0 0 1400 680" role="img" aria-labelledby="title desc">',
         f'<title id="title">Move the installed sensor signal from GPIO27 to ADC1 GPIO{sensor}</title>',
         '<desc id="desc">Keep the installed sensor power, resistor network and common ground. Move only the existing divided signal wire to GPIO34. No resistor change is specified.</desc>',
         '<style>text{font-family:Arial,sans-serif;fill:#172b36}.wire{fill:none;stroke-linejoin:round;stroke-linecap:round}</style>',
         '<rect width="1400" height="680" fill="#f7f9fb"/>']
text(45, 55, f'SENSOR SIGNAL: GPIO27 → GPIO{sensor}', 32, weight='bold')
text(45, 100, 'Leave all installed resistors, sensor power and common ground unchanged.', 24)
box(65, 190, 350, 230)
text(90, 235, 'EXISTING SENSOR', 25, weight='bold')
text(90, 278, 'Keep power and GND', 21)
text(390, 352, 'Existing OUT', 20, SIGNAL, anchor='end')
box(530, 190, 460, 230, '#fff6e8')
text(555, 235, 'EXISTING RESISTORS', 25, weight='bold')
text(555, 278, 'Keep the complete interface.', 21)
text(555, 315, 'No components need changing.', 21)
text(965, 352, 'Signal junction', 20, SIGNAL, anchor='end')
wire([(415,345),(530,345)], SIGNAL, 'SENSOR_OUT')
wire([(990,345),(1150,345)], SIGNAL, f'GPIO{sensor}')
dot(1150,345,SIGNAL)
text(1180, 350, f'GPIO{sensor}', 29, SIGNAL, weight='bold')
text(1140, 395, 'ADC1, no pull-up', 19, SIGNAL)
text(65, 480, 'Measured: clear ≈ 95 mV; hand ≈ 2.5 mV. These values still need ADC verification.', 23)
text(65, 525, 'Firmware: detect < 40 mV; clear > 60 mV; zero counts are handled as below the ADC floor.', 22)
text(65, 570, 'GPIO34 avoids ADC2 / Wi-Fi contention. Below 100 mV, absolute accuracy is not specified.', 21)
text(65, 624, 'Connect only the existing low-voltage junction. Never connect raw sensor OUT / 24 V to an ESP32 pin.', 20, RED)
parts.append('</svg>')
output = ROOT / 'Docs/master-sensor-wiring.svg'
output.write_text('\n'.join(parts) + '\n', encoding='utf-8')
print(output)
