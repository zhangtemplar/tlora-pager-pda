/**
 * @file      ui_calculator.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Scientific Calculator app using Shunting-Yard expression parser.
 */
#include "ui_define.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---- Expression Parser (Shunting-Yard) ----

#define MAX_EXPR_LEN    256
#define MAX_STACK       64
#define MAX_HISTORY     10

static bool deg_mode = true;
static char history[MAX_HISTORY][MAX_EXPR_LEN];
static char history_results[MAX_HISTORY][64];
static int history_count = 0;

static double to_rad(double x) { return deg_mode ? x * M_PI / 180.0 : x; }
static double from_rad(double x) { return deg_mode ? x * 180.0 / M_PI : x; }

static int factorial_int(int n)
{
    if (n < 0) return -1;
    if (n > 20) return -1;
    int r = 1;
    for (int i = 2; i <= n; i++) r *= i;
    return r;
}

enum TokenType { TOK_NUM, TOK_OP, TOK_FUNC, TOK_LPAREN, TOK_RPAREN, TOK_END };

struct Token {
    TokenType type;
    double num;
    char op;
    char func[10];
};

static int op_prec(char op)
{
    switch (op) {
    case '+': case '-': return 1;
    case '*': case '/': case '%': return 2;
    case '^': return 3;
    default: return 0;
    }
}

static bool op_right_assoc(char op) { return op == '^'; }

static const char *skip_spaces(const char *s) { while (*s == ' ') s++; return s; }

static const char *parse_token(const char *s, Token *t)
{
    s = skip_spaces(s);
    if (*s == '\0') { t->type = TOK_END; return s; }

    if (*s == '(') { t->type = TOK_LPAREN; return s + 1; }
    if (*s == ')') { t->type = TOK_RPAREN; return s + 1; }

    if (*s == '+' || *s == '-' || *s == '*' || *s == '/' || *s == '^' || *s == '%') {
        t->type = TOK_OP;
        t->op = *s;
        return s + 1;
    }

    if (isdigit(*s) || *s == '.') {
        t->type = TOK_NUM;
        char *end;
        t->num = strtod(s, &end);
        return end;
    }

    if (isalpha(*s)) {
        int i = 0;
        while (isalpha(*s) && i < 9) {
            t->func[i++] = *s++;
        }
        t->func[i] = '\0';

        if (strcmp(t->func, "pi") == 0) {
            t->type = TOK_NUM;
            t->num = M_PI;
            return s;
        }
        if (strcmp(t->func, "e") == 0 && !isalpha(*s)) {
            t->type = TOK_NUM;
            t->num = M_E;
            return s;
        }
        t->type = TOK_FUNC;
        return s;
    }

    t->type = TOK_END;
    return s;
}

static bool apply_func(const char *func, double arg, double *result)
{
    if (strcmp(func, "sin") == 0) { *result = sin(to_rad(arg)); return true; }
    if (strcmp(func, "cos") == 0) { *result = cos(to_rad(arg)); return true; }
    if (strcmp(func, "tan") == 0) { *result = tan(to_rad(arg)); return true; }
    if (strcmp(func, "asin") == 0) { *result = from_rad(asin(arg)); return true; }
    if (strcmp(func, "acos") == 0) { *result = from_rad(acos(arg)); return true; }
    if (strcmp(func, "atan") == 0) { *result = from_rad(atan(arg)); return true; }
    if (strcmp(func, "log") == 0) { *result = log10(arg); return true; }
    if (strcmp(func, "ln") == 0) { *result = log(arg); return true; }
    if (strcmp(func, "exp") == 0) { *result = exp(arg); return true; }
    if (strcmp(func, "sqrt") == 0) { *result = sqrt(arg); return true; }
    if (strcmp(func, "abs") == 0) { *result = fabs(arg); return true; }
    if (strcmp(func, "fact") == 0) {
        int f = factorial_int((int)arg);
        if (f < 0) return false;
        *result = (double)f;
        return true;
    }
    return false;
}

static bool apply_op(char op, double a, double b, double *result)
{
    switch (op) {
    case '+': *result = a + b; return true;
    case '-': *result = a - b; return true;
    case '*': *result = a * b; return true;
    case '/':
        if (b == 0) return false;
        *result = a / b;
        return true;
    case '%':
        if (b == 0) return false;
        *result = fmod(a, b);
        return true;
    case '^': *result = pow(a, b); return true;
    default: return false;
    }
}

