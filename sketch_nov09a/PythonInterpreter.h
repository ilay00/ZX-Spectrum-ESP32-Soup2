#ifndef PYTHON_INTERPRETER_H
#define PYTHON_INTERPRETER_H

#include <Arduino.h>
#include <vector>
#include "fabgl.h"

// Терминал из основного .ino
extern fabgl::Terminal terminal;

// ─── Переменные Python ─────────────────────────
extern std::vector<String> py_vars_name;
extern std::vector<float>  py_vars_val;
extern std::vector<String> py_vars_str_name;
extern std::vector<String> py_vars_str_val;
extern std::vector<String> py_history;

// ─── for-цикл состояние ────────────────────────
extern String  py_for_var;
extern float   py_for_to;
extern float   py_for_step;
extern int     py_for_line;   // строка в py_program где был for
extern bool    py_in_for;

// ─── Запуск файла .py ──────────────────────────
extern std::vector<String> py_program;
extern bool py_prog_running;
extern int  py_prog_pc;

// ─── Функции ───────────────────────────────────
void     pyExec(String line);
float    pyEval(String expr);
String   pyEvalStr(String expr);
void     pyRun(const String& filepath);   // запуск .py файла
void     pyClear();

#endif
