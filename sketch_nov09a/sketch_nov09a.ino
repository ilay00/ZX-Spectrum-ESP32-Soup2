
// ═══════════════════════════════════════════════════════
//  ESP32SUP  —  VGAOS-COMP  v1.0
//  Hardware : ESP32SUP  |  Composite TV  |  PS2 keyboard
//  Video    : GPIO 25 (CVBS)
//  PS2 CLK  : GPIO 32   PS2 DATA : GPIO 33
//  Storage  : SPIFFS (SD — позже)
// ═══════════════════════════════════════════════════════

#include "fabgl.h"
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include "BasicInterpreter.h"   // твой BASIC — оставляем

// ─── Железо ────────────────────────────────────────────
#define VIDEOOUT_GPIO  GPIO_NUM_25
#define PS2_CLK        GPIO_NUM_32
#define PS2_DATA       GPIO_NUM_33

fabgl::CVBS16Controller  DisplayController;
fabgl::PS2Controller     ps2Controller;
fabgl::Terminal          terminal;
// --- Объявляем переменные и функции из интерпретатора ---
extern bool py_waiting_input;
extern String py_input_var;
extern String py_input_buffer;
extern int py_prog_pc;

extern void py_out(const String& s); // Важно: здесь const String& s, как в твоем коде!
extern void pyExec(String cmd);
// ─── Состояния UI ──────────────────────────────────────
enum UIState {
  MODE_MENU,
  MODE_FILES,
  MODE_EDITOR,
  MODE_ENTER_FILENAME,
  MODE_SAVE_EXT,
  MODE_BASIC,
  MODE_PYTHON,
  MODE_WIFI,
  MODE_SOUP1,
  MODE_SOUP2
};
UIState ui_state = MODE_MENU;

// ─── Меню ──────────────────────────────────────────────
const char* MENU_ITEMS[] = {
  "1. File Manager",
  "2. Edit File",
  "3. Run BASIC",
  "4. Python",
  "5. WiFi Upload",
  "6. SOUP I  - Cook it!",
  "7. SOUP II - Catch it!"
};
const int MENU_COUNT = 7;
int menu_idx = 0;

// ─── WiFi ───────────────────────────────────────────────
#include <WiFi.h>
#include <WebServer.h>
WebServer webServer(80);
bool wifiRunning = false;
const char* AP_SSID = "ESP32SUP";
const char* AP_PASS = "12345678";

// ─── Редактор / файлы ──────────────────────────────────
std::vector<String> file_list;
int file_scroll      = 0;
int file_cursor      = 0;
std::vector<String> editor_lines;
String current_file  = "";
int editor_cursor    = 0;
int editor_scroll    = 0;
const int FILE_PAGE  = 14;
const int EDIT_PAGE  = 16;

// ─── Python REPL ───────────────────────────────────────
// Переменные объявлены в PythonInterpreter.cpp
extern std::vector<String> py_vars_name;
extern std::vector<float>  py_vars_val;
extern std::vector<String> py_vars_str_name;
extern std::vector<String> py_vars_str_val;
extern std::vector<String> py_history;

// ─── Ввод строки ───────────────────────────────────────
String inputLine      = "";
bool   lastEnterPressed = false;

// ─── Extern из BasicInterpreter.cpp ────────────────────
extern bool   waitingForInput;
extern String inputVarName;
extern String lastKey;
extern bool   basic_running;
extern std::map<String, float>  basic_vars_num;
extern std::map<String, String> basic_vars_str;
void basic_run();

// ─── Расширение → ВЕРХНИЙ регистр ──────────────────────
String lowerExt(String f) {
  int d = f.lastIndexOf('.');
  if (d < 0) return f;
  String ext = f.substring(d + 1);
  ext.toLowerCase();
  return f.substring(0, d + 1) + ext;
}

// ═══════════════════════════════════════════════════════
//  TERMINAL HELPERS
// ═══════════════════════════════════════════════════════

void cls()                         { terminal.write("\e[2J\e[H"); }
void setCur(int r, int c)          { terminal.printf("\e[%d;%dH", r, c); }
void color(const char* ansi)       { terminal.printf("\e[%sm", ansi); }
void colorReset()                  { terminal.write("\e[0m"); }

void printHeader(const char* t) {
  color("1;36");
  terminal.printf("══ %s ", t);
  colorReset();
  terminal.write("\r\n");
}

void printPrompt() { terminal.write("\e[33m> \e[0m"); }

void printStatusBar(const char* msg) {
  // Строка 24 — статус
  setCur(24, 1);
  color("44;37");  // синий фон, белый текст
  terminal.printf(" %-38s ", msg);
  colorReset();
}

// ─── Рамка окна (ANSI псевдографика) ───────────────────
// row, col — 1-based; w, h — символы
void drawBox(int row, int col, int w, int h, const char* title) {
  color("1;36");

  // Верхняя граница
  setCur(row, col);
  terminal.write("┌");
  for (int i = 0; i < w - 2; i++) terminal.write("─");
  terminal.write("┐");

  // Боковые стороны
  for (int r = row + 1; r < row + h - 1; r++) {
    setCur(r, col);     terminal.write("│");
    setCur(r, col + w - 1); terminal.write("│");
  }

  // Нижняя граница
  setCur(row + h - 1, col);
  terminal.write("└");
  for (int i = 0; i < w - 2; i++) terminal.write("─");
  terminal.write("┘");

  // Заголовок
  if (title && strlen(title) > 0) {
    setCur(row, col + 2);
    color("1;33");
    terminal.printf(" %s ", title);
    color("1;36");
  }

  colorReset();
}

