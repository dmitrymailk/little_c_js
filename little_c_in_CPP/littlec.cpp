/* A Little C interpreter. */

#include <stdio.h>
#include <setjmp.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#define NUMBER_FUNCTIONS 100
#define NUM_GLOBAL_VARS 100
#define NUM_LOCAL_VARS 200
#define NUM_BLOCK 100
#define ID_LEN 32
#define FUNC_CALLS 31
#define NUM_PARAMS 31
#define PROG_SIZE 10000
#define LOOP_NEST 31

// Secure function compatibility
#if !defined(_MSC_VER) || _MSC_VER < 1400
#define strcpy_s(dest, count, source) strncpy((dest), (source), (count))
#define fopen_s(pFile, filename, mode) (((*(pFile)) = fopen((filename), (mode))) == NULL)
#endif

enum token_types
{
	DELIMITER,
	// может быть именем переменной, функции или константы
	VARIABLE,
	NUMBER,
	KEYWORD,
	TEMP,
	STRING,
	BLOCK
};

/* add additional C keyword tokens here */
enum tokens
{
	ARG,
	CHAR,
	INT,
	IF,
	ELSE,
	FOR,
	DO,
	WHILE,
	SWITCH,
	RETURN,
	CONTINUE,
	BREAK,
	EOL,
	FINISHED,
	END
};

/* add additional double operators here (such as ->) */
enum double_ops
{
	LOWER = 1,
	LOWER_OR_EQUAL,
	GREATER,
	GREATER_OR_EQUAL,
	EQUAL,
	NOT_EQUAL
};

/* These are the constants used to call syntax_error() when
   a syntax error occurs. Add more if you like.
   NOTE: SYNTAX is a generic error message used when
   nothing else seems appropriate.
*/
enum error_msg
{
	SYNTAX,
	UNBAL_PARENS,
	NO_EXP,
	EQUALS_EXPECTED,
	NOT_VAR,
	PARAM_ERR,
	SEMICOLON_EXPECTED,
	UNBAL_BRACES,
	FUNC_UNDEFINED,
	TYPE_EXPECTED,
	NESTED_FUNCTIONS,
	RET_NOCALL,
	PAREN_EXPECTED,
	WHILE_EXPECTED,
	QUOTE_EXPECTED,
	NOT_TEMP,
	TOO_MANY_LVARS,
	DIV_BY_ZERO
};

char *source_code_location; /* current location in source code */
char *program_start_buffer; /* points to start of program buffer */
jmp_buf execution_buffer;	/* hold environment for longjmp() */

/* An array of these structures will hold the info
   associated with global variables.
*/
struct variable_type
{
	char variable_name[ID_LEN];
	int variable_type;
	int variable_value;
} global_vars[NUM_GLOBAL_VARS];

struct variable_type local_var_stack[NUM_LOCAL_VARS];

struct function_type
{
	char func_name[ID_LEN];
	int ret_type;
	char *loc; /* location of entry point in file */
} function_table[NUMBER_FUNCTIONS];

int call_stack[NUMBER_FUNCTIONS];

struct commands
{ /* keyword lookup table_with_statements */
	char command[20];
	char tok;
} table_with_statements[] = {
	/* Commands must be entered lowercase */
	{"if", IF}, /* in this table_with_statements. */
	{"else", ELSE},
	{"for", FOR},
	{"do", DO},
	{"while", WHILE},
	{"char", CHAR},
	{"int", INT},
	{"return", RETURN},
	{"continue", CONTINUE},
	{"break", BREAK},
	{"end", END},
	{"", END} /* mark end of table_with_statements */
};

char current_token[80];
char token_type, current_tok_datatype;

int function_last_index_on_call_stack; /* index to top of function call stack */
int function_position;				   /* index into function table_with_statements */
int global_variable_position;		   /* индекс глобальной переменной в таблице global_vars */
int lvartos;						   /* index into local variable stack */

int ret_value;		 /* function return value */
int ret_occurring;	 /* function return is occurring */
int break_occurring; /* loop break is occurring */

void print(void), prescan_source_code(void);
void declare_global_variables(void), call_function(void), shift_source_code_location_back(void);
void declare_local_variables(void), local_push(struct variable_type i);
void eval_expression(int *value), syntax_error(int error);
void execute_if_statement(void), find_eob(void), exec_for(void);
void get_function_parameters(void), get_function_arguments(void);
void exec_while(void), function_push_variables_on_call_stack(int i), exec_do(void);
void assign_var(char *var_name, int value);
int load_program(char *p, char *fname), find_var(char *s);
void interpret_block(void), function_return(void);
int func_pop(void), is_variable(char *s);
char *find_function_in_function_table(char *name), get_next_token(void);

