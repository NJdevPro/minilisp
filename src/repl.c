#ifndef __COSMOPOLITAN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include <unistd.h>
#include "../bestline/bestline.h"
#include "ketopt.h"
#include "gc.h"
#include "minilisp.h"

int ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr) 
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

void completion(const char *buf, int pos, bestlineCompletions *lc) {
    (void) pos;
    if (ends_with(buf, "(")) {
        bestlineAddCompletion(lc,"()");
    }
    if (ends_with(buf, "pr")) {
        bestlineAddCompletion(lc,"println");
    }
}

char *hints(const char *buf, const char **ansi1, const char **ansi2) {
    if (!strcmp(buf,"defun")) {
        *ansi1 = "\033[35m"; /* magenta foreground */
        *ansi2 = "\033[39m"; /* reset foreground */
        return " fn (expr . rest) rest)";
    }
    if (!strcmp(buf,"define")) {
        *ansi1 = "\033[35m"; /* magenta foreground */
        *ansi2 = "\033[39m"; /* reset foreground */
        return " var expr)";
    }
    if (!strcmp(buf,"while")) {
        *ansi1 = "\033[35m"; /* magenta foreground */
        *ansi2 = "\033[39m"; /* reset foreground */
        return " cond expr ...)";
    }
    return NULL;
}

void minilisp(char *text, size_t length, bool with_repl, Obj **env, Obj **expr) {

    if(text != NULL){
        // Save old stdin
        FILE * old_stdin = stdin;
        // Open a memory stream
        FILE * stream = fmemopen(text, length, "r");
        // Redirect stdin to the in memory stream in order to use getchar()
        stdin = stream;
        eval_input(text, env, expr);
        free(text);
        // restore stdin
        stdin = old_stdin;
        fclose(stream);
    }

    int max_iter = (with_repl) ? __INT_MAX__ : 0; 
    for(int promptnum = 1; promptnum < max_iter; promptnum++) {
        char prompt[15] = "";
        sprintf(prompt, "%d:", promptnum);
        char *line = bestline(prompt);
        if(line == NULL) continue;

        // Save old stdin
        FILE * old_stdin = stdin;
        // Open a memory stream
        FILE * stream = fmemopen(line, strlen(line), "r");
        // Redirect stdin to the in memory stream in order to use getchar()
        stdin = stream;

        usleep(10000);
        
        if (line[0] != '\0' && line[0] != '/') {
            int ok = eval_input(line, env, expr);
            if (ok == 0) {
                bestlineHistoryAdd(line);
                bestlineHistorySave("history.txt");
            }
        } else if (line[0] == '/') {
            fputs("Unreconized command: ", stdout);
            fputs(line, stdout);
           putchar('\n');
        }

        free(line);
        // restore stdin
        stdin = old_stdin;
        fclose(stream);
    }
}

__attribute((noreturn)) void error(char *fmt, ...);

// Duplicate a string
static char *dup_string(const char *str) {
    const size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (copy){
        strcpy(copy, str);
        copy[len] = '\0';
    }
    return copy;
}

static size_t read_file(char *fname, char **text) {
    size_t length = 0;
    FILE *f = fopen(fname, "r");
    if (!f) {
        error("Failed to load file %s", fname);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    *text = malloc(length + 1);
    if (!*text) {
        error("Out of memory.");
        fclose(f);
        return 0;
    }

    size_t read = fread(*text, 1, length, f);
    if (read != length) {
        error("Failed to read entire file");
        free(*text);
        *text = NULL;
        fclose(f);
        return 0;
    }

    (*text)[length] = '\0';
    fclose(f);
    return length;
}

void process_file(char *fname, Obj **env, Obj **expr) {
    char *text;
    size_t len = read_file(fname, &text);
    if (len == 0) return;

    // Save old stdin
    FILE *old_stdin = stdin;
    // Create a single stream for the entire file
    FILE *stream = fmemopen(text, len, "r");
    if (!stream) {
        free(text);
        error("Failed to create memory stream");
        return;
    }

    // Redirect stdin to the memory stream
    stdin = stream;

    // Process expressions until we reach end of file
    while (!feof(stream)) {
        eval_input(text, env, expr);
    }

    // Cleanup
    stdin = old_stdin;
    fclose(stream);
    free(text);
}

static bool no_history = false;
static int num_args = 0;
static char *one_liner = NULL;
static char **filenames;
static bool with_repl = true;

void parse_args(int argc, char **argv) {

    static ko_longopt_t long_options[] = {
        {"exec",        ko_required_argument,   300 }, // exec Lisp code
        {"no-history",  ko_no_argument,         301 }, // disable history
        {"no-repl",     ko_no_argument,         302 }, // disable the REPL
        {"help",        ko_no_argument,         303 }, // show help
        {NULL,          0             ,         0   }
    };

    ketopt_t option = KETOPT_INIT;
    
    /*
    ketopt() returns -1 when it's done processing options
    status.ind tells us where the non-option arguments begin in argv[]
    We need to explicitly process those remaining arguments after the ketopt loop.

    The argument permute flag:  When permute is true (non-zero), 
    ketopt skips over non-option arguments initially, collecting them at the end.
    
    The option string ostr follows the getopt convention:
        No colon: option takes no argument (like 'h')
        Single colon: option requires argument (like 'x:')
        Double colon: option has optional argument (like 'd::')
        Leading '-': special behavior for error reporting 
        (allows distinguishing between missing required argument and unknown option)
    */
    while (true) {
        int c = ketopt(&option, argc, argv, 0, "-:x:Hrh", long_options);
        if (c == -1)
            break;
        int idx = option.longidx;
        switch (c) {
            case 0: // long --options
                printf("long option %s", long_options[idx].name);
                if (long_options[idx].has_arg)
                    printf(" with argument '%s'", option.arg);
                printf("\n");
                break;

            case 'x':
            case 300:  // --exec "... lisp code ..."
                one_liner = dup_string(option.arg);
                break;

            case 'H':
            case 301:  // --no-history
                no_history = true;
                break;

            case 'r':
            case 302: // --no-repl: don't launch the REPL
                with_repl = false;
                break;

            case 'h':
            case 303: // --help
                printf("HELP\n");
                break;

            case '?': // unknown option
                printf("Unknown option '%c'\n", option.opt);
                break;

            case ':': // missing required arg
                if (option.longidx >= 0) {
                    printf("Missing argument for option %s\n", long_options[idx].name);
                } else {
                    printf("Missing argument for option '%c'\n", option.opt);
                }
                break;

            default:
                printf("?? ketopt returned character code 0%o ??\n", c);
        }
    }

    // regular arguments: run files
    filenames = (char **) malloc(argc * sizeof(char *));
    for (int i = option.ind; i < argc; i++) {
        filenames[i] = dup_string(argv[i]);
        printf("Executing %s\n", filenames[i]);
    }
}

int main(int argc, char **argv) {

    parse_args(argc, argv);

    DEFINE2(env, expr);
    init_minilisp(env);

    for (int i = 0; i < num_args; i++) {
        printf("%s", filenames[i]);
       process_file(filenames[i], env, expr);
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    bestlineSetCompletionCallback(completion);
    bestlineSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    if (!no_history){
        bestlineHistoryLoad("history.txt"); /* Load the history at startup */
    }
    else {
        fputs("Command history disengaged.", stdout);
    }


    //printf("%s\n", one_liner);
    size_t len = one_liner ? strlen(one_liner) : 0;
    minilisp(one_liner, len, with_repl, env, expr);

    return 0;
}
