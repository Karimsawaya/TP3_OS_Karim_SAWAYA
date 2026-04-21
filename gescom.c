#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include "gescom.h"

char **Mots = NULL;
int   NMots = 0;

/* Table des commandes internes enregistrees dynamiquement. */
static Commande tabCom[NBMAXC];
static int nbCom = 0;

static int cmd_exit(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    exit(0);
}

static int cmd_cd(int argc, char *argv[])
{
    const char *dest = (argc >= 2) ? argv[1] : getenv("HOME");
    if (dest == NULL || chdir(dest) != 0) {
        perror("cd");
    }
    return 0;
}

static int cmd_pwd(int argc, char *argv[])
{
    char cwd[4096];
    (void)argc;
    (void)argv;
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return 1;
    }
    puts(cwd);
    return 0;
}

void cleanupMots(void)
{
    /* Libere le resultat du dernier parsing de commande. */
    int i;
    if (Mots == NULL) {
        NMots = 0;
        return;
    }
    for (i = 0; i < NMots; i++) {
        free(Mots[i]);
    }
    free(Mots);
    Mots = NULL;
    NMots = 0;
}

void ajouteCom(const char *nom, FuncCmd func)
{
    if (nbCom >= NBMAXC) {
        fprintf(stderr, "gescom: NBMAXC=%d trop petit\n", NBMAXC);
        exit(1);
    }
    tabCom[nbCom].nom = nom;
    tabCom[nbCom].func = func;
    nbCom++;
}

void majComInt(void)
{
    /* Commandes internes minimales du shell. */
    ajouteCom("exit", cmd_exit);
    ajouteCom("cd", cmd_cd);
    ajouteCom("pwd", cmd_pwd);
}

void listeComInt(void)
{
    int i;
    for (i = 0; i < nbCom; i++) {
        printf("%s\n", tabCom[i].nom);
    }
}

int analyseCom(char *buffer)
{
    /* Tokenisation simple par espaces/tabulations/sauts de ligne. */
    char *tmp;
    char *ptr;
    char *tok;
    int count = 0;
    int i = 0;

    cleanupMots();

    if (buffer == NULL) {
        return 0;
    }

    tmp = strdup(buffer);
    if (tmp == NULL) {
        perror("strdup");
        exit(1);
    }

    ptr = tmp;
    while ((tok = strsep(&ptr, " \t\n")) != NULL) {
        if (*tok != '\0') {
            count++;
        }
    }
    free(tmp);

    if (count == 0) {
        return 0;
    }

    Mots = calloc((size_t)count + 1, sizeof(char *));
    if (Mots == NULL) {
        perror("calloc");
        exit(1);
    }

    ptr = buffer;
    while ((tok = strsep(&ptr, " \t\n")) != NULL) {
        if (*tok == '\0') {
            continue;
        }
        Mots[i] = strdup(tok);
        if (Mots[i] == NULL) {
            perror("strdup");
            exit(1);
        }
        i++;
    }

    Mots[i] = NULL;
    NMots = i;
    return NMots;
}

int execComInt(int argc, char **argv)
{
    /* Retourne 1 si la commande est interne et executee. */
    int i;
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 0;
    }
    for (i = 0; i < nbCom; i++) {
        if (strcmp(argv[0], tabCom[i].nom) == 0) {
            tabCom[i].func(argc, argv);
            return 1;
        }
    }
    return 0;
}

static void apply_redirects(int *argc, char **argv)
{
    /* Interprete <, >, >>, 2>, 2>> puis compacte argv. */
    int i = 0;
    int j = 0;

    while (i < *argc) {
        int fd = -1;
        int target = -1;
        int consumed = 0;

        if (i + 1 < *argc) {
            char *op = argv[i];
            char *name = argv[i + 1];

            if (strcmp(op, "<") == 0) {
                fd = open(name, O_RDONLY);
                target = STDIN_FILENO;
                consumed = 2;
            } else if (strcmp(op, ">") == 0) {
                fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                target = STDOUT_FILENO;
                consumed = 2;
            } else if (strcmp(op, ">>") == 0) {
                fd = open(name, O_WRONLY | O_CREAT | O_APPEND, 0644);
                target = STDOUT_FILENO;
                consumed = 2;
            } else if (strcmp(op, "2>") == 0) {
                fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                target = STDERR_FILENO;
                consumed = 2;
            } else if (strcmp(op, "2>>") == 0) {
                fd = open(name, O_WRONLY | O_CREAT | O_APPEND, 0644);
                target = STDERR_FILENO;
                consumed = 2;
            }
        }

        if (consumed != 0) {
            if (fd < 0) {
                perror(argv[i + 1]);
                exit(1);
            }
            if (dup2(fd, target) < 0) {
                perror("dup2");
                close(fd);
                exit(1);
            }
            close(fd);
            i += consumed;
        } else {
            argv[j++] = argv[i++];
        }
    }

    argv[j] = NULL;
    *argc = j;
}

int execComExt(char **argv)
{
    /* Lance une commande externe dans un processus fils. */
    pid_t pid;
    int status = 0;
    int argc = 0;

    while (argv[argc] != NULL) {
        argc++;
    }

    fflush(stdout);
    fflush(stderr);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        apply_redirects(&argc, argv);
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

#ifdef TRACE
    if (WIFEXITED(status)) {
        fprintf(stderr, "[TRACE] status=%d\n", WEXITSTATUS(status));
    }
#endif

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int execPipeline(char **cmds, int ncmds)
{
    /* Monte un pipeline de n commandes separees par '|'. */
    int pipes[MAX_PIPE][2];
    pid_t pids[MAX_PIPE + 1];
    int i;

    if (ncmds <= 0) {
        return 0;
    }
    if (ncmds > MAX_PIPE) {
        fprintf(stderr, "gescom: trop de pipes\n");
        return -1;
    }

    for (i = 0; i < ncmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return -1;
        }
    }

    for (i = 0; i < ncmds; i++) {
        char *segment = cmds[i];
        char *copy;
        int argc;

        while (*segment == ' ' || *segment == '\t') {
            segment++;
        }

        copy = strdup(segment);
        if (copy == NULL) {
            perror("strdup");
            return -1;
        }

        argc = analyseCom(copy);
        free(copy);

        if (argc == 0) {
            pids[i] = -1;
            continue;
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return -1;
        }

        if (pids[i] == 0) {
            int j;
            signal(SIGINT, SIG_DFL);

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < ncmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (j = 0; j < ncmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            argc = NMots;
            apply_redirects(&argc, Mots);

            if (!execComInt(argc, Mots)) {
                execvp(Mots[0], Mots);
                perror(Mots[0]);
                _exit(127);
            }
            _exit(0);
        }
    }

    for (i = 0; i < ncmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (i = 0; i < ncmds; i++) {
        int status;
        if (pids[i] > 0) {
            waitpid(pids[i], &status, 0);
        }
    }

    return 0;
}