// ═══════════════════════════════════════════════════════
//  ГЛАВНОЕ МЕНЮ
// ═══════════════════════════════════════════════════════

void drawMenu() {
  cls();
  drawBox(1, 1, 40, 2 + MENU_COUNT, "ESP32SUP v1.0");
  for (int i = 0; i < MENU_COUNT; i++) {
    setCur(3 + i, 3);
    if (i == menu_idx) { color("7;32"); } else { color("0;37"); }
    terminal.printf(" %-35s", MENU_ITEMS[i]);
    colorReset();
  }
  int srow = 3 + MENU_COUNT + 1;
  drawBox(srow, 1, 40, 24 - srow - 1, "SYSTEM");
  size_t ram   = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t total = SPIFFS.totalBytes();
  size_t used  = SPIFFS.usedBytes();
  scanFiles();
  setCur(srow+2, 3); color("32");
  terminal.printf("RAM:%uKB  SPIFFS:%u/%uKB",
    (unsigned)(ram/1024),(unsigned)(used/1024),(unsigned)(total/1024));
  colorReset();
  setCur(srow+3, 3);
  terminal.printf("Files:%-3d  Uptime:%-6lus  Py:%d",
    (int)file_list.size(), millis()/1000, (int)py_vars_name.size());
  setCur(srow+4, 3); color("36");
  terminal.write("CPU:240MHz  PAL  PS2  BAS+PY");
  colorReset();
  setCur(23, 1); color("44;1;37");
  terminal.printf("%-40s", " CMD> "); colorReset();
  setCur(23, 7); printPrompt();
}

// Только курсор меню — без cls() без мигания!
void drawMenuCursor(int prev, int curr) {
  // Стереть старый
  setCur(3 + prev, 3); color("0;37");
  terminal.printf(" %-35s", MENU_ITEMS[prev]);
  colorReset();
  // Нарисовать новый
  setCur(3 + curr, 3); color("7;32");
  terminal.printf(" %-35s", MENU_ITEMS[curr]);
  colorReset();
  setCur(23, 7);
}


// ═══════════════════════════════════════════════════════
//  ФАЙЛОВЫЙ МЕНЕДЖЕР
// ═══════════════════════════════════════════════════════

void scanFiles() {
  file_list.clear();
  File root = SPIFFS.open("/");
  File f    = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String nm = String(f.name());
      // SPIFFS возвращает "/aq.bas" — убираем слэш для отображения
      if (nm.startsWith("/")) nm = nm.substring(1);
      file_list.push_back(nm);
    }
    f = root.openNextFile();
  }
  std::sort(file_list.begin(), file_list.end());
}

void drawFiles() {
  cls();
  scanFiles();
  int total = (int)file_list.size();

  // Защита курсора
  if (file_cursor >= total) file_cursor = std::max(0, total - 1);
  if (file_cursor < file_scroll) file_scroll = file_cursor;
  if (file_cursor >= file_scroll + FILE_PAGE) file_scroll = file_cursor - FILE_PAGE + 1;

  // Заголовок с номером страницы
  char title[32];
  int pages = (total + FILE_PAGE - 1) / FILE_PAGE;
  int curPage = file_scroll / FILE_PAGE + 1;
  snprintf(title, sizeof(title), "FILES %d/%d  [%d]", curPage, std::max(1,pages), total);
  drawBox(1, 1, 40, 18, title);

  if (total == 0) {
    setCur(4, 3); color("33");
    terminal.write("No files.  'new test.bas'");
    colorReset();
  } else {
    int end = std::min(total, file_scroll + FILE_PAGE);
    for (int i = file_scroll; i < end; i++) {
      setCur(3 + (i - file_scroll), 3);
      if (i == file_cursor) {
        color("7;32");  // выделение курсором
        terminal.printf("  %2d  %-32s", i + 1, file_list[i].c_str());
      } else {
        color("0;36");
        terminal.printf("  %2d  %-32s", i + 1, file_list[i].c_str());
      }
      colorReset();
    }
    // Индикаторы прокрутки
    if (file_scroll > 0) {
      setCur(2, 38); color("33"); terminal.write("↑"); colorReset();
    }
    if (file_scroll + FILE_PAGE < total) {
      setCur(17, 38); color("33"); terminal.write("↓"); colorReset();
    }
  }

  setCur(19, 1); color("90");
  terminal.write("↑↓=Cursor ENTER=edit  run <n>  new <name>  back");
  colorReset();
  printStatusBar("FILE MANAGER");
}

// Только строки курсора — без cls()!
void drawFilesCursor(int prev, int curr) {
  int total = (int)file_list.size();
  // Если нужна прокрутка — перерисуем всё
  bool needScroll = (curr < file_scroll || curr >= file_scroll + FILE_PAGE);
  if (needScroll) { drawFiles(); return; }
  // Иначе только две строки
  if (prev >= file_scroll && prev < file_scroll + FILE_PAGE && prev < total) {
    setCur(3 + (prev - file_scroll), 3); color("0;36");
    terminal.printf("  %2d  %-32s", prev + 1, file_list[prev].c_str());
    colorReset();
  }
  if (curr >= 0 && curr < total) {
    setCur(3 + (curr - file_scroll), 3); color("7;32");
    terminal.printf("  %2d  %-32s", curr + 1, file_list[curr].c_str());
    colorReset();
  }
  setCur(23, 1);
}