static bool evaluate(const char *expr, double *result)
{
    double num_stack[MAX_STACK];
    int num_top = -1;
    char op_stack[MAX_STACK];
    int op_top = -1;
    char func_stack[MAX_STACK][10];
    int func_top = -1;
    // Type tracking for unary minus: 0=start/op/lparen, 1=number/rparen
    int last_type = 0;

    const char *s = expr;
    Token tok;

    while (1) {
        s = parse_token(s, &tok);
        if (tok.type == TOK_END) break;

        if (tok.type == TOK_NUM) {
            if (num_top >= MAX_STACK - 1) return false;
            num_stack[++num_top] = tok.num;
            last_type = 1;
        } else if (tok.type == TOK_FUNC) {
            if (func_top >= MAX_STACK - 1 || op_top >= MAX_STACK - 1) return false;
            func_top++;
            strncpy(func_stack[func_top], tok.func, 9);
            func_stack[func_top][9] = '\0';
            op_stack[++op_top] = 'F';
            last_type = 0;
        } else if (tok.type == TOK_OP) {
            // Handle unary minus
            if (tok.op == '-' && last_type == 0) {
                s = parse_token(s, &tok);
                if (tok.type == TOK_NUM) {
                    if (num_top >= MAX_STACK - 1) return false;
                    num_stack[++num_top] = -tok.num;
                    last_type = 1;
                    continue;
                } else if (tok.type == TOK_LPAREN) {
                    // Push 0 and then subtract
                    if (num_top >= MAX_STACK - 1) return false;
                    num_stack[++num_top] = 0;
                    op_stack[++op_top] = '-';
                    op_stack[++op_top] = '(';
                    last_type = 0;
                    continue;
                } else {
                    return false;
                }
            }

            while (op_top >= 0 && op_stack[op_top] != '(' && op_stack[op_top] != 'F' &&
                   (op_prec(op_stack[op_top]) > op_prec(tok.op) ||
                    (op_prec(op_stack[op_top]) == op_prec(tok.op) && !op_right_assoc(tok.op)))) {
                if (num_top < 1) return false;
                double b = num_stack[num_top--];
                double a = num_stack[num_top--];
                double r;
                if (!apply_op(op_stack[op_top--], a, b, &r)) return false;
                num_stack[++num_top] = r;
            }
            if (op_top >= MAX_STACK - 1) return false;
            op_stack[++op_top] = tok.op;
            last_type = 0;
        } else if (tok.type == TOK_LPAREN) {
            if (op_top >= MAX_STACK - 1) return false;
            op_stack[++op_top] = '(';
            last_type = 0;
        } else if (tok.type == TOK_RPAREN) {
            while (op_top >= 0 && op_stack[op_top] != '(') {
                if (op_stack[op_top] == 'F') {
                    if (num_top < 0 || func_top < 0) return false;
                    double a = num_stack[num_top--];
                    double r;
                    if (!apply_func(func_stack[func_top--], a, &r)) return false;
                    op_top--;
                    num_stack[++num_top] = r;
                } else {
                    if (num_top < 1) return false;
                    double b = num_stack[num_top--];
                    double a = num_stack[num_top--];
                    double r;
                    if (!apply_op(op_stack[op_top--], a, b, &r)) return false;
                    num_stack[++num_top] = r;
                }
            }
            if (op_top < 0) return false;
            op_top--; // pop '('
            // Check if top of op_stack is a function
            if (op_top >= 0 && op_stack[op_top] == 'F') {
                if (num_top < 0 || func_top < 0) return false;
                double a = num_stack[num_top--];
                double r;
                if (!apply_func(func_stack[func_top--], a, &r)) return false;
                op_top--;
                num_stack[++num_top] = r;
            }
            last_type = 1;
        }
    }

    while (op_top >= 0) {
        if (op_stack[op_top] == '(' || op_stack[op_top] == ')') return false;
        if (op_stack[op_top] == 'F') {
            if (num_top < 0 || func_top < 0) return false;
            double a = num_stack[num_top--];
            double r;
            if (!apply_func(func_stack[func_top--], a, &r)) return false;
            op_top--;
            num_stack[++num_top] = r;
        } else {
            if (num_top < 1) return false;
            double b = num_stack[num_top--];
            double a = num_stack[num_top--];
            double r;
            if (!apply_op(op_stack[op_top--], a, b, &r)) return false;
            num_stack[++num_top] = r;
        }
    }

    if (num_top != 0) return false;
    *result = num_stack[0];
    return true;
}

// ---- UI ----

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *expr_ta = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *mode_label = NULL;
static lv_obj_t *history_list = NULL;

static void format_result(double val, char *buf, size_t len)
{
    if (isinf(val)) {
        snprintf(buf, len, "Infinity");
    } else if (isnan(val)) {
        snprintf(buf, len, "NaN");
    } else if (val == (long long)val && fabs(val) < 1e15) {
        snprintf(buf, len, "%lld", (long long)val);
    } else {
        snprintf(buf, len, "%.10g", val);
    }
}

