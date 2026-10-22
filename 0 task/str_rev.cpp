#include "M_str_rev.h"


#define NUM_STROK               10
#define MAX_SIZE_OF_STR         128
#define MAX_NUM_OF_WORDS_IN_STR 16


//_________________________________________________________________________________________________________________________________________//
int main(int argc, char* argv[])
{
    AsserT(argc < 2, syntax_err, 0);

    ////////////////////INIT_STRUCT_OF_STR_S////////////////////
    string_s STR_S = {0};
    int err = init_of_strs(&STR_S, argv[1]);
    AsserT(err == ERROR_IN_FUNC, error_in_deep, 0);
    ////////////////////////////////////////////////////////////

    char middle_buff[MAX_SIZE_OF_STR] = {0};
    for(size_t i = 0; fgets(middle_buff, MAX_SIZE_OF_STR, FILE_INPUT) != NULL; i++)
    {
        err = parsing_to_words(middle_buff, &STR_S, i);
        AsserT(err == ERROR_IN_FUNC, error_in_deep, 0);

        (STR_S.true_num_str)++;
    }

    err = printing_result(&STR_S);
    AsserT(err == ERROR_IN_FUNC, error_in_deep, 0);

    ////////////////////////////////////////////////////////////
    free_all(&STR_S);

    return 0;
}
//_________________________________________________________________________________________________________________________________________//


//___________________________________________INITIAL_OF_STRUCT + FREE______________________________________________________________________//
int init_of_strs(string_s* STR_S, char* need_num_from_stdin)
{
    AsserT(STR_S == NULL, give_null_ptr, ERROR_IN_FUNC);

    ////////////////////////////////////////////////////////////
    STR_S->num_of_need_reverse = strtol(need_num_from_stdin, NULL, 10);     // number of need reverse word
    ////////////////////////////////////////////////////////////

    STR_S->all_words = (stk**)calloc(NUM_STROK, sizeof(stk*));              // massive of ptr to struct of strings with words
    AsserT(STR_S->all_words == NULL, memory_aloca, ERROR_IN_FUNC);

    for(size_t i = 0; i < NUM_STROK; i++)
    {
        STR_S->all_words[i] = (stk*)calloc(1, sizeof(stk));                 // ptr to struct
        AsserT(STR_S->all_words[i] == NULL, memory_aloca, ERROR_IN_FUNC);

        STR_S->all_words[i]->stack = (char**)calloc(MAX_NUM_OF_WORDS_IN_STR, sizeof(char*));   // massive of words in struct for save in stack       
        AsserT(STR_S->all_words[i]->stack == NULL, memory_aloca, ERROR_IN_FUNC);
    }

    return ALL_RIGHT;
}


void free_all(string_s* STR_S)
{
    for(size_t i = 0; i < NUM_STROK && i < STR_S->true_num_str; i++)
    {
        free(STR_S->all_words[i]->stack);
        free(STR_S->all_words[i]);
    }

    free(STR_S->all_words);
}
//_________________________________________________________________________________________________________________________________________//


//____________________________________________________BODY_OF_TASK_________________________________________________________________________//
// функция принимает строку и структуру проекта, строку парсит на слова (хавая и накапливая количество пробелов), слова стекпушит
int parsing_to_words(char* eating_str, string_s* STR_S, size_t now_str)
{
    AsserT(eating_str   == NULL, give_null_ptr, ERROR_IN_FUNC);
    AsserT(STR_S        == NULL, give_null_ptr, ERROR_IN_FUNC);
    ///////////////////////////////////////////////////////////

    char ptr_to_word[MAX_SIZE_OF_STR] = {0};
    int offset_of_ptr = 0;
    int add_ptr = 0;

    while(sscanf(eating_str + offset_of_ptr, "%s%n", ptr_to_word, &add_ptr) != EOF)
    {
        stack_push(STR_S->all_words[now_str], strdup(ptr_to_word));
        (STR_S->num_of_space)++;

        offset_of_ptr = offset_of_ptr + add_ptr;
    }

    return ALL_RIGHT;
}


int printing_result(string_s* STR_S)
{
    AsserT(STR_S == NULL, give_null_ptr, ERROR_IN_FUNC);

    long nnn = STR_S->num_of_need_reverse;
    // first -- find need word and reverse him, than stckpop+printf
    for(size_t i = 0; i < NUM_STROK && i < STR_S->true_num_str; i++, fprintf(stderr, "\n"))
    {
        reverse_need_word(STR_S->all_words[i]->stack[nnn]); // передается структура просто одной строки и номер кого реверсать

        print_full_task(STR_S, i);
    }

    return ALL_RIGHT;
}


void reverse_need_word(char* need_reverse)
{
    size_t len = strlen(need_reverse);
    std::reverse(need_reverse, need_reverse + len);
}


void print_full_task(string_s* STR_S, size_t i)
{
    fprintf(stderr, "\n__________________________________________\n");

    size_t capacity_of_now_stk = STR_S->all_words[i]->size;

    for(size_t k = 0; k < MAX_NUM_OF_WORDS_IN_STR && k < capacity_of_now_stk; k++)
        fprintf(stderr, "%s ", stack_pop(STR_S->all_words[i]));

    fprintf(stderr, "\n__________________________________________\n");
}
//_________________________________________________________________________________________________________________________________________//


//_______________________________________________STACK_FUNC_________________________________________________________________________________//
void stack_push(struct stk* data, tip_lac value)
{
    data->stack[data->size] = value;
    data->size++;
}

tip_lac stack_pop(struct stk* data)
{
    data->size--;
    AsserT(data->size < 0, stack_errorr, NULL);

    tip_lac paramchik = data->stack[data->size];

    return paramchik;
}
//_________________________________________________________________________________________________________________________________________//