// Только строки курсора редактора — без cls()!
void drawEditorCursor(int prev, int curr) {
  int total = (int)editor_lines.size();
  bool needScroll = (curr < editor_scroll || curr >= editor_scroll + EDIT_PAGE);
  if (needScroll) { drawEditor(); return; }
  if (prev >= editor_scroll && prev < editor_scroll + EDIT_PAGE && prev < total) {
    setCur(3 + (prev - editor_scroll), 3); color("0;37");
    terminal.printf("%3d  %-30s", prev + 1, editor_lines[prev].c_str());
    colorReset();
  }
  if (curr >= 0 && curr < total) {
    setCur(3 + (curr - editor_scroll), 3); color("7;32");
    terminal.printf("%3d  %-30s", curr + 1, editor_lines[curr].c_str());
    colorReset();
  }
  setCur(23, 1);
}

// ═══════════════════════════════════════════════════════
//  РЕДАКТОР
// ═══════════════════════════════════════════════════════

void loadFile(const String& fname) {
  editor_lines.clear();
  editor_cursor = 0;
  editor_scroll = 0;
  current_file  = fname;
  if (current_file.indexOf('.') < 0) current_file += ".bas";

  // SPIFFS всегда нужен "/" перед именем
  String spiffsPath = current_file.startsWith("/") ? current_file : "/" + current_file;

  File f = SPIFFS.open(spiffsPath, "r");
  if (f) {
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      // Убираем только \r и хвостовые пробелы — НЕ ведущие (отступы Python!)
      if (ln.endsWith("\r")) ln.remove(ln.length()-1);
      while (ln.endsWith(" ") || ln.endsWith("\t")) ln.remove(ln.length()-1);
      if (ln.length()) editor_lines.push_back(ln);
    }
    f.close();
  }
  if (editor_lines.empty()) editor_lines.push_back("");
}

void drawEditor() {
  cls();
  int total = (int)editor_lines.size();
  char title[40];
  snprintf(title, sizeof(title), "EDITOR %s  %d/%d",
           current_file.c_str(), editor_cursor + 1, total);
  drawBox(1, 1, 40, 20, title);

  int end = std::min(total, editor_scroll + EDIT_PAGE);
  for (int i = editor_scroll; i < end; i++) {
    setCur(3 + (i - editor_scroll), 3);
    if (i == editor_cursor) color("7;32");
    else                    color("0;37");
    terminal.printf("%3d  %-30s", i + 1, editor_lines[i].c_str());
    colorReset();
  }

  // Индикаторы прокрутки
  if (editor_scroll > 0) {
    setCur(2, 38); color("33"); terminal.write("↑"); colorReset();
  }
  if (editor_scroll + EDIT_PAGE < total) {
    setCur(19, 38); color("33"); terminal.write("↓"); colorReset();
  }

  setCur(21, 1); color("90");
  terminal.write("↑↓=string ENTER=Editor. ins del add save ren back");
  colorReset();
  printStatusBar(("EDITOR: " + current_file).c_str());
}

void saveFile(const String& ext) {
  if (ext.length() && !current_file.endsWith("." + ext))
    current_file = current_file.substring(0, current_file.lastIndexOf('.') + 1) + ext;
  current_file = lowerExt(current_file);
  String spiffsPath = current_file.startsWith("/") ? current_file : "/" + current_file;

  File f = SPIFFS.open(spiffsPath, "w");
  if (f) {
    for (auto& ln : editor_lines) f.println(ln);
    f.close();
    terminal.printf("\e[32mSave: %s (%d string)\e[0m\r\n",
                    current_file.c_str(), (int)editor_lines.size());
  } else {
    terminal.write("\e[31mError save!\e[0m\r\n");
  }
  delay(1000);
  drawEditor();
}

// ═══════════════════════════════════════════════════════
//  PYTHON — вынесен в PythonInterpreter.cpp / .h
// ═══════════════════════════════════════════════════════
#include "PythonInterpreter.h"

void drawPython() {
  cls();
  drawBox(1, 1, 40, 3, "Python  REPL");
  color("32");
  terminal.write("Python on ESP32  (print, vars, math)\r\n");
  terminal.write("'exit' — exit  'clear' — Clear\r\n");
  colorReset();
  // Показать историю
  int start = std::max(0, (int)py_history.size() - 15);
  for (int i = start; i < (int)py_history.size(); i++) {
    bool isInput = (i % 2 == 0);  // чередуем input/output
    if (isInput) { color("33"); terminal.print(">>> "); colorReset(); }
    terminal.println(py_history[i]);
  }
  terminal.write("\e[33m>>> \e[0m");
  printStatusBar("PYTHON REPL");
}