using namespace std;

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		printf("Usage: littlec <filename>\n");
		exit(1);
	}

	/* выделить память под программу
	 * PROG_SIZE - размер программы*/
	if ((program_start_buffer = (char *)malloc(PROG_SIZE)) == NULL)
	{
		printf("Allocation Failure"); //если программа пустая
		exit(1);
	}

	/* загрузить программу для выполнения */
	char *file_name = argv[1];
	if (!load_program(program_start_buffer, file_name))
		exit(1);
	if (setjmp(execution_buffer))
		exit(1); /* инициализация буфера longjump */

	global_variable_position = 0; /* инициализация индекса глобальных переменных */

	/* установка указателя на начало буфера программы  */
	source_code_location = program_start_buffer;
	prescan_source_code(); /* определение адресов всех функций
				  и глобальных переменных
				  короче говоря, предварительный проход компилятора */

	lvartos = 0;						   /* инициализация индекса стека локальных переменных */
	function_last_index_on_call_stack = 0; /* инициализация индекса стека вызова CALL */
	break_occurring = 0;				   /* initialize the break occurring flag */

	/* вызываем функцию main
	 * она всегда вызывается первой*/
	source_code_location = find_function_in_function_table("main"); /* ищем начало программы */

	if (!source_code_location)
	{ /* main написан с ошибкой или отсутствует */
		printf("main() not found.\n");
		exit(1);
	}

	source_code_location--; /* возвращаемся к открывающей ( */
	strcpy_s(current_token, 80, "main");
	call_function(); /* вызываем main и интерпретируем */

	return 0;
}

/* Interpret a single statement or block of code. When
   interpret_block() returns from its initial call, the final
   brace (or a return) in main() has been encountered.
*/
void interpret_block(void)
{
	int value;
	char block = 0;

	do
	{
		token_type = get_next_token();

		/* If interpreting single statement, return on
		   first semicolon.
		*/

		/* see what kind of current_token is up */
		if (token_type == VARIABLE)
		{
			/* Not a keyword, so process expression. */
			shift_source_code_location_back(); /* restore current_token to input stream for
						  further processing by eval_exp() */
			eval_expression(&value);		   /* process the expression */
			if (*current_token != ';')
				syntax_error(SEMICOLON_EXPECTED);
		}
		else if (token_type == BLOCK)
		{							   /* if block delimiter */
			if (*current_token == '{') /* is a block */
				block = 1;			   /* interpreting block, not statement */
			else
				return; /* is a }, so return */
		}
		else /* is keyword */
			switch (current_tok_datatype)
			{
			case CHAR:
			case INT: /* declare local variables */
				shift_source_code_location_back();
				declare_local_variables();
				break;
			case RETURN: /* return from function call */
				function_return();
				ret_occurring = 1;
				return;
			case CONTINUE: /* continue loop execution */
				return;
			case BREAK: /* break loop execution */
				break_occurring = 1;
				return;
			case IF: /* process an if statement */
				execute_if_statement();
				if (ret_occurring > 0 || break_occurring > 0)
				{
					return;
				}
				break;
			case ELSE:		/* process an else statement */
				find_eob(); /* find end of else block and continue execution */
				break;
			case WHILE: /* process a while loop */
				exec_while();
				if (ret_occurring > 0)
				{
					return;
				}
				break;
			case DO: /* process a do-while loop */
				exec_do();
				if (ret_occurring > 0)
				{
					return;
				}
				break;
			case FOR: /* process a for loop */
				exec_for();
				if (ret_occurring > 0)
				{
					return;
				}
				break;
			case END:
				exit(0);
			}
	} while (current_tok_datatype != FINISHED && block);
}

/* Загрузить программу */
int load_program(char *p, char *fname)
{
	FILE *fp;
	int i;

	if (fopen_s(&fp, fname, "rb") != 0 || fp == NULL)
		return 0;

	i = 0;
	do
	{
		*p = (char)getc(fp);
		p++;
		i++;
	} while (!feof(fp) && i < PROG_SIZE);

	if (*(p - 2) == 0x1a) // рудимент из бейсика. Ставится в конце исполняемого файла
		*(p - 2) = '\0';  /* конец строки завершает программу */
	else
		*(p - 1) = '\0';
	fclose(fp);
	return 1;
}

