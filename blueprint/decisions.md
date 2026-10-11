# Décisions d'architecture

Chaque décision : ce qui est retenu, pourquoi, et **ce qui est écarté**. Une
décision qui change devient une nouvelle entrée qui la remplace ; on ne réécrit
pas l'historique.

Les entrées marquées **OUVERTE** sont des propositions : elles attendent une
mesure ou un choix du mainteneur, et ne doivent pas être traitées comme
acquises.

---

## ADR-001 — Un dépôt à part

**Décision.** L'étude de la microstructure vit dans son propre dépôt, sans
dépendance sur `quant-modeling` ni sur `quant-backtest`.

**Pourquoi.** Demande du mainteneur (11 octobre 2026). Et rien n'est commun :
les données sont événement par événement, les prix sont des entiers, les
modèles sont des processus ponctuels. Le moteur de `quant-backtest` raisonne
en clôtures quotidiennes et en poids ; le plier à un carnet d'ordres le
déformerait.

**Écarté.**
- *Un module de `quant-backtest`* : les deux s'appellent « backtest », mais
  l'un tient un livre à la journée et l'autre une file d'attente à la
  nanoseconde. Ils partageraient un nom, pas du code.
- *Un module de `quant-modeling`* : le mainteneur ne veut pas charger le dépôt
  de pricing.

**Lien futur.** Les modèles de coût calibrés ici (fourchette, impact)
pourraient nourrir `quant-backtest`. Cela se fera par un contrat écrit, le
moment venu, pas par une dépendance de code décidée maintenant.

---

## ADR-002 — C++20, des entiers, un seul format d'événement

**Décision.** Lib C++20. Prix en ticks entiers, quantités en lots entiers,
temps en nanosecondes entières. Un seul format d'événement pour un
enregistrement de marché, un simulateur et le moteur d'appariement.

**Pourquoi.** Un carnet d'ordres est le cas d'école où la structure de
données et la performance sont le sujet : c'est là que le C++ du mainteneur
compte, et c'est une question d'entretien à part entière. Les entiers rendent
les comparaisons de prix exactes et les tests de conservation stricts. Le
format unique permet de tester le moteur contre lui-même (il relit ce qu'il
émet) et de faire tourner une stratégie indifféremment sur un rejeu ou dans
un simulateur.

**Écarté.**
- *Python (pandas, numpy)* : suffisant pour les mesures du §7 du compagnon,
  pas pour un carnet rejoué événement par événement. Le wheel donne Python là
  où il est à sa place : l'analyse et les graphiques.
- *Des prix en `double`* : plus simple à l'interface, mais deux prix « égaux »
  ne le sont plus, et un carnet indexé par prix devient fragile.

---

## ADR-003 — Des cryptomonnaies comme terrain d'étude

**Décision.** Les données réelles viennent du flux public d'une bourse de
cryptomonnaies.

**Pourquoi.** C'est la seule source de carnet gratuite, sans compte, ouverte
en continu, et qui donne le côté agresseur des transactions. Le mainteneur
préfère ingérer une vraie donnée de marché plutôt que de s'en tenir à des
simulations. Le choix de la place et des paires se fait au lot M1, après
lecture de la documentation des flux (disponibilité d'un flux ordre par
ordre, numérotation des messages).

**Écarté.**
- *Acheter des données actions* (LOBSTER, Databento) : c'est le marché que
  décrivent les livres, mais c'est payant, et inutile tant que le code ne
  tourne pas. À reconsidérer après le lot M5.
- *Des simulations seules* : on ne verrait jamais un fait stylisé qu'on n'a
  pas soi-même mis dans le simulateur.

**Limite assumée.** Un marché de cryptomonnaies n'est pas un marché actions.
Tout résultat le dit (§15 du compagnon).

---

## ADR-004 — Capture et stockage des flux — OUVERTE

**Proposition.** La capture est un service de `data-ingest` (une connexion
permanente, pas une tâche planifiée), qui écrit des fichiers d'événements
compressés, un par paire et par jour, sur le disque du serveur. Postgres ne
garde que le catalogue (quels fichiers, quelles plages, combien de trous).

**Pourquoi cette proposition.** Un flux de carnet produit beaucoup plus de
lignes qu'aucune table actuelle de `data-ingest`, et se relit toujours en
entier et dans l'ordre : c'est le cas d'usage d'un fichier en colonnes, pas
d'une table relationnelle.

**Ce qu'il faut avant de décider.**
- **Mesurer** le volume d'une journée sur une paire liquide. Aucun chiffre
  n'est avancé ici.
- **Le choix du mainteneur** : `data-ingest` est aujourd'hui Airflow vers
  Postgres ; un service de capture permanent et des fichiers hors base sont
  deux nouveautés pour ce dépôt. La règle de `quant-modeling` (« tout passe
  par la base ») est une règle de `quant-modeling` ; elle ne s'applique pas
  ici d'office, mais c'est au mainteneur de le dire.
