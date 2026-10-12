# Décisions d'architecture

Chaque décision : ce qui est retenu, pourquoi, et **ce qui est écarté**. Une
décision qui change devient une nouvelle entrée qui la remplace ; on ne réécrit
pas l'historique.

Les entrées marquées **OUVERTE** sont des propositions : elles attendent une
mesure ou un choix du mainteneur, et ne doivent pas être traitées comme
acquises. Il n'en reste aucune : l'ADR-004 a été tranchée par l'ADR-015.

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

## ADR-004 — Capture et stockage des flux — OUVERTE, tranchée par l'ADR-015

*Le stockage a été décidé le 11 octobre 2026 : voir l'ADR-015. Le texte
ci-dessous est conservé tel qu'il était.*

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

---

## ADR-010 — La bande, et ce que les mesures supposent

**Décision.** Les mesures de `measures/` ne lisent pas un flux d'événements
mais une **bande** (`measures/tape.hpp`) : la suite des cotations (meilleurs
prix et quantités, à chaque changement) et la suite des transactions, tirées
d'un flux par `buildTape`. Un flux ordre par ordre et le flux agrégé du même
marché donnent la même bande (c'est un test).

Conventions :

- **une transaction = un ordre agressif.** Les événements `Trade` de même
  horodatage et de même côté sont regroupés, avec leur prix moyen pondéré,
  comme le font Bouchaud, Gefen, Potters & Wyart (2004, §2.1) ;
- **le prix d'une transaction est comparé au milieu qui la précède**, jamais à
  une cotation qu'elle a elle-même produite ;
- la fonction de réponse et la corrélation des signes sont en **temps de
  transaction** ; le déséquilibre est observé **à chaque mise à jour de
  cotation**. Les observations successives ne sont pas indépendantes : les
  comptages ne se lisent pas comme des tirages ;
- les variances réalisées sont dans le carré de l'unité des prix fournis, sur
  tout l'échantillon, pas par unité de temps.

**Pourquoi.** Toutes les mesures du §4.5 et du §7 du compagnon se calculent
au meilleur prix : la bande est la plus petite donnée qui les porte toutes,
et elle se construit de la même façon à partir d'un flux L3, L2 ou de
transactions seules.

**Ce qui n'est pas fait dans le lot M3.**
- Le lambda de Kyle par régression : au lot M6, avec l'impact.
- La mesure d'Amihud (données quotidiennes), le VPIN (§7.4 : à ne pas
  afficher sans son débat), l'effet Epps (il faut deux actifs), le
  déséquilibre du flux d'ordres de Cont, Kukanov & Stoikov, le micro-prix de
  Stoikov, les noyaux réalisés : cités par le compagnon, pas codés.
- **Aucune mesure n'a encore tourné sur des données réelles** (lot M1).

**Formules.** Relues dans l'article : fonction de réponse et corrélation des
signes (Bouchaud et al. 2004, éq. 3 et 6) ; biais de la variance réalisée et
estimateur à deux échelles (Zhang, Mykland & Aït-Sahalia 2005, éq. 5, 34, 55,
64). **Non relue** : la règle de Lee & Ready (1991), codée telle que la
décrit le §7.2 du compagnon (règle de cotation puis *tick test*), sans le
décalage des cotations de l'article d'origine.

**Écarté.**
- *Calculer les mesures directement sur le flux d'événements* : chaque mesure
  devrait rejouer le carnet.
- *Une transaction par événement `Trade`* : un ordre qui traverse trois
  ordres en attente compterait pour trois, ce qui gonflerait artificiellement
  l'autocorrélation des signes.

---

## ADR-011 — Le backtest au niveau du carnet : ce qu'il suppose

**Décision.** Une stratégie (`replay/backtest.hpp`) ne voit que deux choses :
le marché (meilleurs prix et sa propre position) et une passerelle pour
envoyer des ordres. Elle tourne sans changement en deux lieux :

| Lieu | Les données | La réaction du marché | La file d'attente |
|---|---|---|---|
| `runReplay` : rejeu d'un flux enregistré | réelles | **aucune** | exacte en L3, supposée en L2 |
| `runSimulation` : dans le simulateur d'intelligence zéro | un modèle | mécanique, par le carnet | exacte (c'est le moteur qui la tient) |

Les hypothèses du rejeu, toutes déclarées dans `BacktestConfig` ou dans le
commentaire de `runReplay`, et reprises dans `BacktestResult` :

