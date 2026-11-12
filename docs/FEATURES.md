# Fonctionnalités d'Avenoir

Liste **exhaustive** de ce qu'Avenoir sait faire aujourd'hui, mise à jour à chaque fonctionnalité ajoutée. Ce qui est prévu mais pas encore fait est dans [`ROADMAP.md`](ROADMAP.md) ; le pourquoi des choix, dans [`PROJECT.md`](PROJECT.md) ; le fonctionnement du code, dans [`FONCTIONNEMENT.md`](FONCTIONNEMENT.md).

Plateformes : **Linux** et **Windows** (sauf mention contraire).

## Sommaire

1. [Dialogue et fournisseurs LLM](#1-dialogue-et-fournisseurs-llm)
2. [Voie rapide](#2-voie-rapide)
3. [Outils natifs](#3-outils-natifs)
4. [Sécurité et permissions](#4-sécurité-et-permissions)
5. [Interruption et annulation](#5-interruption-et-annulation)
6. [Conversations](#6-conversations)
7. [Modules](#7-modules)
8. [Module météo](#8-module-météo)
9. [Démon et IPC](#9-démon-et-ipc)
10. [Équipe d'agents (orchestrateur)](#10-équipe-dagents-orchestrateur)
11. [Interface en ligne de commande](#11-interface-en-ligne-de-commande)
12. [Configuration](#12-configuration)
13. [Journaux](#13-journaux)
14. [Banc d'essai avec de vrais modèles](#14-banc-dessai-avec-de-vrais-modèles)
15. [Développement, qualité et publication](#15-développement-qualité-et-publication)

## 1. Dialogue et fournisseurs LLM

- Dialogue en langage naturel, en français, avec réponses brèves (prévues pour être lues à voix haute plus tard).
- **Boucle agent** : le LLM choisit des outils, Avenoir les exécute après contrôle des permissions, renvoie les résultats, et recommence jusqu'à la réponse finale ; nombre d'allers-retours limité (`max_steps`, 10 par défaut).
- **Réponses au fil de l'eau** (API compatibles OpenAI, dont Ollama, et API Anthropic) : le texte s'affiche pendant que le modèle l'écrit, ligne par ligne, Markdown rendu ; un bloc de code ou un tableau s'affiche une fois complet ; les appels d'outils arrivent aussi en flux ; une réponse écartée par la vérification (action annoncée mais non faite) est signalée (« ↻ Réponse écartée… Nouvelle tentative… ») avant la suivante ; Ctrl+C arrête aussi une réponse en cours d'écriture. Claude Code montre encore sa réponse en entier.
- Plusieurs appels d'outils dans une même réponse ; suite de conversation (« et demain ? ») grâce à l'historique.
- Résultats d'outils tronqués à 20 000 caractères avant d'être renvoyés au LLM (sans couper un caractère UTF-8).
- Réponse vide du modèle signalée par une erreur explicite (la question peut être reposée telle quelle).
- **Prompt système** par défaut ajusté sur le banc d'essai (prompt v4 : entre autres, aucune donnée sans outil), remplaçable dans la configuration.
- **Trois fournisseurs** :
  - `openai_compatible` : toute API `/chat/completions` (Ollama, llama.cpp server, LM Studio, vLLM, OpenAI, Mistral, OpenRouter…), appels d'outils natifs, arguments acceptés en chaîne JSON ou en objet (Ollama), `temperature` et `reasoning_effort` (`none`, `low`, `medium`, `high`) réglables ;
  - `anthropic` : API Messages, appels d'outils natifs, fusion des messages consécutifs de même rôle ;
  - `claude_code` : abonnement Claude (Pro / Max) via la CLI officielle `claude -p`, sans clé d'API ; outils, plugins, hooks, MCP et réglages de Claude Code désactivés ; outils d'Avenoir décrits dans le prompt, réponse imposée par `--json-schema` ; prompt passé par stdin (taille illimitée) ; clés d'API retirées de l'environnement ; dossier de travail temporaire ; délai réglable ; **processus `claude` gardé ouvert** (`--input-format stream-json`, réglage `persistent`, vrai par défaut) : seuls les nouveaux messages lui sont envoyés, la session est relancée avec tout l'historique si la conversation change (autre conversation, réponse écartée, autre prompt système) ; deux sessions au plus (conversation et lecture de pages), arrêtées en changeant de modèle et à la fermeture d'Avenoir ; mesuré : 6,1 s au lieu de 14,8 s pour trois requêtes enchaînées (haiku) ; CLI non connectée : « Claude Code n'est pas connecté à un compte : lancez `claude` dans un terminal, puis `/login` ».
- Clé d'API lue dans la variable d'environnement dont la configuration donne le **nom** ; jamais citée dans une erreur ni un log.
- Consommation remontée par les trois fournisseurs (tokens en entrée et en sortie, coût indicatif) et conservée avec chaque réponse.
- Annulation d'une requête en cours chez tous les fournisseurs (requête HTTP interrompue, processus `claude` arrêté).
- Modèles recommandés et mesurés : Gemma 4 26B-A4B (local, 24 Go), Ministral 3 8B (local léger), Claude (distant) — PROJECT.md § 2.5.
- **Modèles** : un par section `[models.<nom>]` ; le premier sert au premier lancement, puis Avenoir reprend le **dernier modèle utilisé** (retenu dans sa base) ; sans aucun modèle, seule la voie rapide répond (une requête qui demande le LLM reçoit un message clair). **Changement à tout moment**, la conversation continuant avec le nouveau modèle :
  - commande `/modele` (liste, modèle actif marqué) et `/modele <nom>` ;
  - outil `switch_llm` (réversible, annulable : `/annuler` revient au modèle précédent, sauf si le modèle a changé entre-temps), que le modèle actif peut appeler ;
  - voie rapide : « passe / bascule / change sur (le) modèle X », « utilise le modèle X », et, pour un modèle de la configuration nommé seul, « bascule sur qwen », « passe à gemma », « change de modèle et bascule sur qwen » (tout autre mot part au LLM : « passe à la suite ») ;
  - **le modèle connaît son nom** : le modèle actif (« qwen : qwen3.8:27b, local ») est ajouté à la fin du prompt système et mis à jour à chaque changement ; « tu es quel modèle ? » ne se devine plus d'après la conversation (voie rapide : « tu es quel modèle ? », « quel modèle es-tu ? » → `llm_status`) ;
  - **changement annoncé mais non fait** (voir aussi § 4) : une réponse qui annonce un changement de modèle (« je bascule sur qwen ») sans appel de `switch_llm`, ou un chargement sans `load_llm` / `unload_llm`, est écartée et le modèle relancé une fois avec un rappel ; s'il recommence, sa réponse est suivie de « (Avenoir : aucun changement de modèle n'a été fait. Utilise `/modele <nom>`…) » : une action non faite n'est jamais présentée comme faite ; activé par défaut, désactivable (`[agent] verify_replies = false`) ;
  - nom reconnu sans casse ni accents (« Léger » → `leger`), ou par identifiant du modèle (« ministral-3:8b ») ;
  - **mode « local uniquement »** (`[agent] local_only = true`) : un modèle distant n'est jamais utilisé, ni au lancement (le premier modèle local à la place ; aucun : voie rapide seule, avec un avertissement), ni par un changement (refusé sans demander) ; `/modele` les marque indisponibles ;
  - **passer à un modèle distant demande toujours confirmation** (la conversation sortirait de la machine) ; local = adresse de la machine (`localhost`, `127.x`, `::1`) ou `local = true` déclaré pour un serveur du réseau local ;
  - modèle actif affiché dans le cadre d'accueil ;
  - **l'ancien modèle est déchargé** après la requête quand il est local, servi par le **même serveur** que le nouveau et différent de lui (sur un autre serveur, le décharger ne libérerait rien pour le nouveau) ; annoncé (« L'ancien modèle « gemma » sera déchargé », puis « déchargé : sa mémoire est libérée ») ; annulé si l'on revient sur lui avant la fin de la requête ; désactivable (`[agent] unload_on_switch = false`).
- **Chargement visible** : quand le modèle actif n'est pas en mémoire (Ollama le décharge après une inactivité), la toupie affiche « Chargement du modèle « gemma »… » au lieu de « Réflexion… », puis « Modèle « gemma » chargé en 12 s. » ; savoir s'il est chargé passe par `/api/ps` d'Ollama (délai court, rien n'est annoncé dans le doute). `/charger`, `/decharger` et `/recharger` affichent aussi « Chargement / Déchargement / Rechargement du modèle… ».
- **Surveillance des modèles** : pendant la saisie, Ollama est interrogé toutes les 3 s ; un modèle local chargé ou déchargé **hors d'Avenoir** (déchargement d'Ollama après inactivité, autre programme) est annoncé dans l'historique sans toucher à la saisie en cours (le message passe par la boucle de replxx, curseur gardé avec ESC 7 / ESC 8) ; ce qu'Avenoir charge ou décharge lui-même n'est pas annoncé deux fois.
- **État des modèles** : `/memoire [nom]`, outil `llm_status` (lecture seule), voie rapide (« quels modèles sont chargés ? », « état des modèles », « le modèle gemma est-il chargé ? ») : chargé avec la mémoire vidéo occupée, pas chargé, distant, ou état inconnu ; modèle actif signalé.
- **Raisonnement à chaud** (`openai_compatible`) : `/raisonnement [niveau] [modèle]` (sans niveau : l'actuel), outil `set_reasoning` (annulable) que le modèle peut appeler, voie rapide (« mets / passe / règle le raisonnement à X », « raisonnement X », « active / désactive le raisonnement ») ; niveaux `aucun`, `faible`, `moyen`, `élevé`, `défaut` (valeurs de l'API acceptées) :
  - appliqué dès la réponse suivante, **sans recharger le modèle** (`reasoning_effort` est un paramètre de chaque requête) ; la conversation continue ;
  - réglable aussi pour un modèle qui n'est pas actif ; retenu par modèle d'un lancement à l'autre ; affiché par `/modele`.
- **Charger, décharger, recharger un modèle local** (servi par Ollama, `keep_alive`) : commandes `/charger [nom]`, `/decharger [nom]`, `/recharger [nom]` (le modèle actif par défaut), outils `load_llm` (option `reload`) et `unload_llm` que le modèle peut appeler, voie rapide (« décharge / stoppe / arrête / éteins le modèle [X] », « libère la carte graphique », « charge / allume / démarre / lance le modèle [X] », « recharge le modèle [X] », et pour un modèle de la configuration nommé seul : « charge qwen », « décharge / stoppe gemma ») :
  - décharger libère la mémoire vidéo (mesuré : 12 Go → 1 Go pour Ministral 3 8B) ; le modèle se recharge tout seul à la requête suivante ;
  - décharger le modèle actif pendant une requête est **différé à la fin de la réponse** (sinon elle le rechargerait aussitôt), puis annoncé ;
  - refusé pour un modèle distant ou un serveur qui n'est pas Ollama, avec un message clair.

## 2. Voie rapide

- Commandes simples reconnues par des règles strictes et exécutées **sans LLM** : réponse quasi instantanée, même hors ligne.
- Règles d'Avenoir : « ouvre / va sur <URL> », « ouvre / lance / démarre <application> », « quelle heure est-il ? », « quel jour sommes-nous ? », et le contrôle du système (§ 3).
- Règles déclarées par les modules (limitées à leurs propres outils, arguments fixes possibles) ; elles ne masquent jamais celles d'Avenoir.
- Insensible à la casse, sur la phrase entière ; dans le doute, la phrase part au LLM.
- Même contrôle des permissions et même journal d'annulation que les appels du LLM ; le texte de l'outil est affiché tel quel.
- Désactivable (`fast_path = false`).

## 3. Outils natifs

| Outil | Risque | Ce qu'il fait |
|---|---|---|
| `current_time` | lecture seule | date et heure locales en phrase française (« Il est 13 h 41, vendredi 6 novembre 2026. »), fuseau du système |
| `system_info` | lecture seule | système, distribution, architecture, nom de la machine, nombre de cœurs |
| `launch_app` | réversible | lance une application **par le nom qu'on lui donne** (« bloc-notes », « calculatrice », « Visual Studio Code », « code ») : cherchée d'abord parmi les applications installées (nom affiché, nom générique, mots-clés, exécutable ; sans casse, accents ni ponctuation), puis comme exécutable (Linux : `gtk-launch` puis PATH, détaché ; Windows : `ShellExecuteW`) ; « navigateur » lance le navigateur par défaut ; plusieurs candidates : listées pour demander laquelle ; introuvable : signalé clairement |
| `list_apps` | lecture seule | cherche parmi les applications installées (« éditeur de texte ») ; sans recherche, leur nombre et une partie de la liste |
| `open_url` | réversible | ouvre une adresse `http(s)` dans le navigateur par défaut (Linux : `xdg-open` ; Windows : `ShellExecuteW`) |
| `set_volume` | réversible | volume de la sortie audio : niveau (0 à 100 %), variation (« +10 », « -10 »), son coupé ou remis ; sans argument, donne le volume ; régler le volume remet le son ; **annulable** (Linux : `wpctl`, sinon `pactl` ; Windows : Core Audio ; non proposé sans sortie audio) |
| `media_control` | réversible | lecteur multimédia actif : lecture/pause, lecture, pause, suivant, précédent, arrêt ; `now_playing` dit ce qui passe (titre, artiste, état) (Linux : `playerctl`, MPRIS ; Windows : contrôles multimédias du système, SMTC) |
| `set_brightness` | réversible | luminosité de l'écran intégré : niveau (1 à 100 %, jamais 0) ou variation ; sans argument, la donne ; **annulable** (Linux : `brightnessctl` ; Windows : WMI) ; non proposé sans écran réglable (PC de bureau) |
| `set_dark_mode` | réversible | mode sombre ou clair du système ; sans argument, dit lequel est actif ; **annulable** (Linux : `gsettings`, GNOME et portail XDG ; Windows : registre `Personalize`, applications et système, fenêtres prévenues) |
| `lock_screen` | réversible | verrouille la session (Linux : `loginctl lock-session` ; Windows : `LockWorkStation`) |
| `notify` | lecture seule | notification du système (titre et message) ; texte nettoyé (caractères de contrôle, marques bidirectionnelles et invisibles retirés) et borné (Linux : `notify-send` ; Windows : icône de notification, retirée après 10 s ou à la fermeture) |
| `power` | **sensible** | mise en veille, extinction ou redémarrage, **toujours confirmé** (Linux : `systemctl` ; Windows : `SetSuspendState`, `ExitWindowsEx`) |

- **Index des applications installées** : fichiers `.desktop` sous Linux (dossiers XDG et Flatpak, nom dans la langue du système, traductions françaises en alias, `NoDisplay`/`Hidden` ignorés) ; sous Windows, raccourcis du menu Démarrer (noms localisés : `Notepad.lnk` → « Bloc-notes ») et applications du Store (dossier Applications) ; relu si une demande ne trouve rien ; navigateur par défaut (`mimeapps.list` sous Linux, association `https` sous Windows).
- Voie rapide : « ouvre / lance / démarre [le, la, l'] X » (un seul mot, accents permis : « ouvre le bloc-notes »).
- Voie rapide du contrôle du système : « monte / baisse le son », « coupe le son », « remets le son », « volume à 30 », « pause », « lecture », « morceau suivant / précédent », « qu'est-ce qui passe ? », « luminosité à 60 », « monte / baisse la luminosité », « (active le) mode sombre / clair », « verrouille l'ordinateur », « mets l'ordinateur en veille » (confirmé) ; éteindre et redémarrer passent toujours par le LLM.
- Contrôle du système : programmes du système lancés par tableau d'arguments, jamais par un shell (Linux) ; API natives (Windows) ; nombres validés (« 30 », « 30 % », bornes), annulation refusée si l'état a changé depuis.
- Arguments validés côté outil : nom d'application à chercher sans contrôle, séparateur de commande, chemin ni marque bidirectionnelle (accents et apostrophes permis) ; seul un nom ASCII strict peut être lancé tel quel ; URL `http(s)` uniquement (plus de 40 tentatives d'injection testées).
- Un outil non pris en charge par le système n'est pas proposé.
- **Recherche web** (`web_search`, lecture seule) : titres, adresses et extraits des premiers résultats (5 par défaut, 8 au plus) ; moteur **SearXNG** par défaut (instance locale ci-dessous, ou celle de `[web] searxng_url`, format JSON), sinon **DuckDuckGo** (version HTML, sans compte ni clé) ; si SearXNG ne peut pas tourner, la recherche passe par DuckDuckGo et le dit ; si DuckDuckGo demande un captcha (recherches rapprochées), message clair ; voie rapide « cherche sur le web X » / « recherche sur internet X ».
- **Lecture d'une page** (`read_webpage`, lecture seule) : adresse `http(s)`, question facultative (sans question : résumé) ; page téléchargée (15 s, 3 Mo, 5 redirections `http(s)` au plus, User-Agent « Avenoir/<version> »), réduite à son texte principal (`<main>` ou `<article>`, sans scripts, menus ni pieds de page ; Latin-1 converti), puis **lue à part par le modèle actif, sans outils ni historique** : seule sa réponse entre dans la conversation, une consigne cachée dans la page ne peut donc rien déclencher ; page en JavaScript, PDF ou autre contenu non textuel : refus clair.
- **Adresses locales et privées refusées** pour la lecture et pour DuckDuckGo : `localhost`, `*.local`, `*.lan`, `*.internal`, adresses privées, de lien local, réservées, formes numériques détournées (`2130706433`, `0x7f.1`, `0177.0.0.1`), identifiants dans l'adresse ; l'adresse IP **réellement contactée** est vérifiée par curl à chaque requête, redirections et noms qui s'y résolvent compris. Seule l'instance SearXNG de la configuration peut être locale.
- **SearXNG local** (`[web] local_searxng = true` par défaut) : image officielle `searxng/searxng` lancée par **podman** (sinon docker) **à la première recherche**, téléchargée d'elle-même la première fois (« Première recherche : téléchargement de SearXNG… ») ; port publié sur **127.0.0.1 seulement** (choisi par le système) ; `settings.yml` généré dans le dossier de données (format JSON, clé secrète aléatoire, limiteur et proxy d'images coupés), monté en lecture seule ; **arrêtée après 10 min sans recherche** (`searxng_idle_minutes`) **et à la fermeture d'Avenoir** ; une instance laissée par un lancement interrompu est reprise si elle répond, sinon remplacée ; Ctrl+C interrompt le téléchargement ou le démarrage.
- **Moteur de recherche** : `/moteur` (actif et disponibles), `/moteur <nom>`, outil `set_search_engine` (réversible, annulable ; sa description donne le moteur actuel), voie rapide « utilise duckduckgo pour chercher » ; choix retenu d'un lancement à l'autre ; au départ, `[web] engine`, sinon SearXNG, sinon DuckDuckGo.

## 4. Sécurité et permissions

- **Trois niveaux de risque** par outil : lecture seule, réversible, sensible.
- **Politique par niveau** (`allow`, `confirm`, `deny`), réglable ; une action **sensible n'est jamais exécutée sans confirmation**, quoi que dise la configuration.
- Confirmation au terminal (« Autoriser ? [o/N] ») : tout ce qui n'est pas o / oui / y / yes vaut refus, Ctrl+C aussi.
- Une action refusée revient au LLM comme une erreur, avec la consigne de ne pas la contourner par un autre outil.
- **Actions annoncées mais non faites** (activé par défaut, `[agent] verify_replies = false` pour le couper) : une réponse qui annonce une action sans que l'outil correspondant ait été appelé pendant la requête est écartée, et le modèle relancé une fois avec un rappel ; s'il recommence, la réponse est suivie de « (Avenoir : rien n'a été fait — …) ». Familles vérifiées, seulement si leur outil existe sur la machine : lancer une application ou ouvrir une page (« j'ai lancé Firefox »), régler le son ou la luminosité, contrôler la lecture, changer le mode sombre, verrouiller la session, éteindre / redémarrer / mettre en veille (« l'outil va éteindre l'ordinateur, confirmez-vous ? » : c'est Avenoir qui confirme), afficher une notification, changer le moteur de recherche, changer ou charger un modèle (§ 1). Phrases à la première personne ou de constat : une négation (« je ne peux pas lancer ») ne déclenche rien ; une fausse alerte ne coûte qu'une relance, jamais une action.
- Les données (résultats d'outils, pages, messages, modules) sont traitées comme des données, jamais comme des instructions ; la vraie barrière reste le contrôle des permissions.
- **Provenance des arguments** : une action (hors lecture seule) dont un argument texte apparaît dans un résultat d'outil sans apparaître dans aucune demande de l'utilisateur se fait **confirmer**, même si son niveau de risque l'autoriserait d'ordinaire ; le cadre de confirmation dit quelle valeur vient d'un résultat, et le journal d'audit le note (`provenance : …`). Pour une adresse web, son domaine cité par l'utilisateur suffit (sans « www. »). Ne sont pas examinés : les outils en lecture seule, les valeurs d'une liste fermée (`enum`), les textes de moins de 3 ou de plus de 200 caractères. Pour un agent d'une équipe, seules les demandes de l'utilisateur comptent : la tâche écrite par le coordinateur et les résultats transmis par les autres agents (donnés comme un résultat d'outil) sont des données.
- Aucun outil shell générique ; aucun processus lancé par un shell ou une chaîne construite (tableaux d'arguments, `ShellExecuteW`).
- Secrets uniquement dans des variables d'environnement.
- Dossier de données en `700`, base des conversations en `600` (Linux).
- **Modules jamais entièrement dignes de confiance** : voir § 7.

## 5. Interruption et annulation

- **Ctrl+C n'arrête jamais l'application** : il interrompt l'action en cours (appel au LLM, outil, module) et rend la main tout de suite ; sans action en cours, il efface la saisie.
- Une commande ou une voie rapide interrompue s'affiche « Requête interrompue », jamais comme un échec, et l'historique garde « Action interrompue par l'utilisateur. ».
- Après une interruption : liste des actions effectuées (↺ annulables, ✗ non annulables) et proposition d'annuler celles qui peuvent l'être.
- **`/annuler`** : même proposition pour la dernière requête ; répétée, elle remonte aux requêtes précédentes (20 au plus).
- Annulations en ordre inverse, avec vérification que l'état n'a pas changé depuis (sinon refus, sans rien écraser) ; échecs signalés sans bloquer les suivantes ; chaque annulation est journalisée.
- Les outils de modules peuvent proposer leur propre annulation (ex. : ville par défaut de la météo).

## 6. Conversations

- Chaque message est **enregistré immédiatement** dans une base SQLite locale (`avenoir.db`), avec les appels et résultats d'outils et la consommation.
- Au lancement : reprise de la dernière conversation et réaffichage des 50 derniers messages ; le LLM se souvient.
- Commandes : `/nouvelle [titre]` (rien n'est effacé), `/reset` (efface les messages de la conversation en cours, qui garde son numéro et son titre ; s'il y en a : confirmation, puis proposition d'en garder une transcription Markdown dans `transcriptions/` du dossier de données, lisible par l'utilisateur seul — si elle échoue, rien n'est effacé ; l'écran et l'historique du terminal sont vidés, puis le cadre d'accueil réaffiché avec le chemin de la transcription), `/conversations`, `/ouvrir <n° ou titre>`, `/renommer <titre>`, `/supprimer [n° ou titre]` (avec confirmation, puis proposition de transcription), `/historique`, `/transcription` (même transcription Markdown que `/reset`, sans rien effacer). Transcriptions : en-tête (titre, numéro, date en toutes lettres, nombre d'échanges), une rubrique « › Vous » / « ◆ Avenoir » par message, appels d'outils repliés dans la réponse avec leurs arguments lisibles et leur résultat ; rien d'actif à l'ouverture dans un lecteur Markdown (résultats d'outils en blocs de code, HTML neutralisé et images désactivées hors code, bloc de code resté ouvert refermé), le code des messages restant intact.
- Titre automatique (premiers mots de la première question).
- Requêtes SQL toujours paramétrées ; schéma versionné ; suppression en cascade.
- Une base illisible n'empêche pas de démarrer (conversations alors non conservées, avec un message).

## 7. Modules

- Extension par des **programmes séparés**, dans n'importe quel langage, décrits par un manifeste `module.toml` : outils, commandes, règles de voie rapide, permissions, variables d'environnement.
- **Protocole** : JSON-RPC 2.0 sur stdio, un message par ligne ; protocole `avenoir` = sur-ensemble de MCP (`initialize`, `tools/list` paginé, `tools/call`, `notifications/cancelled`, `avenoir/undo`).
- **Compatibilité MCP** : un serveur MCP existant s'utilise comme module (`protocol = "mcp"`).
- Modules fournis à côté de l'exécutable (activés par défaut) et modules de l'utilisateur dans son dossier de données (**désactivés** tant qu'il ne les a pas activés) ; un module ne peut pas remplacer un module fourni.
- Outils exposés sous le nom `module__outil` ; commandes de module (`/meteo`) qui n'écrasent jamais celles d'Avenoir ; complétion (Tab) et aide incluses.
- **Sécurités** (directive 15) :
  - manifeste validé strictement avant tout lancement (clé inconnue refusée, programme sans `..` ni chemin absolu, règles et commandes limitées aux outils du module) ;
  - lancement sans shell ; **environnement minimal** (jamais les clés d'Avenoir ni les secrets des autres modules ; seulement les variables déclarées) ;
  - outils `sensitive` si le manifeste ne dit rien ;
  - messages bornés (8 Mio), 128 outils au plus, schémas et descriptions bornés, délai maximal par requête (10 s pour la poignée de main, 30 s par appel) ;
  - textes nettoyés (caractères de contrôle, C1, marques bidirectionnelles retirés) ;
  - chaque appel passe par les permissions et l'audit ;
  - un module qui plante, traîne ou triche n'empêche pas le démarrage et ne fait pas tomber l'assistant.
- **Risque des outils** : celui du manifeste, relevable par l'utilisateur (`[modules.<id>] risk` dans la configuration, ou `/risque`) ; une **baisse** exige avertissement, disclaimer et saisie de « Je sais ce que je fais » (accord enregistré dans la base) ; appliqué à chaud ; une baisse sans accord est ignorée avec un avertissement.
- `/modules` : état de chaque module (actif, désactivé, en échec), origine, permissions, outils et risque.
- Dossier de données par module (`module-data/<id>/`, variable `AVENOIR_MODULE_DATA`), stderr dans `logs/modules/<id>.log` (taille bornée).
- Ctrl+C pendant un appel de module : la main revient tout de suite, le module reçoit `notifications/cancelled`.

## 8. Module météo

- Fourni avec Avenoir, activé par défaut, données **Open-Meteo** (CC BY 4.0, sans clé d'API) ; seuls le nom de la ville et ses coordonnées sont envoyés.
- Conditions actuelles (température, ressenti, ciel, vent, humidité) et prévisions jour par jour jusqu'à 7 jours (ciel, minimum, maximum, probabilité et cumul de pluie), en phrases françaises.
- Ville par défaut réglée par l'utilisateur (« ma ville est Lyon », `/ville Lyon`), **annulable** ; pas de géolocalisation.
- Homonymes : pays précisé (« Brest, France ») ou, à défaut, pays de la ville par défaut préféré.
- Commandes `/meteo [ville]`, `/previsions [ville]`, `/ville <ville>` ; voie rapide (« quel temps fait-il à Lyon ? », « va-t-il pleuvoir demain ? », « prévisions météo pour Brest »).
- Noms de ville validés (pas de caractère de contrôle ni de symbole étranger à un nom de lieu).

## 9. Démon et IPC

- **Démon `avenoird`** : garde l'agent, les outils, les modules, les modèles, la recherche web, les conversations, les réglages scellés (vérifiés à son démarrage, seul rédacteur) et le journal d'audit ; les interfaces en sont des clients. Une requête à la fois (un seul agent) ; plusieurs clients peuvent être connectés.
- **Lancement** : par la CLI quand il ne répond pas (détaché du terminal : nouvelle session sous Linux, sans console sous Windows), une seule instance par utilisateur ; arguments `--config`, `--endpoint`, `--exit-when-idle`.
- **Démon privé** : `avenoir chemin/config.toml` lance un démon propre à cette configuration (adresse dérivée de son chemin), non scellée (mode développement), arrêté quand son dernier client part.
- **Commandes** : `/redemarrer` reconstruit Avenoir avec les réglages actuels (après `/reglage` ou `/configurer`) ; `/arreter` arrête le démon ; `/quitter` et Ctrl+D ne ferment que la CLI.
- **Démarrage à l'ouverture de session** (`/demarrage [oui|non]`, sur demande seulement, noté à l'audit) : sous Linux, unité systemd utilisateur `avenoird.service` liée à `graphical-session.target` (environnement du bureau disponible), activée par `systemctl --user` (chemin absolu, tableau d'arguments) ; sans systemd, entrée `~/.config/autostart/avenoird.desktop` ; sous Windows, valeur « Avenoir » de la clé `Run` du compte (sans droits d'administrateur) ; jamais de service système. Sans argument : l'état actuel.
- **Transport** : socket Unix dans `$XDG_RUNTIME_DIR/avenoir/` (sinon `/tmp/avenoir-<uid>/`), dossier `0700` vérifié, identité du pair vérifiée des deux côtés (`SO_PEERCRED`) ; sous Windows, tube nommé `\\.\pipe\avenoir-<SID>` réservé au compte (DACL), clients distants refusés, propriétaire du processus vérifié des deux côtés, client sans délégation d'identité ; messages JSON-RPC 2.0 d'une ligne, 8 Mo au plus ; une seule instance à une adresse.
- **Interface** : le démon fait afficher (messages, actions, réponse au fil de l'eau, toupie et son libellé, cadre d'accueil, conversations), pose les questions et fait confirmer les actions par le client de la requête ; hors requête (modèle déchargé par Ollama, SearXNG arrêté…), les messages vont à tous les clients ; sans client, au journal.
- **Ctrl+C** : la CLI demande l'arrêt de la requête au démon, qui s'arrête proprement puis peut encore proposer d'annuler.
- **Journaux** : `logs/daemon.log` du dossier de données (rotation 5 × 5 Mo, niveau `AVENOIR_LOG`, `info` par défaut) ; audit dans `logs/audit.log`.

## 10. Équipe d'agents (orchestrateur)

- **Délégation** : outil `delegate_tasks` du coordinateur (Avenoir) pour une demande longue ou en plusieurs parties (une demande simple se traite directement) ; plan : tâches (`id`, `agent`, consigne, `depends_on`) et, selon le mode, agents créés à la volée (`name`, `role`, `instructions`, `tools`).
- **Plan validé avant tout lancement** : agents connus ou créés selon `orchestration.mode` (`auto`, `configured`, `both`), outils de chaque agent compris dans ceux permis aux agents (tous sauf `delegate_tasks`, `switch_llm`, `load_llm`, `unload_llm`, `set_reasoning`, `set_search_engine`), 16 au plus par agent, identifiants `[a-z0-9_-]` uniques, dépendances connues, aucun cycle, bornes (`max_tasks`, `max_agents`) ; un plan refusé l'est avec un message précis, que le coordinateur peut corriger.
- **Exécution** : tâches en parallèle (`max_parallel`), une tâche attend le résultat de celles dont elle dépend, transmis comme des données ; une tâche dont une dépendance a échoué n'est pas faite ; Ctrl+C arrête toute l'équipe ; un modèle local sert un agent à la fois (file d'attente) ; compte rendu au coordinateur (résultats bornés, présentés comme des données), qui répond à l'utilisateur.
- **Chaque agent** : une instance à part, avec ses seuls outils, son modèle (`model` ou le modèle actif ; un modèle distant est refusé en mode « local uniquement », et demande confirmation sinon), son nombre d'étapes et un prompt : règles communes d'Avenoir, puis son rôle et sa consigne ; une seule profondeur de délégation.
- **Sécurité** : permissions et confirmations inchangées, confirmations toujours par l'utilisateur, une question à la fois ; journal d'audit avec l'agent qui a agi (`agent X : exécution : …`).
- **Affichage** : « ◇ agent (rôle) : consigne » au début de chaque tâche, ses actions en direct, puis l'issue de chaque tâche.
- **Configuration** : `[orchestration]` (`enabled`, `mode`, `max_tasks`, `max_agents`, `max_parallel`) et `[agents.<nom>]` (`role`, `instructions`, `model`, `tools`, `max_steps`), lus dans l'ordre du fichier, modèle et noms vérifiés ; un outil inconnu des agents est signalé au démarrage.
- **Projets en arrière-plan** (`/projet <nom> <demande>`) : un projet travaille à part de la conversation, en parallèle (4 au plus en même temps), isolé : son propre coordinateur (historique et contexte), ses équipes (la délégation y est permise), le modèle actif au moment de sa création, les seuls outils permis aux agents (jamais ce qui change le modèle ou le moteur de tout Avenoir), son nom au journal d'audit (`agent projet <nom> : …`). Son avancement (actions, fin) est annoncé à tous les clients (`[projet <nom>] …`). Personne n'étant là pour confirmer, une action à confirmer est **refusée** (et c'est dit), une question vaut « non », un modèle distant pour un agent est refusé. `/projets` (état, dernière demande, dernière réponse), `/projet voir <nom>` (historique, une fois le travail fini), `/projet arreter <nom>`, `/projet supprimer <nom>` ; une nouvelle demande au même projet reprend son contexte. Les projets vivent tant que le démon tourne (perdus à `/redemarrer` ou à l'arrêt).
- **Commandes** : `/agents` (équipe, mode, bornes, outils permis aux agents) ; `/agent ajouter <nom> <rôle>`, `/agent outils <nom> <outil,…|aucun>` (seulement des outils permis aux agents), `/agent modele <nom> <modèle|actif>`, `/agent consigne <nom> <texte>`, `/agent etapes <nom> <n>`, `/agent supprimer <nom>` : écrits par le sceau, notés à l'audit, appliqués par `/redemarrer` ; réglages `orchestration.*` dans `/reglage`.

## 11. Interface en ligne de commande

- Exécutable `avenoir [chemin/config.toml]` ; sans argument, la configuration de l'utilisateur (jamais celle du dossier courant).
- **Commandes** : `/aide`, `/outils`, `/annuler`, `/configurer`, `/reglages`, `/reglage <clé> <valeur>`, `/redemarrer`, `/arreter`, `/demarrage [oui|non]`, `/agents`, `/agent …`, `/projet …`, `/projets`, `/modules`, `/risque [outil niveau]`, `/modele [nom]`, `/moteur [nom]`, `/memoire`, `/raisonnement`, `/charger`, `/decharger`, `/recharger`, `/nouvelle`, `/reset`, `/conversations`, `/ouvrir`, `/renommer`, `/supprimer`, `/historique`, `/transcription`, `/quitter` (`/q`), et celles des modules.
- **Saisie** (replxx) : édition de ligne, historique des saisies conservé entre deux lancements, complétion des commandes (Tab), Ctrl+D pour quitter.
- **Apparence** :
  - barre de saisie fixe en bas du terminal (grandit avec la saisie, suit les redimensionnements, sous Windows aussi) ;
  - à la sortie, écran et historique du terminal vidés, puis cadre d'au revoir avec le pingouin (« À bientôt ! », conversation qui reprendra au prochain lancement) ;
  - cadre d'accueil avec le pingouin en pixel art (ASCII sans couleurs) ;
  - styles distincts pour la requête (›), la réponse (◆ Avenoir), les actions (» … ✓ / ✗ en direct), les avertissements (▲) — symboles d'une colonne présents dans les polices de console courantes, Windows compris, les confirmations encadrées, les badges de risque et les erreurs ;
  - réponses en Markdown rendu, découpées aux mots, affichées au fil de l'eau ; toupie « Réflexion… » jusqu'au premier mot ;
  - couleurs désactivées hors terminal et avec `NO_COLOR`.
- Avertissements de démarrage (modules ignorés ou en échec) affichés après l'accueil.

## 12. Configuration

- Fichier `config.toml` (exemple commenté : `config.example.toml`) ; une clé inconnue ou de mauvais type est une erreur qui cite la clé.
- Emplacement : `~/.config/avenoir/config.toml` (ou `$XDG_CONFIG_HOME/avenoir/`), `%APPDATA%\avenoir\config.toml` sous Windows ; un autre fichier en argument ; le dossier courant n'est jamais lu. Sans terminal et sans configuration : erreur qui donne l'emplacement attendu.
- **Réglages scellés** (configuration à l'emplacement standard) :
  - sceau `config.toml.seal` : HMAC-SHA256 (libsodium) du contenu et copie de la version scellée ; clé de 32 octets aléatoires, propre à chaque fichier, dans le trousseau du système (Gestionnaire d'identification sous Windows ; Secret Service par `/usr/bin/secret-tool` sous Linux, secret passé par l'entrée) ; repli : fichier `0600` dans `secrets/` du dossier de données, signalé à chaque lancement ;
  - dernier sceau émis gardé aussi au trousseau : un ancien couple fichier + sceau remis en place est refusé (rejeu) ;
  - vérification à chaque lancement, avant toute lecture : fichier modifié ou supprimé hors d'Avenoir → version scellée remise, lignes changées affichées (`-` / `+`, caractères de contrôle écartés, 40 au plus) ; sceau supprimé, falsifié, illisible ou rejoué → fichier mis de côté (`config.toml.rejected`), création guidée ; trousseau muet → chargé, signalé « non vérifié » ; configuration jamais scellée → scellée (avec un message) ; erreur de lecture → Avenoir ne démarre pas ;
  - écritures d'Avenoir (création guidée, `/configurer`) scellées : sceau, trousseau puis fichier, `0600`, atomiques ;
  - chaque événement noté au journal d'audit (`réglages : …`), même si Avenoir s'arrête ensuite ;
  - fichier donné en argument : non scellé (mode développement), signalé au lancement.
- **Création guidée** au premier lancement (terminal, aucune configuration) :
  - recherche des serveurs compatibles OpenAI de la machine sur les ports par défaut (Ollama 11434, LM Studio 1234, llama.cpp server 8080) ;
  - sur demande seulement, recherche sur le réseau local (sous-réseau privé /24 de la machine, jamais une adresse publique ; en parallèle, quelques secondes, Ctrl+C pour arrêter) ; chaque serveur trouvé n'est retenu que si l'utilisateur déclare que la machine est à lui (`local = true`) ;
  - adresse d'un autre serveur saisie à la main (http(s), sans identifiants), à soi ou tenu pour distant ;
  - Claude Code proposé s'il est dans le PATH, l'API Anthropic (Claude Sonnet 5.5) si `ANTHROPIC_API_KEY` est définie ;
  - liste numérotée : modèles conseillés par le banc d'essai en tête avec leur score, les autres « non évalués », les distants signalés ; choix de plusieurs modèles et du modèle par défaut ; mode « local uniquement » proposé quand il y a des modèles locaux et distants (les distants sont alors écartés) ;
  - noms des modèles des serveurs filtrés (caractères de contrôle écartés, 100 au plus) ;
  - fichier écrit lisible par l'utilisateur seul (dossier `0700`, fichier `0600`), remplacement atomique ; seules les valeurs qui diffèrent des défauts sont écrites hors modèles ;
  - « aucun » (ou aucun modèle trouvé) : « Continuer sans modèle ? » ; oui : configuration sans modèle enregistrée, la voie rapide seule répond et la création ne revient pas d'elle-même ; non : rien n'est écrit, la création sera proposée de nouveau au prochain lancement.
- **`/configurer`** : la même création guidée en cours de session, questions posées dans la barre de saisie ; seuls les modèles et « local uniquement » changent, les autres réglages du fichier en usage sont gardés ; confirmation avant de réécrire un fichier existant (ses commentaires sont perdus) ; Ctrl+C ou Ctrl+D abandonne sans rien changer ; appliquée par `/redemarrer`.
- **`/reglages`** : réglages modifiables avec leur valeur, ce qu'ils font et les valeurs acceptées ; « ▲ » pour ceux qui touchent à la sécurité. **`/reglage <clé> <valeur>`** :
  - clés : `agent.local_only`, `agent.verify_replies`, `agent.fast_path`, `agent.max_steps` (1 à 100), `agent.unload_on_switch`, `permissions.read_only` / `reversible` / `sensitive` (autoriser, confirmer, refuser ; aussi allow, confirm, deny), `web.local_searxng`, `web.searxng_idle_minutes` (1 à 1440), `web.searxng_url` (adresse ou « aucune »), `modules.<id>.enabled` (modules découverts) ; booléens : oui / non ;
  - valeur validée avec les mêmes règles que le fichier ; clé inconnue, prompt système et modèles refusés (les modèles : `/configurer`) ;
  - `permissions.sensitive` ne peut jamais valoir « autoriser » ;
  - **affaiblir** (permissions plus larges, `local_only` ou `verify_replies` coupé, `searxng_url` vers une instance, module installé par l'utilisateur activé) : avertissement, ce que le changement retire, mention de responsabilité, puis saisie exacte de « Je sais ce que je fais » ; sinon rien ne change ;
  - écrit par le sceau, noté au journal d'audit (`réglages : clé : avant → après`, avec l'accord le cas échéant) ; appliqué par `/redemarrer` (invitation affichée).
- Sections : `[models.<nom>]` (un par modèle, dans l'ordre voulu : fournisseur, modèle, adresse, nom de la variable de la clé, `temperature`, `reasoning_effort`, `max_tokens`, commande, délai et `persistent` de Claude Code, `local`) ; l'ancienne section `[llm]` est refusée avec un message qui indique la nouvelle forme ; `[agent]` (`max_steps`, `fast_path`, `system_prompt`, `unload_on_switch`, `local_only`, `verify_replies`), `[web]` (`local_searxng`, `searxng_idle_minutes`, `searxng_url`, `engine`), `[permissions]` (politique par niveau), `[modules.<id>]` (`enabled`, `risk` par outil).
- Dossier de données : `~/.local/share/avenoir` (ou `$XDG_DATA_HOME`), `%LOCALAPPDATA%\avenoir` sous Windows.

## 13. Journaux

- Logs sur stderr, niveau `warn` par défaut, réglable avec `AVENOIR_LOG` (`debug`, `info`…).
- **Journal d'audit** (logger `audit`) : chaque exécution, refus, échec et annulation d'outil ; toujours écrit dans `logs/audit.log` du dossier de données (dossier `0700`, chaque entrée aussitôt sur le disque, horodatée à la milliseconde ; rotation : 5 fichiers de 5 Mo au plus), quel que soit `AVENOIR_LOG` ; affiché à l'écran avec `AVENOIR_LOG=info`.
- stderr de chaque module dans son propre fichier de log.

## 14. Banc d'essai avec de vrais modèles

- `avenoir-evals` (`--llm nom` : modèle de la configuration, le premier par défaut) : passe les **requêtes de référence** (`evals/requetes.toml`, 26 cas : simples, multi-outils, suite de conversation, ambiguës, hors de portée, erreur d'outil, injections, arguments piégés, refus et accord, données inventées) sur un vrai modèle.
- Vrai agent et vrai fournisseur, mais **doublures d'outils** (mêmes nom, description, schéma et risque ; résultats ou erreurs préparés) : rien n'est lancé ni modifié ; voie rapide coupée.
- Vérifications par cas : appels attendus (arguments partiels, sans casse), outils interdits, textes interdits dans les arguments, nombre d'appels maximal, mots attendus ou interdits dans la réponse, données interdites dans le texte écrit avec un appel d'outil (avant son résultat), politique et réponse aux confirmations propres au cas.
- Options : `--config`, `--model`, `--reasoning`, `--cases`, `--only`, `--repeat`, `--prompt`, `--report`, `--modules`.
- Rapport Markdown (réussite par catégorie, durée, tokens, coût, détail des échecs) dans `evals/resultats/` ; synthèse dans `evals/resultats/COMPARAISON.md`.

## 15. Développement, qualité et publication

- C++23, CMake + vcpkg (préréglages `debug`, `release`, `asan`) ; options `AVENOIR_BUILD_TESTS`, `AVENOIR_SANITIZE`, `AVENOIR_MODULE_METEO`, `AVENOIR_EVALS`.
- Scripts `scripts/build.sh` et `scripts\build.ps1` (mêmes options) : `-c` build propre, `-r` lancer Avenoir, `-n` sans tests, `-f` reformater, `-k` vérifications d'avant commit ; sous Windows, environnement Visual Studio chargé automatiquement.
- Avertissements traités comme des erreurs, `clang-format`, `clang-tidy`, AddressSanitizer + UndefinedBehaviorSanitizer ; plus de 130 tests Catch2, dont un test de bout en bout de la CLI.
- **CI** à chaque push : Linux GCC 14, Clang 20, ASan + UBSan, Windows MSVC, formatage, shellcheck et PSScriptAnalyzer.
- **Releases** automatiques sur tag `vX.Y.Z` : contrôle de la version, build et tests Linux / Windows, binaires publiés (avec les modules fournis).
- Images du README générées par des scripts (`docs/readme/`).
