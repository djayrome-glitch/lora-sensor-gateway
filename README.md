# Passerelle capteurs LoRa — T-Deck Plus

Des petits nœuds LoRa (station météo, boîte aux lettres, potager) envoient leurs
mesures en 868 MHz. Le **T-Deck Plus** les reçoit, les affiche, et les relaie en
Wi-Fi vers **MQTT / Home Assistant**, avec découverte automatique des capteurs.

```
 [Météo]  ─┐
 [Boîte]  ─┼── LoRa 868 MHz ──►  T-Deck Plus  ── Wi-Fi ──►  MQTT  ──►  Home Assistant
 [Potager]─┘   (paquets signés)   (écran + alertes)
```

## Contenu

```
common/        protocole radio partagé (format des paquets, signature, paramètres radio)
  protocol.h
  network_key.example.h   ← modèle de la clé secrète partagée
gateway/       firmware de la passerelle T-Deck Plus (PlatformIO)
node/          firmware des nœuds capteurs (PlatformIO, 3 environnements)
```

## Matériel

**Passerelle** : LILYGO T-Deck Plus 868 MHz.

**Chaque nœud** : une **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262, ~20 €),
une batterie LiPo 1S, et selon le type :

| Nœud | Capteur | Branchement |
|---|---|---|
| Météo | BME280 (I2C) — *pas* un BMP280, il n'a pas l'humidité | VCC → **Ve**, GND → GND, SDA → GPIO41, SCL → GPIO42 |
| Boîte aux lettres | Contact reed (ILS) + aimant sur la trappe | Contact entre GPIO6 et GND |
| Potager | Capteur d'humidité du sol **capacitif** v1.2 (+ BME280 optionnel) | VCC → **Ve**, GND → GND, AOUT → GPIO5 |

La broche **Ve** est une sortie 3,3 V coupée pendant le sommeil : les capteurs ne
consomment rien entre deux mesures. Les broches sont modifiables dans
`node/include/node_config.h` (vérifie le brochage de ta révision de carte).

Une autre carte ESP32 + SX1262 (LilyGO T3S3, RAK…) convient aussi : adapte le
bloc « Carte » de `node_config.h`.

## Mise en route

### 1. Installer PlatformIO
Avec VS Code + l'extension **PlatformIO IDE**, ou en ligne de commande :

```bash
pip install platformio
```

### 2. Créer ta clé réseau
Copie `common/network_key.example.h` en `common/network_key.h` et remplace les
16 octets par des valeurs aléatoires (la commande pour les générer est dans le
fichier). Cette clé signe tous les paquets : sans elle, personne ne peut envoyer
de fausses mesures à ta passerelle. Le fichier est ignoré par git, ne le publie pas.

### 3. Configurer la passerelle
Édite `gateway/include/config.h` :
- `WIFI_SSID` / `WIFI_PASSWORD` (laisse vide pour un usage sans Wi-Fi) ;
- `MQTT_HOST` (IP de ton Home Assistant / broker), `MQTT_USER` / `MQTT_PASSWORD` ;
- optionnel : des noms pour tes nœuds dans `NODE_ALIASES`.

### 4. Flasher la passerelle
Branche le T-Deck en USB-C puis :

```bash
cd gateway
pio run -t upload
pio device monitor
```

Si le port n'apparaît pas : éteins le T-Deck, maintiens le **clic du trackball**
et rallume-le (mode téléchargement).

### 5. Flasher les nœuds
```bash
cd node
pio run -e weather -t upload
```

Remplace `weather` par `mailbox` ou `garden` selon le nœud. Le moniteur série
affiche l'**ID** du nœud au démarrage, utile pour les alias.