- **Latence** : un délai constant entre la décision et l'arrivée de l'ordre.
  La stratégie voit le marché sans délai ; tout l'aller-retour est imputé à
  l'envoi.
- **File** : un ordre limité arrive en queue, derrière tout ce qui est
  affiché à son prix. En L3 on sait quels ordres étaient devant. En L2 une
  annulation est anonyme : `Pessimistic` (elle vient de derrière nous tant
  qu'il y a quelqu'un) ou `ProRata` (répartie sur toute la file).
- **Exécution passive** : quand le volume agressif enregistré dépasse la file
  devant nous, ou tout de suite si la transaction enregistrée s'est faite à
  un prix moins bon que le nôtre.
- **Impact de nos ordres** : un ordre agressif prend les quantités affichées,
  du meilleur prix vers le moins bon, et **laisse le carnet enregistré
  intact** : la liquidité prise est de nouveau là pour l'ordre suivant.
  C'est la limite la plus forte du rejeu, et la raison d'être du simulateur.
- **Frais** : une fraction du notionnel, par côté (fournisseur ou preneur),
  négative pour une remise.
- **Après un trou** (événement `Clear`) nos ordres perdent leur place et
  passent derrière ce que montre l'instantané.

**Pas de futur.** Un ordre qui arrive à l'instant d'un événement est appliqué
avant lui : il voyageait déjà. Le test coupe le flux à un instant $t$, le
remplace par deux autres futurs, et vérifie que décisions et exécutions
jusqu'à $t$ sont identiques.

**Écarté.**
- *Exécuter un ordre limité dès que le prix touche son niveau* : la version
  naïve du §10 ; un test vérifie qu'on ne la fait pas.
- *Retirer du carnet rejoué la liquidité que nous prenons* : il faudrait
  décider comment elle revient, c'est-à-dire un modèle de réaction posé sur
  des données réelles. Le simulateur le fait proprement.
- *Deux latences* (données et ordres) : une seule suffit tant qu'aucune
  stratégie ne dépend de la différence.

