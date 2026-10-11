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
