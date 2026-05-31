#include "PythonInterpreter.h"
#include <SPIFFS.h>
#include <math.h>

//-----------------------------------------------
//  ФИКС
//-----------------------------------------------

bool py_waiting_input = false; // Флаг: ждет ли интерпретатор ввод с экрана
String py_input_var = "";     // Имя переменной, куда запишем текст (например, "x")
String py_input_buffer = "";  // Сюда будем копить символы по мере нажатия клавиш





// ═══════════════════════════════════════════════
//  Переменные
// ═══════════════════════════════════════════════
std::vector<String> py_vars_name;
std::vector<float>  py_vars_val;
std::vector<String> py_vars_str_name;
std::vector<String> py_vars_str_val;
std::vector<String> py_history;

// ─── List storage ──────────────────────────
struct PyList {
  String name;
  std::vector<float>  nums;
  std::vector<String> strs;
  bool isStr; // тип элементов
};
std::vector<PyList> py_lists;

// ─── Dict storage ──────────────────────────
struct PyDict {
  String name;
  std::vector<String> keys;
  std::vector<String> vals;
};
std::vector<PyDict> py_dicts;

// ─── Def (функции) storage ─────────────────
struct PyFunc {
  String name;
  std::vector<String> params;
  std::vector<String> body;  // строки тела
};
std::vector<PyFunc> py_funcs;

// ─── Forward declarations ──────────────────
PyList* pyGetList(const String& name);
PyList& pyMakeList(const String& name);
PyDict* pyGetDict(const String& name);
PyDict& pyMakeDict(const String& name);

// ─── Программный режим (.py файлы) ────────────
std::vector<String> py_program;
bool  py_prog_running = false;
int   py_prog_pc      = 0;

// ─── for-цикл ──────────────────────────────────
String py_for_var  = "";
float  py_for_to   = 0;
float  py_for_step = 1;
int    py_for_line = 0;
bool   py_in_for   = false;

// ─── if-блок состояние ─────────────────────────
bool py_skip_block = false;
bool py_if_was_true = false;

// ═══════════════════════════════════════════════
//  Вспомогательный вывод
// ═══════════════════════════════════════════════
void py_print(const String& s) {
  terminal.write((s + "\r\n").c_str());
}
void py_out(const String& s) {
  terminal.write(s.c_str());
}


// ═══════════════════════════════════════════════
//  Переменные числовые
// ═══════════════════════════════════════════════
float pyGetVar(const String& n) {
  for (int i = 0; i < (int)py_vars_name.size(); i++)
    if (py_vars_name[i] == n) return py_vars_val[i];
  return 0;
}
void pySetVar(const String& n, float v) {
  for (int i = 0; i < (int)py_vars_name.size(); i++)
    if (py_vars_name[i] == n) { py_vars_val[i] = v; return; }
  py_vars_name.push_back(n);
  py_vars_val.push_back(v);
}

