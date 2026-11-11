# Fonctionnement du code d'Avenoir

Ce document décrit **le code tel qu'il existe aujourd'hui** : comment il démarre, le trajet d'une requête et le rôle de chaque bibliothèque. La vision et l'architecture cible (démon, interface web, voix) sont dans [`PROJECT.md`](PROJECT.md), l'avancement dans [`ROADMAP.md`](ROADMAP.md). Ce document se met à jour avec le code, comme eux.

Aujourd'hui, Avenoir est **une CLI** (`apps/cli`, exécutable `avenoir`) qui contient l'agent. Le démon et les interfaces graphiques viendront aux jalons 3 et 6. Ils réutiliseront les mêmes bibliothèques : la CLI ne fait qu'assembler.

## 1. Les bibliothèques

```
                    apps/cli (avenoir)          evals (avenoir-evals)
                          │                             │
     ┌──────────┬─────────┼───────────┬──────────┐      │
     ▼          ▼         ▼           ▼          ▼      │
  terminal   storage   modules      tools       llm ◄───┤
     │          │         │           │          │      │
     │          └─────────┴─────┬─────┴──────────┘      │
     │                          ▼                       │
     │                        core ◄────────────────────┘
     ▼
 (rien d'Avenoir)            modules/meteo : programme séparé,
                             parle à `modules` par JSON-RPC
```

| Bibliothèque | Rôle | Fichiers clés |
|---|---|---|
| `libs/core` | Le cœur, sans OS ni réseau : types des messages, interface `Tool`, `ToolRegistry`, permissions, voie rapide, **boucle agent**, lecture de `config.toml`, erreurs. Ne dépend d'aucune autre bibliothèque interne. | `agent.cpp`, `permission.cpp`, `router.cpp`, `config.cpp` |
| `libs/llm` | Les fournisseurs LLM : traduisent l'historique d'Avenoir vers chaque API, et la réponse en retour. Le changement de modèle à chaud (`ModelSwitcher`, outil `switch_llm`). | `openai_compatible.cpp`, `anthropic.cpp`, `claude_code.cpp`, `factory.cpp`, `models.cpp` |
| `libs/tools` | Les outils natifs (`current_time`, `system_info`, `launch_app`, `list_apps`, `open_url`, et le contrôle du système : `set_volume`, `media_control`, `set_brightness`, `set_dark_mode`, `lock_screen`, `notify`, `power`) et leurs règles de voie rapide (`register_system_rules`) ; la recherche web et la lecture de pages (`web_search`, `read_webpage`, `set_search_engine`, `register_web_rules`) ; l'index des applications installées (`AppIndex`, logique portable testée) ; le code propre à chaque OS est dans `src/platform/` (Linux : programmes du système par `reproc` ; Windows : Core Audio, WinRT, WMI, registre). | `register.cpp`, `apps.cpp`, `app_index.cpp`, `system_control.cpp`, `web.cpp`, `searxng.cpp`, `validation.cpp` |
| `libs/modules` | L'hôte de modules : découverte, validation des manifestes, lancement des processus, JSON-RPC, MCP, risque des outils. | `discovery.cpp`, `manifest.cpp`, `host.cpp`, `rpc_client.cpp`, `module.cpp` |
| `libs/storage` | Conversations en SQLite (`avenoir.db`), choix de risque des outils de modules, dossier de données. | `store.cpp`, `message_json.cpp`, `paths.cpp` |
| `libs/terminal` | Rendu dans le terminal : couleurs, largeur des caractères, cadres, Markdown. Ne connaît rien d'Avenoir. | `style.cpp`, `markdown.cpp` |
| `apps/cli` | Assemble le tout : saisie (replxx), affichage, commandes `/…`, thread de requête, Ctrl+C. | `main.cpp`, `display.cpp`, `prompt.cpp`, `conversations.cpp`, `modules.cpp` |
| `modules/meteo` | Module fourni : programme à part, lancé par l'hôte. | `src/main.cpp`, `src/weather.cpp` |
| `evals` | Banc d'essai avec de vrais modèles (§ 9). | `src/runner.cpp`, `requetes.toml` |

