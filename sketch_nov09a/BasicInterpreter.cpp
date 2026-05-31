#include "BasicInterpreter.h"
#include <SPIFFS.h>      // SD вместо SPIFFS
#include <stack>
#include <vector>

std::stack<int> for_stack;

#define MAX_STACK 20
struct BasicArray {
    String name;
    int size;
    float* data;
};

// Строковые массивы: DIM A$(10)
struct BasicStrArray {
    String name;
    int size;
    String* data;
};

extern int ui_state;
extern String lineAccumulator;
#define RUN_BASIC 3

std::vector<BasicArray>    basic_arrays;
std::vector<BasicStrArray> basic_str_arrays;

String basic_parse_print_arg(String arg);

// Глобальные переменные
std::vector<String> basic_lines;
std::map<String, float> basic_vars_num;
std::map<String, String> basic_vars_str;
int basic_pc = 0;
int basic_stack[10];
int basic_stack_ptr = 0;
bool basic_running = false;
bool waitingForInput = false;
String lastInput = "";
String inputVarName = "";
String lastKey = "";

// Вспомогательная функция вывода строки в терминал
void term_println(String s) {
  terminal.write((s + "\r\n").c_str());
}
void term_print(String s) {
  terminal.write(s.c_str());
}

// Поиск строки по номеру
int basic_find_line_idx(int target_line_num) {
    for (int i = 0; i < (int)basic_lines.size(); i++) {
        String line = basic_lines[i];
        line.trim();
        if (line.startsWith(String(target_line_num) + " ") || line == String(target_line_num)) {
            return i;
        }
    }
    Serial.println("BASIC ERROR: Line " + String(target_line_num) + " not found!");
    return -1;
}