/* Найти адреса всех функций
   и запомнить глобальные переменные. */
void prescan_source_code(void) // Предварительный проход компилятора
{
	char *initial_source_code_location, *temp_source_code_location; //*p - указатель на указатель ? (На source_code_location). temp_source_code_location тоже указатель на source_code_location???
	char temp_token[ID_LEN + 1];
	int datatype;
	int is_brace_open = 0; /* Если is_brace_open = 0, о текущая
					  позиция оказателя программы находится
					  в не какой-либо функции */

	initial_source_code_location = source_code_location;
	function_position = 0;
	do
	{
		while (is_brace_open)
		{ /* обхода кода функции внутри фигурных скобок */
			get_next_token();
			if (*current_token == '{') //когда встречаем открывающую скобку, увеличиваем is_brace_open на один
				is_brace_open++;
			if (*current_token == '}')
				is_brace_open--; //когда встречаем закрывающую уменьшаем на один
		}

		temp_source_code_location = source_code_location; /* запоминаем текущую позицию */
		get_next_token();
		/* тип глобальной переменной или возвращаемого значения функции */
		if (current_tok_datatype == CHAR || current_tok_datatype == INT)
		{
			datatype = current_tok_datatype; /* сохраняем тип данных */
			get_next_token();
			if (token_type == VARIABLE)
			{
				//
				strcpy_s(temp_token, ID_LEN + 1, current_token);
				get_next_token();
				if (*current_token != '(')
				{													  /* должно быть глобальной переменной */
					source_code_location = temp_source_code_location; /* вернуться в начало объявления */
					declare_global_variables();
				}
				else if (*current_token == '(')
				{ /* должно быть функцией */
					function_table[function_position].loc = source_code_location;
					function_table[function_position].ret_type = datatype;
					strcpy_s(function_table[function_position].func_name, ID_LEN, temp_token);
					function_position++;
					while (*source_code_location != ')')
						source_code_location++;
					source_code_location++;
					/* сейчас source_code_location указывает на открывающуюся
					   фигурную скобку функции */
				}
				else
					shift_source_code_location_back();
			}
		}
		else if (*current_token == '{')
			is_brace_open++;
	} while (current_tok_datatype != FINISHED);
	source_code_location = initial_source_code_location;
}

/* Return the entry point of the specified function.
   Return NULL if not found.
*/
char *find_function_in_function_table(char *name)
{
	register int function_pos;

	for (function_pos = 0; function_pos < function_position; function_pos++)
		if (!strcmp(name, function_table[function_pos].func_name))
			return function_table[function_pos].loc;

	return NULL;
}

/* Объявление глобальной переменной в ИНТЕРПРЕТИРУЕМОЙ программе
 * Данные хранятся в списке global vars */
void declare_global_variables(void)
{
	int variable_type;

	get_next_token(); /* получаем тип данных */

	variable_type = current_tok_datatype; /* запоминаем тип данных */

	do
	{ /* обработка списка с разделителями запятыми */
		global_vars[global_variable_position].variable_type = variable_type;
		global_vars[global_variable_position].variable_value = 0; /* инициализируем нулем */
		get_next_token();										  /* определяем имя */
		strcpy_s(global_vars[global_variable_position].variable_name, ID_LEN, current_token);
		get_next_token();
		global_variable_position++;
	} while (*current_token == ',');
	if (*current_token != ';')
		syntax_error(SEMICOLON_EXPECTED);
}

/* Declare a local variable. */
void declare_local_variables(void)
{
	struct variable_type i;

	get_next_token(); /* get type */

	i.variable_type = current_tok_datatype;
	i.variable_value = 0; /* init to 0 */

	do
	{					  /* process comma-separated list */
		get_next_token(); /* get var name */
		strcpy_s(i.variable_name, ID_LEN, current_token);
		local_push(i);
		get_next_token();
	} while (*current_token == ',');
	if (*current_token != ';')
		syntax_error(SEMICOLON_EXPECTED);
}

