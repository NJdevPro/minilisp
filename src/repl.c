// This software is under the MIT License.

#ifndef __COSMOPOLITAN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
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

static bool no_history = false;

void minilisp(char *text, size_t length, bool with_repl, Obj **env, Obj **expr) {

    // Process the code given on the command line
    if(text != NULL){
        set_input(text, length);
        eval_input(gc_root, env, expr);
        free(text);
    }

    // REPL
    if (with_repl)
        for(int promptnum = 1;; promptnum++) {
            char prompt[15] = "";
            filepos = (filepos_t){ .filename = "", .file_len = 0, .line_num = 1 };
            sprintf(prompt, "%d:", promptnum);
            errno = 0;
            char *line = bestline(prompt);
            if (line == NULL) {
                if (errno == EAGAIN)  // Ctrl-C
                    continue;
                break;                // end of input (Ctrl-D)
            }

            if (line[0] != '\0' && line[0] != '/') {
                set_input(line, strlen(line));
                eval_input(gc_root, env, expr);
                if (!no_history) {
                    bestlineHistoryAdd(line);
                    bestlineHistorySave("history.txt");
                }
            } else if (line[0] == '/') {
                if (!strncmp(line, "/memory", 7)){
                    printf("Memory used: %zu / Total: %zu (the heap can grow up to %zu)\n",
                           mem_nused, heap_size, max_heap_size);
                }
                else if (!strncmp(line, "/help", 5)){
                    puts("Type Ctrl-C to quit.");
                    puts("/memory to display the amount of memory used.");
                }
                else {
                    printf("Unrecognized command: %s", line);
                }
                putchar('\n');
            }

            free(line);
        }
}


static char *one_liner = NULL;
static int num_files = 0;
static char **filenames;
static bool with_repl = true;

// Reads a size in KB: a positive decimal integer. Exits with a message if it is something else.
static size_t parse_size(const char *arg, const char *what) {
    char *end = (char *)arg;
    errno = 0;
    unsigned long long n = isdigit((unsigned char)arg[0]) ? strtoull(arg, &end, 10) : 0;
    if (n == 0 || *end != '\0' || errno != 0 || n > SIZE_MAX) {
        fprintf(stderr, "minilisp: invalid size for the %s: '%s' (a number of KB is expected)\n",
                what, arg);
        exit(1);
    }
    return (size_t)n;
}

void parse_args(int argc, char **argv) {
    size_t heap_kb = 0, max_heap_kb = 0;   // 0 when not given

    // What follows -- is not for minilisp: it is given to the Lisp program, see (args)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            set_script_args(argc - i - 1, argv + i + 1);
            argc = i;
            break;
        }
    }

    static ko_longopt_t long_options[] = {
        {"exec",        ko_required_argument,   300 }, // exec Lisp code
        {"no-history",  ko_no_argument,         301 }, // disable history
        {"no-repl",     ko_no_argument,         302 }, // disable the REPL
        {"help",        ko_no_argument,         303 }, // show help
        {"heap",        ko_required_argument,   304 }, // initial heap size
        {"max-heap",    ko_required_argument,   305 }, // maximum heap size
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
        // permute: the options may be given after the file names
        int c = ketopt(&option, argc, argv, 1, "-:x:m:M:Hrh", long_options);
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

            case 'm':
            case 304: // --heap: the initial size of the heap, in KB
                heap_kb = parse_size(option.arg, "heap (-m)");
                break;

            case 'M':
            case 305: // --max-heap: the maximum size of the heap, in KB
                max_heap_kb = parse_size(option.arg, "maximum heap (-M)");
                break;

            case 'h':
            case 303: // --help
                puts("minilisp [-r -x -m -M -h] FILE1 FILE2 ... [-- ARG1 ARG2 ...]\n");
                puts("Run the Lisp files FILE1, FILE2, ... in that order,");
                puts("and enter the read-eval-print loop once finished.");
                puts("-r | --no-repl    : don't enter the read-eval-print loop.");
                puts("-x | --exec       : execute lisp code passed as argument.");
                puts("-m | --heap KB    : initial size of the heap, in KB (default 4096, minimum 32).");
                puts("-M | --max-heap KB: the heap grows when it is full, up to this size in KB");
                puts("                    (default 1048576). -m and -M together with the same value");
                puts("                    give a heap of fixed size. The memory used is twice the heap.");
                puts("-h | --help       : print this help.");
        puts("ARG1 ARG2 ... after -- are given to the Lisp program: see (args).");
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

    const char *err = gc_set_heap_limits(heap_kb, max_heap_kb);
    if (err) {
        fprintf(stderr, "minilisp: %s\n", err);
        exit(1);
    }

    // regular arguments: run files
    num_files = argc - option.ind;
    filenames = (char **) malloc(num_files * sizeof(char *));
    for (int i = 0; i < num_files; i++) {
        filenames[i] = strdup(argv[option.ind + i]);
    }
}

int main(int argc, char **argv) {

    parse_args(argc, argv);

    DEFINE2(gc_root, env, expr);
    init_minilisp(env);

    for (int i = 0; i < num_files; i++) {
        printf("Loading %s\n", filenames[i]);
        process_file(gc_root, filenames[i], env, expr);
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