void basic_run() {

  if (basic_lines.empty()) {
    term_println("BASIC: No lines loaded!");
    return;
  }

  if (!basic_running) {
    basic_pc = 0;
    basic_vars_num.clear();
    basic_vars_str.clear();
    basic_stack_ptr = 0;
    basic_running = true;
  }

  while (basic_running && basic_pc < (int)basic_lines.size()) {
    String line = basic_lines[basic_pc];

    int space_idx = line.indexOf(' ');
    if (space_idx == -1) {
      basic_pc++;
      continue;
    }

    String temp_cmd = line.substring(space_idx + 1);
    temp_cmd.trim();
    String cmd = temp_cmd;

    // PRINT
    if (cmd.startsWith("PRINT ")) {
      String temp_arg = cmd.substring(6);
      temp_arg.trim();
      String arg = temp_arg;
      String output;
      if (arg.startsWith("\"") && arg.endsWith("\"")) {
        output = arg.substring(1, arg.length() - 1);
      } else if (arg.startsWith("'") && arg.endsWith("'")) {
        output = arg.substring(1, arg.length() - 1);
      } else if (arg.indexOf('$') >= 0) {
        // Строковая переменная или строковый массив A$(i)
        output = basic_eval_expr_str(arg);
      } else {
        output = String(basic_eval_expr(arg));
        // Убрать .00 для целых чисел
        float v = basic_eval_expr(arg);
        if (v == (int)v) output = String((int)v);
      }
      term_println(output);
      basic_pc++;
      continue;
    }

    // DIM
    else if (cmd.startsWith("DIM ")) {
        int openB = cmd.indexOf('(');
        int closeB = cmd.lastIndexOf(')');
        if (openB != -1 && closeB != -1) {
            String name = cmd.substring(4, openB);
            name.trim();
            String dims = cmd.substring(openB + 1, closeB);
            int comma = dims.indexOf(',');

            if (name.endsWith("$")) {
                // Строковый массив A$(n)
                int size = (int)basic_eval_expr(dims);
                for (int i = 0; i < (int)basic_str_arrays.size(); i++) {
                    if (basic_str_arrays[i].name == name) {
                        delete[] basic_str_arrays[i].data;
                        basic_str_arrays.erase(basic_str_arrays.begin() + i);
                        break;
                    }
                }
                BasicStrArray newArr;
                newArr.name = name;
                newArr.size = size + 1;
                newArr.data = new String[newArr.size];
                for (int i = 0; i < newArr.size; i++) newArr.data[i] = "";
                basic_str_arrays.push_back(newArr);
            } else if (comma >= 0) {
                // Двумерный массив A(rows, cols)
                int rows = (int)basic_eval_expr(dims.substring(0, comma)) + 1;
                int cols = (int)basic_eval_expr(dims.substring(comma + 1)) + 1;
                // Храним как одномерный: idx = row * cols + col
                // Имя с суффиксом "_C" для хранения cols
                String sizeName = name + "_C";
                basic_vars_num[sizeName] = cols;
                for (int i = 0; i < (int)basic_arrays.size(); i++) {
                    if (basic_arrays[i].name == name) {
                        delete[] basic_arrays[i].data;
                        basic_arrays.erase(basic_arrays.begin() + i);
                        break;
                    }
                }
                BasicArray newArr;
                newArr.name = name;
                newArr.size = rows * cols;
                newArr.data = new float[newArr.size];
                for (int i = 0; i < newArr.size; i++) newArr.data[i] = 0.0f;
                basic_arrays.push_back(newArr);
            } else {
                // Одномерный числовой массив
                int size = (int)basic_eval_expr(dims);
                for (int i = 0; i < (int)basic_arrays.size(); i++) {
                    if (basic_arrays[i].name == name) {
                        delete[] basic_arrays[i].data;
                        basic_arrays.erase(basic_arrays.begin() + i);
                        break;
                    }
                }
                BasicArray newArr;
                newArr.name = name;
                newArr.size = size + 1;
                newArr.data = new float[newArr.size];
                for (int i = 0; i < newArr.size; i++) newArr.data[i] = 0.0f;
                basic_arrays.push_back(newArr);
            }
        }
        basic_pc++;
        continue;
    }

    // Массив A(i) = val  или  A(i,j) = val  или  A$(i) = "text"
    // Проверяем ДО LET — отдельно, не в else if цепочке
    {
    String array_cmd = cmd;
    if (array_cmd.startsWith("LET ")) array_cmd = array_cmd.substring(4);
    int openB2 = array_cmd.indexOf('(');
    int eq2    = array_cmd.indexOf('=');
    if (openB2 != -1 && eq2 != -1 && openB2 < eq2) {
        int closeB2  = array_cmd.indexOf(')');
        String aName2 = array_cmd.substring(0, openB2); aName2.trim();
        String idxStr = array_cmd.substring(openB2 + 1, closeB2);
        String valStr = array_cmd.substring(eq2 + 1); valStr.trim();
        bool found2 = false;

        if (aName2.endsWith("$")) {
            int idx = (int)basic_eval_expr(idxStr);
            for (auto &arr : basic_str_arrays) {
                if (arr.name == aName2) {
                    if (idx >= 0 && idx < arr.size) {
                        if (valStr.startsWith("\"") || valStr.startsWith("'"))
                            arr.data[idx] = valStr.substring(1, valStr.length()-1);
                        else arr.data[idx] = basic_eval_expr_str(valStr);
                    } else term_println("ERROR: StrArray out of bounds");
                    found2 = true; break;
                }
            }
        } else {
            int idx = 0;
            int comma2 = idxStr.indexOf(',');
            if (comma2 >= 0) {
                int row = (int)basic_eval_expr(idxStr.substring(0, comma2));
                int col = (int)basic_eval_expr(idxStr.substring(comma2+1));
                int cols = basic_vars_num.count(aName2+"_C") ? (int)basic_vars_num[aName2+"_C"] : 1;
                idx = row * cols + col;
            } else {
                idx = (int)basic_eval_expr(idxStr);
            }
            float val2 = basic_eval_expr(valStr);
            for (auto &arr : basic_arrays) {
                if (arr.name == aName2) {
                    if (idx >= 0 && idx < arr.size) arr.data[idx] = val2;
                    else term_println("ERROR: Array out of bounds");
                    found2 = true; break;
                }
            }
        }
        if (found2) { basic_pc++; continue; }
    }
    } // end array block

    // LET
    if (cmd.startsWith("LET ")) {
        int eq_idx = cmd.indexOf('=');
        if (eq_idx != -1) {
            String var = cmd.substring(4, eq_idx); var.trim();
            String val = cmd.substring(eq_idx + 1); val.trim();
            if (var.endsWith("$")) {
                basic_vars_str[var] = val.startsWith("\"") ? val.substring(1, val.length()-1) : basic_eval_expr_str(val);
            } else {
                basic_vars_num[var] = basic_eval_expr(val);
                basic_pc++;
                continue;
            }
        }
    }

    // IF
    else if (cmd.startsWith("IF ")) {
        int split_idx = cmd.indexOf(" THEN ");
        int offset = 6;
        if (split_idx == -1) { split_idx = cmd.indexOf(" GOTO "); offset = 1; }
        if (split_idx != -1) {
            String cond = cmd.substring(3, split_idx); cond.trim();
            String then_part = cmd.substring(split_idx + offset); then_part.trim();
            float cond_val = basic_eval_condition(cond);
            if (cond_val != 0) {
                if (then_part.startsWith("GOTO ")) {
                    int target = then_part.substring(5).toInt();
                    int new_pc = basic_find_line_idx(target);
                    if (new_pc != -1) { basic_pc = new_pc; continue; }
                } else if (then_part.startsWith("PRINT ")) {
                    term_println(basic_parse_print_arg(then_part.substring(6)));
                } else if (then_part.startsWith("LET ")) {
                    int eq_idx = then_part.indexOf('=');
                    if (eq_idx != -1) {
                        String var = then_part.substring(4, eq_idx); var.trim();
                        basic_vars_num[var] = basic_eval_expr(then_part.substring(eq_idx + 1));
                    }
                }
            }
        }
        basic_pc++;
        continue;
    }

    // FOR
    else if (cmd.startsWith("FOR ")) {
        int eq_idx = cmd.indexOf('=');
        int to_idx = cmd.indexOf(" TO ");
        if (eq_idx != -1 && to_idx != -1) {
            String var_str = cmd.substring(4, eq_idx); var_str.trim();
            String from_val = cmd.substring(eq_idx + 1, to_idx); from_val.trim();
            String to_val = cmd.substring(to_idx + 4); to_val.trim();
            if (for_stack.empty() || for_stack.top() != basic_pc) {
                basic_vars_num[var_str] = basic_eval_expr(from_val);
                basic_vars_num[var_str + "_TO"] = basic_eval_expr(to_val);
                for_stack.push(basic_pc);
            }
        }
    }

    // NEXT
    else if (cmd.startsWith("NEXT ")) {
        String var_str = cmd.substring(5); var_str.trim();
        if (basic_vars_num.count(var_str)) {
            basic_vars_num[var_str] += 1;
            if (basic_vars_num[var_str] <= basic_vars_num[var_str + "_TO"]) {
                basic_pc = for_stack.top();
                continue;
            } else {
                for_stack.pop();
                basic_vars_num.erase(var_str + "_TO");
                basic_pc++;
                continue;
            }
        } else {
            basic_pc++;
        }
    }

    // GOTO
    else if (cmd.startsWith("GOTO ")) {
        int target = cmd.substring(5).toInt();
        int next_pc = basic_find_line_idx(target);
        if (next_pc != -1) { basic_pc = next_pc; continue; }
    }

    // GOSUB
    else if (cmd.startsWith("GOSUB ")) {
        int target = cmd.substring(6).toInt();
        int next_pc = basic_find_line_idx(target);
        if (next_pc != -1) {
            if (basic_stack_ptr < MAX_STACK) {
                basic_stack[basic_stack_ptr++] = basic_pc + 1;
                basic_pc = next_pc;
                continue;
            } else {
                term_println("ERROR: Stack overflow");
            }
        }
    }

    // RETURN
    else if (cmd == "RETURN") {
        if (basic_stack_ptr > 0) {
            basic_pc = basic_stack[--basic_stack_ptr];
            continue;
        } else {
            term_println("ERROR: Return without Gosub");
        }
    }

    // END
    else if (cmd == "END") {
        basic_running = false;
        term_println("READY.");
        return;
    }

    // INPUT
    else if (cmd.startsWith("INPUT ")) {
        inputVarName = cmd.substring(6); inputVarName.trim();
        term_print("? ");
        waitingForInput = true;
        basic_pc++;
        return;
    }

    // CLS
    else if (cmd == "CLS") {
        terminal.clear();
        basic_pc++;
        continue;
    }

    // LOCATE
    else if (cmd.startsWith("LOCATE ")) {
        String args = cmd.substring(7);
        int commaPos = args.indexOf(',');
        if (commaPos != -1) {
            String xStr = args.substring(0, commaPos); xStr.trim();
            String yStr = args.substring(commaPos + 1); yStr.trim();
            int x = (int)basic_eval_expr(xStr);
            int y = (int)basic_eval_expr(yStr);
            String locateCmd = "\033[" + String(y) + ";" + String(x) + "H";
            term_print(locateCmd);
        }
        basic_pc++;
        continue;
    }

    // DELAY
    else if (cmd.startsWith("DELAY ")) {
        int ms = (int)basic_eval_expr(cmd.substring(6));
        unsigned long start = millis();
        while (millis() - start < ms) {
            // ── Читаем клавиатуру прямо здесь! ──
            fabgl::VirtualKeyItem ki;
            if (ps2Controller.keyboard()->getNextVirtualKey(&ki, 0)) {
                if (ki.down && ki.ASCII >= 0x20 && ki.ASCII < 0x7F) {
                    lastKey += (char)ki.ASCII;
                }
                // Стрелки → WASD для BASIC игр
                if (ki.down) {
                    if (ki.vk == fabgl::VirtualKey::VK_UP)    lastKey += 'w';
                    if (ki.vk == fabgl::VirtualKey::VK_DOWN)  lastKey += 's';
                    if (ki.vk == fabgl::VirtualKey::VK_LEFT)  lastKey += 'a';
                    if (ki.vk == fabgl::VirtualKey::VK_RIGHT) lastKey += 'd';
                }
            }
            yield();
            delay(1);
        }
        basic_pc++;
        continue;
    }

    // Присваивание A = expr
    else if (cmd.indexOf('=') != -1) {
        int assign_idx = cmd.indexOf('=');
        String var_name = cmd.substring(0, assign_idx); var_name.trim();
        String expression = cmd.substring(assign_idx + 1); expression.trim();
        basic_vars_num[var_name] = basic_eval_expr(expression);
    }

    // Неизвестная команда
    else {
        Serial.println("BASIC: Unknown cmd '" + cmd + "'");
    }

    basic_pc++;
    yield();
  }

  if (basic_pc >= (int)basic_lines.size()) {
    basic_running = false;
    term_println("READY.");
  }
}

