/* Copyright 2026 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file smfunc_calc.c
 * This file contains expression parser for calc().
 *
 *  @author Bernhard R. Fischer
 *  \date 2026/11/05
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "rdata.h"
#include "smrender_dev.h"


#define MAX_TOKENS 256
#define MAX_TOKEN_LEN 64
#define MAX_VARS 64

// Variable symbol table entry
typedef struct 
{
   char name[32];
   double value;
   int ro;
} calc_var_t;

// calc_parser_t structure
typedef struct 
{
   int token_count;
   int pos;
   int err;
   int var_count;
   char tokens[MAX_TOKENS][MAX_TOKEN_LEN];
   calc_var_t vars[MAX_VARS];
} calc_parser_t;

// Math function mapping
typedef struct 
{
   char name[32];
   double (*fn)(double);
} MathFunc;

typedef struct
{
   char *expr;
   char *restag;
} sm_calc_parser_t;

static MathFunc math_funcs[] = 
{
   {"sin", sin},
   {"cos", cos},
   {"tan", tan},
   {"asin", asin},
   {"acos", acos},
   {"atan", atan},
   {"sqrt", sqrt},
   {"log", log},
   {"abs", fabs},
   {"exp", exp}
};
static int math_funcs_count = 10;

// Function declarations
double parse_expr(calc_parser_t *p);
double parse_term(calc_parser_t *p);
double parse_factor(calc_parser_t *p);


/*! Test if character is valid expression character which is [a-zA-Z0-9_$].
 * @param c Input character.
 * @return returns 1 if input characters is valid, otherwise 0 is returned.
 */
int isvarc(int c)
{
   return isalnum(c) || c == '_' || c == '$';
}


int isexpterm(int c)
{
   return c == '\0' || c == ',' || c == ';';
}


/*! Tokenize expression string.
 * @param p Pointer to parser struct.
 * @param input Input string.
 */
int tokenize(calc_parser_t *p, const char *input) 
{
   int i, start, len;

   p->token_count = 0;
   p->pos = 0;
   for (i = 0; !isexpterm(input[i]) && p->token_count < MAX_TOKENS; )
   {
      // Skip whitespace
      if (isspace((unsigned char) input[i])) 
      {
         i++;
         continue;
      }
      // Numbers
      if (isdigit((unsigned char) input[i]) || input[i] == '.') 
      {
         for (start = i; isdigit((unsigned char) input[i]) || input[i] == '.'; i++);
         len = i - start;
         if (len >= MAX_TOKEN_LEN)
            len = MAX_TOKEN_LEN - 1;
         strncpy(p->tokens[p->token_count], &input[start], len);
         p->tokens[p->token_count][len] = '\0';
         p->token_count++;
         continue;
      }
      // Identifiers (Variables or Functions)
      if (isvarc((unsigned char)input[i])) 
      {
         for (start = i; isvarc((unsigned char) input[i]); i++);
         len = i - start;
         if (len >= MAX_TOKEN_LEN) len = MAX_TOKEN_LEN - 1;
         strncpy(p->tokens[p->token_count], &input[start], len);
         p->tokens[p->token_count][len] = '\0';
         p->token_count++;
         continue;
      }
      // Operators and delimiters: +, -, *, /, (, ), =
      if (strchr("+-*/()=", input[i])) 
      {
         p->tokens[p->token_count][0] = input[i];
         p->tokens[p->token_count][1] = '\0';
         p->token_count++;
         i++;
         continue;
      }
      i++; // Skip unknown characters
   }

   return i;
}


/*! Return value for variable by name.
 * @param p Pointer to parser struct.
 * @param name Name of variable.
 * @return The function returns the value of the variable. If the variable is
 * not found in the list of variables, 0.0 is returned and the error counter of
 * the parser is increased.
 */
double get_var(calc_parser_t *p, const char *name) 
{
   for (int i = 0; i < p->var_count; i++) 
      if (strcmp(p->vars[i].name, name) == 0) 
         return p->vars[i].value;

   log_msg(LOG_WARN, "variable '%s' not found", name);
   p->err++;

   return 0;
}


/*! Set variable. If variable already exists, its value is overwritten.
 * Variables which are set to ro (read only) are not overwritten.
 * @param p Pointer to parser.
 * @param name Name of variable.
 * @param val Value of variable.
 * @return Returns the index of variable within the variable table which is 0
 * <= index < MAX_VARS. In case of error -1 is returned. Erros might be either
 * that the variable is set to ro or that the variable table is full.
 */
