#ifndef __COSMOPOLITAN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include <unistd.h>
#include <sys/select.h>
#include "bestline/bestline.h"
#include "gc.h"
#include "minilisp.h"

#if 0
// should be ~50kb statically linked
// will save history to ~/.foo_history
// cc -fno-jump-tables -Os -o foo foo.c bestline.c
int main() {
    char *line;
    while ((line = bestlineWithHistory("IN> ", "foo"))) {
        fputs("OUT> ", stdout);
        fputs(line, stdout);
        fputs("\n", stdout);
        free(line);
    }
}
#endif


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

void minilisp() {

    DEFINE1(env);
    init_minilisp(env);

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

    /* Now this is the main loop of the typical bestline-based application.
     * The call to bestline() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * bestline, so the user needs to free() it. */
    int promptnum = 1;
    for(;;promptnum++) {
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

        usleep(50000);
        
        if (line[0] != '\0' && line[0] != '/') {
            DEFINE1(expr);
            int ok = eval_input(line, env, expr);
            if (ok == 0) {
                bestlineHistoryAdd(line); /* Add to the history. */
                bestlineHistorySave("history.txt"); /* Save the history on disk. */
            }
        } else if (!strncmp(line, "/balance", 8)) {
            bestlineBalanceMode(1);
        } else if (!strncmp(line, "/unbalance", 10)) {
            bestlineBalanceMode(0);
        } else if (line[0] == '/') {
            fputs("Unreconized command: ", stdout);
            fputs(line, stdout);
            fputs("\n", stdout);
        }

        free(line);
        // restore stdin
        stdin = old_stdin;
        fclose(stream);
    }
}

__attribute((noreturn)) void error(char *fmt, ...);

int main(int argc, char **argv) {

   bool no_hist = false;

    DEFINE2(env, expr);
    init_minilisp(env);

    if (argc > 1) {
        for (int i = 1; i <= argc; ++i){
            char *arg = argv[i];
            if (strcmp(arg, "--no-history") == 0){
                no_hist = true;
            }
            else if ((strcmp(arg, "--exec") == 0) || (strcmp(arg, "--exec")) == 0){ 

            }
            else
                process_file(arg, env, expr);
        }
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    bestlineSetCompletionCallback(completion);
    bestlineSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    if (!no_hist) {
        bestlineHistoryLoad("history.txt"); /* Load the history at startup */
    }

    minilisp();
        
    return 0;
}