float basic_eval_expr(String expr) {
  expr.trim();
  if (expr.length() == 0) return 0;

  // RND
  if (expr.startsWith("RND(") && expr.endsWith(")")) {
    int limit = (int)basic_eval_expr(expr.substring(4, expr.length()-1));
    if (limit < 1) limit = 1;
    return (float)(random(limit) + 1);
  }

  // Массив одномерный A(i) или двумерный A(i,j)
  int openBracket = expr.indexOf('(');
  if (openBracket != -1 && expr.endsWith(")")) {
    String aName = expr.substring(0, openBracket); aName.trim();
    int closeBracket = expr.lastIndexOf(')');
    String idxStr = expr.substring(openBracket+1, closeBracket);
    int comma = idxStr.indexOf(',');
    int index = 0;
    if (comma >= 0) {
      // Двумерный: A(row, col)
      int row = (int)basic_eval_expr(idxStr.substring(0, comma));
      int col = (int)basic_eval_expr(idxStr.substring(comma+1));
      int cols = (int)basic_vars_num.count(aName+"_C") ? (int)basic_vars_num[aName+"_C"] : 1;
      index = row * cols + col;
    } else {
      index = (int)basic_eval_expr(idxStr);
    }
    for (auto &arr : basic_arrays) {
      if (arr.name == aName && index >= 0 && index < arr.size) return arr.data[index];
    }
    return 0;
  }

  // Сложение / вычитание
  int plus_idx = expr.lastIndexOf('+');
  int minus_idx = expr.lastIndexOf('-');
  if (plus_idx != -1 || (minus_idx > 0)) {
    if (plus_idx > minus_idx)
      return basic_eval_expr(expr.substring(0, plus_idx)) + basic_eval_expr(expr.substring(plus_idx+1));
    else if (minus_idx > 0)
      return basic_eval_expr(expr.substring(0, minus_idx)) - basic_eval_expr(expr.substring(minus_idx+1));
  }

  // Умножение / деление
  int mul_idx = expr.lastIndexOf('*');
  int div_idx = expr.lastIndexOf('/');
  if (mul_idx != -1 || div_idx != -1) {
    if (mul_idx > div_idx)
      return basic_eval_expr(expr.substring(0, mul_idx)) * basic_eval_expr(expr.substring(mul_idx+1));
    else {
      float d = basic_eval_expr(expr.substring(div_idx+1));
      return (d != 0) ? basic_eval_expr(expr.substring(0, div_idx)) / d : 0;
    }
  }

  // INKEY$
  if (expr == "INKEY$") {
    if (lastKey.length() > 0) {
      char c = lastKey[0];
      lastKey.remove(0, 1);
      return (float)c;
    }
    return 0;
  }

  // Переменная
  if (basic_vars_num.count(expr)) return basic_vars_num[expr];

  return expr.toFloat();
}

