# RyuLDN Tesla Overlay

Interface graphique Tesla pour gérer le sysmodule ryuldn_nx directement sur la Switch.

## Fonctionnalités

### Écran Principal
- **Status en temps réel**:
  - État de la connexion serveur
  - Jeu en cours d'exécution
  - Informations de session (si connecté)
  - Node ID et IP virtuelle
  - Statistiques réseau (Ko envoyés/reçus, ping)

- **Actions rapides**:
  - Activer/Désactiver RyuLDN (toggle)
  - Reconnect au serveur
  - Force Disconnect (déconnexion d'urgence)

- **Configuration**:
  - Paramètres serveur (IP, port)
  - Gestion de la passphrase
  - Nom d'utilisateur

### Écran Status

Affichage détaillé de l'état du système:
```
RyuLDN Status
━━━━━━━━━━━━━━━━━━━━━━━━
State: Connected
Server: Connected ●
Game: Running

Session Info:
  Room: Mario Kart 8 Lobby
  Players: 4/8
  Your Node: #2
  Virtual IP: 10.114.0.2

Network:
  Sent: 45.23 KB
  Recv: 128.67 KB
  Ping: 42 ms
```

### Configuration Serveur

- **Server IP**: Adresse du serveur RyuLDN (défaut: ldn.ryujinx.org)
- **Server Port**: Port du serveur (défaut: 8000)

**Note**: Redémarrage requis pour appliquer les changements.

### Gestion Passphrase

- Affichage masqué (********)
- Format requis: `Ryujinx-XXXXXXXX` (8 chiffres hexadécimaux)
- Laisser vide pour parties publiques
- **Sécurité**: Modification bloquée pendant un jeu

## Prérequis

### Bibliothèques
```bash
# libtesla (framework overlay Tesla)
git clone https://github.com/WerWolv/libtesla.git
cd libtesla
make install

# libnx (Nintendo Switch library)
sudo dkp-pacman -S switch-dev
```

### Sysmodule
Le sysmodule `ryuldn_nx` doit être installé et actif.

## Compilation

```bash
cd overlay
make clean
make -j$(nproc)
```

**Sortie**:
- `ovlRyuLDN.ovl` - Overlay installable

## Installation

### Structure sur SD Card
```
sdmc:/switch/.overlays/ovlRyuLDN.ovl
```

### Installation manuelle
1. Compiler l'overlay (`make`)
2. Copier `ovlRyuLDN.ovl` vers `/switch/.overlays/` sur la SD
3. Redémarrer la Switch
4. Ouvrir Tesla menu (L + Dpad Down + R3 par défaut)
5. Sélectionner "RyuLDN"

### Avec Tesla Menu

Si Tesla Menu est installé:
1. Ouvrir Tesla (L + Dpad Down + R3)
2. Overlays → RyuLDN

## Utilisation

### Activation Rapide
1. Ouvrir Tesla menu
2. RyuLDN → Toggle "RyuLDN Enabled"
3. Lancer un jeu LDN

### Changer de Serveur
1. RyuLDN → Configuration → Server Settings
2. Modifier IP/Port
3. Redémarrer la Switch
4. Reconnecter

### Rejoindre une Partie Privée
1. RyuLDN → Configuration → Passphrase
2. Entrer le code (format: Ryujinx-XXXXXXXX)
3. Sauvegarder
4. Scanner les parties dans le jeu

### Diagnostics
Si problèmes:
1. RyuLDN → Actions → Force Disconnect
2. Vérifier "/config/ryuldn_nx/ryuldn.log"
3. RyuLDN → Actions → Reconnect

## Codes Erreur

| Erreur | Cause | Solution |
|--------|-------|----------|
| "Service Error" | Sysmodule non actif | Vérifier installation sysmodule |
| "Sysmodule not running?" | ryuldn_nx crashé | Redémarrer Switch, check logs |
| "Cannot change while game running" | Jeu actif | Fermer le jeu avant modification |

## Communication avec le Sysmodule

L'overlay utilise le service IPC `ryuldn:cfg` pour communiquer:

```cpp
// Exemple: Obtenir le status
RyuLdnStatus status;
Result rc = ryuldnCfgGetStatus(&status);

// Exemple: Activer RyuLDN
ryuldnCfgSetEnabled(true);

// Exemple: Reconnect
ryuldnCfgReconnect();
```

Voir [common/ryuldn_ipc.h](../common/ryuldn_ipc.h) pour les structures complètes.

## Architecture

```
overlay/
├── source/
│   └── main.cpp          # UI Tesla + IPC client
├── Makefile              # Build configuration
├── ovlRyuLDN.json        # Métadonnées Tesla
└── README.md             # Documentation
```

### Classes Principales

**RyuLdnOverlay**:
- Point d'entrée Tesla
- Initialisation service IPC
- Gestion lifecycle

**MainGui**:
- Écran principal
- Liste d'options
- Actions (toggle, reconnect, etc.)

**StatusElement**:
- Widget d'affichage status
- Mise à jour toutes les 1s
- Affichage stats réseau

**ServerConfigGui**:
- Configuration serveur
- IP + Port

**PassphraseGui**:
- Gestion passphrase
- Affichage masqué
- Validation format

## Raccourcis Clavier

- **A**: Sélectionner/Activer
- **B**: Retour
- **D-Pad**: Navigation
- **L/R**: Changement rapide overlay

## Dépannage

### L'overlay ne s'affiche pas
```bash
# Vérifier que Tesla Menu est installé
ls /switch/.overlays/ovlMenu.ovl

# Vérifier l'overlay
ls /switch/.overlays/ovlRyuLDN.ovl
```

### "Service Error" persistant
```bash
# Vérifier le sysmodule
ls /atmosphere/contents/4200000000000011/exefs.nsp

# Vérifier les logs
cat /config/ryuldn_nx/ryuldn.log
```

### Overlay freeze
1. Fermer Tesla (Home)
2. Relancer Tesla
3. Si problème persiste: redémarrer Switch

## Limitations

- Modification passphrase impossible pendant jeu (sécurité)
- Configuration serveur nécessite redémarrage
- Pas de clavier virtuel (utiliser fichier config.ini pour l'instant)

## TODO Futur

- [ ] Clavier virtuel pour passphrase
- [ ] Éditeur serveur IP avec clavier
- [ ] Historique des connexions
- [ ] Liste des joueurs en session
- [ ] Graphiques statistiques réseau
- [ ] Thèmes couleurs personnalisables

## Licence

Voir [LICENSE](../LICENSE) du projet principal.

## Support

- Issues: https://github.com/votre-repo/ryuldn_nx/issues
- Documentation: [FINAL_COMPLETE.md](../FINAL_COMPLETE.md)
- Wiki: [Architecture](../ARCHITECTURE.md)

---

**Version**: 1.0.1
**Compatible avec**: ryuldn_nx sysmodule v1.0.1+
**Testé sur**: Atmosphère 1.6.0+
