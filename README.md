
# TP3 OS User 

Ce projet est une version personnelle de **BICEPS v3** construite pour répondre au sujet du TP3.
Il prolonge l'esprit du TP2 :
- mini-shell avec commandes internes et externes,
- protocole BEUIP,
- commandes de messagerie,
- ajout du multi-threading,
- adaptation automatique aux interfaces réseau,
- partage de fichiers par TCP.

## Fichiers

- `biceps.c` : shell principal, historique, prompt, commandes internes
- `gescom.c` / `gescom.h` : parsing simple, exécution, redirections, pipes
- `creme.c` / `creme.h` : protocole BEUIP, thread UDP, thread TCP, liste chaînée, `ls/get`
- `Makefile` : compilation

## Commandes disponibles

- `exit`
- `cd [rep]`
- `pwd`
- `vers`
- `beuip start <pseudo>`
- `beuip stop`
- `beuip ls <pseudo>`
- `beuip get <pseudo> <fichier>`
- `mess liste`
- `mess <pseudo> <message>`
- `mess all <message>`

## Points TP3 couverts

- serveur UDP dans un **thread**
- serveur TCP dans un **second thread**
- table des utilisateurs en **liste chaînée triée**
- accès concurrents protégés par **mutex**
- détection des interfaces IPv4 via `getifaddrs()` / `getnameinfo()`
- arrêt propre via `beuip stop`
- codes UDP traités côté serveur : `0`, `1`, `2`, `9`
- transfert de fichiers avec `beuip ls` et `beuip get`

## Compilation

```bash
make
```

Version avec traces :

```bash
make trace
```

## Remarque

Le parser du shell reste volontairement simple, dans la continuité du TP2 : il découpe les mots sur les espaces et ne gère pas les guillemets complexes.
