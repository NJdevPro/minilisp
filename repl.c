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


void completion(const char *buf, int pos, bestlineCompletions *lc) {
    (void) pos;
    if (buf[pos] == '(') {
        bestlineAddCompletion(lc,")");
    }
    if (!strcmp(buf,"pr")) {
        bestlineAddCompletion(lc,"intln");
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
            eval_input(line, env, expr);

            bestlineHistoryAdd(line); /* Add to the history. */
            bestlineHistorySave("history.txt"); /* Save the history on disk. */
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

int main(int argc, char **argv) {

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    bestlineSetCompletionCallback(completion);
    bestlineSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    bestlineHistoryLoad("history.txt"); /* Load the history at startup */

    minilisp();
        
    return 0;
}
