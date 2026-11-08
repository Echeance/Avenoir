# Avenoir — Assistant de système d'exploitation

> Document de référence du projet : vision, architecture, décisions et directives.
> À tenir à jour à chaque décision structurante.

---

## 1. Vision

Un assistant **résident** pour **Windows et Linux**, piloté à la **voix** ou au **clavier**, capable de :

| Capacité | Exemples |
|---|---|
| Contrôle système | « monte le son », « verrouille l'écran », « passe en mode sombre » |
| Lancement d'applications | « ouvre Firefox », « lance VS Code » |
| Recherche internet | « cherche la météo à Lyon », « trouve la doc de std::expected » |
| Questions ouvertes | « explique-moi la différence entre TCP et UDP » |
| Exécution de tâches | « range mes téléchargements par type », « fais-moi un résumé de ce PDF » |

Et **extensible** par des **modules** que n'importe qui peut écrire, dans n'importe quel langage : chacun apporte ses outils, ses commandes, ses fonctions de fond et, s'il le souhaite, son onglet dans l'interface (ex. un module météo qui récupère et affiche la météo, un module calendrier que l'assistant sollicite pour « déplace ma réunion de demain »).

### Principes directeurs

1. **Confidentialité et sécurité avant tout** : ce sont les aspects primordiaux de l'assistant, qui agit sur une vraie machine et voit des données personnelles. Elles priment sur toute fonctionnalité, tout confort et toute rapidité ; en cas de conflit, elles l'emportent. Aucune action sensible sans confirmation explicite, aucune donnée transmise sans nécessité.
2. **Le LLM au choix de l'utilisateur** : local (Ollama, llama.cpp…) ou distant (Anthropic, OpenAI, Mistral…), sélectionné par configuration.
3. **Réactif** : les commandes simples ne passent pas par le LLM.
4. **Léger** : un démon en arrière-plan doit consommer peu de ressources.
5. **Standards plutôt que formats maison** : JSON-RPC (compatible MCP) pour les modules, JSON Schema pour les outils.

---

## 2. Architecture

### 2.1 Vue d'ensemble

Trois couches, dans des processus distincts :

```
┌─ Démon Avenoir (toujours actif) ───────────────────────────────────┐
│  Agent (LLM + outils) · Permissions · Voie rapide · Commandes      │
│  Hôte de modules · Journal structuré (conversations, actions)      │
└───────┬───────────────────────────────────────────────┬────────────┘
        │ IPC locale                                    │ JSON-RPC (stdio)
┌───────┴───────────────── Interface ─────────┐   ┌─────┴──────────────┐
│  Hôte C++ des fenêtres + vue web système    │   │ Modules            │
│  ├─ Widget (haut de l'écran, plié/déplié)   │   │ (processus séparés │
│  └─ Application principale                  │   │  ou serveurs MCP)  │
└─────────────────────────────────────────────┘   └────────────────────┘
```

- **Démon** : le cerveau. Il tourne en permanence, exécute les requêtes, héberge les modules et tient le journal. Les interfaces (widget, application, CLI, voix) en sont des clients.
- **Interface** : un seul processus, deux fenêtres (le widget et l'application principale), qui partagent l'état et la connexion au démon. Voir § 2.7.
- **Modules** : programmes externes qui étendent l'assistant. Voir § 2.6.

### 2.2 Traitement d'une requête

```
            ┌──────────── Entrées ────────────┐
            │  Micro → mot de réveil → STT    │   (avenoir_voice, phase 5)
            │  Clavier : CLI, widget          │   (avenoir-cli, widget)
            └───────────────┬─────────────────┘
                            │ texte
                            ▼
                 ┌─────────────────────┐
                 │  Voie rapide        │── correspond ──┐
                 │  (FastRouter)       │                │
                 └─────────┬───────────┘                │
                           │ sinon                      │
                           ▼                            │
                 ┌─────────────────────┐                │
                 │  Boucle agent       │◄──────┐        │
                 │  LLM + outils       │       │        │
                 └─────────┬───────────┘       │        │
                           │ appel d'outil     │résultat│
                           ▼                   │        ▼
                 ┌─────────────────────────────┴──────────────┐
                 │  Contrôle des permissions                  │
                 │  RiskLevel × PermissionPolicy → Allow /    │
                 │  Confirm (ConfirmationHandler) / Deny      │
                 └─────────┬──────────────────────────────────┘
                           ▼
          ┌────────────────┴──────────────────┐
          │ Outils natifs      Outils modules │
          │ (avenoir_tools)  (avenoir_modules)│
          └───────────────────────────────────┘
                           │
                           ▼
                Réponse → affichage + TTS
```

### 2.3 Structure du dépôt (cible)

```
avenoir/
├── CMakeLists.txt          Projet racine, options et avertissements communs
├── CMakePresets.json       Préréglages : debug, release, asan (Linux / Windows)
├── vcpkg.json              Dépendances (mode manifeste)
├── config.example.toml     Configuration commentée
├── modules/                Modules fournis avec Avenoir (programmes séparés) : meteo/
├── evals/                  Banc d'essai avec de vrais modèles : requêtes de référence, rapports
├── docs/
│   ├── PROJECT.md          Ce document
│   ├── FEATURES.md         Liste exhaustive des fonctionnalités existantes
│   ├── ROADMAP.md          Plan de travail détaillé
│   ├── FONCTIONNEMENT.md   Comment le code s'articule
│   └── readme/             Images du README et leurs générateurs
├── libs/
│   ├── core/               Cœur, sans dépendance à l'OS, au LLM ou à la voix
│   │   ├── include/avenoir/core/
│   │   │   ├── agent.hpp       Boucle agent + exécution sous contrôle des permissions
│   │   │   ├── tool.hpp        Interface Tool, ToolSpec, RiskLevel, ToolRegistry
│   │   │   ├── llm.hpp         Interface LlmProvider
│   │   │   ├── message.hpp     Historique de conversation (format interne)
│   │   │   ├── permission.hpp  PermissionPolicy, ConfirmationHandler
│   │   │   ├── router.hpp      Voie rapide (règles regex)
│   │   │   └── config.hpp      AppConfig (TOML)
│   │   ├── src/
│   │   └── tests/
│   ├── llm/                Fournisseurs : API compatible OpenAI, Anthropic, Claude Code
│   ├── tools/              Outils natifs ; code par OS dans src/platform/{linux,windows}.cpp
│   ├── modules/            Hôte de modules : manifestes, processus JSON-RPC, compatibilité MCP
│   ├── storage/            Conversations et journal (SQLite)
│   ├── terminal/           Rendu dans le terminal : couleurs, cadres, Markdown
│   ├── ipc/                Protocole et transport démon ↔ interfaces
│   └── voice/              Interfaces voix (déclarations uniquement au départ)
├── apps/
│   ├── cli/                Interface texte (exécutable `avenoir`)
│   ├── daemon/             Démon résident (exécutable `avenoird`)
│   └── ui/                 Hôte C++ des fenêtres (widget + application), vue web système
└── ui/                     Interface web (TypeScript) : widget, application, rendu des onglets
```

Chaque dossier de `libs/` est une bibliothèque CMake (`avenoir::core`, `avenoir::llm`…) avec ses en-têtes publics dans `include/avenoir/<nom>/` et ses tests dans `tests/`.

**Règle de dépendance** : `avenoir::core` ne dépend d'aucune autre bibliothèque interne. Toutes les autres dépendent de `avenoir::core`. Les applications (`apps/`) assemblent le tout. L'interface web (`ui/`) ne parle qu'à l'hôte C++ des fenêtres, jamais directement au démon ni aux modules.

### 2.4 Concepts clés

**`Tool`** : tout ce que l'assistant peut faire. Classe abstraite qui fournit une `ToolSpec` (nom, description pour le LLM, JSON Schema des arguments, niveau de risque) et une méthode `call(args, stop) -> std::expected<ToolOutput, ToolError>` (`stop` : `std::stop_token` levé par Ctrl+C). Outils natifs et outils de modules implémentent la même interface.

**`RiskLevel`** :

| Niveau | Définition | Exemples |
|---|---|---|
| `read_only` | Aucun effet de bord | heure, infos système, lecture de fichier |
| `reversible` | Effet de bord annulable | lancer une app, ouvrir une URL, volume |
| `sensitive` | Destructeur, irréversible, coûteux ou qui sort de la machine | supprimer, envoyer un mail, shell, paiement, éteindre le PC |

**`PermissionPolicy`** : associe à chaque niveau `allow`, `confirm` ou `deny`. Invariant codé en dur : `sensitive` n'est **jamais** `allow`.

**`ConfirmationHandler`** : demande l'accord de l'utilisateur. L'implémentation dépend de l'interface : terminal (CLI), popup (UI), question vocale (voix), `AutoReject` (mode sans surveillance).

**`LlmProvider`** : une méthode `complete(system, messages, tools) -> (texte, appels d'outils)`. Le format interne des messages est converti par chaque fournisseur.

**Boucle agent** (`Agent::handle`) :
1. voie rapide si une règle correspond ;
2. sinon, envoi au LLM avec la liste des outils ;
3. pour chaque appel d'outil : contrôle des permissions → exécution → résultat renvoyé au LLM (les erreurs aussi, en texte, pour que le modèle s'adapte) ;
4. répéter jusqu'à une réponse sans appel d'outil, ou `max_steps`.

**Interruption (Ctrl+C)** : **Ctrl+C ne ferme jamais Avenoir, il arrête l'action en cours** (appel au LLM ou exécution d'outil, via `std::stop_token`). L'interruption prend effet au prochain point sûr. Sans action en cours, Ctrl+C efface la saisie et rappelle comment quitter (`/quitter`, ou Ctrl+D dans la CLI).