int set_var(calc_parser_t *p, const char *name, double val) 
{
   for (int i = 0; i < p->var_count; i++) 
   {
      if (!strcmp(p->vars[i].name, name)) 
      {
         if (p->vars[i].ro)
         {
            log_msg(LOG_WARN, "cannot re-set constant '%s'", name);
            p->err++;
            return -1;
         }
         p->vars[i].value = val;
         return i;
      }
   }

   if (p->var_count >= MAX_VARS) 
   {
      log_msg(LOG_WARN, "variable table full (var_count >= MAX_VARS(%d))", MAX_VARS);
      p->err++;
      return -1;
   }

   strncpy(p->vars[p->var_count].name, name, 31);
   p->vars[p->var_count].name[31] = '\0';
   p->vars[p->var_count].value = val;
   p->var_count++;

   return p->var_count - 1;
}


/*! This function sets a variable by calling set_var() and then sets the ro
 * flag to 1.
 * @see See set_var() for parameters and return value.
 */
int set_const(calc_parser_t *p, const char *name, double val) 
{
   int i = set_var(p, name, val);
   if (i < 0)
      return i;
   p->vars[i].ro = 1;
   return i;
}


double call_func(calc_parser_t *p, const char *name, double arg) 
{
   for (int i = 0; i < math_funcs_count; i++) 
      if (strcmp(math_funcs[i].name, name) == 0) 
         return math_funcs[i].fn(arg);

   log_msg(LOG_ERR, "unknown function: %s()", name);
   p->err++;

   return 0;
}


const char* peek(calc_parser_t *p) 
{
   if (p->pos < p->token_count) 
      return p->tokens[p->pos];

   return NULL;
}


int consume(calc_parser_t *p, const char *expected) 
{
   const char *token = peek(p);
   if (!token || strcmp(token, expected) != 0) 
   {
      log_msg(LOG_ERR, "calc_parser expected '%s' but found '%s'", expected, token ? token : "\\0");
      p->err++;
      return -1;
   }
   p->pos++;
   return 0;
}


// Main parse entry point (handles assignments or expressions)
double parse(calc_parser_t *p) 
{
   if (p->token_count == 0) return 0.0;

   // Check for variable assignment: ID = expr
   if (p->pos + 1 < p->token_count &&
      isalpha((unsigned char)p->tokens[p->pos][0]) &&
      strcmp(p->tokens[p->pos + 1], "=") == 0) 
   {
      char var_name[32];
      strcpy(var_name, p->tokens[p->pos]);
      p->pos += 2; // consume ID and '='
      double val = parse_expr(p);
      set_var(p, var_name, val);
      return val;
   }

   double result = parse_expr(p);
   if (p->pos < p->token_count) 
   {
      log_msg(LOG_WARN, "unexpected token '%s'", p->tokens[p->pos]);
      p->err++;
   }

   if (p->err)
      log_msg(LOG_WARN, "parser returned %d errors", p->err);

   return result;
}

// expr = term ((‘+’ | ‘-’) term)*
double parse_expr(calc_parser_t *p) 
{
   double node = parse_term(p);
   const char *tk;
   while ((tk = peek(p)) != NULL && (strcmp(tk, "+") == 0 || strcmp(tk, "-") == 0)) 
   {
      char op = tk[0];
      p->pos++;
      double right = parse_term(p);
      if (op == '+') node += right;
      else node -= right;
   }
   return node;
}

// term = factor ((‘*’ | ‘/’) factor)*
double parse_term(calc_parser_t *p) 
{
   double node = parse_factor(p);
   const char *tk;
   while ((tk = peek(p)) != NULL && (strcmp(tk, "*") == 0 || strcmp(tk, "/") == 0)) 
   {
      char op = tk[0];
      p->pos++;
      double right = parse_factor(p);
      if (op == '/') 
      {
         if (right == 0.0) 
         {
            log_msg(LOG_ERR, "division by zero");
            p->err++;
         }
         else
            node /= right;
      } 
      else 
      {
         node *= right;
      }
   }
   return node;
}