String basic_eval_expr_str(String expr) {
  expr.trim();
  // Строковый массив A$(i)
  int ob = expr.indexOf('(');
  if (ob > 0 && expr.endsWith(")") && expr.indexOf('$') >= 0) {
    String aName = expr.substring(0, ob); aName.trim();
    int idx = (int)basic_eval_expr(expr.substring(ob+1, expr.length()-1));
    for (auto &arr : basic_str_arrays)
      if (arr.name == aName && idx >= 0 && idx < arr.size) return arr.data[idx];
    return "";
  }
  if (basic_vars_str.count(expr)) return basic_vars_str[expr];
  if (expr.startsWith("\"") && expr.endsWith("\""))
    return expr.substring(1, expr.length()-1);
  if (expr.startsWith("'") && expr.endsWith("'"))
    return expr.substring(1, expr.length()-1);
  return expr;
}

float basic_eval_condition(String cond) {
    cond.trim();
    String op = ""; int opPos = -1; int opLen = 0;
    if (cond.indexOf("==") != -1) { op="=="; opPos=cond.indexOf("=="); opLen=2; }
    else if (cond.indexOf("<=") != -1) { op="<="; opPos=cond.indexOf("<="); opLen=2; }
    else if (cond.indexOf(">=") != -1) { op=">="; opPos=cond.indexOf(">="); opLen=2; }
    else if (cond.indexOf("<") != -1) { op="<"; opPos=cond.indexOf("<"); opLen=1; }
    else if (cond.indexOf(">") != -1) { op=">"; opPos=cond.indexOf(">"); opLen=1; }
    else if (cond.indexOf("=") != -1) { op="="; opPos=cond.indexOf("="); opLen=1; }
    if (opPos == -1) return (basic_eval_expr(cond) != 0.0f) ? 1.0f : 0.0f;
    String leftStr = cond.substring(0, opPos); leftStr.trim();
    String rightStr = cond.substring(opPos + opLen); rightStr.trim();
    float lval = basic_eval_expr(leftStr);
    float rval = basic_eval_expr(rightStr);
    if (op == "==" || op == "=") return (lval == rval) ? 1.0f : 0.0f;
    if (op == "<") return (lval < rval) ? 1.0f : 0.0f;
    if (op == ">") return (lval > rval) ? 1.0f : 0.0f;
    if (op == "<=") return (lval <= rval) ? 1.0f : 0.0f;
    if (op == ">=") return (lval >= rval) ? 1.0f : 0.0f;
    return 0.0f;
}

// Загрузка программы с SD карты
void run_basic_program(const String& filename) {
  basic_lines.clear();

  // Ищем файл на SD
    String fpath = filename.startsWith("/") ? filename : "/" + filename;
    File file = SPIFFS.open(fpath, "r");
  if (file) {
    while (file.available()) {
      String line_raw = file.readStringUntil('\n');
      line_raw.trim();
      if (line_raw.length() > 0) basic_lines.push_back(line_raw);
    }
    file.close();
    Serial.println("Loaded: " + filename + " lines: " + String(basic_lines.size()));
  } else {
    term_println("File not found: " + filename);
    return;
  }

  if (basic_lines.empty()) {
    term_println("Empty file!");
    return;
  }

  basic_pc = 0;
  basic_running = false;
  basic_vars_num.clear();
  basic_vars_str.clear();
  lastKey = "";

  term_println("Running: " + filename);
  basic_run();
  term_println("READY.");
}

String basic_parse_print_arg(String arg) {
    arg.trim();
    if (arg.startsWith("\"") && arg.endsWith("\""))
        return arg.substring(1, arg.length()-1);
    return String(basic_eval_expr(arg));
}