Les interfaces entre ces couches sont des **classes abstraites du cœur** : `LlmProvider` (fournisseur), `Tool` (outil), `ConfirmationHandler` (demander l'accord). Le cœur ne voit jamais une API HTTP, un processus ou un terminal.

## 2. Démarrage de la CLI

Tout se passe dans `run()` (`apps/cli/src/main.cpp`), dans cet ordre :

1. **Logs** : loggers `avenoir` et `audit` sur stderr, niveau `warn` (ou `AVENOIR_LOG`). L'audit est coupé par défaut.
2. **Console** : UTF-8 et séquences VT sous Windows, gestionnaire de Ctrl+C (`console.cpp`), couleurs selon le terminal et `NO_COLOR`.
3. **Configuration** : `core::load_config` lit `config.toml`. Une clé inconnue ou de mauvais type est une erreur qui cite la clé.
4. **Fournisseur LLM** : `llm::ModelSwitcher::start` choisit le modèle de départ (le dernier utilisé, retenu dans la base sous `llm.active`, sinon le premier `[models.<nom>]`) et `llm::make_provider` construit son fournisseur. Une clé d'API est lue dans la variable d'environnement dont la config donne le *nom*. Sans modèle, l'agent n'a pas de fournisseur : seule la voie rapide répond.
5. **Options de l'agent** : politique de permissions, prompt système (`kDefaultSystemPrompt` sauf remplacement), nombre d'étapes, voie rapide (`FastRouter::with_defaults`).
6. **Outils natifs** : `tools::register_native_tools` ajoute ceux que l'OS permet.
7. **Données** : dossier de données (`~/.local/share/avenoir`, ou `%LOCALAPPDATA%\avenoir`), base `avenoir.db`, historique des saisies. Une base illisible n'empêche pas de démarrer : les conversations ne sont alors pas conservées.
8. **Modules** : `modules::discover` lit les manifestes (modules fournis à côté de l'exécutable, puis ceux de l'utilisateur), puis `ModuleHost::load` lance les modules activés. Leurs outils rejoignent le registre, leurs règles la voie rapide, leurs commandes la CLI. Un module en échec devient un avertissement, pas une erreur.
9. **Agent** : `core::Agent` reçoit le fournisseur, le registre d'outils, la confirmation au terminal (`CliConfirmation`) et les options. Un observateur de l'historique enregistre chaque message en base et affiche les actions en direct.
10. **Affichage** : barre de saisie fixe, accueil, avertissements de démarrage, reprise de la dernière conversation. Puis la boucle de saisie.

## 3. Trajet d'une requête

Exemple : « quel temps fait-il à Brest ? ».

```
saisie (Prompt, replxx)
  │  ligne commençant par « / » → run_command (commandes d'Avenoir ou de modules)
  ▼
run_request : thread de travail (std::jthread) ; le thread principal anime la
  │           toupie et transforme Ctrl+C en request_stop()
  ▼
Agent::handle(texte, stop_token)
  │  1. nouveau journal d'annulation pour la requête
  │  2. voie rapide : FastRouter::route → règle trouvée et outil existant ?
  │        oui → execute(appel) → réponse = résultat de l'outil (pas de LLM)
  │  3. sinon, boucle agent (max_steps étapes) :
  │        LlmProvider::complete(prompt système, historique, outils)
  │          ├─ réponse sans appel d'outil → vérifications (add_reply_check) :
  │          │     action annoncée mais non faite → réponse écartée, relance une
  │          │     fois avec un rappel ; sinon (ou à la 2e fois, avec un
  │          │     avertissement) → réponse finale
  │          └─ appels d'outils → execute(chacun) → résultats dans l'historique → étape suivante
  ▼
display().assistant(réponse)   (Markdown rendu, découpé aux mots)
```

**`Agent::execute`** est le seul chemin vers un outil, quelle que soit l'origine de l'appel (LLM, voie rapide ou commande de module) :

1. l'outil est cherché dans le registre (un nom inconnu revient au LLM comme une erreur) ;
2. `PermissionPolicy::for_risk(spec.risk)` décide : `Allow`, `Confirm` (demander via le `ConfirmationHandler`) ou `Deny`. `sensitive` n'est jamais `Allow`, quoi que dise la configuration ;
3. un refus revient au LLM comme une erreur (« L'utilisateur a refusé cette action. ») et le prompt système lui demande de ne pas contourner ;
4. l'audit note chaque exécution, refus et échec ;
5. le résultat est tronqué à 20 000 caractères. Si l'action a un effet de bord, elle entre dans le journal d'annulation, avec son `UndoAction` éventuelle.

Les **messages** (`core::Message`) sont indépendants du fournisseur : `UserMessage`, `AssistantMessage` (texte, appels d'outils, consommation) et `ToolResultMessage`. Chaque fournisseur les convertit vers son format à chaque appel ; l'historique complet est renvoyé à chaque étape.

## 4. Fournisseurs LLM (`libs/llm`)

| Fournisseur | Transport | Particularités |
|---|---|---|
| `openai_compatible` | HTTP (`cpr`), `/chat/completions` | Ollama, llama.cpp, LM Studio, vLLM, OpenAI, Mistral… ; les arguments d'outils arrivent en chaîne JSON ou en objet (Ollama). |
| `anthropic` | HTTP, API Messages | Les messages consécutifs de même rôle sont fusionnés (exigence de l'API). |
| `claude_code` | Processus `claude -p` (`reproc`, sans shell) | Les outils de Claude Code sont désactivés. Les outils d'Avenoir et la conversation sont décrits dans le prompt, et la réponse est imposée par `--json-schema` (`text`, `tool_calls`). Le prompt passe par stdin. Les clés d'API sont retirées de l'environnement. |

Les trois respectent le `stop_token` (requête HTTP interrompue, processus arrêté) et remontent la consommation (`Usage`).

**Changement de modèle** (`models.cpp`) : `models_from` lit les `[models.<nom>]` (dans l'ordre du fichier : toml++ les range par ordre alphabétique, la ligne de chaque section rétablit l'ordre) ; `ModelSwitcher` crée le fournisseur choisi (`make_provider`) et le donne à l'agent (`Agent::set_llm`), qui garde l'historique. Un modèle est local si son adresse est celle de la machine (ou `local = true`) ; sinon le passage se confirme par le `ConfirmationHandler` de la CLI, avec le risque `sensitive`. L'outil `switch_llm` et ses règles de voie rapide ne sont enregistrés que s'il y a au moins deux modèles ; son annulation revient au modèle précédent si le modèle actif n'a pas changé entre-temps. Chaque changement réussi est retenu dans la base (`Store::set_state`), pour le lancement suivant. `set_reasoning` change l'effort de raisonnement d'un modèle ; pour le modèle actif, un nouveau fournisseur est créé avec la nouvelle configuration et donné à l'agent, le serveur gardant le modèle en mémoire. En changeant de modèle, l'ancien est mis en attente de déchargement s'il est local, sur le même serveur (`server_root`) et différent ; revenir sur lui l'en retire. `is_loaded` interroge `/api/ps` d'Ollama (500 ms au plus) : la CLI s'en sert pour la toupie « Chargement du modèle… » (interrogée toutes les 500 ms pendant la requête), pour `LoadWatch`, qui compare l'état des modèles locaux toutes les 3 s pendant la saisie (`Prompt::Hooks::poll`, dans un autre thread) et fait montrer les changements par la boucle de replxx (`Hooks::notify`, touche simulée, `Display::notify`), et pour `/memoire` (`status`). `set_residency` charge ou décharge un modèle local par l'API d'Ollama (`POST /api/generate`, `keep_alive = 0` pour décharger) ; décharger le modèle actif est mis en attente et fait par `finish_request`, que la CLI appelle après chaque requête et chaque commande. `make_model_tools` et `register_model_rules` donnent les outils de modèle et leurs règles selon les modèles configurés (même liste pour la CLI et le banc) ; les règles par nom seul sont construites avec les noms de la configuration. `attach` et chaque changement donnent à l'agent le nom du modèle actif (`Agent::set_model_identity`, ajouté au prompt système). `make_model_claim_check` est une vérification de réponse (`Agent::add_reply_check`) : un autre modèle nommé avec un verbe de changement (« je bascule », « je passe ») sans appel de `switch_llm`, ou de chargement sans `load_llm` / `unload_llm`, est une action annoncée mais non faite. Pour les autres actions, `core::make_claim_check` (`claims.cpp`) applique des règles `ClaimRule` (outils qui font l'action, phrases qui l'annoncent, réponse normalisée par `normalize_reply`) ; celles des outils natifs viennent de `tools::action_claim_rules` (`libs/tools/src/claims.cpp`), filtrées sur les outils présents. La CLI et le banc ajoutent ces vérifications si `[agent] verify_replies` est vrai.

## 5. Outils natifs (`libs/tools`)

Chaque outil implémente `core::Tool` : `spec()` (nom, description pour le LLM, JSON Schema, risque) et `call(arguments, stop)`. Les arguments viennent du LLM : ils sont lus dans une structure typée puis **validés** (`validation.cpp` : noms d'applications sans métacaractère, URL `http(s)` seulement). Le code propre à l'OS est derrière `src/platform.hpp`, avec une implémentation Linux, une Windows et un repli « non supporté ». Un outil non pris en charge n'est pas enregistré.

**Recherche web** (`web.cpp`, `web.hpp`) : `SearchEngines` tient le moteur actif (DuckDuckGo, ou SearXNG si `[web] searxng_url`), retenu dans la base par la CLI (`web.engine`). `web_search` construit l'adresse du moteur, la télécharge par `HttpGet` (par défaut `default_http_get`, avec cpr) et analyse la réponse (`parse_duckduckgo` : liens `result__a` et extraits, redirections `uddg` décodées, publicités et défis anti-robots repérés ; `parse_searxng` : JSON). `read_webpage` valide l'adresse (`validate_url`, `url_host`, `is_local_host`), télécharge la page, en tire le texte (`html_to_text`), puis la fait lire par le `PageReader` : la CLI y branche `Agent::ask_aside`, une demande au modèle actif **sans outils ni historique** ; seule la réponse revient à l'agent. `default_http_get` borne la requête (15 s, 3 Mo, 5 redirections, protocoles `http(s)`) et installe `CURLOPT_PREREQFUNCTION`, qui refuse toute connexion vers une adresse IP non publique (`is_public_ip`), sauf pour l'instance SearXNG de la configuration. Les tests passent un faux `HttpGet` et un faux lecteur : aucun accès réseau.

**SearXNG local** (`searxng.cpp`) : `SearxngHost::ensure_started` trouve podman ou docker, écrit `settings.yml` (une fois), télécharge l'image si `image inspect` échoue, reprend une instance `avenoir-searxng` qui répond (`port`, `/healthz`) ou la remplace, puis lance `run --detach --rm --publish 127.0.0.1::8080 --volume settings.yml:…:ro,Z` et attend `/healthz` (30 s au plus). Un fil (`std::jthread`) appelle `stop_if_idle` toutes les 15 s ; le destructeur arrête l'instance (la CLI déclare l'hôte avant les moteurs et l'agent). Les commandes passent par `platform::run_command` : sous Linux, `posix_spawnp` puis `waitpid` (la fin du programme lui-même : `conmon` hérite des descripteurs et garderait ouvert un tube), sortie dans un fichier temporaire ; sous Windows, reproc. Les tests passent un faux `CommandRunner` : aucun conteneur lancé.

## 6. Hôte de modules (`libs/modules`)

Un module est un **programme séparé** décrit par un `module.toml` (PROJECT.md § 2.6). L'hôte ne lui fait jamais entièrement confiance (directive 15) :

1. **Découverte** (`discovery.cpp`, `manifest.cpp`) : manifeste validé strictement (clé inconnue refusée, programme sans `..` ni chemin absolu, variables d'environnement listées, règles et commandes limitées aux outils du module). Un module de l'utilisateur est désactivé tant qu'il ne l'a pas activé.
2. **Lancement** (`host.cpp`, `rpc_client.cpp`) : API C de `reproc`, tableau d'arguments, sans shell. L'environnement est minimal (variables de base, proxy, celles du manifeste) ; le dossier de données est `AVENOIR_MODULE_DATA` ; stderr va dans `logs/modules/<id>.log`.
3. **Dialogue** : JSON-RPC 2.0, un message par ligne sur stdin/stdout ; un thread lit les réponses et les écritures sont protégées par un mutex. Les messages sont limités à 8 Mio et chaque requête a un délai maximal. Poignée de main `initialize`, puis `tools/list` (paginé, 128 outils au plus, schémas et descriptions bornés).
4. **Outils** (`module.cpp`) : chaque outil devient un `ModuleTool` nommé `id__outil`. Son risque est celui du manifeste (`sensitive` s'il est absent), ajusté par `/risque` et `config.toml` (`resolve_risk`). Le texte renvoyé est nettoyé : caractères de contrôle, C1 et marques bidirectionnelles sont retirés.
5. **Appel** : `tools/call`. Ctrl+C envoie `notifications/cancelled`. Si le module propose une annulation (`_meta["avenoir/undo"]`), elle devient une `UndoAction` qui appelle `avenoir/undo`.
6. **Panne** : un module qui plante, ne répond pas ou triche renvoie une erreur à l'agent ; l'assistant continue.

## 7. Annulation et Ctrl+C

- Le gestionnaire de signal (`console.cpp`) ne fait que lever un indicateur. Le thread principal le lit dans `run_request` et demande l'arrêt au thread de travail (`std::stop_token`). Ce jeton est vérifié entre deux étapes de l'agent et passé au fournisseur LLM et aux outils. Sur la voie rapide et pour les commandes (`Agent::run_direct`), il est relu après l'outil : interrompue, l'exécution renvoie `ErrorCode::Cancelled` plutôt que l'échec rendu par l'outil.
- Chaque requête a un **journal d'annulation** (`RequestJournal`) qui liste les actions à effet de bord et leur `UndoAction` éventuelle. Après une interruption, ou avec `/annuler`, la CLI propose de défaire les actions en ordre inverse (`Agent::undo_last`). Chaque annulation vérifie que l'état n'a pas changé depuis.

## 8. Conversations (`libs/storage`)

`storage::Store` ouvre `avenoir.db` (SQLite, WAL, schéma versionné). L'observateur de l'historique de l'agent écrit **chaque message dès qu'il est ajouté** (`Conversations::record`), avec ses appels d'outils et sa consommation, en JSON (`message_json.cpp`). Au lancement, la dernière conversation est rechargée dans l'agent (`Agent::load_history`). La même base garde les choix de risque faits avec `/risque` (table `tool_risks`) et l'état repris d'un lancement à l'autre, comme le dernier modèle utilisé (table `app_state`).

## 9. Tests et banc d'essai

- **Tests unitaires** (Catch2, `ctest`) : à côté de chaque bibliothèque (`libs/*/tests`). La boucle agent y est testée avec un LLM scripté. Aucun test ne lance de vraie application ni n'appelle un vrai modèle. L'hôte de modules est testé avec un module de test (`fixture_module.cpp`) qui plante, traîne ou renvoie des sorties piégées. `apps/cli/tests/smoke.cmake` lance la vraie CLI de bout en bout, par la voie rapide.
- **Banc d'essai** (`evals/`, exécutable `avenoir-evals`) : passe les requêtes de référence (`evals/requetes.toml`) sur un **vrai modèle**, avec le vrai agent mais des **doublures** d'outils. Les doublures ont les mêmes nom, description, schéma et risque que les vrais outils, mais rien n'est lancé et elles renvoient des résultats préparés (dont des injections). La voie rapide est coupée. Les outils de modèle (`switch_llm`, `load_llm`…) y sont décrits pour une liste fixe (gemma, qwen, ministral, phi, claude), le modèle essayé étant le modèle actif ; `{modele}` dans `requetes.toml` donne son nom. Le rapport Markdown va dans `evals/resultats/`, et la synthèse des comparaisons dans `evals/resultats/COMPARAISON.md`. Le banc d'essai n'est lancé ni par ctest ni par la CI, mais sa logique est testée (`evals/tests`).

```sh
./build/release/evals/avenoir-evals --llm claude --repeat 2       # un modèle de config.toml (le premier par défaut)
./build/release/evals/avenoir-evals --config ollama.toml --only heure,injection-meteo
./build/release/evals/avenoir-evals --prompt essai-prompt.txt     # essayer un autre prompt système
```

## 10. Où ajouter quoi

| Je veux… | Où |
|---|---|
| un outil natif | `libs/tools/src/` + `register.cpp` (PROJECT.md § 4.4), ses phrases d'annonce dans `libs/tools/src/claims.cpp` s'il agit, puis un cas dans `evals/requetes.toml` |
| un fournisseur LLM | `libs/llm` (implémenter `core::LlmProvider`) + `factory.cpp` + `core::LlmConfig` |
| une commande de la CLI | `run_command` et `builtin_commands` dans `apps/cli/src/main.cpp`, aide dans `print_help` |
| une règle de voie rapide | `FastRouter::with_defaults` (`libs/core/src/router.cpp`), ou dans le manifeste d'un module |
| un module fourni | `modules/<id>/` avec son `module.toml` et son `CMakeLists.txt` (sortie dans `AVENOIR_MODULES_OUTPUT_DIR`), option CMake dans le `CMakeLists.txt` racine |
| un réglage | `core::AppConfig` + `config.cpp` + `config.example.toml` |
