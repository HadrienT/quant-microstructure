# Microstructure : comment un prix se forme, et ce qu'on construit ici

| | |
|---|---|
| **Dépend de** | le dépôt `~/data-ingest` pour la capture des flux de carnet (§15) ; rien d'autre. Aucune dépendance sur `quant-modeling` ni sur `quant-backtest` |
| **Débloque** | un carnet d'ordres et un moteur d'appariement mesurés ; des simulateurs de flux ; un backtest au niveau du carnet ; exécution optimale et tenue de marché testées sur données ; à terme, des coûts de transaction calibrés pour `quant-backtest` |
| **Branches** | une par lot : `feat/m0-skeleton`, `feat/m2-book`, … (§13) |
| **Lectures principales** | Bouchaud, Bonart, Donier & Gould, *Trades, Quotes and Prices*, Cambridge University Press 2018 (le fil conducteur, §2 à §6) ; Cartea, Jaimungal & Penalva, *Algorithmic and High-Frequency Trading*, Cambridge University Press 2015 (§8, §9) ; Guéant, *The Financial Mathematics of Market Liquidity*, CRC 2016 (§8, §9) ; Hasbrouck, *Empirical Market Microstructure*, Oxford University Press 2007 (§4, §7) |

Ce document a deux usages. C'est d'abord un **compagnon de lecture** : les §1
à §10 décrivent le domaine, avec les formules et les références. C'est ensuite
une **spécification** : les §11 à §15 disent ce qu'on construit, dans quel
ordre, et comment on le teste.

Les formules sont données de mémoire, avec leur source. **Avant d'en coder
une, la revérifier dans l'article cité** au §17 et citer l'équation dans le
commentaire de la fonction. Les références marquées † sont celles dont un
détail bibliographique est lui-même à revérifier.

---

## Sommaire