// ═══════════════════════════════════════════════════════
//  WiFi AP — загрузка файлов с телефона
// ═══════════════════════════════════════════════════════
void wifiServeRoot() {
  scanFiles();
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>ESP32SUP</title>"
    "<style>body{background:#111;color:#0f0;font-family:monospace;padding:20px}"
    "h2{color:#0ff}a{color:#ff0}"
    "input[type=submit]{background:#333;color:#0f0;border:1px solid #0f0;padding:5px 15px}"
    "table{width:100%}td{padding:3px 8px;border-bottom:1px solid #333}</style></head><body>"
    "<h2>ESP32SUP  Files</h2>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='file'> "
    "<input type='submit' value='Upload'></form><hr><table>";
  for (auto& f : file_list)
    html += "<tr><td>" + f + "</td><td><a href='/delete?f=" + f +
            "' onclick=\"return confirm('Delete?')\">del</a></td></tr>";
  html += "</table><small>RAM:" +
          String(heap_caps_get_free_size(MALLOC_CAP_8BIT)/1024) +
          "KB</small></body></html>";
  webServer.send(200, "text/html", html);
}
File uploadFile;
bool uploadOK = false;
void wifiHandleUpload() {
  HTTPUpload& up = webServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    String fname = up.filename;
    int sl = fname.lastIndexOf('/');
    if (sl >= 0) fname = fname.substring(sl + 1);
    String flow = fname; flow.toLowerCase();
    // Только .bas и .py — защита SPIFFS!
    if (!flow.endsWith(".bas") && !flow.endsWith(".py") && !flow.endsWith(".txt")) {
      uploadOK = false;
      return;
    }
    String fpath = "/" + fname;
    uploadFile = SPIFFS.open(fpath, "w");
    uploadOK   = uploadFile ? true : false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadOK)
      uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    uploadOK = false;
    webServer.sendHeader("Location","/"); webServer.send(303);
  }
}
void wifiHandleDelete() {
  SPIFFS.remove("/" + webServer.arg("f"));
  webServer.sendHeader("Location","/"); webServer.send(303);
}
void drawWifi() {
  cls();
  drawBox(1, 1, 40, 14, "WiFi AP UPLOAD");
  if (!wifiRunning) {
    setCur(4,3);  color("33"); terminal.write("Start WiFi access point?"); colorReset();
    setCur(6,3);  terminal.write("SSID: ESP32SUP");
    setCur(7,3);  terminal.write("PASS: 12345678");
    setCur(9,3);  terminal.write("Then open on phone:");
    setCur(10,3); color("36"); terminal.write("http://192.168.4.1"); colorReset();
    setCur(12,3); color("32"); terminal.write("ENTER=start  ESC=back"); colorReset();
  } else {
    setCur(4,3); color("1;32"); terminal.write("WiFi ACTIVE!"); colorReset();
    setCur(6,3); terminal.write("SSID: ESP32SUP");
    setCur(7,3); terminal.write("PASS: 12345678");
    setCur(8,3); color("36"); terminal.write("http://192.168.4.1"); colorReset();
    setCur(10,3);color("33"); terminal.write("Upload .bas .py from browser!"); colorReset();
    setCur(12,3);color("90"); terminal.write("ESC=stop WiFi + exit"); colorReset();
  }
  printStatusBar(wifiRunning ? "WiFi ON 192.168.4.1" : "WiFi OFF  ENTER=start");
}

// ═══════════════════════════════════════════════════════
//  SOUP PART I  —  Cook it!
//  Текстовая игра: выбирай ингредиенты для супа
//  (c) SOUPEsp32 — DMKS(laic) + Claude/Anthropic
//      + GoogleAI, Cursor, Grok
// ═══════════════════════════════════════════════════════
void runSoup1() {
  cls();
  drawBox(1, 1, 40, 22, "SOUP I  -  Cook it!");

  // Заставка
  setCur(3,3);  color("1;33"); terminal.write("~ SOUPEsp32 ~"); colorReset();
  setCur(4,3);  color("32");   terminal.write("(c) DMKS(laic) + Claude"); colorReset();
  setCur(5,3);  color("90");   terminal.write("  GoogleAI, Cursor, Grok"); colorReset();
  setCur(7,3);  terminal.write("The ESP32 needs SOUP to run!");
  setCur(8,3);  terminal.write("Choose ingredients:");
  setCur(10,3); color("1;36"); terminal.write("1. BASIC  - game engine"); colorReset();
  setCur(11,3); color("1;35"); terminal.write("2. Python - smart logic"); colorReset();
  setCur(12,3); color("1;32"); terminal.write("3. WiFi   - connect world"); colorReset();
  setCur(14,3); color("33");   terminal.write("Add all 3 to make SOUP!"); colorReset();
  setCur(16,3); terminal.write("Type 1, 2 or 3.  ESC=exit");
  printStatusBar("SOUP I  -  Cook it!");
  printPrompt();

  int score = 0;
  bool has[3] = {false, false, false};
  bool playing = true;

  while (playing) {
    fabgl::VirtualKeyItem ki;
    if (!ps2Controller.keyboard()->getNextVirtualKey(&ki, 50)) continue;
    if (!ki.down) continue;

    if (ki.vk == fabgl::VirtualKey::VK_ESCAPE) break;

    char c = ki.ASCII;
    int choice = c - '0';

    if (choice >= 1 && choice <= 3) {
      int idx = choice - 1;
      if (!has[idx]) {
        has[idx] = true;
        score++;
        // Показать добавление
        setCur(18,3);
        color("1;32");
        const char* names[] = {"BASIC","Python","WiFi"};
        terminal.printf("Added: %s  +1 point!", names[idx]);
        colorReset();
        setCur(19,3);
        terminal.printf("Score: %d/3", score);
      } else {
        setCur(18,3); color("33");
        terminal.write("Already in the pot!    ");
        colorReset();
      }

      if (score >= 3) {
        // ПОБЕДА!
        delay(400);
        cls();
        drawBox(3, 3, 34, 16, "SOUP READY!");
        setCur(5,5);  color("1;33"); terminal.write("*** SOUP IS READY! ***"); colorReset();
        setCur(7,5);  color("1;32"); terminal.write("ESP32 is now POWERED!"); colorReset();
        setCur(9,5);  color("36");   terminal.write("SOUPEsp32 v1.0"); colorReset();
        setCur(11,5); terminal.write("(c) DMKS(laic)");
        setCur(12,5); terminal.write("    + Claude/Anthropic");
        setCur(13,5); color("90");
        terminal.write("    GoogleAI,Cursor,Grok"); colorReset();
        setCur(15,5); color("1;33"); terminal.write("BASIC+Python+WiFi = SOUP!"); colorReset();
        setCur(17,5); color("33");   terminal.write("Press any key..."); colorReset();
        // Ждём нажатия
        while (true) {
          if (ps2Controller.keyboard()->getNextVirtualKey(&ki, 100) && ki.down) break;
        }
        break;
      }
    }
    vTaskDelay(1);
  }
}

