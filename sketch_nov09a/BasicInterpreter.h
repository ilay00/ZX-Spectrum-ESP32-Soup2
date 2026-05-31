#ifndef BASIC_INTERPRETER_H
#define BASIC_INTERPRETER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include "fabgl.h"

// Терминал и клавиатура из основного .ino
extern fabgl::Terminal    terminal;
extern fabgl::PS2Controller ps2Controller;

// Глобальные для BASIC
extern std::vector<String> basic_lines;
extern std::map<String, float> basic_vars_num;
extern std::map<String, String> basic_vars_str;
extern int basic_pc;
extern int basic_stack[10];
extern int basic_stack_ptr;
extern bool basic_running;

// Функции BASIC
void basic_run();
float basic_eval_expr(String expr);
String basic_eval_expr_str(String expr);
float basic_eval_condition(String cond);
void run_basic_program(const String& filename);
void term_print(String s);
void term_println(String s);
#endif