// ─── Строковые переменные ──────────────────────
String pyGetStrVar(const String& n) {
  for (int i = 0; i < (int)py_vars_str_name.size(); i++)
    if (py_vars_str_name[i] == n) return py_vars_str_val[i];
  return "";
}
void pySetStrVar(const String& n, const String& v) {
  for (int i = 0; i < (int)py_vars_str_name.size(); i++)
    if (py_vars_str_name[i] == n) { py_vars_str_val[i] = v; return; }
  py_vars_str_name.push_back(n);
  py_vars_str_val.push_back(v);
}
bool pyIsStrVar(const String& n) {
  for (auto& s : py_vars_str_name) if (s == n) return true;
  return false;
}
//часть 1
// ═══════════════════════════════════════════════
//  Вычисление строковых выражений
// ═══════════════════════════════════════════════
// --- СНАЧАЛА ЯВНО ОБЪЯВЛЯЕМ ФУНКЦИЮ ОПРОСА ДЛЯ КОМПИЛЯТОРА ---
// ==============================================================================
//  ФИКС НЕБЛОКИРУЮЩЕГО ВВОДА ДЛЯ КОМПОЗИТ TV (БЕЗ АУТОВ И ЗАВИСАНИЙ)
// ==============================================================================
String pyReadInputString() {
  #include <fabgl.h>
  extern fabgl::PS2Controller ps2Controller;

  String buffer = "";
  bool waiting = true;
  
  while (waiting) {
    fabgl::VirtualKeyItem item;
    
    // Опрашиваем клавиатуру с таймаутом 1мс
    if (ps2Controller.keyboard()->getNextVirtualKey(&item, 1)) {
      if (item.down) {
        if (item.vk == fabgl::VirtualKey::VK_RETURN) {
          py_out("\n");
          waiting = false;
        } 
        else if (item.vk == fabgl::VirtualKey::VK_BACKSPACE) {
          if (buffer.length() > 0) {
            buffer.remove(buffer.length() - 1);
            py_out("\b \b");
          }
        } 
        else if (item.ASCII != 0) {
          buffer += (char)item.ASCII;
          String s = ""; s += (char)item.ASCII;
          py_out(s);
        }
      }
    }
    // Даем процессору крутить PAL/NTSC сигнал, экран НЕ погаснет!
    delay(1); 
  }
    

    py_waiting_input = false; 
  return buffer;
}
// ═══════════════════════════════════════════════
//  Вычисление строковых выражений
// ═══════════════════════════════════════════════
// ═══════════════════════════════════════════════
//  Вычисление строковых выражений
// ═══════════════════════════════════════════════
String pyEvalStr(String expr) {
  expr.trim();

  // 1. ИЗОЛИРОВАННЫЙ И ИСПРАВЛЕННЫЙ INPUT ДЛЯ СТРОК (БЕЗ ПОВТОРОВ!)
  int inIdx = expr.indexOf("input(");
  if (inIdx != -1) {
    int cp = expr.lastIndexOf(')');
    String prompt = expr.substring(inIdx + 6, cp); 
    prompt.trim();
    
    // Чистим кавычки вокруг текста подсказки вручную
    if ((prompt.startsWith("\"") && prompt.endsWith("\"")) || 
        (prompt.startsWith("'") && prompt.endsWith("'"))) {
      prompt = prompt.substring(1, prompt.length() - 1);
    } else if (pyIsStrVar(prompt)) {
      prompt = pyGetStrVar(prompt);
    } else if (pyGetVar(prompt) != 0) {
      prompt = String(pyGetVar(prompt), 0);
    }
    
    py_out(prompt);             // Печатаем подсказку (даже с пробелами!) на ТВ
    return pyReadInputString(); // Забираем текст ввода с клавиатуры FabGL
  }

  // 2. СТРОКОВЫЙ ЛИТЕРАЛ "..." ИЛИ '...' (ИСПРАВЛЕН И ЗАКРЫТ)
  if ((expr.startsWith("\"") && expr.endsWith("\"")) ||
      (expr.startsWith("'")  && expr.endsWith("'"))) {
    return expr.substring(1, expr.length() - 1);
  }

  // 3. str(число)
  if (expr.startsWith("str(") && expr.endsWith(")")) {
// ... ДАЛЬШЕ ИДЕТ ТВОЙ СТАРЫЙ КОД (len, конкатенация +, и возврат переменной) ...
    float v = pyEval(expr.substring(4, expr.length() - 1));
    if (v == (int)v) return String((int)v);
    return String(v, 4);
  }

  // len(строка)
  if (expr.startsWith("len(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length() - 1);
    return String(pyEvalStr(inner).length());
  }

  // Конкатенация a + b
  int plus = expr.lastIndexOf('+');
  if (plus > 0) {
    String left  = pyEvalStr(expr.substring(0, plus));
    String right = pyEvalStr(expr.substring(plus + 1));
    return left + right;
  }
   // Строковая переменная
  if (pyIsStrVar(expr)) return pyGetStrVar(expr);
  // ── ЧТЕНИЕ ИЗ СЛОВАРЯ: dict["key"] ───────────────────────────
  if (expr.indexOf("[\"") > 0 && expr.endsWith("\"]")) {
    int lb = expr.indexOf("[\""), rb = expr.lastIndexOf("\"]");
    String dname = expr.substring(0, lb); dname.trim();
    String key = expr.substring(lb + 2, rb);
    
    PyDict* d = pyGetDict(dname);
    if (d) {
      for (int i = 0; i < (int)d->keys.size(); i++) {
        if (d->keys[i] == key) return d->vals[i]; // Возвращаем значение из словаря!
      }
    }
    return ""; // Если ключ не найден
  }

 
  return expr;

}



// ═══════════════════════════════════════════════
//  Вычисление числовых выражений
// ═══════════════════════════════════════════════
float pyEval(String expr) {
  expr.trim();
  if (expr.length() == 0) return 0;
// Изолированный input для чисел
  int inIdx = expr.indexOf("input(");
  if (inIdx != -1) {
    int cp = expr.lastIndexOf(')');
    String prompt = expr.substring(inIdx + 6, cp); prompt.trim();
    if ((prompt.startsWith("\"") && prompt.endsWith("\"")) || (prompt.startsWith("'") && prompt.endsWith("'"))) {
      prompt = prompt.substring(1, prompt.length() - 1);
    }
    py_out(prompt);
    String res = pyReadInputString();
    return res.toFloat();
  }
// ... дальше твой старый код pyEval ...
  // ── НАШ ИСПРАВЛЕННЫЙ ФИКС ДЛЯ INPUT В ЧИСЛАХ ───────────────────
  if (expr.startsWith("input(") && expr.endsWith(")")) {
    String prompt = expr.substring(6, expr.length() - 1); prompt.trim();
    if (prompt.startsWith("\"") || prompt.startsWith("'")) prompt = prompt.substring(1, prompt.length() - 1);
    
    py_out(prompt); // Печатаем подсказку
    String res = pyReadInputString();
    return res.toFloat(); // Отдаем калькулятору чистое число
  }
  // ───────────────────────────────────────────────────────────────

  // ── Встроенные функции ────────────────────
  // abs(x)
  if (expr.startsWith("abs(") && expr.endsWith(")"))
    return fabs(pyEval(expr.substring(4, expr.length()-1)));
  // int(x)
  if (expr.startsWith("int(") && expr.endsWith(")"))
    return (float)(int)pyEval(expr.substring(4, expr.length()-1));
  // float(x)
  if (expr.startsWith("float(") && expr.endsWith(")"))
    return pyEval(expr.substring(6, expr.length()-1));
  // sqrt(x)
  if (expr.startsWith("sqrt(") && expr.endsWith(")"))
    return sqrt(pyEval(expr.substring(5, expr.length()-1)));
  // pow(x,y)
  if (expr.startsWith("pow(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length()-1);
    int c = inner.indexOf(',');
    if (c > 0) return pow(pyEval(inner.substring(0,c)), pyEval(inner.substring(c+1)));
  }
  // round(x)
  if (expr.startsWith("round(") && expr.endsWith(")"))
    return round(pyEval(expr.substring(6, expr.length()-1)));
  // max(x,y)
  if (expr.startsWith("max(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length()-1);
    int c = inner.indexOf(',');
    if (c > 0) return std::max(pyEval(inner.substring(0,c)), pyEval(inner.substring(c+1)));
  }
  // min(x,y)
  if (expr.startsWith("min(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length()-1);
    int c = inner.indexOf(',');
    if (c > 0) return std::min(pyEval(inner.substring(0,c)), pyEval(inner.substring(c+1)));
  }
  // len(строка)
  if (expr.startsWith("len(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length()-1); inner.trim();
    return (float)pyEvalStr(inner).length();
  }
  // bool True/False
  if (expr == "True"  || expr == "true")  return 1;
  if (expr == "False" || expr == "false") return 0;

  // ── Скобки ───────────────────────────────
  if (expr.startsWith("(") && expr.endsWith(")"))
    return pyEval(expr.substring(1, expr.length()-1));

  // ── + − (справа налево, за скобками) ────
  int depth = 0;
  for (int i = expr.length()-1; i >= 1; i--) {
    char c = expr[i];
    if (c == ')') depth++;
    if (c == '(') depth--;
    if (depth == 0 && (c == '+' || c == '-')) {
      return c == '+' ?
        pyEval(expr.substring(0,i)) + pyEval(expr.substring(i+1)) :
        pyEval(expr.substring(0,i)) - pyEval(expr.substring(i+1));
    }
  }
  // ── * / % ─────────────────────────────────
  depth = 0;
  for (int i = expr.length()-1; i >= 1; i--) {
    char c = expr[i];
    if (c == ')') depth++;
    if (c == '(') depth--;
    if (depth == 0 && (c == '*' || c == '/' || c == '%')) {
      float l = pyEval(expr.substring(0,i));
      float r = pyEval(expr.substring(i+1));
      if (c == '*') return l * r;
      if (c == '%') return fmod(l, r);
      return (r != 0) ? l / r : 0;
    }
  }
  // ── ** степень ────────────────────────────
  int pw = expr.indexOf("**");
  if (pw > 0) return pow(pyEval(expr.substring(0,pw)), pyEval(expr.substring(pw+2)));

  // ── Число ─────────────────────────────────
  bool isNum = true;
  for (int i = 0; i < (int)expr.length(); i++) {
    char c = expr[i];
    if (!isDigit(c) && c != '.' && !(c == '-' && i == 0)) { isNum = false; break; }
  }
  if (isNum) return expr.toFloat();

   // ── list[i] (ЖЕЛЕЗОБЕТОННЫЙ ЧИТАТЕЛЬ С ИСПРАВЛЕНИЕМ АДРЕСА) ───────────────
  if (expr.indexOf('[') > 0 && expr.endsWith("]")) {
    int lb = expr.indexOf('['), rb = expr.lastIndexOf(']');
    String lname = expr.substring(0, lb); lname.trim();
    int idx = (int)pyEval(expr.substring(lb + 1, rb));
    
    // ПРИНУДИТЕЛЬНО ГОВОРИМ КОМПИЛЯТОРУ СТРУКТУРУ ФУНКЦИИ (Фикс нуля!)
    extern PyList* pyGetList(const String& name);
    PyList* pl = pyGetList(lname);
    
    if (pl) {
      // Если это числовой массив
      if (!pl->nums.empty() && idx >= 0 && idx < (int)pl->nums.size()) {
        return pl->nums[idx];
      }
      // Если это строковый массив, но его вызвали в числовом контексте pyEval
      if (!pl->strs.empty() && idx >= 0 && idx < (int)pl->strs.size()) {
        return pl->strs[idx].toFloat();
      }
    }
    return 0;
  }

  // ── len(list) (ИСПРАВЛЕННЫЙ ВАРЯНТ) ─────────────────────────────
  if (expr.startsWith("len(") && expr.endsWith(")")) {
    String inner = expr.substring(4, expr.length() - 1); inner.trim();
    
    extern PyList* pyGetList(const String& name);
    PyList* pl = pyGetList(inner);
    
    if (pl) return (float)(pl->nums.size() + pl->strs.size());
    return (float)pyEvalStr(inner).length();
  }

  // ── Переменная ────────────────────────────
  return pyGetVar(expr);
}

// ═══════════════════════════════════════════════
//  Проверка условия  ==  !=  <  >  <=  >=
// ═══════════════════════════════════════════════
bool pyEvalCond(String cond) {
  cond.trim();
  // Строковые сравнения
  struct { const char* op; int len; } ops[] = {
    {"==",2},{"!=",2},{"<=",2},{">=",2},{"<",1},{">",1}
  };
  for (auto& o : ops) {
    int pos = cond.indexOf(o.op);
    if (pos > 0) {
      String ls = cond.substring(0, pos);   ls.trim();
      String rs = cond.substring(pos+o.len); rs.trim();
      // Строки?
      bool lStr = (ls.startsWith("\"") || ls.startsWith("'") || pyIsStrVar(ls));
      bool rStr = (rs.startsWith("\"") || rs.startsWith("'") || pyIsStrVar(rs));
      if (lStr || rStr) {
        String lv = pyEvalStr(ls), rv = pyEvalStr(rs);
        if (strcmp(o.op,"==")==0) return lv == rv;
        if (strcmp(o.op,"!=")==0) return lv != rv;
        return false;
      }
      float lv = pyEval(ls), rv = pyEval(rs);
      if (strcmp(o.op,"==")==0) return lv == rv;
      if (strcmp(o.op,"!=")==0) return lv != rv;
      if (strcmp(o.op,"<") ==0) return lv <  rv;
      if (strcmp(o.op,">") ==0) return lv >  rv;
      if (strcmp(o.op,"<=")==0) return lv <= rv;
      if (strcmp(o.op,">=")==0) return lv >= rv;
    }
  }
  return pyEval(cond) != 0;
}

// ═══════════════════════════════════════════════
//  Вывод значения — число или строка
// ═══════════════════════════════════════════════
static void pyPrintExpr(String arg) {
  arg.trim();
  // Строковый литерал
  if ((arg.startsWith("\"") && arg.endsWith("\"")) ||
      (arg.startsWith("'")  && arg.endsWith("'"))) {
    py_print(arg.substring(1, arg.length()-1));
    return;
  }
  // Строковая переменная
  if (pyIsStrVar(arg)) { py_print(pyGetStrVar(arg)); return; }
  // str(...)
  if (arg.startsWith("str(")) { py_print(pyEvalStr(arg)); return; }
  // Число
  float v = pyEval(arg);
  if (v == (int)v) py_print(String((int)v));
  else             py_print(String(v, 4));
}

// ─── Вывод нескольких аргументов print(a, b, c) ──
static void pyPrintArgs(String args) {
  // Разбить по запятой вне скобок и кавычек
  String result = "";
  bool inQ = false; char qc = 0; int depth = 0;
  String cur = "";
  for (int i = 0; i <= (int)args.length(); i++) {
    char c = (i < (int)args.length()) ? args[i] : ',';
    if (!inQ && (c == '"' || c == '\'')) { inQ = true; qc = c; cur += c; continue; }
    if (inQ && c == qc) { inQ = false; cur += c; continue; }
    if (!inQ && c == '(') depth++;
    if (!inQ && c == ')') depth--;
    if (!inQ && depth == 0 && c == ',') {
      cur.trim();
      // Собрать значение
      if ((cur.startsWith("\"") && cur.endsWith("\"")) ||
          (cur.startsWith("'")  && cur.endsWith("'"))) {
        result += cur.substring(1, cur.length()-1);
      } else if (pyIsStrVar(cur)) {
        result += pyGetStrVar(cur);
      } else {
        float v = pyEval(cur);
        result += (v == (int)v) ? String((int)v) : String(v, 4);
      }
      result += " ";
      cur = "";
    } else {
      cur += c;
    }
  }
  if (result.endsWith(" ")) result.remove(result.length()-1);
  py_print(result);
}

// ═══════════════════════════════════════════════
//  Выполнить одну строку Python
// ═══════════════════════════════════════════════
// ─── List helpers ──────────────────────────
PyList* pyGetList(const String& name) {
  for (auto& l : py_lists) if (l.name == name) return &l;
  return nullptr;
}
PyList& pyMakeList(const String& name) {
  for (auto& l : py_lists) if (l.name == name) return l;
  py_lists.push_back({name, {}, {}, false});
  return py_lists.back();
}

// ─── Dict helpers ──────────────────────────
PyDict* pyGetDict(const String& name) {
  for (auto& d : py_dicts) if (d.name == name) return &d;
  return nullptr;
}
PyDict& pyMakeDict(const String& name) {
  for (auto& d : py_dicts) if (d.name == name) return d;
  py_dicts.push_back({name, {}, {}});
  return py_dicts.back();
}

// ─── Вызов def-функции ─────────────────────
void pyCallFunc(const String& name, const String& argsStr);

// ─── Состояние записи def ──────────────────
bool   py_in_def    = false;
String py_def_name  = "";
std::vector<String> py_def_params;
std::vector<String> py_def_body;

void pyExec(String line) {
  // 1. СТРОГО СОХРАНЯЕМ ИСХОДНУЮ СТРОКУ ДЛЯ ПРОВЕРКИ ОТСТУПОВ
  String raw = line; 
  
  // Очищаем рабочую строку от пробелов для проверки команд
  line.trim();
  if (line.length() == 0 || line.startsWith("#")) return;

  // Проверяем реальный отступ по исходной строке raw
  bool has_indent = raw.startsWith(" ") || raw.startsWith("\t");

  // === СИНХРОНИЗАЦИЯ БЛОКОВ ПРОПУСКА (БЕЗ СБОЕВ) ===
  if (py_skip_block) {
    if (line == "else:" || line.startsWith("else:") || line.startsWith("elif ")) {
      // Наткнулись на else/elif после ложного if — даем ему шанс провериться
    } 
    else if (has_indent) {
      return; // Строка с отступом внутри пропущенного блока -> молча пропускаем
    } 
    else {
      py_skip_block = false; // Строка без отступа -> вышли из блока, выполняем её
    }
  }

  // Если встретили команду без отступа (и это не else/elif), полностью сбрасываем пропуск
  if (!has_indent && !line.startsWith("else:") && !line.startsWith("elif ")) {
    py_skip_block = false;
  }

  // === УМНОЕ ПРИСВОЕНИЕ ПЕРЕМЕННЫХ (ЧИСЛА И БУКВЫ) ===
  int eqIdx = line.indexOf('=');
  // Проверяем, что это обычное присвоение '=', а не логическое '==' или '>='
  if (eqIdx > 0 && line[eqIdx+1] != '=' && line[eqIdx-1] != '>' && line[eqIdx-1] != '<' && line[eqIdx-1] != '!') {
    String varName = line.substring(0, eqIdx); varName.trim();
    String expr = line.substring(eqIdx + 1); expr.trim();
    
    // Если справа находится input() или явная строка в кавычках/строковая переменная
    if (expr.startsWith("input(") || 
        (expr.startsWith("\"") && expr.endsWith("\"")) || 
        (expr.startsWith("'") && expr.endsWith("'")) ||
        pyIsStrVar(expr)) {
        
        String str_val = pyEvalStr(expr); // Считаем как СТРОКУ (запустит клавиатуру TV)
        pySetStrVar(varName, str_val);    // Кладем строго в текстовый вектор
    } 
    // В остальных случаях считаем как число или математику
    else {
        float num_val = pyEval(expr);
        pySetVar(varName, num_val);       // Кладем в числовой вектор
    }
    return;
  }

  // ── Дальше идет твой оригинальный код (обработка print, if, else, for и т.д.) ──


  // ── Дальше идет твой обычный код (if, else, for, принти и т.д.) ──

  
  if (line.length() == 0 || line.startsWith("#")) return;

  // ── Пропуск блока после False if ──────────
  if (py_skip_block) {
    if (line == "else:" || line.startsWith("else:")) {
      py_skip_block = false;  // else после false if → выполняем
      return;
    }
    if (!line.startsWith(" ") && !line.startsWith("\t")) {
      py_skip_block = false;  // новая команда без отступа → выходим из skip
      // НЕ return — выполняем эту строку!
    } else {
      return;  // строка с отступом = тело пропущенного if → пропускаем
    }
  }

  // ── print(...) ────────────────────────────
//  if (line.startsWith("print(") && line.endsWith(")")) {
//    String args = line.substring(6, line.length()-1);
//    // print(list) или print(dict)?
//    PyList* pl = pyGetList(args);
//    if (pl) {
//      String out = "[";
//      for (int i = 0; i < (int)pl->nums.size(); i++) {
//        if (i) out += ", ";
//        float v = pl->nums[i];
//        out += (v==(int)v) ? String((int)v) : String(v,4);
//      }
//      for (int i = 0; i < (int)pl->strs.size(); i++) {
//        if (i || pl->nums.size()) out += ", ";
//        out += "'" + pl->strs[i] + "'";
//      }
//      out += "]";
//      py_print(out);
//      return;
//    }
//    if (args.indexOf(',') >= 0) pyPrintArgs(args);
//    else                        pyPrintExpr(args);
//    return;

       // ── ВСЕЯДНЫЙ ПРИНТ ДЛЯ БУКВ И ЧИСЕЛ ─────────────────────
   // ── ВСЕЯДНЫЙ И МЕГА-СТАБИЛЬНЫЙ ПРИНТ (ЧИСЛА, БУКВЫ И МАССИВЫ) ──
  if (line.startsWith("print(") && line.endsWith(")")) {
    String expr = line.substring(6, line.length() - 1);
    expr.trim();

    // 1. Убираем кавычки, если это прямой текст: print("Hello")
    if ((expr.startsWith("\"") && expr.endsWith("\"")) || 
        (expr.startsWith("'") && expr.endsWith("'"))) {
      py_print(expr.substring(1, expr.length() - 1));
    }
    // 2. Если это имя СТРОКОВОЙ переменной: print(x)
    else if (pyIsStrVar(expr)) {
      py_print(pyGetStrVar(expr));
    }
    // 3. НОВАЯ ЛОГИКА: Если это имя МАССИВА/СПИСКА: print(arr)
    else if (pyGetList(expr) != nullptr) {
      PyList* pl = pyGetList(expr);
      String out = "[";
      
      // Если это список чисел
      if (!pl->nums.empty()) {
        for (int i = 0; i < (int)pl->nums.size(); i++) {
          float v = pl->nums[i];
          if (v == (int)v) out += String((int)v);
          else             out += String(v, 2);
          if (i < (int)pl->nums.size() - 1) out += ", ";
        }
      } 
      // Если это список строк
      else if (!pl->strs.empty()) {
        for (int i = 0; i < (int)pl->strs.size(); i++) {
          out += "'" + pl->strs[i] + "'";
          if (i < (int)pl->strs.size() - 1) out += ", ";
        }
      }
      out += "]";
      py_print(out); // Красиво печатаем [10, 55, 30] на Composite TV!
    }
    // 4. Во всех остальных случаях считаем как обычное число или математику
    else {
      float v = pyEval(expr);
      if (v == (int)v) py_print(String((int)v)); 
      else             py_print(String(v, 4));    
    }
    return;
  }


  
//3
  // ── input(prompt) ─────────────────────────
  // В REPL просто печатает приглашение — ввод через следующую команду
  if (line.startsWith("input(")) {
    int cp = line.indexOf(')');
    String prompt = line.substring(6, cp); prompt.trim();
    if (prompt.startsWith("\"") || prompt.startsWith("'"))
      prompt = prompt.substring(1, prompt.length()-1);
    py_out(prompt);
    // В файловом режиме — сложно без ОС, возвращаем ""
    return;
  }

//    // ── input(prompt) ─────────────────────────
//   // ── НАДЕЖНЫЙ ОПРЕДЕЛИТЕЛЬ INPUT(PROMPT) ──
//  // Ищем слово "input(" в любом месте строки (даже если впереди имя переменной)
//  // ── НАДЕЖНЫЙ ОПРЕДЕЛИТЕЛЬ INPUT(PROMPT) ──
//  int inputIdx = line.indexOf("input(");
//  if (inputIdx != -1) {
//    int cp = line.lastIndexOf(')');
//    String prompt = line.substring(inputIdx + 6, cp); 
//    prompt.trim();
//    
//    // Очищаем кавычки у подсказки
//    if (prompt.startsWith("\"") || prompt.startsWith("'")) {
//      prompt = prompt.substring(1, prompt.length() - 1);
//    } else if (pyIsStrVar(prompt)) {
//      prompt = pyGetStrVar(prompt);
//    } else if (pyGetVar(prompt) != 0) {
//      prompt = String(pyGetVar(prompt), 0);
//    }
//    
//    // Печатаем подсказку (1) на экран телевизора
//    py_out(prompt); 
//    
//    // Вытаскиваем имя переменной (например, nam)
//    int eqIdx = line.indexOf('=');
//    if (eqIdx != -1 && eqIdx < inputIdx) {
//        py_input_var = line.substring(0, eqIdx);
//        py_input_var.trim(); 
//    } else {
//        py_input_var = ""; 
//    }
//
//    py_input_buffer = "";     
//    py_waiting_input = true;  // Включаем режим ожидания для loop()
//
//    // === КООПЕРАТИВНОЕ ОЖИДАНИЕ ВВОДА ДЛЯ КОМПОЗИТ TV ===
//    // Цикл крутится, пока пользователь не нажмет Enter в loop()
//   while (py_waiting_input) {
//        delay(10); // Это автоматически освобождает FreeRTOS и дает ресурсы FabGL
//    }
//    // ====================================================
//    
//    return;
//  }




  // ── list = [] или list = [1,2,3] (СИНХРОНИЗИРОВАНО С ОТСТУПАМИ) ──────────
  if (line.indexOf("= [") > 0 || line.endsWith("= []")) {
    // ЗАЩИТА: Если строка имеет отступ и блок нужно пропустить — выходим!
    if (has_indent && py_skip_block) return;

    int eq = line.indexOf('='); String vname = line.substring(0,eq); vname.trim();
    int lb = line.indexOf('['), rb = line.lastIndexOf(']');
    if (lb >= 0 && rb > lb) {
      PyList& lst = pyMakeList(vname);
      lst.nums.clear(); lst.strs.clear();
      String items = line.substring(lb+1, rb); items.trim();
      if (items.length() > 0) {
        int pos = 0;
        while (pos <= (int)items.length()) {
          int comma = items.indexOf(',', pos);
          if (comma < 0) comma = items.length();
          String it = items.substring(pos, comma); it.trim();
          if (it.startsWith("\"") || it.startsWith("'"))
            lst.strs.push_back(it.substring(1, it.length()-1));
          else
            lst.nums.push_back(pyEval(it));
          pos = comma + 1;
        }
      }
      return;
    }
  }

  // ── list.append(val) (СИНХРОНИЗИРОВАНО С ОТСТУПАМИ) ──────────────────────
  if (line.indexOf(".append(") > 0 && line.endsWith(")")) {
    // ЗАЩИТА ОТСТУПА
    if (has_indent && py_skip_block) return;

    int dot = line.indexOf(".append(");
    String lname = line.substring(0, dot);
    String val   = line.substring(dot+8, line.length()-1); val.trim();
    PyList* pl = pyGetList(lname);
    if (pl) {
      if (val.startsWith("\"") || val.startsWith("'"))
        pl->strs.push_back(val.substring(1, val.length()-1));
      else
        pl->nums.push_back(pyEval(val));
    }
    return;
  }

  // ── list[i] = val (СИНХРОНИЗИРОВАНО С ОТСТУПАМИ) ─────────────────────────
  if (line.indexOf('[') > 0 && line.indexOf(']') > 0 && line.indexOf('=') > 0) {
    // ЗАЩИТА ОТСТУПА
    if (has_indent && py_skip_block) return;

    int lb = line.indexOf('['), rb = line.indexOf(']');
    int eq = line.indexOf('=');
    if (lb < rb && rb < eq) {
      String lname = line.substring(0, lb); lname.trim();
      int idx = (int)pyEval(line.substring(lb+1, rb));
      String val = line.substring(eq+1); val.trim();
      PyList* pl = pyGetList(lname);
      if (pl && idx >= 0) {
        if (!pl->nums.empty() && idx < (int)pl->nums.size()) {
          pl->nums[idx] = pyEval(val);
        } else if (!pl->strs.empty() && idx < (int)pl->strs.size()) {
          if (val.startsWith("\"") || val.startsWith("'"))
            pl->strs[idx] = val.substring(1, val.length()-1);
        }
      }
      return;
    }
  }


  // ── dict = {} или dict = {"k":"v"} ────────
   // ── dict = {} или dict = {"k":"v"} (СИНХРОНИЗИРОВАНО С ОТСТУПАМИ) ────────
  if (line.indexOf("= {") > 0 || line.endsWith("= {}")) {
    // ЗАЩИТА: Если строка имеет отступ и блок нужно пропустить — выходим!
    if (has_indent && py_skip_block) return;

    int eq = line.indexOf('='); String dname = line.substring(0,eq); dname.trim();
    int lb = line.indexOf('{'), rb = line.lastIndexOf('}');
    if (lb >= 0 && rb > lb) {
      PyDict& d = pyMakeDict(dname);
      d.keys.clear(); d.vals.clear();
      String items = line.substring(lb+1, rb); items.trim();
      if (items.length() > 0) {
        int pos = 0;
        while (pos <= (int)items.length()) {
          int comma = items.indexOf(',', pos);
          if (comma < 0) comma = items.length();
          String pair = items.substring(pos, comma); pair.trim();
          int colon = pair.indexOf(':');
          if (colon > 0) {
            String k = pair.substring(0, colon); k.trim();
            String v = pair.substring(colon+1); v.trim();
            
            // Ключ очищаем от кавычек
            if (k.startsWith("\"") || k.startsWith("'")) k = k.substring(1, k.length()-1);
            
            // Значение вычисляем по-умному: как строку или как число
            if (v.startsWith("\"") || v.startsWith("'") || pyIsStrVar(v)) {
              v = pyEvalStr(v);
            } else {
              // Если это число, переводим результат pyEval в строку для хранения в d.vals
              v = String(pyEval(v), 0); 
            }
            
            d.keys.push_back(k); d.vals.push_back(v);
          }
          pos = comma + 1;
        }
      }
      return;
    }
  }

  // ── dict["key"] = val (СИНХРОНИЗИРОВАНО С ОТСТУПАМИ) ─────────────────────
  if (line.indexOf("[\"") > 0 && line.indexOf("\"]") > 0 && line.indexOf('=') > 0) {
    // ЗАЩИТА ОТСТУПА
    if (has_indent && py_skip_block) return;

    int lb = line.indexOf("[\""), rb = line.indexOf("\"]");
    int eq = line.lastIndexOf('=');
    if (lb < rb && rb < eq) {
      String dname = line.substring(0, lb); dname.trim();
      String key   = line.substring(lb+2, rb); // Сам ключ внутри [" "]
      String val   = line.substring(eq+1); val.trim();
      
      // Вычисляем значение перед записью
      if (val.startsWith("\"") || val.startsWith("'") || pyIsStrVar(val)) {
        val = pyEvalStr(val);
      } else {
        float num = pyEval(val);
        if (num == (int)num) val = String((int)num);
        else                 val = String(num, 2);
      }
      
      PyDict& d = pyMakeDict(dname);
      for (int i=0; i<(int)d.keys.size(); i++) {
        if (d.keys[i]==key) { d.vals[i]=val; return; }
      }
      d.keys.push_back(key); d.vals.push_back(val);
      return;
    }
  }

  // ── if условие: ───────────────────────────
    // ── if условие: ───────────────────────────
  // ── УМНОЕ if условие (НЕ ЗАВИСИТ ОТ ПРОБЕЛОВ) ───────────────────────────
  if (line.startsWith("if ") && line.endsWith(":")) {
    String cond = line.substring(3, line.length() - 1); 
    cond.trim();
    
    bool result = pyEvalCond(cond); // Вычисляем условие
    
    if (result) {
      py_if_was_true = true;  // if сработал -> выполняем следующую строку (тело if)
    } else {
      py_if_was_true = false; // if не сработал -> ищем, куда прыгнуть
      
      // Сканируем программу вперед в поисках else: или окончания блока
      int scan = py_prog_pc + 1;
      while (scan < (int)py_program.size()) {
        String test_line = py_program[scan];
        test_line.trim();
        // Если нашли else:, останавливаемся прямо на нем, чтобы движок выполнил его тело!
        if (test_line.startsWith("else:") || test_line == "else:") {
          py_prog_pc = scan - 1; // Переставляем указатель на строку ПЕРЕД else
          break;
        }
        // Если нашли следующий независимый if или for без отступа (в будущем), тоже выходим
        scan++;
      }
    }
    return;
  }

  // ── УМНОЕ else (ПЕРЕПРЫГИВАЕТ ТЕЛО, ЕСЛИ IF СРАБОТАЛ) ───────────────────
  if (line == "else:" || line.startsWith("else:")) {
    if (py_if_was_true) {
      // Если прошлый if БЫЛ истинным, нам нужно ПЕРЕПРЫГНУТЬ тело else!
      // Просто шагаем на одну строчку вперед, пропуская принт внутри else
      py_prog_pc++; 
    }
    py_if_was_true = false; // Сбрасываем флаг
    return;
  }

  // ── for i in range(n): ────────────────────
  if (line.startsWith("for ") && line.endsWith(":")) {
    int inPos = line.indexOf(" in range(");
    if (inPos > 0) {
      py_for_var = line.substring(4, inPos); py_for_var.trim();
      String rng = line.substring(inPos + 10, line.length()-2);
      rng.trim();
      float start = 0, stop = 0, step = 1;
      int c1 = rng.indexOf(',');
      int c2 = (c1 >= 0) ? rng.indexOf(',', c1+1) : -1;
      if (c1 < 0) {
        stop = pyEval(rng);
      } else if (c2 < 0) {
        start = pyEval(rng.substring(0, c1));
        stop  = pyEval(rng.substring(c1+1));
      } else {
        start = pyEval(rng.substring(0, c1));
        stop  = pyEval(rng.substring(c1+1, c2));
        step  = pyEval(rng.substring(c2+1));
      }
      py_for_to   = stop;
      py_for_step = (step != 0) ? step : 1;
      pySetVar(py_for_var, start);
      py_in_for   = true;
      py_for_line = py_prog_pc + 1;  // ТЕЛО начинается на СЛЕДУЮЩЕЙ строке!
    }
    return;
  }

  // ── Присваивание строки  x = "..." ────────
  {
    int eq = line.indexOf('=');
    if (eq > 0 && line[eq-1] != '!' && line[eq-1] != '<' && line[eq-1] != '>') {
      String varName = line.substring(0, eq); varName.trim();
      String valStr  = line.substring(eq+1);  valStr.trim();
      // += -= *= /=
      char op = line[eq-1];
      if (op == '+' || op == '-' || op == '*' || op == '/') {
        varName = line.substring(0, eq-1); varName.trim();
        float cur = pyGetVar(varName);
        float val = pyEval(valStr);
        if (op == '+') pySetVar(varName, cur + val);
        if (op == '-') pySetVar(varName, cur - val);
        if (op == '*') pySetVar(varName, cur * val);
        if (op == '/' && val != 0) pySetVar(varName, cur / val);
        return;
      }
      // Строка?
      if ((valStr.startsWith("\"") && valStr.endsWith("\"")) ||
          (valStr.startsWith("'")  && valStr.endsWith("'"))  ||
          pyIsStrVar(valStr) || valStr.startsWith("str(")) {
        pySetStrVar(varName, pyEvalStr(valStr));
      } else {
        pySetVar(varName, pyEval(valStr));
      }
      return;
    }
  }

  // ── Просто выражение — вывести ─────────────
  float v = pyEval(line);
  if (v == (int)v) py_print(String((int)v));
  else             py_print(String(v, 4));
}

// ═══════════════════════════════════════════════
//  Запуск .py файла с SPIFFS
// ═══════════════════════════════════════════════
void pyRun(const String& filepath) {
  String fpath = filepath.startsWith("/") ? filepath : "/" + filepath;
  File f = SPIFFS.open(fpath, "r");
  if (!f) {
    py_print("File not found: " + filepath);
    return;
  }
  py_program.clear();
  while (f.available()) {
    String ln = f.readStringUntil('\n');
    if (ln.endsWith("\r")) ln.remove(ln.length()-1);
    py_program.push_back(ln);
  }
  f.close();

  py_print("Running: " + filepath);
  py_prog_running = true;
  py_prog_pc      = 0;
  py_skip_block   = false;
  py_if_was_true  = false;
  py_in_for       = false;

  while (py_prog_running && py_prog_pc < (int)py_program.size()) {
 if (py_waiting_input) { delay(5); continue; }
    
    String raw    = py_program[py_prog_pc];
    bool indented = raw.startsWith(" ") || raw.startsWith("\t");
    String line   = raw; line.trim();
    if (line.length() == 0 || line.startsWith("#")) { py_prog_pc++; continue; }

    // Тело for-цикла
    if (py_in_for && indented) {
      pyExec(line);
      py_prog_pc++;
      bool nextIsBody = (py_prog_pc < (int)py_program.size() &&
                        (py_program[py_prog_pc].startsWith(" ") ||
                         py_program[py_prog_pc].startsWith("\t")));
      if (!nextIsBody) {
        float cur = pyGetVar(py_for_var) + py_for_step;
        pySetVar(py_for_var, cur);
        bool cont = (py_for_step > 0) ? (cur < py_for_to) : (cur > py_for_to);
        if (cont) py_prog_pc = py_for_line;
        else      py_in_for  = false;
      }
      continue;
    }
    if (py_in_for && !indented) py_in_for = false;

    // Тело def — записываем в функцию
    if (py_in_def) {
      if (indented) {
        py_def_body.push_back(line);
        py_prog_pc++; continue;
      } else {
        // Конец def — сохраняем
        PyFunc fn; fn.name=py_def_name; fn.params=py_def_params; fn.body=py_def_body;
        // Обновить или добавить
        bool found=false;
        for (auto& f : py_funcs) if (f.name==fn.name) { f=fn; found=true; break; }
        if (!found) py_funcs.push_back(fn);
        py_in_def=false; py_def_body.clear();
      }
    }

    // ── def fname(params): ──────────────────
    if (line.startsWith("def ") && line.indexOf(":") > 0) {
      int lp=line.indexOf('('), rp=line.indexOf(')');
      if (lp>0 && rp>lp) {
        py_def_name = line.substring(4, lp); py_def_name.trim();
        String pstr = line.substring(lp+1, rp); pstr.trim();
        py_def_params.clear();
        if (pstr.length()>0) {
          int pos=0;
          while (pos<=(int)pstr.length()) {
            int c=pstr.indexOf(',',pos); if (c<0) c=pstr.length();
            String p=pstr.substring(pos,c); p.trim();
            if (p.length()) py_def_params.push_back(p);
            pos=c+1;
          }
        }
        py_def_body.clear();
        py_in_def=true;
        py_prog_pc++; continue;
      }
    }

    // ── Вызов функции fname(args) ────────────
    if (line.indexOf('(') > 0 && line.endsWith(")") && !line.startsWith("print")) {
      int lp=line.indexOf('(');
      String fname=line.substring(0,lp); fname.trim();
      for (auto& fn : py_funcs) {
        if (fn.name==fname) {
          String astr=line.substring(lp+1,line.length()-1);
          // Установить параметры
          int pos=0, pi=0;
          while (pos<=(int)astr.length() && pi<(int)fn.params.size()) {
            int c=astr.indexOf(',',pos); if (c<0) c=astr.length();
            String av=astr.substring(pos,c); av.trim();
            if (av.startsWith("\"")||av.startsWith("'"))
              pySetStrVar(fn.params[pi], av.substring(1,av.length()-1));
            else pySetVar(fn.params[pi], pyEval(av));
            pos=c+1; pi++;
          }
          // Выполнить тело
          for (auto& bl : fn.body) {
            String bl2=bl; bl2.trim();
            pyExec(bl2);
          }
          py_prog_pc++; continue;
        }
      }
    }

    pyExec(line);
    py_prog_pc++;
    yield();
  }

  py_prog_running = false;
  py_print("Done.");
}

// ═══════════════════════════════════════════════
//  Очистить все переменные
// ═══════════════════════════════════════════════
void pyClear() {
  py_vars_name.clear();
  py_vars_val.clear();
  py_vars_str_name.clear();
  py_vars_str_val.clear();
  py_history.clear();
  py_skip_block   = false;
  py_if_was_true  = false;
  py_in_for       = false;
  py_in_def       = false;
  py_lists.clear();
  py_dicts.clear();
  py_def_body.clear();
  py_prog_running = false;
}
