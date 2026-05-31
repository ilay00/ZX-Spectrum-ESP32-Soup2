# ZX-Spectrum-ESP32-Soup2
**Retro computer on ESP32 with composite TV output**  > "Soup" of BASIC, Python, editor and games on modern hardware! >  > **(c) DMKS(laic) + Claude/Anthropic + GoogleAI + Cursor + Grok + GPT-4**
---
<img width="3264" height="2448" alt="IMG_20260501_121645" src="https://github.com/user-attachments/assets/c2ec0d3b-c480-4e5b-a29e-56c0e9e459d3" />
<img width="3264" height="2448" alt="IMG_20260501_121447" src="https://github.com/user-attachments/assets/27b57caa-c4b3-4edf-9952-18cf60ad2c2f" />
<img width="3264" height="2448" alt="IMG_20260501_121429" src="https://github.com/user-attachments/assets/e0f93f81-2ddf-46fa-9fe6-7e0092527060" />
<img width="3264" height="2448" alt="IMG_20260501_121358" src="https://github.com/user-attachments/assets/cb67cb94-df32-40c2-ab87-bf72db6523f3" />
---

## ✨ Features

| Feature | Status |
|---|---|
| Composite TV output (PAL) | ✅ |
| PS/2 keyboard | ✅ |
| File Manager | ✅ |
| Code Editor | ✅ |
| BASIC interpreter (TenBasic) | ✅ |
| Python interpreter | ✅ |
| WiFi file upload | ✅ |
| Games (SOUP I/II + Snake) | ✅ |
| SD card | 🔜 v3 |
| Sound output | 🔜 v3 |
| PCB + 3D printed case | 🔜 v3 |

---

## 🔧 Hardware

### Required components
- ESP32 DevKit (any version)
- RCA connector for video
- Resistor 270Ω (optional) for composite video
- PS/2 keyboard (any standard)

### Wiring

```
ESP32 GPIO 25 → 270Ω (optional) → RCA center (video)
ESP32 GND     → RCA outer (ground)

PS/2 keyboard:
  CLK  → GPIO 32
  DATA → GPIO 33
  GND  → ESP32 GND
  +5V  → ESP32 5V
```

### Flash settings (Arduino IDE)
```
Board:           ESP32 Dev Module
CPU Frequency:   240MHz
Flash Size:      4MB
Partition:       3MB app / 1MB SPIFFS
Upload Speed:    921600
```

---

## 📦 Installation

### Required libraries
- **FabGL** by Fabrizio Di Vittorio
- ESP32 Arduino Core

### Steps
1. Clone this repository
2. Open `sketch_esp32sup.ino` in Arduino IDE
3. Install libraries via Library Manager
4. Select board: ESP32 Dev Module
5. Set partition scheme: **3MB APP / 1MB SPIFFS**
6. Upload firmware
7. Connect ESP32 to TV via composite cable
8. Connect PS/2 keyboard
9. Power on — main menu appears!

---

## 🖥️ Usage

### Main Menu
Navigate with **↑↓**, select with **Enter**:

```
1. File Manager    — browse and manage files
2. Edit File       — built-in code editor
3. Run BASIC       — run .bas programs
4. Python          — run .py programs  
5. WiFi Upload     — upload files via browser
6. SOUP I          — Cook it! (text game)
7. SOUP II         — Catch it! (arcade game)
```

### File Manager commands
```
↑↓         — navigate files
Enter       — open file in editor
new name.bas — create new file
run 1       — run file #1 (.bas or .py)
del 1       — delete file #1
ls          — list all files (debug)
format      — format SPIFFS (caution!)
back        — return to menu
```

### Editor commands
```
↑↓          — move between lines
Enter        — edit current line
save         — save file
save name    — save as new name
ins          — insert line after current
ins 20       — insert after line 20
del          — delete current line
add          — add line at end
ren          — renumber BASIC lines (10,20,30...)
back         — exit editor
```

### WiFi Upload
1. Select **WiFi Upload** from menu
2. Press **Enter** to start access point
3. Connect phone/PC to: `ESP32SUP` / password: `12345678`
4. Open browser: `http://192.168.4.1`
5. Upload `.bas` or `.py` files
6. Press **ESC** to stop WiFi

