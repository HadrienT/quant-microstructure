# quant-microstructure — notes pour Claude Code

Étude de la **microstructure des marchés** en **C++20** : carnet d'ordres et
moteur d'appariement, simulateurs de flux, impact de marché, exécution
optimale, tenue de marché, backtest au niveau du carnet. Exposé à Python par
un wheel pybind11 `quantmicrostructure`, pour l'analyse et les graphiques.

C'est un **chantier d'apprentissage** déclaré : le mainteneur a étudié les
dérivés puis les xVA (`quant-modeling`), puis le backtest (`quant-backtest`),
et ouvre ici le côté carnet d'ordres. Le dépôt sert autant à comprendre qu'à
produire : le compagnon de lecture compte autant que le code.

**Ce dépôt n'est pas un système de trading.** Aucune connexion à une place
pour passer un ordre, aucune clé d'API de trading, nulle part. Ne jamais en
ajouter.

## État du dépôt

**Spécification seule, aucun code.** Le dépôt a été créé le 11 octobre 2026.
Le premier lot à faire est **M0** (socle de build). Mettre cette section à
jour à chaque lot livré.

## Par où commencer

| Fichier | Rôle |
|---|---|
| [`docs/companion.md`](docs/companion.md) | Compagnon de lecture **et** spécification : la théorie (§1 à §10), l'architecture (§11), ce que le dépôt n'est pas (§12), les lots (§13), les tests de propriété (§14), l'état des données (§15), la bibliographie (§17) |
| [`blueprint/README.md`](blueprint/README.md) | Les lots et leur graphe de dépendances : le consulter avant de démarrer un lot |
| [`blueprint/decisions.md`](blueprint/decisions.md) | Décisions d'architecture, avec les alternatives écartées, et les décisions **encore ouvertes** |

## Relation avec les autres dépôts

| Dépôt | Rôle vis-à-vis d'ici |
|---|---|
| `~/data-ingest` | **Capture des flux de carnet** (lot M1). Ce dépôt lit ce qu'il enregistre ; il n'ouvre lui-même aucune connexion réseau |
| `~/quant-backtest` | Moteur de backtest sur données quotidiennes. Aucune dépendance aujourd'hui. Sert de **modèle pour la chaîne de build** (CMake, presets, vcpkg, scripts, CI). Plus tard, consommateur possible des modèles de coût calibrés ici |
| `~/quant-modeling` | Lib de pricing et site web. Aucune dépendance, dans aucun sens |
| `~/quant-platform` | Kafka, audit, observabilité. Ce dépôt n'y émet rien |

Règle de frontière : **ne jamais éditer un dépôt voisin depuis une session
ouverte ici.** Un changement qui touche deux dépôts = une issue dans chacun,
référencées l'une par l'autre.

## Principes d'architecture non négociables

1. **Prix, quantités et temps sont des entiers** : ticks, lots, nanosecondes.
   Aucun `double` dans `core/`, `book/` ni `matching/`.
2. **Tout est un flux d'événements, dans un seul format** : un enregistrement
   de marché, la sortie d'un simulateur et la sortie du moteur d'appariement
   se lisent de la même façon.
3. **La stratégie est séparée du lieu d'exécution** : la même tourne sur un
   rejeu et dans un simulateur.
4. **Aucun réseau ici.** La capture est dans `data-ingest` ; ici on lit des
   fichiers d'événements.
5. **Déterminisme** : même flux et même graine, mêmes sorties au bit près.
6. **Pas de futur** : dans le rejeu, rien de ce qui arrive après $t$ ne peut
   changer une décision ou une exécution jusqu'à $t$.
7. **Un backtest de carnet déclare ses hypothèses** : latence, position dans
   la file, impact de ses propres ordres, frais. Le résultat est montré avec
   sa sensibilité à la latence et à l'hypothèse de file (§10 du compagnon).
8. **La performance se mesure.** Aucune affirmation de vitesse sans le
   benchmark qui la porte, tenu à jour dans le dépôt.
9. **Rester près des sources** : noms, formules et conventions de signe sont
   ceux des références du §17, d'abord Bouchaud et al. (2018) et Cartea,
   Jaimungal & Penalva (2015). La formule est **revérifiée dans l'article**
   avant d'être codée, et l'équation citée dans le commentaire.
10. **Un modèle déclare son régime** (gros tick ou petit tick) et ce qu'il
    suppose du flux.
11. **Chaque chiffre montré est défendable** : il porte sa convention, son
    incertitude, et la limite des données (un marché de cryptomonnaies n'est
    pas un marché actions ; ce serveur n'est pas en colocation).

## Commandes

Aucune pour l'instant : les fichiers de build arrivent avec le lot M0, qui
remplira ce tableau en reprenant la chaîne de `~/quant-backtest`
(`scripts/make.sh`, `scripts/make_asan.sh`, `scripts/build_wheel.sh`).

## Suivi du travail — GitHub Issues

`gh` est authentifié (compte `HadrienT`).

- **Au démarrage d'une session** : `gh issue list --state open`.
- **Issue traitée** → `gh issue close <n> --comment "fait dans <sha>"`.
- **Jamais** de fichier markdown de passation entre sessions. Une tâche qui
  survit à la session est une issue.
- La conception vit dans `docs/` et `blueprint/` ; l'issue y renvoie, elle ne
  la remplace pas.

## Conventions

- **Conversation avec le mainteneur : en français.** Code, identifiants,
  commentaires, messages de commit, `README.md` : en anglais. `docs/` et
  `blueprint/` sont en français.
- **Commits : passer par une branche, jamais directement sur `main`** (le
  commit initial de documentation est la seule exception). Terminer les
  messages par la ligne `Co-Authored-By` de la session.
- **C++** : `clang-format --style=file`, **version 18** ; le `.clang-format`
  est celui de `quant-modeling`. **Python** : `black`. Le hook pre-commit est à
  installer sur chaque clone : `pre-commit install -t pre-commit`.
- **Propriété mémoire** : préférer `unique_ptr` et des références non
  propriétaires.
- **Tout module arrive avec son test**, et de préférence un test de
  *propriété* (conservation, priorité prix-temps, aller-retour du flux,
  formule fermée retrouvée sur un modèle simulé) plutôt qu'une égalité
  numérique. La liste est au §14 du compagnon.
- **Gestion des dépendances déclarative** : tout passe par `vcpkg.json` et les
  presets CMake ; un clone neuf se construit sans étape manuelle.
- **Le serveur est partagé** avec d'autres projets : borner le nombre de
  threads des tests et des benchmarks, et ne pas lancer un benchmark de
  latence pendant qu'un autre build tourne (le chiffre ne vaudrait rien).