**Ce qui n'est pas fait.** Le queue-reactive n'est pas un lieu d'exécution
(il ne connaît que des quantités par prix, pas d'ordres) ; la stratégie ne
voit que le meilleur prix, pas la profondeur ; le backtest n'est pas exposé à
Python (une stratégie est une classe C++), ce qui viendra avec les stratégies
des lots M7 et M8.

---

## ADR-012 — L'impact : ce qui est mesuré, et un résultat négatif

**Décision.** Le lot M6 livre dans `impact/` : le modèle de propagateur de
Bouchaud, Gefen, Potters & Wyart (2004) sans le facteur de volume
(simulation, diffusion $D(\ell)$, ajustement de $G$ par résolution du
système linéaire qui relie la réponse aux corrélations de signes) ; le lambda
de Kyle par régression ; la mesure de l'impact d'un métaordre injecté dans le
simulateur d'intelligence zéro ; le coût d'un aller-retour dans un modèle
d'impact transitoire. Et dans `sim/` un flux de signes à mémoire longue (le
modèle de découpage d'ordres de Lillo, Mike & Farmer 2005, forme « N fixé »).

**Ce que les tests retrouvent.**
- Kyle simulé : la régression donne $\sigma_v / (2\sigma_u)$.
- Sur un flux de mémoire $\gamma = 1/2$, l'exposant de croissance de
  $D(\ell)$ vaut $2 - 2\beta - \gamma$ sous l'exposant critique, et passe par
  1 (prix diffusif) à $\beta = (1-\gamma)/2$.
- Le propagateur est relu à quelques pour cent près sur la première moitié
  des retards ajustés, malgré des signes très corrélés et du bruit.
- Avec un impact linéaire et un noyau décroissant, aucun aller-retour ne
  gagne d'argent ; avec un impact concave et une décroissance exponentielle,
  on en exhibe un qui gagne.

**Résultat négatif : la loi en racine carrée n'est pas reproduite.** Dans le
simulateur d'intelligence zéro, l'impact d'un métaordre exécuté lentement est
**proportionnel** à sa taille (exposant mesuré proche de 1, pas de 1/2). Seul
l'impact instantané d'un ordre qui traverse le carnet est concave (exposant
mesuré autour de 0,9). Ce flux n'a pas de mémoire et n'anticipe rien : le
carnet se reconstitue au même rythme quoi qu'on exécute. Le critère du §5.4
(« vérifier la loi en racine carrée dans un simulateur ») **n'est donc pas
rempli**, et un test garde ce constat. La retrouver demande un modèle à
liquidité latente (Tóth et al. 2011), qui n'est pas écrit.

**Tranché le 12 octobre 2026** (issue #15) : ce modèle n'est pas construit
dans les lots du blueprint. Le compagnon dit au §5.4 que la loi n'est pas
vérifiée ici, le critère est retiré du lot M6 au §13, et le simulateur à
liquidité latente y figure comme suite possible.

**Limites de mesure.** L'exposant de mémoire d'un flux se mesure mal : les
tailles d'ordres ont une variance infinie, et sur un million de signes
l'exposant ajusté varie de 0,4 à 0,6 selon la graine pour une valeur
théorique de 0,5. Les tests moyennent plusieurs tirages et comparent d'abord
la corrélation à sa valeur exacte aux petits retards. La même prudence vaudra
sur données réelles.

**Formules.** Relues dans l'article : modèle de propagateur, réponse,
forme en loi de puissance et exposant critique (Bouchaud et al. 2004, éq. 1,
12, 17, 22, 23) ; modèle de découpage et exposant de la corrélation (Lillo,
Mike & Farmer 2005, §II, éq. 16 et 17). **Non relu** : Gatheral (2010), dont
seul le résumé a pu être lu ; il confirme l'énoncé du §5.5 (décroissance
exponentielle compatible seulement avec un impact linéaire).

**Corrigé dans le compagnon.** Le §5.5 illustrait l'arbitrage par « acheter
vite, revendre lentement ». Avec un impact concave, c'est l'inverse qui
gagne dans notre modèle : acheter lentement (chaque lot pousse davantage le
prix) et revendre vite. Le sens dépend de la forme de l'impact ; le §5.5 le
dit depuis le 12 octobre 2026.

**Écarté.**
- *Ajuster le propagateur avec le facteur $\ln V$ de l'article* : les
  simulateurs du dépôt traitent des ordres de taille unique.
- *Présenter l'exposant mesuré dans l'intelligence zéro comme une
  vérification de la loi en racine carrée* : ce n'en est pas une.

---

## ADR-013 — L'exécution : le modèle, les échéanciers, l'analyse des coûts

**Décision.** Le lot M7 livre dans `execution/` :

- **Almgren & Chriss (2000) dans sa forme discrète**, avec la notation de
  l'article : un programme de **vente** de $X$ titres en $N$ intervalles de
  longueur $\tau$. Trajectoire optimale, coût attendu, variance, frontière
  efficiente, et simulation du modèle de prix de l'article ;
- les échéanciers usuels : TWAP, VWAP (un profil de volume attendu), POV (une
  fraction du volume observé, qui peut ne pas finir) ;
- un exécuteur (`ScheduleExecutor`) qui déroule un échéancier dans le backtest
  du lot M5, par un ordre au marché au début de chaque tranche ;
- le déficit d'exécution et sa décomposition (retard, exécution, opportunité,
  frais), et le VWAP du marché comme référence.

**Pourquoi la forme discrète.** C'est celle de l'article, et celle qu'on
exécute : $\kappa$ y est la solution de
$\frac{2}{\tau^2}(\cosh(\kappa\tau) - 1) = \lambda\sigma^2/\tilde\eta$ avec
$\tilde\eta = \eta - \gamma\tau/2$, et ne vaut $\sqrt{\lambda\sigma^2/\eta}$
(la formule du §8.2 du compagnon) qu'à la limite $\tau \to 0$. Coder la
formule continue sur une grille grossière donnerait une trajectoire qui
n'est pas optimale, ce qu'un test vérifie.

**Ce que les tests retrouvent.** Aversion nulle : la droite, qui est le TWAP.
Le coût simulé a la moyenne et la variance des formules fermées (éq. 20).
Aucune autre trajectoire ne fait mieux. La frontière est décroissante et
convexe, de pente $-\lambda$. Plus d'aversion vend plus vite à chaque date.
Dans le simulateur, un ordre découpé coûte moins par lot qu'un bloc.

**Limites.**
- Le modèle suppose un impact **linéaire** dans la vitesse d'exécution, ce
  que la loi en racine carrée contredit, et une marche aléatoire arithmétique
  sans dérive. La trajectoire ne réagit pas au prix.
- Ses paramètres ($\sigma$, $\gamma$, $\eta$) ne sont calibrés sur rien : il
  n'y a pas encore de données.
- **Une seule tactique** : un ordre au marché par tranche. Le second étage du
  §8.3 (ordre limité ou au marché, à quel prix, quand annuler) n'est pas
  écrit ; il relève du queue-reactive, qui n'est pas un lieu d'exécution.