// ═══════════════════════════════════════════════════════
//  SOUP PART II  —  Catch it!
//  Аркада: собирай буквы S→O→U→P
// ═══════════════════════════════════════════════════════
#define SOUP_W  37
#define SOUP_H  20
#define SOUP_OBJ 8

struct SoupObj { int x, y; char ch; bool active; };
SoupObj soup_objs[SOUP_OBJ];
int  soup_px, soup_py, soup_need, soup_lives, soup_score;
const char* SOUP_SEQ = "SOUP";

void soupPlace(int i) {
  soup_objs[i].x = random(2, SOUP_W);
  soup_objs[i].y = random(3, SOUP_H);
  soup_objs[i].ch = (i < 4) ? SOUP_SEQ[i] : 'X';
  soup_objs[i].active = true;
}
void soupDraw() {
  cls();
  setCur(1,1); color("1;36");
  terminal.printf("SOUP II Score:%-4d Lives:%d Need:[%c]",
                  soup_score, soup_lives, SOUP_SEQ[soup_need]);
  colorReset();
  setCur(2,1); color("36");
  for (int i=0;i<40;i++) terminal.write("-");
  colorReset();
  for (int i=0;i<SOUP_OBJ;i++) {
    if (!soup_objs[i].active) continue;
    setCur(soup_objs[i].y, soup_objs[i].x);
    color(soup_objs[i].ch=='X' ? "1;31" : "1;33");
    char s[2]={soup_objs[i].ch,0}; terminal.write(s); colorReset();
  }
  setCur(soup_py, soup_px); color("1;32"); terminal.write("@"); colorReset();
}
void runSoup2() {
  soup_px=SOUP_W/2; soup_py=SOUP_H/2;
  soup_need=0; soup_lives=3; soup_score=0;
  for (int i=0;i<SOUP_OBJ;i++) soupPlace(i);
  soupDraw();

  while (true) {
    fabgl::VirtualKeyItem ki;
    if (!ps2Controller.keyboard()->getNextVirtualKey(&ki, 0)) { vTaskDelay(1); continue; }
    if (!ki.down) continue;
    if (ki.vk == fabgl::VirtualKey::VK_ESCAPE) break;

    int nx=soup_px, ny=soup_py;
    if (ki.vk==fabgl::VirtualKey::VK_UP)    ny--;
    if (ki.vk==fabgl::VirtualKey::VK_DOWN)  ny++;
    if (ki.vk==fabgl::VirtualKey::VK_LEFT)  nx--;
    if (ki.vk==fabgl::VirtualKey::VK_RIGHT) nx++;
    nx=constrain(nx,2,SOUP_W); ny=constrain(ny,3,SOUP_H);

    setCur(soup_py,soup_px); terminal.write(" ");
    soup_px=nx; soup_py=ny;

    for (int i=0;i<SOUP_OBJ;i++) {
      if (!soup_objs[i].active) continue;
      if (soup_objs[i].x==soup_px && soup_objs[i].y==soup_py) {
        if (soup_objs[i].ch=='X') {
          soup_lives--;
          soup_objs[i].active=false; soupPlace(i);
          setCur(21,1); color("1;31"); terminal.write("OUCH!   "); colorReset();
          if (soup_lives<=0) {
            delay(500); cls();
            setCur(10,5); color("1;31");
            terminal.printf("GAME OVER!  Score:%d", soup_score);
            colorReset();
            setCur(12,5); terminal.write("(c) SOUPEsp32 DMKS+Claude");
            delay(2000); goto soup2_end;
          }
        } else if (soup_objs[i].ch==SOUP_SEQ[soup_need]) {
          soup_score+=10*(soup_need+1);
          soup_need++;
          soup_objs[i].active=false; soupPlace(i);
          if (soup_need>=4) {
            soup_score+=50; soup_need=0;
            setCur(21,1); color("1;32"); terminal.write("SOUP!+50"); colorReset();
            for (int j=0;j<SOUP_OBJ;j++) soupPlace(j);
            delay(500);
          }
        }
      }
    }
    setCur(1,1); color("1;36");
    terminal.printf("SOUP II Score:%-4d Lives:%d Need:[%c]  ",
                    soup_score,soup_lives,SOUP_SEQ[soup_need]);
    colorReset();
    setCur(soup_py,soup_px); color("1;32"); terminal.write("@"); colorReset();
    vTaskDelay(1);
  }
  soup2_end:;
}

