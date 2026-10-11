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

**Tous les lots de ce dépôt (M0, M2 à M8) sont livrés ; aucune donnée réelle.**
Il reste la capture (M1, dans `data-ingest`), sans laquelle rien n'est mesuré
ni calibré sur un vrai marché. Le dépôt a été créé le 11 octobre 2026. Mettre cette
section à jour à chaque lot livré.

| Lot | État |
|---|---|
| M0 — Socle | livré : chaîne de build, tests, benchmark, wheel, CI |
| M2 — Carnet et moteur | livré : `core/` (types entiers, format d'événement), `feed/` (fichier d'événements, détection de trous), `book/` (carnet L3, carnet L2, agrégation L3 vers L2), `matching/` (moteur prix-temps) |
| M4 — Simulateurs | livré : `sim/` (générateur aléatoire, intelligence zéro, Hawkes univarié avec maximum de vraisemblance et changement de temps, queue-reactive, Roll, Kyle, Glosten-Milgrom). Écarts aux articles et formules non relues : ADR-009 |
| M3 — Mesures | livré : `measures/` (bande des cotations et transactions, fourchettes cotée, effective, réalisée, Roll, déséquilibre, profondeur, variance réalisée, graphique de signature, deux échelles, corrélation des signes, fonction de réponse, signe des transactions) et **liaisons Python** de tout ce qui précède. Ce qui n'est pas fait : ADR-010 |
| M5 — Backtest carnet | livré : `replay/` (stratégie, passerelle, latence, file, frais ; `runReplay` sur un flux L2 ou L3, `runSimulation` dans l'intelligence zéro). Hypothèses : ADR-011. Exposé à Python à travers l'exécution (`execute_schedule`) et la tenue de marché (`market_maker_replay`, `market_maker_simulation`) ; une stratégie reste une classe C++ |
| M6 — Impact | livré : `impact/` (propagateur, lambda de Kyle, impact d'un métaordre en simulateur, coût d'un aller-retour) et `sim/order_splitting` (signes à mémoire longue). **La loi en racine carrée n'est pas reproduite** par l'intelligence zéro : ADR-012 |
| M7 — Exécution | livré : `execution/` (Almgren-Chriss discret, TWAP, VWAP, POV, exécuteur d'échéancier dans le backtest, déficit d'exécution). Limites : ADR-013 |
| M8 — Tenue de marché | livré : `mm/` (Avellaneda-Stoikov, Guéant-Lehalle-Fernandez-Tapia exact et approché, teneur de marché pour le backtest, décomposition du P&L, markouts). Ce que vaut le résultat : ADR-014 |
| M1 — Capture | **à faire, dans `data-ingest`** (ADR-004 toujours ouverte). C'est le lot qui débloque tout le reste : mesures, calibrage, backtest sur données |

Trois constats à ne pas oublier, consignés dans les ADR et les issues :
la loi en racine carrée n'est pas reproduite par l'intelligence zéro (#15,
ADR-012) ; plusieurs formules ont été codées d'après une dérivation, faute
d'accès à l'article (#16) ; le P&L d'un teneur de marché dans l'intelligence
zéro ne dit rien d'un marché réel (ADR-014).

Où lire le code, dans l'ordre : `core/event.hpp` (le format, ADR-007),
`book/order_book.hpp` (la structure, ADR-008), `matching/matching_engine.hpp`,
puis `tests/testBookProperties.cpp` (les propriétés du §14). Pour les
simulateurs : `sim/toy_models.hpp` (les trois modèles du §4, avec leur
dérivation), `sim/zero_intelligence.hpp`, `sim/hawkes.hpp`,
`sim/queue_reactive.hpp`.

Pour les mesures : `measures/tape.hpp` d'abord (ADR-010), puis un fichier par
famille. Côté Python, tout est dans `bindings/python/module.cpp` ; les tests
de `tests/python/` montrent comment enchaîner simulateur, bande et mesure.

Pour le backtest : `replay/backtest.hpp`, dont les commentaires portent les
hypothèses (ADR-011), puis `tests/testBacktest.cpp`, qui les déroule une par
une sur des marchés écrits à la main.

`sim/`, `measures/`, `replay/`, `impact/`, `execution/` et `mm/` utilisent
des `double` (taux, temps en secondes, valeurs de Kyle) :
l'interdiction ne vaut que pour `core/`, `book/` et `matching/`. Tout
simulateur prend une graine et passe par `sim/rng.hpp`, jamais par les lois de
`<random>`.

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

`VCPKG_ROOT` doit pointer sur un clone de vcpkg ; les dépendances sont
installées par le manifeste [`vcpkg.json`](vcpkg.json) à la configuration.

| Commande | Effet |
|---|---|
| `scripts/make.sh` | Configure (preset `default`), construit dans `build/`, lance tous les tests |
| `scripts/make_asan.sh` | Idem avec ASan et UBSan, dans `build-asan/` |
| `cmake --preset default && cmake --build build` | Construire sans lancer les tests |
| `ctest --test-dir build --output-on-failure` | Tous les tests |
| `ctest --test-dir build -R BuildInfo` | Les tests dont le nom contient un motif |
| `scripts/bench.sh` | Construit (preset `bench`, dans `build-bench/`) et lance le benchmark ; les arguments vont à google-benchmark, par exemple `--benchmark_filter=...` |
| `scripts/build_wheel.sh` | Wheel `quantmicrostructure` dans `dist/` (sans vcpkg : la lib n'a aucune dépendance tierce) |
| `pytest` | Tests Python, sur le wheel installé dans l'environnement courant (le reconstruire et le réinstaller après un changement de C++ : `uv pip install --force-reinstall dist/*.whl`) |
| `pre-commit run --all-files` | `clang-format` 18 et `black` |

Les scripts bornent le build et les tests à `NPROC` processus (16 par défaut,
8 sous ASan) : `NPROC=4 scripts/make.sh`.

Cibles CMake : `quantMicrostructure` (la lib), `quantMicrostructure_tests`,
`quantMicrostructure_bench`, et `quantmicrostructure` (le module Python).
Options : `QM_BUILD_TESTS`, `QM_BUILD_BENCHMARKS`, `QM_BUILD_PYTHON`. Un
fichier source ou un test nouveau s'ajoute à la main dans la liste de sa cible.

Les tests et le benchmark tirent leurs flux d'ordres de
[`tests/support/random_flow.hpp`](tests/support/random_flow.hpp) : un flux
aléatoire sans modèle, fait pour éprouver le carnet, pas pour ressembler à un
marché. Les tests statistiques (moyennes, test de Kolmogorov-Smirnov) passent
par [`tests/support/statistics.hpp`](tests/support/statistics.hpp), à graine
fixe et avec des tolérances de quatre à cinq écarts-types.

Les résultats du benchmark sont dans
[`benchmarks/RESULTS.md`](benchmarks/RESULTS.md), avec leurs limites. **Le
relancer et mettre ce fichier à jour à chaque changement de `book/` ou de
`matching/`** (`scripts/bench.sh --benchmark_repetitions=5`), après avoir
vérifié qu'aucun autre build ne tourne.

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