- La rétention, et la place disponible sur le serveur.

**Alternatives à départager.**
- *Tout dans Postgres* (éventuellement avec une extension de séries
  temporelles) : une seule source, des requêtes SQL ; coût à mesurer.
- *Un service de capture dans ce dépôt* : plus simple à démarrer, mais
  contredit « aucun réseau ici » et met de l'exploitation dans un dépôt
  d'étude.

---

## ADR-005 — Simulateurs d'abord, données ensuite

**Décision.** Chaque module est développé et testé sur des flux simulés,
avant de tourner sur des données capturées. Les modèles à vérité connue
(Roll, Kyle, Glosten-Milgrom, Hawkes de paramètres donnés) sont simulés pour
vérifier que chaque estimateur retrouve sa formule.

**Pourquoi.** Rien ne dépend ainsi de la capture ni de la longueur de
l'historique. Et c'est la seule façon de tester un estimateur : sur des
données réelles on ne connaît pas la bonne réponse. C'est la démarche du
backtest de couverture de `quant-backtest`, qui commence en monde simulé.

**Écarté.**
- *Attendre les données pour commencer* : des semaines perdues, et des
  estimateurs qu'on ne saurait pas valider.

---

## ADR-006 — Pas de trading réel

**Décision.** Aucun code qui envoie un ordre à une place, aucune clé d'API de
trading, ni ici ni dans la capture (qui n'utilise que des flux publics, sans
compte).

**Pourquoi.** C'est un dépôt d'étude. Un bug dans un moteur d'appariement
simulé coûte un test rouge ; le même bug branché sur un compte coûte de
l'argent. Et depuis une connexion grand public, une stratégie de tenue de
marché est une course de vitesse perdue d'avance (§12 du compagnon).

**Écarté.**
- *Un mode « papier » branché sur une place* : pédagogique, mais c'est déjà
  une connexion authentifiée et un pas vers le réel. Si le mainteneur le veut
  un jour, c'est un nouvel ADR et un dépôt séparé.

---

## ADR-007 — Le format d'événement

**Décision.** Un événement est un enregistrement de taille fixe
(`core/event.hpp`) : deux horodatages (`ts` de la place, `ts_recv` de
réception), un numéro de séquence, un identifiant d'ordre, un prix, une
quantité, un type et un côté. Cinq types : `Add`, `Cancel`, `Trade` pour un
flux ordre par ordre (L3) ; `Level`, `Trade` pour un flux agrégé (L2) ;
`Clear` pour les deux. Sur disque : un en-tête de 8 octets puis des
enregistrements de 50 octets, entiers petit-boutistes écrits champ par champ.

Conventions :

- le `side` d'un `Trade` est celui de **l'agresseur** (le $\varepsilon$ du
  compagnon : $+1$ pour un achat) ; l'ordre en attente est du côté opposé, et
  le prix est le sien ;
- un `Level` porte la quantité **totale** au prix, pas une variation ; zéro
  supprime le niveau ;
- dans un flux agrégé, un `Trade` n'est qu'une information : c'est le `Level`
  qui le suit qui change le carnet ;
- deux événements consécutifs ont des numéros qui se suivent ; tout autre
  écart est un trou.

**Pourquoi.** Trois messages suffisent à décrire un marché à carnet (§2.1 du
compagnon), et un flux agrégé n'en demande qu'un de plus. Une taille fixe se
lit sans analyse syntaxique, et l'écriture champ par champ rend un fichier
indépendant de la machine et du compilateur, ce que demande le déterminisme.
Les deux horodatages sont ceux que le §15 du compagnon demande de garder.

**Écarté.**
- *Écrire la structure telle quelle en mémoire* : plus court, mais les octets
  de remplissage et le boutisme dépendraient du compilateur.
- *Un type par message, dans un `std::variant`* : plus sûr à la lecture, mais
  la taille n'est plus fixe et chaque consommateur doit visiter le variant.
- *Des types forts pour `Price` et `Quantity`* : ils empêcheraient de passer
  l'un pour l'autre. Ce sont de simples alias pour l'instant ; à reconsidérer
  si l'erreur se produit.

**Ce que le format ne porte pas, et qui pourra demander une version 2.**
- L'identifiant de l'ordre **agresseur** d'une transaction. Le moteur le
  rend à l'appelant (`SubmitResult`), le flux ne le garde pas.
- La compression et le découpage en fichiers, qui relèvent de l'ADR-004.

---

## ADR-008 — La structure de données du carnet

**Décision.** Le carnet ordre par ordre (`book/order_book.hpp`) est fait de
trois structures : par côté, une table ordonnée (`std::map`) du prix vers le
niveau ; par niveau, une liste doublement chaînée des ordres, du plus ancien
au plus récent ; une table de hachage de l'identifiant vers l'ordre. Les
ordres vivent dans un vecteur et se chaînent par indice ; chacun garde un
itérateur vers son niveau.

