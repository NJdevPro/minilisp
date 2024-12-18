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

// This struct keeps track of the current file/line being evaluated
filepos_t filepos = {"", 0, 1};

void minilisp(char *text, size_t length, bool with_repl, Obj **env, Obj **expr) {

    // Process file
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

    // REPL
    if (with_repl)
        for(int promptnum = 1;; promptnum++) {
            char prompt[15] = "";
            filepos = (filepos_t){ .filename = "", .file_len = 0, .line_num = 1 };
            sprintf(prompt, "%d:", promptnum);
            char *line = bestline(prompt);
            if(line == NULL) continue;
            // Save old stdin
            FILE * old_stdin = stdin;
            // Open a memory stream
            FILE * stream = fmemopen(line, strlen(line), "r");
            // Redirect stdin to the in memory stream in order to use getchar()
            stdin = stream;
            
            if (line[0] != '\0' && line[0] != '/') {
                eval_input(line, env, expr);
                bestlineHistoryAdd(line);
                bestlineHistorySave("history.txt");
            } else if (line[0] == '/') {
                if (!strncmp(line, "/memory", 7)){
                    extern size_t mem_nused;
                    printf("Memory used: %ld / Total: %d\n", mem_nused, MEMORY_SIZE);
                }
                else if (!strncmp(line, "/help", 5)){
                    puts("Type Ctrl-C to quit.");
                    puts("/memory to display the amount of memory used.");
                }
                else {
                    printf("Unreconized command: %s", line);
                }
                putchar('\n');
            }

            free(line);
            // restore stdin
            stdin = old_stdin;
            fclose(stream);
        }
}


static bool no_history = false;
static char *one_liner = NULL;
static int num_files = 0;
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
                one_liner = strdup(option.arg);
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
                puts("minilisp [-r -x -h] FILE1 FILE2 ...\n");
                puts("Run the Lisp files FILE1, FILE2, ... in that order,");
                puts("and enter the read-eval-print loop once finished.");
                puts("-r | --no-repl    : don't enter the read-eval-print loop.");
                puts("-x | --exec       : execute lisp code passed as argument.");
                puts("-h | --help       : print this help.");
                exit(0);

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
    num_files = argc - option.ind;
    filenames = (char **) malloc(num_files * sizeof(char *));
    for (int i = 1; i <= num_files; i++) {
        filenames[i-1] = strdup(argv[i]);
    }
}

int main(int argc, char **argv) {

    parse_args(argc, argv);

    DEFINE2(gc_root, env, expr);
    init_minilisp(env);

    for (int i = 0; i < num_files; i++) {
        printf("Loading %s\n", filenames[i]);
        void process_file(char *fname, Obj **env, Obj **expr);
        process_file(filenames[i], env, expr);
        free(filenames[i]);
    }
    free(filenames);

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

    size_t len = one_liner ? strlen(one_liner) : 0;
    minilisp(one_liner, len, with_repl, env, expr);

    return 0;
}
