#ifndef CREME_H
#define CREME_H

/* Constantes de protocole et de dimensionnement BEUIP. */
#define CREME_VERSION "3.0"
#define BEUIP_PORT 9998
#define BEUIP_MAGIC "BEUIP"
#define BEUIP_MAX_MSG 1024
#define BEUIP_MAX_PSEUDO 23
#define BEUIP_SHARE_DIR "reppub"

/* API publique du module reseau/messagerie BEUIP. */
int  beuip_start(const char *pseudo);
int  beuip_stop(void);
int  beuip_is_running(void);
void beuip_liste(void);
int  beuip_message_user(const char *pseudo, const char *message);
int  beuip_message_all(const char *message);
int  beuip_ls(const char *pseudo);
int  beuip_get(const char *pseudo, const char *nomfic);

#endif