0. [Notation](#0-notation)
1. [Le problème en une page](#1-le-problème-en-une-page)
2. [La mécanique d'un marché à carnet](#2-la-mécanique-dun-marché-à-carnet)
3. [Faits stylisés](#3-faits-stylisés)
4. [Pourquoi il y a une fourchette](#4-pourquoi-il-y-a-une-fourchette)
5. [L'impact de marché](#5-limpact-de-marché)
6. [Modèles de flux et de carnet](#6-modèles-de-flux-et-de-carnet)
7. [Mesurer à haute fréquence](#7-mesurer-à-haute-fréquence)
8. [Exécution optimale](#8-exécution-optimale)
9. [Tenue de marché](#9-tenue-de-marché)
10. [Backtester au niveau du carnet](#10-backtester-au-niveau-du-carnet)
11. [Architecture](#11-architecture)
12. [Ce que ce dépôt n'est pas](#12-ce-que-ce-dépôt-nest-pas)
13. [Les lots](#13-les-lots)
14. [Tests de propriété](#14-tests-de-propriété)
15. [Données et limites assumées](#15-données-et-limites-assumées)
16. [Aide-mémoire](#16-aide-mémoire)
17. [Bibliographie](#17-bibliographie)
18. [Questions d'entretien](#18-questions-dentretien)

---

## 0. Notation

| Symbole | Sens |
|---|---|
| $b_t$, $a_t$ | meilleur prix acheteur (*bid*) et vendeur (*ask*) à l'instant $t$ |
| $m_t = (a_t + b_t)/2$ | prix milieu ; $s_t = a_t - b_t$ la fourchette (*spread*) |
| $V^b_t$, $V^a_t$ | volume affiché au meilleur bid et au meilleur ask |
| $\vartheta$ | pas de cotation (*tick*) : le plus petit écart de prix autorisé |
| $\varepsilon_n \in \{+1, -1\}$ | signe de la $n$-ième transaction : $+1$ si l'agresseur achète |
| $v_n$, $p_n$ | volume et prix de la $n$-ième transaction |
| $q_t$ | inventaire d'un teneur de marché, ou quantité restant à exécuter |
| $\sigma$ | volatilité du prix milieu, dans l'unité de temps du contexte |
| $\lambda(t)$ | intensité d'un processus ponctuel (événements par unité de temps) ; aussi le lambda de Kyle au §4.3, précisé à chaque fois |
| L1 / L2 / L3 | données au meilleur prix seul / agrégées par niveau de prix / ordre par ordre |

Le temps se compte de deux façons, et il faut toujours dire laquelle : en
**temps physique** (secondes) ou en **temps d'événement** (un pas par
transaction ou par mise à jour du carnet).

---

## 1. Le problème en une page

Le pricing de dérivés suppose un prix : on achète et on vend le sous-jacent à
$S_t$, en quantité quelconque, sans le déplacer. La microstructure étudie ce
que cette hypothèse cache. Il n'y a pas un prix mais un **carnet** : une file
d'ordres d'achat sous le marché, une file d'ordres de vente au-dessus, et un
écart entre les deux. Trois questions organisent tout le domaine.

1. **Comment le prix se forme-t-il ?** Par l'arrivée d'ordres, dont certains
   portent de l'information et d'autres non. Le prix bouge parce qu'on traite
   (§4, §5).
2. **Combien coûte une transaction ?** La fourchette pour un petit ordre,
   l'impact pour un gros. C'est la question de celui qui **prend** la
   liquidité, et le sujet de l'exécution optimale (§5, §8).
3. **Combien rapporte de coter ?** La fourchette encaissée, moins ce que
   coûtent l'inventaire et les contreparties mieux informées. C'est la
   question de celui qui **fournit** la liquidité, et le sujet de la tenue de
   marché (§4, §9).

Qui fait quoi :

| Métier | Ce qu'il optimise | Section |
|---|---|---|
| Desk d'exécution (courtier, gérant) | le coût d'un ordre parent découpé en ordres fils | §8 |
| Teneur de marché, trading haute fréquence | fourchette encaissée contre risque d'inventaire et sélection adverse | §9 |
| Recherche quantitative | le coût réaliste d'une stratégie, et sa capacité | §5 |
| Bourse, régulateur | pas de cotation, frais, règles de priorité | §2 |

Le changement de point de vue par rapport au pricing : le prix n'est plus une
diffusion donnée, c'est le **résultat d'une file d'attente**. Les outils
changent avec lui : processus ponctuels au lieu de browniens, temps
d'événement au lieu de temps continu, et des nombres entiers de ticks.

---

## 2. La mécanique d'un marché à carnet

### 2.1 Les ordres

Presque tous les marchés électroniques (actions, contrats à terme,
cryptomonnaies) sont des **doubles enchères continues**. Trois messages
suffisent à tout décrire.

- **Ordre à cours limité** : « j'achète au plus à ce prix ». S'il ne peut pas
  être exécuté tout de suite, il attend dans le carnet. Il **fournit** de la
  liquidité.
- **Ordre au marché** (ou ordre limité agressif) : exécuté tout de suite
  contre les ordres en attente, en remontant les niveaux de prix. Il **prend**
  de la liquidité.
- **Annulation** d'un ordre en attente. Sur un marché moderne, la grande
  majorité des ordres limités sont annulés avant d'être exécutés.

### 2.2 La priorité

L'appariement suit presque partout la règle **prix puis temps** : le meilleur
prix d'abord ; à prix égal, l'ordre arrivé le premier. C'est ce qui donne de
la valeur à la **position dans la file** : deux ordres au même prix ne valent
pas la même chose. Certains marchés de taux courts répartissent au prorata de
la taille ; ce n'est pas le cas traité ici.

### 2.3 Le pas de cotation

Le prix est un multiple entier de $\vartheta$. Le rapport entre $\vartheta$
et le prix sépare deux régimes.

| | Actif à **gros tick** | Actif à **petit tick** |
|---|---|---|
| Fourchette | presque toujours égale à 1 tick | plusieurs ticks, variable |
| Files au meilleur prix | longues ; la position dans la file est tout | courtes ; on gagne la priorité en améliorant le prix |
| Variable d'état utile | le déséquilibre des volumes | la forme du carnet |

Presque tous les modèles du §6 sont écrits pour l'un des deux régimes.
Toujours vérifier lequel avant de l'appliquer.

### 2.4 Les données

| Niveau | Contenu | Ce qu'on peut en faire |
|---|---|---|
| Transactions seules | prix, volume, parfois le côté agresseur | volatilité réalisée, signature du flux |
| L1 | meilleur bid et ask, avec volumes | fourchette, déséquilibre, signe des transactions |
| L2 | volume agrégé par niveau de prix | profondeur, forme du carnet ; position dans la file **estimée** |
| L3 | chaque ordre, avec son identifiant | reconstruction exacte, position dans la file **connue** |

Un flux L2 arrive sous forme d'un **instantané** suivi de **différences**
numérotées. Reconstruire le carnet, c'est appliquer les différences dans
l'ordre et détecter un numéro manquant : un trou rend le carnet faux jusqu'au
prochain instantané.

### 2.5 Le reste de l'infrastructure

Enchères d'ouverture et de clôture (un seul prix qui maximise le volume
traité), frais différents pour celui qui fournit et celui qui prend
(*maker-taker*), ordres cachés et icebergs, fragmentation entre places. Ils
comptent en pratique ; ils sont hors du périmètre de ce dépôt (§12).

---

## 3. Faits stylisés

Ce que tout modèle doit reproduire, et ce que le lot M3 mesure en premier.

- **Le signe du flux a une mémoire longue.** L'autocorrélation des signes de
  transaction décroît en loi de puissance, $C(\ell) \sim \ell^{-\gamma}$ avec
  $\gamma < 1$, sur des milliers de transactions (Lillo & Farmer 2004 ;
  Bouchaud et al. 2004). La cause dominante est le **découpage** des gros
  ordres. Et pourtant les rendements ne sont presque pas autocorrélés : c'est
  l'énigme que le §5.3 résout.
- **Le prix milieu rebondit à très court terme** : les transactions alternent
  entre bid et ask, ce qui crée une autocorrélation négative des variations
  de prix de transaction (§4.1).
- **Le graphique de signature** : la variance réalisée par unité de temps
  dépend de la fréquence d'échantillonnage, et explose aux hautes fréquences
  (§7.1).
- **Effet Epps** : la corrélation mesurée entre deux actifs tend vers zéro
  quand l'intervalle d'échantillonnage diminue (Epps 1979), parce que leurs
  transactions ne sont pas synchrones.
- **Saisonnalité dans la journée** : volume et volatilité en U sur les marchés
  actions, fourchette large à l'ouverture. Les marchés ouverts en continu ont
  leur propre profil, lié aux fuseaux horaires.
- **Le déséquilibre prédit le prochain mouvement.** Avec
  $I_t = V^b_t / (V^b_t + V^a_t)$, la probabilité que le prochain changement
  de prix milieu soit une hausse croît avec $I_t$. Le **prix milieu pondéré**
  $(a_t V^b_t + b_t V^a_t)/(V^b_t + V^a_t)$ en est la version la plus simple ;
  Stoikov (2018) en construit une version qui est une martingale.
- **Le flux d'ordres explique le prix** : sur de courts intervalles, la
  variation du prix milieu est à peu près linéaire dans le **déséquilibre du
  flux d'ordres** (somme signée des arrivées, annulations et exécutions aux
  meilleurs prix), avec une pente inversement proportionnelle à la profondeur
  (Cont, Kukanov & Stoikov 2014).
- **Les événements arrivent en grappes** : l'activité appelle l'activité, ce
  qui motive les processus de Hawkes du §6.3.

---

## 4. Pourquoi il y a une fourchette

Trois explications classiques. Elles ne s'excluent pas ; les modèles modernes
les combinent.

### 4.1 Les coûts de traitement : Roll

Le prix efficient $m_t$ est une marche aléatoire ; les transactions se font à
$m_t \pm s/2$ selon un signe $\varepsilon_t$ tiré au hasard, indépendant d'une
transaction à l'autre. Alors les variations du prix de transaction sont
négativement autocorrélées, et (Roll 1984) :

$$\text{Cov}(\Delta p_t, \Delta p_{t-1}) = -\frac{s^2}{4} \quad\Longrightarrow\quad s = 2\sqrt{-\text{Cov}(\Delta p_t, \Delta p_{t-1})}$$

C'est un estimateur de fourchette à partir des seuls prix de transaction. Il
suppose des signes indépendants, ce que le §3 contredit : sur données réelles
la covariance est souvent positive et l'estimateur n'est pas défini.

### 4.2 La sélection adverse : Glosten et Milgrom

Le teneur de marché cote sans savoir si sa contrepartie est informée. La
valeur $V$ vaut $V_H$ ou $V_L$ avec probabilité ½ ; une fraction $\pi$ des
contreparties connaît $V$, les autres achètent ou vendent au hasard. Un
teneur de marché en concurrence, à profit nul, cote des **espérances
conditionnelles** (Glosten & Milgrom 1985) :

$$a = \mathbb{E}[V \mid \text{achat}], \qquad b = \mathbb{E}[V \mid \text{vente}], \qquad a - b = \pi\,(V_H - V_L)$$

Un achat est une mauvaise nouvelle pour celui qui vend : la fourchette est le
prix de cette mauvaise nouvelle. Deux conséquences qui restent vraies bien
au-delà du modèle : la fourchette existe **même sans coût ni aversion au
risque**, et chaque transaction déplace le prix, de façon permanente, dans son
sens.

### 4.3 L'information stratégique : Kyle

Un initié connaît la valeur $v \sim \mathcal N(p_0, \sigma_v^2)$, des agents
non informés traitent $u \sim \mathcal N(0, \sigma_u^2)$, et un teneur de
marché ne voit que le flux total $x + u$. À l'équilibre (Kyle 1985, une
période) l'initié traite $x = \beta (v - p_0)$ et le prix est linéaire dans
le flux :

$$p = p_0 + \lambda\,(x + u), \qquad \lambda = \frac{\sigma_v}{2\sigma_u}, \qquad \beta = \frac{\sigma_u}{\sigma_v}$$

Le **lambda de Kyle** est le prix d'une unité de flux : l'inverse de la
profondeur. L'initié se retient pour ne pas révéler son information : le prix
n'en incorpore que la moitié, la variance résiduelle de $v$ vaut
$\sigma_v^2/2$. C'est l'origine de l'idée d'impact linéaire, et la régression
de $\Delta p$ sur le flux signé en est l'estimateur usuel.

### 4.4 Le risque d'inventaire

Un teneur de marché averse au risque décale ses prix pour ramener son
inventaire vers zéro : il baisse bid et ask quand il est long (Ho & Stoll
1981). C'est le mécanisme du §9.

### 4.5 Décomposer la fourchette sur données

Pour une transaction de signe $\varepsilon_t$ au prix $p_t$, avec un horizon
$\Delta$ :

| Mesure | Définition | Lecture |
|---|---|---|
| Demi-fourchette effective | $\varepsilon_t\,(p_t - m_t)$ | ce que paie réellement l'agresseur |
| Impact de prix | $\varepsilon_t\,(m_{t+\Delta} - m_t)$ | la part due à l'information : le prix a bougé contre le teneur de marché |
| Demi-fourchette réalisée | $\varepsilon_t\,(p_t - m_{t+\Delta})$ | ce que garde le teneur de marché |

Par construction, effective = réalisée + impact. La demi-fourchette réalisée
est le *markout* d'un teneur de marché : négative, elle veut dire qu'il est
sélectionné par des contreparties mieux informées.

---

## 5. L'impact de marché

### 5.1 Trois choses qu'on appelle impact

| | Définition | Ordre de grandeur |
|---|---|---|
| Impact d'une **transaction** | déplacement moyen du prix milieu après un ordre au marché | une fraction de fourchette |
| Impact d'un **métaordre** | déplacement moyen entre le début et la fin d'un gros ordre découpé | loi en racine carrée |
| Impact **permanent** | ce qui reste longtemps après | une fraction du pic, débattue |

### 5.2 Fonction de réponse

La mesure de base, en temps d'événement (Bouchaud et al. 2004) :

$$R(\ell) = \mathbb{E}\big[\varepsilon_n\,(m_{n+\ell} - m_n)\big]$$

$R(\ell)$ croît avec $\ell$ puis se stabilise. Elle mélange l'effet de la
transaction $n$ et celui des transactions suivantes, qui ont tendance à être
de même signe (§3).

### 5.3 Le propagateur

Pour concilier un flux à mémoire longue et des rendements presque sans
mémoire, Bouchaud, Gefen, Potters & Wyart (2004) écrivent le prix comme une
somme d'impacts qui **décroissent** :

$$m_n = m_0 + \sum_{k < n} G(n - k)\,\varepsilon_k\,f(v_k) + \text{bruit}$$

Si les signes sont corrélés en $\ell^{-\gamma}$, le prix n'est diffusif que si
le propagateur décroît en $G(\ell) \sim \ell^{-\beta}$ avec

$$\beta = \frac{1 - \gamma}{2}$$

L'impact d'une transaction n'est donc ni permanent ni fixe : il s'efface
lentement, exactement assez pour compenser la persistance du flux. Le marché
est efficient par un équilibre entre deux effets qui, pris séparément, le
rendraient prévisible.

### 5.4 La loi en racine carrée

Pour un métaordre de $Q$ titres sur un actif de volume quotidien $V$ et de
volatilité quotidienne $\sigma_d$ :

$$\frac{\Delta p}{p} \approx Y\,\sigma_d\,\sqrt{\frac{Q}{V}}, \qquad Y \text{ de l'ordre de } 1$$

(Tóth et al. 2011 ; Almgren, Thum, Hauptmann & Li 2005). Elle est
remarquablement stable d'un marché à l'autre, et **concave** : le premier
pour cent du volume coûte beaucoup plus que le dixième. Elle contredit
l'impact linéaire de Kyle pour les gros ordres. Le coût total croît comme
$Q^{3/2}$, ce qui borne la capacité d'une stratégie.

Elle ne se mesure que si l'on connaît le début et la fin de métaordres, donc
sur ses propres ordres ou sur des données propriétaires. Sur données
publiques on ne peut que la **vérifier dans un simulateur** (§13, lot M6).

### 5.5 Contraintes de cohérence

Un modèle d'impact ne doit pas permettre de gagner de l'argent en faisant un
aller-retour (acheter vite, revendre lentement). Gatheral (2010) en tire des
conditions qui lient la forme de l'impact instantané à la décroissance du
noyau : par exemple, une décroissance exponentielle n'est compatible qu'avec
un impact instantané linéaire. C'est un test à faire passer à tout modèle
d'impact du dépôt.

---

## 6. Modèles de flux et de carnet

### 6.1 Intelligence zéro

Trois flux de Poisson indépendants : ordres limités à taux $\lambda$ par
niveau de prix, ordres au marché à taux $\mu$, annulation de chaque ordre en
attente à taux $\nu$ (Smith, Farmer, Gillemot & Krishnamurthy 2003 ; Cont,
Stoikov & Talreja 2010 en donnent une version calculable, avec des
probabilités de transition par transformée de Laplace). Aucun agent ne
réfléchit, et le modèle reproduit pourtant l'ordre de grandeur de la
fourchette, la forme moyenne du carnet et un impact concave. C'est le
**simulateur de référence** : simple, entièrement spécifié, et assez riche
pour tester un moteur d'appariement et une stratégie.

Ce qu'il ne reproduit pas : la mémoire du flux, donc le prix y est trop
prévisible.

### 6.2 Queue-reactive

Huang, Lehalle & Rosenbaum (2015) font dépendre les intensités d'arrivée,
d'annulation et d'exécution de **l'état du carnet** (la taille des files).
Les intensités s'estiment directement sur données, file par file. C'est le
modèle de référence pour un actif à gros tick, et celui qu'on utilise pour
évaluer une tactique d'exécution en tenant compte de sa position dans la
file.

### 6.3 Processus de Hawkes

Un processus ponctuel dont l'intensité dépend de son propre passé (Hawkes
1971) :

$$\lambda(t) = \mu + \sum_{t_i < t} \phi(t - t_i)$$

Avec un noyau exponentiel $\phi(u) = \alpha e^{-\beta u}$ :

| Grandeur | Formule | Sens |
|---|---|---|
| Rapport de branchement | $n = \int_0^\infty \phi = \alpha/\beta$ | nombre moyen d'événements déclenchés par un événement |
| Stationnarité | $n < 1$ | |
| Intensité moyenne | $\Lambda = \mu / (1 - n)$ | |
| Part endogène | $n$ | fraction des événements qui sont des réponses à d'autres événements |

En version multivariée (achats, ventes, hausses et baisses du prix), les
noyaux croisés décrivent qui déclenche quoi : c'est une écriture en temps
continu du propagateur du §5.3 (Bacry, Mastromatteo & Muzy 2015). Sur
données, on trouve $n$ proche de 1 : l'essentiel de l'activité répond à
l'activité (Filimonov & Sornette 2012 ; la valeur dépend beaucoup du noyau
choisi et de la fenêtre, ce qui est une limite connue).

Simulation par l'algorithme d'amincissement d'Ogata (1981) ; estimation par
maximum de vraisemblance ; validation par le **théorème de changement de
temps** : les durées $\int_{t_i}^{t_{i+1}} \lambda(s)\,ds$ doivent être des
exponentielles de paramètre 1, indépendantes.

### 6.4 Ce qu'on choisit, et pourquoi

| Besoin | Modèle |
|---|---|
| Tester le moteur et le backtest, sans données | intelligence zéro |
| Flux réaliste (grappes, mémoire) | Hawkes |
| Tactique d'exécution, position dans la file, gros tick | queue-reactive |
| Vérité connue pour valider un estimateur | Roll, Kyle, Glosten-Milgrom simulés |

---

## 7. Mesurer à haute fréquence

### 7.1 Volatilité et bruit de microstructure

Le prix observé est le prix efficient plus un bruit (rebond bid-ask,
arrondi au tick) : $p_{t_i} = m_{t_i} + \eta_i$, avec
$\mathbb{E}[\eta^2] = \omega^2$. La variance réalisée sur $n$ observations
vaut alors, en espérance,

$$\mathbb{E}[RV_n] \approx \int_0^T \sigma_t^2\,dt + 2n\,\omega^2$$

Elle **diverge** quand on échantillonne plus vite : c'est le graphique de
signature du §3. Trois parades :

- échantillonner moins souvent (toutes les cinq minutes, par convention) et
  perdre presque toutes les données ;
- la variance réalisée à **deux échelles** (Zhang, Mykland & Aït-Sahalia
  2005) : moyenne des variances réalisées sur des sous-grilles lentes, moins
  $\bar n / n$ fois la variance réalisée rapide, ce qui retire le biais ;
- les **noyaux réalisés** (Barndorff-Nielsen, Hansen, Lunde & Shephard 2008).

Le terme $2n\omega^2$ est aussi un estimateur du bruit, donc de la
fourchette.

### 7.2 Signer les transactions

Quand le flux ne donne pas le côté agresseur, on l'infère (Lee & Ready
1991) : une transaction au-dessus du prix milieu est un achat, en dessous
une vente ; au milieu, on prend le signe de la dernière variation de prix
(*tick test*). L'erreur de classement n'est pas négligeable, et elle biaise
vers zéro tout ce qui se calcule avec les signes. Les flux des bourses de
cryptomonnaies donnent le côté agresseur : on peut y **mesurer** l'erreur de
l'algorithme, ce qui est un bon premier exercice.

### 7.3 Mesures de liquidité

Fourchette cotée, effective et réalisée (§4.5) ; profondeur à $k$ ticks ;
lambda de Kyle par régression ; mesure d'illiquidité d'Amihud (2002),
$|r_t| / \text{volume}_t$, quand on n'a que des données quotidiennes.

### 7.4 Une mesure à manier avec prudence

Le VPIN (Easley, López de Prado & O'Hara 2012) prétend mesurer la toxicité du
flux par le déséquilibre de volume classé en masse. Sa capacité prédictive
est contestée (Andersen & Bondarenko 2014 †). On le cite parce qu'il revient
en entretien ; on ne l'affiche pas sans ce débat.

---

## 8. Exécution optimale

### 8.1 Le problème

Vendre $X$ titres avant l'instant $T$. Vendre vite coûte de l'impact ; vendre
lentement expose au mouvement du prix. La mesure est le **déficit
d'exécution** (*implementation shortfall*, Perold 1988) : l'écart entre la
valeur du portefeuille « sur le papier », au prix de la décision, et ce qui a
été réellement encaissé.

### 8.2 Almgren et Chriss

Le prix subit un impact permanent linéaire dans la vitesse d'exécution
$v_t = -\dot x_t$ et un impact temporaire $\eta\,v_t$ ; on minimise
$\mathbb{E}[\text{coût}] + \lambda\,\text{Var}[\text{coût}]$ (Almgren & Chriss
2000 ; ici $\lambda$ est l'aversion au risque). En temps continu, la
quantité restante optimale est

$$x(t) = X\,\frac{\sinh\big(\kappa\,(T - t)\big)}{\sinh(\kappa\,T)}, \qquad \kappa = \sqrt{\frac{\lambda\,\sigma^2}{\eta}}$$

- $\lambda \to 0$ : $x(t) = X(1 - t/T)$, vitesse constante. C'est le **TWAP**,
  qui minimise le coût attendu.
- $\lambda$ grand : on exécute d'autant plus vite au début. $1/\kappa$ est le
  temps caractéristique de l'ordre.
- L'impact permanent linéaire coûte $\tfrac12 \gamma X^2$ quelle que soit la
  trajectoire : il ne change pas la solution.
- En faisant varier $\lambda$ on trace une **frontière efficiente** coût
  attendu contre écart-type, comme chez Markowitz.

La trajectoire est déterministe : elle ne réagit pas au prix. C'est optimal
dans ce cadre (moyenne-variance, marche aléatoire arithmétique), pas en
général.

### 8.3 Au-delà

- **Impact transitoire** : chez Obizhaeva & Wang (2013) le carnet se
  reconstitue à vitesse finie ; la solution a un bloc au début, un bloc à la
  fin, et une vitesse constante entre les deux.
- **Impact non linéaire** : avec un impact temporaire en puissance, la
  trajectoire change de forme (Almgren 2003 †).
- **Cibles usuelles** : VWAP (suivre le profil de volume du marché), POV
  (participer à une fraction fixe du volume), clôture.
- **Deux étages** : l'ordonnancement (combien par tranche, §8.2) et la
  **tactique** (dans la tranche : ordre limité ou au marché, à quel prix,
  quand annuler), qui dépend de l'état du carnet et relève du §6.2.

### 8.4 Analyse des coûts de transaction

Après coup, on décompose le déficit d'exécution : retard entre décision et
premier ordre, fourchette payée, impact, coût d'opportunité de la partie non
exécutée, frais. On compare à des références (prix d'arrivée, VWAP de la
période, clôture) en sachant que chacune se manipule : un algorithme jugé au
VWAP a intérêt à traiter comme le marché, pas à bien traiter.

---

## 9. Tenue de marché

### 9.1 Avellaneda et Stoikov

Un teneur de marché cote un bid à $m_t - \delta^b$ et un ask à
$m_t + \delta^a$. Ses ordres sont exécutés à des intensités qui décroissent
avec la distance au prix milieu, $\Lambda(\delta) = A\,e^{-k\delta}$. Il
maximise une utilité exponentielle d'aversion $\gamma$ de sa richesse finale
en $T$ (Avellaneda & Stoikov 2008). La solution approchée se lit en deux
temps.

**Prix de réservation** : le prix auquel il est indifférent à son inventaire
$q$.

$$r_t = m_t - q\,\gamma\,\sigma^2\,(T - t)$$

**Fourchette totale** autour de ce prix :

$$\delta^a + \delta^b = \gamma\,\sigma^2\,(T - t) + \frac{2}{\gamma}\,\ln\!\left(1 + \frac{\gamma}{k}\right)$$

Le premier résultat est le mécanisme du §4.4, chiffré : long, il baisse ses
deux prix pour vendre plus et acheter moins. Le second sépare un terme de
risque et un terme de concurrence ($k$ grand : les ordres loin du milieu ne
sont jamais exécutés, il faut coter serré).

Limites du modèle : l'horizon $T$ fait tendre le décalage vers zéro en fin de
séance ; l'inventaire n'est pas borné ; le prix milieu est une martingale,
donc **aucune sélection adverse**.

### 9.2 Guéant, Lehalle et Fernandez-Tapia

Avec une borne sur l'inventaire, l'équation de Hamilton-Jacobi-Bellman se
ramène à un système linéaire d'équations différentielles, et il existe une
solution asymptotique fermée, indépendante de $T$ (Guéant, Lehalle &
Fernandez-Tapia 2013). C'est la version utilisée en pratique : des cotations
stationnaires, fonctions du seul inventaire. La formule est à reprendre dans
l'article au moment de la coder.

### 9.3 Ce que le modèle ne dit pas

- **Sélection adverse** : un ordre limité est exécuté plus souvent quand le
  prix va le traverser. Le P&L d'un teneur de marché se décompose en
  **fourchette encaissée**, **P&L d'inventaire** et **sélection adverse**
  (les markouts du §4.5, à plusieurs horizons).
- **Position dans la file** : sur un actif à gros tick, coter au meilleur
  prix ne suffit pas, il faut être devant.
- **Signaux** : un teneur de marché réel décale son prix de réservation avec
  le déséquilibre du carnet (§3), pas seulement avec son inventaire.
- **Frais et remises**, qui déterminent souvent le signe du résultat.

---

## 10. Backtester au niveau du carnet

Rejouer un historique de carnet n'est **pas** simuler un marché : les autres
participants ne réagissent pas à nos ordres. Tout le soin d'un backtest de
microstructure consiste à borner cette erreur.

| Hypothèse | Version naïve | Version défendable |
|---|---|---|
| Exécution d'un ordre limité | exécuté dès que le prix touche son niveau | exécuté quand le volume traité à ce prix a épuisé la file **devant** lui |
| Position dans la file | en tête | en queue à l'arrivée ; avance avec les exécutions, et avec une fraction des annulations (hypothèse à déclarer en L2, connue en L3) |
| Latence | nulle | délai entre l'événement de marché, la décision et l'arrivée de l'ordre ; le carnet a changé entre-temps |
| Impact de nos ordres | nul | ordres au marché : consommer le carnet affiché, puis déclarer ce qu'on suppose de sa reconstitution |
| Frais | nuls | grille de la place, par côté |

Deux contrôles obligatoires, comme pour un backtest de stratégie :

- **sensibilité à la latence** : si le résultat s'effondre avec quelques
  millisecondes de plus, la stratégie est une course de vitesse que ce
  serveur ne gagnera pas ;
- **sensibilité à l'hypothèse de file** : le résultat avec « en queue, les
  annulations sont toutes derrière nous » (pessimiste) et avec « réparties au
  prorata » (central).

Le remède au premier défaut (pas de réaction du marché) est de faire tourner
la même stratégie dans un **simulateur** du §6, où le flux réagit. Les deux
résultats se lisent ensemble : le rejeu a les vraies données et pas de
réaction, le simulateur a la réaction et pas les vraies données.

---

## 11. Architecture

Une **lib C++20 autonome**, exposée à Python par un wheel pybind11 pour
l'analyse et les graphiques. Les décisions et leurs alternatives sont dans
[`blueprint/decisions.md`](../blueprint/decisions.md).

```
include/quantMicrostructure/
  core/        prix en ticks entiers, horodatage en nanosecondes, types d'ordre et d'événement
  feed/        format d'événement normalisé, lecture d'un enregistrement, détection de trous
  book/        carnet L2 et L3, reconstruction à partir d'un flux
  matching/    moteur d'appariement prix-temps
  measures/    fourchettes, profondeur, déséquilibre, variance réalisée, signature, signe des transactions
  sim/         intelligence zéro, Hawkes, queue-reactive, modèles jouets (Roll, Kyle, Glosten-Milgrom)
  replay/      backtest au niveau du carnet : latence, file, exécutions
  impact/      fonction de réponse, propagateur, lambda de Kyle
  execution/   Almgren-Chriss, TWAP, VWAP, POV, analyse des coûts
  mm/          Avellaneda-Stoikov, Guéant-Lehalle-Fernandez-Tapia, markouts
src/           implémentations, même découpage
bindings/python/  pybind11 → wheel `quantmicrostructure`
tests/         GoogleTest, tests de propriété du §14
benchmarks/    google-benchmark : débit et latence du carnet et du moteur
```

Règles d'architecture :

- **Les prix sont des entiers** (un nombre de ticks), les quantités aussi (un
  nombre de lots), le temps est en nanosecondes entières. Aucun `double` dans
  `core/`, `book/` ni `matching/` : une comparaison de prix est exacte.
- **Tout est un flux d'événements.** Un enregistrement de marché, la sortie
  d'un simulateur et la sortie du moteur d'appariement ont le **même format**.
  Tout ce qui lit un flux fonctionne donc sur les trois, et le moteur peut
  relire ce qu'il a émis.
- **La stratégie est séparée du lieu d'exécution** : la même stratégie tourne
  sur un rejeu et dans un simulateur, sans changement.
- **Le moteur ne lit aucun réseau.** La capture des flux est ailleurs (§15) ;
  ici on lit des fichiers d'événements.
- **Déterminisme** : même flux et même graine, mêmes sorties au bit près.
- **La performance se mesure.** Le carnet et le moteur ont un benchmark (débit
  en événements par seconde, latence par événement aux centiles 50, 99 et
  99,9), tenu à jour dans le dépôt. Aucune affirmation de vitesse sans son
  chiffre.
- **Un modèle déclare son régime** (gros tick ou petit tick, §2.3) et ce qu'il
  suppose du flux.

---

## 12. Ce que ce dépôt n'est pas

- **Pas un système de trading.** Aucune connexion à une place pour passer un
  ordre, aucune clé d'API de trading, nulle part. C'est un dépôt d'étude.
- **Pas une course à la latence.** Le serveur est chez un particulier, derrière
  une connexion grand public. On mesure la latence du code ; on ne prétend pas
  rivaliser avec une machine en colocation.
- **Pas d'apprentissage par renforcement** ni de réseau de neurones sur le
  carnet, tant que les modèles classiques des §6 à §9 ne sont pas faits et
  testés.
- Hors périmètre : enchères, ordres cachés, appariement au prorata,
  fragmentation et routage entre places.

---

## 13. Les lots

Dans l'ordre. M1 ne bloque rien d'ici : tout se teste d'abord sur des flux
simulés, comme le backtest de couverture de `quant-backtest` commence en
monde simulé.

| Lot | Dépôt | Contenu | Critère d'acceptation |
|---|---|---|---|
| **M0 — Socle** | ici | CMake, presets, vcpkg, GoogleTest, google-benchmark, clang-format 18, pre-commit, CI, squelette du wheel. Même chaîne que `quant-backtest` | `scripts/make.sh` construit et passe un test ; le wheel s'importe ; la CI est verte |
| **M1 — Capture** | `data-ingest` | Enregistrement continu du carnet et des transactions d'une ou deux paires sur une bourse de cryptomonnaies (flux public, sans compte) ; format de stockage et rétention à décider (ADR-004) ; contrôles : trous de séquence, carnet croisé, horloge | Une journée complète rejouée reconstruit à chaque instantané le carnet publié par la bourse. Le taux de trous est mesuré et rapporté |
| **M2 — Carnet et moteur d'appariement** | ici | `core/`, `feed/`, `book/`, `matching/` : carnet L3, priorité prix-temps, ordres limités, au marché, annulations ; reconstruction L2 ; premier benchmark | Tests de propriété du §14 (conservation, jamais croisé, priorité, aller-retour du flux). Benchmark publié |
| **M3 — Mesures et faits stylisés** | ici | `measures/` : les mesures du §4.5 et du §7, autocorrélation des signes, fonction de réponse, graphique de signature, déséquilibre et prochain mouvement | Sur un modèle de Roll simulé, l'estimateur retrouve la fourchette. Sur un brownien sans bruit le graphique de signature est plat ; avec bruit, le biais mesuré vaut $2n\omega^2$ |
| **M4 — Simulateurs de flux** | ici | `sim/` : intelligence zéro, Hawkes (amincissement d'Ogata, maximum de vraisemblance), queue-reactive, modèles jouets | Hawkes : intensité moyenne simulée égale à $\mu/(1-n)$ ; le maximum de vraisemblance retrouve les paramètres ; les durées changées de temps passent un test d'exponentialité |
| **M5 — Backtest au niveau du carnet** | ici | `replay/` : stratégie, latence, position dans la file, exécutions, frais ; même interface sur rejeu et sur simulateur | Latence nulle et stratégie passive qui ne cote jamais : aucune exécution, P&L nul. Un ordre en queue de file n'est exécuté qu'après le volume qui le précède |
| **M6 — Impact** | ici | `impact/` : fonction de réponse, ajustement du propagateur, lambda de Kyle ; loi en racine carrée vérifiée sur des métaordres injectés dans un simulateur | Kyle simulé : la régression retrouve $\sigma_v / (2\sigma_u)$. Propagateur : la relation $\beta = (1-\gamma)/2$ est retrouvée sur un flux simulé de mémoire connue |
| **M7 — Exécution** | ici | `execution/` : Almgren-Chriss, TWAP, VWAP, POV, déficit d'exécution et sa décomposition ; frontière efficiente | Aversion nulle : la trajectoire est linéaire. Le coût simulé retrouve la formule fermée. La frontière est convexe et décroissante |
| **M8 — Tenue de marché** | ici | `mm/` : Avellaneda-Stoikov puis Guéant-Lehalle-Fernandez-Tapia ; décomposition du P&L ; markouts ; dans un simulateur puis sur rejeu | Inventaire nul : cotations symétriques. Le décalage a le signe opposé à l'inventaire. Contre une stratégie symétrique autour du prix milieu, l'écart-type du P&L et celui de l'inventaire final sont plus faibles (le résultat de l'article) |

Suite possible, non spécifiée : calibrer sur ces mesures les modèles de coût
de `quant-backtest` ; une page sur le site de `quant-modeling`.

---

## 14. Tests de propriété

- **Conservation** : le volume acheté égale le volume vendu ; la somme des
  quantités en attente, exécutées et annulées d'un ordre égale sa quantité
  initiale.
- **Jamais croisé** : après chaque événement traité, meilleur bid < meilleur
  ask.
- **Priorité prix-temps** : un ordre n'est jamais exécuté tant qu'un ordre à
  meilleur prix, ou au même prix et plus ancien, attend.
- **Aller-retour du flux** : le carnet reconstruit à partir du flux émis par
  le moteur est identique à celui du moteur, à chaque événement.
- **L3 vers L2** : agréger le carnet L3 par niveau donne le carnet L2
  reconstruit à partir du flux agrégé.
- **Déterminisme** : même flux, même graine, mêmes sorties.
- **Pas de futur** : dans `replay/`, modifier le flux après $t$ ne change
  aucune décision ni aucune exécution jusqu'à $t$.
- **Modèles à vérité connue** : Roll, Kyle et Glosten-Milgrom simulés
  redonnent leurs formules fermées (fourchette, lambda, $\pi(V_H - V_L)$).
- **Hawkes** : intensité moyenne, paramètres retrouvés, changement de temps.
- **Graphique de signature** : plat sans bruit, pente $2\omega^2$ en $n$ avec
  bruit ; la variance à deux échelles retire le biais.
- **Almgren-Chriss** : limite TWAP, coût fermé, monotonie en aversion.
- **Avellaneda-Stoikov** : symétrie à inventaire nul, signe du décalage,
  inventaire qui revient vers zéro.
- **Absence d'arbitrage** : un aller-retour dans un modèle d'impact du dépôt
  ne gagne pas d'argent en moyenne.

---

## 15. Données et limites assumées

Rien de ce qui suit n'a été mesuré : c'est l'état des connaissances au
11 octobre 2026, à confirmer au lot M1.

| Besoin | Ce qu'on a | Ce qui manque | Décision |
|---|---|---|---|
| Carnet et transactions | rien : la base de `data-ingest` ne contient que des données quotidiennes | tout | Capturer le flux public d'une bourse de cryptomonnaies : gratuit, sans compte, ouvert en continu, avec le côté agresseur des transactions. La disponibilité d'un flux L3 dépend de la place : à vérifier dans sa documentation |
| Carnet sur actions | rien | les flux des places actions sont payants (LOBSTER pour la recherche, Databento à l'usage) | Pas d'achat tant que les lots M2 à M5 ne tournent pas sur données capturées. Les prix sont à relever au moment de décider |
| Historique | rien | un flux capturé ne remonte pas dans le passé : l'historique s'accumule à partir du jour où la capture démarre | Démarrer M1 tôt, même avant d'en avoir besoin, et ne pas l'interrompre |
| Volume de stockage | inconnu | une mesure sur une journée | Mesurer avant de choisir le format (ADR-004) |
| Métaordres | rien, et il n'en existe pas de publics | début et fin de gros ordres | La loi en racine carrée ne se vérifie qu'en simulateur (§5.4) |
| Horloge | celle du serveur | une synchronisation meilleure que NTP | Garder l'horodatage de la bourse **et** celui de réception ; ne rien conclure sous la milliseconde |

Limites à écrire partout où un résultat est montré : un marché de
cryptomonnaies n'est pas un marché actions (pas d'enchère, pas de teneur de
marché désigné, frais et pas de cotation différents, flux retail important) ;
ses faits stylisés sont proches, pas identiques. Et un résultat de tenue de
marché obtenu depuis ce serveur ne dit rien de ce qu'obtiendrait un
participant en colocation.

---

## 16. Aide-mémoire

| À savoir | Valeur |
|---|---|
| Fourchette de Roll | $2\sqrt{-\text{Cov}(\Delta p_t, \Delta p_{t-1})}$ |
| Fourchette de Glosten-Milgrom | $\pi\,(V_H - V_L)$ |
| Lambda de Kyle | $\sigma_v / (2\sigma_u)$ ; l'initié révèle la moitié de son information |
| Décomposition | fourchette effective = réalisée + impact |
| Mémoire du flux et propagateur | signes en $\ell^{-\gamma}$, impact en $\ell^{-\beta}$, $\beta = (1-\gamma)/2$ |
| Loi en racine carrée | $\Delta p / p \approx Y \sigma_d \sqrt{Q/V}$ ; coût total en $Q^{3/2}$ |
| Hawkes | $n = \alpha/\beta < 1$ ; intensité moyenne $\mu/(1-n)$ |
| Bruit de microstructure | $\mathbb{E}[RV_n] \approx$ variance intégrée $+\ 2n\omega^2$ |
| Almgren-Chriss | $x(t) = X \sinh(\kappa(T-t))/\sinh(\kappa T)$, $\kappa = \sqrt{\lambda\sigma^2/\eta}$ |
| Avellaneda-Stoikov, prix de réservation | $m - q\gamma\sigma^2(T-t)$ |
| Avellaneda-Stoikov, fourchette | $\gamma\sigma^2(T-t) + \tfrac{2}{\gamma}\ln(1 + \gamma/k)$ |

---

## 17. Bibliographie

**Livres**

| Référence | Section |
|---|---|
| Bouchaud, Bonart, Donier & Gould, *Trades, Quotes and Prices: Financial Markets Under the Microscope*, Cambridge University Press, 2018 | §2 à §6 |
| Cartea, Jaimungal & Penalva, *Algorithmic and High-Frequency Trading*, Cambridge University Press, 2015 | §8, §9 |
| Guéant, *The Financial Mathematics of Market Liquidity*, Chapman & Hall / CRC, 2016 | §8, §9 |
| Hasbrouck, *Empirical Market Microstructure*, Oxford University Press, 2007 | §4, §7 |
| O'Hara, *Market Microstructure Theory*, Blackwell, 1995 | §4 |
| Harris, *Trading and Exchanges: Market Microstructure for Practitioners*, Oxford University Press, 2003 | §2 |
| Lehalle & Laruelle (dir.), *Market Microstructure in Practice*, 2ᵉ éd., World Scientific, 2018 | §2, §8 |

**Fourchette et information**

| Référence | Section |
|---|---|
| Roll, « A Simple Implicit Measure of the Effective Bid-Ask Spread in an Efficient Market », *Journal of Finance* 39(4), 1984 | §4.1 |
| Glosten & Milgrom, « Bid, Ask and Transaction Prices in a Specialist Market with Heterogeneously Informed Traders », *Journal of Financial Economics* 14(1), 1985 | §4.2 |
| Kyle, « Continuous Auctions and Insider Trading », *Econometrica* 53(6), 1985 | §4.3 |
| Ho & Stoll, « Optimal Dealer Pricing Under Transactions and Return Uncertainty », *Journal of Financial Economics* 9(1), 1981 | §4.4 |
| Amihud, « Illiquidity and Stock Returns: Cross-Section and Time-Series Effects », *Journal of Financial Markets* 5(1), 2002 | §7.3 |

**Flux, impact, carnet**

| Référence | Section |
|---|---|
| Lillo & Farmer, « The Long Memory of the Efficient Market », *Studies in Nonlinear Dynamics & Econometrics* 8(3), 2004 | §3 |
| Bouchaud, Gefen, Potters & Wyart, « Fluctuations and Response in Financial Markets: The Subtle Nature of "Random" Price Changes », *Quantitative Finance* 4(2), 2004 | §3, §5.2, §5.3 |
| Tóth et al., « Anomalous Price Impact and the Critical Nature of Liquidity in Financial Markets », *Physical Review X* 1, 2011 | §5.4 |
| Almgren, Thum, Hauptmann & Li, « Direct Estimation of Equity Market Impact », *Risk*, 2005 | §5.4 |
| Gatheral, « No-Dynamic-Arbitrage and Market Impact », *Quantitative Finance* 10(7), 2010 | §5.5 |
| Cont, Kukanov & Stoikov, « The Price Impact of Order Book Events », *Journal of Financial Econometrics* 12(1), 2014 | §3 |
| Stoikov, « The Micro-Price: A High-Frequency Estimator of Future Prices », *Quantitative Finance* 18(12), 2018 | §3 |
| Epps, « Comovements in Stock Prices in the Very Short Run », *Journal of the American Statistical Association* 74(366), 1979 | §3 |
| Smith, Farmer, Gillemot & Krishnamurthy, « Statistical Theory of the Continuous Double Auction », *Quantitative Finance* 3(6), 2003 | §6.1 |
| Cont, Stoikov & Talreja, « A Stochastic Model for Order Book Dynamics », *Operations Research* 58(3), 2010 | §6.1 |
| Huang, Lehalle & Rosenbaum, « Simulating and Analyzing Order Book Data: The Queue-Reactive Model », *Journal of the American Statistical Association* 110(509), 2015 | §6.2 |
| Hawkes, « Spectra of Some Self-Exciting and Mutually Exciting Point Processes », *Biometrika* 58(1), 1971 | §6.3 |
| Ogata, « On Lewis' Simulation Method for Point Processes », *IEEE Transactions on Information Theory* 27(1), 1981 | §6.3 |
| Bacry, Mastromatteo & Muzy, « Hawkes Processes in Finance », *Market Microstructure and Liquidity* 1(1), 2015 | §6.3 |
| Filimonov & Sornette, « Quantifying Reflexivity in Financial Markets: Toward a Prediction of Flash Crashes », *Physical Review E* 85, 2012 | §6.3 |

**Mesure à haute fréquence**

| Référence | Section |
|---|---|
| Zhang, Mykland & Aït-Sahalia, « A Tale of Two Time Scales: Determining Integrated Volatility with Noisy High-Frequency Data », *Journal of the American Statistical Association* 100(472), 2005 | §7.1 |
| Barndorff-Nielsen, Hansen, Lunde & Shephard, « Designing Realized Kernels to Measure the ex post Variation of Equity Prices in the Presence of Noise », *Econometrica* 76(6), 2008 | §7.1 |
| Lee & Ready, « Inferring Trade Direction from Intraday Data », *Journal of Finance* 46(2), 1991 | §7.2 |
| Easley, López de Prado & O'Hara, « Flow Toxicity and Liquidity in a High-Frequency World », *Review of Financial Studies* 25(5), 2012 | §7.4 |
| Andersen & Bondarenko, « VPIN and the Flash Crash », *Journal of Financial Markets* 17, 2014 † | §7.4 |

**Exécution et tenue de marché**

| Référence | Section |
|---|---|
| Perold, « The Implementation Shortfall: Paper Versus Reality », *Journal of Portfolio Management* 14(3), 1988 | §8.1 |
| Almgren & Chriss, « Optimal Execution of Portfolio Transactions », *Journal of Risk* 3(2), 2000 | §8.2 |
| Almgren, « Optimal Execution with Nonlinear Impact Functions and Trading-Enhanced Risk », *Applied Mathematical Finance* 10(1), 2003 † | §8.3 |
| Obizhaeva & Wang, « Optimal Trading Strategy and Supply/Demand Dynamics », *Journal of Financial Markets* 16(1), 2013 | §8.3 |
| Avellaneda & Stoikov, « High-Frequency Trading in a Limit Order Book », *Quantitative Finance* 8(3), 2008 | §9.1 |
| Guéant, Lehalle & Fernandez-Tapia, « Dealing with the Inventory Risk: A Solution to the Market Making Problem », *Mathematics and Financial Economics* 7(4), 2013 | §9.2 |

---

## 18. Questions d'entretien

1. Pourquoi y a-t-il une fourchette sur un marché où personne n'a de coût ni
   d'aversion au risque ?
2. Le flux d'ordres est fortement autocorrélé et les rendements ne le sont
   pas. Comment est-ce possible ?
3. Vous devez vendre 5 % du volume quotidien d'un titre. Combien cela
   coûte-t-il, à peu près, et comment le coût change-t-il si vous doublez la
   taille ?
4. Dans Almgren-Chriss, que devient la trajectoire quand l'aversion au risque
   tend vers zéro ? Vers l'infini ? Pourquoi la trajectoire ne dépend-elle pas
   du prix ?
5. Un teneur de marché est long de dix lots. Que fait-il de ses cotations, et
   de combien ?
6. Votre backtest de tenue de marché gagne de l'argent tous les jours. Quelles
   hypothèses vérifiez-vous en premier ?
7. Pourquoi ne mesure-t-on pas la volatilité avec toutes les transactions de
   la journée ?
8. Qu'est-ce qui change, pour une stratégie, entre un actif à gros tick et un
   actif à petit tick ?
9. Comment concevez-vous la structure de données d'un carnet d'ordres ?
   Quelles opérations doivent être rapides, et lesquelles sont les plus
   fréquentes ?