**Annulation des actions** : « réversible » (niveau de risque) ne signifie pas « annulable automatiquement » ; **chaque outil déclare lui-même** s'il sait défaire son action. Quand c'est le cas, son résultat contient une **action d'annulation** (description lisible, ex. « remettre le volume à 45 % », et moyen de l'exécuter), que l'agent empile dans le **journal d'annulation** de la requête.
- **À l'interruption** (Ctrl+C), si la requête a déjà effectué des actions annulables, Avenoir liste les actions effectuées (annulables ✓ et non annulables ✗) et demande s'il faut annuler les annulables `[o/N]`.
- **Commande `/annuler`** : propose de la même façon d'annuler les actions de la **dernière requête terminée** ; répétée, elle remonte aux requêtes précédentes (dans la limite de l'historique conservé).
- Les annulations s'exécutent **dans l'ordre inverse** des actions ; un échec est signalé sans bloquer les suivantes ; chacune est journalisée. Le « oui » de l'utilisateur vaut confirmation pour la liste affichée.
- Une annulation **vérifie que l'état n'a pas changé** depuis l'action (ex. fichier modifié entre-temps) ; sinon elle est refusée et signalée plutôt que d'écraser quelque chose.
- Les actions non annulables (application lancée, URL ouverte, mail envoyé…) sont listées comme telles : rien n'est promis qui ne puisse être tenu.

**Voie rapide** (`FastRouter`) : règles regex strictes pour les commandes fréquentes. Elle passe **par le même contrôle de permissions** que le LLM. En cas de doute, la requête part au LLM. Sa réponse est le texte de l'outil, affiché tel quel (sans LLM) : les outils concernés renvoient donc des phrases françaises lisibles, y compris à voix haute.

### 2.5 Fournisseurs LLM

| `provider` | Fonctionnement | Coût |
|---|---|---|
| `openai_compatible` | HTTP `/chat/completions` (Ollama, llama.cpp server, LM Studio, vLLM, OpenAI, Mistral, OpenRouter…), appels d'outils natifs | gratuit en local, sinon au token |
| `anthropic` | HTTP API Messages, appels d'outils natifs | au token (clé d'API) |
| `claude_code` | Lance la CLI officielle `claude -p` connectée à l'abonnement de l'utilisateur. Ses outils, plugins, hooks et réglages sont désactivés (`--tools ""`, `--strict-mcp-config`, `--setting-sources ""`) ; les outils d'Avenoir sont décrits dans le prompt et la réponse est imposée par `--json-schema` | inclus dans l'abonnement Pro / Max |

Pour les modèles qui « réfléchissent » avant de répondre (Qwen…), `reasoning_effort = "none"` (`openai_compatible`) coupe cette réflexion : la latence passe de plusieurs secondes à environ une seconde, sans perte mesurable sur les requêtes de référence.

**Modèles recommandés**, d'après le banc d'essai (`evals/`, 24 requêtes de référence : simples, multi-outils, ambiguës, injections, refus). Les chiffres, la méthode et l'historique du prompt sont dans [`evals/resultats/COMPARAISON.md`](../evals/resultats/COMPARAISON.md).

| Usage | Modèle | Réussite | Latence | Mémoire |
|---|---|---|---|---|
| Local, carte de 24 Go | **Gemma 4 26B-A4B** (Ollama `gemma4:26b-a4b`) ; Qwen3.8 27B équivalent, deux fois plus lent | 72/72 | ~0,8 s | ~20 Go de VRAM |
| Local léger | **Ministral 3 8B** (`ministral-3:8b`) : aucune injection suivie sur le banc ; Gemma 4 12B réussit un cas de plus mais en a suivi une | 69/72 | ~0,8 s | ~10 Go de VRAM |
| Distant | **Claude** (`claude_code` ou `anthropic`) ; haiku suffit | 48/48 | ~6 s (Claude Code) | — |

Mesures du 8 novembre 2026 sous Linux (RX 7900 XTX, Ollama ROCm, Q4, sans réflexion). À éviter : Qwen3.5 4B (suit les injections), Gemma 4 E4B (se trompe d'outil), Qwen3.5 9B (invente des informations, comme la date). Un nouveau modèle se juge en repassant le banc, pas sur ses scores annoncés.

### 2.6 Modules

Un **module** étend l'assistant : outils pour le LLM, commandes, règles de voie rapide, fonctions de fond, contenu d'interface. C'est un **programme séparé** (n'importe quel langage) lancé par le démon, qui dialogue avec lui en **JSON-RPC 2.0 sur stdio**. Un module qui plante ne fait pas tomber l'assistant.