// ═══════════════════════════════════════════════════════
//  ОБРАБОТКА КОМАНД
// ═══════════════════════════════════════════════════════

void processCommand(String cmd) {
  // В редакторе trim только для команд, не для кода
  if (ui_state == MODE_EDITOR) {
    String t = cmd; t.trim();
    // Если это команда редактора — trim
    if (t == "save" || t == "back" || t == "add" || t == "del" ||
        t == "ins"  || t == "ren"  || t == "up"  || t == "down" ||
        t == "enter"|| t.startsWith("save ") || t.startsWith("ins ")) {
      cmd = t;
    }
    // Иначе оставляем как есть — это код Python/BASIC с отступом
  } else {
    cmd.trim();
  }
  if (cmd.length() == 0) return;

  // ── ВЕЗДЕ работает 'back' / 'menu' ──
  if (cmd == "back" || cmd == "menu") {
    ui_state = MODE_MENU;
    drawMenu();
    return;
  }

  switch (ui_state) {

    // ── ГЛАВНОЕ МЕНЮ ──────────────────────────────────
    case MODE_MENU:
      if (cmd == "up")   { int p=menu_idx; menu_idx=(menu_idx-1+MENU_COUNT)%MENU_COUNT; drawMenuCursor(p,menu_idx); }
      else if (cmd == "down") { int p=menu_idx; menu_idx=(menu_idx+1)%MENU_COUNT; drawMenuCursor(p,menu_idx); }
      else if (cmd == "enter" || cmd == String(menu_idx + 1)) {
        switch (menu_idx) {
          case 0: ui_state = MODE_FILES;    drawFiles();   break;
          case 1: // Edit — спросить имя
            ui_state = MODE_ENTER_FILENAME;
            cls(); printHeader("ENTER FILENAME");
            terminal.write("Name file (Exs:. test.bas) & 'back':\r\n");
            printPrompt();
            break;
          case 2: ui_state = MODE_FILES;    drawFiles();   break;  // BASIC → выбор файла
          case 3: ui_state = MODE_PYTHON; drawPython(); break;
          case 4: ui_state = MODE_WIFI;   drawWifi();   break;
          case 5:
            ui_state = MODE_SOUP1;
            runSoup1();
            ui_state = MODE_MENU; drawMenu(); break;
          case 6:
            ui_state = MODE_SOUP2;
            runSoup2();
            ui_state = MODE_MENU; drawMenu(); break;
        }
      } else {
        // Цифра напрямую
        int n = cmd.toInt();
        if (n >= 1 && n <= MENU_COUNT) { menu_idx = n - 1; drawMenu(); }
      }
      break;

    // ── ФАЙЛОВЫЙ МЕНЕДЖЕР ─────────────────────────────
    case MODE_FILES:
      if (cmd == "up") {
        int p = file_cursor;
        file_cursor = std::max(0, file_cursor - 1);
        if (file_cursor < file_scroll) file_scroll = file_cursor;
        drawFilesCursor(p, file_cursor);
      } else if (cmd == "down") {
        int p = file_cursor;
        file_cursor = std::min((int)file_list.size() - 1, file_cursor + 1);
        if (file_cursor >= file_scroll + FILE_PAGE) file_scroll = file_cursor - FILE_PAGE + 1;
        drawFilesCursor(p, file_cursor);
      } else if (cmd == "enter") {
        // ENTER на файле — открыть в редакторе
        if (!file_list.empty()) {
          ui_state = MODE_EDITOR;
          loadFile(file_list[file_cursor]);
          drawEditor();
        }
      } else if (cmd.startsWith("new ")) {
        String nm = cmd.substring(4); nm.trim();
        ui_state = MODE_EDITOR;
        loadFile(nm);
        drawEditor();
      } else if (cmd.startsWith("run ")) {
        int n = cmd.substring(4).toInt();
        if (n > 0 && n <= (int)file_list.size()) {
          String fname = file_list[n-1];
          String fpath = fname.startsWith("/") ? fname : "/" + fname;
          String flow = fname; flow.toLowerCase();
          if (flow.endsWith(".bas")) {
            ui_state = MODE_BASIC;
            cls(); printHeader("BASIC");
            run_basic_program(fpath);
            terminal.write("\r\n\e[33mPlis eny key...\e[0m");
          } else if (flow.endsWith(".py")) {
            cls(); printHeader("Python");
            pyRun(fpath);
            terminal.write("\r\n\e[33mPlis eny key...\e[0m");
          } else {
            terminal.write("\e[31m .bas & .py!\e[0m\r\n");
          }
        } else {
          terminal.write("\e[31mNo file!\e[0m\r\n");
        }
      } else if (cmd.startsWith("edit ")) {
        int n = cmd.substring(5).toInt();
        if (n > 0 && n <= (int)file_list.size()) {
          ui_state = MODE_EDITOR;
          loadFile(file_list[n-1]);
          drawEditor();
        }
      } else {
        // просто номер
        int n = cmd.toInt();
        if (n > 0 && n <= (int)file_list.size()) {
          ui_state = MODE_EDITOR;
          loadFile(file_list[n-1]);
          drawEditor();
        }
      }
      break;

    // ── ВВОД ИМЕНИ ФАЙЛА ──────────────────────────────
    case MODE_ENTER_FILENAME:
      current_file = lowerExt(cmd);
      ui_state = MODE_EDITOR;
      loadFile(current_file);
      drawEditor();
      break;

    // ── РЕДАКТОР ──────────────────────────────────────
    case MODE_EDITOR:
      if (cmd == "save") {
        // Спрашиваем расширение
        ui_state = MODE_SAVE_EXT;
        cls(); printHeader("Save all");
        terminal.printf("File: %s\r\n", current_file.c_str());
        terminal.write(": bas  py  skip  back\r\n");
        printPrompt();
      } else if (cmd == "up") {
        int p = editor_cursor;
        editor_cursor = std::max(0, editor_cursor - 1);
        if (editor_cursor < editor_scroll) editor_scroll = editor_cursor;
        drawEditorCursor(p, editor_cursor);
      } else if (cmd == "down") {
        int p = editor_cursor;
        editor_cursor = std::min((int)editor_lines.size()-1, editor_cursor+1);
        if (editor_cursor >= editor_scroll + EDIT_PAGE) editor_scroll = editor_cursor - EDIT_PAGE + 1;
        drawEditorCursor(p, editor_cursor);
      } else if (cmd == "add") {
        editor_lines.push_back("");
        editor_cursor = editor_lines.size() - 1;
        // Прокрутить к новой строке
        if (editor_cursor >= editor_scroll + EDIT_PAGE) editor_scroll = editor_cursor - EDIT_PAGE + 1;
        drawEditor();
      } else if (cmd == "del") {
        if (editor_lines.size() > 1) {
          editor_lines.erase(editor_lines.begin() + editor_cursor);
          editor_cursor = std::min(editor_cursor, (int)editor_lines.size() - 1);
          if (editor_cursor < editor_scroll) editor_scroll = editor_cursor;
        }
        drawEditor();

      } else if (cmd == "ins" || cmd.startsWith("ins ")) {
        // ins        — вставить после текущей строки
        // ins 20     — вставить после строки 20
        int pos = editor_cursor + 1;
        if (cmd.startsWith("ins ")) {
          int n = cmd.substring(4).toInt();
          if (n > 0 && n <= (int)editor_lines.size())
            pos = n; // после строки N
        }
        pos = constrain(pos, 0, (int)editor_lines.size());
        editor_lines.insert(editor_lines.begin() + pos, String(""));
        editor_cursor = pos;
        if (editor_cursor >= editor_scroll + EDIT_PAGE)
          editor_scroll = editor_cursor - EDIT_PAGE + 1;
        if (editor_cursor < editor_scroll)
          editor_scroll = editor_cursor;
        drawEditor();
        setCur(22, 1); color("1;33");
        terminal.printf(" ins: line %d ready - type content + ENTER ", pos+1);
        colorReset();
        printPrompt();

      } else if (cmd == "ren") {
        // Перенумеровать BASIC строки: 10, 20, 30...
        for (int i = 0; i < (int)editor_lines.size(); i++) {
          String ln = editor_lines[i];
          // Найти старый номер строки (до первого пробела)
          int sp = ln.indexOf(' ');
          if (sp > 0) {
            String rest = ln.substring(sp); // " PRINT ..." 
            editor_lines[i] = String((i + 1) * 10) + rest;
          } else {
            // Строка без номера — добавить
            editor_lines[i] = String((i + 1) * 10) + " " + ln;
          }
        }
        drawEditor();

      } else {
        editor_lines[editor_cursor] = cmd;
        drawEditor();
      }
      break;

    // ── СОХРАНЕНИЕ — РАСШИРЕНИЕ ───────────────────────
    case MODE_SAVE_EXT:
      if (cmd == "bas" || cmd == "py" || cmd == "skip") {
        saveFile(cmd == "skip" ? "" : cmd);
        ui_state = MODE_EDITOR;
      }
      break;

    // ── BASIC — ждём любой клавиши ────────────────────
    case MODE_BASIC:
      ui_state = MODE_FILES;
      drawFiles();
      break;

    // ── PYTHON REPL ───────────────────────────────────
    case MODE_PYTHON:
      if (cmd == "clear") {
        pyClear();
        drawPython();
        return;
      }
      if (cmd == "vars") {
        for (int i = 0; i < (int)py_vars_name.size(); i++)
          terminal.printf("\e[33m%s\e[0m = %g\r\n", py_vars_name[i].c_str(), py_vars_val[i]);
        for (int i = 0; i < (int)py_vars_str_name.size(); i++)
          terminal.printf("\e[33m%s\e[0m = \"%s\"\r\n", py_vars_str_name[i].c_str(), py_vars_str_val[i].c_str());
        terminal.write("\e[33m>>> \e[0m");
        return;
      }
      if (cmd.startsWith("run ")) {
        String fname = cmd.substring(4); fname.trim();
        pyRun(fname.startsWith("/") ? fname : "/" + fname);
        terminal.write("\e[33m>>> \e[0m");
        return;
      }
      terminal.write("\r\n");
      pyExec(cmd);
      terminal.write("\e[33m>>> \e[0m");
      printStatusBar("PYTHON  clear/vars/run <file>/back");
      break;

    // ── WiFi ──────────────────────────────────────────────
    case MODE_WIFI:
      if (cmd == "enter" && !wifiRunning) {
        WiFi.softAP(AP_SSID, AP_PASS);
        webServer.on("/", HTTP_GET, wifiServeRoot);
        webServer.on("/upload", HTTP_POST,
          [](){webServer.sendHeader("Location","/");webServer.send(303);},
          wifiHandleUpload);
        webServer.on("/delete", HTTP_GET, wifiHandleDelete);
        webServer.begin();
        wifiRunning = true;
        drawWifi();
      }
      break;

    default: break;
  }
}

