# Client SSH de poche — T-Deck Plus

Transforme le T-Deck en petit terminal SSH Wi-Fi pour administrer un Raspberry Pi,
un serveur ou Home Assistant.

- Menu des serveurs lu sur la **carte microSD**
- Authentification par **clé** (ed25519, ECDSA, RSA, avec ou sans phrase de passe)
  ou par **mot de passe** (demandé à chaque fois, jamais enregistré)
- Vérification de l'**empreinte du serveur** au premier contact, puis à chaque connexion
- Terminal **xterm** de 53 × 29 caractères : couleurs, `nano`, `vim`, `htop`, `less`…
- Plusieurs réseaux Wi-Fi (maison, partage de connexion du téléphone)

## Ce qu'il faut

- Un T-Deck / T-Deck Plus et une carte **microSD** formatée en **FAT32**.
- Un réseau Wi-Fi **2,4 GHz** (l'ESP32-S3 ne gère pas le 5 GHz).
- Sur le Raspberry Pi : SSH activé (`sudo raspi-config` → *Interface Options* → *SSH*,
  ou l'option SSH de Raspberry Pi Imager). Son adresse IP : `hostname -I`.

## Installation

### 1. Préparer la carte SD
Copie le dossier [`sd-exemple/ssh`](sd-exemple/ssh) à la racine de la carte, puis
modifie `/ssh/config.ini` : tes réseaux Wi-Fi et tes serveurs.

### 2. (Recommandé) Créer une clé dédiée au T-Deck
Sur ton PC :

```bash
ssh-keygen -t ed25519 -f tdeck_key -C "t-deck"
```

- Copie **`tdeck_key`** (la clé privée) sur la carte SD sous `/ssh/id_ed25519`.
- Ajoute **`tdeck_key.pub`** (la clé publique) sur le Pi :

```bash
ssh-copy-id -i tdeck_key.pub pi@192.168.1.20
```

Une clé propre au T-Deck se révoque facilement (en retirant sa ligne de
`~/.ssh/authorized_keys`) si tu perds l'appareil ou la carte. Mets-lui une phrase
de passe si la carte SD risque de traîner.

### 3. Flasher
```bash
cd ssh-client
pio run -t upload
```

Si le port n'apparaît pas : éteins le T-Deck, maintiens le clic du trackball et
rallume-le.

## Utilisation

### Menu
| Action | Effet |
|---|---|
| Trackball haut / bas | Choisir un serveur |
| Clic / Entrée | Se connecter |
| **R** | Relire la carte SD (après modification de `config.ini`) |

À la première connexion, le T-Deck affiche l'empreinte du serveur. Compare-la
avec celle affichée sur le serveur par
`ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`, puis tape **O**. Elle est
enregistrée dans `/ssh/known_hosts.txt`. Si elle change un jour, la connexion
est refusée (protection contre l'interception).

### Dans le terminal
Le clavier du T-Deck n'a ni Ctrl, ni Échap, ni flèches :

| Action | Envoie |
|---|---|
| Trackball | Flèches ↑ ↓ ← → |
| **Clic court**, puis une touche | **Ctrl** + touche (ex. clic puis `c` = Ctrl+C). « CTRL » s'affiche en haut à droite |
| **Clic long** | **Échap** (pour quitter le mode insertion de `vim`) |
| Clic, puis `i` | Tab (complétion dans le shell) |
| Entrée, puis `~` puis `.` | Se déconnecter (comme OpenSSH) |
| `exit` | Fermer la session normalement |

Raccourcis utiles : Ctrl+C (interrompre), Ctrl+D (quitter), Ctrl+L (effacer
l'écran), Ctrl+R (rechercher dans l'historique), Ctrl+X puis Ctrl+O / Ctrl+X dans `nano`.

## Limites connues
- Pas d'historique de défilement : utilise `less` ou `| tail`.
- Les caractères non latins (emoji, CJK) s'affichent en `?`.
- La connexion prend quelques secondes (échange de clés sur l'ESP32-S3).
- Pas de redirection de ports ni de transfert de fichiers.

## Sécurité
- Ne mets jamais ta clé privée ni ton mot de passe Wi-Fi dans le dépôt git : tout
  reste sur la carte SD.
- Le Wi-Fi est en clair dans `config.ini` : garde la carte SD pour toi.

## Dépannage
| Symptôme | Piste |
|---|---|
| « Carte microSD absente » | Formate en FAT32, vérifie qu'elle est bien enfoncée. |
| « Wi-Fi : recherche... » en boucle | Réseau 2,4 GHz ? SSID et mot de passe exacts (majuscules) ? |
| « Connexion impossible » | IP du Pi, SSH activé, même réseau ? Teste `ssh pi@IP` depuis un PC. |
| « Cle refusee par le serveur » | La clé publique n'est pas dans `~/.ssh/authorized_keys` du Pi. |
| « Cle illisible » | Format OpenSSH ou PEM attendu ; phrase de passe correcte ? |
| Couleurs inversées | Décommente `-DTFT_INVERSION_ON=1` dans `platformio.ini`. |
| Redémarrage pendant la connexion | Regarde le moniteur série (`pio device monitor`) et envoie-moi la trace. |