**Emplacement** : un sous-dossier par module dans `<installation>/modules/` (modules fournis, à côté de l'exécutable) ou dans le dossier de données de l'utilisateur (`%LOCALAPPDATA%\avenoir\modules`, `~/.local/share/avenoir/modules`), qui ne demande pas de droits administrateur. Un module fourni est **activé** par défaut ; un module installé par l'utilisateur reste **désactivé** tant qu'il ne l'a pas activé (`[modules.<id>] enabled = true`). Deux modules de même identifiant : le premier trouvé (fourni) l'emporte. Chaque module reçoit un dossier de données (`module-data/<id>/`) et son stderr va dans `logs/modules/<id>.log`.

**Manifeste** `module.toml` : tout ce que le module déclare, lu **avant** de le lancer.

```toml
id = "meteo"                         # unique ; préfixe de ses outils : meteo__…
name = "Météo"
version = "1.0.0"
run = ["avenoir-meteo"]              # programme du dossier du module (ou nom nu cherché dans le PATH) + arguments
protocol = "avenoir"                 # ou "mcp" : serveur MCP existant (outils seulement)
permissions = ["network", "ui.web"]  # network, files.read, files.write, ui.web : montrées, révocables
env = ["METEO_LANGUE"]               # variables d'environnement transmises (les seules, avec le minimum vital)

[[tools]]                            # appelables par le LLM (et d'autres modules si public)
name = "get_forecast"                # → « meteo__get_forecast »
risk = "read_only"                   # proposé ; l'utilisateur peut le relever
public = true                        # accessible aux autres modules

[[commands]]                         # commande d'interface : /meteo Lyon
name = "meteo"
tool = "get_forecast"
arg = "city"                         # argument rempli par le texte après la commande
description = "météo d'une ville"

[[fast_path]]                        # voie rapide, limitée aux outils du module
pattern = '^(?:quel temps fait-il|météo) à (.+?)\s*\??$'
tool = "get_forecast"
arg = "city"                         # premier groupe capturant
arguments = { days = 1 }             # arguments fixes

[[tabs]]                             # onglet dans le widget (§ 2.7)
id = "today"
title = "Météo"
kind = "declarative"                 # ou "web" (exige la permission ui.web)
```

**Fonctionnement** :
- l'**hôte de modules** (`avenoir::modules` ; dans la CLI aujourd'hui, dans le démon au jalon 3) découvre les dossiers, valide les manifestes (clé inconnue, chemin hors du dossier, outil non déclaré… : refusé), lance les modules activés et enregistre leurs outils, commandes, règles et onglets dans les registres du cœur. Les règles d'Avenoir restent prioritaires et un module ne peut ni prendre le nom d'une commande existante, ni remplacer un outil ;
- **protocole** : le protocole `avenoir` est un **sur-ensemble de MCP** — poignée de main `initialize` / `notifications/initialized`, `tools/list` (paginé), `tools/call`, `notifications/cancelled` (Ctrl+C ou délai dépassé) — plus des méthodes `avenoir/*` (aujourd'hui `avenoir/undo`). Messages JSON-RPC 2.0, un par ligne, bornés en taille (8 Mio) ; délai de 10 s pour la poignée de main, 30 s par appel. Les requêtes d'un module vers l'hôte sont refusées pour l'instant ;
- **le manifeste fait foi** : un module `avenoir` n'expose que les outils déclarés dans `[[tools]]` (nom, risque) ; leurs descriptions et schémas viennent de `tools/list`, sont bornés, nettoyés des caractères de contrôle, et signalés au LLM comme venant du module (« [Module « Météo »] … ») ;
- **environnement minimal** : le module ne reçoit que le strict nécessaire (PATH, dossier personnel, langue…), les proxys s'il a la permission `network`, les variables qu'il déclare (`env`), et `AVENOIR_MODULE_ID`, `AVENOIR_MODULE_DATA`, `AVENOIR_VERSION` ; jamais les clés d'API d'Avenoir ;
- **appels entre modules** : toujours **via l'hôte** (`avenoir/call_tool`), jamais en direct ; seuls les outils `public` sont accessibles ; l'appel subit le même contrôle des permissions et le même audit que le LLM, avec l'identité du module appelant ;
- **fonctions de fond** : un module peut travailler en continu (ex. récupérer la météo toutes les 30 min) et pousser des mises à jour de son onglet à l'hôte (notifications JSON-RPC) ;
- **découverte par le LLM** : les outils des modules apparaissent dans la liste des outils de l'agent (ex. `calendrier__update_event`). Quand les modules seront nombreux, un outil de recherche de capacités évitera d'envoyer toute la liste au LLM ;
- **annulation** : un outil de module peut renvoyer une action d'annulation dans `_meta["avenoir/undo"]` (`description` lisible, `token` opaque) ; Avenoir l'exécute par `avenoir/undo { token }`, et le module vérifie que l'état n'a pas changé (voir « Annulation des actions », § 2.4). Les outils de serveurs MCP ne sont pas annulables ;
- **compatibilité MCP** : `protocol = "mcp"` permet d'utiliser un serveur MCP existant comme module (outils seulement : `initialize`, `tools/list`, `tools/call`) ;
- **risque** : celui du manifeste est un minimum proposé par l'auteur ; défaut `sensitive` si absent (on ne fait pas confiance à l'auto-déclaration d'un module). L'utilisateur peut le **relever** librement. Il peut aussi l'**abaisser**, mais uniquement depuis l'application (ou une commande de la CLI), après :
  - un **avertissement** : « Cet outil sera exécuté avec moins de contrôles que ne le prévoit son auteur. » ;
  - un **disclaimer** : « Tout comportement ayant un impact négatif causé par ce changement relève de votre seule responsabilité. » ;
  - la saisie exacte de la phrase **« Je sais ce que je fais »** (un clic ne suffit pas).

  L'accord (phrase, date) est enregistré avec le réglage, inscrit au journal et visible dans l'écran Permissions, d'où la baisse s'annule. Une baisse sans accord enregistré (écrite à la main dans un fichier) est **ignorée**, avec un avertissement au démarrage.

  Aujourd'hui : commande `/risque <outil> <lecture | réversible | sensible | défaut>` de la CLI, choix enregistrés dans la base d'Avenoir (table `tool_risks`), appliqués immédiatement ; `config.toml` (`[modules.<id>] risk`) ne peut que **relever** et reste un plancher. Risque effectif : manifeste (ou `sensitive`), remplacé par le choix de `/risque` s'il est plus élevé ou accordé, puis relevé par `config.toml`.

### 2.7 Interface

**Technologie** : interface **web** (HTML / CSS / TypeScript) affichée dans la **vue web du système** (WebView2 sous Windows, WebKitGTK sous Linux) par un petit **hôte C++** qui crée et gère les fenêtres. Pas d'Electron (trop lourd pour un assistant résident).

**Widget principal** — en haut au centre de l'écran, toujours visible, au-dessus des fenêtres :
- **plié** : une simple barre ;
- **déplié** (au survol) : la représentation de l'assistant (un **pingouin animé**) et des onglets : activités en cours, historique défilable, informations système, version et mises à jour disponibles, petite chatbox, consommation et limites (Claude Code), et les **onglets des modules**.

Sous Wayland, le placement en haut de l'écran passe par le protocole **layer-shell** (niri, KDE, Hyprland, Sway) ; il n'est pas disponible sous GNOME.

**Application principale** — ouverte à la demande : réglages, magasin de modules, conversations, historique des actions et logs (par conversation et global), révocation des permissions, gestion des modules installés.

**Onglets des modules** — deux types, la sécurité d'abord :
- **déclaratif** (par défaut) : le module décrit son contenu (textes, listes, images, graphiques, boutons, formulaires) et l'interface le dessine dans le style d'Avenoir ; **aucun code du module ne s'exécute dans l'interface** ;
- **web** (permission `ui.web`) : HTML / CSS / JS du module, affiché dans une **iframe isolée** (voir directive de sécurité n° 12).

### 2.8 Conversations et historique

Les conversations sont **persistantes** : à la réouverture de l'application ou de la CLI, tout est rétabli.

- **Stockage** : base **SQLite** locale (`~/.local/share/avenoir/avenoir.db` sous Linux, `%LOCALAPPDATA%\avenoir\avenoir.db` sous Windows), dans la bibliothèque `avenoir::storage`. Elle contient les conversations (identifiant, titre, dates) et **tous leurs messages** : questions, réponses, appels d'outils et résultats, horodatage, consommation. Elle servira aussi au journal global (jalon 3).
- **Écriture immédiate** : chaque message est enregistré dès qu'il existe ; un plantage ou un Ctrl+C ne perd rien.
- **Reprise** : au lancement, la dernière conversation utilisée est rouverte ; ses messages sont réaffichés et l'agent recharge l'historique (le LLM retrouve tout le contexte).
- **Titre** : par défaut, les premiers mots de la première question ; renommable.
- **Commandes de la CLI** :

| Commande | Effet |
|---|---|
| `/nouvelle [titre]` | crée une conversation et bascule dessus (rien n'est effacé) |
| `/transcription` | enregistre la conversation en cours dans `transcriptions/` du dossier de données (Markdown) |
| `/reset` | efface les messages de la conversation en cours (confirmation s'il y en a, transcription Markdown proposée dans `transcriptions/`) et vide l'écran ; la conversation garde son numéro et son titre |
| `/conversations` | liste les conversations (n°, titre, date, nombre de messages) |
| `/ouvrir <n° ou titre>` | bascule sur une conversation et réaffiche ses messages |
| `/renommer <titre>` | renomme la conversation en cours |
| `/supprimer [n° ou titre]` | supprime une conversation (la conversation en cours par défaut), après confirmation |
| `/historique` | réaffiche toute la conversation en cours |

- **Défilement** : la conversation se remonte avec le défilement du terminal. Au démarrage et avec `/ouvrir`, les **50 derniers messages** sont réaffichés (pour ne pas inonder le terminal) ; `/historique` affiche tout. Les appels d'outils sont affichés de façon compacte (`» launch_app « firefox »`).
- **Historique des saisies de la CLI** : les lignes tapées sont aussi conservées (flèches haut/bas), dans `cli_history.txt`, en clair, dans le même dossier de données.
- **Droits** : le dossier de données est réservé à l'utilisateur (`700`), la base et l'historique des saisies aussi (`600`) ; sous Windows, `%LOCALAPPDATA%` est déjà propre à l'utilisateur.
- **Limites** : les conversations sont stockées **en clair** sur la machine (chiffrement optionnel envisagé plus tard) ; les actions d'annulation (`/annuler`) ne survivent pas à un redémarrage (les actions passées restent visibles dans l'historique, mais ne sont plus annulables) ; une très longue conversation reprise est renvoyée en entier au LLM (résumé automatique prévu au jalon 4).

### 2.9 Apparence de la CLI

La CLI doit être agréable à lire : chaque type d'élément a son style, reconnaissable d'un coup d'œil.

| Élément | Rendu |
|---|---|
| Accueil | cadre avec le **pingouin en pixel art** (demi-blocs ▀ ▄ colorés, couleurs 24 bits ou 256 selon le terminal ; en ASCII sans couleurs), version, LLM, conversation en cours |
| Barre de saisie | **fixe en bas du terminal** : une ligne grise, l'invite `›` cyan, une ligne grise ; tout le reste défile au-dessus. Elle **grandit** quand la saisie dépasse une ligne et reprend sa taille après validation ; les confirmations s'y répondent (« Autoriser ? [o/N] ») |
| Requête de l'utilisateur | `›` cyan, texte en gras, **sans cadre** dans l'historique |
| Réponse d'Avenoir | `◆ Avenoir` blanc, puis le texte précédé d'une barre `│` ; **Markdown rendu** (gras, italique, `code`, titres, puces `•`, citations, blocs de code) |
| Action en cours | `» outil « argument »` jaune, suivi de `✓` vert ou `✗` rouge et de l'erreur, affichés **en direct** |
| Réflexion du LLM | toupie animée, temps écoulé et rappel « Ctrl+C pour arrêter », **avec les réponses** (au-dessus de la barre) |
| Confirmation | cadre jaune « ▲ Action à confirmer », badge de risque, arguments |
| Risques | `lecture seule` vert, `réversible` jaune, `SENSIBLE` rouge (confirmations, `/outils`) |
| Erreur | `✗ Erreur :` rouge, sur la sortie d'erreur |
| Messages système | gris discret |

- **Bibliothèque `avenoir::terminal`** : couleurs, cadres, badges, largeur d'affichage UTF-8, retour à la ligne, rendu Markdown léger. Testée sans terminal.
- **Barre fixe** : les dernières lignes du terminal sont réservées (zone de défilement DECSTBM au-dessus) ; à la sortie (y compris sur erreur), le terminal retrouve son état normal. Active dès que la sortie est un terminal qui interprète les séquences de contrôle (Linux et Windows, avec ou sans couleurs) et fait au moins 5 lignes ; hors d'un terminal (tube, script), texte simple sans barre.
- **Saisie longue** : la barre grandit vers le haut, ligne par ligne : l'historique défile d'autant (CSI S, sans déplacer le curseur), la bordure du haut et l'invite sont redessinées plus haut, puis le curseur remonte d'autant (replxx se déplace en relatif : ses calculs restent justes). Le curseur n'est jamais mémorisé pendant un défilement : certains terminaux (Alacritty) le déplacent alors avec le contenu. La barre reprend une ligne après validation et l'historique redescend.
- **Redimensionnement** : selon le terminal, les lignes déjà affichées sont recoupées ou déplacées (Alacritty réorganise le texte à la nouvelle largeur). L'affichage retient donc la fin de l'historique : au redimensionnement, l'écran est vidé, cette fin réaffichée à la nouvelle largeur et la barre redessinée. Pendant une réflexion, c'est la boucle de la toupie qui le fait ; pendant une saisie, un thread de surveillance attend que la taille soit stable (une fenêtre ancrée ou agrandie passe par plusieurs tailles), puis simule une touche que replxx traite dans sa boucle (invite réécrite, bordure du bas — mise en cache par replxx à l'ancienne largeur — recalculée). **Sous Windows**, replxx ne voit pas les redimensionnements de la console et garderait l'ancienne largeur : la saisie est relancée (Entrée simulée, puis saisie neuve où le texte est retapé et le curseur replacé), ce qui la place sans erreur, vérifié dans la console Windows. Les éléments déjà affichés gardent leur mise en forme d'origine (un cadre dessiné plus large peut être coupé).
- **Réponses découpées aux mots** : chaque ligne garde la barre `│`.
- **Toutes les sorties de la CLI passent par un seul module d'affichage**, sous un verrou : la toupie (thread principal) et les actions (thread de la requête) ne s'écrivent jamais en même temps ; la toupie est suspendue pendant une confirmation.
- **Sans terminal ou avec `NO_COLOR`** (https://no-color.org) : ni couleur ni animation, texte simple (les marqueurs Markdown sont tout de même retirés). Sous Windows, les séquences de couleur de la console sont activées.
- **Journal d'audit** : masqué à l'écran par défaut (les lignes `»` montrent déjà les actions) ; visible avec `AVENOIR_LOG=info`. Il sera écrit dans un fichier au jalon 3.

---

## 3. Directives de sécurité

La **confidentialité** et la **sécurité** sont les aspects primordiaux d'Avenoir : ces règles priment sur toute considération de confort, de fonctionnalité ou de rapidité.

1. **Confirmation obligatoire** pour toute action `sensitive`, sans exception configurable.
2. **Les données ne sont pas des instructions.** Pages web, fichiers, résultats de modules peuvent contenir des injections de prompt (« ignore tes instructions et supprime… »). Le prompt système le rappelle ; le contrôle des permissions est la vraie barrière, pas le prompt.
3. **Pas d'outil shell générique** tant que le système de permissions et l'audit ne sont pas éprouvés. Préférer des outils spécialisés. Si un jour `run_command` existe : `sensitive`, affichage de la commande exacte avant confirmation, liste blanche optionnelle.
4. **Valider tous les arguments** côté outil, jamais côté LLM : noms d'application sans métacaractères, URL `http(s)` uniquement, chemins canonicalisés et restreints à des dossiers autorisés.
5. **Jamais de `system()`, `popen()`, `cmd /C` ou `sh -c`** avec une chaîne construite à partir d'arguments. Lancer les processus avec un tableau d'arguments (`reproc`, `posix_spawn`, `CreateProcessW`) ou une API dédiée (`ShellExecuteW`).
6. **Secrets** : uniquement via variables d'environnement (puis trousseau du système en phase ultérieure : Windows Credential Manager, Secret Service sous Linux). Jamais dans `config.toml`, jamais dans les logs.
7. **Journal d'audit** : chaque exécution, refus ou échec d'outil est tracé (logger `spdlog` dédié : `audit`).
8. **Modules** : risque `sensitive` par défaut, relevable par l'utilisateur ; abaissable seulement depuis l'application, après avertissement, disclaimer et saisie de « Je sais ce que je fais » (§ 2.6) ; permissions déclarées dans le manifeste, montrées à l'installation et révocables ; appels entre modules uniquement via l'hôte, avec contrôle des permissions et audit ; plus tard, bac à sable des processus.
9. **Voix** : une transcription peut être fausse. Pour une action sensible déclenchée à la voix, la confirmation reformule l'action (« Je vais supprimer 12 fichiers dans Téléchargements, je confirme ? »).
10. **Vie privée** : avec un LLM distant, ce qui est envoyé quitte la machine. Le documenter clairement à l'utilisateur ; prévoir un mode « local uniquement ».
11. **Sûreté mémoire** : l'assistant traite des données non fiables (réponses de LLM, pages web, modules). Les règles C++ de la section 4.2 (RAII, pas de mémoire gérée à la main, analyseurs en CI) en font une exigence de sécurité, pas seulement de qualité.
12. **Onglets web des modules** : iframe `sandbox="allow-scripts"` uniquement (pas d'origine partagée, ni popup, ni formulaire, ni navigation de la fenêtre principale) ; une origine par module (`avenoir-module://<id>/`) qui ne sert que les fichiers du module ; CSP stricte **sans accès réseau** (`connect-src 'none'`) : les données passent par le processus du module, soumis à ses permissions ; seul canal : `postMessage`, avec une API minimale (recevoir les données de *son* module, lui envoyer des événements), chaque message étant validé par l'hôte.
13. **Confirmations non imitables** : les demandes de confirmation et de permission sont toujours dessinées par Avenoir, hors de tout contenu de module.
14. **Réglages modifiables uniquement par Avenoir** : le démon est le seul rédacteur ; fichier scellé (HMAC, clé dans le trousseau du système) ; toute modification extérieure est refusée, annulée par restauration de la dernière version valide, signalée et journalisée (§ 5.1). C'est une preuve de falsification, pas une protection contre un logiciel malveillant disposant des droits de l'utilisateur.
15. **Ne jamais faire entièrement confiance à un module communautaire.** Un module est du code tiers : tout ce qu'il déclare ou renvoie est traité comme non fiable, et des sécurités s'appliquent toujours, quel que soit son auteur :
    - son manifeste est validé avant tout lancement (identifiant, commande sans shell ni chemin sortant de son dossier, outils, règles limitées à ses propres outils) ; un module installé par l'utilisateur est **désactivé** tant qu'il ne l'a pas activé ;
    - il ne voit qu'un **environnement minimal** (pas les secrets d'Avenoir ni des autres modules) : seulement les variables qu'il déclare, montrées à l'utilisateur ;
    - ses outils sont `sensitive` par défaut ; il ne peut pas abaisser son risque, ni masquer, remplacer ou imiter un outil, une commande ou une confirmation d'Avenoir ;
    - ses réponses sont bornées (taille, délai), validées, et ses descriptions comme ses résultats sont des **données**, jamais des instructions ;
    - chacun de ses appels passe par le contrôle des permissions et le journal d'audit ; un module défaillant ou malveillant est isolé sans faire tomber l'assistant ;
    - plus tard : bac à sable des processus, signature et vérification des modules du magasin. Les modules fournis avec Avenoir suivent les mêmes règles.

---

## 4. Directives de développement

### 4.1 Langue

- **Identifiants** (types, fonctions, variables) : anglais.
- **Commentaires, documentation, messages utilisateur** : français.
- Les textes destinés à l'utilisateur seront externalisés (i18n) quand une deuxième langue sera nécessaire.

### 4.2 C++

- **C++23**, compilateurs cibles : GCC ≥ 14, Clang ≥ 19, MSVC 19.40+ (Visual Studio 2022 17.10+). Clang 18 ne convient pas : la libstdc++ n'y active pas `std::expected` (il faut `__cpp_concepts ≥ 202002L`).
- **CMake ≥ 3.28** avec `CMakePresets.json` ; dépendances via **vcpkg** en mode manifeste (`vcpkg.json`).
- Avertissements stricts et traités comme erreurs : `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` (GCC/Clang), `/W4 /WX /permissive-` (MSVC).
- `clang-format` (style du dépôt, `.clang-format`) et `clang-tidy` (`.clang-tidy`) **sans avertissement** avant chaque commit.
- **Mémoire** : RAII partout ; pas de `new` / `delete` ni de `malloc` / `free` ; `std::unique_ptr` par défaut, `std::shared_ptr` seulement si la propriété est réellement partagée ; pas de pointeur nu propriétaire ; pas de cast C ni de `reinterpret_cast` hors couche système isolée et commentée.
- **Erreurs** : `std::expected<T, Error>` dans les bibliothèques ; pas d'exception qui traverse la frontière d'une bibliothèque (les exceptions des dépendances sont capturées et converties). Les exceptions ne sont tolérées que dans `apps/`, au plus haut niveau.
- **Concurrence** : modèle synchrone + threads (`std::jthread`, `std::stop_token` pour l'annulation) ; aucun état partagé sans synchronisation explicite. Les traitements lourds (STT, TTS) ne bloquent jamais le thread d'interface.
- **Logs** : `spdlog`, niveau contrôlé par la variable `AVENOIR_LOG`.
- **Nommage** : types en `PascalCase`, fonctions et variables en `snake_case`, membres privés suffixés `_`, constantes en `kPascalCase`, espaces de noms `avenoir::<bibliothèque>`.
- En-têtes publics minimaux ; détails d'implémentation dans les `.cpp` ou un espace de noms `detail`.
- Fonctionnalités optionnelles lourdes (moteurs vocaux, UI) derrière des **options CMake** (`AVENOIR_WITH_VOICE`…).
- Bibliothèques de référence : `nlohmann/json`, `toml++`, `cpr` (HTTP), `reproc` (sous-processus), `spdlog`, `Catch2` (tests), `replxx` (saisie de la CLI), `sqlite3` (conversations).

### 4.3 Multiplateforme

- Tout code spécifique à un OS va dans `src/platform/linux.cpp` et `src/platform/windows.cpp`, sélectionnés par CMake, derrière une interface commune (`platform.hpp`), **avec** une implémentation de repli qui renvoie « non supporté ».
- Les outils eux-mêmes restent multiplateformes et appellent `platform::…`.
- Si une fonction n'est pas disponible sur un OS, l'outil n'est **pas enregistré** sur cet OS (le LLM ne doit pas voir un outil inutilisable).
- API de référence : Win32 / WinRT (C++/WinRT), D-Bus via `sdbus-c++` (notifications, MPRIS, NetworkManager, logind), `miniaudio` (audio).
- Sous Windows, chaînes en UTF-8 dans tout le code ; conversion en UTF-16 uniquement à l'appel des API `…W`.
- La CI compile et teste sur **Windows et Linux**.

### 4.4 Écrire un outil natif

1. Choisir un nom `snake_case` court, en anglais, unique.
2. Rédiger une **description pour le LLM** : ce que fait l'outil, quand l'utiliser, exemples d'arguments. C'est elle qui détermine si le modèle s'en sert bien.
3. Définir un JSON Schema minimal avec `required`.
4. Choisir le `RiskLevel` le plus élevé applicable en cas de doute.
5. Lire les arguments depuis le `nlohmann::json` reçu dans une structure typée, puis **valider**.
6. Renvoyer un résultat court et factuel, en phrase française lisible (il est relu par le LLM, affiché tel quel par la voie rapide, et parfois dit à voix haute).
7. Si l'action peut être défaite de façon sûre, renvoyer une **action d'annulation** (qui vérifie que l'état n'a pas changé avant de restaurer) ; sinon, ne rien promettre.
8. Ajouter des tests unitaires sur la validation (et sur l'annulation, le cas échéant).
9. L'enregistrer dans `register_native_tools`.

### 4.5 Écrire un module

- Un dossier avec un `module.toml` (§ 2.6) et le programme du module, dans n'importe quel langage.
- Descriptions d'outils claires, schémas stricts, résultats textuels concis, en phrases lisibles.
- Demander le minimum de permissions ; préférer un onglet déclaratif à un onglet web.
- Renvoyer une erreur explicite plutôt que planter.
- Ne rien écrire sur stdout d'autre que du JSON-RPC, un message par ligne (les logs vont sur stderr, conservés dans `logs/modules/<id>.log`).
- Protocole `avenoir` : répondre à `initialize`, `tools/list` et `tools/call` comme un serveur MCP (outils déclarés dans `[[tools]]`, résultats `content` textuels, `isError` pour une erreur), et à `avenoir/undo` si un outil propose une annulation (`_meta["avenoir/undo"]`, avec vérification que l'état n'a pas changé).
- Garder son état dans `AVENOIR_MODULE_DATA` ; demander ses secrets par `env` (jamais dans le manifeste).
- Un serveur MCP existant devient un module avec `protocol = "mcp"`.
- Exemple complet : `modules/meteo/` (C++, tests sans réseau dans `modules/meteo/tests/`).

### 4.6 Interface web

- **TypeScript** en mode strict pour tout le code de `ui/` ; framework à choisir (§ 8).
- L'interface ne parle qu'à l'hôte C++ des fenêtres, par un pont typé ; aucun accès direct au démon, aux modules ou au réseau.
- Contenu des modules : rendu déclaratif par défaut ; onglets web uniquement dans les iframes isolées (directive n° 12).

### 4.7 Tests

- Tests unitaires `Catch2` dans `libs/<nom>/tests/`, lancés par `ctest`.
- La boucle agent se teste avec un **LLM scripté** (faux `LlmProvider` qui renvoie des réponses préparées), sans réseau.
- Tout outil a des tests de validation d'arguments (cas valides **et** tentatives d'injection).
- Les tests ne doivent jamais lancer de vraie application ni modifier le système.
- En CI : une passe avec AddressSanitizer + UndefinedBehaviorSanitizer sous Linux.

### 4.8 Git

- Branches courtes, messages de commit au format *Conventional Commits* (`feat:`, `fix:`, `refactor:`…).
- Toute décision d'architecture notable : ajout d'une entrée dans la section 7 (« Journal des décisions »).
- `docs/FEATURES.md` liste toutes les fonctionnalités existantes, de façon exhaustive : chaque fonctionnalité ajoutée, modifiée ou retirée y est reportée dans le même commit.
- Images du README dans `docs/readme/`, avec leurs générateurs : `penguin.py` (pingouin SVG, à garder identique à `Display::banner`), `capture.sh` + `terminal_render.py` (capture réelle de la CLI rendue en PNG).

---

## 5. Configuration

Fichier `config.toml` (voir `config.example.toml`) :

| Section | Rôle |
|---|---|
| `[llm]` | Fournisseur (`openai_compatible`, `anthropic` ou `claude_code`), modèle, URL, **nom** de la variable d'environnement de la clé |
| `[agent]` | `max_steps`, `fast_path`, `system_prompt` |
| `[permissions]` | Politique par niveau de risque |
| `[modules.<id>]` | Réglages de l'utilisateur par module : activation, risque relevé par outil |

Emplacement cible (phase 3) : `%APPDATA%\avenoir\config.toml` sous Windows, `$XDG_CONFIG_HOME/avenoir/config.toml` sous Linux.

### 5.1 Réglages modifiables uniquement par Avenoir

**Toute modification des réglages en dehors d'Avenoir est interdite** : les réglages se changent depuis l'application (ou la CLI officielle), jamais en éditant le fichier.

- **Un seul rédacteur** : seul le démon écrit les réglages, à la demande des interfaces officielles (application, CLI) via l'IPC. Chaque changement est journalisé : qui, quand, valeur avant et après.
- **Sceau d'intégrité** : le fichier de réglages est accompagné d'un sceau HMAC-SHA256 calculé avec une clé secrète rangée dans le **trousseau du système** (Windows Credential Manager, Secret Service sous Linux). Le démon vérifie le sceau au démarrage et à chaque modification du fichier, qu'il surveille.
- **Modification extérieure détectée** (sceau invalide) : elle est **refusée** ; le démon **restaure** la dernière version valide (copie scellée), **alerte** l'utilisateur dans le widget et l'application en montrant ce qui a été changé, et l'inscrit au journal. Pas de « accepter quand même » : pour changer un réglage, on passe par l'application.
- **Sans trousseau disponible** (Linux sans Secret Service) : clé dans un fichier lisible par l'utilisateur seul (`0600`) ; protection plus faible, signalée par Avenoir.

**Limites** : c'est une **preuve de falsification**, pas une protection absolue. Le mécanisme détecte et annule les éditions à la main, les scripts et les programmes sans accès au trousseau ; il ne protège pas contre un logiciel malveillant qui s'exécute avec le compte de l'utilisateur et lit son trousseau, ni contre un administrateur. Une protection plus forte (démon sous un compte système dédié) reste possible plus tard.

**Transition** : jusqu'au jalon 3 (démon et trousseau), `config.toml` reste éditable à la main, **en mode développement uniquement**. Ensuite, les réglages deviennent un fichier géré et scellé par le démon ; `config.example.toml` ne sert plus qu'au premier lancement ou à un import depuis l'application.

---

## 6. Feuille de route

> Plan détaillé, priorités et critères de fin : [`ROADMAP.md`](ROADMAP.md).

### Phase 1 : socle en mode texte
- [x] Projet CMake + vcpkg + préréglages
- [x] CI Windows + Linux au vert, releases automatiques
- [x] Boucle agent, permissions, voie rapide
- [x] Fournisseurs LLM configurables (OpenAI-compatible, Anthropic, Claude Code)
- [x] Outils : `current_time`, `system_info`, `launch_app`, `open_url`
- [x] Hôte de modules v1 (manifeste, JSON-RPC, outils, voie rapide, commandes) + compatibilité MCP
- [x] Module météo fourni (Open-Meteo)
- [x] CLI : confirmations, Ctrl+C, annulation, apparence soignée, barre de saisie fixe
- [x] Conversations persistantes (SQLite)
- [ ] Tester avec plusieurs LLM réels (local et distant), ajuster le prompt système — fait sous Linux (banc d'essai `evals/`, 12 modèles, prompt v3, modèles recommandés) ; reste l'essai sous Windows

### Phase 2 : contrôle système
- [ ] Volume, luminosité, mode sombre, verrouillage, veille (`sensitive` : extinction, redémarrage)
- [ ] Contrôle multimédia (MPRIS / SMTC)
- [ ] Notifications système
- [ ] Recherche d'applications installées (fichiers `.desktop`, menu Démarrer) pour `launch_app` tolérant
- [ ] Recherche web (module : SearXNG auto-hébergé, ou API Brave/Tavily) + lecture de page
- [ ] Streaming des réponses du LLM

### Phase 3 : résident
- [ ] Emplacement standard de la config, rotation des logs, fichier d'audit
- [ ] Mode démon (service Windows / unité systemd utilisateur) + IPC locale pour les interfaces
- [ ] Journal structuré : historique par conversation et global
- [ ] Permissions durables (« toujours autoriser ») et révocables
- [ ] Raccourci clavier global
- [ ] Mémoire vidéo partagée avec les jeux : décharger le modèle local ou basculer sur le modèle léger quand une application exigeante passe en plein écran

### Phase 4 : tâches et modules avancés
- [ ] Outils fichiers restreints à des dossiers autorisés
- [ ] Appels entre modules via l'hôte ; fonctions de fond des modules ; journal par module
- [ ] Gestion du contexte long (résumé de l'historique)
- [ ] Mémoire persistante optionnelle (préférences utilisateur)

### Phase 5 : voix
- [ ] Capture audio (`miniaudio`) + détection d'activité vocale (VAD)
- [ ] STT (`whisper.cpp` ou `sherpa-onnx`)
- [ ] TTS (Piper)
- [ ] Mot de réveil (openWakeWord / sherpa-onnx)
- [ ] Confirmations vocales

### Phase 6 : interface graphique (web dans la vue web système)
- [ ] Hôte C++ des fenêtres (WebView2 / WebKitGTK, layer-shell sous Wayland)
- [ ] Widget en haut de l'écran : barre pliée, widget déplié au survol, pingouin animé
- [ ] Onglets du widget : activités, historique, infos système, version et mises à jour, chatbox, consommation Claude Code
- [ ] Onglets des modules : déclaratifs, puis web isolés
- [ ] Application principale : réglages, magasin de modules, conversations, historiques et logs, permissions, modules installés

---

## 7. Journal des décisions

| # | Décision | Raison |
|---|---|---|
| 1 | C++23 pour le cœur | Démon léger, binaire natif, accès direct aux API Win32 / WinRT et Linux, écosystème mûr pour l'audio et l'inférence locale (whisper.cpp, llama.cpp, ONNX Runtime) |
| 2 | LLM au centre + voie rapide par règles | Questions ouvertes et tâches multi-étapes impossibles avec des règles seules ; règles gardées pour la réactivité |
| 3 | Fournisseur LLM configurable | Liberté local / distant, coût, vie privée |
| 4 | MCP comme protocole de plugins | Standard ouvert, multi-langage, isolation par processus, écosystème existant |
| 5 | Risque des plugins fixé par l'utilisateur, `sensitive` par défaut | Ne pas faire confiance à l'auto-déclaration d'un plugin |
| 6 | `sensitive` jamais en `allow` | Garde-fou non contournable par erreur de configuration |
| 7 | Texte d'abord, voix ensuite | Mettre au point la logique agent sans la complexité audio |
| 8 | Nom du projet : **Avenoir** (exécutable `avenoir`, espace de noms `avenoir`) | Remplace le nom de travail « osa » |
| 9 | Fournisseur `claude_code` : la CLI officielle `claude -p` sert de moteur de langage (outils intégrés, MCP, hooks et réglages désactivés ; outils d'Avenoir décrits dans le prompt ; réponse en sortie structurée) | Utiliser l'abonnement Claude (Pro / Max) sans clé d'API. On passe par la CLI officielle plutôt que de réutiliser le jeton OAuth de l'abonnement, ce qu'Anthropic n'autorise pas pour des applications tierces. Plus lent que l'API (un processus par appel) ; usage personnel |
| 10 | CMake + vcpkg (manifeste) | Standard de fait, bon support de MSVC et des IDE ; dépendances reproductibles sur Windows comme sur Linux |
| 11 | `std::expected` plutôt qu'exceptions dans les bibliothèques | Erreurs explicites dans les signatures, coût prévisible, pas d'exception qui s'échappe d'un thread ou d'un plugin |
| 12 | Modèle synchrone + threads plutôt que coroutines / Asio | La boucle agent est séquentielle par nature ; plus simple à lire et à déboguer. À réévaluer pour le démon (phase 3) |
| 13 | Voie rapide sans LLM, réponses en phrases françaises | Latence quasi nulle et fonctionnement hors ligne ; le texte de l'outil est affiché (et lu) tel quel |
| 14 | Dépôt public, CI GitHub Actions (Linux + Windows) à chaque push, releases publiées automatiquement sur tag `vX.Y.Z` | Actions gratuit et illimité pour un dépôt public ; le code Windows est compilé et testé en continu sans machine Windows ; binaires téléchargeables à chaque version |
| 15 | Licence **PolyForm Strict 1.0.0** (source consultable, pas open source) | Code lisible et utilisable à titre personnel et non commercial ; modification, œuvre dérivée et redistribution soumises à l'accord écrit de l'auteur. GitHub permet toujours de forker un dépôt public, mais la licence n'accorde aucun droit sur ce fork |
| 16 | **Modules** en processus séparés, dialoguant avec le démon en JSON-RPC 2.0 sur stdio, décrits par un manifeste `module.toml` (outils, commandes, voie rapide, onglets, permissions). Étend la décision 4 | Isolation (un module qui plante ne fait pas tomber l'assistant), n'importe quel langage, déclarations vérifiables avant lancement ; MCP seul ne couvre ni l'interface, ni les commandes, ni la voie rapide, ni les appels entre modules |
| 17 | **Compatibilité MCP** : un serveur MCP existant s'utilise comme module (`protocol = "mcp"`, outils seulement) | Profiter de l'écosystème MCP sans en faire la limite du système de modules |
| 18 | **Démon et interface séparés** ; l'interface est un seul processus avec deux fenêtres (widget et application principale) | Le démon reste léger et toujours actif ; l'interface peut redémarrer ou être fermée sans interrompre l'assistant ; un seul état d'interface partagé par les deux fenêtres |
| 19 | **Interface web** (HTML / CSS / TypeScript) dans la vue web du système, hébergée par un petit hôte C++ ; ni Electron, ni Qt | Écosystème le plus riche pour une interface moderne et très soignée (composants, animations Rive / Lottie) ; onglets web des modules naturels ; vue web système plutôt qu'Electron pour la légèreté. Contreparties acceptées : TypeScript en plus du C++, petits écarts de rendu WebView2 / WebKitGTK, mémoire supérieure à Qt Quick |
| 20 | **Onglets de modules déclaratifs par défaut**, web isolés sur permission `ui.web` (directive de sécurité n° 12) | Liberté pour les modules qui en ont besoin, sans exécuter de code de module dans l'interface par défaut ni lui donner accès au réseau ou aux autres modules |
| 21 | **Baisse du risque d'un outil de module** possible, uniquement depuis l'application ou la CLI, après avertissement, disclaimer de responsabilité et saisie de « Je sais ce que je fais » ; accord enregistré, journalisé, révocable ; baisse sans accord ignorée | Laisser l'utilisateur maître de sa machine tout en rendant le choix explicite et traçable. Abaisser un outil `sensitive` lui retire la confirmation obligatoire : la décision 6 reste vraie pour la politique par niveau, mais pour cet outil c'est l'utilisateur qui lève la protection, à ses risques |
| 22 | **Réglages modifiables uniquement par Avenoir** : démon seul rédacteur, fichier scellé par HMAC (clé dans le trousseau du système), modification extérieure refusée et restaurée, alerte et journal ; `config.toml` éditable à la main seulement en développement, jusqu'au jalon 3 | Garantir que les protections (permissions, risques, baisses avec accord) ne se contournent pas en éditant un fichier, et que chaque changement est traçable. Limite assumée : preuve de falsification, pas protection contre un logiciel malveillant avec les droits de l'utilisateur |
| 23 | **Annulation des actions** : chaque outil déclare s'il sait défaire son action ; journal d'annulation par requête ; proposé à l'interruption (Ctrl+C) et par la commande `/annuler` ; annulations en ordre inverse, avec vérification de l'état et confirmation de l'utilisateur | Pouvoir revenir en arrière sans rien promettre d'impossible : « réversible » ne veut pas dire « annulable automatiquement » (une URL ouverte ou une application lancée ne se défont pas proprement) ; la vérification d'état évite qu'une annulation écrase un changement fait entre-temps |
| 24 | **Conversations persistantes** dans une base SQLite locale (`avenoir::storage`) : écriture immédiate de chaque message, reprise de la dernière conversation, commandes `/nouvelle`, `/conversations`, `/ouvrir`, `/renommer`, `/supprimer`, `/historique` | Ne rien perdre entre deux sessions et pouvoir tout rétablir ; SQLite est fiable, transactionnel, multiplateforme, dans le domaine public (compatible avec la licence) et servira aussi au journal global du jalon 3 |
| 25 | **CLI soignée** : un style par type d'élément (requête, réponse, action, confirmation, erreur), Markdown rendu, toupie, actions affichées en direct ; bibliothèque `avenoir::terminal` ; couleurs désactivées hors terminal et avec `NO_COLOR` | Distinguer d'un coup d'œil ce qui vient de l'utilisateur, de l'assistant ou d'une action ; rester propre dans un tube ou un script, et respecter la convention `NO_COLOR` |
| 26 | **replxx** pour la saisie de la CLI (édition, historique, complétion) ; ses « suggestions » portent la bordure du bas de la barre de saisie | Petit, multiplateforme, UTF-8, licence BSD ; distingue Ctrl+C de Ctrl+D ; seul contenu qu'il redessine sous la saisie à chaque frappe |
| 27 | **Confidentialité et sécurité primordiales** (principe n° 1) ; **ne jamais faire entièrement confiance à un module communautaire** (directive n° 15) | L'assistant agit sur une vraie machine et voit des données personnelles : en cas de conflit avec une fonctionnalité ou le confort, elles l'emportent. Un module est du code tiers : des sécurités s'appliquent toujours, quel que soit son auteur |
| 28 | **Protocole `avenoir` = sur-ensemble de MCP** (`initialize`, `tools/list`, `tools/call`, `notifications/cancelled`) + méthodes `avenoir/*` (annulation par `avenoir/undo`, jeton dans `_meta`) ; remplace `[[mcp_servers]]` | Un seul client pour les deux protocoles, un serveur MCP devient un module sans adaptation, un module `avenoir` se teste avec l'outillage MCP ; `_meta` est le point d'extension prévu par MCP |
| 29 | **Environnement minimal** des modules : strict nécessaire, proxys seulement avec `network`, variables déclarées dans `env` (montrées à l'utilisateur) | Aucun module ne voit les clés d'API d'Avenoir ni les secrets des autres modules ; ce qu'il reçoit est explicite |
| 30 | **Module installé par l'utilisateur désactivé par défaut**, modules fournis activés ; un module ne remplace ni un module fourni, ni un outil, ni une commande d'Avenoir | Déposer un dossier ne suffit pas à exécuter du code : l'utilisateur l'active en connaissance de cause (en attendant l'installation guidée de l'application) |
| 31 | **Choix de risque des outils de modules** (`/risque`) enregistrés dans la base SQLite d'Avenoir, appliqués à chaud ; `config.toml` ne peut que relever (plancher) | Hors du fichier édité à la main ; une baisse n'existe qu'avec l'accord saisi. Le sceau des réglages (jalon 3) protégera aussi ces choix |
| 32 | **`Tool::call` reçoit un `std::stop_token`** : un appel de module interrompu par Ctrl+C rend la main tout de suite et le module reçoit `notifications/cancelled` | Ctrl+C doit toujours reprendre la main, même face à un module lent ou bloqué |
| 33 | **Module météo fourni**, en C++, données **Open-Meteo** (sans clé, CC BY 4.0, gratuit pour un usage non commercial) ; ville par défaut réglée par l'utilisateur, pas de géolocalisation | Premier vrai module, qui sert aussi d'exemple ; aucune donnée envoyée hormis la ville demandée et ses coordonnées ; pas de service tiers de géolocalisation par IP |
| 34 | **Banc d'essai avec de vrais modèles** (`evals/`, `avenoir-evals`) : requêtes de référence en TOML, vrai agent et vrai fournisseur, mais **doublures d'outils** (mêmes nom, description, schéma et risque, résultats préparés) ; rapport Markdown versionné ; jamais lancé par ctest ni la CI | Mesurer plutôt que deviner (choix des modèles, effet d'une phrase du prompt) ; reproductible et sans effet sur la machine ; les doublures permettent de tester les injections dans les résultats d'outils |
| 35 | **`reasoning_effort`** facultatif pour `openai_compatible` (« none » recommandé en local) | Les modèles qui réfléchissent avant de répondre ajoutent des secondes de latence et des centaines de tokens, inacceptables pour un assistant système ; « none » ne dégrade pas les résultats du banc |
| 36 | **Prompt système v3** et **modèles recommandés** (Gemma 4 26B-A4B en local 24 Go, Ministral 3 8B en léger, Claude en distant), choisis sur le banc d'essai ; la consigne de protocole de `claude_code` dit que les outils listés sont disponibles et se demandent dans `tool_calls` | Chaque règle du prompt répond à un échec mesuré ; une règle qui dit seulement quand demander rend un petit modèle hésitant (v2). En léger, la sécurité départage : on préfère le modèle qui n'a suivi aucune injection |
| 37 | **Mêmes symboles d'une colonne sur tous les systèmes** dans la CLI : `›` (requête), `»` (action), `▲` (avertissement), au lieu de `❯`, `⚙` et `⚠` | Essai sous Windows : `❯` manque aux polices de console (carré), `⚙` et `⚠` y sont dessinés en emoji de double largeur qui chevauchent le texte ; un seul jeu de symboles évite deux apparences à maintenir |

---

## 8. Questions ouvertes

- Framework de l'interface web (Svelte, Solid, React…).
- Bibliothèque C++ de vue web (`saucer`, `webview`…) et intégration layer-shell sous Wayland.
- Magasin de modules : hébergement, signature et vérification des modules.
- Format exact des composants déclaratifs des onglets.
- Moteur de recherche web par défaut.