---

## 💻 Programming

### BASIC (TenBasic)

```basic
10 CLS
20 PRINT "Hello ESP32!"
30 LET A = 10
40 INPUT B
50 IF A > B THEN PRINT "A wins"
60 FOR I = 1 TO 5
70 PRINT I
80 NEXT I
90 DIM X(10)          ' number array
100 DIM S$(10)        ' string array
110 DIM M(3,3)        ' 2D array
120 LOCATE 10, 5      ' move cursor (col, row)
130 DELAY 500         ' pause 500ms
140 LET K = INKEY$    ' read key (WASD/arrows)
150 LET R = RND(100)  ' random 1..100
160 GOSUB 1000        ' call subroutine
170 END
1000 PRINT "sub"
1010 RETURN
```

**Supported commands:**
`PRINT` `LET` `INPUT` `IF/THEN` `FOR/NEXT/STEP` `GOTO` `GOSUB/RETURN`
`DIM` `LOCATE` `DELAY` `CLS` `END` `RND()` `INKEY$` `REM`

> ⚠️ Basic version — designed as a radio kit.
> Anyone can add commands for their own purposes!

---

### Python (ESP32 dialect, indent = 1 space)

```python
# Variables and math
x = 15
print(x ** 2)       # 225
print(abs(-5))      # 5
print(sqrt(16))     # 4.0

# Strings
a = "Hello"
b = " ESP32"
print(a + b)        # Hello ESP32

# Conditions
if x > 10:
 print("big")
else:
 print("small")

# Loop
for i in range(5):
 print(i)

# Functions
def greet(name):
 print(name)
greet("ESP32SUP")

# Input
x = input("Enter name: ")
print(x)
```

**Supported:**
`print()` `input()` `if/else` `for/range()` `def`
`abs()` `sqrt()` `pow()` `round()` `max()` `min()`
`int()` `float()` `str()` `len()`
`+= -= *= /=` `** % //`

> ⚠️ `list` and `dict` — in development

---

## 🎮 Games

### SOUP I — Cook it!
Text game. Collect ingredients (BASIC + Python + WiFi)
to cook the perfect retro computer soup!

### SOUP II — Catch it!
Arcade game. Move `@` around the screen,
collect letters S→O→U→P in order.
Avoid `X` bombs. Score multiplies each letter!

### Snake (BASIC)
Classic snake on BASIC. WASD or arrow keys.
Tail grows when eating `*` food.

---

## 🏗️ Roadmap

### v3.0 (Winter 2025-26)
- [ ] SD card support
- [ ] Sound output (TV speaker)
- [ ] GPIO commands for Python
- [ ] More BASIC graphics commands
- [ ] PCB design
- [ ] 3D printed retro case with keyboard

---

## 📊 Technical specs

| Parameter | Value |
|---|---|
| CPU | ESP32 240MHz dual-core |
| RAM | 520KB SRAM |
| Flash | 4MB (3MB app + 1MB SPIFFS) |
| Video | PAL composite, 40×25 chars |
| Interface | PS/2 keyboard |
| Connectivity | WiFi 802.11 b/g/n |

---

## 🛠️ Troubleshooting

**No video** → Check GPIO 25 connection

**Keyboard not working** → Check GPIO 32 (CLK) and GPIO 33 (DATA), check 5V power

**WiFi not connecting** → SSID: `ESP32SUP`, password: `12345678`

**SPIFFS errors** → Use `format` command in File Manager

---

## 📜 License

MIT License — use freely, modify, share!

---

## 👥 Authors

| Role | Author |
|---|---|
| Project creator, hardware, integration | **DMKS (laic)** |
| AI assistant, BASIC fixes, Python, SOUP game | **Claude / Anthropic** |
| Python interpreter co-development | **GoogleAI** |
| Early BASIC work | **GPT-4, Grok** |
| README template | **Cursor** |

---

## 💬 Support

Open an issue in the repository!

---

*Enjoy retro programming on modern hardware! 🥣🎮*
