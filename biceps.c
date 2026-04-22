#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "gescom.h"
#include "creme.h"

#define BICEPS_VERSION "3.0"

static char *g_histfile = NULL;

/* ------------------------------------------------------------------ */
/*  Nettoyage a la sortie                                              */
/* ------------------------------------------------------------------ */

static void app_cleanup(void)
{
    if (beuip_is_running())
        beuip_stop();
    cleanupMots();
    if (g_histfile != NULL) {
        write_history(g_histfile);
        free(g_histfile);
        g_histfile = NULL;
    }
    clear_history();
}

/* ------------------------------------------------------------------ */
/*  Utilitaire : join des mots argv[first..argc-1]                    */
/* ------------------------------------------------------------------ */

static char *join_words(int argc, char **argv, int first)
{
    size_t total = 1;
    char  *out;
    int    i;

    if (first >= argc)
        return strdup("");

    for (i = first; i < argc; i++)
        total += strlen(argv[i]) + 1;

    out = malloc(total);
    if (out == NULL) { perror("malloc"); return NULL; }
    out[0] = '\0';
    for (i = first; i < argc; i++) {
        strcat(out, argv[i]);
        if (i + 1 < argc) strcat(out, " ");
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Commande interne : vers                                            */
/* ------------------------------------------------------------------ */

static int cmd_vers(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("biceps %s / gescom %s / creme %s\n",
           BICEPS_VERSION, GESCOM_VERSION, CREME_VERSION);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Commande interne : beuip                                           */
/*  beuip start <pseudo>                                               */
/*  beuip stop                                                         */
/*  beuip list                                                         */
/*  beuip message <pseudo|all> <message...>                           */
/*  beuip ls <pseudo>                                                  */
/*  beuip get <pseudo> <fichier>                                       */
/* ------------------------------------------------------------------ */

static int cmd_beuip(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "usage: beuip start <pseudo> | beuip stop | beuip list\n"
            "       beuip message <pseudo|all> <message>\n"
            "       beuip ls <pseudo> | beuip get <pseudo> <fichier>\n");
        return 1;
    }

    if (strcmp(argv[1], "start") == 0) {
        if (argc != 3) { fprintf(stderr, "usage: beuip start <pseudo>\n"); return 1; }
        return beuip_start(argv[2]);
    }

    if (strcmp(argv[1], "stop") == 0)
        return beuip_stop();

    if (strcmp(argv[1], "list") == 0) {
        if (!beuip_is_running()) { fprintf(stderr, "beuip: non demarre\n"); return 1; }
        beuip_liste();
        return 0;
    }

    if (strcmp(argv[1], "message") == 0) {
        char *msg;
        if (argc < 4) { fprintf(stderr, "usage: beuip message <pseudo|all> <message>\n"); return 1; }
        if (!beuip_is_running()) { fprintf(stderr, "beuip: non demarre\n"); return 1; }
        msg = join_words(argc, argv, 3);
        if (msg == NULL) return 1;
        if (strcmp(argv[2], "all") == 0)
            beuip_message_all(msg);
        else
            beuip_message_user(argv[2], msg);
        free(msg);
        return 0;
    }

    if (strcmp(argv[1], "ls") == 0) {
        if (argc != 3) { fprintf(stderr, "usage: beuip ls <pseudo>\n"); return 1; }
        return beuip_ls(argv[2]);
    }

    if (strcmp(argv[1], "get") == 0) {
        if (argc != 4) { fprintf(stderr, "usage: beuip get <pseudo> <fichier>\n"); return 1; }
        return beuip_get(argv[2], argv[3]);
    }

    fprintf(stderr, "beuip: sous-commande inconnue '%s'\n", argv[1]);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Prompt dynamique : user@host$                                      */
/* ------------------------------------------------------------------ */

static char *make_prompt(void)
{
    char        host[256];
    const char *user   = getenv("USER");
    char        suffix = (getuid() == 0) ? '#' : '$';
    size_t      len;
    char       *prompt;

    if (user == NULL) user = "unknown";
    if (gethostname(host, sizeof(host)) != 0) strcpy(host, "localhost");

    len    = strlen(user) + strlen(host) + 5;
    prompt = malloc(len);
    if (prompt == NULL) { perror("malloc"); exit(1); }
    snprintf(prompt, len, "%s@%s%c ", user, host, suffix);
    return prompt;
}

/* ------------------------------------------------------------------ */
/*  Execution d'une ligne (pipes + ;)                                  */
/* ------------------------------------------------------------------ */

static void exec_line(char *line)
{
    char *seq_buf = strdup(line);
    char *seq_ptr = seq_buf;
    char *one;

    if (seq_buf == NULL) { perror("strdup"); return; }

    while ((one = strsep(&seq_ptr, ";")) != NULL) {
        char *pipe_buf;
        char *pipe_ptr;
        char *seg;
        char *cmds[MAX_PIPE + 1];
        int   ncmds = 0;

        while (*one == ' ' || *one == '\t') one++;
        if (*one == '\0') continue;

        pipe_buf = strdup(one);
        if (pipe_buf == NULL) { perror("strdup"); break; }
        pipe_ptr = pipe_buf;

        while ((seg = strsep(&pipe_ptr, "|")) != NULL && ncmds <= MAX_PIPE)
            cmds[ncmds++] = seg;

        if (ncmds == 1) {
            char *copy = strdup(cmds[0]);
            int   n;
            if (copy == NULL) { perror("strdup"); free(pipe_buf); break; }
            n = analyseCom(copy);
            free(copy);
            if (n > 0 && !execComInt(NMots, Mots))
                execComExt(Mots);
        } else if (ncmds > 1) {
            execPipeline(cmds, ncmds);
        }
        free(pipe_buf);
    }
    free(seq_buf);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    char *home;
    char *line;

    signal(SIGINT, SIG_IGN);

    majComInt();
    ajouteCom("vers",  cmd_vers);
    ajouteCom("beuip", cmd_beuip);

#ifdef TRACE
    fprintf(stderr, "[TRACE] commandes internes:\n");
    listeComInt();
#endif

    home = getenv("HOME");
    if (home == NULL) home = "/tmp";

    g_histfile = malloc(strlen(home) + 32);
    if (g_histfile == NULL) { perror("malloc"); return 1; }
    sprintf(g_histfile, "%s/.biceps_history", home);
    read_history(g_histfile);

    atexit(app_cleanup);

    while (1) {
        char *prompt = make_prompt();
        line = readline(prompt);
        free(prompt);

        if (line == NULL) { printf("\nAu revoir !\n"); break; }
        if (*line != '\0') { add_history(line); exec_line(line); }
        free(line);
    }
    return 0;
}