### 6. Home Assistant
1. Installe l'add-on **Mosquitto broker** et l'intégration **MQTT** (si ce n'est pas déjà fait).
2. Crée un utilisateur HA dédié pour la passerelle et mets ses identifiants dans `config.h`.
3. C'est tout : chaque nœud apparaît comme un **appareil** dans
   *Paramètres → Appareils et services → MQTT*, avec ses capteurs (température,
   humidité, pression, humidité du sol, batterie, signal) et, pour la boîte aux
   lettres, une entité **Courrier** qui passe à « on » pendant 5 minutes.

Exemple d'automatisation pour être notifié :

```yaml
alias: Courrier arrivé
triggers:
  - trigger: state
    entity_id: binary_sensor.boite_9a1f_courrier   # adapte le nom de l'entité
    to: "on"
actions:
  - action: notify.notify
    data:
      message: "📬 Il y a du courrier !"
```

## Utilisation du T-Deck

| Action | Effet |
|---|---|
| Trackball haut / bas | Sélectionner un nœud (ou passer au suivant dans le détail) |
| Clic / droite / Entrée | Ouvrir le détail du nœud |
| Clic / gauche / Q / ⌫ | Retour à la liste |
| Touche **S** | Écran d'état (radio, Wi-Fi, MQTT, compteurs) |
| N'importe quelle touche | Acquitter l'alerte courrier |

- Une ligne grisée = nœud muet depuis trop longtemps (30 min météo, 3 h potager, 13 h boîte).
- L'écran s'éteint après 60 s d'inactivité, et se rallume tout seul à l'arrivée du courrier.

## Topics MQTT

| Topic | Contenu |
|---|---|
| `lorabridge/gateway/status` | `online` / `offline` (retenu, message de dernière volonté) |
| `lorabridge/<id>/state` | `{"temperature":21.4,"humidity":56,"pressure":1013,"battery_voltage":3.92,"rssi":-87,"snr":7.5,"packets":12}` (retenu) |
| `lorabridge/<id>/mail` | `ON` à chaque courrier |
| `homeassistant/sensor/lorabridge_<id>/<clé>/config` | découverte automatique HA |

## Le protocole radio

- **868,3 MHz**, LoRa SF9 / 125 kHz, 14 dBm (25 mW), *sync word* privé `0x12`
  (Meshtastic utilise `0x2B`, les deux réseaux s'ignorent).
- Paquet binaire de 17 à 57 octets : en-tête (ID du nœud, type, numéro de
  séquence), mesures en entiers à échelle fixe, et **signature HMAC-SHA256
  tronquée à 4 octets** avec ta clé `common/network_key.h`.
  La passerelle rejette tout paquet non signé avec ta clé, donc personne ne peut
  injecter un faux « courrier ».
- Les données ne sont **pas chiffrées** (une température n'est pas un secret).
- Un événement courrier est envoyé 3 fois. La passerelle ignore les doublons
  grâce au numéro de séquence.

Pour ajouter un nouveau type de mesure : ajoute-le dans `ReadingType` et dans le
tableau `readingMeta()` de `protocol.h`. L'écran et Home Assistant le prennent
en charge automatiquement.

### Réglementation (UE)
La sous-bande 868,0–868,6 MHz impose 25 mW maximum et 1 % de temps d'émission.
Un paquet dure ~200 ms : une mesure toutes les 5 minutes représente 0,07 %. Ne
descends pas sous ~20 secondes entre deux envois d'un même nœud.

## Autonomie des nœuds (ordre de grandeur)
Heltec V3 en sommeil profond : ~20–50 µA selon la révision, plus le capteur s'il
reste alimenté. Avec une LiPo de 1000 mAh, compte **plusieurs mois** pour la
météo (5 min) et **bien plus** pour la boîte aux lettres.

## Dépannage

| Symptôme | Piste |
|---|---|
| Écran noir | `BOARD_POWERON` (GPIO10) doit être HIGH (c'est fait dans `setup()`) ; vérifie le rétroéclairage. |
| Couleurs inversées (fond blanc) | Décommente `-DTFT_INVERSION_ON=1` dans `gateway/platformio.ini`. |
| « ERREUR RADIO » / code -2 | Antenne et alimentation ; regarde le code d'erreur RadioLib dans le moniteur série. |
| « signature invalide » dans le moniteur | La passerelle et le nœud n'ont pas la même `network_key.h` : reflashe les deux. |
| Aucun paquet reçu | Mêmes paramètres radio des deux côtés (`protocol.h` commun) ; antennes vissées ; teste d'abord à 2 m de distance. |
| Batterie du nœud à 0 V | Inverse `VBAT_CTRL_ACTIVE` dans `node_config.h` (dépend de la révision Heltec V3.x). |
| Humidité du sol incohérente | Mesure la tension sonde à l'air puis dans l'eau, et mets ces valeurs dans `SOIL_DRY_MV` / `SOIL_WET_MV`. |
| La boîte envoie en boucle | Vérifie `MAIL_OPEN_LEVEL` selon ton contact (normalement ouvert ou fermé). |
| Rien dans Home Assistant | Écran **S** : MQTT doit être vert ; vérifie l'utilisateur / mot de passe ; écoute `lorabridge/#` avec MQTT Explorer. |

## Idées d'évolution
- Accusé de réception de la passerelle vers le nœud (pour la boîte aux lettres).
- Historique et graphiques sur la carte microSD du T-Deck.
- Bip sur le haut-parleur du T-Deck à l'arrivée du courrier.
- Commandes descendantes (ex. déclencher l'arrosage du potager depuis HA).
