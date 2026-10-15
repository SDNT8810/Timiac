"""Draw the master PCB connections, including its 24 V NPN sensor divider."""
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
         '<title id="title">Timiac master wiring: existing 24 V to 3.3 V converter and NPN NC infrared sensor</title>',
         '<desc id="desc">DS3231 uses GPIO21 and GPIO22. E3F-DS30B2 uses three resistors into GPIO27. GPIO18 and GPIO19 remain unused. All grounds share the existing PCB ground.</desc>',
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

box(40, 555, 1545, 530)
text(65, 590, '2   NEW SENSOR INTERFACE — RESISTORS ONLY, FOR A FIXED 24 V SUPPLY', 22, weight='bold')
box(80, 680, 280, 260, '#fff')
text(100, 714, 'HESCHEN', 24, weight='bold')
text(100, 746, 'E3F-DS30B2', 23, weight='bold')
text(100, 775, 'NPN normally closed', 18)
text(340, 817, 'BROWN / +V', 18, '#925429', anchor='end')
text(340, 867, 'BLACK / OUT', 18, '#172b36', anchor='end')
text(340, 917, 'BLUE / 0 V', 18, '#1675b8', anchor='end')

# The three wires join power and the divided output, not a 24 V GPIO connection.
wire([(360,810),(470,810),(470,630),(800,630)], RED, '+24V')
text(480, 656, '+24 V: same master-board input', 18, RED)
wire([(800,630),(800,675)], RED, '+24V')
box(788,675,24,80,'#fff6e8','#344b60',0)
wire([(800,755),(800,800)], '#172b36', 'SENSOR_OUT')
text(835, 707, 'R1: 4.7 kΩ', 21, weight='bold')
text(835, 735, '1%, ¼ W', 18)
wire([(360,860),(650,860),(650,800),(920,800)], '#172b36', 'SENSOR_OUT')
dot(800,800,'#172b36')
text(690, 837, 'A: black output', 18)
box(920,788,100,24,'#fff6e8','#344b60',0)
text(970, 762, 'R2: 68 kΩ', 21, weight='bold',anchor='middle')
text(970, 735, '1%, ¼ W', 18,anchor='middle')
wire([(1020,800),(1250,800)], SIGNAL, f'GPIO{sensor}')
dot(1250,800,SIGNAL)
text(1245, 767, f'TEST / GPIO{sensor}', 21, SIGNAL, weight='bold',anchor='middle')
wire([(1250,800),(1250,855)], SIGNAL, f'GPIO{sensor}')
box(1238,855,24,80,'#fff6e8','#344b60',0)
text(1290, 891, 'R3: 10 kΩ', 21, weight='bold')
text(1290, 919, '1%, ¼ W', 18)
wire([(1250,935),(1250,1010),(470,1010),(470,910),(360,910)], GND, 'GND')
text(490, 1043, 'GND: sensor blue + PSU 0 V + converter OUT− + ESP32 GND + clock GND', 18, GND)
text(680, 970, 'Black output is a high-voltage node.', 20, RED, weight='bold')
text(680, 1000, f'Only the R2/R3 junction goes to GPIO{sensor}.', 19, RED)
wire([(980,395),(980,515),(1640,515),(1640,800),(1250,800)], SIGNAL, f'GPIO{sensor}')

box(40, 1110, 1620, 155, '#edf5fa')
text(65, 1145, 'CHECK BEFORE CONNECTING THE GPIO', 20, weight='bold')
text(65, 1178, f'NC sensor clear → GPIO test point near 0 V. Hand detected → approximately 2.90 V at a 24.0 V supply.', 20)
text(65, 1210, 'Use a shared ground. No ESP32 internal pull-up. 500 ms clear arms the gesture; 200 ms detected triggers once.', 19)
text(65, 1242, 'This is an IR presence switch, not LiDAR. Set detection range on the sensor; no centimetre reading is available.', 18)
parts.append('</svg>')
output = ROOT / 'Docs/master-wiring.svg'
output.write_text('\n'.join(parts) + '\n', encoding='utf-8')
print(output)
