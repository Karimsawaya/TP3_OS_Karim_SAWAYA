# TP3 OS User 
Mini shell Unix avec protocole BEUIP de messagerie et partage de fichiers.

## Structure du code

- `biceps.c` : shell principal, prompt, historique readline, commandes internes
- `gescom.c` / `gescom.h` : parsing, exécution commandes externes, redirections, pipes
- `creme.c` / `creme.h` : protocole BEUIP, thread UDP, thread TCP, liste chaînée triée

## Compilation

```bash
make                # produit ./biceps
make memory-leak    # produit ./biceps-memory-leaks (avec -g -O0 pour valgrind)
make clean          # supprime tous les binaires et .o
```

## Commandes disponibles

```
beuip start <pseudo>          # lance les serveurs UDP et TCP
beuip stop                    # arrête les serveurs et notifie les autres
beuip list                    # affiche la liste des utilisateurs présents
beuip message <pseudo> <msg>  # envoie un message à un utilisateur (octet '4')
beuip message all <msg>       # envoie un message à tous (octet '5')
beuip ls <pseudo>             # liste les fichiers partagés par un utilisateur
beuip get <pseudo> <fichier>  # télécharge un fichier depuis un utilisateur
vers                          # affiche les versions
cd / pwd / exit               # commandes shell classiques
```

## Architecture réseau

- Port UDP/TCP : **9998**
- Adresse broadcast TP : **192.168.88.255** (définie dans `BEUIP_BROADCAST`)
- Protocole BEUIP : premier octet = code ('0'=départ, '1'=annonce, '2'=réponse, '4'=message user, '5'=message all)
- Table des contacts : liste chaînée triée alphabétiquement, protégée par mutex

## Vérification fuites mémoire

```bash
make memory-leak
valgrind --leak-check=full --track-origins=yes --errors-for-leak-kinds=all --error-exitcode=1 ./biceps-memory-leaks
```