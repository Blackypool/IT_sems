#ifndef MAIN_STR_REV_H
#define MAIN_STR_REV_H

#define FILE_INPUT stdin

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <algorithm>

//_________________________________________________________________________________________________________________________________________//
typedef char* tip_lac;
struct stk
{
    tip_lac* stack = 0;
    size_t size = 0;
    size_t capacity = 0; // = summ of all varia in program need save in main AST struct and calculate in read
};

struct string_s
{
    stk** all_words;

    size_t num_of_space;
    size_t true_num_str;
    
    long num_of_need_reverse;
};
//_________________________________________________________________________________________________________________________________________//

#define ERROR_IN_FUNC   -1
#define ALL_RIGHT       1

enum errors_
{
    memory_aloca    = 1,

    oversize_any    = 2,
    
    syntax_err      = 3,

    give_null_ptr   = 4,
    
    file_errorr     = 5,

    error_in_deep   = 6,

    stack_errorr    = 7,
};

//_______________________________________________DE_B_U_G_SS_______________________________________________________________________________//
#define AsserT(what_need, type_err, retern)                                                   \
            if(what_need) [[unlikely]]                                                         \
            {                                                                                   \
                fprintf(stderr, "\n\nerror in line, %s:%d. Problem with:", __FILE__, __LINE__);  \
                switch(type_err)                                                                  \
                {                                                                                  \
                    case memory_aloca:                                                              \
                        fprintf(stderr, " memory allocation \n");                                    \
                    break;                                                                            \
                                                                                                       \
                    case oversize_any:                                                                  \
                        fprintf(stderr, " oversize \n");                                                 \
                    break;                                                                                \
                                                                                                           \
                    case syntax_err:                                                                        \
                        fprintf(stderr, " some syntax error \n");                                            \
                    break;                                                                                    \
                                                                                                               \
                    case give_null_ptr:                                                                         \
                        fprintf(stderr, " you give here NULL ptr \n");                                           \
                    break;                                                                                        \
                                                                                                                   \
                    case file_errorr:                                                                               \
                        fprintf(stderr, " problem with openning file \n");                                           \
                    break;                                                                                            \
                                                                                                                       \
                    case error_in_deep:                                                                                 \
                        fprintf(stderr, " error in return of upper func \n");                                            \
                    break;                                                                                                \
                                                                                                                           \
                    case stack_errorr:                                                                                      \
                        fprintf(stderr, "some problem with stack (mb size)\n");                                              \
                    break;                                                                                                    \
                                                                                                                               \
                    default:                                                                                                    \
                        fprintf(stderr, " undefined error \n");                                                                  \
                    break;                                                                                                        \
                }                                                                                                                  \
                                                                                                                                    \
                return retern;                                                                                                       \
            }                                                                                                                     
//_________________________________________________________________________________________________________________________________________//


int init_of_strs(string_s* STR_S, char* need_num_from_stdin);

int parsing_to_words(char* eating_str, string_s* STR_S, size_t now_str);

int printing_result(string_s* STR_S);

void reverse_need_word(char* need_reverse);

void print_full_task(string_s* STR_S, size_t i);

void stack_push(struct stk* data, tip_lac value);

tip_lac stack_pop(struct stk* data);

void free_all(string_s* STR_S);


#endif