/* Call a function. */
void call_function(void)
{
	char *function_location, *temp_source_code_location;
	int lvartemp;

	function_location = find_function_in_function_table(current_token); /* find entry point of function */
	if (function_location == NULL)
		syntax_error(FUNC_UNDEFINED); /* function not defined */
	else
	{
		lvartemp = lvartos;								  /* save local var stack index */
		get_function_arguments();						  /* get function arguments */
		temp_source_code_location = source_code_location; /* save return location */
		function_push_variables_on_call_stack(lvartemp);  /* save local var stack index */
		source_code_location = function_location;		  /* reset prog to start of function */
		ret_occurring = 0;								  /* P the return occurring variable */
		get_function_parameters();						  /* load the function's parameters with the values of the arguments */
		interpret_block();								  /* interpret the function */
		ret_occurring = 0;								  /* Clear the return occurring variable */
		source_code_location = temp_source_code_location; /* reset the program initial_source_code_location */
		lvartos = func_pop();							  /* reset the local var stack */
	}
}

/* Push the arguments to a function onto the local
   variable stack. */
void get_function_arguments(void)
{
	int value, count, temp[NUM_PARAMS];
	struct variable_type i;

	count = 0;
	get_next_token();
	if (*current_token != '(')
		syntax_error(PAREN_EXPECTED);

	/* process a comma-separated list of values */
	do
	{
		eval_expression(&value);
		temp[count] = value; /* save temporarily */
		get_next_token();
		count++;
	} while (*current_token == ',');
	count--;
	/* now, push on local_var_stack in reverse order */
	for (; count >= 0; count--)
	{
		i.variable_value = temp[count];
		i.variable_type = ARG;
		local_push(i);
	}
}

/* Get function parameters. */
void get_function_parameters(void)
{
	struct variable_type *variable_type_pointer;
	int position;

	position = lvartos - 1;
	do
	{ /* process comma-separated list of parameters */
		get_next_token();
		variable_type_pointer = &local_var_stack[position];
		if (*current_token != ')')
		{
			if (current_tok_datatype != INT && current_tok_datatype != CHAR)
				syntax_error(TYPE_EXPECTED);

			variable_type_pointer->variable_type = token_type;
			get_next_token();

			/* link parameter name with argument already on
			   local var stack */
			strcpy_s(variable_type_pointer->variable_name, ID_LEN, current_token);
			get_next_token();
			position--;
		}
		else
			break;
	} while (*current_token == ',');
	if (*current_token != ')')
		syntax_error(PAREN_EXPECTED);
}

/* Return from a function. */
void function_return(void)
{
	int value;

	value = 0;
	/* get return value, if any */
	eval_expression(&value);

	ret_value = value;
}

/* Push a local variable. */
void local_push(struct variable_type i)
{
	if (lvartos >= NUM_LOCAL_VARS)
	{
		syntax_error(TOO_MANY_LVARS);
	}
	else
	{
		local_var_stack[lvartos] = i;
		lvartos++;
	}
}

/* Pop index into local variable stack. */
int func_pop(void)
{
	int index = 0;
	function_last_index_on_call_stack--;
	if (function_last_index_on_call_stack < 0)
	{
		syntax_error(RET_NOCALL);
	}
	else if (function_last_index_on_call_stack >= NUMBER_FUNCTIONS)
	{
		syntax_error(NESTED_FUNCTIONS);
	}
	else
	{
		index = call_stack[function_last_index_on_call_stack];
	}

	return index;
}

/* Push index of local variable stack. */
// добавляет локальные переменные функции в стек
void function_push_variables_on_call_stack(int i)
{
	if (function_last_index_on_call_stack >= NUMBER_FUNCTIONS)
	{
		syntax_error(NESTED_FUNCTIONS);
	}
	else
	{
		call_stack[function_last_index_on_call_stack] = i;
		function_last_index_on_call_stack++;
	}
}

/* Assign a value to a variable. */
void assign_var(char *var_name, int value)
{
	register int i;

	/* first, see if it's a local variable */
	for (i = lvartos - 1; i >= call_stack[function_last_index_on_call_stack - 1]; i--)
	{
		if (!strcmp(local_var_stack[i].variable_name, var_name))
		{
			local_var_stack[i].variable_value = value;
			return;
		}
	}
	if (i < call_stack[function_last_index_on_call_stack - 1])
		/* if not local, try global var table_with_statements */
		for (i = 0; i < NUM_GLOBAL_VARS; i++)
			if (!strcmp(global_vars[i].variable_name, var_name))
			{
				global_vars[i].variable_value = value;
				return;
			}
	syntax_error(NOT_VAR); /* variable not found */
}