**Pourquoi.** Les opérations fréquentes sont, dans l'ordre : ajouter un
ordre, en annuler un, lire le meilleur prix, exécuter le premier de la file.
Avec cette structure elles sont en temps constant, sauf l'ouverture d'un
nouveau niveau de prix, logarithmique dans le nombre de niveaux. C'est la
structure la plus simple qui ait ces complexités, et le premier benchmark
(`benchmarks/RESULTS.md`) donne le point de départ contre lequel mesurer la
suite.

**Écarté pour l'instant.**
- *Un tableau indexé par le prix en ticks* : accès au niveau sans arbre, mais
  il faut borner la plage de prix, ce qui convient mal à un actif à petit
  tick dont le prix se déplace de milliers de ticks.
- *Des tables de hachage et des allocateurs spécialisés* : sans mesure sur un
  flux réel, on ne sait pas ce qu'ils rapporteraient.

Ces deux pistes ne se rouvrent qu'avec un benchmark sur un flux capturé
(lot M1) : le flux aléatoire actuel n'a que 41 niveaux de prix et ne dit rien
du coût de l'arbre sur un vrai carnet.

---

## ADR-009 — Ce que les simulateurs sont, et où ils s'écartent des articles

**Décision.** Le lot M4 livre quatre familles de simulateurs dans `sim/`, avec
un générateur aléatoire commun (`sim/rng.hpp` : `std::mt19937_64`, dont la
suite est fixée par la norme, et des lois écrites à la main parce que celles
de la bibliothèque standard ne sont pas spécifiées).

| Simulateur | Sortie | Régime | Écart par rapport à l'article |
|---|---|---|---|
| Intelligence zéro (Smith et al. 2003) | flux ordre par ordre, à travers le moteur d'appariement | aucun supposé | ordres déposés sur une **fenêtre finie** de prix, pas sur un intervalle semi-infini : le carnet peut se vider, et un côté vide est remplacé par son dernier meilleur prix |
| Hawkes, noyau exponentiel | instants d'arrivée en secondes | sans objet | **univarié** seulement ; pas encore un flux d'événements |
| Queue-reactive (Huang, Lehalle & Rosenbaum 2015), modèles I et III | flux **agrégé** (`Level`, `Trade`) | **gros tick** | une seule taille d'événement pour toutes les files (l'article en a une par file) ; tailles de file bornées par la longueur des tables d'intensité |
| Roll, Kyle (une période), Glosten-Milgrom | transactions pour Roll ; tableaux de nombres pour les deux autres | sans objet | aucun |

**Pourquoi ces écarts.** La fenêtre finie est ce qui rend l'intelligence zéro
simulable ; elle ne change pas la profondeur loin du meilleur prix, que le
test retrouve ($\alpha/\delta$). La taille unique et la borne des files
simplifient le queue-reactive sans toucher à sa loi invariante, que le test
retrouve aussi. Le Hawkes multivarié n'est utile qu'au lot M6 (propagateur) :
il s'écrira là, avec son besoin.

**Formules relues dans l'article, et celles qui ne l'ont pas été.**

- Relues (les équations sont citées dans les commentaires) : vraisemblance
  récursive, amincissement d'Ogata et changement de temps dans Laub, Taimre &
  Pollett (2015) ; intensité moyenne dans Bacry, Mastromatteo & Muzy (2015) ;
  modèle et profondeur asymptotique dans Smith et al. (2003) ; modèles I et
  III et loi invariante dans Huang, Lehalle & Rosenbaum (2015).
- **Non relues** : Roll (1984), Kyle (1985) et Glosten & Milgrom (1985), dont
  les articles ne sont pas en accès libre. Leurs formules sont dérivées dans
  les commentaires de `sim/toy_models.hpp` et vérifiées par simulation ;
  aucun numéro d'équation n'est cité. Même chose pour Ogata (1981), lu à
  travers Laub et al. À relire dans les originaux quand ils seront
  disponibles.

**Déterminisme.** Une graine donne partout les mêmes entiers. Les tirages en
virgule flottante passent par `std::log`, `std::sqrt` et `std::cos` : ils sont
identiques là où la bibliothèque mathématique arrondit de la même façon, ce
qui n'est pas garanti d'une plateforme à l'autre.

**Écarté.**
- *Les lois de `<random>`* (`std::exponential_distribution`, etc.) : leur
  algorithme dépend de la bibliothèque standard, donc la même graine ne
  donnerait pas le même flux avec un autre compilateur.
- *Sortir le queue-reactive en flux ordre par ordre* : le modèle ne connaît
  que des quantités par prix ; inventer des ordres ajouterait une hypothèse
  de file que l'article ne fait pas.