// factor = NUMBER | VARIABLE | FUNCTION ‘(‘ expr ‘)’ | ‘(‘ expr ‘)’
double parse_factor(calc_parser_t *p) 
{
   const char *token = peek(p);
   if (!token) 
   {
      log_msg(LOG_ERR, "unexpected end of expression");
      p->err++;
      return 0;
   }

   // Parentheses
   if (strcmp(token, "(") == 0) 
   {
      p->pos++;
      double node = parse_expr(p);
      consume(p, ")");
      return node;
   }

   // Number literal
   char *endptr;
   double num = strtod(token, &endptr);
   if (*endptr == '\0') 
   {
      p->pos++;
      return num;
   }

   // Identifier (Function or Variable lookup)
   if (isvarc((unsigned char)token[0])) 
   {
      char name[32];
      strcpy(name, token);
      p->pos++;

      // Function call check
      const char *next_tk = peek(p);
      if (next_tk && strcmp(next_tk, "(") == 0) 
      {
         p->pos++; // consume '('
         double arg = parse_expr(p);
         consume(p, ")");
         return call_func(p, name, arg);
      }

      // Variable lookup
      return get_var(p, name);
   }

   log_msg(LOG_ERR, "unexpected token '%s'", token);
   p->err++;

   return 0;
}


void init_parser(calc_parser_t *p)
{
   memset(p, 0, sizeof(*p));
   set_const(p, "PI", M_PI);
   set_const(p, "E", M_E);
}


int act_calc_ini(smrule_t *r)
{
   sm_calc_parser_t *cp;

   if ((cp = calloc(1, sizeof(*cp))) == NULL)
   {
      log_errno(LOG_ERR, "calloc()");
      return -1;
   }

   if ((cp->expr = get_param("expr", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'expr' missing");
      goto ac_err;
   }

   if ((cp->restag = get_param("result", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'result' missing");
      goto ac_err;
   }

   log_debug("expr = \"%s\", result = \"%s\"", cp->expr, cp->restag);
   r->data = cp;
   return 0;

ac_err:
   free(cp);
   return 1;
}


/*! Set parser variables from OSM objects according to the action arguments
 * listed by repeated 'arg=' parameters. The first 'arg' will be assigned to
 * '$0' within the parser, the second to '$1' and so on.
 */
void set_vars(calc_parser_t *p, fparam_t **fp, const osm_obj_t *o)
{
   char buf[32];
   double val;
   char *key;
   int i, n;

   for (key = NULL, i = 0; *fp != NULL; fp++)
      if (!strcasecmp((*fp)->attr, "arg"))
      {
         key = (*fp)->val;

         val = 0;
         if ((n = match_attr(o, key, NULL)) >= 0)
            val = bs_tod(o->otag[n].v);

         snprintf(buf, sizeof(buf), "$%d", i);
         set_var(p, buf, val);
      }
}


int set_result(osm_obj_t *o, char *key, double val)
{
   char buf[32];
   int n;

   snprintf(buf, sizeof(buf), "%f", val);

   if ((n = match_attr(o, key, NULL)) >= 0)
   {
      o->otag[n].v.buf = smstrdup(buf);
      o->otag[n].v.len = strlen(buf);
      return 0;
   }

   if (realloc_tags(o, o->tag_cnt + 1) == -1)
   {
      log_errno(LOG_ERR, "realloc_tags()");
      return -1;
   }

   set_const_tag(&o->otag[o->tag_cnt - 1], key, smstrdup(buf));

   return 0;
}


int act_calc_main(smrule_t *r, osm_obj_t *o)
{
   calc_parser_t p;
   double res = 0;
   char *expr;
   int i;

   init_parser(&p);
   set_vars(&p, r->act->fp, o);

   for (expr = ((sm_calc_parser_t*) r->data)->expr; *expr != '\0'; )
   {
      p.err = 0;
      i = tokenize(&p, expr);
      res = parse(&p);
      log_debug("res = %f, err = %d, i = %d, expr[i] = 0x%02x", res, p.err, i, expr[i]);
      if (p.err)
         return 1;

      expr += i;
      if (*expr != '\0')
         expr++;
   }

   return set_result(o, ((sm_calc_parser_t*) r->data)->restag, res);
}


int act_calc_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


//#define TEST_CALC_PARSER
#ifdef TEST_CALC_PARSER
int main() 
{
   calc_parser_t parser = {0};

   // Pre-define initial variables (like M_PI)
   set_var(&parser, "pi", M_PI);
   set_var(&parser, "x", 10.0);

   const char *expressions[] = 
   {
      "sin(pi / 2.3), x*5.33",
      "sqrt(x + 6)",
      "tan(pi / 4) * 2",
      "y = sin(pi / 6)",
      "y * 10",
      "13.3",
      NULL
   };

   for (int i = 0; expressions[i] != NULL; i++) 
   {
      printf("Evaluating: %s\n", expressions[i]);
      parser.err = 0;
      int n = tokenize(&parser, expressions[i]);
      double res = parse(&parser);
      printf("Result = %f\n\n", res);

      if (expressions[i][n] != '\0')
      {
         expressions[i] += n + 1;
         i--;
      }
   }

   return 0;
}
#endif
