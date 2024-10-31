#ifndef __COSMOPOLITAN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include <unistd.h>
#include <sys/select.h>
#include "../bestline/bestline.h"
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

void minilisp(char *text, size_t length) {

    DEFINE2(env, expr);
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
   for(int promptnum = 1;;promptnum++) {
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
           putchar('\n');
        }

        free(line);
        // restore stdin
        stdin = old_stdin;
        fclose(stream);
    }
}

__attribute((noreturn)) void error(char *fmt, ...);

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

int main(int argc, char **argv) {

    DEFINE2(env, expr);
    init_minilisp(env);

    if (argc > 1) {
       process_file(argv[1], env, expr);
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    bestlineSetCompletionCallback(completion);
    bestlineSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    bestlineHistoryLoad("history.txt"); /* Load the history at startup */

    minilisp(NULL, 0);

    return 0;
}
