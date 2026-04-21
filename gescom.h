#ifndef GESCOM_H
#define GESCOM_H

/* Capacites du mini-shell (commandes internes et taille max pipeline). */
#define NBMAXC         16
#define MAX_PIPE       64
#define GESCOM_VERSION "1.3"

typedef int (*FuncCmd)(int, char *[]);

typedef struct {
    const char *nom;
    FuncCmd     func;
} Commande;

/* Resultat global du dernier parsing de commande. */
extern char **Mots;
extern int    NMots;

int  analyseCom(char *buffer);
void cleanupMots(void);
void ajouteCom(const char *nom, FuncCmd func);
void majComInt(void);
void listeComInt(void);
int  execComInt(int argc, char **argv);
int  execComExt(char **argv);
int  execPipeline(char **cmds, int ncmds);

#endif