/* Find the value of a variable. */
int find_var(char *s)
{
	register int i;

	/* first, see if it's a local variable */
	for (i = lvartos - 1; i >= call_stack[function_last_index_on_call_stack - 1]; i--)
		if (!strcmp(local_var_stack[i].variable_name, current_token))
			return local_var_stack[i].variable_value;

	/* otherwise, try global vars */
	for (i = 0; i < NUM_GLOBAL_VARS; i++)
		if (!strcmp(global_vars[i].variable_name, s))
			return global_vars[i].variable_value;

	syntax_error(NOT_VAR); /* variable not found */
	return -1;
}

/* Determine if an identifier is a variable. Return
   1 if variable is found; 0 otherwise.
*/
int is_variable(char *s)
{
	register int i;

	/* first, see if it's a local variable */
	for (i = lvartos - 1; i >= call_stack[function_last_index_on_call_stack - 1]; i--)
		if (!strcmp(local_var_stack[i].variable_name, current_token))
			return 1;

	/* otherwise, try global vars */
	for (i = 0; i < NUM_GLOBAL_VARS; i++)
		if (!strcmp(global_vars[i].variable_name, s))
			return 1;

	return 0;
}

/* Execute an if statement. */
void execute_if_statement(void)
{
	int condition;

	eval_expression(&condition); /* get if expression */

	if (condition)
	{ /* is true so process target of IF */
		interpret_block();
	}
	else
	{				/* otherwise skip around IF block and
					process the ELSE, if present */
		find_eob(); /* find start of next line */
		get_next_token();

		if (current_tok_datatype != ELSE)
		{
			shift_source_code_location_back(); /* restore current_token if
						  no ELSE is present */
			return;
		}
		interpret_block();
	}
}

/* Execute a while loop. */
void exec_while(void)
{
	int cond;
	char *temp;

	break_occurring = 0; /* clear the break flag */
	shift_source_code_location_back();
	temp = source_code_location; /* save location of top of while loop */
	get_next_token();
	eval_expression(&cond); /* check the conditional expression */
	if (cond)
	{
		interpret_block(); /* if true, interpret */
		if (break_occurring > 0)
		{
			break_occurring = 0;
			return;
		}
	}
	else
	{ /* otherwise, skip around loop */
		find_eob();
		return;
	}
	source_code_location = temp; /* loop back to top */
}

/* Execute a do loop. */
void exec_do(void)
{
	int cond;
	char *temp;

	shift_source_code_location_back();
	temp = source_code_location; /* save location of top of do loop */
	break_occurring = 0;		 /* clear the break flag */

	get_next_token();  /* get start of loop */
	interpret_block(); /* interpret loop */
	if (ret_occurring > 0)
	{
		return;
	}
	else if (break_occurring > 0)
	{
		break_occurring = 0;
		return;
	}
	get_next_token();
	if (current_tok_datatype != WHILE)
		syntax_error(WHILE_EXPECTED);
	eval_expression(&cond); /* check the loop condition */
	if (cond)
		source_code_location = temp; /* if true loop; otherwise,
					   continue on */
}

/* Find the end of a block. */
void find_eob(void)
{
	int brace;

	get_next_token();
	brace = 1;
	do
	{
		get_next_token();
		if (*current_token == '{')
			brace++;
		else if (*current_token == '}')
			brace--;
	} while (brace);
}

/* Execute a for loop. */
void exec_for(void)
{
	int cond;
	char *temp, *temp2;
	int brace;

	break_occurring = 0; /* clear the break flag */
	get_next_token();
	eval_expression(&cond); /* initialization expression */
	if (*current_token != ';')
		syntax_error(SEMICOLON_EXPECTED);
	source_code_location++; /* get past the ; */
	temp = source_code_location;
	for (;;)
	{
		eval_expression(&cond); /* check the condition */
		if (*current_token != ';')
			syntax_error(SEMICOLON_EXPECTED);
		source_code_location++; /* get past the ; */
		temp2 = source_code_location;

		/* find the start of the for block */
		brace = 1;
		while (brace)
		{
			get_next_token();
			if (*current_token == '(')
				brace++;
			if (*current_token == ')')
				brace--;
		}

		if (cond)
		{
			interpret_block(); /* if true, interpret */
			if (ret_occurring > 0)
			{
				return;
			}
			else if (break_occurring > 0)
			{
				break_occurring = 0;
				return;
			}
		}
		else
		{ /* otherwise, skip around loop */
			find_eob();
			return;
		}
		source_code_location = temp2;
		eval_expression(&cond);		 /* do the increment */
		source_code_location = temp; /* loop back to top */
	}
}