// ═══════════════════════════════════════════════════════
//  ЧТЕНИЕ PS2 КЛАВИАТУРЫ  — без блокировки
// ═══════════════════════════════════════════════════════

void handleKey(fabgl::VirtualKeyItem& item) {
  auto vk = item.vk;

  if (!item.down) return;

  // ── Служебные ─────────────────────────────────────
  if (vk == fabgl::VirtualKey::VK_RETURN ||
      vk == fabgl::VirtualKey::VK_KP_ENTER) {
    // ── BASIC ждёт INPUT ──────────────────────
    if (waitingForInput) {
      terminal.write("\r\n");
      if (inputVarName.endsWith("$"))
        basic_vars_str[inputVarName] = inputLine;
      else
        basic_vars_num[inputVarName] = inputLine.toFloat();
      inputLine        = "";
      waitingForInput  = false;
      basic_run();
      return;
    }
    // ── Обычный ввод ─────────────────────────
    terminal.write("\r\n");
    lastEnterPressed = true;
    if (inputLine.length() == 0) {
      processCommand("enter");
    } else {
      processCommand(inputLine);
      inputLine = "";
    }
    lastEnterPressed = false;
    return;
  }

  if (vk == fabgl::VirtualKey::VK_BACKSPACE) {
    if (inputLine.length() > 0) {
      inputLine.remove(inputLine.length() - 1);
      terminal.write("\b \b");
    }
    return;
  }

  if (vk == fabgl::VirtualKey::VK_ESCAPE) {
    inputLine = "";
    ui_state  = MODE_MENU;
    drawMenu();
    return;
  }

  // ── Стрелки UP/DOWN ────────────────────────
  if (vk == fabgl::VirtualKey::VK_UP) {
    if (basic_running) lastKey += 'w';
    else { processCommand("up"); return; }
    return;
  }
  if (vk == fabgl::VirtualKey::VK_DOWN) {
    if (basic_running) lastKey += 's';
    else { processCommand("down"); return; }
    return;
  }
  if (vk == fabgl::VirtualKey::VK_LEFT) {
    if (basic_running) lastKey += 'a';
    return;
  }
  if (vk == fabgl::VirtualKey::VK_RIGHT) {
    if (basic_running) lastKey += 'd';
    return;
  }

  // TAB — зарезервирован
  if (vk == fabgl::VirtualKey::VK_TAB) { return; }

  // ── Печатаемый символ ──────────────────────────────
  if (item.ASCII >= 0x20 && item.ASCII < 0x7F) {
    char c = (char)item.ASCII;
    if (basic_running) {
      lastKey += c;   // BASIC INKEY$ читает отсюда
    } else {
      inputLine += c;
      terminal.write(c);
    }
  }

  vTaskDelay(1);
}

