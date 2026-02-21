# 🗜️ Guide Complet : Comprendre la Compression de Fichiers

> **Objectif** : Maîtriser les fondements théoriques et les algorithmes existants  
> **Public** : Développeur avec de bonnes bases en programmation, débutant en compression  
> **Projet** : Créer un algorithme de compression révolutionnaire

---

## 📋 Table des Matières

1. [Les Fondements Théoriques](#1-les-fondements-théoriques)
2. [Les Briques de Base (Building Blocks)](#2-les-briques-de-base)
3. [Les Algorithmes de Dictionnaire (Famille LZ)](#3-les-algorithmes-de-dictionnaire)
4. [Les Codeurs Entropiques](#4-les-codeurs-entropiques)
5. [Les Transformations Préalables](#5-les-transformations-préalables)
6. [Les Algorithmes Modernes (État de l'Art)](#6-les-algorithmes-modernes)
7. [La Frontière : IA & Compression](#7-la-frontière--ia--compression)
8. [Tableau Comparatif](#8-tableau-comparatif)
9. [Limites Théoriques & Opportunités](#9-limites-théoriques--opportunités)
10. [Pistes pour un Algo Révolutionnaire](#10-pistes-pour-un-algo-révolutionnaire)

---

## 1. Les Fondements Théoriques

### 1.1 La Théorie de l'Information de Shannon (1948)

Tout commence ici. Claude Shannon a posé les bases mathématiques de la compression avec son article fondateur *"A Mathematical Theory of Communication"*.

#### L'Entropie de Shannon

L'**entropie** mesure la quantité moyenne d'information (ou de "surprise") contenue dans une source de données. C'est la mesure fondamentale de la compressibilité.

**Formule** :
```
H(X) = -Σ p(x) × log₂(p(x))
```

Où `p(x)` est la probabilité d'apparition de chaque symbole `x`.

**Exemple concret** :
- Un texte en français qui n'utilise que les lettres `A` (90%) et `B` (10%) :
  - `H = -(0.9 × log₂(0.9) + 0.1 × log₂(0.1))`
  - `H ≈ 0.469 bits par caractère`
  - Au lieu de 8 bits/caractère (ASCII), on peut théoriquement descendre à ~0.47 bits !

#### Le Théorème du Codage Source

> **Limite fondamentale** : Il est mathématiquement **impossible** de compresser des données sans perte en dessous de leur entropie de Shannon.

C'est la "barrière physique" de la compression lossless. Tout algorithme, aussi brillant soit-il, ne peut pas aller en dessous. **Mais** — et c'est crucial — la plupart des algorithmes existants sont encore **loin** de cette limite optimale pour beaucoup de types de données.

### 1.2 Compression Lossless vs Lossy

| Aspect | Lossless (Sans Perte) | Lossy (Avec Perte) |
|--------|----------------------|-------------------|
| **Principe** | Reconstruction parfaite des données originales | Accepte une perte contrôlée d'information |
| **Ratio typique** | 2:1 à 10:1 | 10:1 à 1000:1 |
| **Usage** | Texte, code, données critiques, archives | Images (JPEG), audio (MP3), vidéo (H.265) |
| **Limite** | Entropie de Shannon | Théorie Rate-Distortion |

### 1.3 Types de Redondance dans les Données

La compression exploite la **redondance**. Il existe trois types principaux :

1. **Redondance de caractères** : Certains symboles apparaissent plus souvent que d'autres
   - Ex : En français, 'e' est ~17% tandis que 'z' est ~0.1%
   
2. **Redondance de séquences** : Des motifs se répètent
   - Ex : "the" apparaît des milliers de fois dans un texte anglais
   
3. **Redondance structurelle** : Des patterns de haut niveau existent
   - Ex : En XML, les balises ouvrantes/fermantes sont prévisibles

---

## 2. Les Briques de Base

### 2.1 Run-Length Encoding (RLE)

Le plus simple des algorithmes. Remplace les séquences de caractères identiques par un compteur.

```
Entrée  : AAAAAABBBCCDDDDD
Sortie  : 6A3B2C5D
```

**Taux de compression** : Excellent pour les données avec beaucoup de répétitions consécutives (ex : images bitmap monochromes). Terrible pour du texte normal.

**Utilisé dans** : BMP, TIFF, fax (Group 3/4)

### 2.2 Codage de Huffman (1952)

Un des algorithmes les plus importants historiquement. Assigne des codes de longueur variable aux symboles en fonction de leur fréquence.

**Principe** :
1. Compter la fréquence de chaque symbole
2. Construire un arbre binaire bottom-up (les symboles les moins fréquents en bas)
3. Parcourir l'arbre pour obtenir les codes

```
Symbole | Fréquence | Code Huffman
--------|-----------|-------------
  'e'   |    45%    |    0
  'a'   |    25%    |    10
  't'   |    15%    |    110
  'r'   |    10%    |    1110
  'z'   |     5%    |    1111
```

- 'e' (le plus fréquent) → 1 bit au lieu de 8
- 'z' (le plus rare) → 4 bits au lieu de 8

**Limitation clé** : Un symbole = au minimum 1 bit. On ne peut pas encoder un symbole très fréquent (ex : 99% de probabilité) à moins de 1 bit. C'est là que le codage arithmétique surpasse Huffman.

**Utilisé dans** : ZIP (partie de DEFLATE), JPEG, MP3, PNG

### 2.3 Move-to-Front (MTF)

Transformation qui exploite la localité temporelle des symboles.

**Principe** :
1. Maintenir une liste ordonnée de tous les symboles
2. Pour chaque symbole rencontré, émettre sa position dans la liste
3. Déplacer ce symbole en tête de liste

Après la BWT (voir section 5), les symboles identiques sont regroupés, et MTF les transforme en longues séquences de petits nombres (0, 1, 2...), très faciles à compresser ensuite.

---

## 3. Les Algorithmes de Dictionnaire (Famille LZ)

La famille Lempel-Ziv est la colonne vertébrale de la compression moderne. L'idée : remplacer les séquences répétées par des références à un "dictionnaire".

### 3.1 LZ77 (1977) — Fenêtre Glissante

**Concept** : Chercher dans les données récemment vues (fenêtre glissante) les correspondances avec les données à venir.

```
┌──────────────────────┬──────────────┐
│   Fenêtre Glissante  │  Buffer de   │
│   (données passées)  │  Prédiction  │
│   "search buffer"    │ "look-ahead" │
└──────────────────────┴──────────────┘
         ← 32KB →         ← 258B →
```

**Encodage** : Triplets `(distance, longueur, prochain_caractère)`

**Exemple** :
```
Entrée : "ABCABCABC"
                     
Position 0-2 : A, B, C (littéraux, pas de match)
Position 3   : "ABC" trouvé à distance 3, longueur 3 → (3, 3, ?)
Position 6   : "ABC" trouvé à distance 3, longueur 3 → (3, 3, ?)
```

**Forces** : Simple, rapide en décompression  
**Faiblesses** : Fenêtre limitée, ne trouve que les répétitions récentes

**Utilisé dans** : DEFLATE (ZIP, gzip, PNG), LZSS, Snappy

### 3.2 LZ78 (1978) — Dictionnaire Explicite

**Concept** : Construire un dictionnaire de phrases au fur et à mesure du traitement.

**Différence avec LZ77** : Au lieu d'une fenêtre glissante, on maintient un dictionnaire explicite qui grandit progressivement.

**Encodage** : Paires `(index_dictionnaire, prochain_caractère)`

**Exemple** :
```
Entrée : "ABABABAB"

Étape 1 : "" + 'A' → Dict[1] = "A",     sortie: (0, 'A')
Étape 2 : "" + 'B' → Dict[2] = "B",     sortie: (0, 'B')
Étape 3 : "A" + 'B' → Dict[3] = "AB",   sortie: (1, 'B')
Étape 4 : "A" + 'B' → trouvé "AB", puis "AB" + 'A' → Dict[4] = "ABA", sortie: (3, 'A')
Étape 5 : "B" → trouvé, sortie: (2, EOF)
```

### 3.3 LZW (1984) — L'Évolution Pratique

**Concept** : Amélioration de LZ78. Le dictionnaire est pré-initialisé avec tous les caractères simples, et on n'envoie que des **codes** (plus de "prochain caractère").

**C'est l'algo derrière le format GIF et les early ZIP.**

### 3.4 LZMA (Lempel-Ziv-Markov chain Algorithm)

**Concept** : Combine LZ77 avec un encodeur de plage (range coder) et un modèle de Markov. C'est l'algo derrière **7-Zip** et le format `.xz`.

**Innovations** :
- Fenêtre de dictionnaire **énorme** (jusqu'à 4 Go !)
- Encodage des distances et longueurs via un codeur arithmétique adaptatif
- Modèle de contexte sophistiqué pour prédire les bits

**Performance** : Parmi les **meilleurs ratios de compression** pour un algorithme général. Mais lent.

---

## 4. Les Codeurs Entropiques

Ces algorithmes transforment une séquence de symboles avec des probabilités connues en une séquence de bits quasi-optimale.

### 4.1 Codage Arithmétique

**Le concept le plus élégant en compression.**

Au lieu d'assigner un code à chaque symbole (comme Huffman), le codage arithmétique encode le **message entier** comme un **seul nombre fractionnaire** entre 0 et 1.

**Principe** :
1. Commencer avec l'intervalle [0, 1)
2. Pour chaque symbole, subdiviser l'intervalle proportionnellement aux probabilités
3. Le sous-intervalle du symbole encodé devient le nouvel intervalle
4. Le nombre final (n'importe quel nombre dans l'intervalle final) **est** le message compressé

**Exemple avec A=60%, B=30%, C=10%** :
```
Message : "BAC"

Intervalle initial : [0.0, 1.0)
  A → [0.0, 0.6)    B → [0.6, 0.9)    C → [0.9, 1.0)

Symbole 'B' : Sélection [0.6, 0.9)
  Subdivision de [0.6, 0.9) :
  A → [0.6, 0.78)   B → [0.78, 0.87)   C → [0.87, 0.9)

Symbole 'A' : Sélection [0.6, 0.78)
  Subdivision de [0.6, 0.78) :
  A → [0.6, 0.708)  B → [0.708, 0.762)  C → [0.762, 0.78)

Symbole 'C' : Sélection [0.762, 0.78)

Message "BAC" encodé comme un nombre entre 0.762 et 0.78
→ Par exemple : 0.77 (en binaire, quelques bits suffisent !)
```

**Avantage** : Peut encoder un symbole à **moins de 1 bit** ! (impossible avec Huffman)  
**Inconvénient** : Beaucoup de multiplications/divisions → plus lent

### 4.2 ANS — Asymmetric Numeral Systems (2014)

**La révolution récente du codage entropique** par Jarosław Duda.

ANS combine :
- La compression quasi-optimale du codage arithmétique
- La vitesse du codage de Huffman

**Principe** : L'état est stocké comme un **seul entier** qui évolue au fur et à mesure de l'encodage. Contrairement au codage arithmétique qui manipule des fractions, ANS travaille avec des entiers et des tables de lookup.

**Variantes** :
- **rANS** (range ANS) : Proche du codage arithmétique, bon pour le streaming
- **tANS** (tabled ANS) : Utilise une machine à états finis, ultra-rapide

**Adoption** : Facebook Zstandard, Apple LZFSE, Google Draco, JPEG XL

> 💡 **Importance pour votre projet** : ANS est probablement le codeur entropique le plus efficace que vous devriez intégrer comme composant de base.

---

## 5. Les Transformations Préalables

Ces transformations ne compressent pas directement mais **réorganisent les données** pour rendre la compression ultérieure plus efficace.

### 5.1 Burrows-Wheeler Transform (BWT)

**Une des idées les plus brillantes en compression.**

**Principe** :
1. Prendre le texte d'entrée, ajouter un marqueur de fin
2. Générer toutes les rotations cycliques
3. Trier ces rotations lexicographiquement
4. Prendre la dernière colonne → c'est la BWT !

**Exemple** :
```
Entrée : "banana$"

Rotations triées :       Dernière colonne (BWT) :
$banana                  a
a$banan                  n
ana$ban                  n
anana$b                  b
banana$                  $
na$bana                  a
nana$ba                  a

BWT = "annb$aa"
```

**Magie** : Les caractères identiques se regroupent ! Ici, les 'a' et les 'n' sont côte à côte. Cela crée de la localité que MTF + RLE + Huffman exploitent magistralement.

**Utilisé dans** : bzip2

**La transformation est parfaitement réversible !**

### 5.2 Delta Encoding

Stocker les différences entre valeurs consécutives plutôt que les valeurs absolues.

```
Données    : [100, 102, 104, 103, 105, 108]
Delta      : [100,   2,   2,  -1,   2,   3]
```

Les deltas ont une entropie plus faible → meilleure compression.

**Utilisé pour** : Données numériques, séries temporelles, audio (FLAC)

---

## 6. Les Algorithmes Modernes (État de l'Art)

### 6.1 DEFLATE (1993)

La combinaison **LZ77 + Huffman**. C'est le standard de facto.

- **Format** : gzip, ZIP, PNG, HTTP compression
- **Ratio** : Modéré mais très rapide
- **Taille de fenêtre** : 32 KB (la limite originale)

### 6.2 Zstandard / zstd (2016, Facebook)

Le roi actuel du **compromis ratio/vitesse**.

| Niveau | Ratio Compression | Vitesse Compression | Vitesse Décompression |
|--------|-------------------|--------------------|-----------------------|
| 1      | ~2.88x            | ~515 MB/s          | ~1380 MB/s            |
| 3      | ~3.15x            | ~360 MB/s          | ~1380 MB/s            |
| 19     | ~3.70x            | ~10 MB/s           | ~1380 MB/s            |

**Innovations** :
- Fenêtre de dictionnaire jusqu'à 128 Mo
- Utilise des dictionnaires entraînés sur des données similaires
- Encodage entropique via tANS (Finite State Entropy)
- 22 niveaux de compression configurables

### 6.3 Brotli (2015, Google)

Optimisé pour le **contenu web** (HTML, CSS, JS).

**Innovations** :
- Dictionnaire statique pré-intégré de texte web courant
- LZ77 + contexte d'ordre 2 + Huffman
- Fenêtre jusqu'à 16 Mo

### 6.4 LZ4 (2011)

Le champion de la **vitesse**.

- Compression : ~780 MB/s
- Décompression : ~4 200 MB/s (!!)
- Ratio faible (~2.1x) mais décompression quasi-instantanée

**Cas d'usage** : Bases de données en mémoire, kernels, gaming

### 6.5 PAQ / ZPAQ — L'Extrême

PAQ est le champion des **benchmarks de compression**. Il pousse la compression au maximum absolu, au détriment de la vitesse.

**Technique** : **Context Mixing** — Combine les prédictions de dizaines/centaines de modèles statistiques différents.

**Comment ça marche** :
1. Maintenir N modèles de contexte (unigram, bigram, jusqu'à très longs contextes)
2. Chaque modèle prédit P(prochain bit = 1)
3. Combiner ces prédictions avec des poids adaptatifs (réseau de neurones !)
4. Encoder le bit réel avec un codeur arithmétique

**Performance** :
- Ratio de compression : **Le meilleur au monde** pour le texte
- Vitesse : ~1 à 10 KB/s (oui, **kilo**bytes par seconde !)
- Mémoire : Plusieurs GB

> 💡 PAQ nous montre que **la modélisation** est la clé de la compression. Plus votre modèle est bon pour prédire les données, meilleure sera la compression.

---

## 7. La Frontière : IA & Compression

### 7.1 LLM comme Compresseurs

**Découverte fascinante** : Un modèle de langage (GPT, etc.) EST un compresseur ! La prédiction du prochain token est exactement ce dont on a besoin pour le codage arithmétique.

**LMCompress** : Utilise des LLMs pour la compression lossless. Résultats : **2x à 4x** meilleur que les algorithmes classiques sur du texte !

**Principe** :
```
Texte : "Le chat est sur le toit"
LLM prédit : P("est") = 0.3 après "Le chat"  → ~1.7 bits
LLM prédit : P("le") = 0.4 après "sur"       → ~1.3 bits
→ Chaque mot bien prédit = très peu de bits !
```

### 7.2 Compression Neuronale d'Images

Les codecs basés sur réseaux de neurones (autoencoder + hyperprior) surpassent parfois JPEG/WebP/HEIC en qualité perceptuelle à taille égale.

### 7.3 ZipNN (IBM, 2025)

Compression lossless spécialisée pour les modèles de réseaux de neurones (poids en float16/bfloat16). Gains de 33-50%.

---

## 8. Tableau Comparatif

| Algorithme | Année | Ratio (texte) | Vitesse Comp. | Vitesse Décomp. | Mémoire | Technique |
|-----------|-------|---------------|---------------|-----------------|---------|-----------|
| gzip/DEFLATE | 1993 | ~3x | ⭐⭐⭐ | ⭐⭐⭐⭐ | Faible | LZ77 + Huffman |
| bzip2 | 1996 | ~4x | ⭐⭐ | ⭐⭐ | Moyenne | BWT + MTF + Huffman |
| LZMA/7z | 1998 | ~5x | ⭐ | ⭐⭐ | Haute | LZ77 + Range Coder |
| LZ4 | 2011 | ~2x | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | Faible | LZ77 simplifié |
| Brotli | 2015 | ~4x | ⭐⭐ | ⭐⭐⭐⭐ | Moyenne | LZ77 + Huffman + Dict |
| Zstandard | 2016 | ~3.5x | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | Moyenne | LZ77 + tANS |
| PAQ/ZPAQ | 2002+ | ~6-8x | ★ | ★ | Très haute | Context Mixing + AC |
| LMCompress | 2024 | ~10-15x | ★ | ★ | GPU req. | LLM + AC |

*★ = extrêmement lent*

---

## 9. Limites Théoriques & Opportunités

### 9.1 Ce qui est IMPOSSIBLE

1. **Compresser en dessous de l'entropie** (compression lossless)
2. **Compresser des données aléatoires** (entropie maximale = incompressible) 
3. **Algorithme universel magique** : Le "Pigeon Hole Principle" prouve qu'aucun algorithme ne peut compresser TOUS les fichiers. Si un fichier de N bits est compressé en M < N bits, alors un autre fichier de N bits sera AGRANDI.

### 9.2 Où sont les OPPORTUNITÉS

| Opportunité | Détail |
|-------------|--------|
| **Meilleur modélisation** | PAQ montre que la qualité du modèle prédictif est le facteur #1. La plupart des compresseurs n'utilisent que des modèles très simples. |
| **Connaissance du type** | Un algo qui sait qu'il compresse du JSON, du Python, du CSV, etc. peut exploiter la structure spécifique |
| **Compression par blocs adaptatifs** | Adapter la stratégie bloc par bloc (certains blocs sont du texte, d'autres du binaire, etc.) |
| **Préprocessing intelligent** | Transformations adaptées au type de données avant la compression |
| **Dictionnaires partagés** | Zstd montre que des dictionnaires entraînés améliorent drastiquement la compression de petites données similaires |
| **Parallélisme** | Beaucoup d'algos sont séquentiels. Un algo nativement parallèle pourrait être bien plus rapide |
| **Compression hiérarchique** | Compresser à plusieurs niveaux : structure ➝ patterns ➝ bits |
| **Hybride IA + Traditionnel** | Utiliser un modèle IA pour la prédiction + ANS pour l'encodage |

### 9.3 Le "Sweet Spot" Non Exploité

```
                    ┌─────────────┐
                    │  LMCompress │  ← Excellent ratio, trop lent
                    │   (LLM+AC)  │
                    └─────────────┘
                         ↕
    ┌────────────────────────────────────────┐
    │          🎯 ZONE D'OPPORTUNITÉ 🎯      │
    │                                        │
    │   Ratio PAQ + Vitesse Zstd             │
    │   = Le Graal de la compression          │
    │                                        │
    └────────────────────────────────────────┘
                         ↕
    ┌─────────┐   ┌─────────┐   ┌─────────┐
    │  Zstd   │   │  Brotli │   │  LZMA   │  ← Bon ratio, rapide
    └─────────┘   └─────────┘   └─────────┘
```

---

## 10. Pistes pour un Algo Révolutionnaire

### Piste 1 : Context Mixing Accéléré par Hardware

PAQ a les meilleurs ratios mais est terriblement lent. Pourquoi ne pas :
- Simplifier le context mixing (moins de modèles, mais mieux choisis)
- Utiliser des instructions SIMD (AVX-512) pour paralléliser les calculs
- Implémenter les tables de prédiction en cache L1/L2

### Piste 2 : Compression Consciente du Type

```
Fichier d'entrée
    │
    ▼
[Détecteur de type automatique]
    │
    ├── Texte naturel → BWT + Context Mixing léger + ANS
    ├── Code source → Parser AST + Delta Encoding + ANS
    ├── JSON/XML → Schema extraction + Données séparées
    ├── Images → Prédiction spatiale + ANS
    ├── Binaire structuré → Analyse de colonnes + Delta + ANS
    └── Aléatoire → Stockage brut (pas de compression)
```

### Piste 3 : Petit Réseau de Neurones Embarqué

Au lieu d'un LLM énorme, entraîner un **petit réseau de neurones** (~1 Mo) qui fait de la prédiction de bytes :
- Assez petit pour tourner sans GPU
- Assez puissant pour battre les modèles statistiques classiques
- Utiliser ANS comme codeur entropique final

### Piste 4 : Compression Récursive Multi-Niveaux

```
Niveau 1 : Détection de structure macro (headers, blocs, patterns)
Niveau 2 : Compression de la structure elle-même
Niveau 3 : Pour chaque bloc, compression optimale selon le type
Niveau 4 : Codage entropique final (ANS)
```

### Piste 5 : Dictionnaires Universels Pré-Entraînés

Créer des dictionnaires de compression pré-entraînés pour les types de fichiers courants :
- Un dictionnaire pour HTML/CSS/JS
- Un dictionnaire pour Python/Java/C
- Un dictionnaire pour JSON
- etc.

Ces dictionnaires seraient partagés et ne compteraient pas dans la taille du fichier compressé.

---

## 📚 Ressources Pour Aller Plus Loin

### Livres
- *"Introduction to Data Compression"* — Khalid Sayood
- *"Managing Gigabytes"* — Witten, Moffat, Bell
- *"Elements of Information Theory"* — Cover & Thomas

### Benchmarks
- [Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/)
- [Large Text Compression Benchmark](http://mattmahoney.net/dc/text.html) (Matt Mahoney)
- [Hutter Prize](http://prize.hutter1.net/) (1 Go de Wikipedia → le moins de bits possible)

### Papers Clés
- Shannon 1948 : "A Mathematical Theory of Communication"
- Ziv & Lempel 1977 : "A Universal Algorithm for Sequential Data Compression"
- Duda 2014 : "Asymmetric Numeral Systems" (ANS)
- Burrows & Wheeler 1994 : "A Block-sorting Lossless Data Compression Algorithm"

### Implémentations
- [zstd (GitHub)](https://github.com/facebook/zstd) — Le standard moderne
- [PAQ (GitHub)](https://github.com/mattmahoney/paq) — Le meilleur ratio
- [lz4 (GitHub)](https://github.com/lz4/lz4) — Le plus rapide

---

## 🚀 Prochaines Étapes

1. **Phase 1** : Implémenter les briques de base (Huffman, LZ77, RLE) en C/C++ pour comprendre intimement
2. **Phase 2** : Implémenter un codeur arithmétique et ANS
3. **Phase 3** : Implémenter la BWT + MTF
4. **Phase 4** : Créer un système de pipeline combinant ces éléments
5. **Phase 5** : Ajouter la couche d'intelligence (détection de type, modèle prédictif)
6. **Phase 6** : Optimiser (SIMD, multithreading, profiling)
7. **Phase 7** : Benchmarker contre zstd, brotli, LZMA, PAQ

> *"La compression, c'est la prédiction. Si vous pouvez prédire les données parfaitement, vous pouvez les compresser parfaitement."*

---

*Document créé le 19 février 2026 — Projet MaxCompression*