static void do_calculate()
{
    const char *expr = lv_textarea_get_text(expr_ta);
    if (!expr || expr[0] == '\0') return;

    double result;
    char buf[64];
    if (evaluate(expr, &result)) {
        format_result(result, buf, sizeof(buf));
        lv_label_set_text_fmt(result_label, "= %s", buf);

        // Add to history
        if (history_count < MAX_HISTORY) {
            strncpy(history[history_count], expr, MAX_EXPR_LEN - 1);
            strncpy(history_results[history_count], buf, 63);
            history_count++;
        } else {
            memmove(history[0], history[1], (MAX_HISTORY - 1) * MAX_EXPR_LEN);
            memmove(history_results[0], history_results[1], (MAX_HISTORY - 1) * 64);
            strncpy(history[MAX_HISTORY - 1], expr, MAX_EXPR_LEN - 1);
            strncpy(history_results[MAX_HISTORY - 1], buf, 63);
        }
    } else {
        lv_label_set_text(result_label, "= Error");
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        disable_keyboard();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }
        menu_show();
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);
        if (code == LV_EVENT_CLICKED) {
            if (edited) {
                lv_group_set_editing(lv_obj_get_group(ta), false);
                disable_keyboard();
            }
        } else if (code == LV_EVENT_FOCUSED) {
            if (edited) {
                enable_keyboard();
            }
        }
    }

    if (code == LV_EVENT_KEY) {
        lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
        if (key == LV_KEY_ENTER) {
            lv_textarea_delete_char(ta);
            do_calculate();
            lv_event_stop_processing(e);
        }
    }

    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
        if (code == LV_EVENT_KEY) {
            lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
            if (key == LV_KEY_ENTER) {
                lv_textarea_delete_char(ta);
                do_calculate();
                lv_event_stop_processing(e);
            }
        }
    }
}

void ui_calculator_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(cont, LV_FLEX_ALIGN_START, LV_PART_MAIN);

    // Expression input
    expr_ta = lv_textarea_create(cont);
    lv_obj_set_width(expr_ta, lv_pct(100));
    lv_obj_set_height(expr_ta, 45);
    lv_textarea_set_placeholder_text(expr_ta, "e.g. sin(45)+log(100)*2");
    lv_textarea_set_one_line(expr_ta, true);
    lv_textarea_set_max_length(expr_ta, MAX_EXPR_LEN);
    lv_obj_add_event_cb(expr_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    // Result display
    result_label = lv_label_create(cont);
    lv_obj_set_width(result_label, lv_pct(100));
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(result_label, lv_color_make(0, 200, 0), LV_PART_MAIN);
    lv_label_set_text(result_label, "= ");

    // Bottom row: DEG/RAD toggle + Clear
    lv_obj_t *btn_row = lv_obj_create(cont);
    lv_obj_set_size(btn_row, lv_pct(100), 40);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN);

    // DEG/RAD button
    lv_obj_t *mode_btn = lv_btn_create(btn_row);
    lv_obj_set_size(mode_btn, 80, 35);
    mode_label = lv_label_create(mode_btn);
    lv_label_set_text(mode_label, "DEG");
    lv_obj_center(mode_label);
    lv_obj_add_event_cb(mode_btn, [](lv_event_t *e) {
        deg_mode = !deg_mode;
        lv_label_set_text(mode_label, deg_mode ? "DEG" : "RAD");
    }, LV_EVENT_CLICKED, NULL);

    // Clear button
    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_size(clear_btn, 80, 35);
    lv_obj_t *clr_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clr_lbl, "Clear");
    lv_obj_center(clr_lbl);
    lv_obj_add_event_cb(clear_btn, [](lv_event_t *e) {
        lv_textarea_set_text(expr_ta, "");
        lv_label_set_text(result_label, "= ");
    }, LV_EVENT_CLICKED, NULL);

    // History button
    lv_obj_t *hist_btn = lv_btn_create(btn_row);
    lv_obj_set_size(hist_btn, 80, 35);
    lv_obj_t *hist_lbl = lv_label_create(hist_btn);
    lv_label_set_text(hist_lbl, "History");
    lv_obj_center(hist_lbl);
    lv_obj_add_event_cb(hist_btn, [](lv_event_t *e) {
        if (history_count == 0) {
            ui_msg_pop_up("History", "No history yet.");
            return;
        }
        // Build history string
        static char hist_buf[2048];
        hist_buf[0] = '\0';
        for (int i = history_count - 1; i >= 0; i--) {
            char line[384];
            snprintf(line, sizeof(line), "%s = %s\n", history[i], history_results[i]);
            strncat(hist_buf, line, sizeof(hist_buf) - strlen(hist_buf) - 1);
        }
        ui_msg_pop_up("History", hist_buf);
    }, LV_EVENT_CLICKED, NULL);

    lv_menu_set_page(menu, main_page);

    // Focus the textarea and enable keyboard
    lv_group_add_obj(lv_group_get_default(), expr_ta);
    lv_group_focus_obj(expr_ta);
    lv_obj_add_state(expr_ta, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
    enable_keyboard();

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_calculator_exit(lv_obj_t *parent)
{
}

app_t ui_calculator_main = {
    .setup_func_cb = ui_calculator_enter,
    .exit_func_cb = ui_calculator_exit,
    .user_data = nullptr,
};