// ═══════════════════════════════════════════════════════
//  SETUP / LOOP
// ═══════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);

  // 1. Видео — сначала!
  DisplayController.begin(VIDEOOUT_GPIO);
  DisplayController.setResolution("P-PAL-B");

  // 2. Терминал
  terminal.begin(&DisplayController);
  terminal.write("\e[40;37m\e[2J\e[H");   // чёрный фон, белый текст
  terminal.println("ESP32SUP  BOOTING...");

  // 3. SPIFFS
  if (!SPIFFS.begin(true)) {
    terminal.println("\e[31mSPIFFS ERROR!\e[0m");
    delay(2000);
  } else {
    terminal.println("SPIFFS  OK");
  }

  // 4. Клавиатура
  delay(200);
  ps2Controller.begin(PS2Preset::KeyboardPort0);
  terminal.println("PS2 KEYBOARD  OK");

  delay(500);

  // 5. Главное меню
  drawMenu();
  printPrompt();
}

void loop() {
  fabgl::VirtualKeyItem item;

  if (ps2Controller.keyboard()->getNextVirtualKey(&item, 0)) {
    // ESC из WiFi — выключить
    if (item.down && item.vk == fabgl::VirtualKey::VK_ESCAPE
        && ui_state == MODE_WIFI) {
      if (wifiRunning) {
        webServer.stop();
        WiFi.softAPdisconnect(true);
        wifiRunning = false;
      }
      ui_state = MODE_MENU;
      drawMenu();
    } else {
      handleKey(item);
    }
  }

  if (wifiRunning) webServer.handleClient();

  vTaskDelay(1);
}