- L'impact transitoire d'Obizhaeva & Wang et l'impact non linéaire d'Almgren
  (2003) ne sont pas codés.
- L'exécution dans le simulateur se lit **en moyenne sur des graines** : une
  seule trajectoire est dominée par le hasard du prix.

**Formules.** Relues dans l'article : Almgren & Chriss (2000), éq. 1 à 13 et
16 à 20, dans une copie de l'article hébergée par un tiers. **Non relu** :
Perold (1988) ; la décomposition du déficit est celle du §8.4 du compagnon,
dérivée dans le commentaire de `execution/tca.hpp` et vérifiée contre sa
définition (portefeuille papier moins portefeuille réel).

**Écarté.**
- *Séparer « fourchette » et « impact » dans le coût d'exécution* : il faut
  le prix milieu juste avant chaque exécution, que `Fill` ne porte pas. À
  ajouter si une stratégie le demande.

---

## ADR-014 — La tenue de marché : les modèles, le teneur de marché, ce que le résultat vaut

**Décision.** Le lot M8 livre dans `mm/` :

- les cotations d'**Avellaneda & Stoikov (2008)** : prix de réservation et
  fourchette (éq. 3.17 et 3.18), et la simulation de leur §3.3 ;
- celles de **Guéant, Lehalle & Fernandez-Tapia (2013)** : la solution exacte
  à inventaire borné (l'exponentielle de matrice de leur proposition 2) et
  l'approximation fermée, stationnaire, qui ne dépend que de l'inventaire ;
- un **teneur de marché pour le backtest** (`MarketMaker`) : un ordre de
  chaque côté, aux distances que donne une politique de cotation autour du
  prix milieu, arrondies au tick en s'éloignant du milieu, jamais à travers
  le carnet, borné par une limite d'inventaire ;
- la **décomposition du P&L** (fourchette encaissée, mouvement du prix,
  frais) et les **markouts** à plusieurs horizons.

**Ce que les tests retrouvent.** Inventaire nul : cotations symétriques. Le
décalage a le signe opposé à l'inventaire, la fourchette n'en dépend pas.
Contre la stratégie symétrique, les écarts-types du P&L et de l'inventaire
final sont plus faibles : dans la simulation de l'article, et dans le carnet
simulé avec le teneur de marché du backtest. La solution exacte devient
stationnaire loin de l'horizon, et l'approximation fermée en est proche.

**L'article n'est reproduit qu'à peu près.** La procédure du §3.3, codée
telle qu'elle est écrite, donne pour $\gamma = 0{,}1$ un profit de 64,9
(écart-type 6,5) et un inventaire final d'écart-type 2,9, contre 62,94, 5,89
et 2,80 dans le tableau 1. Pour $\gamma = 0{,}5$ l'écart est franc : 48,6
contre 33,92 (tableau 3). L'article ne décrit pas sa simulation assez
précisément pour l'expliquer, et ses tableaux portent sur 1000 trajectoires.
Le test compare à 5 % près sur les profits et 15 % sur les écarts-types pour
$\gamma = 0{,}1$, et ne prétend rien pour $\gamma = 0{,}5$.

**Ce que vaut un résultat de tenue de marché obtenu ici.**
- **Dans le simulateur d'intelligence zéro, coter au meilleur prix gagne la
  fourchette presque sans risque.** Ce flux n'est jamais informé : il n'y a
  de sélection adverse que mécanique. Le niveau du P&L n'y veut rien dire ;
  seules les comparaisons (avec ou sans décalage d'inventaire) se lisent.
- **Sur un rejeu**, le résultat dépend de la latence et de l'hypothèse de
  file (principe 7) : un test vérifie qu'ils changent le nombre
  d'exécutions, et la liaison Python renvoie les hypothèses avec le
  résultat. Il n'y a pas encore de flux réel à rejouer.
- Les paramètres $A$ et $k$ ne sont **ajustés sur rien**. Les estimer
  (intensité d'exécution en fonction de la distance) est un travail à faire
  sur données capturées.
- Les modèles supposent un prix de référence martingale, sans tick ni file
  d'attente : tout ce que le §9.3 du compagnon liste comme absent l'est.

**Un défaut du backtest corrigé dans ce lot.** Pendant un rejeu, la stratégie
était prévenue d'une exécution avant que l'événement qui la cause soit
appliqué au carnet : elle recotait sur une image en retard d'un événement et
pouvait traverser le carnet. Les exécutions sont maintenant comptabilisées
tout de suite et notifiées une fois le carnet à jour (test
`Replay.FillIsReportedWithTheBookUpToDate`). Cela complète l'ADR-011.

**Limites.**
- Pas d'ordre « post-only » : sous latence, une cotation peut arriver dans un
  carnet qui a bougé et s'exécuter en preneur.
- Le teneur de marché ne regarde que le prix milieu et son inventaire : pas
  de signal de déséquilibre, pas de gestion de sa place dans la file.

**Formules.** Relues dans l'article : Avellaneda & Stoikov (2008), éq. 3.17,
3.18 et §3.3 ; Guéant, Lehalle & Fernandez-Tapia (2013), propositions 1 à 3
et théorème 1.

---

## ADR-015 — Stockage des flux capturés : fichiers binaires, séries temporelles en appui

Remplace la proposition de l'ADR-004 pour ce qui est du stockage.

**Décision.** Choix du mainteneur (11 octobre 2026).

- **Les événements bruts vont dans des fichiers binaires à enregistrements de
  taille fixe** : le format d'événement de l'ADR-007 (`feed/event_file.hpp`),
  un fichier par paire et par jour. Ces fichiers sont la **source de
  vérité** : c'est eux que lisent le rejeu, le backtest et les mesures.
- **Une base de séries temporelles vient en appui**, dans le Postgres de
  `data-ingest` : le catalogue (quels fichiers, quelles plages, combien de
  trous) et des **agrégats calculés à partir des fichiers** (barres,
  fourchette, volume, qualité du flux), interrogeables en SQL.
- **Le sens est unique** : fichiers vers base. Un agrégat faux ou nouveau se
  recalcule depuis les fichiers ; on ne reconstruit jamais un carnet à partir
  d'agrégats.

**Pourquoi.**
- Le besoin premier est de relire une journée entière dans l'ordre : un
  fichier le fait sans intermédiaire, et le lecteur existe déjà ici.
- La capture ne dépend pas de la base : si Postgres ralentit ou tombe, on
  continue d'écrire. Une heure de flux perdue ne se rattrape pas.
- La base sert là où le SQL est utile (exploration, contrôle de qualité,
  questions ponctuelles sur des mois), et fait pratiquer au mainteneur une
  compétence demandée.

**Décidé sans la mesure que l'ADR-004 demandait.** Le volume d'une journée
n'a pas été mesuré. La décision tient quel que soit ce volume pour le brut ;
la mesure reste à faire, parce qu'elle fixe la rétention et la compression.
Ce qui la ferait rouvrir : un volume assez petit pour que tout tienne
confortablement en base, ou un usage qui deviendrait surtout de
l'exploration en Python plutôt que du rejeu en C++ (des fichiers Parquet
seraient alors un meilleur compromis).

**Écarté.**
- *Tout dans une base de séries temporelles* : une seule source et du SQL
  partout, mais le rejeu passerait par une requête et une conversion, et la
  capture dépendrait de la disponibilité de la base.
- *Des fichiers Parquet pour le brut* : lisibles directement par DuckDB,
  Polars et pandas, mais un format et une dépendance de plus pour le moteur,
  qui lit déjà le sien.
- *kdb+* : la référence du métier pour ce type de données, mais une licence
  et un langage de plus, que rien dans ce besoin ne justifie.
- *Des fichiers texte* : lisibles à l'œil, plus gros et plus lents à relire.

**Ce qui reste à décider au lot M1**, dans `data-ingest` :
- la place de marché et les paires (flux ordre par ordre ou agrégé, une paire
  à gros tick et une à petit tick si possible) ;
- la mesure du volume, puis la rétention ;
- la compression : un fichier compressé d'un bloc perd l'accès direct au
  n-ième enregistrement. Compresser la journée une fois close est le plus
  simple ;
- l'extension de séries temporelles (TimescaleDB est le candidat naturel,
  Postgres étant déjà là) et la liste des agrégats ;
- deux manques du format, qui demanderont peut-être une version 2 : le
  fichier ne dit pas quel type de flux il contient (ordre par ordre ou
  agrégé), et rien n'y décrit l'instrument (taille du tick et du lot), sans
  quoi les entiers ne se convertissent pas en prix.

Le service de capture lui-même (une connexion permanente, dans
`data-ingest`) reste comme le proposait l'ADR-004 et comme le dit
`CLAUDE.md` : ce dépôt n'ouvre aucune connexion réseau.

